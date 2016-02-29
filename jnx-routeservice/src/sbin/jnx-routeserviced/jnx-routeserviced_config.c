/*
 * $Id: jnx-routeserviced_config.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file 
 *     jnx-routeserviced_config.c
 * @brief 
 *     Configurations and patricia tree related routines 
 *
 * This file contains the routines to read/check configuration database,
 * store the configuration in a local data structure (patricia tree) and
 * eventually call the routines to add routes via SSD.
 */


#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include <isc/eventlib.h>

#include <ddl/dax.h>

#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include <jnx/ssd_ipc.h>
#include <jnx/ssd_ipc_msg.h>
#include <jnx/junos_trace.h>
#include <jnx/junos_kcom.h>

#include JNX_ROUTESERVICED_OUT_H

#include "jnx-routeserviced_config.h"
#include "jnx-routeserviced_ssd.h"


/*
 * Global Data
 */
extern struct jnx_routeserviced_base jnx_routeserviced_base;
extern evContext jnx_routeserviced_event_ctx;
extern int jnx_routeserviced_ssd_fd;
extern uint32_t rttid;

/* 
 * Global Variables
 */
static patroot pat_rt_data_root;    /* root data for the patricia tree */
static uint32_t rt_data_count; /* number of nodes in rt_data pat tree */

/*
 * Defining an inline, which will return NULL if the patnode pointer
 * is NULL, or the enclosing structure otherwise.
 */
PATNODE_TO_STRUCT(rt_data_entry, jnx_routeserviced_data_t, 
                  routeserviced_patnode)


/**
 * @brief
 *     Free route date entry 
 *
 * Frees the route data fields first before freeing the structure.
 *
 * @param[in] rt_data
 *     Pointer to route data entry to be freed 
 */

static void
rt_data_free (jnx_routeserviced_data_t *rt_data)
{
    if (rt_data == NULL) {
        return;
    }

    if (rt_data->ifname) {
        free(rt_data->ifname);
    }
    if (rt_data->dest) {
        free(rt_data->dest);
    }
    if (rt_data->nhop) {
        free(rt_data->nhop);
    }
    free(rt_data);
}
/**
 * @brief
 *     Clear a particular route entry 
 *
 * Calls @patricia_delete to delete the entry. Routine decrements and frees
 * the data iff @patricia_delete returns with a success.
 *
 * @param[in] rt_data
 *     Pointer to route data entry to be deleted 
 */

static void
rt_data_delete_entry (jnx_routeserviced_data_t *rt_data)
{
    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_CONFIG, "%s: deleting rt_data %s",
                __func__, rt_data->dest);

    /* 
     * Remove route data from patricia tree
     */
    if (patricia_delete(&pat_rt_data_root, &rt_data->routeserviced_patnode)) {
        /*
         * Decrement count and free only if it successfully deleted.
         */
        rt_data_count--;
        rt_data_free(rt_data);
    } else {
        ERRMSG(JNX_ROUTESERVICED_PATRICIA_ERR, LOG_EMERG,
               "patricia %s failure", "delete");
    }
}

/**
 * @brief
 *     Clear a particular route entry by context
 *
 * Tries to search for the context and calls @rt_data_delete_entry to 
 * delete the entry. 
 *
 * @param[in] ctx
 *     Context stored for the route add data
 */
void 
rt_data_delete_entry_by_ctx (unsigned int ctx)
{
    jnx_routeserviced_data_t *rt_data;

    /*
     * First of all, delete the entry if it has the same destination
     */
    rt_data = rt_data_first();

    while (rt_data != NULL) {
	if (rt_data->req_ctx == ctx) {
	    /*
	     * Delete the route data entry from the patricia storage
	     */
	    rt_data_delete_entry(rt_data);
	    break;
	}
	rt_data = rt_data_next(rt_data);
    }
}

/**
 * @brief
 *     Add route data to patricia storage
 *
 * New data entry is populated and added to the patricia storage. The
 * key for the patricia storage is the route destination.
 *
 * @param[in] dest
 *     Destination inet addr
 * @param[in] nhop
 *     Next hop inet addr
 * @param[in] ifname
 *     Interface name
 *     
 * @return 
 *     Pointer to the address of the new route data
 */

static jnx_routeserviced_data_t *
rt_data_add (const char *dest, const char *nhop, const char *ifname,
	     const char *plen)
{
    jnx_routeserviced_data_t *rt_data;

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_CONFIG, "%s: adding rt_data %s",
                __func__, dest);

    /*
     * First of all, delete the entry if it has the same destination
     */
    rt_data = rt_data_first();

    while (rt_data != NULL) {
	if (!strncmp(dest, rt_data->dest, strlen(dest))) {
	    /*
	     * Delete the route data entry from the patricia storage
	     */
	    rt_data_delete_entry(rt_data);
	    break;
	}
	rt_data = rt_data_next(rt_data);
    }

    /* 
     * Create a new route data entry and also send a route add request
     */

    /* 
     * Allocate & initialize route data 
     */
    rt_data = calloc(1, sizeof(jnx_routeserviced_data_t));
    if (rt_data == NULL) {
        ERRMSG(JNX_ROUTESERVICED_ALLOC_ERR, LOG_EMERG,
               "failed to allocate memory by %s", "calloc");
        return NULL;
    }

    /*
     * All fields are mandatory
     */
    if (dest != NULL && nhop != NULL && ifname != NULL && plen != NULL) {
        rt_data->dest = strdup(dest);
        rt_data->nhop = strdup(nhop);
        rt_data->ifname = strdup(ifname);

        if (!(rt_data->dest) || !(rt_data->nhop) || !(rt_data->ifname)) {
            ERRMSG(JNX_ROUTESERVICED_ALLOC_ERR, LOG_EMERG,
                   "failed to allocate memory by %s", "strdup");
            /*
             * Free successful allocations
             */
            rt_data_free(rt_data);
            return NULL;
        }

        rt_data->prefixlen = (unsigned int) strtol(plen, NULL, 0);
        rt_data->req_status = RT_ADD_REQ_STAT_PENDING;
        /*
         * The address of the allocated structure is used as the
         * unique context for route requests
         */
        rt_data->req_ctx = (unsigned int) rt_data;
    } else {
        ERRMSG(JNX_ROUTESERVICED_RT_DATA_ERR, LOG_ERR,
               "Error in route data %s", "NULL elements");
        /*
         * Free route data 
         */
        rt_data_free(rt_data);
        return NULL;
    }

    /*
     * Initialize the tree nodes key length with the number of 
     * required bytes
     */
    patricia_node_init_length(&rt_data->routeserviced_patnode,
                              strlen(rt_data->dest) + 1);

    /* 
     * Add the populated route data in the patricia tree
     */
    if (patricia_add(&pat_rt_data_root, &rt_data->routeserviced_patnode)) {
        /*
         * Increment count and request route add only if it successfully 
         * added to the patricia storage. Avoids unneccesary inconsistencies.
         */
        rt_data_count++;

        /*
         * Send only when you are sure that you have gotten a routing 
         * table-id from SSD.
         */
        if (rttid) {
            jnx_routeserviced_ssd_add_route(jnx_routeserviced_ssd_fd, 
                                            rt_data->dest, 
                                            rt_data->nhop,
                                            rt_data->ifname,
                                            rt_data->prefixlen,
                                            rt_data->req_ctx);
        }
    } else {
        ERRMSG(JNX_ROUTESERVICED_PATRICIA_ERR, LOG_EMERG,
               "patricia %s failure", "add");
        /*
         * Free route data 
         */
        rt_data_free(rt_data);
        return NULL;
    }
    return rt_data;
}

/**
 * @brief
 *     Get next route data 
 *
 * Wrapper function to retrieve the next route data entry stored 
 * in the patricia storage. Effectively, first data is returned if 
 * @a rt_data is NULL
 *
 * @param[in] rt_data
 *     Pointer to the route data next of which has to be found. 
 *
 * @return 
 *     Pointer to the address of the route data
 */
jnx_routeserviced_data_t *
rt_data_next (jnx_routeserviced_data_t *rt_data)
{
    return rt_data_entry(patricia_find_next(&pat_rt_data_root,
                                            rt_data ? 
                                            &rt_data->routeserviced_patnode : 
                                            NULL));
}

/**
 * @brief
 *     Get first route data 
 *
 * Wrapper function to retrieve the first route data entry stored 
 * in the patricia storage.
 *
 * @return 
 *     Pointer to the address of the route data
 */

jnx_routeserviced_data_t *
rt_data_first (void)
{
    return rt_data_entry(patricia_find_next(&pat_rt_data_root, NULL));
}

/**
 * @brief
 *     Find and delete the patricia tree storage for a particular
 *     routing data for which the @c rt_dest matches. It also 
 *     sends a route delete request to SSD.
 *
 * @param[in] rt_dest
 *     Destination address for the routing data
 */
static void
rt_data_find_and_delete (char *rt_dest)
{
    jnx_routeserviced_data_t *rt_data = NULL;

    rt_data = rt_data_first();

    while (rt_data != NULL) {
	if (!strncmp(rt_dest, rt_data->dest, strlen(rt_dest))) {
	    jnx_routeserviced_ssd_del_route(jnx_routeserviced_ssd_fd, 
					    rt_data->dest, 
					    rt_data->nhop,
					    rt_data->ifname,
					    rt_data->prefixlen,
					    rt_data->req_ctx);

	    break;
	}
	rt_data = rt_data_next(rt_data);
    }
}

/**
 * @brief
 *     Clear stored route data and delete route(s)
 *
 * Traverses through the patricia tree storage and deletes them one
 * by one. As in when a route data entry is picked up for deletion,
 * request, thereby, is sent to the SSD for route deletion.
 *
 * @note
 *     Could possibly result in an inconsistent information when route
 *     deletion fails but the route entry is removed from patricia
 *     storage.
 */

static void
rt_data_clear (void)
{
    jnx_routeserviced_data_t *rt_data = NULL;

    while ((rt_data = rt_data_first()) != NULL) {
        /* 
         * Delete routes 
         *
         * Note: Using libssd APIs to deregister the route service,
         * one can clean up all the routes added by a client.
         */
        jnx_routeserviced_ssd_del_route(jnx_routeserviced_ssd_fd, 
                                        rt_data->dest, 
                                        rt_data->nhop,
                                        rt_data->ifname,
                                        rt_data->prefixlen,
                                        rt_data->req_ctx);

        /*
         * Delete the route data entry from the patricia storage
         */
        rt_data_delete_entry(rt_data);
    }
}

/**
 * @brief
 *     Send pending stored route add requests
 *
 * Traverses through the patricia tree storage and checks whether any
 * stored route add requests were pending. As in when a route data entry 
 * is picked up for addition, a request is sent to the SSD for 
 * route addition.
 */
void
rt_add_data_send_pending (void)
{
    jnx_routeserviced_data_t *rt_data = NULL;

    rt_data = rt_data_first();

    while (rt_data != NULL) {
        /* 
         * Adds pending routes 
         */
        if (rt_data->req_status == RT_ADD_REQ_STAT_PENDING) {
            jnx_routeserviced_ssd_add_route(jnx_routeserviced_ssd_fd, 
                                            rt_data->dest, 
                                            rt_data->nhop,
                                            rt_data->ifname,
                                            rt_data->prefixlen,
                                            rt_data->req_ctx);
        }
        rt_data = rt_data_next(rt_data);
    }
}

/**
 * @brief
 *     Mark all stored route add requests as pending
 *
 *     This is particularly useful in sending all route add
 *     requests again.
 */
void 
rt_add_data_mark_pending (void)
{
    jnx_routeserviced_data_t *rt_data = NULL;

    rt_data = rt_data_first();

    while (rt_data != NULL) {
        rt_data->req_status = RT_ADD_REQ_STAT_PENDING;
        rt_data = rt_data_next(rt_data);
    }
}

/**
 * @brief
 *     Init patricia tree storage 
 *
 * Initialize the patricia root node.
 */

void
rt_data_init (void)
{
    patricia_root_init(&pat_rt_data_root, TRUE, RT_DATA_STR_SIZE, 0);
    return;
}

/**
 * @brief
 *     Update the route add request
 *
 * Based on the server's reply and the context of the request
 * which is echoed back, this routine updates the request's status
 * to either a success, failure or pending.
 *
 * @param[in] cli_ctx
 *     Requests context echoed back from SSD server
 * @param[in] result
 *     Result of the request 
 */

void
rt_add_data_update (unsigned int cli_ctx, int result)
{
    jnx_routeserviced_data_t *rt_data = NULL;
    rt_data = rt_data_first();

    while (rt_data != NULL) {
        if (rt_data->req_ctx == cli_ctx) {
            rt_data->req_status = result;
        }
        rt_data = rt_data_next(rt_data);
    }
}

/**
 * @brief
 *     Handler for dax_walk_list to parse each configured route-info knob
 *
 * @param[in] dwd
 *     Opaque dax data
 * @param[in] dop
 *     DAX pointer for the data object
 * @param[in] action
 *     Action on the given application's object
 * @param[in] data
 *     User data passed to the handler
 *
 * @return
 *     DAX_WALK_OK    success
 *     DAX_WALK_ABORT failure
 */
static int
parse_route_info (dax_walk_data_t *dwd,
		  ddl_handle_t *dop,
		  int action,
		  void *data __unused)
{
    char rt_dest[RT_DATA_STR_SIZE];
    char rt_nhop[RT_DATA_STR_SIZE];
    char rt_ifname[RT_DATA_STR_SIZE];
    char rt_plen[RT_DATA_STR_SIZE];

    switch (action) {
    case DAX_ITEM_DELETE_ALL:
	/*
	 * Flush all data from patricia storage and send delete requests for
	 * all route data
	 */
	rt_data_clear();
	return DAX_WALK_OK;

    case DAX_ITEM_DELETE:
	if (!dax_get_stringr_by_dwd_ident(dwd, NULL, 0, rt_dest, 
					  sizeof(rt_dest))) {
	    return DAX_WALK_ABORT;
	}

	/*
	 * Find and delete the routing data from patricia tree. The following 
	 * routine will also send the route delete request
	 */
	rt_data_find_and_delete(rt_dest);
	break;

    case DAX_ITEM_CHANGED:
	/*
	 * Fetch the attributes
	 */
	if (!dax_get_stringr_by_aid(dop, RT_DATA_DEST, rt_dest, 
				    sizeof(rt_dest))) {
	    return DAX_WALK_ABORT;
	}
        if (!dax_get_stringr_by_aid(dop, RT_DATA_NHOP, rt_nhop, 
				    sizeof(rt_nhop))) {
	    return DAX_WALK_ABORT;
	}
        if (!dax_get_stringr_by_aid(dop, RT_DATA_IFNAME, rt_ifname, 
				    sizeof(rt_ifname))) {
	    return DAX_WALK_ABORT;
	}
        if (!dax_get_stringr_by_aid(dop, RT_DATA_PREFIXLEN, rt_plen, 
				    sizeof(rt_plen))) {
	    return DAX_WALK_ABORT;
	}

	/*
	 * Now, add the data. This routine will overwrite the patricia 
	 * storage for the same destination if other attributes have 
	 * changed for the destination object.
	 */
        rt_data_add(rt_dest, rt_nhop, rt_ifname, rt_plen);

	break;

    case DAX_ITEM_UNCHANGED:
	/*
	 * Do nothing, this should not be called because dax_walk_list has 
	 * the parameter DAX_WALK_DELTA passed to it
	 */
	break;

    }

    return DAX_WALK_OK;
}

/**
 * @brief
 *     Read jnx-routeserviced's configuration from database
 *
 * This routine is called as a callback from @a junos_daemon_init for
 * purposes like to check if the configuration exists and eventually
 * read the configurations if they exist. While the configuration is
 * being read, the route data is being populated in a patricia tree.
 *
 * @note
 *     Do not use ERRMSG during configuration check. Could use during
 *     configuration read phase.
 * 
 * @param[in] check 
 *     Non-zero is configuration is to be checked only and zero if it
 *     has to be read.
 *
 * @return Status of config read
 *     @li 0      Success
 *     @li ENOENT Field not found in configuration
 *     @li -1  If junos_trace_read_config() fails
 */

int
jnx_routeserviced_config_read (int check)
{
    ddl_handle_t *top = NULL;
    ddl_handle_t *dop = NULL;
    const char *rt_data_config_name[] = { DDLNAME_JNX_ROUTESERVICE,
                                          DDLNAME_JNX_ROUTESERVICE_ROUTE_INFO,
                                          NULL };
    char rt_dest[RT_DATA_STR_SIZE];
    char rt_nhop[RT_DATA_STR_SIZE];
    char rt_ifname[RT_DATA_STR_SIZE];
    char rt_plen[RT_DATA_STR_SIZE];
    const char *missing_field_name = NULL;
    int  status = 0;

    /*
     * Read routeservice data configuration
     */
    if (dax_get_object_by_path(NULL, rt_data_config_name, &top, FALSE)) {
        if (dax_is_changed(top) == FALSE) {
            /* 
             * If top hasn't changed, ignore this configuration and exit
             */
            goto exit;
        }

        /* 
         * All attributes are mandatory. Do a sanity check and error out
	 * if attributes are missing.
         */
        while (status == 0 && dax_visit_container(top, &dop)) {
            if (dax_get_stringr_by_aid(dop, RT_DATA_DEST, rt_dest,
                                       sizeof(rt_dest) ) == FALSE) {

                /*
                 * Set the missing field and bail out
                 */
                missing_field_name = DDLNAME_RT_DATA_DEST;
                status = ENOENT;
                goto exit;
            } 

            if (dax_get_stringr_by_aid(dop, RT_DATA_NHOP, rt_nhop,
                                       sizeof(rt_nhop)) == FALSE) {
                /*
                 * Set the missing field and bail out
                 */
                missing_field_name = DDLNAME_RT_DATA_NHOP;
                status = ENOENT;
                goto exit;
            }

            if (dax_get_stringr_by_aid(dop, RT_DATA_IFNAME, rt_ifname,
                                       sizeof(rt_ifname)) == FALSE) {
                /*
                 * Set the missing field and bail out
                 */
                missing_field_name = DDLNAME_RT_DATA_IFNAME;
                status = ENOENT;
                goto exit;
            }
            if (dax_get_stringr_by_aid(dop, RT_DATA_PREFIXLEN, rt_plen,
                                       sizeof(rt_plen)) == FALSE) {
                /*
                 * Set the missing field and bail out
                 */
                missing_field_name = DDLNAME_RT_DATA_PREFIXLEN;
                status = ENOENT;
                goto exit;
            }

        }
    } else {
        /*
         * Error in reading configuration, data can be cleared from 
         * the patricia storage
         */
        rt_data_clear();
	goto exit;
    }

    /*
     * No need to populate local data structures with configuration data 
     * when check is non-zero. When the check is non-zero, its a good 
     * idea to do sanity checks with the data and return an appropriate 
     * error code.
     */
    /*
     * Walk the deleted and changed list. Send delete requests for the 
     * deleted items and add (with overwrite flag) for changed and added 
     * items.
     */
    if (status == 0 && check == 0) {
	dax_walk_list(top, DAX_WALK_DELTA, parse_route_info, NULL);
    }

exit:

    /*
     * Print error, if any
     */
    if (status != 0 && missing_field_name) {
        dax_error(dop, "Missing '%s' field in route-info", missing_field_name);
    }


    /*
     * Finish up before return 
     */
    if (top) {
        dax_release_object(&top);
    }

    if (dop) {
        dax_release_object(&dop);
    }
    return status;
}

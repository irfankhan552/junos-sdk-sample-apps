/*
 * $Id: jnx-routeserviced_config.cc 346460 2009-11-14 05:06:47Z ssiano $
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
 *     jnx-routeserviced_config.cc
 * @brief 
 *     Configurations and patricia tree related routines 
 *
 * This file contains the routines to read/check configuration database,
 * store the configuration in a local data structure (patricia tree) and
 * eventually call the routines to add routes via SSD.
 */


#include <string.h>

#include <memory>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/queue.h>

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

#include JNX_CC_ROUTESERVICED_OUT_H

using namespace std;
using namespace junos; 

#include "jnx-routeserviced_ssd.h"
#include "jnx-routeserviced_config.h"

/*
 * Defining an inline, which will return NULL if the patnode pointer
 * is NULL, or the enclosing structure otherwise.
 */
PATNODE_TO_STRUCT(data_entry, rs_config_data_t, 
                  routeserviced_patnode)

/**
 * @brief
 *     Returns the reference of the unique instance
 */
jnx_routeserviced_config & 
jnx_routeserviced_config::Instance() {
    if (unique_instance.get() == 0) {
        unique_instance.reset(new jnx_routeserviced_config);
    }
    return *unique_instance;
}

/**
 * @brief
 *     Clear a particular route entry 
 *
 * Calls @patricia_delete to delete the entry. Routine decrements and frees
 * the data iff @patricia_delete returns with a success.
 *
 * @param[in] data
 *     Pointer to route data entry to be deleted 
 */

void
jnx_routeserviced_config::data_delete_entry (rs_config_data_t 
                                             *data)
{
    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_CONFIG, "%s: deleting data %s",
                __func__, data->dest);

    /* 
     * Remove route data from patricia tree
     */
    if (patricia_delete(&patricia_root, &data->routeserviced_patnode)) {
        /*
         * Decrement count and free only if it successfully deleted.
         */
        data_count--;
        data_free(data);
    } else {
        ERRMSG(JNX_ROUTESERVICED_PATRICIA_ERR, LOG_EMERG,
               "patricia %s failure", "delete");
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

void
jnx_routeserviced_config::data_add_entry (const char *dest, const char *nhop, 
                                          const char *ifname,
                                          const char *plen)
{
    rs_config_data_t *data;

    /*
     * Fetch the instance
     */
    jnx_routeserviced_ssd& rs_ssd = jnx_routeserviced_ssd::Instance();

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_CONFIG, "%s: adding data %s",
                __func__, dest);

    /* 
     * Create a new route data entry and also send a route add request
     */

    /* 
     * Allocate & initialize route data 
     */
    data = (rs_config_data_t *)
        calloc(1, sizeof(rs_config_data_t));
    if (data == NULL) {
        ERRMSG(JNX_ROUTESERVICED_ALLOC_ERR, LOG_EMERG,
               "failed to allocate memory by %s", "calloc");
        return;
    }

    /*
     * All fields are mandatory
     */
    if (dest != NULL && nhop != NULL && ifname != NULL && plen != NULL) {
        data->dest = strdup(dest);
        data->nhop = strdup(nhop);
        data->ifname = strdup(ifname);

        if (!(data->dest) || !(data->nhop) || !(data->ifname)) {
            ERRMSG(JNX_ROUTESERVICED_ALLOC_ERR, LOG_EMERG,
                   "failed to allocate memory by %s", "strdup");
            /*
             * Free successful allocations
             */
            data_free(data);
            return;
        }

        data->prefixlen = (unsigned int) strtol(plen, NULL, 0);
        data->req_status = rs_ssd.get_pending_status();
        /*
         * The number of route requests to SSD is used as the
         * unique context for route requests
         */
        data->req_ctx = ++ssd_req_count;
    } else {
        ERRMSG(JNX_ROUTESERVICED_RT_DATA_ERR, LOG_ERR,
               "Error in route data %s", "NULL elements");
        /*
         * Free route data 
         */
        data_free(data);
        return;
    }

    /*
     * Initialize the tree nodes key length with the number of 
     * required bytes
     */
    patricia_node_init_length(&data->routeserviced_patnode,
                              strlen(data->dest) + 1);

    /* 
     * Add the populated route data in the patricia tree
     */
    if (patricia_add(&patricia_root, &data->routeserviced_patnode)) {
        /*
         * Increment count and request route add only if it successfully 
         * added to the patricia storage. Avoids unneccesary inconsistencies.
         */
        data_count++;
        if (rs_ssd.get_rtt_id()) {
            rs_ssd.add_route(data->dest, 
                         data->nhop,
                         data->ifname,
                         data->prefixlen,
                         data->req_ctx);
        }
    } else {
        ERRMSG(JNX_ROUTESERVICED_PATRICIA_ERR, LOG_EMERG,
               "patricia %s failure", "add");
        /*
         * Free route data 
         */
        data_free(data);
        return;
    }
    return;
}

/**
 * @brief
 *     Get next route data 
 *
 * Wrapper function to retrieve the next route data entry stored 
 * in the patricia storage. Effectively, first data is returned if 
 * @a data is NULL
 *
 * @param[in] data
 *     Pointer to the route data next of which has to be found. 
 *
 * @return 
 *     Pointer to the address of the route data
 */
rs_config_data_t *
jnx_routeserviced_config::data_next (rs_config_data_t *data)
{
    return data_entry(patricia_find_next(&patricia_root,
                                         data ? 
                                         &data->routeserviced_patnode : 
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

rs_config_data_t *
jnx_routeserviced_config::data_first (void)
{
    return data_entry(patricia_find_next(&patricia_root, NULL));
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

void
jnx_routeserviced_config::data_delete_all (void)
{
    rs_config_data_t *data = NULL;
    jnx_routeserviced_ssd& rs_ssd = jnx_routeserviced_ssd::Instance();

    while ((data = data_first()) != NULL) {
        /*
         * Delete routes
         *
         * Note: Using libssd API to deregister the route service, 
         * one can clean up all the routes added by a client.
         */
        rs_ssd.del_route(data->dest, 
                         data->nhop,
                         data->ifname,
                         data->prefixlen,
                         data->req_ctx);

        ++ssd_req_count;

        /*
         * Delete the route data entry from the patricia storage
         */
        data_delete_entry(data);
    }
}


/**
 * @brief
 *     Init patricia tree storage 
 *
 * Initialize the patricia root node.
 */

void
jnx_routeserviced_config::data_init (void)
{
    patricia_root_init(&patricia_root, TRUE, RT_DATA_STR_SIZE, 0);
}

/**
 * @brief
 *     Update the route add request
 *
 * Based on the server's reply and the context of the request
 * which is echoed back, this routine updates the request's status
 * to either a success, failure or pending.
 *
 * @param[in] cfg
 *     Reference of the configuration object
 * @param[in] cli_ctx
 *     Requests context echoed back from SSD server
 * @param[in] result
 *     Result of the request 
 */
void
config_data_update_status (jnx_routeserviced_config &cfg, unsigned int ctx, 
                           unsigned int result)
{
    rs_config_data_t *data = NULL;
    data = cfg.data_first();

    while (data != NULL) {
        if (data->req_ctx == ctx) {
            data->req_status = result;
            return;
        }
        data = cfg.data_next(data);
    }
}

/**
 * @brief
 *     Send pending stored route add requests
 *
 * Traverses through the patricia tree storage and checks whether any
 * stored route add requests were pending. As in when a route data entry
 * is picked up for addition, a request is sent to the SSD for route addition.
 */
void
send_pending_rt_add_requests (jnx_routeserviced_config &cfg)
{
    rs_config_data_t *data = NULL;
    jnx_routeserviced_ssd& rs_ssd = jnx_routeserviced_ssd::Instance();

    data = cfg.data_first();

    while (data != NULL) {
        /*
         * Adds pending routes
         */
        if (data->req_status == rs_ssd.get_pending_status()) {
            rs_ssd.add_route(data->dest,
                             data->nhop,
                             data->ifname,
                             data->prefixlen,
                             data->req_ctx);
        }
        data = cfg.data_next(data);
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
mark_pending_rt_add_requests (jnx_routeserviced_config &cfg)
{
    rs_config_data_t *data = NULL;
    jnx_routeserviced_ssd& rs_ssd = jnx_routeserviced_ssd::Instance();

    data = cfg.data_first();

    while (data != NULL) {
        data->req_status = rs_ssd.get_pending_status();
        data = cfg.data_next(data);
    }
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
 *     @li -1      If junos_trace_read_config() fails
 */

int
jnx_routeserviced_config::read (int check)
{
    ddl_handle_t *top = NULL;
    ddl_handle_t *dop = NULL;
    const char *data_config_name[] = { DDLNAME_JNX_ROUTESERVICE,
                                       DDLNAME_JNX_ROUTESERVICE_ROUTE_INFO,
                                       NULL };
    const char *jnx_routeserviced_config_name[] = { DDLNAME_JNX_ROUTESERVICE, 
                                                    NULL };
    char rt_dest[RT_DATA_STR_SIZE];
    char rt_nhop[RT_DATA_STR_SIZE];
    char rt_ifname[RT_DATA_STR_SIZE];
    char rt_plen[RT_DATA_STR_SIZE];
    const char *missing_field_name = NULL;
    int  status = 0;
    
    /*
     * Fetch the config instance
     */
    jnx_routeserviced_config& rs_cfg_ = jnx_routeserviced_config::Instance();


    /*
     * Read trace configuration
     */
    if (junos_trace_read_config(check, jnx_routeserviced_config_name) != 0) {
	    return -1;
    }


    /*
     * Read routeservice data configuration
     */
    if (dax_get_object_by_path(NULL, data_config_name, &top, FALSE)) {
        if (dax_is_changed(top) == FALSE) {
            /* 
             * If top hasn't changed, ignore this configuration and exit
             */
            goto exit;
        }

        /* 
         * Clear stored config data in the patricia tree and
         * repopulate with new config data
         */
        if (check == 0) {
            rs_cfg_.data_delete_all();
        }

        /* 
         * All attributes are mandatory
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

            if (status == 0) {
                /*
                 * Read configuration successfully insofar. Go ahead and
                 * add route data to patricia tree, if check is 0
                 */
                if (check == 0) {
                    /*
                     * No need to populate local data structures with 
                     * configuration data when check is non-zero. 
                     * When the check is non-zero, its a good idea to do sanity
                     * checks with the data and return an appropriate error
                     * code.
                     */
                    rs_cfg_.data_add_entry(rt_dest,                                                                        rt_nhop, 
                                           rt_ifname, 
                                           rt_plen);
                }
            }
        }
    } else {
        /*
         * Error in reading configuration, data can be cleared from 
         * the patricia storage
         */
        if (check == 0) {
            rs_cfg_.data_delete_all();
        }
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

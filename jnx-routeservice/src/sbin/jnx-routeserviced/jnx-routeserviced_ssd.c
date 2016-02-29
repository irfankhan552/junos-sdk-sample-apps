/*
 * $Id: jnx-routeserviced_ssd.c 346460 2009-11-14 05:06:47Z ssiano $
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
 *     jnx-routeserviced_ssd.c
 * @brief 
 *     SDK Service Daemon (SSD) related routines 
 *
 * This file contains the routines to connect and communicate
 * with the SSD server including route additions and deletion
 * APIS.
 */


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <isc/eventlib.h>

#include <ddl/dax.h>

#include <jnx/ssd_ipc.h>
#include <jnx/ssd_ipc_msg.h>
#include <jnx/jnx_types.h>
#include <jnx/aux_types.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>
#include <jnx/patricia.h>

#include JNX_ROUTESERVICED_OUT_H

#include "jnx-routeserviced_config.h"
#include "jnx-routeserviced_ssd.h"
#include "jnx-routeserviced_util.h"
#include "jnx-routeserviced_gencfg.h"

/*
 * Global Data
 */
extern evContext jnx_routeserviced_event_ctx;

/*
 * Global Variables
 */
uint32_t rttid = 0;  /* Routing table id */
int jnx_routeserviced_ssd_fd;
TAILQ_HEAD(, jnx_routeserviced_route_add_req) route_add_req_list;
TAILQ_HEAD(, jnx_routeserviced_route_del_req) route_del_req_list;

/*
 * Static Variables
 */
static int client_id;    /* Unique client id */

static jnx_routeserviced_state_t jnx_routeserviced_state; /* variable
                                                             to keep track
                                                             of the 
                                                             application's
                                                             state */

/* 
 * Forward declarations
 */
static int jnx_routeserviced_ssd_connection (int fd);
static void jnx_routeserviced_ssd_connection_close (int fd, int cause);
static void jnx_routeserviced_ssd_msg (int fd, struct ssd_ipc_msg *cmsg);

/* 
 * Function table handler instance for communication
 */
static struct ssd_ipc_ft jnx_routeserviced_ssd_ft = {
    jnx_routeserviced_ssd_connection,       /**< Handler for a conn
                                              reply from server */
    jnx_routeserviced_ssd_connection_close, /**< Handler for a conn
                                              close decree from 
                                              server */
    jnx_routeserviced_ssd_msg               /**< Handler for messages
                                              received from server
                                              as responses to client's
                                              requests */
};

/**
 * @brief
 *     Fetch the route add request with a particular context
 *     
 * It run through the list of requests and returns the address
 * of the one with the matching context.
 *
 * @param[in] ctx
 *     Request's context
 *
 * @return
 *     Pointer to route add request or NULL if not found
 */
static struct jnx_routeserviced_route_add_req *
jnx_routeserviced_get_add_req_by_ctx (unsigned int ctx)
{
    struct jnx_routeserviced_route_add_req *req;

    TAILQ_FOREACH(req, &route_add_req_list, entries) {
        if (req->ctx == ctx) {
            return req;
        }
    }
    return NULL;
}

/**
 * @brief
 *     Fetch the route delete request with a particular context
 *     
 * It run through the list of requests and returns the address
 * of the one with the matching context.
 *
 * @param[in] ctx
 *     Request's context
 *
 * @return
 *     Pointer to route delete request or NULL if not found
 */
static struct jnx_routeserviced_route_del_req *
jnx_routeserviced_get_del_req_by_ctx (unsigned int ctx)
{
    struct jnx_routeserviced_route_del_req *req;

    TAILQ_FOREACH(req, &route_del_req_list, entries) {
        if (req->ctx == ctx) {
            return req;
        }
    }
    return NULL;
}

/**
 * @brief
 *     Handles connection request's reply from server
 *
 * This function is called as a callback when the handler was registered
 * in @a ssd_ipc_connect_rt_tbl. Stores the server's fd in the base 
 * structure for future communication purposes and request for the client 
 * id from the SSD server using @a ssd_get_client_id
 *
 * @param[in] fd
 *     SSD server's file descriptor
 *
 * @return
 *     0 Always
 */
static int
jnx_routeserviced_ssd_connection (int fd)
{
    /* 
     * Store the server fd
     */
    jnx_routeserviced_ssd_fd = fd;

    if (fd <= 0) {
        evSetTimer(jnx_routeserviced_event_ctx, 
                   jnx_routeserviced_ssd_reconnect,
                   NULL, evAddTime(evNowTime(), evConsTime(5,0)),
                   evConsTime(0,0), NULL);

        junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: ",
                    "SSD connection failed, shall retry in few "
                    "seconds ...", __func__);
        return -1;
    }

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                " connected to SSD server with fd %d",
                __func__,  fd);

    if (jnx_routeserviced_state == JNX_RS_STATE_RESTARTING) {
        /*
         * jnx-routeserviced has restarted, fetch client-id from
         * JUNOS's GENCFG store
         */
        if (jnx_routeserviced_gencfg_get_client_id(&client_id) == 0) {
            junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                        " fetched client-id %d from GENCFG store",
                        __func__,  client_id);
        } else {
            /*
             * Error fetching client-id, default to no client-id
             */
            client_id = 0;
        }

        if (ssd_get_client_id(fd, client_id) < 0) {
            ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
                   "call to : %s failed with error code %d",
                   "ssd_get_client_id", errno);
        }

    } else {

        /* 
         * Use original client_id if SSD had closed connection
         */
        if (ssd_get_client_id(fd, client_id) < 0) {
            ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
                   "call to : %s failed with error code %d",
                   "ssd_get_client_id", errno);
        }
    }

    return 0;
}

/**
 * @brief
 *     Handles connection close decree by server
 *
 * This function is called as a callback when the handler was registered
 * in @a ssd_ipc_connect_rt_tbl. Resets the SSD fd
 * stored in @jnx_routeserviced_base 
 *
 * @param[in] fd
 *     SSD server's file descriptor
 * @param[in] cause
 *     Cause of the connection closure
 */
static void
jnx_routeserviced_ssd_connection_close (int fd __unused, int cause)
{
    /* 
     * Reset server fd
     */
    jnx_routeserviced_ssd_fd = 0;

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                " connection closed by server with cause %d",
                __func__, cause);


    /*
     * SSD has closed the connection
     */
    jnx_routeserviced_state = JNX_RS_STATE_SSD_DISCONNECTED;

    /*
     * Start a reconnection timer and trace this event
     */
    evSetTimer(jnx_routeserviced_event_ctx, 
               jnx_routeserviced_ssd_reconnect,
               NULL, evAddTime(evNowTime(), evConsTime(5,0)),
               evConsTime(0,0), NULL);

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: ",
                "SSD closed connection, shall retry in few seconds ...",
                __func__);
}

/**
 * @brief
 *     Add route routine
 *
 * The route params are populated and request sent over SSD ipc.
 * Many values are defaulted here to exemplify the usage.
 *
 * @param[in] fd
 *     Server's descriptor
 * @param[in] destination
 *     Route's destination address
 * @param[in] nhop
 *     Route's next hop address
 * @param[in] ifname
 *     Route's logical interface name
 * @param[in] prefixlen 
 *     prefix length 
 * @param[in] req_ctx
 *     Request's context
 */
void
jnx_routeserviced_ssd_add_route (int fd, char *destination, 
                                 char *nhop, char *ifname,
                                 unsigned int prefixlen,
                                 unsigned int req_ctx)
{
    struct jnx_routeserviced_route_add_req *ssd_add_req;
    ifl_idx_t ifl_idx;
    int error;

    /* 
     * Get the ifl structure of the logical interface
     */
    error = jnx_routeservice_util_get_idx_by_iflname(ifname, &ifl_idx);
    if (error != 0) {
        ERRMSG(JNX_ROUTESERVICED_UTIL_ERR, LOG_ERR,
               "Util error: %s with return code %d",
               "could not get ifl index", error);
        
        rt_add_data_update(req_ctx, RT_ADD_REQ_STAT_FAILURE);

        return;
    }

    /* 
     * Allocate memory for route add
     */
    ssd_add_req = calloc(1, sizeof(struct jnx_routeserviced_route_add_req));
    if (ssd_add_req == NULL) {
        ERRMSG(JNX_ROUTESERVICED_ALLOC_ERR, LOG_EMERG,
               "failed to allocate memory: %s", "calloc");
        
        rt_add_data_update(req_ctx, RT_ADD_REQ_STAT_FAILURE);

        return;
    }

    /* 
     * Destination route info
     */
    memset(&(ssd_add_req->route_addr), 0, sizeof(ssd_sockaddr_un));
    ssd_setsocktype(&(ssd_add_req->route_addr), SSD_GF_INET);
    ssd_add_req->route_addr.in.gin_addr = inet_addr(destination);

    /* 
     * Next hop for destination
     */
    memset(&(ssd_add_req->route_nh), 0, sizeof(ssd_sockaddr_un));
    ssd_setsocktype(&(ssd_add_req->route_nh), SSD_GF_INET);
    ssd_add_req->route_nh.in.gin_addr = inet_addr(nhop);

    /* 
     * Local address of logical interface
     */
    memset(&(ssd_add_req->route_local), 0, sizeof(ssd_sockaddr_un));
    ssd_setsocktype(&(ssd_add_req->route_local), SSD_GF_INET);

    if (jnx_routeservice_util_get_prefix_by_ifl(ifl_idx) == NULL) {
        ERRMSG(JNX_ROUTESERVICED_UTIL_ERR, LOG_ERR,
               "Util error: %s", "could not get local address");
        
        rt_add_data_update(req_ctx, RT_ADD_REQ_STAT_FAILURE);

        /*
         * Free allocated structure before returning
         */
        free(ssd_add_req);
        return;
    }

    ssd_add_req->route_local.in.gin_addr = 
        inet_addr(jnx_routeservice_util_get_prefix_by_ifl(ifl_idx));

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: route add"
               " request: dest: %s, nhop: %s via %s with context %u",
                __func__,  destination, nhop, ifname, req_ctx);

    /* 
     * Prepare route paramater structure
     */
    bzero(&(ssd_add_req->rtp), sizeof(struct ssd_route_parms));
    ssd_add_req->rtp.rta_dest = &(ssd_add_req->route_addr);
    ssd_add_req->rtp.rta_prefixlen = prefixlen;
    ssd_add_req->rtp.rta_preferences.rtm_val[0] = 1;
    ssd_add_req->rtp.rta_preferences.rtm_type[0] = SSD_RTM_VALUE_PRESENT;
    ssd_add_req->rtp.rta_rtt = rttid;
    ssd_add_req->rtp.rta_flags = SSD_ROUTE_ADD_FLAG_OVERWRITE;

    ssd_add_req->rtp.rta_n_gw = 1;
    ssd_add_req->rtp.rta_gateway[0].rtg_gwaddr = &(ssd_add_req->route_nh);
    ssd_add_req->rtp.rta_gateway[0].rtg_ifl = ifl_idx;
    ssd_add_req->rtp.rta_gateway[0].rtg_lcladdr = &(ssd_add_req->route_local);

    /* 
     * Request to add route
     */
    if (ssd_request_route_add(fd, &(ssd_add_req->rtp), req_ctx)) {
        ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
               "call to : %s failed with error code %d",
               "ssd_request_route_add", errno);
        
        rt_add_data_update(req_ctx, RT_ADD_REQ_STAT_FAILURE);

        /*
         * Free allocated structure before returning
         */
        free(ssd_add_req);
        return;
    }

    ssd_add_req->ctx = req_ctx;
    TAILQ_INSERT_TAIL(&route_add_req_list, ssd_add_req, entries);

    return;
}

/**
 * @brief
 *     Delete route routine
 *
 * The route params are populated and delete request sent over SSD ipc.
 *
 * @param[in] fd
 *     Server's descriptor
 * @param[in] destination
 *     Route's destination address
 * @param[in] nhop
 *     unused
 * @param[in] ifname
 *     unused
 * @param[in] prefixlen 
 *     prefix length 
 * @param[in] req_ctx
 *     Request's context
 */

void
jnx_routeserviced_ssd_del_route (int fd, char *destination, 
                                 char *nhop __unused, 
                                 char *ifname __unused,
                                 unsigned int prefixlen,
                                 unsigned int req_ctx)
{
    struct jnx_routeserviced_route_del_req *ssd_del_req;

    /* 
     * Allocate memory for route delete 
     */
    ssd_del_req = calloc(1, sizeof(struct jnx_routeserviced_route_del_req));
    if (ssd_del_req == NULL) {
        ERRMSG(JNX_ROUTESERVICED_ALLOC_ERR, LOG_EMERG,
               "failed to allocate memory: %s", "calloc");
        return;
    }


    /* 
     * Destination route info
     */
    memset(&(ssd_del_req->route_addr), 0, sizeof(ssd_sockaddr_un));
    ssd_setsocktype(&(ssd_del_req->route_addr), SSD_GF_INET);
    ssd_del_req->route_addr.in.gin_addr = inet_addr(destination);

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: route del"
                " request: dest: %s, nhop: %s via %s",
                __func__, destination, nhop, ifname);

    /* 
     * Prepare route paramater structure
     */
    bzero(&(ssd_del_req->rtp), sizeof(struct ssd_rt_delete_parms));
    ssd_del_req->rtp.rtd_dest = &(ssd_del_req->route_addr);
    ssd_del_req->rtp.rtd_prefixlen = prefixlen;
    ssd_del_req->rtp.rtd_rtt = rttid;

    /* 
     * Request to delete route
     */
    if (ssd_request_route_delete(fd, &(ssd_del_req->rtp), req_ctx) < 0) {
        ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
               "call to : %s failed with error code %d",
               "ssd_request_route_delete", errno);
        /*
         * Free allocated structure before returning
         */
        free(ssd_del_req);
        return; 
    }

    ssd_del_req->ctx = req_ctx;
    TAILQ_INSERT_TAIL(&route_del_req_list, ssd_del_req, entries);

    return;
}

/**
 * @brief
 *     Handles messages from SSD server
 *
 * This function is called as a callback when the handler was registered
 * in @a ssd_ipc_connect_rt_tbl. Handles different types and subtypes
 * appropriately. 
 *
 * @param[in] fd
 *     SSD server's file descriptor
 * @param[in] cmsg
 *     Pointer to the message structure
 */

static void
jnx_routeserviced_ssd_msg (int fd, struct ssd_ipc_msg *cmsg)
{
    /*
     * Context for route table lookup API
     * TBD: Could be changed in future when this application supports
     * multiple route tables
     */
    unsigned int rt_table_ctx = 2;
    switch (cmsg->ssd_ipc_cmd) {
    case SSD_SESSION_ID_REPLY:
        if (SSD_SESSION_ID_REPLY_CHECK_STATUS(cmsg)) {
            switch (SSD_SESSION_ID_REPLY_GET_STATUS(cmsg)) {
            case SSD_SESSION_SUCCESS:
                client_id = SSD_SESSION_ID_REPLY_GET_CLIENT_ID(cmsg);
                junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                            "received client id from SSD: %u",
                            __func__, client_id);

                /*
                 * Push the client-id in the gencfg store
                 */
                jnx_routeserviced_gencfg_delete_client_id(0);
                jnx_routeserviced_gencfg_store_client_id(client_id);

                /* 
                 * Request to setup route service if connecting for the
                 * first time.
                 */
                if (ssd_setup_route_service(fd) < 0) {
                    ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
                           "call to : %s failed with error code %d",
                           "ssd_setup_route_service", errno);
                }
                break;
            case SSD_SESSION_CLIENT_ID_RESTORED:
                if (jnx_routeserviced_state == JNX_RS_STATE_RESTARTING) { /*
                     * If jnx-routeserviced is restarting, we need
                     * to get the routing table id once again.
                     */
                    char *rt_tb_name = strdup("inet.0");
                    if (!rt_tb_name) {
                        ERRMSG(JNX_ROUTESERVICED_ALLOC_ERR, LOG_EMERG,
                               "failed to allocate memory: %s", 
                               "strdup");
                    } else {
                        /* 
                         * Request for a route table id
                         */
                        if (ssd_request_route_table_lookup(fd, rt_tb_name, 
                                                           rt_table_ctx) < 0) {
                            ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
                                   "call to : %s failed with error code %d",
                                   "ssd_request_route_table_lookup", errno);
                        }
                    }
                }

                /*
                 * Set the appropriate state
                 */
                jnx_routeserviced_state = JNX_RS_STATE_SSD_CLIENT_ID_RESTORED;

                junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS,
                            "%s: SSD has retored the original client-id %d",
                            __func__, 
                            SSD_SESSION_ID_REPLY_GET_CLIENT_ID(cmsg));
                break;
            default:
                junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS,
                            "%s: unknown type for get client-id request",
                            __func__);
                break;
            }
        } else {
            /*
             * Error in session id reply
             */
            ERRMSG(JNX_ROUTESERVICED_SSD_REPLY_FAIL, LOG_ERR, 
                   "SSD replies with failure status for %s", 
                   "get client id request");
        }
        break;

    case SSD_ROUTE_SERVICE_REPLY:
        if (!SSD_ROUTE_SERVICE_REPLY_CHECK_STATUS(cmsg)) {
            ERRMSG(JNX_ROUTESERVICED_SSD_REPLY_FAIL, LOG_ERR, 
                   "SSD replies with failure status for"
                   "setup route service request with error code %d",
                   SSD_ROUTE_SERVICE_REPLY_GET_ERROR_CODE(cmsg));

            switch (SSD_ROUTE_SERVICE_REPLY_GET_ERROR_CODE(cmsg)) {
                /*
                 * Handle the following case and resend all the routes
                 * add requests
                 */
                case SSD_ROUTE_SERVICE_RESET:
                    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, 
                                "%s: SSD replies with route service reset type",
                                __func__);
                    
                    /*
                     * Mark all requests as pending so that it will be 
                     * resent
                     */
                    rt_add_data_mark_pending();

                    /* 
                     * Request to setup route service
                     */
                    if (ssd_setup_route_service(fd) < 0) {
                        ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
                               "call to : %s failed with error code %d",
                               "ssd_setup_route_service", errno);
                    }
                    break;
                default:
                    break;
            }

        } else {
            char *rt_tb_name = strdup("inet.0");

            junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: route"
                        " service has been setup successfully", __func__);

            if (!rt_tb_name) {
                ERRMSG(JNX_ROUTESERVICED_ALLOC_ERR, LOG_EMERG,
                       "failed to allocate memory: %s", 
                       "strdup");
            } else {
                /* 
                 * Request for a route table id
                 */
                if (ssd_request_route_table_lookup(fd, rt_tb_name, 
                                                   rt_table_ctx) < 0) {
                    ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
                           "call to : %s failed with error code %d",
                           "ssd_request_route_table_lookup", errno);
                 }
            }
        }
        break;

    case SSD_ROUTE_TABLE_LOOKUP_REPLY:
        if (SSD_ROUTE_TABLE_LOOKUP_REPLY_CHECK_STATUS(cmsg)) {
            rttid = SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_RTTID(cmsg);
            junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                        "received routing table id as %u", 
                        __func__, rttid);
            /*
             * See if we have any pending route add requests and send those
             */
            rt_add_data_send_pending();
        } else {
            ERRMSG(JNX_ROUTESERVICED_SSD_REPLY_FAIL, LOG_ERR, 
                   "SSD replies with a failure status for %s",
                   "route table lookup request");
        }
        break;

    case SSD_ROUTE_ADD_REPLY:
        if (SSD_ROUTE_ADD_REPLY_CHECK_STATUS(cmsg)) {
            /* 
             * Update the patricia tree's status field using
             * the echoed client context.
             */
            rt_add_data_update((unsigned int)
                           SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg), 
                           RT_ADD_REQ_STAT_SUCCESS);
            junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, 
                        "%s: success: add request with context: %u",
                        __func__,
                        SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg));
        } else {
            /* 
             * Update the patricia tree's status field using
             * the echoed client context.
             */
            rt_add_data_update((unsigned int)
                           SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg), 
                           RT_ADD_REQ_STAT_FAILURE);
            ERRMSG(JNX_ROUTESERVICED_SSD_REPLY_FAIL, LOG_ERR,
                   "SSD replies with a failure status for"
                   "add route request ctx %u error %d",
                   SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg),
                   SSD_ROUTE_ADD_REPLY_GET_ERROR_CODE(cmsg));
        }
        /*
         * Delete the request structure from the queue of pending
         * requests
         */
        struct jnx_routeserviced_route_add_req *add_req;
        add_req = jnx_routeserviced_get_add_req_by_ctx((unsigned int)
                        SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg));
        if (add_req) {
            TAILQ_REMOVE(&route_add_req_list, add_req, entries);
            free(add_req);
        }

        break;

    case SSD_ROUTE_DELETE_REPLY:
        if (SSD_ROUTE_DELETE_REPLY_CHECK_STATUS(cmsg)) {
            junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, 
                        "%s: success: del request with context: %u",
                        __func__,
                        SSD_ROUTE_DELETE_REPLY_GET_CLIENT_CTX(cmsg));

            rt_data_delete_entry_by_ctx(
			SSD_ROUTE_DELETE_REPLY_GET_CLIENT_CTX(cmsg));

        } else {
            ERRMSG(JNX_ROUTESERVICED_SSD_REPLY_FAIL, LOG_ERR,
                   "SSD replies with a failure status for"
                   "del route request ctx %u error %d",
                   SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg),
                   SSD_ROUTE_ADD_REPLY_GET_ERROR_CODE(cmsg));
        }
        /*
         * Delete the request structure from the queue of pending
         * requests
         */
        struct jnx_routeserviced_route_del_req *del_req;
        del_req = jnx_routeserviced_get_del_req_by_ctx((unsigned int)
                        SSD_ROUTE_DELETE_REPLY_GET_CLIENT_CTX(cmsg));
        if (del_req) {
            TAILQ_REMOVE(&route_del_req_list, del_req, entries);
            free(del_req);
        }

        break;

    default:
        junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, 
                    "%s: SSD replies with unexpected message type",
                    __func__);
        break;
    }
    return;
}

/**
 * @brief
 *     Connects to the SDK Service Daemon (SSD)
 *     
 *  It populates the necessary socket address of the server, gets
 *  the routing table id and connects to the SDK Service Daemon
 *  using @a ssd_ipc_connect_rt_tbl.
 *
 *  @param[in] ev_ctx
 *      Event context to attach for ipc with ssd
 *
 *  @return Status of connections
 *      @li 0     success
 *      @li -1    connection failed
 */


static int
jnx_routeserviced_ssd_connect (evContext ev_ctx)
{
    int rt_table_id;
    int ret;

    /* 
     * Get routing table id 
     */
    rt_table_id = ssd_get_routing_instance();

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: got " 
                "route table id as %d ", __func__, rt_table_id);

    /*
     * Connect to the SSD server and hope for a nice server
     * file decsriptor
     */
    ret = ssd_ipc_connect_rt_tbl(NULL, &jnx_routeserviced_ssd_ft,
                                 rt_table_id, ev_ctx);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief
 *     Initializes data structures and connects to SSD server.
 *
 * Initializes the queues for route add and delete requests. Also,
 * connects to the SSD server and return the appropriate status.
 *
 * @param[in] ev_ctx
 *     Event context to attach for ipc with ssd
 *
 * @return Status of initialization
 *     @li 0    success
 *     @li -1   failure
 */

int
jnx_routeserviced_ssd_init (evContext ev_ctx)
{
    int error;
    int dummy;

    /*
     * Make sure that we do not re-initialize some data when
     * SSD closed the connection
     */
    if (jnx_routeserviced_state != JNX_RS_STATE_SSD_DISCONNECTED) {
        /*
         * Initialize the tail queue for route requests to SSD
         */
        TAILQ_INIT(&route_add_req_list);
        TAILQ_INIT(&route_del_req_list);

        /*
         * Set the appropriate state if this application has a 
         * client-id in GENCFG
         */
        error = jnx_routeserviced_gencfg_get_client_id(&dummy);
        if (error != 0) {
            jnx_routeserviced_state = JNX_RS_STATE_RESTARTING;
        }
    }

    /* 
     * Check if connection to ssd success, otherwise exit
     */
    error = jnx_routeserviced_ssd_connect(ev_ctx);
    if (error != 0){
        return -1;
    }

    return 0;
}

/**
 * @brief
 *     Wrapper for SSD reconnection routine
 *
 * @param[in] ev_ctx
 *     Event context to attach for ipc with ssd
 */
void 
jnx_routeserviced_ssd_reconnect (evContext ev_ctx, 
                                 void *uap __unused,
                                 struct timespec due __unused,
                                 struct timespec inter __unused)
{
    int error;

    error = jnx_routeserviced_ssd_init(ev_ctx);
    if (error != 0) {
        ERRMSG(JNX_ROUTESERVICED_SSD_CONN_FAIL, LOG_ERR,
               "connection failure with SSD with return code %d", error);
    }
}


/*
 * $Id: jnx-routeserviced_ssd.cc 346460 2009-11-14 05:06:47Z ssiano $
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
 *     jnx-routeserviced_ssd.cc
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

#include <memory>

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
#include <jnx/junos_init.h>
#include <jnx/patricia.h>

#include JNX_CC_ROUTESERVICED_OUT_H

using namespace std;
using namespace junos;

#include "jnx-routeserviced_ssd.h"
#include "jnx-routeserviced_config.h"

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
    jnx_routeserviced_ssd::Instance().set_server_fd(fd);

    if (fd <= 0) {
        jnx_routeserviced_ssd::Instance().set_reconnect_timer();

        junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: ",
                    "SSD connection failed, shall retry in few "
                    "seconds ...", __func__);

        return -1;
    }

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                " connected to SSD server with fd %d",
                __func__,  fd);

    /* 
     * Request for unique client id
     */
    if (jnx_routeserviced_ssd::Instance().request_client_id() < 0) {
        return -1;
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
    jnx_routeserviced_ssd::Instance().set_server_fd(0);
    jnx_routeserviced_ssd::Instance().set_state_ssd_disconnected();
    jnx_routeserviced_ssd::Instance().set_reconnect_timer();

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                "SSD closed connection with cause %d, shall retry in few "
                "seconds ...", __func__, cause);

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
    /*
     * Fetch the instances
     */
    jnx_routeserviced_config& rs_cfg = jnx_routeserviced_config::Instance();
    jnx_routeserviced_ssd& rs_ssd = jnx_routeserviced_ssd::Instance();

    switch (cmsg->ssd_ipc_cmd) {
    case SSD_SESSION_ID_REPLY:
        if (SSD_SESSION_ID_REPLY_CHECK_STATUS(cmsg)) {
            switch (SSD_SESSION_ID_REPLY_GET_STATUS(cmsg)) {
            case SSD_SESSION_SUCCESS:
                rs_ssd.set_client_id(SSD_SESSION_ID_REPLY_GET_CLIENT_ID(cmsg));
                junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                            "received client id from SSD: %u",
                            __func__, rs_ssd.get_client_id());

                /*
                 * Push the client-id in the gencfg store
                 */
                rs_ssd.gencfg_update_client_id(rs_ssd.get_client_id());

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
                /*
                 * SSD has restored the previously assigned client-id
                 */
                rs_ssd.process_client_id_restore_msg();

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
                mark_pending_rt_add_requests(rs_cfg);
                
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
            rs_ssd.set_rtt_id(SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_RTTID(cmsg));
            junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                        "received routing table id as %u", 
                        __func__, rs_ssd.get_rtt_id());
            /*
             * See if we have any pending route add requests and send those
             */
            send_pending_rt_add_requests(rs_cfg);
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
            config_data_update_status(rs_cfg, (unsigned int)
                                      SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg), 
                                      rs_ssd.get_success_status());
            junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, 
                        "%s: success: add request with context: %u",
                        __func__, SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg));
        } else {
            /* 
             * Update the patricia tree's status field using
             * the echoed client context.
             */
            config_data_update_status(rs_cfg, (unsigned int)
                                      SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg), 
                                      rs_ssd.get_failure_status());
            ERRMSG(JNX_ROUTESERVICED_SSD_REPLY_FAIL, LOG_ERR, 
                   "SSD replies with a failure status for %s with context %d", 
                   "add route request", SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg));
        }
        /*
         * Delete the request structure from the queue of pending
         * requests
         */
        rs_ssd.deque_route_add_req_by_ctx((unsigned int) 
                                  SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg));
        break;

    case SSD_ROUTE_DELETE_REPLY:
        if (SSD_ROUTE_DELETE_REPLY_CHECK_STATUS(cmsg)) {
            junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, 
                        "%s: success: del request with context: %u",
                        __func__,
                        SSD_ROUTE_DELETE_REPLY_GET_CLIENT_CTX(cmsg));
        } else {
            ERRMSG(JNX_ROUTESERVICED_SSD_REPLY_FAIL, LOG_ERR, 
                   "SSD replies with a failure status for %s with context %d",
                   "del route request",
                   SSD_ROUTE_DELETE_REPLY_GET_CLIENT_CTX(cmsg));
        }
        /*
         * Delete the request structure from the queue of pending
         * requests
         */
        rs_ssd.deque_route_del_req_by_ctx((unsigned int) 
                             SSD_ROUTE_DELETE_REPLY_GET_CLIENT_CTX(cmsg));
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
 *     Returns the reference of the unique instance
 */
jnx_routeserviced_ssd & 
jnx_routeserviced_ssd::Instance() {
    if (unique_instance.get() == 0) {
        unique_instance.reset(new jnx_routeserviced_ssd);
    }
    return *unique_instance;
}

/**
 * @brief
 *     Find and delete the route add request with a particular context
 *     
 * It run through the list of requests and frees up the memory for
 * the one with the matching context.
 *
 * @param[in] ctx
 *     Request's context
 */
void 
jnx_routeserviced_ssd::deque_route_add_req_by_ctx (unsigned int ctx) {
    struct rs_route_add_req *req;

    TAILQ_FOREACH(req, &route_add_queue, entries) {
        if (req->ctx == ctx) {
            TAILQ_REMOVE(&route_add_queue, req, entries);
            delete req;
            return;
        }
    }
}

/**
 * @brief
 *     Find and delete the route delete request with a particular context
 *     
 * It run through the list of requests and frees up the memory for
 * the one with the matching context.
 *
 * @param[in] ctx
 *     Request's context
 */
void 
jnx_routeserviced_ssd::deque_route_del_req_by_ctx (unsigned int ctx) {
    struct rs_route_del_req *req;

    TAILQ_FOREACH(req, &route_del_queue, entries) {
        if (req->ctx == ctx) {
            TAILQ_REMOVE(&route_del_queue, req, entries);
            delete req;
            return;
        }
    }
}


/**
 * @brief
 *     Add route routine
 *
 * The route params are populated and request sent over SSD ipc.
 * Many values are defaulted here to exemplify the usage.
 *
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
jnx_routeserviced_ssd::add_route (char *destination, 
                                  char *nhop, char *ifname,
                                  unsigned int prefixlen,
                                  unsigned int req_ctx)
{
    struct rs_route_add_req *ssd_add_req;
    jnx_routeserviced_config& rs_cfg = jnx_routeserviced_config::Instance();
    ifl_idx_t ifl_idx;
    int error;

    /*
     * Check whether we are connected to SSD
     */
    if (get_server_fd() == 0) {
        config_data_update_status(rs_cfg, req_ctx, RT_ADD_REQ_STAT_FAILURE);
        return;
    }

    /* 
     * Get the ifl structure of the logical interface
     */
    error = get_idx_by_iflname(ifname, &ifl_idx);
    if (error != 0) {
        ERRMSG(JNX_ROUTESERVICED_UTIL_ERR, LOG_ERR,
               "Util error: %s with return code %d",
               "could not get ifl index", error);

        config_data_update_status(rs_cfg, req_ctx, RT_ADD_REQ_STAT_FAILURE);

        return;
    }
    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: route add"
               " request: dest: %s, nhop: %s via %s with context %u",
                __func__,  destination, nhop, ifname, req_ctx);

    /* 
     * Allocate memory for route add
     */
    ssd_add_req = new rs_route_add_req;
    if (ssd_add_req == NULL) {
        ERRMSG(JNX_ROUTESERVICED_ALLOC_ERR, LOG_EMERG,
               "failed to allocate memory: %s", "new");

        config_data_update_status(rs_cfg, req_ctx, RT_ADD_REQ_STAT_FAILURE);

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
    if (get_prefix_by_ifl(ifl_idx) == NULL) {
        ERRMSG(JNX_ROUTESERVICED_UTIL_ERR, LOG_ERR,
               "Util error: %s", "could not get local address");

        config_data_update_status(rs_cfg, req_ctx, RT_ADD_REQ_STAT_FAILURE);

        /*
         * Free allocated structure before returning
         */
        delete ssd_add_req;
        return;
    }
    ssd_add_req->route_local.in.gin_addr = 
        inet_addr(get_prefix_by_ifl(ifl_idx));

    /* 
     * Prepare route paramater structure
     */
    bzero(&(ssd_add_req->rtp), sizeof(struct ssd_route_parms));
    ssd_add_req->rtp.rta_dest = &(ssd_add_req->route_addr);
    ssd_add_req->rtp.rta_prefixlen = prefixlen;
    ssd_add_req->rtp.rta_preferences.rtm_val[0] = 1;
    ssd_add_req->rtp.rta_preferences.rtm_type[0] = SSD_RTM_VALUE_PRESENT;
    ssd_add_req->rtp.rta_rtt = rtt_id;
    ssd_add_req->rtp.rta_flags = SSD_ROUTE_ADD_FLAG_OVERWRITE;
    ssd_add_req->rtp.rta_n_gw = 1;
    ssd_add_req->rtp.rta_gateway[0].rtg_gwaddr = &(ssd_add_req->route_nh);
    ssd_add_req->rtp.rta_gateway[0].rtg_ifl = ifl_idx;
    ssd_add_req->rtp.rta_gateway[0].rtg_lcladdr = &(ssd_add_req->route_local);

    /* 
     * Request to add route
     */
    if (ssd_request_route_add(server_fd, &(ssd_add_req->rtp), req_ctx)) {
        ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
               "call to : %s failed with error code %d",
               "ssd_request_route_add", errno);

        config_data_update_status(rs_cfg, req_ctx, RT_ADD_REQ_STAT_FAILURE);
        
        /*
         * Free allocated structure before returning
         */
        delete ssd_add_req;
        return;
    }

    ssd_add_req->ctx = req_ctx;
    enque_route_add_req(ssd_add_req);

    return;
}

/**
 * @brief
 *     Delete route routine
 *
 * The route params are populated and delete request sent over SSD ipc.
 *
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
jnx_routeserviced_ssd::del_route (char *destination, 
                                  char *nhop __unused, 
                                  char *ifname __unused,
                                  unsigned int prefixlen,
                                  unsigned int req_ctx)
{
    struct rs_route_del_req *ssd_del_req;

    /* 
     * Allocate memory for route delete 
     */
    ssd_del_req = new rs_route_del_req;
    if (ssd_del_req == NULL) {
        ERRMSG(JNX_ROUTESERVICED_ALLOC_ERR, LOG_EMERG,
               "failed to allocate memory: %s", "new");
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
    ssd_del_req->rtp.rtd_rtt = rtt_id;

    /* 
     * Request to delete route
     */
    if (ssd_request_route_delete(server_fd, &(ssd_del_req->rtp), req_ctx)) {
        ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
               "call to : %s failed with error code %d",
               "ssd_request_route_delete", errno);
        /*
         * Free allocated structure before returning
         */
        delete ssd_del_req;
        return; 
    }

    ssd_del_req->ctx = req_ctx;
    enque_route_del_req(ssd_del_req);

    return;
}

/**
 * Request client-id from SSD
 */
int 
jnx_routeserviced_ssd::request_client_id (void)
{
    int client_id_gencfg;

    if (jnx_routeserviced_state == JNX_RS_STATE_RESTARTING) {
        /*
         * jnx-routeserviced has restarted, fetch client-id from
         * JUNOS's GENCFG store
         */
        if (gencfg_get_client_id(&client_id_gencfg) == 0) {
            set_client_id(client_id_gencfg);
            junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                        " fetched client-id %d from GENCFG store",
                        __func__,  get_client_id());
        } else {
            /*
             * Error fetching client-id, default to no client-id
             */
            set_client_id(0);
        }

        if (ssd_get_client_id(get_server_fd(), get_client_id()) < 0) {
            ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
                   "call to : %s failed with error code %d",
                   "ssd_get_client_id", errno);

            return -1;
        }

    } else {

        /* 
         * Use original client_id if SSD had closed connection
         */
        if (ssd_get_client_id(get_server_fd(), get_client_id()) < 0) {
            ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
                   "call to : %s failed with error code %d",
                   "ssd_get_client_id", errno);

            return -1;
        }
    }

        return 0;
}

/**
 * Process client-id restore message from SSD
 */
void
jnx_routeserviced_ssd::process_client_id_restore_msg (void) 
{
    /*
     * Context for route table lookup API
     * TBD: Could be changed in future when this application supports
     * multiple route tables
     */
    unsigned int rt_table_ctx = 2;
    if (jnx_routeserviced_state == JNX_RS_STATE_RESTARTING) { 
        /*
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
            if (ssd_request_route_table_lookup(get_server_fd(), rt_tb_name, 
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
    set_state_client_id_restored();
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


int
jnx_routeserviced_ssd::connect (evContext ev_ctx)
{
    struct sockaddr_in serv_addr;
    struct in_addr *addr;
    int rt_table_id;
    int ret;

    /* 
     * Get the SSD server IP address
     */
    addr = ssd_get_server_ip_addr();
    if (!addr) {
        ERRMSG(JNX_ROUTESERVICED_LIBSSD_ERR, LOG_ERR,
               "call to : %s failed with error code %d",
               "ssd_get_server_ip_addr", errno);
        return -1;
    }

    /* 
     * Set up the server address
     */
    bzero((char *)&serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = addr->s_addr;
    serv_addr.sin_port = htons(SSD_SERVER_PORT);

    /* 
     * Get routing table id 
     */
    rt_table_id = ssd_get_routing_instance();

    junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: got " 
                " route table id as %d ", __func__, rt_table_id);

    /*
     * Connect to the SSD server and hope for a nice server
     * file decsriptor
     */
    ret = ssd_ipc_connect_rt_tbl(&serv_addr, &jnx_routeserviced_ssd_ft,
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
jnx_routeserviced_ssd::init (evContext ev_ctx)
{
    int error;
    int dummy;

    /*
     * Bookkeeping
     */
    event_ctx = ev_ctx;

    /*
     * Initialize the tail queue for route requests to SSD
     * Make sure that we do not re-initialize some data when
     * SSD closed the connection
     */
    if (jnx_routeserviced_state != JNX_RS_STATE_SSD_DISCONNECTED) {

        TAILQ_INIT(&route_add_queue);
        TAILQ_INIT(&route_del_queue);

        /*
         * Initialize the GENCFG subsystem
         */
        gencfg_init();

        /*
         * Register SIGTERM
         */
        junos_sig_register(SIGTERM, 
                           jnx_routeserviced_ssd::gencfg_delete_client_id);

        error = gencfg_get_client_id(&dummy);
        if (error != 0) {
            set_state_restarting();
        }
    }

    /* 
     * Check if connection to ssd success, otherwise exit
     */
    error = connect(ev_ctx);
    if (error != 0){
        return -1;
    }

    return 0;
}

/**
 * @brief
 *     Set the timer to reconnect with SSD
 *
 * @param[in] ev_ctx
 *     Event context to attach for ipc with ssd
 */
void 
jnx_routeserviced_ssd::set_reconnect_timer (void)
{
    evSetTimer(event_ctx, jnx_routeserviced_ssd::reconnect, NULL, 
               evAddTime(evNowTime(), evConsTime(5,0)),
               evConsTime(0,0), NULL);
}

/**
 * @brief
 *     Wrapper for SSD reconnection routine
 *
 * @param[in] ev_ctx
 *     Event context to attach for ipc with ssd
 */
void 
jnx_routeserviced_ssd::reconnect (evContext ev_ctx, void *uap __unused,
                                  struct timespec due __unused,
                                  struct timespec inter __unused)
{
    int error;
    
    error = jnx_routeserviced_ssd::Instance().init(ev_ctx);
    if (error != 0) {
        ERRMSG(JNX_ROUTESERVICED_SSD_CONN_FAIL, LOG_ERR,
               "connection failure with SSD with return code %d", error);
    }
}


/**
 * @brief
 *     Get primary address associated with logical interface
 *
 * Looks up the logical interface name and returns the IPv4 
 * address in ASCII format, if found. 
 *
 * @param[in] idx 
 *     Logical interface index 
 * 
 * @return 
 *     Pointer to IPv4 address; NULL if not found
 */

const char *
jnx_routeserviced_ssd::get_prefix_by_ifl (ifl_idx_t idx) 
{
    kcom_ifa_t ifa;
    static char ifa_lprefix[KCOM_MAX_PREFIX_LEN];
    struct in_addr in;
    int error;
    
    /*
     * Invalid interface 
     */
    if (ifl_idx_t_getval(idx) == 0) {
        return NULL;
    }
    
    memset(ifa_lprefix, 0, sizeof(ifa_lprefix));

    /*
     * Get the local address associated with the interface
     */
    error = junos_kcom_ifa_get_first(idx, AF_INET, &ifa);
    if (error) {
        return NULL;
    }

    /*
     * Check if the local address length is non-zero
     */
    if (ifa.ifa_lplen && ifa.ifa_lprefix != NULL) {
        /*
         * Store the address in network byte order 
         */
        in.s_addr = *(u_long *) ifa.ifa_lprefix;

        /*
         * Convert the internet address in the ASCII '.' notation
         */
        strlcpy(ifa_lprefix, inet_ntoa(in), sizeof(ifa_lprefix));
        return ifa_lprefix;
    }

    return NULL;
}

/**
 * @brief
 *     Get primary address associated with logical interface
 *
 * Searches for the logical interface name and returns the IPv4 
 * address in ASCII, if found. 
 *
 * @note
 *     If the interface name doesnt contain a suffix of the subunit,
 *     then this routine assumes a subunit of 0
 *
 * @param[in] iflname 
 *     Pointer to the name of the logical interface
 * @param[out] idx
 *     IFL index which needs to be set
 * 
 * @return Status
 *     @li 0      success
 *     @li EINVAL Invalid arguments 
 *     @li ENOENT kcom returned error 
 */
int
jnx_routeserviced_ssd::get_idx_by_iflname (const char* iflname, 
                                          ifl_idx_t *idx) 
{
    kcom_ifl_t ifl;
    char *ifl_name_str;
    char *ifl_subunit_str;
    char name[KCOM_IFNAMELEN];
    if_subunit_t subunit;
    int error = 0;
    
    if (iflname == NULL) {
        return EINVAL;
    }
    
    /*
     * Copy it locally
     */
    if(strlcpy(name, iflname, sizeof(name)) > sizeof(name)) {
        return EINVAL;
    }

    /*
     * Split the string at the first '.' occurence, if any
     */
    ifl_name_str = strtok(name, ".");
    
    /*
     * If interface name has a subunit passed with it, extract it
     */
    ifl_subunit_str = strchr(name, '.');
    if (ifl_subunit_str) {
        subunit = (if_subunit_t) strtol(ifl_subunit_str + 1, NULL, 0);
    } else {
        subunit = (if_subunit_t) 0;
    }

    /*
     * KCOM api to get the IFl associated with the logical
     * interface.  Note: 'ifl_name_str' does not contain the subunit. 
     * Subunit is explicitly provided as another argument (here second).
     */
    error = junos_kcom_ifl_get_by_name(ifl_name_str, subunit, &ifl);
    if (error != KCOM_OK) {
        return ENOENT;
    }
    
    if (!idx) {
        return EINVAL;
    }
    /*
     * Set the ifl index number
     */
    idx->x = ifl.ifl_index.x;
    return 0;
}
/**
 * @brief
 *     Initialize the gencfg subsystem
 *
 * This API performs the initialization functions required in order
 * to use the JUNOS gencfg subsystem
 */
int 
jnx_routeserviced_ssd::gencfg_init (void) 
{
    int error;
    junos_kcom_gencfg_t gencfg_obj;

    bzero(&gencfg_obj, sizeof(junos_kcom_gencfg_t));
    gencfg_obj.opcode = JUNOS_KCOM_GENCFG_OPCODE_INIT;
    gencfg_obj.user_info_p = NULL;
    gencfg_obj.user_handler = NULL; 

    error = junos_kcom_gencfg((junos_kcom_gencfg_t *) &gencfg_obj);

    if (error) {
        ERRMSG(JNX_ROUTESERVICED_KCOM_INIT_FAIL, LOG_ERR, "%s: "
               "gencfg subsystem initialization failed with cause %d",
               __func__, errno);
        return error;
    }

    return 0;
}

/**
 * @brief
 *     Store client-id in the gencfg store
 * 
 * @param[in]
 *     client-id provided by SSD
 */
void 
jnx_routeserviced_ssd::gencfg_store_client_id (int id) 
{
    int error;

    junos_kcom_gencfg_t gencfg_obj;
    u_int32_t client_id_blob_key = JNX_ROUTESERVICED_CLIENT_ID_BLOB_KEY;

    bzero(&gencfg_obj, sizeof(junos_kcom_gencfg_t));

    gencfg_obj.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_ADD;
    gencfg_obj.blob_id  = JNX_ROUTESERVICED_CLIENT_ID_BLOB_ID;

    gencfg_obj.key.size = sizeof(u_int32_t);
    gencfg_obj.key.data_p = (void *) &client_id_blob_key;

    gencfg_obj.blob.size = sizeof(id);
    gencfg_obj.blob.data_p = (void *) &id;

    error = junos_kcom_gencfg((junos_kcom_gencfg_t *) &gencfg_obj);

    if (error) {
        ERRMSG(JNX_ROUTESERVICED_KCOM_INIT_FAIL, LOG_ERR, "%s: "
               "gencfg unable to store client-id with cause %d",
               __func__, errno);
    }
}

/**
 * @brief
 *     Fetch client-id from the gencfg store
 * 
 * @param[out] id
 *     Pointer to where client-id is going to be stored
 */
int 
jnx_routeserviced_ssd::gencfg_get_client_id (int *id) 
{
    int error;

    if (!id) {
        return -1;
    }

    junos_kcom_gencfg_t gencfg_obj;
    u_int32_t client_id_blob_key = JNX_ROUTESERVICED_CLIENT_ID_BLOB_KEY;

    bzero(&gencfg_obj, sizeof(junos_kcom_gencfg_t));

    gencfg_obj.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_GET;
    gencfg_obj.blob_id = JNX_ROUTESERVICED_CLIENT_ID_BLOB_ID;

    gencfg_obj.key.size = sizeof(u_int32_t);
    gencfg_obj.key.data_p = (void *) &client_id_blob_key;

    error = junos_kcom_gencfg((junos_kcom_gencfg_t *) &gencfg_obj);

    if (error) {
        ERRMSG(JNX_ROUTESERVICED_KCOM_INIT_FAIL, LOG_ERR, "%s: "
               "gencfg unable to get client-id with cause %d",
               __func__, errno);
        *id = 0;
        return -1;
    }

    /* 
     * Get the client-id from the blob
     */
    *id = *((u_int32_t *) (gencfg_obj.blob.data_p));

    return 0;
}

/**
 * @brief
 *     Delete client-id from GENCFG store
 */
void 
jnx_routeserviced_ssd::gencfg_delete_client_id (int id __unused) 
{
    int error;

    junos_kcom_gencfg_t gencfg_obj;
    u_int32_t client_id_blob_key = JNX_ROUTESERVICED_CLIENT_ID_BLOB_KEY;

    bzero(&gencfg_obj, sizeof(junos_kcom_gencfg_t));

    gencfg_obj.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_DEL;
    gencfg_obj.blob_id = JNX_ROUTESERVICED_CLIENT_ID_BLOB_ID;

    gencfg_obj.key.size = sizeof(u_int32_t);
    gencfg_obj.key.data_p = (void *) &client_id_blob_key;

    error = junos_kcom_gencfg((junos_kcom_gencfg_t *) &gencfg_obj);

    if (error) {
        ERRMSG(JNX_ROUTESERVICED_KCOM_INIT_FAIL, LOG_ERR, "%s: "
               "gencfg unable to delete client-id with cause %d",
               __func__, errno);
    }
}

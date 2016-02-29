/*
 * $Id: jnx-msprsmd_ssd.c 441535 2011-05-10 18:15:17Z rkasyap $
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file
 *     jnx-msprsmd_ssd.c
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
#include <stddef.h>
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
#include <jnx/pconn.h>
#include <jnx/name_len_shared.h>
#include <jnx/vrf_util_pub.h>

#include JNX_MSPRSM_OUT_H

#include "jnx-msprsmd.h"
#include "jnx-msprsmd_config.h"
#include "jnx-msprsmd_ssd.h"
#include "jnx-msprsmd_kcom.h"


/*
 * Global definitions
 */
typedef struct {
    /* ssd fields */
    nh_idx_t            nh_idx;
    ssd_sockaddr_un    *route_addr;
    u_int32_t           has_route;
} msprsmd_ssd_if_t;

#define SSD_CLIENT_CTX(msprsmd_if) (0xa + msprsmd_if)
#define SSD_MSPRSMD_IF(ctx) (ctx-0xa)


/*
 * Global variables
 */
extern const char *msprsmd_if_str[IF_MAX];


/*
 * Static Variables
 */
static char             ssd_rt_name[NAME_LEN_RT_TABLE]; /* Route table name */
static uint32_t         ssd_rt_id = 0;                  /* Route table ID */
static ssd_sockaddr_un  ssd_route_addr;                 /* Routing address */
static int              ssd_fd = -1;                    /* SSD socket */
static msprsmd_ssd_if_t ssd_if[IF_MAX];


/*
 * Forward declarations
 */
static int msprsmd_ssd_connection (int fd);
static void msprsmd_ssd_connection_close (int fd, int cause);
static void msprsmd_ssd_msg (int fd, struct ssd_ipc_msg *cmsg);


/*
 * Function table handler instance for communication
 */
static struct ssd_ipc_ft msprsmd_ssd_ft = {
    msprsmd_ssd_connection,       /**< Handler for a conn
                                       reply from server */
    msprsmd_ssd_connection_close, /**< Handler for a conn
                                       close decree from
                                       server */
    msprsmd_ssd_msg               /**< Handler for messages
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
msprsmd_ssd_connection (int fd)
{
    junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                "connected to SSD server with fd %d",
                __func__,  fd);

    /*
     * Request for unique client id
     */
    if (ssd_get_client_id(fd, 0) < 0) {

        ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR, "%s: "
               "ssd_get_client_id() returned -1, errno is %s",
               __func__, strerror(errno));
    }

    return 0;
}


/**
 * @brief
 *     Handles connection close decree by server
 *
 * This function is called as a callback when the handler was registered
 * in @a ssd_ipc_connect_rt_tbl. Resets the SSD fd
 * stored in @msprsmd_base
 *
 */
static void
msprsmd_ssd_connection_close (int fd __unused, int cause)
{
    junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                "connection closed by server with cause %d",
                __func__, cause);
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
msprsmd_ssd_msg (int fd, struct ssd_ipc_msg *cmsg)
{
    int client_ctx;

    /*
     * See if this message came over the current ssd socket
     */
    if (fd != ssd_fd) {
        junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                    "got old fd=%d, ssd_fd=%d",
                    __func__, fd, ssd_fd);
        return;
    }

    /*
     * Context for route table lookup API
     */
    switch (cmsg->ssd_ipc_cmd) {
    case SSD_SESSION_ID_REPLY:
        if (SSD_SESSION_ID_REPLY_CHECK_STATUS(cmsg)) {
            junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                        "received client id from SSD: %u",
                        __func__,
                        SSD_SESSION_ID_REPLY_GET_CLIENT_ID(cmsg));
            /*
             * Request to setup route service
             */
            if (ssd_setup_route_service(fd) < 0) {

                ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR, "%s: "
                   "%s() failed with error code %s",
                   __func__, "ssd_setup_route_service", strerror(errno));

                return;
            }
        } else {
            ERRMSG(JNX_MSPRSMD_SSD_REPLY_FAIL, LOG_ERR, "%s: "
                   "ssd replies with failure status for "
                   "get_client_id request",
                   __func__);
            return;
        }
        break;

    case SSD_ROUTE_SERVICE_REPLY:
        if (!SSD_ROUTE_SERVICE_REPLY_CHECK_STATUS(cmsg)) {
            ERRMSG(JNX_MSPRSMD_SSD_REPLY_FAIL, LOG_ERR, "%s: "
                   "ssd replies with failure status for "
                   "setup route service request", __func__);

        } else {
            char *rt_name;

            junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                        "route service has been setup successfully",
                        __func__);

            rt_name = strdup(ssd_rt_name);
            if (!rt_name) {
                ERRMSG(JNX_MSPRSMD_ALLOC_ERR, LOG_EMERG, "%s: "
                       "failed to allocate memory: %s",
                       __func__, "strdup");
                return;

            } else {
                /*
                 * Request for a route table id
                 */
                if (ssd_request_route_table_lookup(fd, rt_name, 0) < 0) {

                    ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR, "%s: "
                           "%s() failed with error code %s",
                           __func__,
                           "ssd_request_route_table_lookup", strerror(errno));

                    return;
                }
            }
        }
        break;

    case SSD_ROUTE_TABLE_LOOKUP_REPLY:
        if (SSD_ROUTE_TABLE_LOOKUP_REPLY_CHECK_STATUS(cmsg)) {
            ssd_rt_id = SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_RTTID(cmsg);

            junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                        "received routing table id as %u",
                        __func__, ssd_rt_id);

            /*
             * Route service is set, we are ready to start switchover
             */
            msprsmd_kcom_start_switchover();

        } else {
            ERRMSG(JNX_MSPRSMD_SSD_REPLY_FAIL, LOG_ERR,
                   "ssd replies with a failure status for %s",
                   "route table lookup request");
            return;
        }
        break;

    case SSD_NH_ADD_REPLY:
        client_ctx = SSD_NEXTHOP_ADD_REPLY_GET_CLIENT_CTX(cmsg);
        if (SSD_NEXTHOP_ADD_REPLY_CHECK_STATUS(cmsg)) {

            int nh_idx = SSD_NEXTHOP_ADD_REPLY_GET_NH_ID(cmsg);

            junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                    "nexthop add success, "
                    "nexthop id is %d, client ctx is %d",
                    __func__, nh_idx, client_ctx);

            if ((client_ctx != SSD_CLIENT_CTX(IF_PRI)) &&
                (client_ctx != SSD_CLIENT_CTX(IF_SEC))) {

                ERRMSG(JNX_MSPRSMD_SSD_REPLY_FAIL, LOG_ERR,
                       "ssd sends %s with wrong client_ctx %d",
                       "SSD_NH_ADD_REPLY", client_ctx);
                return;
            }

            /*
             * Register this nexthop
             */
            nh_idx_t_setval(ssd_if[SSD_MSPRSMD_IF(client_ctx)].nh_idx,
                            nh_idx);

            /*
             * Now when the nexthop is here, add the route
             */
            msprsmd_ssd_add_route(SSD_MSPRSMD_IF(client_ctx));

            } else {

            ERRMSG(JNX_MSPRSMD_SSD_NH_ADD_FAIL, LOG_ERR, "%s: "
                   "ssd replies with a failure status for "
                   "adding next hop request, client ctx is %d",
                   __func__, client_ctx);
            return;
        }
        break;

    case  SSD_NH_DELETE_REPLY:
        client_ctx = SSD_NEXTHOP_DELETE_REPLY_GET_CLIENT_CTX(cmsg);
        if (SSD_NEXTHOP_DELETE_REPLY_CHECK_STATUS(cmsg)) {

            junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                    "nexthop delete success, "
                    "client ctx is %d",
                    __func__, client_ctx);

            if ((client_ctx != SSD_CLIENT_CTX(IF_PRI)) &&
                (client_ctx != SSD_CLIENT_CTX(IF_SEC))) {

                ERRMSG(JNX_MSPRSMD_SSD_REPLY_FAIL, LOG_ERR,
                       "ssd sends %s with wrong client_ctx %d",
                       "SSD_NH_DELETEREPLY", client_ctx);
                return;
            }

                /*
             * Nullify this nexthop
                 */
            nh_idx_t_setval(ssd_if[SSD_MSPRSMD_IF(client_ctx)].nh_idx, 0);

        } else {

            ERRMSG(JNX_MSPRSMD_SSD_NH_DELETE_FAIL, LOG_ERR, "%s: "
                   "ssd replies with a failure status for "
                   "adding next hop request, client ctx is %d",
                   __func__, client_ctx);
            return;
        }
        break;

    case SSD_ROUTE_ADD_REPLY:
        client_ctx = SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg);
        if (SSD_ROUTE_ADD_REPLY_CHECK_STATUS(cmsg)) {

            junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                    "route add success, client ctx is %d",
                    __func__, client_ctx);

            if ((client_ctx != SSD_CLIENT_CTX(IF_PRI)) &&
                (client_ctx != SSD_CLIENT_CTX(IF_SEC))) {

                ERRMSG(JNX_MSPRSMD_SSD_REPLY_FAIL, LOG_ERR,
                       "ssd sends %s with wrong client_ctx %d",
                       "SSD_ROUTE_ADD_REPLY", client_ctx);
                return;
            }

            ssd_if[SSD_MSPRSMD_IF(client_ctx)].has_route = 1;

        } else {

            ERRMSG(JNX_MSPRSMD_SSD_ROUTE_ADD_FAIL, LOG_ERR, "%s: "
                   "ssd replies with a failure status for "
                   "adding route request, client ctx is %d",
                   __func__, client_ctx);
        }
        break;

    case SSD_ROUTE_DELETE_REPLY:
        client_ctx = SSD_ROUTE_DELETE_REPLY_GET_CLIENT_CTX(cmsg);
        if (SSD_ROUTE_DELETE_REPLY_CHECK_STATUS(cmsg)) {
            junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                    "route delete success, client ctx is %d",
                    __func__, client_ctx);


            if ((client_ctx != SSD_CLIENT_CTX(IF_PRI)) &&
                (client_ctx != SSD_CLIENT_CTX(IF_SEC))) {

                ERRMSG(JNX_MSPRSMD_SSD_REPLY_FAIL, LOG_ERR,
                       "ssd sends %s with wrong client_ctx %d",
                       "SSD_ROUTE_DELETE_REPLY", client_ctx);
                return;
            }

            ssd_if[SSD_MSPRSMD_IF(client_ctx)].has_route = 0;

            /*
             * Now when there is no routes, remove the nexthop
             */
            msprsmd_ssd_del_nexthop(SSD_MSPRSMD_IF(client_ctx));


        } else {
            ERRMSG(JNX_MSPRSMD_SSD_ROUTE_ADD_FAIL, LOG_ERR, "%s: "
                   "ssd replies with a failure status for "
                   "deleting route request, client ctx is %d",
                   __func__, client_ctx);
        }
        break;

    default:
        junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                    "ssd replies with unexpected message type %d",
                    __func__, cmsg->ssd_ipc_cmd);
        break;
    }
    return;
}


/**
 * @brief
 *     Traverses the user config
 *
 *  Reads the config from the CLI, performs constrain checks
 *  and fills in module's internal data structures.
 *
 * @return 0 on success or error on failure
 */
static int
msprsmd_ssd_read_config(msprsmd_config_t *msprsmd_config)
{
    int error;
    int vrf_idx[IF_MAX];
    msprsmd_if_t msprsmd_if;
    char vrf_name[NAME_LEN_RT_TABLE];

    /*
     * Fill in ssd_route_addr
     */
    bzero(&ssd_route_addr, sizeof(ssd_sockaddr_un));
    ssd_setsocktype(&ssd_route_addr, SSD_GF_INET);
    ssd_route_addr.in.gin_addr = msprsmd_config->floating_ip.s_addr;

    /*
     * Fill in ssd_rt_name
     */
    for (msprsmd_if = IF_PRI; msprsmd_if < IF_MAX; msprsmd_if++) {

        vrf_idx[msprsmd_if] =
            vrf_getindexbyifname(msprsmd_config->ifname[msprsmd_if], AF_INET);

        if (vrf_idx[msprsmd_if] < 0) {

            ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR, "%s "
                    "Can't get VRF index in AF_INET interface=%s, error=%d",
                    __func__,
                    msprsmd_config->ifname[msprsmd_if], vrf_idx[msprsmd_if]);

            return vrf_idx[msprsmd_if];
        }
    }

    if (vrf_idx[IF_PRI] != vrf_idx[IF_SEC]) {

        ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR,
                "Interface %s and %s belong to different vrfs",
                msprsmd_config->ifname[IF_PRI],
                msprsmd_config->ifname[IF_SEC]);

        return EINVAL;
    }

    if (vrf_idx[IF_PRI]) {

        error = vrf_getvrfnamebyindex(vrf_idx[IF_PRI], AF_INET,
                                      vrf_name, NAME_LEN_RT_TABLE);
        if (error) {

            ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR,
                   "Can't get VRF name by index %d, error=%d",
                   vrf_idx[IF_PRI], error);

            return error;
        }

        strncpy(ssd_rt_name, vrf_name, sizeof(vrf_name));
        strcat(ssd_rt_name, ".inet.0");

    } else {

        strncpy(ssd_rt_name, "inet.0", sizeof(vrf_name));
    }

    junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                "%s interface is %s, %s interface is %s "
                "vrf_idx is %d, rt_tb_name is %s",
                __func__,
                msprsmd_if_str[IF_PRI], msprsmd_config->ifname[IF_PRI],
                msprsmd_if_str[IF_SEC], msprsmd_config->ifname[IF_SEC],
                vrf_idx[IF_PRI], ssd_rt_name);

    /* Nullify the nexthop and the route info */
    nh_idx_t_setval(ssd_if[IF_PRI].nh_idx, 0);
    nh_idx_t_setval(ssd_if[IF_SEC].nh_idx, 0);
    ssd_if[IF_PRI].has_route = 0;
    ssd_if[IF_SEC].has_route = 0;

    return 0;
}


/*
 * Exported functions
 */

/**
 * @brief
 *     Adds the service style nexthop
 *
 *  Sends nexthop add request for the given service interface.
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_ssd_add_nexthop (msprsmd_if_t msprsmd_if)
{
    struct ssd_nh_add_parms parms;
    int error = 0;

    /*
     * Make sure there is no nexthop yet
     */
    if (nh_idx_t_getval(ssd_if[msprsmd_if].nh_idx) != 0) {

        ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR,
               "interface %s: nextopd %d was already created",
               msprsmd_if_str[msprsmd_if],
               nh_idx_t_getval (ssd_if[msprsmd_if].nh_idx));
        return 0;
    }

    /*
     * Fill in ssd_nh_add_parms
     */
    memset(&parms, 0, sizeof(struct ssd_nh_add_parms));
    msprsmd_kcom_ifl_by_idx(msprsmd_if, &parms.ifl);
    parms.dst_flag = SSD_NEXTHOP_DST_FLAG_CTRL;
    parms.pkt_dist_type = SSD_NEXTHOP_PKT_DIST_FA;
    parms.af = SSD_GF_INET;

    /*
     * Send the request
     */
    error = ssd_request_nexthop_add(ssd_fd, SSD_CLIENT_CTX(msprsmd_if),
                                    &parms, SSD_CLIENT_CTX(msprsmd_if));
    if (error) {
        ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR,
               "call to : %s failed with error code %d",
               "ssd_request_nexthop_add", error);
    }

    return error;
}


/**
 * @brief
 *     Removes the service style nexthop
 *
 *  Sends nexthop delete request for the given service interface.
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_ssd_del_nexthop (msprsmd_if_t msprsmd_if)
{
    int error = 0;

    /*
     * Make sure we don't delete already deleted nexthops
     */
    if (nh_idx_t_getval(ssd_if[msprsmd_if].nh_idx) == 0) {
        ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR,
               "interface %s: nexthop was already deleted",
               msprsmd_if_str[msprsmd_if]);
        return 0;
    }

    /*
     * Send the request
     */
    error = ssd_request_nexthop_delete(ssd_fd, SSD_CLIENT_CTX(msprsmd_if),
                                       ssd_if[msprsmd_if].nh_idx,
                                       SSD_CLIENT_CTX(msprsmd_if));
    if (error) {
        ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR,
               "call to : %s failed with error code %d",
               "ssd_request_nexthop_delete", error);
    }

    return error;
}


/**
 * @brief
 *     Adds the route to the service PIC
 *
 *  Sends route add request for the given service interface
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_ssd_add_route (msprsmd_if_t msprsmd_if)
{
    struct ssd_route_parms rtp;
    int error;


    /*
     * Make sure there is no route yet
     */
    if (ssd_if[msprsmd_if].has_route != 0) {

        ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR,
               "interface %s: route was already created",
               msprsmd_if_str[msprsmd_if]);
        return 0;
    }

    /*
     * Fill in ssd_route_parms
     */
    bzero(&rtp, sizeof(rtp));
    rtp.rta_dest = &ssd_route_addr;
    rtp.rta_prefixlen = sizeof(struct in_addr) * NBBY;
    rtp.rta_nhtype = SSD_RNH_SERVICE;
    rtp.rta_preferences.rtm_val[IF_PRI] = 1;
    rtp.rta_preferences.rtm_type[IF_PRI] = SSD_RTM_VALUE_PRESENT;
    rtp.rta_gw_handle = 0;
    rtp.rta_n_gw = 1;

    rtp.rta_flags |= SSD_ROUTE_ADD_FLAG_PROXY_ARP;
    rtp.rta_rtt = ssd_rt_id;
    msprsmd_kcom_ifl_by_idx(msprsmd_if, &rtp.rta_gateway[IF_PRI].rtg_ifl);
    rtp.rta_gateway[IF_PRI].rtg_nhindex = ssd_if[msprsmd_if].nh_idx;

    error = ssd_request_route_add(ssd_fd, &rtp, SSD_CLIENT_CTX(msprsmd_if));

    junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                "ssd_request_route_add(ssd_fd=%d, %s) returned %d",
                __func__,
                ssd_fd, msprsmd_if_str[msprsmd_if], error);

    return error;
}


/**
 * @brief
 *     Deletes the route to the service PIC
 *
 *  Sends route delete request for the given service interface
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_ssd_del_route (msprsmd_if_t msprsmd_if)
{
    struct ssd_rt_delete_parms rtp;
    int error;

    /*
     * Make sure we don't delete already deleted nexthops
     */
    if (ssd_if[msprsmd_if].has_route == 0) {
        ERRMSG(JNX_MSPRSMD_LIBSSD_ERR, LOG_ERR,
               "interface %s: route was already deleted",
               msprsmd_if_str[msprsmd_if]);
        return 0;
    }

    /*
     * Fill in ssd_rt_delete_parms
     */
    bzero(&rtp, sizeof(rtp));
    rtp.rtd_dest = &ssd_route_addr;
    rtp.rtd_prefixlen = 32;
    rtp.rtd_rtt = ssd_rt_id;
    rtp.rtd_gw_handle = 0;

    error = ssd_request_route_delete(ssd_fd, &rtp, SSD_CLIENT_CTX(msprsmd_if));

    junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                "ssd_request_route_delete(ssd_fd=%d, %s) returned %d",
                __func__,
                ssd_fd, msprsmd_if_str[msprsmd_if], error);

    return error;
}


/**
 * @brief
 *     Connects to the SDK Service Daemon (SSD)
 *
 *  It reads the given config, closes old SSD session
 *  if one was in place, populates the necessary
 *  socket address of the server, gets the routing table id,
 *  connects to the SDK Service Daemon and adds nexthops
 *  for the configured interfaces
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_ssd_init (evContext ctx, msprsmd_config_t *msprsmd_config)
{
    int rt_table_id;
    int error;

    error = msprsmd_ssd_read_config (msprsmd_config);
    if (error) {
        return error;
    }

    /*
     * Get routing table id
     */
    rt_table_id = ssd_get_routing_instance();

    junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                "got route table id %d ", __func__, rt_table_id);

    /*
     * Connect to the SSD server and hope for a nice server
     * file decsriptor
     */
    msprsmd_ssd_shutdown();

    ssd_fd = ssd_ipc_connect_rt_tbl(NULL, &msprsmd_ssd_ft,
                                    rt_table_id, ctx);

    junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                "ssd_ipc_connect_rt_tbl() returned ssd_fd=%d",
                __func__, ssd_fd);

    if (ssd_fd < 0) {
        return ssd_fd;
    }

    return 0;
}


/**
 * @brief
 *     Disconnects from the SDK Service Daemon
 *
 *  Closes the session with SDK Service Daemon,
 *  wiping off all routing objects assosiated
 *  with this session
 *
 */
void
msprsmd_ssd_shutdown (void)
{
    /*
     * Disconnect from the SSD server
     */
    if (ssd_fd >= 0) {

        ssd_ipc_close(ssd_fd);

        junos_trace(JNX_MSPRSMD_TRACEFLAG_SSD, "%s: "
                    "ssd_ipc_close() closed ssd_fd=%d",
                    __func__, ssd_fd);

        ssd_fd = -1;
    }
}

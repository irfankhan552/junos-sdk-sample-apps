/*
 * $Id$
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file route-manager_ssd.c
 * @brief Relating to libssd API
 * 
 * These functions manage the connection to SSD and make all types of routing
 * requests.
 */

#include <stdbool.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>
#include <jnx/junos_trace.h>
#include <jnx/ssd_ipc.h>
#include <jnx/ssd_ipc_msg.h>
#include "route-manager.h"

#include OUT_H

extern nh_t nh_pool[];

/** Service ready flag. */
bool svc_ready;

/** SSD connection FD. */
static int ssd_fd;

/* SSD client ID. */
static int client_id;

/**
 * @brief
 * The callback when SSD connection is open.
 *
 * @param[in] fd
 *      Socket FD to SSD
 *
 * @return
 *      0 on success, -1 on failure
 */
static int
ssd_client_conn_open (int fd)
{
    ssd_fd = fd;
    RM_TRACE(TF_SSD, "%s: SSD connection is open.", __func__);

    /* Request SSD client ID. */
    ssd_get_client_id(fd, client_id);
    return 0;
}

/**
 * @brief
 * The callback when SSD connection is closed.
 *
 * @param[in] fd
 *      Socket FD to SSD
 */
static void
ssd_client_conn_close (int fd UNUSED, int cause)
{
    ssd_fd = -1;
    RM_TRACE(TF_SSD, "%s: SSD connection is closed by %d.", __func__, cause);
}

/**
 * @brief
 * The callback to handle reply messages.
 *
 * @param[in] fd
 *      Socket FD to SSD
 * @param[in] msg
 *      Pointer to the message
 */
static void
ssd_client_msg_hdlr (int fd UNUSED, struct ssd_ipc_msg *msg)
{
    route_t *rt;

    switch (msg->ssd_ipc_cmd) {
    case  SSD_SESSION_ID_REPLY:
        if (SSD_SESSION_ID_REPLY_CHECK_STATUS(msg)) {
            if (client_id == 0) {
                /* New session. */
                client_id = SSD_SESSION_ID_REPLY_GET_CLIENT_ID(msg);
                kcom_client_id_save(client_id);
                RM_TRACE(TF_SSD, "%s: Got client ID %d and saved it.",
                        __func__, client_id);

                /* Request for route service */
                ssd_setup_route_service(fd);
            } else {
                /* Restore session. */
                RM_TRACE(TF_SSD, "%s: Session is restored %d.", __func__,
                        client_id);
                svc_ready = true;
            }
        } else {
            RM_TRACE(TF_SSD, "%s: Request client ID %d ERROR!", __func__,
                    client_id);
        }
        break;

    case  SSD_ROUTE_SERVICE_REPLY:
        if (SSD_ROUTE_SERVICE_REPLY_CHECK_STATUS(msg)) {
            RM_TRACE(TF_SSD, "%s: Route service is ready.", __func__);
            svc_ready = true;
            config_rt_proc(NULL);
        } else {
            RM_TRACE(TF_SSD, "%s: Setup route service ERROR!", __func__);
        }
        break;

    case  SSD_ROUTE_TABLE_LOOKUP_REPLY:
        rt = (route_t *)SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_CLIENT_CTX(msg);
        if (SSD_ROUTE_TABLE_LOOKUP_REPLY_CHECK_STATUS(msg)) {
            RM_TRACE(TF_SSD, "%s: Routing table ID %d.", __func__,
                    SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_RTTID(msg));

            /* Update the routing table ID. */
            if (rt->ctx_id) {
                nh_pool[rt->ctx_id].idx =
                        SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_RTTID(msg);
                nh_pool[rt->ctx_id].op_state = NH_STATE_ADD_OK;
            } else {
                /* Should not get here. */
                RM_LOG(LOG_ERR, "%s: No route is waiting for this!", __func__);
            }
        } else {
            RM_TRACE(TF_SSD, "%s: Routing table ID lookup ERROR %d.", __func__,
                    SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_ERROR_CODE(msg));

            /* Update the route who requested this routing table ID. */
            rt->op_state = RT_STATE_ADD_ERR;
        }
        /* Contiue adding route. */
        config_rt_proc(rt);
        break;
    case  SSD_ROUTE_ADD_REPLY:
        rt = (route_t *)SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(msg);
        if (SSD_ROUTE_ADD_REPLY_CHECK_STATUS(msg)) {
            RM_TRACE(TF_SSD, "%s: Add route %08x OK.", __func__,
                    rt->dst_addr.in.gin_addr);
            rt->op_state = RT_STATE_ADD_OK;
        } else {
            RM_TRACE(TF_SSD, "%s: Add route %08x ERROR %d.", __func__,
                    rt->dst_addr.in.gin_addr,
                    SSD_ROUTE_ADD_REPLY_GET_ERROR_CODE(msg));
            rt->op_state = RT_STATE_ADD_ERR;
        }
        /* Contiue adding route. */
        config_rt_proc(rt);
        break;
    case  SSD_NH_ADD_REPLY:
        rt = (route_t *)SSD_NEXTHOP_ADD_REPLY_GET_CLIENT_CTX(msg);
        if (SSD_NEXTHOP_ADD_REPLY_CHECK_STATUS(msg)) {
            RM_TRACE(TF_SSD, "%s: Add next-hop %s OK %d.", __func__,
                    nh_pool[rt->ctx_id].name,
                    SSD_NEXTHOP_ADD_REPLY_GET_NH_ID(msg));
            nh_pool[rt->ctx_id].idx = SSD_NEXTHOP_ADD_REPLY_GET_NH_ID(msg);
            nh_pool[rt->ctx_id].op_state = NH_STATE_ADD_OK;
        } else {
            RM_TRACE(TF_SSD, "%s: Add next-hop %s ERROR %d.", __func__,
                    nh_pool[rt->ctx_id].name,
                    SSD_NEXTHOP_ADD_REPLY_GET_ERROR_CODE(msg));
            nh_pool[rt->ctx_id].op_state = NH_STATE_ADD_ERR;
            /* Adding next-hop failed, so adding route failed. */
            rt->op_state = RT_STATE_ADD_ERR;
        }
        /* Contiue adding route. */
        config_rt_proc(rt);
        break;

    case  SSD_ROUTE_DELETE_REPLY:
        rt = (route_t *)SSD_ROUTE_DELETE_REPLY_GET_CLIENT_CTX(msg);
        if (SSD_ROUTE_DELETE_REPLY_CHECK_STATUS(msg)) {
            RM_TRACE(TF_SSD, "%s: Delete route %08x OK.", __func__,
                    rt->dst_addr.in.gin_addr);
            rt->op_state = RT_STATE_DEL_OK;

        } else {
            RM_TRACE(TF_SSD, "%s: Delete route %08x ERROR %d.", __func__,
                    rt->dst_addr.in.gin_addr,
                    SSD_ROUTE_DELETE_REPLY_GET_ERROR_CODE(msg));
            rt->op_state = RT_STATE_DEL_ERR;
            /* Some junk may be left in the routing table.
             * can't continue to delete dependencies if there is any.
             */
        }
        /* Continue deleting route. */
        config_rt_proc(rt);
        break;

    case  SSD_NH_DELETE_REPLY:
        rt = (route_t *)SSD_NEXTHOP_DELETE_REPLY_GET_CLIENT_CTX(msg);
        if (SSD_NEXTHOP_DELETE_REPLY_CHECK_STATUS(msg)) {
            RM_TRACE(TF_SSD, "%s: Delete next-hop %s OK.", __func__,
                    nh_pool[rt->ctx_id].name);
        } else {
            RM_TRACE(TF_SSD, "%s: Delete next-hop %s ERROR %d.", __func__,
                    nh_pool[rt->ctx_id].name,
                    SSD_NEXTHOP_DELETE_REPLY_GET_ERROR_CODE(msg));
        }
        /* Free next-hop from local pool no matter it's deleted from kernel
         * successfully or not.
         */
        nh_pool[rt->ctx_id].op_state = NH_STATE_FREE;
        /* Continue deleting route. */
        config_rt_proc(rt);
        break;

    case SSD_ROUTE_SERVICE_CLEAN_UP_REPLY:
        if (SSD_ROUTE_SERVICE_CLEAN_UP_REPLY_CHECK_STATUS(msg)) {
            RM_TRACE(TF_SSD, "%s: Clean up routing service OK.", __func__);
        } else {
            RM_TRACE(TF_SSD, "%s: Clean up routing service ERROR.", __func__);
        }
        break;
    default:
        RM_TRACE(TF_SSD, "%s: Undefined message command %d.", __func__,
                msg->ssd_ipc_cmd);
    }
}

/** SSD client callbacks. */
static struct ssd_ipc_ft ssd_client_cbs = {
    ssd_client_conn_open,
    ssd_client_conn_close,
    ssd_client_msg_hdlr
};

/**
 * @brief
 * Setup parameters and call SSD API to add route.
 *
 * @param[in] rt
 *      Pointer to route
 *
 * @return
 *      0 on success, -1 on failure
 */
int
ssd_rt_add (route_t *rt)
{
    struct ssd_route_parms params;
    int i;

    bzero(&params, sizeof(params));
    params.rta_dest = &rt->dst_addr;
    params.rta_prefixlen = rt->prefix_len;
    params.rta_rtt = nh_pool[rt->rtt_id].idx;
    params.rta_nhtype = rt->nh_type;
    params.rta_state = rt->op_state;
    params.rta_flags = rt->flag;
    params.rta_preferences.rtm_val[0] = rt->preference;
    params.rta_preferences.rtm_type[0] = SSD_RTM_VALUE_PRESENT;
    params.rta_n_gw = rt->gw_num;
    for (i = 0; i < rt->gw_num; i++) {
        if (rt->gw_addr[i].in.gin_family == SSD_GF_INET) {
            params.rta_gateway[i].rtg_gwaddr = &rt->gw_addr[i];
        }
        if (rt->gw_ifl_addr[i].in.gin_family == SSD_GF_INET) {
            params.rta_gateway[i].rtg_lcladdr = &rt->gw_ifl_addr[i];
        }
        if (rt->gw_ifl_name[i][0] != '\0') {
            params.rta_gateway[i].rtg_iflname = rt->gw_ifl_name[i];
        }
        ifl_idx_t_setval(params.rta_gateway[i].rtg_ifl, rt->gw_ifl_idx);
        nh_idx_t_setval(params.rta_gateway[i].rtg_nhindex,
                nh_pool[rt->nh_id[i]].idx);
        RM_TRACE(TF_SSD, "%s: %d, %d, %d, %d, %d", __func__,
                rt->gw_addr[i].in.gin_family,
                rt->gw_ifl_addr[i].in.gin_family,
                rt->gw_ifl_name[i][0],
                rt->gw_ifl_idx,
                nh_pool[rt->nh_id[i]].idx);

    }
    if (ssd_request_route_add(ssd_fd, &params, (unsigned int)rt) < 0) {
        return -1;
    } else {
        return 0;
    }
}

/**
 * @brief
 * Setup parameters and call SSD API to delete route.
 *
 * @param[in] rt
 *      Pointer to route
 *
 * @return
 *      0 on success, -1 on failure
 */
int
ssd_rt_del (route_t *rt)
{
    struct ssd_rt_delete_parms params;

    bzero(&params, sizeof(params));
    params.rtd_dest = &rt->dst_addr;
    params.rtd_prefixlen = rt->prefix_len;
    params.rtd_rtt = nh_pool[rt->rtt_id].idx;

    if (ssd_request_route_delete(ssd_fd, &params, (unsigned int)rt) < 0) {
        return -1;
    } else {
        return 0;
    }
}

/**
 * @brief
 * Setup parameters and call SSD API to add next-hop.
 *
 * @param[in] rt
 *      Pointer to route
 * @param[in] nh_id
 *      Next-hop ID assigned by the client
 *
 * @return
 *      0 on success, -1 on failure
 */
int
ssd_nh_add (route_t *rt, int nh_id)
{
    struct ssd_nh_add_parms params;
    int ifl_idx;
    char *name;
    int rc;

    if (rt->rtt_id == nh_id) {
        /* Routing table ID. */
        name = strdup(nh_pool[nh_id].name);
        if (ssd_request_route_table_lookup(ssd_fd, name,
                (unsigned int)rt) < 0) {
            RM_LOG(LOG_ERR, "%s: Request routing table ID ERROR!");
            return -1;
        } else {
            RM_TRACE(TF_SSD, "%s: Request routing table %s ID OK.", __func__,
                    nh_pool[nh_id].name);
            return 0;
        }
    }

    switch (rt->nh_type) {
    case SSD_RNH_SERVICE:
        bzero(&params, sizeof(params));
        ifl_idx = kcom_ifl_get_idx_by_name(nh_pool[nh_id].name);
        if (ifl_idx < 0) {
            return -1;
        }
        ifl_idx_t_setval(params.ifl, ifl_idx);
        params.af = SSD_GF_INET;
        rc = ssd_request_nexthop_add(ssd_fd, nh_id, &params, (unsigned int)rt);
        break;
    case SSD_RNH_TABLE:
        name = strdup(nh_pool[nh_id].name);
        rc = ssd_request_route_table_lookup(ssd_fd, name, (unsigned int)rt);
        break;
    default:
        rc = -1;
    }
    return rc;
}

/**
 * @brief
 * Setup parameters and call SSD API to delete next-hop.
 *
 * @param[in] rt
 *      Pointer to route
 * @param[in] nh_id
 *      Next-hop ID assigned by the client
 *
 * @return
 *      0 on success, -1 on failure
 */
int
ssd_nh_del (route_t *rt, int nh_id)
{
    nh_idx_t idx;

    nh_idx_t_setval(idx, nh_pool[nh_id].idx);
    if (ssd_request_nexthop_delete(ssd_fd, nh_id, idx, (unsigned int)rt) < 0) {
        return -1;
    } else {
        return 0;
    }
}

/**
 * @brief
 * Close SSD connection.
 *
 */
void
ssd_close (void)
{
    ssd_clean_up_route_service(ssd_fd, 1);
    ssd_ipc_close(ssd_fd);
    ssd_fd = -1;
    svc_ready = false;
}

/**
 * @brief
 * Initialize SSD connection.
 *
 * @param[in] ev_ctx
 *      Event context
 *
 * @return
 *      0 on success, -1 on failure
 */
int
ssd_open (evContext ev_ctx)
{
    svc_ready = false;
    client_id = kcom_client_id_restore();
    if (client_id < 0) {
        client_id = 0;
    }
    ssd_fd = ssd_ipc_connect_rt_tbl(NULL, &ssd_client_cbs, 0, ev_ctx);
    if (ssd_fd < 0) {
        return -1;
    } else {
        return 0;
    }
}


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
 * @file route-manager_config.c
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions parse and load the configuration data.
 */

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <isc/eventlib.h>
#include <arpa/inet.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>
#include <jnx/ssd_ipc_msg.h>
#include <jnx/parse_ip.h>
#include <ddl/ddl.h>
#include <ddl/dax.h>
#include "route-manager.h"

#include OUT_H

extern bool svc_ready;

/** Next-hop pool. */
nh_t nh_pool[RM_NH_MAX];

/** The list head of routes. */
static LIST_HEAD(, route_s) rt_list;

/**
 * @brief
 * Get route by destination address and prefix length.
 *
 * @param[in] addr
 *      Destination address
 *
 * @param[in] prefix_len
 *      Prefix length
 *
 * @return
 *      Pointer to the route on success, NULL on failure
 */
route_t *
config_rt_get (in_addr_t addr, int prefix_len);
route_t *
config_rt_get (in_addr_t addr, int prefix_len)
{
    route_t *rt;

    LIST_FOREACH(rt, &rt_list, entry) {
        if (rt->dst_addr.in.gin_addr == addr && rt->prefix_len == prefix_len) {
            return rt;
        }
    }
    return NULL;
}

/**
 * @brief
 * Get next-hop ID by name, if it doesn't exist, allocate one.
 *
 * @param[in] name
 *      Next-hop IFL name
 *
 * @return
 *      Next-hop ID (> 0) on success, -1 on failure
 */
static int
config_nh_id_get (char *name)
{
    int i;
    int free_id = 0;

    /* 0 can not be used as next-hop ID. */
    for (i = 1; i < RM_NH_MAX; i++) {
        if (nh_pool[i].op_state == NH_STATE_FREE) {
            free_id = i;
        }
        if (strncmp(nh_pool[i].name, name, sizeof(nh_pool[i].name)) == 0) {
            break;
        }
    }
    if (i == RM_NH_MAX) {
        /* No match. */
        if (free_id) {
            /* There is free next-hop, take it. */
            strncpy(nh_pool[free_id].name, name,
                    sizeof(nh_pool[free_id].name));
            nh_pool[free_id].op_state = NH_STATE_ADD_PENDING;
            return free_id;
        } else {
            /* No spare slot. */
            return -1;
        }
    } else {
        /* Find the match. */
        return i;
    }
}

/**
 * @brief
 * Handler of dax_walk_list to read routes.
 *
 * @param[in] dwd
 *      Opaque dax data
 * @param[in] dop
 *      DAX Object Pointer
 * @param[in] action
 *      The action on the given object
 * @param[in] data
 *      User data passed to handler
 *
 * @return
 *      DAX_WALK_OK on success, DAX_WALK_ABORT on failure
 */
static int
config_rt_read (dax_walk_data_t *dwd, ddl_handle_t *dop, int action,
        void *data UNUSED)
{
    const char *gateways_config[] = { "gateway", NULL };
    ddl_handle_t *dop_gws = NULL;
    ddl_handle_t *dop_gw = NULL;
    char buf[RM_NAME_SIZE];
    in_addr_t addr;
    int prefix_len;
    int af;
    int i;
    route_t *rt;

    switch (action) {
    case DAX_ITEM_DELETE_ALL:
        /* All items were deleted. */
        RM_TRACE(TF_CONFIG, "%s: Delete all routes.", __func__);

        /* Mark all routes delete-pending. */
        LIST_FOREACH (rt, &rt_list, entry) {
            rt->op_state = RT_STATE_DEL_PENDING;
        }
        break;

    case DAX_ITEM_DELETE:
        /* One object was deleted. Identifier is the only attribute can be
         * retrieved, and it's always returned as a string.
         */
        if (!dax_get_stringr_by_dwd_ident(dwd, NULL, 0, buf, sizeof(buf))) {
            RM_LOG(LOG_ERR, "%s: Read deleted route ERROR!", __func__);
            break;
        }
        RM_TRACE(TF_CONFIG, "%s: Delete route.", __func__);
        af = AF_INET;
        if (parse_ipaddr(&af, buf, PIF_LEN, &addr, sizeof(addr), NULL,
                &prefix_len, NULL, NULL, 0, NULL, 0) == PARSE_OK) {
            rt = config_rt_get(addr, prefix_len);
            if (rt) {
                rt->op_state = RT_STATE_DEL_PENDING;
            } else {
                RM_LOG(LOG_ERR, "%s: Route 0x%08x/%d doesn't exist!",
                        __func__, addr, prefix_len);
            }
        } else {
            RM_LOG(LOG_ERR, "%s: Parse route %s ERROR!", __func__, buf);
        }
        break;

    case DAX_ITEM_CHANGED:
        /* This is a new object or changed object.
         * This action is for all configured objects in check mode.
         */
        rt = calloc(1, sizeof(*rt));
        INSIST_ERR(rt);

        /* Get mandatory route destination address. */
        if (!dax_get_stringr_by_name(dop, "destination", buf, sizeof(buf))) {
            dax_error(dop, "Read route destination ERROR!");
            goto ret_err;
        }
        af = AF_INET;
        if (parse_ipaddr(&af, buf, PIF_LEN, &addr, sizeof(addr), NULL,
                &prefix_len, NULL, NULL, 0, NULL, 0) != PARSE_OK) {
            dax_error(dop, "Parse route %s ERROR!", buf);
            goto ret_err;
        }
        ssd_setsocktype(&rt->dst_addr, SSD_GF_INET);
        rt->dst_addr.in.gin_addr = addr;
        rt->prefix_len = prefix_len;

        RM_TRACE(TF_CONFIG, "%s: Change route 0x%08x/%d.", __func__,
                rt->dst_addr.in.gin_addr, rt->prefix_len);

        /* Get mandatory route next-hop type. */
        if (!dax_get_ubyte_by_name(dop, "next-hop-type", &rt->nh_type)) {
            dax_error(dop, "Read route next-hop type ERROR!");
            goto ret_err;
        }

        /* Convert next-hop from DDL type to SSD type. */
        switch (rt->nh_type) {
        case RT_NH_TYPE_UNICAST:
            rt->nh_type = SSD_RNH_UNICAST;
            break;
        case RT_NH_TYPE_REJECT:
            rt->nh_type = SSD_RNH_REJECT;
            break;
        case RT_NH_TYPE_DISCARD:
            rt->nh_type = SSD_RNH_DISCARD;
            break;
        case RT_NH_TYPE_SERVICE:
            rt->nh_type = SSD_RNH_SERVICE;
            break;
        case RT_NH_TYPE_TABLE:
            rt->nh_type = SSD_RNH_TABLE;
            break;
        default:
            dax_error(dop, "Unkown route next-hop type %d!", rt->nh_type);
            goto ret_err;
        }
        RM_TRACE(TF_CONFIG, "%s: Change route next-hop type %d.", __func__,
                rt->nh_type);

        /* Get optional route preference. */
        if (!dax_get_uint_by_name(dop, "preference", &rt->preference)) {
            rt->preference = RM_RT_PREFERENCE_DEFAULT;
        }
        RM_TRACE(TF_CONFIG, "%s: Change route preference %d.", __func__,
                rt->preference);

        /* Get optional routing table name. */
        if (!dax_get_stringr_by_name(dop, "routing-table", buf, sizeof(buf))) {
            /* Set the default routing table name. */
            sprintf(buf, "inet.0");
        }
        rt->rtt_id = config_nh_id_get(buf);
        RM_TRACE(TF_CONFIG, "%s: Change route routing table %s %d.", __func__,
                buf, rt->rtt_id);

        /* Get optional route state. */
        if (dax_get_ushort_by_name(dop, "state", &rt->state)) {

            /* Convert route state from DDL type to SSD type. */
            switch (rt->state) {
            case RT_STATE_NO_INSTALL:
                rt->state = SSD_RTS_STATE_NOTINSTALL;
            case RT_STATE_NO_ADVERTISE:
                rt->state = SSD_RTS_STATE_NOADVISE;
            default:
                dax_error(dop, "Unkown route state %d!", rt->state);
                goto ret_err;
            }

            /* SSD_RTS_STATE_INTERIOR also needs to be set. */
            rt->state |= SSD_RTS_STATE_INTERIOR;

            RM_TRACE(TF_CONFIG, "%s: Change route state 0x%04x.", __func__,
                    rt->state);
        }

        /* Get optional route flag. */
        if (dax_get_ushort_by_name(dop, "flag", &rt->flag)) {

            /* Convert route flag from DDL type to SSD type. */
            switch (rt->flag) {
            case RT_FLAG_OVER_WRITE:
                rt->flag = SSD_ROUTE_ADD_FLAG_OVERWRITE;
            default:
                dax_error(dop, "Unkown route flag %d!", rt->flag);
                goto ret_err;
            }
            RM_TRACE(TF_CONFIG, "%s: Change route flag %d.", __func__,
                    rt->flag);
        }

        /* Get route gateways. */
        if (dax_get_object_by_path(dop, gateways_config, &dop_gws, FALSE)) {
            i = 0;
            while (dax_visit_container(dop_gws, &dop_gw)) {
                if (i >= SSD_ROUTE_N_MULTIPATH) {
                    dax_error(dop, "Too many gateways!");
                    goto ret_err;
                }
                if (dax_get_stringr_by_name(dop_gw, "address", buf,
                        sizeof(buf))) {
                    af = AF_INET;
                    if (parse_ipaddr(&af, buf, PIF_LENOPT, &addr, sizeof(addr),
                            NULL, NULL, NULL, NULL, 0, NULL, 0) != PARSE_OK) {
                        dax_error(dop, "Parse gateway address %s ERROR!",
                                __func__, buf);
                        goto ret_err;
                    }
                    ssd_setsocktype(&rt->gw_addr[i], SSD_GF_INET);
                    rt->gw_addr[i].in.gin_addr = addr;
                    RM_TRACE(TF_CONFIG, "%s: Gateway address 0x%08x.",
                            __func__, rt->gw_addr[i].in.gin_addr);
                }
                if (dax_get_stringr_by_name(dop_gw, "interface-name",
                        rt->gw_ifl_name[i], sizeof(rt->gw_ifl_name[i]))) {
                    RM_TRACE(TF_CONFIG, "%s: Gateway IFL name %s.", __func__,
                            rt->gw_ifl_name[i]);
                }
                if (dax_get_int_by_name(dop_gw, "interface-index",
                        &rt->gw_ifl_idx)) {
                    RM_TRACE(TF_CONFIG, "%s: Gateway IFL index %d.", __func__,
                            rt->gw_ifl_idx);
                }
                if (dax_get_stringr_by_name(dop_gw, "interface-address", buf,
                        sizeof(buf))) {
                    af = AF_INET;
                    if (parse_ipaddr(&af, buf, PIF_LENOPT, &addr, sizeof(addr),
                            NULL, NULL, NULL, NULL, 0, NULL, 0) != PARSE_OK) {
                        dax_error(dop, "Parse gateway IFL address %s ERROR!",
                                __func__, buf);
                        goto ret_err;
                    }
                    ssd_setsocktype(&rt->gw_ifl_addr[i], SSD_GF_INET);
                    rt->gw_ifl_addr[i].in.gin_addr = addr;
                    RM_TRACE(TF_CONFIG, "%s: Gateway IFL address 0x%08x.",
                            __func__, rt->gw_ifl_addr[i].in.gin_addr);
                }
                if (dax_get_stringr_by_name(dop_gw, "next-hop", buf,
                        sizeof(buf))) {
                    rt->nh_id[i] = config_nh_id_get(buf);
                    if (rt->nh_id[i] < 0) {
                        dax_error(dop, "Next-hop pool is full!");
                        goto ret_err;
                    }
                    RM_TRACE(TF_CONFIG, "%s: Next-hop name %s.", __func__,
                            buf);
                } else {
                    if (rt->nh_type == SSD_RNH_SERVICE ||
                            rt->nh_type == SSD_RNH_TABLE) {
                        dax_error(dop, "Next-hop is not configured!");
                        goto ret_err;
                    }
                }
                i++;
            }
            rt->gw_num = i;
            dax_release_object(&dop_gws);
        } else {
            if (rt->nh_type == SSD_RNH_UNICAST ||
                    rt->nh_type == SSD_RNH_SERVICE ||
                    rt->nh_type == SSD_RNH_TABLE) {
                dax_error(dop, "Gateway is not configured!");
                goto ret_err;
            }
        }
        rt->op_state = RT_STATE_ADD_PENDING;
        LIST_INSERT_HEAD(&rt_list, rt, entry);
        break;
    default:
        break;
    }
    return DAX_WALK_OK;

ret_err:
    if (dop_gws) {
        dax_release_object(&dop_gws);
    }
    if (dop_gw) {
        dax_release_object(&dop_gw);
    }
    if (rt) {
        free(rt);
    }
    return DAX_WALK_ABORT;
}

/**
 * @brief
 * Add next-hop.
 *
 * @param[in] rt
 *      Pointer to the route
 *
 * @return
 *      0 on success, -1 on failure
 */
static int
config_nh_add (route_t *rt)
{
    int i;

    RM_TRACE(TF_CONFIG, "%s", __func__);

    /* Add next-hop if it's needed. */
    for (i = 0; i < rt->gw_num; i++ ) {
        if (rt->nh_id[i] == 0) {
            /* No next-hop. */
            continue;
        }
        switch (nh_pool[rt->nh_id[i]].op_state) {
        case NH_STATE_ADD_PENDING:
            /* Next-hop is allocated, but not being added yet. */
            RM_TRACE(TF_CONFIG, "%s: Add next-hop %s", __func__,
                    nh_pool[rt->nh_id[i]].name);
            if (ssd_nh_add(rt, rt->nh_id[i]) >= 0) {
                rt->ctx_id = rt->nh_id[i];
                /* Add one next-hop at each time. */
                return 0;
            }
            RM_LOG(LOG_ERR, "%s: Add next-hop ERROR!");
            /* Fall through. */
        case NH_STATE_ADD_ERR:
            /* Tried to add next-hop or request routing table ID,
             * but failed, then free it.
             */
            nh_pool[rt->nh_id[i]].op_state = NH_STATE_FREE;
            /* Adding next-hop failed. */
            return -1;
        case NH_STATE_ADD_OK:
        default:
            break;;
        }
    }
    /* All next-hop are added. */
    rt->ctx_id = 0;
    return 0;
}

/**
 * @brief
 * Delete the next-hop.
 *
 * @param[in] rt
 *      Pointer to the route
 *
 * @return
 *      0 on success, -1 on failure
 */
static int
config_nh_del (route_t *rt)
{
    int i;

    /* See if any next-hop needs to be deleted. */
    for (i = 0; i < rt->gw_num; i++) {
        if (rt->nh_id[i] == 0) {
            /* No next-hop. */
            continue;
        }
        if (--nh_pool[rt->nh_id[i]].count > 0) {
            /* The next-hop is still refered by other routes. */
            continue;
        }
        switch (nh_pool[rt->nh_id[i]].op_state) {
        case NH_STATE_ADD_OK:
            /* The next-hop reference counter is 0. */
            if (ssd_nh_del(rt, rt->nh_id[i]) == 0) {
                /* Delete next-hop one at each time. */
                rt->ctx_id = rt->nh_id[i];
                return 0;
            }
            RM_LOG(LOG_ERR, "%s: Delete next-hop ERROR", __func__);
        case NH_STATE_DEL_ERR:
            /* Leave it in kernel, fall through. */
        case NH_STATE_DEL_OK:
            nh_pool[rt->nh_id[i]].op_state = NH_STATE_FREE;
        default:
            break;
        }
    }
    rt->ctx_id = 0;
    return 0;
}

/**
 * @brief
 * Delete the route.
 *
 * @param[in] rt
 *      Pointer to the route
 *
 * @return
 *      0 on success, -1 on failure
 */
int
config_rt_del (route_t *rt)
{
    if (!rt) {
        return -1;
    }

    switch (rt->op_state) {
    case RT_STATE_DEL_PENDING:
        if (ssd_rt_del(rt) < 0) {
            goto ret_err; 
        }
        break;
    case RT_STATE_DEL_OK:
        config_nh_del(rt);
        if (rt->ctx_id == 0) {
            /* No next-hop needs to be deleted. */
            return -1;
        }
    default:
        break;
    }
    return 0;

ret_err:
    rt->op_state = RT_STATE_DEL_ERR;
    return -1;
}

/**
 * @brief
 * Add the route.
 *
 * @param[in] rt
 *      Pointer to the route
 *
 * @return
 *      0 on success, -1 on failure
 */
int
config_rt_add (route_t *rt)
{
    if (!rt) {
        return -1;
    }

    /* The operation state is always add-pending. */
    /* Check routing table ID. */
    if (nh_pool[rt->rtt_id].idx == 0) {
        RM_TRACE(TF_CONFIG, "%s: Request routing table %s ID.", __func__,
                nh_pool[rt->rtt_id].name);
        /* Request routing table ID. */
        if (ssd_nh_add(rt, rt->rtt_id) < 0) {
            RM_LOG(LOG_ERR, "%s: Request routing table %s ID ERROR!",
                    __func__, nh_pool[rt->rtt_id].name);
            goto ret_err;
        }
        rt->ctx_id = rt->rtt_id;
        /* Request routing table ID, leave the rest to the reply callback. */
    } else {
        if (config_nh_add(rt) < 0) {
            /* Adding next-hop failed. */
            goto ret_err;
        }
        if (rt->ctx_id == 0) {
            /* All next-hops are added, go ahead to add the route. */
            if (ssd_rt_add(rt) < 0) {
                RM_LOG(LOG_ERR, "%s: Add route 0x%08x ERROR!", __func__,
                        rt->dst_addr.in.gin_addr);
                goto ret_err;
            }
        }
        /* Add next-hop, leave the rest to the reply handler. */
    }
    return 0;

ret_err:
    rt->op_state = RT_STATE_ADD_ERR;
    return -1;
}

/**
 * @brief
 * Walk through all routes and process them according to the state.
 *
 */
void
config_rt_proc (route_t *rt)
{
    int i;

    if (!rt) {
        /* Process the first route. */
        rt = LIST_FIRST(&rt_list);
        if (!rt) {
            RM_LOG(LOG_ERR, "%s: The route list is empty!", __func__);
            return;
        }
    }

proc_continue:
    RM_TRACE(TF_CONFIG, "%s: Process route %08x.", __func__,
            rt->dst_addr.in.gin_addr);
    switch (rt->op_state) {
    case RT_STATE_ADD_OK:
        /* Adding route is done. */
        /* Update next-hop reference count. */
        RM_TRACE(TF_CONFIG, "%s: Add route %08x OK.", __func__,
                rt->dst_addr.in.gin_addr);
        for (i = 0; i < rt->gw_num; i++) {
            nh_pool[rt->nh_id[i]].count++;
        }
        /* Fall through to add the next. */
    case RT_STATE_ADD_ERR:
        RM_TRACE(TF_CONFIG, "%s: Add route %08x ERROR.", __func__,
                rt->dst_addr.in.gin_addr);
        /* Keeping adding the next. */
        rt = LIST_NEXT(rt, entry);
        if (!rt) {
            /* All done. */
            break;
        }
        /* Fall through to add. */
    case RT_STATE_ADD_PENDING:
        /* Start or continue adding. */
        RM_TRACE(TF_CONFIG, "%s: Add route %08x.", __func__,
                rt->dst_addr.in.gin_addr);
        if (config_rt_add(rt) < 0) {
            goto proc_continue;
        }
        break;
    case RT_STATE_DEL_OK:
        /* Check next-hop if there is any. */
        if (config_rt_del(rt) >= 0) {
            /* There is next-hop to be deleted. */
            break;
        }
        /* No next-hop need to be deleted, fall through. */
    case RT_STATE_DEL_ERR:
        /* Free the route from local database. */
        LIST_REMOVE(rt, entry);
        free(rt);
        rt = LIST_FIRST(&rt_list);
        if (!rt) {
            /* All done. */
            break;
        }
        /* Fall through to delete. */
    case RT_STATE_DEL_PENDING:
        /* Start or continue deleting. */
        if (config_rt_del(rt) < 0) {
            goto proc_continue;
        }
        break;
    default:
        break;
    }
}

/**
 * @brief
 * Read configuration from the database.
 *
 * @param[in] check
 *      1 if this function being invoked because of a commit check
 *
 * @return
 *      0 on success, -1 on failure
 *
 * @note Do not use ERRMSG during config check normally.
 */
int
config_read (int check)
{
    const char *routes_config[] = { "sync", "route-manager", "routes", NULL };
    ddl_handle_t *dop = NULL;
    int rc = 0;

    RM_TRACE(TF_CONFIG, "%s: Read configuration.", __func__);
    if (dax_get_object_by_path(NULL, routes_config, &dop, FALSE)) {
        if (check) {
            /* In check mode, all objects are new to the spawned process
             * that doesn't have previous states. So with DAX_WALK_CHANGED,
             * the handler will be called for each configured object with
             * DAX_ITEM_CHANGED action. The handler won't be called for
             * deleted objects.
             */
            rc = dax_walk_list(dop, DAX_WALK_CHANGED, config_rt_read, NULL);
        } else {
            /* In SIGHUP mode, the handler will be called for each added
             * or changed object with DAX_ITEM_CHANGED action, for each
             * deleted object with DAX_ITEM_DELETED action.
             */
            rc = dax_walk_list(dop, DAX_WALK_DELTA, config_rt_read, NULL);
        }

        dax_release_object(&dop);
    }
    if (rc == DAX_WALK_OK) {
        RM_TRACE(TF_CONFIG, "%s: check %d, svc_ready %d.", __func__, check,
                svc_ready);
        if (!check && svc_ready) {
            config_rt_proc(NULL);
        }
        return 0;
    } else {
        return -1;
    }
}

/**
 * @brief
 * Clear local configuration.
 *
 */
void
config_clear (void)
{
    route_t *rt;

    while ((rt = LIST_FIRST(&rt_list))) {
        LIST_REMOVE(rt, entry);
        free(rt);
    }
}

/**
 * @brief
 * Initialize configuration
 *
 * @return
 *      0 on success, -1 on failure
 *
 * @note Do not use ERRMSG during config check normally.
 */
int
config_init (void)
{
    bzero(nh_pool, sizeof(nh_pool));
    return 0;
}


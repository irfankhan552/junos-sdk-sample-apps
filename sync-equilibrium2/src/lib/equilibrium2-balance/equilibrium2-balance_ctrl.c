/*
 * $Id: equilibrium2-balance_ctrl.c 397964 2010-09-05 21:41:25Z builder $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file equilibrium2-balance_ctrl.c
 * @brief Equilibrium II balance service control functions.
 *
 */

#include <sync/equilibrium2.h>
#include <sync/equilibrium2_svc.h>
#include "equilibrium2-balance.h"

#include <jnx/junos_kcom_mpsdk_cfg.h>
#include <jnx/mpsdk.h>
#include <jnx/msp_objcache.h>
#include <jnx/pconn.h>

static connect_state_t  connect_state;    /**< connection state */
static evTimerID        ev_timer_id;      /**< event timer ID */
static pconn_client_t   *client_hdl;      /**< pconn client handle */

/**
 * @brief
 * Update server group address.
 *
 * @param[in] group
 *      Pointer to the server group
 *
 * @param[in] addr
 *      The address
 */
static void
update_svr_group_addr (svr_group_t *group, in_addr_t addr)
{
    svr_addr_t *svr_addr;

    LIST_FOREACH(svr_addr, &group->group_addr_head, entry) {
        if (svr_addr->addr == addr) {
            /* Address exists, mark it new. */
            svr_addr->addr_new = true;
            return;
        }
    }

    /* New address, create address node and add it to the list. */
    svr_addr = msp_shm_alloc(ctrl_ctx->scc_shm, sizeof(*svr_addr));
    if (svr_addr == NULL) {
        msp_log(LOG_ERR, "%s: Allocate memory ERROR!", __func__);
        return;
    }
    svr_addr->addr = addr;
    svr_addr->addr_ssn_count = 0;
    svr_addr->addr_new = true;

    LIST_INSERT_HEAD(&group->group_addr_head, svr_addr, entry);
}

/**
 * @brief
 * Get server group by name.
 *
 * @param[in] name
 *      Server group name
 *
 * @return
 *      Pointer to the server group on success, NULL on failure
 */
static svr_group_t *
get_svr_group (char *name)
{
    svr_group_t *group;

    LIST_FOREACH(group, svr_group_head, entry) {
        if (strcmp(name, group->group_name) == 0) {
            break;
        }
    }
    return group;
}

/**
 * @brief
 * Process server group blob.
 *
 * @param[in] blob
 *      Pointer to the blob
 *
 * @param[in] op
 *      Operation to the blob
 */
static void
proc_svr_group_blob (void *blob, junos_kcom_gencfg_opcode_t op)
{
    blob_svr_group_set_t *blob_group_set = blob;
    blob_svr_group_t *blob_group;
    in_addr_t *blob_addr;
    svr_group_t *group, *group_tmp;
    svr_addr_t *addr, *addr_tmp;
    int i, j;

    NTOHS(blob_group_set->gs_count);
    svr_group_count = blob_group_set->gs_count;

    switch (op) {
    case JUNOS_KCOM_GENCFG_OPCODE_BLOB_ADD:
        msp_log(LOG_INFO, "%s: Add %d server groups.", __func__,
                svr_group_count);
        break;
    case JUNOS_KCOM_GENCFG_OPCODE_BLOB_DEL:
        msp_log(LOG_INFO, "%s: Delete %d server groups.", __func__,
                svr_group_count);

        /* Don't delete server group, always update server group list
         * when adding a new list.
         */
        return;
    default:
        msp_log(LOG_INFO, "%s: Ignore operation %d.", __func__, op);
        return;
    }

    /* Lock the list when updating it. */
    msp_spinlock_lock(&svr_group_lock);

    blob_group = blob_group_set->gs_group;
    for (i = 0; i < blob_group_set->gs_count; i++) {
        NTOHS(blob_group->group_addr_count);

        group = get_svr_group(blob_group->group_name);
        if (group == NULL) {

            /* A new group, create a group in shared memory. */
            group = msp_shm_alloc(ctrl_ctx->scc_shm, sizeof(*group));
            if (group == NULL) {
                msp_log(LOG_ERR, "%s: Allocate memory ERROR!", __func__);
                goto done;
            }
            strlcpy(group->group_name, blob_group->group_name,
                    sizeof(group->group_name));
            group->group_addr_count = 0;
            LIST_INIT(&group->group_addr_head);

            /* Add group to the list. */
            LIST_INSERT_HEAD(svr_group_head, group, entry);
        }
        group->group_addr_count = blob_group->group_addr_count;

        blob_addr = blob_group->group_addr;
        for (j = 0; j < blob_group->group_addr_count; j++) {
            msp_log(LOG_INFO, "%s: Update 0x%08x in group %s.",
                    __func__, *blob_addr, group->group_name);
            update_svr_group_addr(group, *blob_addr);
            blob_addr++;
        }
        blob_group = (blob_svr_group_t *)blob_addr;
    }

    /* Clear old addresses in server group list. */
    LIST_FOREACH_SAFE(group, svr_group_head, entry, group_tmp) {

        LIST_FOREACH_SAFE(addr, &group->group_addr_head, entry, addr_tmp) {
            if (addr->addr_new) {
                msp_log(LOG_INFO, "%s: Address 0x%08x is new/unchanged.",
                        __func__, addr->addr);
                addr->addr_new = false;
            } else {
                msp_log(LOG_INFO, "%s: Address 0x%08x is deleted.",
                        __func__, addr->addr);
                LIST_REMOVE(addr, entry);
                msp_shm_free(ctrl_ctx->scc_shm, addr);
            }
        }
        if (LIST_FIRST(&group->group_addr_head) == NULL) {
            msp_log(LOG_INFO, "%s: Group %s is deleted.",
                    __func__, group->group_name);

            /* All addresses are deleted in this group, delete it. */
            LIST_REMOVE(group, entry);
            msp_shm_free(ctrl_ctx->scc_shm, group);
        }
    }

done:
    msp_spinlock_unlock(&svr_group_lock);
}

/**
 * @brief
 * Process service-set blob.
 *
 * @param[in] blob
 *      Pointer to the blob
 *
 * @param[in] op
 *      Operation to the blob
 */
static void
proc_svc_set_blob (void *blob, junos_kcom_gencfg_opcode_t op)
{
    blob_svc_set_t *blob_ss = blob;
    blob_svc_set_t *policy = NULL;
    blob_rule_t *blob_rule;
    blob_term_t *blob_term;
    int rule, term;
    msp_policy_db_params_t policy_db_params;
    sp_svc_set_t *ss;

    NTOHS(blob_ss->ss_id);
    NTOHL(blob_ss->ss_svc_id);
    NTOHL(blob_ss->ss_gen_num);
    NTOHS(blob_ss->ss_size);
    NTOHS(blob_ss->ss_rule_count);

    msp_log(LOG_INFO, "%s: %s (%d) with %d rules.", __func__,
            blob_ss->ss_name, blob_ss->ss_id, blob_ss->ss_rule_count);

    blob_rule = blob_ss->ss_rule;
    for (rule = 0; rule < blob_ss->ss_rule_count; rule++) {
        NTOHS(blob_rule->rule_term_count);
        
        blob_term = blob_rule->rule_term;
        for (term = 0; term < blob_rule->rule_term_count; term++) {
            NTOHS(blob_term->term_match_port);
            blob_term++;
        }
        blob_rule = (blob_rule_t *)blob_term;
    }
    
    /* Setup policy-db parameters. */
    bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
    policy_db_params.handle = ctrl_ctx->policy_db_handle;
    policy_db_params.svc_set_id = blob_ss->ss_id;
    policy_db_params.svc_id = blob_ss->ss_svc_id;
    policy_db_params.plugin_id = balance_pid;
    strlcpy(policy_db_params.plugin_name, EQ2_BALANCE_SVC_NAME,
            sizeof(policy_db_params.plugin_name));

    switch (op) {
    case JUNOS_KCOM_GENCFG_OPCODE_BLOB_ADD:
        msp_log(LOG_INFO, "%s: Add policy.", __func__);
        msp_spinlock_lock(&svc_set_lock);

        /* Check active service-set. */
        ss = get_svc_set(blob_ss->ss_id, true);
        if (ss) {
            msp_log(LOG_ERR, "%s: Check active service-set ERROR!",
                    __func__);
            goto done;
        }

        /* Allocate memory from policy-db shared memory. */
        policy = msp_shm_alloc(ctrl_ctx->policy_shm_handle, blob_ss->ss_size);
        if (policy == NULL) {
            msp_log(LOG_ERR, "%s: Allocate memory ERROR!", __func__);
            goto done;
        }

        /* Copy blob to policy. */
        bcopy(blob_ss, policy, blob_ss->ss_size);

        /* Add service-set to the list and mark it active. */
        ss = calloc(1, sizeof(*ss));
        INSIST_ERR(ss != NULL);
        ss->ss_policy = policy;
        ss->ss_active = true;
        LIST_INSERT_HEAD(&svc_set_head, ss, entry);

        /* Check inactive service-set. */
        if (get_svc_set(blob_ss->ss_id, FALSE)) {
            
            /* Inactive service-set exists, there are sessions using this
             * old policy in the middle, the old policy was not detached from
             * service-set yet, so don't add new policy to policy-db.
             */
        } else {

            /* No inactive service-set, the old policy must have been detached
             * and deleted, add new policy to policy-db.
             */
            policy_db_params.policy_op = MSP_POLICY_DB_POLICY_ADD;
            policy_db_params.op.add_params.gen_num = blob_ss->ss_gen_num;
            policy_db_params.op.add_params.policy = policy;
            if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
                msp_log(LOG_ERR, "%s: Policy operation %d ERROR!",
                        __func__, policy_db_params.policy_op);
                del_svc_set(ss);
            }
        }
        break;
    case JUNOS_KCOM_GENCFG_OPCODE_BLOB_DEL:
        msp_log(LOG_INFO, "%s: Delete policy.", __func__);
        msp_spinlock_lock(&svc_set_lock);

        /* Check inactive service-set. */
        if (get_svc_set(blob_ss->ss_id, FALSE)) {
            msp_log(LOG_ERR, "%s: Inactive sevice-set exists!", __func__);
            goto done;
        }

        /* Check active service-set. */
        ss = get_svc_set(blob_ss->ss_id, true);
        if (ss == NULL) {
            msp_log(LOG_ERR, "%s: No active sevice-set to delete!",
                    __func__);
            goto done;
        }

        /* Check session counter. */
        if (ss->ss_ssn_count) {

            /* There are sesions using this service-set policy, to not break
             * thoes sessions, don't delete this service-set, just mark it
             * inactive. Don't detach it from policy-db either, so data handler
             * can keep receiving the rest packet of thoes sessions.
             * Any new session won't be accepted till all existing sessions
             * are closed and new policy is attached.
             */
            ss->ss_active = FALSE;
        } else {

            /* There is no session using this service-set policy,
             * detach policy from policy-db, free policy memory and
             * delete service-set.
             */
            policy_db_params.policy_op = MSP_POLICY_DB_POLICY_DEL;
            policy_db_params.op.del_params.gen_num = blob_ss->ss_gen_num;
            if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
                msp_log(LOG_ERR, "%s: Policy operation %d ERROR!",
                        __func__, policy_db_params.policy_op);
            }
            del_svc_set(ss);
        }
        break;
    default:
        msp_log(LOG_INFO, "%s: Ignore operation %d.", __func__, op);
    }

done:
    msp_spinlock_unlock(&svc_set_lock);
}

/**
 * @brief
 * Read gencfg configuration blob.
 *
 * @param[in] kcom_gencfg
 *     Pointer to kcom gencfg message
 */
static void
read_gencfg_blob (junos_kcom_gencfg_t *kcom_gencfg)
{
    config_blob_key_t *key;

    key = kcom_gencfg->key.data_p;
    msp_log(LOG_INFO, "%s: %s, tag: %d, key len %d, blob len %d",
            __func__, key->key_name, key->key_tag, kcom_gencfg->key.size,
            kcom_gencfg->blob.size);

    switch (key->key_tag) {
    case CONFIG_BLOB_SVC_SET:
        proc_svc_set_blob(kcom_gencfg->blob.data_p, kcom_gencfg->opcode);
        break;
    case CONFIG_BLOB_SVR_GROUP:
        proc_svr_group_blob(kcom_gencfg->blob.data_p, kcom_gencfg->opcode);
        break;
    default:
        msp_log(LOG_INFO, "%s: Ignore blob.", __func__);
    }
    JUNOS_KCOM_MPSDK_CFG_FREE(kcom_gencfg);
}

/**
 * @brief
 * Send server group info to the manager.
 */
static void
send_svr_group (void)
{
    svr_group_t *group;
    svr_addr_t *addr;
    char *msg;
    int len;
    msg_svr_addr_t *msg_addr;
    msg_svr_group_t *msg_group;
    uint32_t *msg_group_count;

    if (svr_group_count == 0) {
        msp_log(LOG_INFO, "%s: No server group.", __func__);
        return;
    }

    msp_spinlock_lock(&svr_group_lock);

    /* Get the total length of message. */
    /* The first 4 bytes is the number of server groups. */
    len = sizeof(uint32_t) + svr_group_count * sizeof(msg_svr_group_t);
    LIST_FOREACH(group, svr_group_head, entry) {
        len += group->group_addr_count * sizeof(msg_svr_addr_t);
    }

    /* Create message. */
    msg = calloc(1, len);
    INSIST_ERR(msg != NULL);

    /* The first two bytes is the number of server groups. */
    msg_group_count = (uint32_t *)msg;
    *msg_group_count = htonl(svr_group_count);

    /* Skip the first two bytes that is the number of server groups and
     * add all server groups and addresses to the message.
     */
    msg_group = (msg_svr_group_t *)(msg + sizeof(uint32_t));
    LIST_FOREACH(group, svr_group_head, entry) {
        strlcpy(msg_group->group_name, group->group_name,
                sizeof(msg_group->group_name));
        msg_group->group_addr_count = htons(group->group_addr_count);

        msg_addr = (msg_svr_addr_t *)msg_group->group_addr;
        LIST_FOREACH(addr, &group->group_addr_head, entry) {
            msg_addr->addr = addr->addr;
            msg_addr->addr_ssn_count = htons(addr->addr_ssn_count);
            msg_addr++;
        }
        msg_group = (msg_svr_group_t *)msg_addr;
    }
    msp_spinlock_unlock(&svr_group_lock);

    pconn_client_send(client_hdl, EQ2_BALANCE_MSG_SVR_GROUP, msg, len);
    free(msg);
}

/**
 * @brief
 * Client event handler.
 *
 * @param[in] client
 *      Pointer to the pconn client
 *
 * @param[in] event
 *      pconn event
 *
 * @param[in] cookie
 *      Pointer to the user data
 */
static void
client_event_hdlr (pconn_client_t *client UNUSED, pconn_event_t event,
        void *cookie UNUSED)
{
    switch (event) {
    case PCONN_EVENT_ESTABLISHED:
        msp_log(LOG_INFO, "%s: Connect to server OK.", __func__);
        connect_state = CONNECT_OK;
        break;
    case PCONN_EVENT_SHUTDOWN:
        msp_log(LOG_INFO, "%s: Connection to server is down.", __func__);
        connect_state = CONNECT_NA;
        break;
    case PCONN_EVENT_FAILED:
        msp_log(LOG_INFO, "%s: Connection to server FAILED.", __func__);
        connect_state = CONNECT_NA;
        break;
    default:
        msp_log(LOG_ERR, "%s: Unknown event %d.", __func__, event);
    }
}

/**
 * @brief
 * Client message handler.
 *
 * @param[in] client
 *      Pointer to the pconn session
 *
 * @param[in] msg
 *      Pointer to the IPC message
 *
 * @param[in] cookie
 *      Pointer to the user data
 *
 * @return
 *      0 always
 */
static status_t
client_msg_hdlr (pconn_client_t *client UNUSED, ipc_msg_t *msg UNUSED,
        void *cookie UNUSED)
{
    /* Don't care any message from the manager. */
    return 0;
}

/**
 * @brief
 * Connect to the manager.
 *
 * @param[in] ctx
 *      Pointer to the event context
 */
static void
connect_manager (evContext ctx)
{
    pconn_client_params_t param;

    bzero(&param, sizeof(param));
    param.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_RE;
    param.pconn_port = EQ2_MGMT_SERVER_PORT;
    param.pconn_num_retries = EQ2_CLIENT_RETRY;
    param.pconn_event_handler = client_event_hdlr;
    client_hdl = pconn_client_connect_async(&param, ctx, client_msg_hdlr, NULL);
    if (client_hdl) {
        connect_state = CONNECT_INPROGRESS;
        msp_log(LOG_INFO, "%s: Connecting to server...", __func__);
    } else {
        msp_log(LOG_ERR, "%s: Connect to server ERROR!", __func__);
    }
}

/**
 * @brief
 * Timer handler to upload service status to the manager.
 *
 * @param[in] ctx
 *      Event context
 *
 * @param[in] uap
 *      Pointer to user data
 *
 * @param[in] due
 *      Due time
 *
 * @param[in] inter
 *      Interval time
 */
static void
upload_status (evContext ctx, void *uap UNUSED, struct timespec due UNUSED,
        struct timespec inter UNUSED)
{
    if (connect_state == CONNECT_OK) {
        msp_log(LOG_INFO, "%s: Send server group info.", __func__);
        send_svr_group();
    } else if (connect_state == CONNECT_NA) {
        connect_manager(ctx);
    }
}

/**
 * @brief
 * Get service-set by service-set ID.
 *
 * There could be maximumly two service-sets with the same ID,
 * one is to be deleted, another is active.
 *
 * @param[in] id
 *      Service-set ID
 *
 * @param[in] active
 *      Activation flag, 'true' is active, 'false' is inactive
 *
 * @return
 *      Pointer to the service-set on success, NULL on failure
 */
sp_svc_set_t *
get_svc_set (uint16_t id, bool active)
{
    sp_svc_set_t *ss;

    LIST_FOREACH(ss, &svc_set_head, entry) {
        if ((ss->ss_policy->ss_id == id) && (ss->ss_active = active)){
            break;
        }
    }
    return ss;
}

/**
 * @brief
 * Delete a service-set by pointer.
 *
 * @param[in] ss
 *      Pointer to the service-set
 */
void
del_svc_set (sp_svc_set_t *ss)
{
    if (ss == NULL) {
        return;
    }
    LIST_REMOVE(ss, entry);
    msp_shm_free(ctrl_ctx->policy_shm_handle, ss->ss_policy);
    free(ss);
}

/**
 * @brief
 * Control event handler.
 *
 * @param[in] ctx
 *      Pointer to control context.
 *
 * @param[in] ev
 *      Control event.
 *
 * @return
 *      Control handler status code
 */
int
equilibrium2_balance_ctrl_hdlr (msvcs_control_context_t *ctx,
        msvcs_control_event_t ev)
{

    /* Save control context. */
    ctrl_ctx = ctx;

    switch (ev) {
    case MSVCS_CONTROL_EV_INIT:
        msp_log(LOG_INFO, "%s: MSVCS_CONTROL_EV_INIT", __func__);

        if (msvcs_plugin_resolve_event_class(EQ2_CLASSIFY_SVC_NAME,
                    EV_CLASS_CLASSIFY, &classify_ev_class) < 0) {
            msp_log(LOG_ERR, "%s: Resovle event class EV_CLASS_CLASSIFY ERROR!",
                    __func__);
        }

        msvcs_plugin_subscribe_data_events(balance_pid, classify_ev_class,
                MSVCS_GET_EVENT_MASK(EV_CLASSIFY_FIRST_PACKET));

        /* Create the head of server group list. */
        svr_group_head = msp_shm_alloc(ctrl_ctx->policy_shm_handle,
                sizeof(*svr_group_head));
        if (svr_group_head == NULL) {
            msp_log(LOG_ERR, "%s: Allocate memory ERROR!", __func__);
            break;
        }
        LIST_INIT(svr_group_head);
        svr_group_count = 0;
        msp_spinlock_init(&svc_set_lock);
        msp_spinlock_init(&svr_group_lock);
        connect_state = CONNECT_NA;

        /* Schedule sending status to manager. */
        evInitID(&ev_timer_id);
        if (evSetTimer(*ctx->scc_ev_ctxt, upload_status, NULL,
                evAddTime(evNowTime(), evConsTime(STATUS_UPDATE_INTERVAL, 0)),
                evConsTime(STATUS_UPDATE_INTERVAL, 0), &ev_timer_id) < 0) {
            msp_log(LOG_ERR, "%s: Schedule sending status ERROR!", __func__);
        }

        break;

    case MSVCS_CONTROL_EV_CFG_BLOB:
        msp_log(LOG_INFO, "%s: MSVCS_CONTROL_EV_CFG_BLOB", __func__);
        read_gencfg_blob(ctx->plugin_data);
        break;

    default:
        msp_log(LOG_ERR, "%s: Unknown event!", __func__);
    }
    return 0;
}


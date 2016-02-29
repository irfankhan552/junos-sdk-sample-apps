/*
 * $Id: equilibrium2-classify_ctrl.c 397964 2010-09-05 21:41:25Z builder $
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
 * @file equilibrium2-classify_ctrl.c
 * @brief Equilibrium II classify service control functions.
 *
 */

#include <sync/equilibrium2.h>
#include <sync/equilibrium2_svc.h>
#include "equilibrium2-classify.h"

#include <jnx/junos_kcom_mpsdk_cfg.h>
#include <jnx/mpsdk.h>
#include <jnx/msp_objcache.h>

/**
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
    blob_svc_set_t *blob_ss = (blob_svc_set_t *)blob;
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
    policy_db_params.plugin_id = classify_pid;
    strlcpy(policy_db_params.plugin_name, EQ2_CLASSIFY_SVC_NAME,
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
        if (get_svc_set(blob_ss->ss_id, false)) {

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
        if (get_svc_set(blob_ss->ss_id, false)) {
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
    return;
}

/**
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
    default:
        msp_log(LOG_INFO, "%s: Ignore blob.", __func__);
    }
    JUNOS_KCOM_MPSDK_CFG_FREE(kcom_gencfg);
}

/**
 * Get a service-set by service-set ID.
 * There could be maximumly two service-sets with the same ID,
 * one is to be deleted, another is active.
 *
 * @param[in] id
 *      Service-set ID
 *
 * @param[in] active
 *      Active flag, 'true' is active, 'false' is inactive
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
 * Delete a service-set by pointer.
 *
 * @param[in] ss
 *      Pointer to the service-set
 */
void
del_svc_set (sp_svc_set_t *ss)
{
    LIST_REMOVE(ss, entry);
    msp_shm_free(ctrl_ctx->policy_shm_handle, ss->ss_policy);
    free(ss);
}

/**
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
equilibrium2_classify_ctrl_hdlr (msvcs_control_context_t *ctx,
        msvcs_control_event_t ev)
{

    /* Save control context. */
    ctrl_ctx = ctx;

    switch (ev) {
    case MSVCS_CONTROL_EV_INIT:
        msp_log(LOG_INFO, "%s: MSVCS_CONTROL_EV_INIT", __func__);
        LIST_INIT(&svc_set_head);
        msp_spinlock_init(&svc_set_lock);
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


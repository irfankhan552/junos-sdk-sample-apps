/*
 * $Id: equilibrium2-balance_data.c 422753 2011-02-01 02:26:15Z thejesh $
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
 * @file equilibrium2-balance_data.c
 * @brief Equilibrium II balance service data functions.
 *
 */

#include <sync/equilibrium2.h>
#include <sync/equilibrium2_svc.h>
#include "equilibrium2-balance.h"

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/jnx/jbuf.h>
#include <jnx/mpsdk.h>
#include <jnx/msp_objcache.h>
#include <jnx/multi-svcs/msvcs_plugin.h>
#include <jnx/multi-svcs/msvcs_session.h>

static msvcs_data_context_t *data_ctx; /**< global copy of data context */

/**
 * Calculate IP header checksum.
 *
 * @param[in] buf
 *      Pointer to IP header
 *
 * @param[in] len
 *      Length of IP header
 *
 * @return
 *      The checksum with network order
 */
static uint16_t
ip_cksum (void *buf, int len)
{
    uint8_t *p = buf;
    uint32_t sum = 0;

    while (len > 1) {
        sum += (*p << 8) + *(p + 1);
        len -= 2;
        p += 2;
    }
    if (len == 1) {
        sum += (*p << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    sum = ~sum;
    return(htons((uint16_t)sum));
}

/** 
 * Calculate TCP checksum.
 *  
 * @param[in] len
 *      Length of TCP segment in byte
 *
 * @param[in] src_addr
 *      Pointer to the source IP address
 * 
 * @param[in] dst_addr
 *      Pointer to the destination IP address
 *
 * @param[in] buf
 *      Pointer to the TCP packet
 *  
 * @return
 *      The checksum with network order
 */ 
static uint16_t
tcp_cksum (uint16_t len, void *src_addr, void *dst_addr, void *buf)
{   
    uint32_t sum = 0;
    uint8_t *p = NULL;
    
    p = (uint8_t *)src_addr;
    sum += (*p << 8) + *(p + 1);
    sum += (*(p + 2) << 8) + *(p + 3);

    p = (uint8_t *)dst_addr;
    sum += (*p << 8) + *(p + 1);
    sum += (*(p + 2) << 8) + *(p + 3);

    sum += IPPROTO_TCP + len;

    p = (uint8_t *)buf;
    while (len > 1) {
        sum += (*p << 8) + *(p + 1);
        len -= 2;
        p += 2;
    }
    if (len == 1) {
        sum += (*p << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    sum = ~sum;
    return(htons((uint16_t)sum));
}

/**
 * @brief
 * Get server address from server group.
 *
 * @param[in] name
 *      Server group name
 *
 * @return
 *      Server address on success, 0 on failure
 */
static svr_addr_t *
get_svr_addr (char *name)
{
    svr_group_t *group;
    svr_addr_t *addr;
    svr_addr_t *min_addr = NULL;
    uint32_t min_count;

    LIST_FOREACH(group, svr_group_head, entry) {
        if (strcmp(group->group_name, name) != 0) {
            continue;
        }

        /* Preset the maximum value of uint32_t to get the minimum count. */
        min_count = 0xFFFFFFFF;
        min_addr = NULL;
        LIST_FOREACH(addr, &group->group_addr_head, entry) {
            if (addr->addr_ssn_count < min_count) {
                min_count = addr->addr_ssn_count;
                min_addr = addr;
            }
        }
        if (min_addr) {
            min_addr->addr_ssn_count++;
        }
        break;
    }
    return min_addr;
}

/**
 * @brief
 * Take action.
 *
 * @return
 *      Status code
 */
static int
take_action (void)
{
    ssn_f_action_t *f_action = NULL;
    ssn_r_action_t *r_action = NULL;
    struct jbuf *jb = (struct jbuf *)data_ctx->sc_pkt;
    struct ip *ip_hdr = jbuf_to_d(jb, struct ip *);
    struct tcphdr *tcp_hdr;

    tcp_hdr = (struct tcphdr *)(jbuf_to_d(jb, char *) + ip_hdr->ip_hl * 4);

    /* Get session action. */
    msvcs_session_get_ext_handle((msvcs_session_t *)data_ctx->sc_session,
            (uint8_t)balance_pid, (void **)&f_action, (void **)&r_action);

    if (jbuf_to_svcs_hdr(jb, struct jbuf_svcs_hdr).jb_svcs_hdr_flags &
            JBUF_SVCS_FLAG_DIR_FORWARD) {
        if (f_action) {
            msp_log(LOG_INFO, "%s: Forward action %x -> %x", __func__,
                    ip_hdr->ip_dst.s_addr, f_action->svr_addr->addr);
            ip_hdr->ip_dst.s_addr = f_action->svr_addr->addr;

            /* Recalculate IP header checksum. */
            ip_hdr->ip_sum = 0;
            ip_hdr->ip_sum = ip_cksum(ip_hdr, ip_hdr->ip_hl * 4);

            /* Recalculate TCP checksum. */
            tcp_hdr->th_sum = 0;
            tcp_hdr->th_sum = tcp_cksum(ip_hdr->ip_len - ip_hdr->ip_hl * 4,
                    &ip_hdr->ip_src.s_addr, &ip_hdr->ip_dst.s_addr, tcp_hdr);
        } else {
            msp_log(LOG_INFO, "%s: No action for forward flow!", __func__);
        }
    } else {
        if (r_action) {
            msp_log(LOG_INFO, "%s: Reverse action %x -> %x", __func__,
                    ip_hdr->ip_src.s_addr, r_action->addr);
            ip_hdr->ip_src.s_addr = r_action->addr;

            /* Recalculate IP header checksum. */
            ip_hdr->ip_sum = 0;
            ip_hdr->ip_sum = ip_cksum(ip_hdr, ip_hdr->ip_hl * 4);

            /* Recalculate TCP checksum. */
            tcp_hdr->th_sum = 0;
            tcp_hdr->th_sum = tcp_cksum(ip_hdr->ip_len - ip_hdr->ip_hl * 4,
                    &ip_hdr->ip_src.s_addr, &ip_hdr->ip_dst.s_addr, tcp_hdr);
        } else {
            msp_log(LOG_INFO, "%s: No action for reversed flow!", __func__);
        }
    }

    return MSVCS_ST_PKT_FORWARD;
}

/**
 * @brief
 * Process the first packet.
 *
 * @return
 *      Status code
 */
static int
first_pkt_proc (void)
{
    struct jbuf *jb = (struct jbuf *)data_ctx->sc_pkt;
    struct ip *ip_hdr;
    blob_svc_set_t *policy;
    blob_rule_t *rule;
    blob_term_t *term;
    svr_addr_t *addr = NULL;
    ssn_f_action_t *f_action;
    ssn_r_action_t *r_action;
    sp_svc_set_t *ss;
    int i, j;

    ip_hdr = jbuf_to_d(jb, struct ip *);

    /* Lock service-set till the end of first packet process,
     * cause session counter may be updated at the end.
     */
    msp_spinlock_lock(&svc_set_lock);

    /* If there is an inactive service-set policy, don't accept any new
     * session.
     */     
    if (get_svc_set(data_ctx->sc_sset_id, FALSE)) {
        msp_log(LOG_INFO, "%s: Inactive policy exists!", __func__);
        goto discard;
    }

    /* Get the active service-set policy. */
    ss = get_svc_set(data_ctx->sc_sset_id, TRUE);
    if (ss == NULL) {
        msp_log(LOG_ERR, "%s: No service set!", __func__);
        goto discard;
    }   
 
    policy = ss->ss_policy;
    if (policy == NULL) {
        msp_log(LOG_ERR, "%s: No policy!", __func__);
        goto discard;
    }

    /* Go through all rules to this service set and create action list.
     * Only one rule/action is supported for now.
     */
    rule = policy->ss_rule;
    for (i = 0; i < policy->ss_rule_count; i++) {
        term = rule->rule_term;
        for (j = 0; j < rule->rule_term_count; j++) {
            if (term->term_match_id == TERM_FROM_SVC_GATE) {
                if (ip_hdr->ip_dst.s_addr == term->term_match_addr) {
                    msp_spinlock_lock(&svr_group_lock);
                    addr = get_svr_addr(term->term_act_group_name);
                    msp_spinlock_unlock(&svr_group_lock);
                    break;
                }
            }
            term++;
        }
        rule = (blob_rule_t *)term;
    }

    /* Create action and attach it to the session. */
    if (addr) {
        /* Only one action (rule) for each direction is supported for now. */
        f_action = msp_shm_alloc(data_ctx->sc_shm, sizeof(ssn_f_action_t));
        INSIST_ERR(f_action != NULL);

        /* Change destination address to service gate address. */
        f_action->svr_addr = addr;

        r_action = msp_shm_alloc(data_ctx->sc_shm, sizeof(ssn_r_action_t));
        INSIST_ERR(r_action != NULL);

        /* Save the destination address as reverse path source address. */
        r_action->addr = ip_hdr->ip_dst.s_addr;

        msvcs_session_set_ext_handle((msvcs_session_t *)data_ctx->sc_session,
                (uint8_t)balance_pid, f_action, r_action);
        ss->ss_ssn_count++;
        msp_log(LOG_INFO, "%s: Forward action %x is created.", __func__, addr);
        msp_log(LOG_INFO, "%s: Reverse action %x is created.", __func__,
                ip_hdr->ip_dst.s_addr);
    }
    msp_spinlock_unlock(&svc_set_lock);
    return MSVCS_ST_PKT_FORWARD;

discard:
    msp_spinlock_unlock(&svc_set_lock);
    return MSVCS_ST_PKT_DISCARD;
}

/**
 * @brief
 * Free related resource when session was closed.
 */
static void
session_close (void)
{
    ssn_f_action_t *f_action = NULL;
    ssn_r_action_t *r_action = NULL;
    sp_svc_set_t *ss;
    msp_policy_db_params_t policy_db_params;

    /* Get and free attached actions. */
    msvcs_session_get_ext_handle((msvcs_session_t *)data_ctx->sc_session,
            (uint8_t)balance_pid, (void **)&f_action, (void **)&r_action);

    msp_spinlock_lock(&svr_group_lock);
    f_action->svr_addr->addr_ssn_count--;
    msp_spinlock_unlock(&svr_group_lock);
    msp_shm_free(data_ctx->sc_shm, f_action);
    msp_shm_free(data_ctx->sc_shm, r_action);

    msp_spinlock_lock(&svc_set_lock);

    /* Get inactive service-set. */
    ss = get_svc_set(data_ctx->sc_sset_id, FALSE);
    if (ss == NULL) {
        /* No inactive service-set, get active service-set and decrement
         * session counter.
         */
        ss = get_svc_set(data_ctx->sc_sset_id, TRUE);
        if (ss == NULL) {
            msp_log(LOG_ERR, "%s: No active service-set!", __func__);
            return;
        }
        ss->ss_ssn_count--;
        goto done;
    }

    /* Inactive service-set exists, this session must belong to it. */
    ss->ss_ssn_count--;
    if (ss->ss_ssn_count == 0) {

        /* No session is using this service-set, detach policy from
         * policy-db, free policy and delete service-set.
         */
        bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
        policy_db_params.handle = ctrl_ctx->policy_db_handle;
        policy_db_params.svc_set_id = ss->ss_policy->ss_svc_id;
        policy_db_params.svc_id = ss->ss_policy->ss_svc_id;
        policy_db_params.plugin_id = balance_pid;
        strlcpy(policy_db_params.plugin_name, EQ2_BALANCE_SVC_NAME,
                sizeof(policy_db_params.plugin_name));
        policy_db_params.policy_op = MSP_POLICY_DB_POLICY_DEL;
        policy_db_params.op.del_params.gen_num = ss->ss_policy->ss_gen_num;
        if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
            msp_log(LOG_ERR, "%s: Policy operation %d ERROR!",
                    __func__, policy_db_params.policy_op);
        }
        del_svc_set(ss);

        /* Check active service-set. */
        ss = get_svc_set(data_ctx->sc_sset_id, TRUE);
        if (ss == NULL) {
            goto done;
        }

        /* Active service-set exists, add policy to policy-db,
         * then packet handler will accept new session and apply new policy.
         */
        bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
        policy_db_params.handle = ctrl_ctx->policy_db_handle;
        policy_db_params.svc_set_id = ss->ss_policy->ss_svc_id;
        policy_db_params.svc_id = ss->ss_policy->ss_svc_id;
        policy_db_params.plugin_id = balance_pid;
        strlcpy(policy_db_params.plugin_name, EQ2_CLASSIFY_SVC_NAME,
                sizeof(policy_db_params.plugin_name));
        policy_db_params.policy_op = MSP_POLICY_DB_POLICY_ADD;
        policy_db_params.op.add_params.gen_num = ss->ss_policy->ss_gen_num;
        policy_db_params.op.add_params.policy = ss->ss_policy;
        if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
            msp_log(LOG_ERR, "%s: Policy operation %d ERROR!",
                    __func__, policy_db_params.policy_op);
            del_svc_set(ss);
        }
    }

done:
    msp_spinlock_unlock(&svc_set_lock);
    return;
}

/**
 * @brief
 * Data event handler.
 *
 * @param[in] ctx
 *      Pointer to data context.
 *
 * @param[in] ev
 *      Data event.
 *
 * @return
 *      Data handler status code
 */
int
equilibrium2_balance_data_hdlr (msvcs_data_context_t *ctx,
        msvcs_data_event_t ev)
{
    msvcs_pub_data_req_t pub_data_req;
    int param;
    int data;

    data_ctx = ctx;

    switch (ev) {
    case MSVCS_DATA_EV_SM_INIT:
        msp_log(LOG_INFO, "%s: MSVCS_DATA_EV_SM_INIT", __func__);
        break;

    case MSVCS_DATA_EV_INIT:
        msp_log(LOG_INFO, "%s: MSVCS_DATA_EV_INIT", __func__);
        break;

    case MSVCS_DATA_EV_FIRST_PKT_PROC:
        msp_log(LOG_INFO, "%s: MSVCS_DATA_EV_FIRST_PKT_PROC", __func__);
        param = EQ2_LUCKY_NUM;
        data = 0;
        pub_data_req.data_id = EQ2_CLASSIFY_PUB_DATA_LUCKY_NUM;
        pub_data_req.err = -1;
        pub_data_req.param = &param;
        pub_data_req.data = &data;
        msvcs_plugin_public_data_get(EQ2_CLASSIFY_SVC_NAME, &pub_data_req);
        msp_log(LOG_INFO, "%02d %s: data %d error %d", msvcs_state_get_cpuid(),
                __func__, data, pub_data_req.err);

        first_pkt_proc();
        return take_action();
        break;

    case MSVCS_DATA_EV_PKT_PROC:
        msp_log(LOG_INFO, "%s: MSVCS_DATA_EV_PKT_PROC", __func__);
        return take_action();
        break;

    case MSVCS_DATA_EV_SESSION_OPEN:
        msp_log(LOG_INFO, "%s: MSVCS_DATA_EV_SESSION_OPEN", __func__);
        break;

    case MSVCS_DATA_EV_SESSION_CLOSE:
        msp_log(LOG_INFO, "%s: MSVCS_DATA_EV_SESSION_CLOSE", __func__);
        session_close();
        break;

    case MSVCS_DATA_EV_SESSION_DESTROY:
        msp_log(LOG_INFO, "%s: MSVCS_DATA_EV_SESSION_DESTROY", __func__);
        break;

    default:
        if (MSVCS_EV_GET_EVENT_CLASS(ev) == classify_ev_class) {
            if (MSVCS_EV_GET_EVENT_TYPE(ev) == EV_CLASSIFY_FIRST_PACKET) {
                msp_log(LOG_INFO, "%02d %s: Event EV_CLASSIFY_FIRST_PACKET %s",
                        msvcs_state_get_cpuid(), __func__, ctx->plugin_data);
            }
        } else {
            msp_log(LOG_ERR, "%02d %s: Unknown event 0x%08x!",
                    msvcs_state_get_cpuid(), __func__, ev);
        }
    }
    return MSVCS_ST_OK;
}


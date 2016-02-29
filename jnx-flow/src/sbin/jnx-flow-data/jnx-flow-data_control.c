/*
 * $Id: jnx-flow-data_control.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-flow-data_control.c -  RE management agent interaction routines
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file jnx-flow-data_control.c
 * @brief This file contains routines for handling the RE management agent
 * configuration, op-command and clear messages
 */

#include "jnx-flow-data.h"

PATNODE_TO_STRUCT(jnx_flow_data_svc_set_entry,
                  jnx_flow_data_svc_set_t, svc_node);

PATNODE_TO_STRUCT(jnx_flow_data_svc_set_id_entry,
                  jnx_flow_data_svc_set_t, svc_id_node);

PATNODE_TO_STRUCT(jnx_flow_data_rule_entry,
                  jnx_flow_data_rule_entry_t, rule_node);

/**
 * This function sends the response message to the RE management agent
 * @param  data_cb    data control block pointer
 * @param  msg_type   message type
 * @param  rsp_hdr    message header pointer
 * @returns
 *      EOK        if successful
 *      EFAIL      otherwise
 */
static status_t
jnx_flow_data_send_msg_resp(jnx_flow_data_cb_t * data_cb, 
                            jnx_flow_msg_type_t msg_type,
                            jnx_flow_msg_header_info_t * hdr)
{
    hdr->msg_type  = msg_type;
    hdr->msg_count = data_cb->cb_msg_count;
    hdr->msg_len   = htons(data_cb->cb_msg_len);

    jnx_flow_log(LOG_INFO, "%s:%d:%d:%d:%d", __func__, __LINE__,
                 msg_type, data_cb->cb_msg_count, data_cb->cb_msg_len);

    if ((data_cb->cb_conn_session) && (data_cb->cb_msg_count) &&
        (data_cb->cb_msg_len)) {
        pconn_server_send(data_cb->cb_conn_session, msg_type, hdr,
                          data_cb->cb_msg_len);
    }

    data_cb->cb_msg_len   = sizeof(*hdr);
    data_cb->cb_msg_count = 0;

    hdr->more = FALSE;
    return EOK;
}

/**
 * This function finds a service set for a service id
 * @param  data_cb    data control block pointer
 * @param  svc_id     service set id
 * @returns
 *                  service set structure pointer 
 */
jnx_flow_data_svc_set_t *  
jnx_flow_data_svc_set_id_lookup(jnx_flow_data_cb_t * data_cb, uint32_t svc_id)
{
    patnode *pnode;
    if ((pnode = patricia_get(&data_cb->cb_svc_id_db,
                              sizeof(svc_id), &svc_id))) {
        return jnx_flow_data_svc_set_id_entry(pnode);
    }
    return NULL;
}

/**
 * This function finds a service set for a service key (type/svc_id, iif)
 * @param  data_cb    data control block pointer
 * @param  svc_ket    service set key
 * @returns
 *                  service set structure pointer 
 */
jnx_flow_data_svc_set_t *  
jnx_flow_data_svc_set_lookup(jnx_flow_data_cb_t * data_cb,
                             jnx_flow_svc_key_t * svc_key)
{
    patnode * pnode = NULL;
    if ((pnode = patricia_get(&data_cb->cb_svc_db,
                              sizeof(*svc_key), svc_key))) {
        return jnx_flow_data_svc_set_entry(pnode);
    }
    return NULL;
}

/**
 * This function finds the next service set entry
 * @param  data_cb    data control block pointer
 * @param  svc_ket    service set structure pointer
 * @returns
 *                  service set structure pointer 
 */
jnx_flow_data_svc_set_t *  
jnx_flow_data_svc_set_get_next(jnx_flow_data_cb_t * data_cb,
                               jnx_flow_data_svc_set_t * svc_set)
{
    patnode * pnode = NULL;

    if (svc_set == NULL) {
        if ((pnode = patricia_find_next(&data_cb->cb_svc_id_db, NULL))) {
            return jnx_flow_data_svc_set_id_entry(pnode);
        }
        return NULL;
    }

    if ((pnode = patricia_find_next(&data_cb->cb_svc_id_db,
                                    &svc_set->svc_id_node))) {
        return jnx_flow_data_svc_set_id_entry(pnode);
    }

    return NULL;
}
/**
 * This function adds a service set to the service set database
 * @param  data_cb       data control block pointer
 * @param  psvc_set_msg  service set add message pointer
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static jnx_flow_err_type_t
jnx_flow_data_svc_set_add(jnx_flow_data_cb_t * data_cb,
                          jnx_flow_msg_svc_info_t * psvc_set_msg)
{
    jnx_flow_data_svc_set_t * psvc_set = NULL;

    jnx_flow_log(LOG_INFO, "%s:%d:cpu-no %d", __func__, __LINE__,
                 msp_get_current_cpu());

    if ((psvc_set = SVC_ALLOC(data_cb->cb_svc_oc, 0)) == NULL) {
        return JNX_FLOW_ERR_ALLOC_FAIL;
    }

    memset(psvc_set, 0, sizeof(jnx_flow_data_svc_set_t));

    /* set the service set key */
    psvc_set->svc_key.svc_type = psvc_set_msg->svc_type;

    if (psvc_set->svc_key.svc_type == JNX_FLOW_SVC_TYPE_INTERFACE) {
        psvc_set->svc_key.svc_id = ntohl(psvc_set_msg->svc_index);
    } else {
        psvc_set->svc_key.svc_iif = ntohl(psvc_set_msg->svc_in_subunit);
    }

    /* set the service set attributes */
    psvc_set->svc_type  = psvc_set_msg->svc_type;
    psvc_set->svc_id    = ntohl(psvc_set_msg->svc_index);

    strncpy(psvc_set->svc_name, psvc_set_msg->svc_name,
            sizeof(psvc_set->svc_name));

    if (psvc_set->svc_key.svc_type == JNX_FLOW_SVC_TYPE_NEXTHOP) {
        psvc_set->svc_iif  = ntohl(psvc_set_msg->svc_in_subunit);
        psvc_set->svc_oif  = ntohl(psvc_set_msg->svc_out_subunit);
    } else {
        strncpy(psvc_set->svc_intf, psvc_set_msg->svc_intf,
                sizeof(psvc_set->svc_intf));
    }
    psvc_set->svc_rule_count  = ntohs(psvc_set_msg->svc_rule_count);

    /* initialize the service set patricia node */
    patricia_node_init_length(&psvc_set->svc_node,
                              sizeof(psvc_set->svc_key));

    patricia_node_init_length(&psvc_set->svc_id_node,
                              sizeof(psvc_set->svc_id));

    /* add to the service set data base */
    if (!patricia_add(&data_cb->cb_svc_db, &psvc_set->svc_node)) {
        SVC_FREE(data_cb->cb_svc_oc, psvc_set, 0);
        jnx_flow_log(LOG_ERR, "%s:%d:service set patricia add failed",
                     __func__, __LINE__);
        return JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }

    /*
     * add to the service set id data base
     */
    if (!patricia_add(&data_cb->cb_svc_id_db, &psvc_set->svc_id_node)) {
        patricia_delete(&data_cb->cb_svc_db, &psvc_set->svc_node);
        SVC_FREE(data_cb->cb_svc_oc, psvc_set, 0);
        jnx_flow_log(LOG_ERR, "%s:%d:service set id patricia add failed",
                     __func__, __LINE__);
        return JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }
    data_cb->cb_stats.svc_set_count++;
    return JNX_FLOW_ERR_NO_ERROR;
}

/**
 * This function deletes a service set from the service set database
 * @param  data_cb       data control block pointer
 * @param  psvc_set      service set structure pointer 
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static jnx_flow_err_type_t
jnx_flow_data_svc_set_delete(jnx_flow_data_cb_t * data_cb,
                             jnx_flow_data_svc_set_t * psvc_set)
{
    jnx_flow_err_type_t err_code = JNX_FLOW_ERR_NO_ERROR;
    jnx_flow_data_list_entry_t * plist_entry, *pcur;
    jnx_flow_data_rule_entry_t * prule_entry;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    plist_entry = pcur = psvc_set->svc_rule_set.list_head;

    /* release the rule set from the service set list */
    while (plist_entry) {

        prule_entry = (typeof(prule_entry))plist_entry->ptr;

        prule_entry->rule_stats.rule_svc_ref_count--;

        plist_entry = plist_entry->next;

        LIST_FREE(data_cb->cb_list_oc, pcur, 0);

        pcur = plist_entry;
    }

    /*
     * delete from the service set data base
     */
    if (!patricia_delete(&data_cb->cb_svc_db, &psvc_set->svc_node)) {
        jnx_flow_log(LOG_ERR, "%s:%d:service set patricia delete failed",
                     __func__, __LINE__);
        err_code = JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }

    /*
     * delete from the service set id data base
     */
    if (!patricia_delete(&data_cb->cb_svc_id_db, &psvc_set->svc_id_node)) {
        jnx_flow_log(LOG_ERR, "%s:%d:service set patricia delete failed",
                     __func__, __LINE__);
        err_code = JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }

    data_cb->cb_stats.svc_set_count--;
    SVC_FREE(data_cb->cb_svc_oc, psvc_set, 0);
    return err_code;
}

/**
 * This function changes the attributes for a service set
 * @param  data_cb       data control block pointer
 * @param  psvc_set      service set structure pointer 
 * @param  psvc_set_msg  service set message structure pointer 
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static jnx_flow_err_type_t
jnx_flow_data_svc_set_change(jnx_flow_data_cb_t * data_cb __unused,
                             jnx_flow_data_svc_set_t * psvc_set,
                             jnx_flow_msg_svc_info_t * psvc_set_msg)
{
    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);

    /*
     * delete from the service set data base
     */
    if (!patricia_delete(&data_cb->cb_svc_db, &psvc_set->svc_node)) {
        jnx_flow_log(LOG_ERR, "%s:%d:service set patricia delete failed",
                     __func__, __LINE__);
        return JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }

    /*
     * delete from the service set id data base
     */
    if (!patricia_delete(&data_cb->cb_svc_id_db, &psvc_set->svc_id_node)) {
        jnx_flow_log(LOG_ERR, "%s:%d:service set patricia delete failed",
                     __func__, __LINE__);
        return JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }

    /* set the service set key */
    psvc_set->svc_key.svc_type = psvc_set_msg->svc_type;

    if (psvc_set->svc_key.svc_type == JNX_FLOW_SVC_TYPE_INTERFACE) {
        psvc_set->svc_key.svc_id = ntohl(psvc_set_msg->svc_index);
    } else {
        psvc_set->svc_key.svc_iif = ntohl(psvc_set_msg->svc_in_subunit);
    }

    /* set the service set attributes */
    psvc_set->svc_type  = psvc_set_msg->svc_type;
    psvc_set->svc_id    = ntohl(psvc_set_msg->svc_index);

    strncpy(psvc_set->svc_name, psvc_set_msg->svc_name,
            sizeof(psvc_set->svc_name));

    if (psvc_set->svc_key.svc_type == JNX_FLOW_SVC_TYPE_NEXTHOP) {
        psvc_set->svc_iif  = ntohl(psvc_set_msg->svc_in_subunit);
        psvc_set->svc_oif  = ntohl(psvc_set_msg->svc_out_subunit);
    } else {
        strncpy(psvc_set->svc_intf, psvc_set_msg->svc_intf,
                sizeof(psvc_set->svc_intf));
    }

    /* initialize the service set patricia node */
    patricia_node_init_length(&psvc_set->svc_node,
                              sizeof(psvc_set->svc_key));

    patricia_node_init_length(&psvc_set->svc_id_node,
                              sizeof(psvc_set->svc_id));

    /* add to the service set data base */
    if (!patricia_add(&data_cb->cb_svc_db, &psvc_set->svc_node)) {
        jnx_flow_log(LOG_ERR, "%s:%d:service set patricia add failed",
                     __func__, __LINE__);
        return JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }

    /*
     * add to the service set id data base
     */
    if (!patricia_add(&data_cb->cb_svc_id_db, &psvc_set->svc_id_node)) {
        jnx_flow_log(LOG_ERR, "%s:%d:service set id patricia add failed",
                     __func__, __LINE__);
        return JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }

    return JNX_FLOW_ERR_NO_ERROR;
}

/**
 * This function handles the service set config message from the 
 * management agent
 * @param  data_cb    data control block pointer
 * @param  msg_hdr    service set message structure pointer 
 * @param  rsp_hdr    response service set message structure pointer 
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static status_t
jnx_flow_data_handle_svc_config(jnx_flow_data_cb_t * data_cb,
                                jnx_flow_msg_header_info_t * msg_hdr,
                                jnx_flow_msg_header_info_t * rsp_hdr)
{
    jnx_flow_err_type_t err_code = JNX_FLOW_ERR_NO_ERROR;
    uint32_t svc_id = 0, sub_len = 0, msg_count = 0;
    jnx_flow_data_svc_set_t    *psvc_set = NULL;
    jnx_flow_msg_svc_info_t    *psvc_set_msg = NULL;
    jnx_flow_msg_sub_header_info_t *subhdr = NULL, *rsp_subhdr = NULL;

    msg_count   = msg_hdr->msg_count;
    subhdr      = (typeof(subhdr))((uint8_t*)msg_hdr + sizeof(*msg_hdr));

    data_cb->cb_msg_count = msg_count;
    data_cb->cb_msg_len   = ntohs(msg_hdr->msg_len);

    jnx_flow_log(LOG_INFO, "%s:%d:mesg-count %d",
                 __func__, __LINE__, msg_count);

    /* prepare the response */
    rsp_subhdr  = (typeof(rsp_subhdr))((uint8_t*)rsp_hdr + sizeof(*rsp_hdr));

    JNX_FLOW_DATA_ACQUIRE_CONFIG_WRITE_LOCK(data_cb);

    for (;(msg_count); subhdr = (typeof(subhdr))((uint8_t *)subhdr + sub_len),
         rsp_subhdr =
         (typeof(rsp_subhdr))((uint8_t *)rsp_subhdr + sub_len), msg_count--) {

        sub_len = ntohs(subhdr->msg_len);
        memcpy(rsp_subhdr, subhdr, sub_len);

        psvc_set_msg = (typeof(psvc_set_msg))
                        ((uint8_t *)subhdr + sizeof(*subhdr));

        svc_id = ntohl(psvc_set_msg->svc_index);
        psvc_set = jnx_flow_data_svc_set_id_lookup(data_cb, svc_id);

        switch (subhdr->msg_type) {

            case JNX_FLOW_MSG_CONFIG_ADD:
                if (psvc_set != NULL) {
                    rsp_subhdr->err_code = JNX_FLOW_ERR_ENTRY_EXISTS;
                    break;
                }
                err_code = jnx_flow_data_svc_set_add(data_cb, psvc_set_msg);
                rsp_subhdr->err_code = err_code;
                break;
            case JNX_FLOW_MSG_CONFIG_DELETE:
                if (psvc_set == NULL) {
                    rsp_subhdr->err_code = JNX_FLOW_ERR_ENTRY_ABSENT;
                    break;
                }
                err_code = 
                    jnx_flow_data_svc_set_delete(data_cb, psvc_set);
                rsp_subhdr->err_code = err_code;
                break;
            case JNX_FLOW_MSG_CONFIG_CHANGE:
                if (psvc_set == NULL) {
                    rsp_subhdr->err_code = JNX_FLOW_ERR_ENTRY_ABSENT;
                    break;
                }
                err_code = jnx_flow_data_svc_set_change(data_cb, psvc_set,
                                                        psvc_set_msg);
                rsp_subhdr->err_code = err_code;
                break;
            default:
                rsp_subhdr->err_code = JNX_FLOW_ERR_CONFIG_INVALID;
                break;
        }
        jnx_flow_log(LOG_INFO, "%s:%d:<\"%s\", %d> \"%s\" \"%s\"",
                     __func__, __LINE__, 
                     psvc_set_msg->svc_name, svc_id,
                     jnx_flow_config_op_str[subhdr->msg_type],
                     jnx_flow_err_str[rsp_subhdr->err_code]);
    }
    JNX_FLOW_DATA_RELEASE_CONFIG_WRITE_LOCK(data_cb);
    return EOK;
}

/**
 * This function gets the next rule entry in the database
 * @param  data_cb    data control block pointer
 * @param  prule     rule entry pointer 
 * @returns
 *                  next rule entry structure pointer 
 */

static jnx_flow_data_rule_entry_t *  
jnx_flow_data_rule_get_next(jnx_flow_data_cb_t * data_cb,
                            jnx_flow_data_rule_entry_t * prule)
{
    patnode *pnode;
    if (prule == NULL) {
        if ((pnode = patricia_find_next(&data_cb->cb_rule_db, NULL))) {
            return jnx_flow_data_rule_entry(pnode);
        }
        return NULL;
    }
    if ((pnode = patricia_find_next(&data_cb->cb_rule_db, &prule->rule_node))) {
        return jnx_flow_data_rule_entry(pnode);
    }
    return NULL;
}

/**
 * This function finds the rule entry for a rule id
 * @param  data_cb    data control block pointer
 * @param  rule_id     rule id
 * @returns
 *                  rule entry structure pointer 
 */

jnx_flow_data_rule_entry_t *  
jnx_flow_data_rule_lookup(jnx_flow_data_cb_t * data_cb, uint32_t rule_id)
{
    patnode *pnode;
    if ((pnode = patricia_get(&data_cb->cb_rule_db,
                              sizeof(rule_id), &rule_id))) {
        return jnx_flow_data_rule_entry(pnode);
    }
    return NULL;
}

/**
 * This function adds a rule to the rule data base
 * management agent
 * @param  data_cb    data control block pointer
 * @param  prule_msg  rule message structure pointer 
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static jnx_flow_err_type_t
jnx_flow_data_rule_add(jnx_flow_data_cb_t * data_cb,
                       jnx_flow_msg_rule_info_t * prule_msg)
{
    jnx_flow_data_rule_entry_t * prule = NULL;

    jnx_flow_log(LOG_INFO, "%s:%d:%d", __func__, __LINE__,
                 msp_get_current_cpu());
    if ((prule = RULE_ALLOC(data_cb->cb_rule_oc, 0)) == NULL) {
        return JNX_FLOW_ERR_ALLOC_FAIL;
    }

    memset(prule, 0, sizeof(jnx_flow_data_rule_entry_t));

    /* set the rule index */
    prule->rule_id        = ntohl(prule_msg->rule_index);

    /* set the rule atributes */
    prule->rule_flags     = prule_msg->rule_flags;
    prule->rule_action    = prule_msg->rule_action;
    prule->rule_match     = prule_msg->rule_match;
    prule->rule_direction = prule_msg->rule_direction;

    /* get the rule name */
    strncpy(prule->rule_name, prule_msg->rule_name, sizeof(prule->rule_name));

    /* get the match conditions */

    prule->rule_session.src_mask = ntohl(prule_msg->rule_src_mask);
    prule->rule_session.dst_mask = ntohl(prule_msg->rule_dst_mask);

    prule->rule_session.session.src_addr =
        ntohl(prule_msg->rule_flow.src_addr);

    prule->rule_session.session.dst_addr =
        ntohl(prule_msg->rule_flow.dst_addr);

    prule->rule_session.session.src_port =
        ntohs(prule_msg->rule_flow.src_port);

    prule->rule_session.session.dst_port =
        ntohs(prule_msg->rule_flow.dst_port);

    prule->rule_session.session.proto =
        prule_msg->rule_flow.proto;

    /* initialize the rule set patricia node */
    patricia_node_init_length(&prule->rule_node,
                              sizeof(prule->rule_id));

    /* add to the rule set data base */
    if (!patricia_add(&data_cb->cb_rule_db, &prule->rule_node)) {
        RULE_FREE(data_cb->cb_rule_oc, prule, 0);
        jnx_flow_log(LOG_ERR, "%s:%d:rule info patricia add failed",
                     __func__, __LINE__);
        return JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }

    data_cb->cb_stats.rule_count++;

    return JNX_FLOW_ERR_NO_ERROR;
}

/**
 * This function deletes a rule from the rule data base
 * management agent
 * @param  data_cb    data control block pointer
 * @param  prule      rule structure pointer 
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static jnx_flow_err_type_t
jnx_flow_data_rule_delete(jnx_flow_data_cb_t * data_cb,
                          jnx_flow_data_rule_entry_t * prule)
{

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    if (prule->rule_stats.rule_svc_ref_count != 0) {
        return JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }

    /* delete from the rule set data base */
    if (!patricia_delete(&data_cb->cb_rule_db, &prule->rule_node)) {
        jnx_flow_log(LOG_ERR, "%s:%d:rule info patricia delete failed",
                     __func__, __LINE__);
        return JNX_FLOW_ERR_ENTRY_OP_FAIL;
    }

    RULE_FREE(data_cb->cb_rule_oc, prule, 0);

    data_cb->cb_stats.rule_count--;

    return JNX_FLOW_ERR_NO_ERROR;
}

/**
 * This function changes the attributes for a rule
 * management agent
 * @param  data_cb    data control block pointer
 * @param  prule      rule structure pointer
 * @param  prule_msg  rule message structure pointer
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static jnx_flow_err_type_t
jnx_flow_data_rule_change(jnx_flow_data_cb_t * data_cb __unused,
                          jnx_flow_data_rule_entry_t * prule,
                          jnx_flow_msg_rule_info_t * prule_msg)
{
    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);

    /* set the rule atributes */
    prule->rule_flags     = prule_msg->rule_flags;
    prule->rule_action    = prule_msg->rule_action;
    prule->rule_match     = prule_msg->rule_match;
    prule->rule_direction = prule_msg->rule_direction;

    /* get the rule name */
    strncpy(prule->rule_name, prule_msg->rule_name, sizeof(prule->rule_name));

    /* get the match conditions */

    prule->rule_session.src_mask = ntohl(prule_msg->rule_src_mask);
    prule->rule_session.dst_mask = ntohl(prule_msg->rule_dst_mask);

    prule->rule_session.session.src_addr =
        ntohl(prule_msg->rule_flow.src_addr);

    prule->rule_session.session.dst_addr =
        ntohl(prule_msg->rule_flow.dst_addr);

    prule->rule_session.session.src_port =
        ntohs(prule_msg->rule_flow.src_port);

    prule->rule_session.session.dst_port =
        ntohs(prule_msg->rule_flow.dst_port);

    prule->rule_session.session.proto =
        prule_msg->rule_flow.proto;

    return JNX_FLOW_ERR_NO_ERROR;
}

/**
 * This function handles the rule config messages from the management agent
 * @param  data_cb    data control block pointer
 * @param  msg_hdr    message header structure pointer
 * @param  rsp_hdr    message header structure pointer
 * @returns
 *       EOK    if successful
 *       EFAIL  otherwise 
 */
static status_t
jnx_flow_data_handle_rule_config(jnx_flow_data_cb_t * data_cb,
                                 jnx_flow_msg_header_info_t * msg_hdr,
                                 jnx_flow_msg_header_info_t * rsp_hdr)
{
    jnx_flow_err_type_t err_code = JNX_FLOW_ERR_NO_ERROR;
    uint32_t rule_id = 0, msg_count = 0, sub_len = 0;
    jnx_flow_data_rule_entry_t     *prule = NULL;
    jnx_flow_msg_rule_info_t       *prule_msg = NULL;
    jnx_flow_msg_sub_header_info_t *subhdr = NULL, *rsp_subhdr = NULL;

    msg_count   = msg_hdr->msg_count;
    subhdr      = (typeof(subhdr))((uint8_t*)msg_hdr + sizeof(*msg_hdr));

    data_cb->cb_msg_count = msg_count;
    data_cb->cb_msg_len   = ntohs(msg_hdr->msg_len);

    jnx_flow_log(LOG_INFO, "%s:%d:mesg-count %d",
                 __func__, __LINE__, msg_count);

    /* prepare the response */
    rsp_subhdr  = (typeof(rsp_subhdr))((uint8_t*)rsp_hdr + sizeof(*rsp_hdr));

    JNX_FLOW_DATA_ACQUIRE_CONFIG_WRITE_LOCK(data_cb);

    for (;(msg_count); subhdr = (typeof(subhdr))((uint8_t *)subhdr + sub_len),
         rsp_subhdr = (typeof(rsp_subhdr))((uint8_t *)rsp_subhdr + sub_len),
         msg_count--) {

        sub_len = ntohs(subhdr->msg_len);
        memcpy(rsp_subhdr, subhdr, sub_len);
        prule_msg = (typeof(prule_msg))((uint8_t *)subhdr + sizeof(*subhdr));

        /* get the rule index */
        rule_id = ntohl(prule_msg->rule_index);

        prule = jnx_flow_data_rule_lookup(data_cb, rule_id);

        switch (subhdr->msg_type) {

            case JNX_FLOW_MSG_CONFIG_ADD:
                if (prule != NULL) {
                    rsp_subhdr->err_code = JNX_FLOW_ERR_ENTRY_EXISTS;
                    break;
                }
                err_code = jnx_flow_data_rule_add(data_cb, prule_msg);
                rsp_subhdr->err_code = err_code;
                break;
            case JNX_FLOW_MSG_CONFIG_DELETE:
                if (prule == NULL) {
                    rsp_subhdr->err_code = JNX_FLOW_ERR_ENTRY_ABSENT;
                    break;
                }
                err_code = jnx_flow_data_rule_delete(data_cb, prule);
                rsp_subhdr->err_code = err_code;
                break;
            case JNX_FLOW_MSG_CONFIG_CHANGE:
                if (prule == NULL) {
                    rsp_subhdr->err_code = JNX_FLOW_ERR_ENTRY_ABSENT;
                    break;
                }
                err_code = jnx_flow_data_rule_change(data_cb, prule, prule_msg);
                rsp_subhdr->err_code = err_code;
                break;
            default:
                rsp_subhdr->err_code = JNX_FLOW_ERR_CONFIG_INVALID;
                break;
        }
        jnx_flow_log(LOG_INFO, "%s:%d:<\"%s\",%d> \"%s\" \"%s\"",
                     __func__, __LINE__,
                     prule_msg->rule_name, rule_id, 
                     jnx_flow_config_op_str[subhdr->msg_type],
                     jnx_flow_err_str[rsp_subhdr->err_code]);
    }
    JNX_FLOW_DATA_RELEASE_CONFIG_WRITE_LOCK(data_cb);
    return EOK;
}

/**
 * This function handles the service rule add config messages
 * from the management agent
 * @param  data_cb        data control block pointer
 * @param  psvc_set       service set structure pointer
 * @param  prule          rule structure pointer
 * @param  psvc_rule_msg  service set rule message structure pointer
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static jnx_flow_err_type_t
jnx_flow_data_svc_rule_add(jnx_flow_data_cb_t * data_cb __unused,
                           jnx_flow_data_svc_set_t * psvc_set,
                           jnx_flow_data_rule_entry_t * prule,
                           jnx_flow_msg_svc_rule_info_t * psvc_set_rule_msg)
{
    uint32_t svc_rule_index, rule_index = 1;
    jnx_flow_data_rule_entry_t * pold_rule = NULL;
    jnx_flow_data_list_entry_t * plist_entry = NULL;
    jnx_flow_data_list_entry_t * prule_entry = NULL, * prev = NULL;

    svc_rule_index = ntohl(psvc_set_rule_msg->svc_svc_rule_index);

    plist_entry = psvc_set->svc_rule_set.list_head;

    /* go to the index */
    for (;((plist_entry) && (rule_index <= svc_rule_index));
         prev = plist_entry, plist_entry = plist_entry->next, rule_index++) {

        if (rule_index < svc_rule_index) {
            continue;
        }

        pold_rule = (typeof(pold_rule))plist_entry->ptr;

        if (pold_rule != prule) {
            pold_rule->rule_stats.rule_svc_ref_count--;
            prule->rule_stats.rule_svc_ref_count++;
            plist_entry->ptr = prule;
        }
        return JNX_FLOW_ERR_NO_ERROR;
    }

    if ((prule_entry = LIST_ALLOC(data_cb->cb_list_oc, 0)) == NULL) {
        return JNX_FLOW_ERR_ALLOC_FAIL;
    }

    memset(prule_entry, 0, sizeof(jnx_flow_data_list_entry_t));

    prule_entry->ptr = prule;

    if (prev)
        prev->next = prule_entry;
    else 
        psvc_set->svc_rule_set.list_head = prule_entry;

    if (plist_entry) {
        prule_entry->next = plist_entry;
    } else {
        psvc_set->svc_rule_set.list_tail = prule_entry;
    }

    /* increment the rule, service reference count */
    prule->rule_stats.rule_svc_ref_count++;

    /* update the list tail, if required */
    psvc_set->svc_stats.rule_count++;
    return JNX_FLOW_ERR_NO_ERROR;
}

/**
 * This function handles the service rule delete config messages
 * from the management agent
 * @param  data_cb        data control block pointer
 * @param  psvc_set       service set structure pointer
 * @param  prule          rule structure pointer
 * @param  psvc_rule_msg  service set rule message structure pointer
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static jnx_flow_err_type_t
jnx_flow_data_svc_rule_delete(jnx_flow_data_cb_t * data_cb __unused,
                              jnx_flow_data_svc_set_t * psvc_set,
                              jnx_flow_data_rule_entry_t * prule,
                              jnx_flow_msg_svc_rule_info_t * psvc_set_rule_msg)
{
    uint32_t svc_rule_index, rule_index = 1;
    jnx_flow_data_list_entry_t * prev_entry = NULL,  * pcur_entry = NULL;
    jnx_flow_data_list_entry_t * plist_entry = NULL, * pnext_entry = NULL;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    svc_rule_index = ntohl(psvc_set_rule_msg->svc_svc_rule_index);

    plist_entry = psvc_set->svc_rule_set.list_head;

    /* go to the index */
    for (;((plist_entry) && (rule_index <= svc_rule_index));
         prev_entry = plist_entry, plist_entry = pnext_entry, rule_index++) {

        pcur_entry  = plist_entry;
        pnext_entry = plist_entry->next;

        if (rule_index < svc_rule_index) {
            continue;
        }

        if (plist_entry->ptr != prule) {
            return JNX_FLOW_ERR_ENTRY_OP_FAIL;
        }

        /* update the previous entry */
        if (prev_entry) {
            prev_entry->next  = pnext_entry;
        } else {
            /* update the head, if required */
            psvc_set->svc_rule_set.list_head = pnext_entry;
        }

        /* update the tail, if required */
        if (pnext_entry == NULL) {
            psvc_set->svc_rule_set.list_tail = prev_entry;
        }

        plist_entry = prev_entry;

        /* decrement the rule, service reference count */
        prule->rule_stats.rule_svc_ref_count--;
        psvc_set->svc_stats.rule_count--;

        /* free the list entry */
        LIST_FREE(data_cb->cb_list_oc, pcur_entry, 0);

        return JNX_FLOW_ERR_NO_ERROR;
    }

    return JNX_FLOW_ERR_ENTRY_OP_FAIL;
}

/**
 * This function handles the service rule change config messages
 * from the management agent
 * @param  data_cb        data control block pointer
 * @param  psvc_set       service set structure pointer
 * @param  prule          rule structure pointer
 * @param  psvc_rule_msg  service set rule message structure pointer
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static jnx_flow_err_type_t
jnx_flow_data_svc_rule_change(jnx_flow_data_cb_t * data_cb __unused,
                              jnx_flow_data_svc_set_t * psvc_set,
                              jnx_flow_data_rule_entry_t * pnew_rule,
                              jnx_flow_msg_svc_rule_info_t * psvc_set_rule_msg)
{
    uint32_t svc_rule_index, rule_index = 1;
    jnx_flow_data_rule_entry_t * pold_rule = NULL;
    jnx_flow_data_list_entry_t * plist_entry = NULL;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);

    svc_rule_index = ntohl(psvc_set_rule_msg->svc_svc_rule_index);

    plist_entry = psvc_set->svc_rule_set.list_head;

    /* go to the index */
    for (;((plist_entry) && (rule_index <= svc_rule_index));
         plist_entry = plist_entry->next, rule_index++) {

        if (rule_index < svc_rule_index) {
            continue;
        }

        pold_rule = (typeof(pold_rule))plist_entry->ptr;

        if (pold_rule != pnew_rule) { 
            pold_rule->rule_stats.rule_svc_ref_count--;
            pnew_rule->rule_stats.rule_svc_ref_count++;
            plist_entry->ptr = pnew_rule;
        }
        return JNX_FLOW_ERR_NO_ERROR;
    }

    return JNX_FLOW_ERR_ENTRY_OP_FAIL;
}

/**
 * This function handles the service rule config messages
 * from the management agent
 * @param  data_cb        data control block pointer
 * @param  msg_hdr        config message header structure pointer
 * @param  rsp_hdr        response message header structure pointer
 * @returns
 *       JNX_FLOW_ERR_NO_ERROR  if successful
 *                              otherwise 
 */
static status_t
jnx_flow_data_handle_svc_rule_config(jnx_flow_data_cb_t * data_cb,
                                     jnx_flow_msg_header_info_t * msg_hdr,
                                     jnx_flow_msg_header_info_t * rsp_hdr)
{
    jnx_flow_err_type_t err_code = JNX_FLOW_ERR_NO_ERROR;
    uint32_t sub_len = 0, msg_count = 0, rule_id = 0, svc_id = 0,
             svc_rule_id = 0;
    jnx_flow_data_rule_entry_t     *prule = NULL;
    jnx_flow_data_svc_set_t        *psvc_set = NULL;
    jnx_flow_msg_svc_rule_info_t   *psvc_set_rule_msg = NULL;
    jnx_flow_msg_sub_header_info_t *subhdr = NULL, *rsp_subhdr = NULL;

    msg_count = msg_hdr->msg_count;
    subhdr    = (typeof(subhdr))((uint8_t*)msg_hdr + sizeof(*msg_hdr));

    data_cb->cb_msg_count = msg_count;
    data_cb->cb_msg_len   = ntohs(msg_hdr->msg_len);

    jnx_flow_log(LOG_INFO, "%s:%d:mesg-count %d", __func__, __LINE__,
                 msg_count);

    /* prepare the response */
    rsp_subhdr  = (typeof(rsp_subhdr))((uint8_t*)rsp_hdr + sizeof(*rsp_hdr));

    JNX_FLOW_DATA_ACQUIRE_CONFIG_WRITE_LOCK(data_cb);

    for (;(msg_count);
         subhdr = (typeof(subhdr)) ((uint8_t *)subhdr + sub_len),
         rsp_subhdr = (typeof(rsp_subhdr)) ((uint8_t *)rsp_subhdr + sub_len),
         msg_count--) {

        sub_len = ntohs(subhdr->msg_len);
        memcpy(rsp_subhdr, subhdr, ntohs(subhdr->msg_len));

        psvc_set_rule_msg = (typeof(psvc_set_rule_msg))
            ((uint8_t *)subhdr + sizeof(*subhdr));

        /* get the service key */
        svc_id = ntohl(psvc_set_rule_msg->svc_index);

        /* get the rule index */
        rule_id = ntohl(psvc_set_rule_msg->svc_rule_index);

        /* get the service set rule index */
        svc_rule_id = ntohl(psvc_set_rule_msg->svc_svc_rule_index);

        /* get the service and rule entries */
        psvc_set = jnx_flow_data_svc_set_id_lookup(data_cb, svc_id);
        prule    = jnx_flow_data_rule_lookup(data_cb, rule_id);

        if (psvc_set == NULL || prule == NULL) {
            rsp_subhdr->err_code = JNX_FLOW_ERR_CONFIG_INVALID;
            jnx_flow_log(LOG_INFO, "%s:%d:<svc(%d,%p),rule(%d,%p), %d>"
                         " \"%s\" \"%s\"",
                         __func__, __LINE__,
                         svc_id, psvc_set, rule_id, prule, svc_rule_id,
                         jnx_flow_config_op_str[subhdr->msg_type],
                         jnx_flow_err_str[rsp_subhdr->err_code]);
            continue;
        }

        switch (subhdr->msg_type) {

            case JNX_FLOW_MSG_CONFIG_ADD:
                err_code = jnx_flow_data_svc_rule_add(data_cb, psvc_set,
                                                      prule,
                                                      psvc_set_rule_msg);
                rsp_subhdr->err_code = err_code;
                break;
            case JNX_FLOW_MSG_CONFIG_DELETE:
                err_code = jnx_flow_data_svc_rule_delete(data_cb, psvc_set,
                                                         prule,
                                                         psvc_set_rule_msg);
                rsp_subhdr->err_code = err_code;
                break;
            case JNX_FLOW_MSG_CONFIG_CHANGE:
                err_code = 
                    jnx_flow_data_svc_rule_change(data_cb, psvc_set, prule,
                                                  psvc_set_rule_msg);
                rsp_subhdr->err_code = err_code;
                break;
            default:
                rsp_subhdr->err_code = JNX_FLOW_ERR_CONFIG_INVALID;
                break;
        }

        jnx_flow_log(LOG_INFO, "%s:%d: <<\"%s\", %d>, <\"%s\", %d>, %d>"
                     " \"%s\" \"%s\"",
                     __func__, __LINE__,
                     psvc_set->svc_name, svc_id,
                     prule->rule_name, rule_id, svc_rule_id,
                     jnx_flow_config_op_str[subhdr->msg_type],
                     jnx_flow_err_str[rsp_subhdr->err_code]);
    }

    JNX_FLOW_DATA_RELEASE_CONFIG_WRITE_LOCK(data_cb);
    return EOK;
}

/**
 * This function prepares the service set status message 
 * @param  data_cb        data control block pointer
 * @param  subhdr         subheader message structure pointer
 * @param  psvc_set       service set structure pointer
 * @returns
 *       sizeof of message  if successful
 *       0                  otherwise 
 */
static uint32_t
jnx_flow_data_svc_set_entry_msg(jnx_flow_data_cb_t * data_cb,
                                jnx_flow_msg_sub_header_info_t * subhdr,
                                jnx_flow_data_svc_set_t * psvc_set)
{
    uint16_t sub_len = 0;
    jnx_flow_msg_stat_svc_set_info_t *pinfo_msg;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    sub_len = sizeof(*subhdr) + sizeof(*pinfo_msg);

    subhdr->msg_type = JNX_FLOW_MSG_FETCH_SVC_ENTRY;
    subhdr->err_code = JNX_FLOW_ERR_NO_ERROR;
    subhdr->msg_len  = htons(sub_len);

    /*
     * get the pointer to the payload offset
     */
    pinfo_msg = (typeof(pinfo_msg))((uint8_t *)subhdr + sizeof(*subhdr));

    /*
     * populate the service set info
     */
    strncpy(pinfo_msg->svc_name, psvc_set->svc_name,
            sizeof( pinfo_msg->svc_name));

    pinfo_msg->svc_index        = htonl(psvc_set->svc_id);
    pinfo_msg->svc_type         = psvc_set->svc_type;

    /*
     * populate the service set stats
     */
    pinfo_msg->svc_stats.rule_count =
        htonl(psvc_set->svc_stats.rule_count);
    pinfo_msg->svc_stats.applied_rule_count = 
        htonl(psvc_set->svc_stats.applied_rule_count);
    pinfo_msg->svc_stats.total_flow_count = 
        htonl(psvc_set->svc_stats.total_flow_count);
    pinfo_msg->svc_stats.total_allow_flow_count = 
        htonl(psvc_set->svc_stats.total_allow_flow_count);
    pinfo_msg->svc_stats.total_drop_flow_count = 
        htonl(psvc_set->svc_stats.total_drop_flow_count);
    pinfo_msg->svc_stats.active_flow_count = 
        htonl(psvc_set->svc_stats.active_flow_count);
    pinfo_msg->svc_stats.active_allow_flow_count = 
        htonl(psvc_set->svc_stats.active_allow_flow_count);
    pinfo_msg->svc_stats.active_drop_flow_count = 
        htonl(psvc_set->svc_stats.active_drop_flow_count);

    data_cb->cb_msg_count++;
    data_cb->cb_msg_len += sub_len;
    return (sub_len);
}

/**
 * This function prepares the service set summary status message 
 * @param  data_cb        data control block pointer
 * @param  subhdr         subheader message structure pointer
 * @param  rsp_hdr        reseponse header message structure pointer
 * @returns
 *       sizeof of message  if successful
 *       0                  otherwise 
 */
static uint32_t 
jnx_flow_data_svc_set_summary_msg(jnx_flow_data_cb_t * data_cb,
                                  jnx_flow_msg_sub_header_info_t * subhdr)
{
    uint16_t sub_len = 0;
    jnx_flow_msg_stat_svc_summary_info_t *pinfo_msg;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    sub_len = (sizeof(*subhdr) + sizeof(*pinfo_msg));

    subhdr->msg_type = JNX_FLOW_MSG_FETCH_SVC_SUMMARY;
    subhdr->err_code = JNX_FLOW_ERR_NO_ERROR;
    subhdr->msg_len  = htons(sub_len);

    /*
     * get the pointer to the payload offset
     */
    pinfo_msg = (typeof(pinfo_msg))((uint8_t *)subhdr + sizeof(*subhdr));

    /*
     * fill the service set count 
     */

    pinfo_msg->svc_set_count = htonl(data_cb->cb_stats.svc_set_count);

    /*
     * populate the service set stats
     */
    pinfo_msg->stats.rule_count =
        htonl(data_cb->cb_stats.rule_count);

    pinfo_msg->stats.applied_rule_count = 
        htonl(data_cb->cb_stats.applied_rule_count);

    pinfo_msg->stats.total_flow_count = 
        htonl(data_cb->cb_stats.total_flow_count);

    pinfo_msg->stats.total_allow_flow_count = 
        htonl(data_cb->cb_stats.total_allow_flow_count);

    pinfo_msg->stats.total_drop_flow_count = 
        htonl(data_cb->cb_stats.total_drop_flow_count);

    pinfo_msg->stats.active_flow_count = 
        htonl(data_cb->cb_stats.active_flow_count);

    pinfo_msg->stats.active_allow_flow_count = 
        htonl(data_cb->cb_stats.active_allow_flow_count);

    pinfo_msg->stats.active_drop_flow_count = 
        htonl(data_cb->cb_stats.active_drop_flow_count);

    data_cb->cb_msg_count++;
    data_cb->cb_msg_len += sub_len;
    return sub_len;
}

/**
 * This function prepares the service set extensive status message 
 * @param  data_cb    data control block pointer
 * @param  hdr        reseponse header message structure pointer
 * @returns
 *    EOK       if successful
 *    EFAIL      otherwise
 */
static status_t
jnx_flow_data_svc_set_extensive_msg(jnx_flow_data_cb_t * data_cb,
                                    jnx_flow_msg_header_info_t * hdr)
{
    uint32_t sub_len = 0;
    jnx_flow_data_svc_set_t * psvc_set = NULL;
    jnx_flow_msg_sub_header_info_t * subhdr = NULL;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    /*
     * fill the summary information first 
     */
    subhdr = (typeof(subhdr))((uint8_t *)hdr + sizeof(*hdr));
    sub_len = jnx_flow_data_svc_set_summary_msg(data_cb, subhdr);

    /*
     * for each service set, populate the information
     */
    while ((psvc_set = jnx_flow_data_svc_set_get_next(data_cb, psvc_set))) {
        /*
         * buffer overflow may happen, send the message out
         */
        if ((data_cb->cb_msg_count > 250) || 
            ((data_cb->cb_msg_len + sub_len) > JNX_FLOW_BUF_SIZE)) {

            hdr->more = TRUE;
            jnx_flow_data_send_msg_resp(data_cb, JNX_FLOW_MSG_FETCH_SVC_INFO,
                                        hdr);
            subhdr  = (typeof(subhdr))((uint8_t *)hdr + sizeof(*hdr));
            sub_len = 0;
        }

        subhdr = (typeof(subhdr))((uint8_t *)subhdr + sub_len);
        sub_len = jnx_flow_data_svc_set_entry_msg(data_cb, subhdr, psvc_set);
    }
    return EOK;
}

/**
 * This function handles the service set status request message 
 * @param  data_cb    data control block pointer
 * @param  msg_hdr    request header message structure pointer
 * @param  rsp_hdr    reseponse header message structure pointer
 * @returns
 *    EOK       if successful
 *    EFAIL      otherwise
 */
static status_t
jnx_flow_data_handle_svc_opcmd(jnx_flow_data_cb_t * data_cb,
                               jnx_flow_msg_header_info_t * msg_hdr,
                               jnx_flow_msg_header_info_t * rsp_hdr)
{
    uint32_t svc_id;
    jnx_flow_data_svc_set_t           *psvc_set = NULL;
    jnx_flow_msg_stat_svc_set_info_t  *pinfo_msg = NULL;
    jnx_flow_msg_sub_header_info_t    *subhdr = NULL, * rsp_subhdr = NULL;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);

    /*
     * get the op-command request filter information
     */
    subhdr    = (typeof(subhdr))((uint8_t*)msg_hdr + sizeof(*msg_hdr));
    pinfo_msg = (typeof(pinfo_msg))((uint8_t *)subhdr + sizeof(*subhdr));

    /*
     * prepare the response, copy the subheader+payload
     * from the request message
     */
    rsp_subhdr  = (typeof(rsp_subhdr))((uint8_t*)rsp_hdr + sizeof(*rsp_hdr));
    memcpy(rsp_subhdr, subhdr, ntohs(subhdr->msg_len));


    switch(subhdr->msg_type) {

        case JNX_FLOW_MSG_FETCH_SVC_ENTRY:
            svc_id = ntohl(pinfo_msg->svc_index);
            if ((psvc_set = jnx_flow_data_svc_set_id_lookup(data_cb, svc_id))
                == NULL) {
                rsp_subhdr->err_code =  JNX_FLOW_MSG_FETCH_SVC_INVALID;
                break;
            }
            jnx_flow_data_svc_set_entry_msg(data_cb, rsp_subhdr, psvc_set);
            break;

        case JNX_FLOW_MSG_FETCH_SVC_SUMMARY:
             jnx_flow_data_svc_set_summary_msg(data_cb, rsp_subhdr);
            break;

        case JNX_FLOW_MSG_FETCH_SVC_EXTENSIVE:
            jnx_flow_data_svc_set_extensive_msg(data_cb, rsp_hdr);
            break;

        default:
            rsp_subhdr->err_code = JNX_FLOW_MSG_FETCH_SVC_INVALID;
            break;
    }
    jnx_flow_data_send_msg_resp(data_cb, rsp_hdr->msg_type, rsp_hdr);

    return EOK;
}

/**
 * This function prepares the flow entry status message 
 * @param  data_cb    data control block pointer
 * @param  subhdr     request header message structure pointer
 * @param  pflow      flow entry structure pointer
 * @returns
 *    sizeof message  if successful
 *    0               otherwise
 */
static uint32_t 
jnx_flow_data_flow_entry_msg(jnx_flow_data_cb_t * data_cb,
                             jnx_flow_msg_sub_header_info_t * subhdr,
                             jnx_flow_data_flow_entry_t * pflow)
{
    uint16_t sub_len = 0;
    jnx_flow_msg_stat_flow_info_t *pinfo_msg;
    jnx_flow_data_svc_set_t * psvc_set;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    sub_len = sizeof(*subhdr) + sizeof(*pinfo_msg);

    subhdr->msg_type = JNX_FLOW_MSG_FETCH_FLOW_ENTRY;
    subhdr->err_code = JNX_FLOW_ERR_NO_ERROR;
    subhdr->msg_len  = htons(sub_len);

    /*
     * get the pointer to the payload offset
     */
    pinfo_msg = (typeof(pinfo_msg))((uint8_t *)subhdr + sizeof(*subhdr));

    /*
     * fill the flow entry information
     */
    pinfo_msg->flow_info.src_addr = htonl(pflow->flow_key.session.src_addr);
    pinfo_msg->flow_info.dst_addr = htonl(pflow->flow_key.session.dst_addr);
    pinfo_msg->flow_info.src_port = htons(pflow->flow_key.session.src_port);
    pinfo_msg->flow_info.dst_port = htons(pflow->flow_key.session.dst_port);
    pinfo_msg->flow_info.proto    = pflow->flow_key.session.proto;
    pinfo_msg->flow_info.svc_id   = htonl(pflow->flow_svc_id);
    pinfo_msg->flow_info.flow_dir = pflow->flow_dir;
    pinfo_msg->flow_info.flow_action = pflow->flow_action;

    if ((psvc_set =
         jnx_flow_data_svc_set_id_lookup(data_cb, pflow->flow_svc_id))) {
        strncpy(pinfo_msg->flow_info.svc_set_name, psvc_set->svc_name,
                sizeof(pinfo_msg->flow_info.svc_set_name));
    }


    /*
     * populate the flow entry stats
     */
    pinfo_msg->flow_stats.pkts_in       = htonl(pflow->flow_stats.pkts_in);
    pinfo_msg->flow_stats.bytes_in      = htonl(pflow->flow_stats.bytes_in);
    pinfo_msg->flow_stats.pkts_out      = htonl(pflow->flow_stats.pkts_out);
    pinfo_msg->flow_stats.bytes_out     = htonl(pflow->flow_stats.bytes_out);
    pinfo_msg->flow_stats.pkts_dropped  = htonl(pflow->flow_stats.pkts_dropped);
    pinfo_msg->flow_stats.bytes_dropped =
        htonl(pflow->flow_stats.bytes_dropped);

    data_cb->cb_msg_count++;
    data_cb->cb_msg_len += sub_len;
    return sub_len;
}

/**
 * This function prepares the flow summary status message 
 * @param  data_cb    data control block pointer
 * @param  subhdr     request header message structure pointer
 * @returns
 *    sizeof message  if successful
 *    0               otherwise
 */
static uint32_t 
jnx_flow_data_flow_summary_msg(jnx_flow_data_cb_t * data_cb,
                               jnx_flow_msg_sub_header_info_t * subhdr)
{
    uint16_t sub_len = 0;
    jnx_flow_msg_stat_flow_summary_info_t *pinfo_msg;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    sub_len = sizeof(*subhdr) + sizeof(*pinfo_msg);

    subhdr->msg_type = JNX_FLOW_MSG_FETCH_FLOW_SUMMARY;
    subhdr->err_code = JNX_FLOW_ERR_NO_ERROR;
    subhdr->msg_len  = htons(sub_len);

    /*
     * get the pointer to the payload offset
     */
    pinfo_msg = (typeof(pinfo_msg))((uint8_t *)subhdr + sizeof(*subhdr));

    /*
     * fill the flow counts
     */
    pinfo_msg->stats.rule_count =
        htonl(data_cb->cb_stats.rule_count);

    pinfo_msg->stats.svc_set_count =
        htonl(data_cb->cb_stats.svc_set_count);

    pinfo_msg->stats.applied_rule_count = 
        htonl(data_cb->cb_stats.applied_rule_count);

    pinfo_msg->stats.total_flow_count = 
        htonl(data_cb->cb_stats.total_flow_count);

    pinfo_msg->stats.total_allow_flow_count = 
        htonl(data_cb->cb_stats.total_allow_flow_count);

    pinfo_msg->stats.total_drop_flow_count = 
        htonl(data_cb->cb_stats.total_drop_flow_count);

    pinfo_msg->stats.active_flow_count = 
        htonl(data_cb->cb_stats.active_flow_count);

    pinfo_msg->stats.active_allow_flow_count = 
        htonl(data_cb->cb_stats.active_allow_flow_count);
    
    pinfo_msg->stats.active_drop_flow_count = 
        htonl(data_cb->cb_stats.active_drop_flow_count);

    data_cb->cb_msg_count++;
    data_cb->cb_msg_len += sub_len;
    return sub_len;
}

/**
 * This function prepares the flow extensive status message 
 * @param  data_cb   data control block pointer
 * @param  hdr       header message structure pointer
 * @returns
 *    EOK     if successful
 *    EFAIL   otherwise
 */
static status_t
jnx_flow_data_flow_extensive_msg(jnx_flow_data_cb_t * data_cb,
                                 jnx_flow_msg_header_info_t * hdr)
{
    uint32_t sub_len = 0, hash = 0;
    jnx_flow_data_flow_entry_t     * pflow = NULL;
    jnx_flow_msg_sub_header_info_t * sub_hdr = NULL;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    /*
     * fill the summary information first 
     */
    sub_hdr = (typeof(sub_hdr))((uint8_t *)hdr + sizeof(*hdr));
    sub_len = jnx_flow_data_flow_summary_msg(data_cb, sub_hdr);

    /*
     * for each flow entry, populate the information
     */

    for (hash = 0; hash < JNX_FLOW_DATA_FLOW_BUCKET_COUNT; hash++)  {

        /*
         * For each flow table entry, in the hash bucket
         */
        pflow = JNX_FLOW_DATA_HASH_CHAIN(data_cb, hash);

        for (;(pflow); pflow = pflow->flow_next) {

            /*
             * buffer overflow may happen, send the message out
             */
            if ((data_cb->cb_msg_count > 250) || 
                ((data_cb->cb_msg_len + sub_len) > JNX_FLOW_BUF_SIZE)) {

                hdr->more = TRUE;
                jnx_flow_data_send_msg_resp(data_cb,
                                            JNX_FLOW_MSG_FETCH_FLOW_INFO, hdr);
                sub_hdr = (typeof(sub_hdr))((uint8_t *)hdr + sizeof(*hdr));
                sub_len = 0;
            }

            sub_hdr = (typeof(sub_hdr))((uint8_t *)sub_hdr + sub_len);
            sub_len = jnx_flow_data_flow_entry_msg(data_cb, sub_hdr, pflow);
        }
    }

    return EOK;
}

/**
 * This function finds a flow entry for a flow key
 * @param  data_cb   data control block pointer
 * @param  flow_key  flow key
 * @returns
 *    EOK     if successful
 *    EFAIL   otherwise
 */
static jnx_flow_data_flow_entry_t *
jnx_flow_data_flow_entry_lookup(jnx_flow_data_cb_t * data_cb,
                                jnx_flow_session_key_t * flow_key)
{
    uint32_t   hash;
    jnx_flow_data_flow_entry_t * flow_entry;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    /*
     * calculate the hash index
     */
    hash = jnx_flow_data_get_flow_hash(flow_key);

    /*
     * acquire the bucket lock
     */

    JNX_FLOW_DATA_HASH_LOCK(data_cb->cb_flow_db, hash);

    /*
     * find the flow entry
     */

    flow_entry = JNX_FLOW_DATA_HASH_CHAIN(data_cb, hash);

    for (;flow_entry; flow_entry = flow_entry->flow_next) {

        /*
         * compare the flow entry key
         */
        if (bcmp(&flow_entry->flow_key, flow_key, sizeof(*flow_key))) {
            continue;
        }

        /*
         * flow entry found 
         * release the bucket lock
         */

        JNX_FLOW_DATA_HASH_UNLOCK(data_cb->cb_flow_db, hash);

        /*
         * the flow entry is not up,
         * return NULL
         */
        if (flow_entry->flow_status != JNX_FLOW_DATA_STATUS_UP) {
            return NULL;
        }

        /*
         * return flow entry
         */
        return flow_entry;
    }

    return NULL;
}

/**
 * This function handles the operational commands
 * @param  data_cb   data control block pointer
 * @param  msg_hdr   request header message structure pointer
 * @param  rsp_hdr   response header message structure pointer
 * @returns
 *    EOK     if successful
 *    EFAIL   otherwise
 */
static status_t
jnx_flow_data_handle_flow_opcmd(jnx_flow_data_cb_t * data_cb,
                               jnx_flow_msg_header_info_t * msg_hdr,
                               jnx_flow_msg_header_info_t * rsp_hdr)
{
    jnx_flow_session_key_t           flow_key;
    jnx_flow_msg_stat_flow_info_t   *pinfo_msg;
    jnx_flow_data_flow_entry_t      *pflow_entry = NULL;
    jnx_flow_msg_sub_header_info_t  *subhdr = NULL, * rsp_subhdr = NULL;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    /*
     * get the op-command request filter information
     */
    subhdr    = (typeof(subhdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));
    pinfo_msg = (typeof(pinfo_msg))((uint8_t *)subhdr + sizeof(*subhdr));

    /*
     * prepare the response, copy the subheader+payload
     * from the request message
     */
    rsp_subhdr  = (typeof(rsp_subhdr))((uint8_t*)rsp_hdr + sizeof(*rsp_hdr));
    memcpy(rsp_subhdr, subhdr, ntohs(subhdr->msg_len));


    switch (subhdr->msg_type) {

        case JNX_FLOW_MSG_FETCH_FLOW_ENTRY:
            flow_key.svc_key.svc_type = pinfo_msg->flow_info.svc_type;
            flow_key.svc_key.svc_id   = ntohl(pinfo_msg->flow_info.svc_id);

            flow_key.session.src_addr = ntohl(pinfo_msg->flow_info.src_addr);
            flow_key.session.dst_addr = ntohl(pinfo_msg->flow_info.dst_addr);
            flow_key.session.src_port = ntohs(pinfo_msg->flow_info.src_port);
            flow_key.session.dst_port = ntohs(pinfo_msg->flow_info.dst_port);
            flow_key.session.proto    = pinfo_msg->flow_info.proto;
            if ((pflow_entry =
                 jnx_flow_data_flow_entry_lookup(data_cb, &flow_key))
                == NULL) {
                rsp_subhdr->err_code = JNX_FLOW_ERR_ENTRY_ABSENT;
                break;
            }
            jnx_flow_data_flow_entry_msg(data_cb, rsp_subhdr, pflow_entry);
            break;

        case JNX_FLOW_MSG_FETCH_FLOW_SUMMARY:
            jnx_flow_data_flow_summary_msg(data_cb, rsp_subhdr);
            break;

        case JNX_FLOW_MSG_FETCH_FLOW_EXTENSIVE:
            jnx_flow_data_flow_extensive_msg(data_cb, rsp_hdr);
            break;

        default:
            rsp_subhdr->err_code = JNX_FLOW_ERR_CONFIG_INVALID;
            break;
    }

    jnx_flow_data_send_msg_resp(data_cb, rsp_hdr->msg_type, rsp_hdr);

    return EOK;
}

/**
 * (TBD) XXX
 * This function clears the flow database
 * @param  data_cb   data control block pointer
 * @returns
 *    JNX_FLOW_ERR_NO_ERR     if successful
 *                            otherwise
 */
static jnx_flow_err_type_t
jnx_flow_data_flow_clear_all(jnx_flow_data_cb_t * data_cb __unused)
{
    jnx_flow_err_type_t err_code = JNX_FLOW_ERR_NO_ERROR;
    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);

    return err_code;
}

/**
 * This function handles the clear command message
 * @param  data_cb   data control block pointer
 * @param  msg_hdr   request header message structure pointer
 * @param  rsp_hdr   response header message structure pointer
 * @returns
 *    EOK     if successful
 *    EFAIL   otherwise
 */
static status_t
jnx_flow_data_handle_clear_msg(jnx_flow_data_cb_t * data_cb,
                               jnx_flow_msg_header_info_t * msg_hdr,
                               jnx_flow_msg_header_info_t * rsp_hdr)
{
    jnx_flow_session_key_t           flow_key;
    jnx_flow_data_flow_entry_t      *pflow_entry = NULL;
    jnx_flow_msg_stat_flow_info_t   *pinfo_msg = NULL;
    jnx_flow_msg_sub_header_info_t  *subhdr = NULL, *rsp_subhdr = NULL;

    jnx_flow_log(LOG_INFO, "%s:%d", __func__, __LINE__);
    /*
     * get the op-command request filter information
     */
    subhdr    = (typeof(subhdr))((uint8_t*)msg_hdr + sizeof(*msg_hdr));
    pinfo_msg = (typeof(pinfo_msg))((uint8_t*)subhdr + sizeof(*subhdr));

    /*
     * prepare the response, copy the subheader+payload
     * from the request message
     */
    rsp_subhdr  = (typeof(rsp_subhdr))((uint8_t*)rsp_hdr + sizeof(*rsp_hdr));
    memcpy(rsp_subhdr, subhdr, ntohs(subhdr->msg_len));

    switch (subhdr->msg_type) {

        case JNX_FLOW_MSG_CLEAR_FLOW_ENTRY:

            flow_key.svc_key.svc_type = pinfo_msg->flow_info.svc_type;
            flow_key.svc_key.svc_id   = ntohl(pinfo_msg->flow_info.svc_id);

            flow_key.session.src_addr = ntohl(pinfo_msg->flow_info.src_addr);
            flow_key.session.dst_addr = ntohl(pinfo_msg->flow_info.dst_addr);
            flow_key.session.src_port = ntohs(pinfo_msg->flow_info.src_port);
            flow_key.session.dst_port = ntohs(pinfo_msg->flow_info.dst_port);
            flow_key.session.proto    = pinfo_msg->flow_info.proto;

            /*
             * mark the entry as down, will be cleared in the
             * periodic event handler function
             */
            if ((pflow_entry =
                 jnx_flow_data_flow_entry_lookup(data_cb, &flow_key))
                == NULL) {
                rsp_subhdr->err_code = JNX_FLOW_ERR_ENTRY_ABSENT;
                break;
            }
            pflow_entry->flow_status = JNX_FLOW_DATA_STATUS_DOWN;

            rsp_subhdr->err_code = JNX_FLOW_ERR_NO_ERROR;
            break;

        case JNX_FLOW_MSG_CLEAR_FLOW_ALL:
            rsp_subhdr->err_code = jnx_flow_data_flow_clear_all(data_cb);
            break;

        default:
            rsp_subhdr->err_code = JNX_FLOW_ERR_CONFIG_INVALID;
            break;
    }
    jnx_flow_data_send_msg_resp(data_cb, rsp_hdr->msg_type, rsp_hdr);
    return EOK;
}

/**
 * This function handles the management module messages
 * @param  session   libconn session structure pointer
 * @param  ipc_msg   ipc message header message structure pointer
 * @param  cookie    data application control block pointer
 * @returns
 *    EOK     if successful
 *    EFAIL   otherwise
 */
status_t
jnx_flow_data_mgmt_msg_handler(pconn_session_t * session __unused,
                               ipc_msg_t * ipc_msg, void * cookie)
{
    jnx_flow_data_cb_t * data_cb = (typeof(data_cb))cookie;
    jnx_flow_msg_header_info_t * msg_hdr = NULL, *rsp_hdr = NULL;
    jnx_flow_msg_sub_header_info_t *subhdr = NULL;

    msg_hdr = (typeof(msg_hdr))(ipc_msg->data);

    /*
     * prepare the response buffer 
     */
    rsp_hdr = (typeof(rsp_hdr))data_cb->cb_send_buf;
    data_cb->cb_msg_len   = sizeof(*rsp_hdr);
    data_cb->cb_msg_count = 0;

    memcpy(rsp_hdr, msg_hdr, sizeof(*rsp_hdr));
    subhdr = (typeof(subhdr))((uint8_t *)rsp_hdr + sizeof(*rsp_hdr));

    jnx_flow_log(LOG_INFO, "%s:%d:\"%s\"", __func__, __LINE__,
                 jnx_flow_mesg_str[msg_hdr->msg_type]);

    switch (msg_hdr->msg_type) {

        case JNX_FLOW_MSG_CONFIG_SVC_INFO:
            jnx_flow_data_handle_svc_config(data_cb, msg_hdr, rsp_hdr);
            break;

        case JNX_FLOW_MSG_CONFIG_RULE_INFO:
            jnx_flow_data_handle_rule_config(data_cb, msg_hdr, rsp_hdr);
            break;

        case JNX_FLOW_MSG_CONFIG_SVC_RULE_INFO:
            jnx_flow_data_handle_svc_rule_config(data_cb, msg_hdr, rsp_hdr);
            break;

        case JNX_FLOW_MSG_FETCH_FLOW_INFO:
            jnx_flow_data_handle_flow_opcmd(data_cb, msg_hdr, rsp_hdr);
            break;

        case JNX_FLOW_MSG_FETCH_SVC_INFO:
            jnx_flow_data_handle_svc_opcmd(data_cb, msg_hdr, rsp_hdr);
            break;

        case JNX_FLOW_MSG_CLEAR_INFO:
            jnx_flow_data_handle_clear_msg(data_cb, msg_hdr, rsp_hdr);
            break;

        default:
            subhdr->err_code = JNX_FLOW_ERR_MESSAGE_INVALID;
            break;
    }

    return EOK;
}

/**
 * This function handles the management module events
 * @param  session   libconn session structure pointer
 * @param  event     libconn event type
 * @param  cookie    data application control block pointer
 * @returns
 *    EOK     if successful
 *    EFAIL   otherwise
 */
void
jnx_flow_data_mgmt_event_handler(pconn_session_t * session,
                                 pconn_event_t event, 
                                 void * cookie)
{
    jnx_flow_data_cb_t * data_cb = (typeof(data_cb))cookie;

    jnx_flow_log(LOG_INFO, "%s:%d:%d", __func__, __LINE__, event);

    if (data_cb == NULL) {
        return;
    }

    switch (event)
    {
        case PCONN_EVENT_ESTABLISHED:
            data_cb->cb_status = JNX_FLOW_DATA_STATUS_UP;
            data_cb->cb_conn_session = session;
            jnx_flow_log(LOG_INFO, "Management connect event");
            break;

        case PCONN_EVENT_SHUTDOWN:
            data_cb->cb_status = JNX_FLOW_DATA_STATUS_INIT;
            data_cb->cb_conn_session = NULL;
            jnx_flow_log(LOG_INFO, "Management shutdown event");
            break;

        case PCONN_EVENT_FAILED:
            data_cb->cb_conn_session = NULL;
            data_cb->cb_status = JNX_FLOW_DATA_STATUS_INIT;
            jnx_flow_log(LOG_INFO, "Management Connection fail event");
            break;

        default:
            break;
    }
    return;
}

/**
 * This function prints the flow table entries
 * @param  data_cb    data application control block pointer
 * @returns
 * None
 */
static void
jnx_flow_data_print_flow_table(jnx_flow_data_cb_t * data_cb)
{
    uint32_t hash = 0;
    jnx_flow_data_flow_entry_t     * pflow = NULL;

    printf("\nFLOW-TABLE");
    printf("\n----------\n");

    for (hash = 0; hash < JNX_FLOW_DATA_FLOW_BUCKET_COUNT; hash++)  {

        pflow = JNX_FLOW_DATA_HASH_CHAIN(data_cb, hash);

        if (pflow == NULL) {
            continue;
        }

        printf("\nhash-bucket\t%d", hash);
        printf("\n-----------\n");

        for (;(pflow); pflow = pflow->flow_next) {

            printf("svc-set-id   %d\tflow-direction  \"%s\"\n"
                   "flow-action  \"%s\"\n",
                   pflow->flow_svc_id,
                   jnx_flow_dir_str[pflow->flow_dir],
                   jnx_flow_action_str[pflow->flow_action]);

            printf("src-addr     %X\tdest-addr       %X\tproto %d\n"
                   "src-port     %d\tdst-port        %d\n",
                   pflow->flow_key.session.src_addr,
                   pflow->flow_key.session.dst_addr,
                   pflow->flow_key.session.proto,
                   pflow->flow_key.session.src_port,
                   pflow->flow_key.session.dst_port);

            printf("pkt_in       %d\tbytes_in        %d\n"
                   "pkt_out      %d\tbytes_out       %d\n"
                   "pkts_dropped %d\tbytes_dropped   %d\n\n",
                   pflow->flow_stats.pkts_in,
                   pflow->flow_stats.bytes_in,
                   pflow->flow_stats.pkts_out,
                   pflow->flow_stats.bytes_out,
                   pflow->flow_stats.pkts_dropped,
                   pflow->flow_stats.bytes_dropped);
        }
    }
}

/**
 * This function prints the service set entries
 * @param  data_cb    data application control block pointer
 * @returns
 * None
 */
static void
jnx_flow_data_print_svc_set_table(jnx_flow_data_cb_t * data_cb)
{
    uint32_t idx = 0;
    jnx_flow_data_svc_set_t * psvc_set = NULL;
    jnx_flow_data_rule_entry_t * prule = NULL;
    jnx_flow_data_list_entry_t * plist_entry = NULL;

    printf("\nSERVICE-SET-LIST");
    printf("\n----------------\n");
    while ((psvc_set = jnx_flow_data_svc_set_get_next(data_cb, psvc_set))) {

        printf("service-set-name        \"%s\"(%d)\n"
               "svc-set-type            \"%s\"\n",
               psvc_set->svc_name, psvc_set->svc_id,
               jnx_flow_svc_str[psvc_set->svc_type]);

        printf("rule_count              %d\tapp_rule_count          %d\n"
               "total_flow_count        %d\tactive_flow_count       %d\n"
               "total_allow_flow_count  %d\tactive_allow_flow_count %d\n"
               "total_drop_flow_count   %d\tactive_drop_flow_count  %d\n",
               psvc_set->svc_stats.rule_count,
               psvc_set->svc_stats.applied_rule_count,
               psvc_set->svc_stats.total_flow_count,
               psvc_set->svc_stats.active_flow_count,
               psvc_set->svc_stats.total_allow_flow_count,
               psvc_set->svc_stats.active_allow_flow_count,
               psvc_set->svc_stats.total_drop_flow_count,
               psvc_set->svc_stats.active_drop_flow_count);

        plist_entry = psvc_set->svc_rule_set.list_head;
        idx = 1;

        for (;(plist_entry); plist_entry = plist_entry->next, idx++) {
            prule = plist_entry->ptr;
            printf("svc-set-rule-index      %d\trule-name \"%s\"(%d)\n",
                   idx, prule->rule_name, prule->rule_id);
        }
        printf("\n");
    }
}

/**
 * This function prints the rule entries
 * @param  data_cb    data application control block pointer
 * @returns
 * None
 */
static void
jnx_flow_data_print_rule_table(jnx_flow_data_cb_t * data_cb)
{
    jnx_flow_data_rule_entry_t * prule = NULL;

    printf("\nRULE-LIST");
    printf("\n---------\n");
    while ((prule = jnx_flow_data_rule_get_next(data_cb, prule))) {

        printf("rule-name   \"%s\"(%d)\n"
               "rule-action \"%s\"\trule-direction \"%s\"\n",
               prule->rule_name, prule->rule_id,
               jnx_flow_action_str[prule->rule_action],
               jnx_flow_dir_str[prule->rule_direction]);

        printf("souce-addr   %X/%X\tdestination-addr %X/%X\tproto %d\n"
               "src-port       %d\tdest-port %d\n",
               prule->rule_session.session.src_addr,
               prule->rule_session.src_mask,
               prule->rule_session.session.dst_addr,
               prule->rule_session.dst_mask,
               prule->rule_session.session.proto,
               prule->rule_session.session.src_port,
               prule->rule_session.session.dst_port);

        printf("svc-ref-count  %d\tapp-count         %d\n"
               "tot-flow-count %d\tactive-flow-count %d\n\n",
               prule->rule_stats.rule_svc_ref_count,
               prule->rule_stats.rule_applied_count,
               prule->rule_stats.rule_total_flow_count,
               prule->rule_stats.rule_active_flow_count);
    }
}

/**
 * This function prints the jnx-flow-data information
 * @param  data_cb    data application control block pointer
 * @returns
 * None
 */
void
jnx_flow_data_print_all(jnx_flow_data_cb_t * data_cb)
{

    jnx_flow_data_print_rule_table(data_cb);
    jnx_flow_data_print_svc_set_table(data_cb);
    jnx_flow_data_print_flow_table(data_cb);
}

/*
 * $Id: jnx-flow-mgmt_rule.c 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * jnx-flow-mgmt_rule.c - rule related routines
 *
 * Vivek Gupta, Aug 2007
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
 * @file : jnx-flow-mgmt_rule.c
 * @brief
 * This file contains the jnx-flow-mgmt rule configuration 
 * management routines
 */
#include <stdlib.h>
#include <sys/param.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ctype.h>

#include <ddl/dax.h>

#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include JNX_FLOW_MGMT_OUT_H
#include <jnx/junos_trace.h>
#include <jnx/parse_ip.h>

#include <jnx/jnx-flow.h>
#include <jnx/jnx-flow_msg.h>

#include "jnx-flow-mgmt.h"
#include "jnx-flow-mgmt_config.h"
#include "jnx-flow-mgmt_rule.h"

static int 
jnx_flow_parse_rules (dax_walk_data_t *dwd __unused, ddl_handle_t *dop,
                      int action,  void *data);
static void
jnx_flow_add_rule_to_list(jnx_flow_cfg_t* flow_cfg, jnx_flow_rule_list_t* rule);

static int jnx_flow_get_rule_id(void);

static jnx_flow_rule_list_t*
jnx_flow_get_rule_from_list( jnx_flow_cfg_t* flow_cfg, char*  rule_name);
/**
 * This function reads the rule configuration for jnx-flow
 */
int 
jnx_flow_mgmt_read_rule_config(int check __unused, jnx_flow_cfg_t* flow_cfg)
{
    ddl_handle_t *top = NULL;
    uint32_t status = FALSE;
    const char * jnx_flow_rule_path[] = {
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_OBJ,
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_OBJ,
        DDLNAME_JNX_FLOW, DDLNAME_JNX_FLOW_RULE, NULL};

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"%s:%d:", __func__, __LINE__);

    /* check rule configuration */
    if (!dax_get_object_by_path(NULL, jnx_flow_rule_path, &top, FALSE))  {
        /*
         * delete thr rule entries
         */
        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "rule configuration is absent");
    } else {

        /* Check if the config has changed */
        if (!check && dax_is_changed(top) == FALSE) {
            jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"rule configuration no change");
            dax_release_object(&top);
            return status;
        }

        if (dax_walk_list (top, DAX_WALK_CONFIGURED,
                           jnx_flow_parse_rules, flow_cfg)) {
            jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"walk rule config list failed");

            dax_release_object(&top);
            return status; 
        }

        dax_release_object(&top);
    }

    return jnx_flow_mgmt_update_rule_list(flow_cfg);
}

/**
 * This function identifies the set of rules deleted in
 * the new candidate configuration
 */
int 
jnx_flow_mgmt_update_rule_list(jnx_flow_cfg_t * flow_cfg)
{
    jnx_flow_rule_node_t*   rule_node;
    jnx_flow_rule_list_t*   rule_list;
    /* Now find out if any node has been deleted */

    for (rule_node =
         (typeof(rule_node))patricia_find_next(&jnx_flow_mgmt.rule_db, NULL);
         rule_node != NULL;
         rule_node = (typeof(rule_node))
         patricia_find_next(&jnx_flow_mgmt.rule_db, &rule_node->node)) {

        /* means the rule is not deleted */
        if ((jnx_flow_get_rule_from_list(flow_cfg, rule_node->rule.rule_name))
            != NULL) {
            jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"rule <\"%s\", %d> is present",
                         rule_node->rule.rule_name, rule_node->rule_id);
            continue;
        }


        /* 
         * Implies this rule has been deleted from the config,
         * add to config list
         */
        if ((rule_list = calloc(1, sizeof(*rule_list))) == NULL) {
            jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d: calloc failed",
                         __func__, __LINE__);
            return TRUE;
        }

        rule_list->rule_node = rule_node;
        rule_list->op_type = JNX_FLOW_MSG_CONFIG_DELETE;

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"rule <\"%s\", %d> mark %s",
                     rule_node->rule.rule_name, rule_node->rule_id,
                     jnx_flow_config_op_str[rule_list->op_type]);

        jnx_flow_add_rule_to_list(flow_cfg, rule_list);
    }
    return FALSE;
}

/**
 * This function parses a rule entry for jnx-flow application
 */
static int 
jnx_flow_parse_rules (dax_walk_data_t *dwd __unused, ddl_handle_t *dop,
                     int action __unused, void *data)
{
    jnx_flow_rule_node_t*   rule_node;
    jnx_flow_rule_list_t*   rule;
    jnx_flow_cfg_t*         flow_cfg = (jnx_flow_cfg_t*)data;
    ddl_handle_t*           obj = NULL;

    if ((rule = calloc (1, sizeof(jnx_flow_rule_list_t))) == NULL) {

        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d:calloc failed", __func__, __LINE__);
        return DAX_WALK_ABORT;
    }

    if (!dax_get_stringr_by_aid(dop, DDLAID_JNX_FLOW_RULE_RULE_NAME,
                                rule->rule.rule_name, JNX_FLOW_STR_SIZE)) {

        free(rule);
        return DAX_WALK_ABORT;                    
    }

    if (!dax_get_int_by_aid(dop, DDLAID_JNX_FLOW_RULE_MATCH_DIRECTION,
                            &rule->rule.match_dir)) {
        free(rule);
        return DAX_WALK_ABORT;
    }

    if (!dax_get_int_by_aid(dop, DDLAID_JNX_FLOW_RULE_THEN,
                            &rule->rule.action)) {

        free(rule);
        return DAX_WALK_ABORT;
    }


    if ((!dax_get_object_by_name(dop, DDLNAME_JNX_FLOW_RULE_FROM,
                                 &obj, FALSE))) {
        free(rule);
        return DAX_WALK_ABORT;
    }

    if(!dax_get_ipv4prefix_by_aid (obj,
                                   DDLAID_JNX_FLOW_MATCH_SOURCE_PREFIX, 
                                 &(rule->rule.match.session.src_addr),
                                 &(rule->rule.match.src_mask))) {
        free(rule);
        dax_release_object(&obj);
        return DAX_WALK_ABORT;
    }

    rule->rule.match.session.src_addr =
        ntohl(rule->rule.match.session.src_addr);
    rule->rule.match.src_mask = ntohl(rule->rule.match.src_mask);

    if(!dax_get_ipv4prefix_by_aid (obj,
                                   DDLAID_JNX_FLOW_MATCH_DESTINATION_PREFIX, 
                                 &(rule->rule.match.session.dst_addr),
                                 &(rule->rule.match.dst_mask))) {
        free(rule);
        dax_release_object(&obj);
        return DAX_WALK_ABORT;
    }

    rule->rule.match.session.dst_addr =
        ntohl(rule->rule.match.session.dst_addr);
    rule->rule.match.dst_mask = ntohl(rule->rule.match.dst_mask);

    if (!dax_get_int_by_aid(obj, DDLAID_JNX_FLOW_MATCH_PROTOCOL,
                            (uint32_t*)&rule->rule.match.session.proto)) {
        free(rule);
        dax_release_object(&obj);
        return DAX_WALK_ABORT;
    }

    if (!dax_get_ushort_by_aid(obj, DDLAID_JNX_FLOW_MATCH_SOURCE_PORT,
                               &rule->rule.match.session.src_port)) {
        free(rule);
        dax_release_object(&obj);
        return DAX_WALK_ABORT;
    }

    if (!dax_get_ushort_by_aid(obj, DDLAID_JNX_FLOW_MATCH_DESTINATION_PORT,
                               &rule->rule.match.session.dst_port)) {
        free(rule);
        dax_release_object(&obj);
        return DAX_WALK_ABORT;
    }

    /*
     * Now that the rule has been parsed completely, 
     * add it to the list of rules present in the cfg 
     * check is the entry is present, try to allocate
     * an entry for it. otherwise compare the attributes
     * & mark it as changed, if something has changed
     */

    if ((rule_node = (typeof(rule_node))
         patricia_get(&jnx_flow_mgmt.rule_db,
                      strlen(rule->rule.rule_name) + 1, 
                      rule->rule.rule_name)) == NULL) {

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"rule \"%s\" is absent",
                     rule->rule.rule_name);

        if (!(rule_node = calloc(1, sizeof(*rule_node)))) {
            jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d:calloc failed",
                         __func__, __LINE__);
            return FALSE;
        }

        memcpy(&rule_node->rule, &rule->rule, sizeof(jnx_flow_rule_t));

        rule_node->rule_id = jnx_flow_get_rule_id();
        rule->op_type      = JNX_FLOW_MSG_CONFIG_ADD;

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"rule <\"%s\", %d> add",
                      rule_node->rule.rule_name, rule_node->rule_id);

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"rule action \"%s\"",
                     jnx_flow_action_str[rule_node->rule.action]);

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"rule direction \"%s\"",
                     jnx_flow_dir_str[rule_node->rule.match_dir]);

    } else {
        /* 
         * The node is already present in the tree, check if there is any
         * change in the config.
         */

        if (memcmp(&rule_node->rule, &rule->rule, sizeof(jnx_flow_rule_t))) {

            /* 
             * Implies rule has changed, just update the info to the tree.
             */
            jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"rule <\"%s\", %d> change",
                         rule_node->rule.rule_name, rule_node->rule_id);
            rule->op_type = JNX_FLOW_MSG_CONFIG_CHANGE;
        }
    }

    rule->rule_node = rule_node;

    jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"add rule <\"%s\", %d> to config list",
                 rule_node->rule.rule_name, rule_node->rule_id);

    jnx_flow_add_rule_to_list(flow_cfg, rule);

    dax_release_object(&obj);

    return DAX_WALK_OK;
}

/**
 * This function updates the rule data base with the candidate
 * configuration
 */
int
jnx_flow_mgmt_update_rule_db(jnx_flow_cfg_t* flow_cfg)
{
    jnx_flow_rule_list_t*   rule_list;
    jnx_flow_rule_node_t*   rule_node;

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"%s:%d:update rule db", __func__, __LINE__);

    /* now scan the list for updating the data base */

    for (rule_list  = flow_cfg->rule_list; (rule_list != NULL);
         rule_list = rule_list->next) {

        if (!(rule_node = rule_list->rule_node)) {
            jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"rule \"%s\" is absent",
                         rule_list->rule.rule_name);
            assert(0);
            continue;
        }

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"%s:%d:rule \"%s\" %s",
                     rule_node->rule.rule_name,
                     jnx_flow_config_op_str[rule_list->op_type]);

        switch (rule_list->op_type) {
            case JNX_FLOW_MSG_CONFIG_ADD:
                patricia_node_init_length(&rule_node->node, 
                                          strlen(rule_node->rule.rule_name)
                                          + 1);
                if (!patricia_add(&jnx_flow_mgmt.rule_db, &rule_node->node)) {
                    jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d:rule \"%s\""
                                 " patricia add failed",
                                 __func__, __LINE__,
                                 rule_node->rule.rule_name);
                }
                jnx_flow_mgmt.rule_count++;

                break;
            case JNX_FLOW_MSG_CONFIG_CHANGE:
                memcpy(&rule_node->rule, &rule_list->rule,
                       sizeof(jnx_flow_rule_t));
                break;
            case JNX_FLOW_MSG_CONFIG_DELETE:
                break;
            default: 
                break;
        }
    }

    return FALSE;
}

/**
 * This function frames a rule message to be sent to the data agent
 * applications running on the ms-pic
 */
uint32_t
jnx_flow_mgmt_frame_rule_msg(jnx_flow_rule_node_t * rule_node,
                             jnx_flow_msg_sub_header_info_t * sub_hdr,
                             jnx_flow_config_type_t op_type)
{
    jnx_flow_msg_rule_info_t * prule_msg;
    jnx_flow_rule_t * prule = NULL;

    if ((rule_node == NULL) || (op_type == JNX_FLOW_MSG_CONFIG_INVALID)) {
        return 0;
    }

    prule = &rule_node->rule;

    sub_hdr->msg_type = op_type;
    sub_hdr->err_code = JNX_FLOW_ERR_NO_ERROR;
    sub_hdr->msg_len  = htons(sizeof(*sub_hdr) + sizeof(*prule_msg));

    prule_msg = (typeof(prule_msg))((uint8_t *)sub_hdr + sizeof(*sub_hdr));

    memset(prule_msg, 0, sizeof(*prule_msg));

    strncpy(prule_msg->rule_name, rule_node->rule.rule_name,
            sizeof(prule_msg->rule_name));

    prule_msg->rule_index          = htonl(rule_node->rule_id);
    prule_msg->rule_match          = JNX_FLOW_RULE_MATCH_5TUPLE;

    prule_msg->rule_action         = prule->action;
    prule_msg->rule_direction      = prule->match_dir;

    prule_msg->rule_src_mask       = htonl(prule->match.src_mask);
    prule_msg->rule_dst_mask       = htonl(prule->match.dst_mask);

    prule_msg->rule_flow.src_addr  = htonl(prule->match.session.src_addr);
    prule_msg->rule_flow.dst_addr  = htonl(prule->match.session.dst_addr);

    prule_msg->rule_flow.src_port  = htons(prule->match.session.src_port);
    prule_msg->rule_flow.dst_port  = htons(prule->match.session.dst_port);

    prule_msg->rule_flow.proto     = prule->match.session.proto;

    return (sizeof(*sub_hdr) + sizeof(*prule_msg));
}

/**
 * This function frames and sends the rule messages to the data agent
 * applications running on the ms-pic
 */
status_t
jnx_flow_mgmt_send_rule_config(jnx_flow_mgmt_data_session_t * pdata_pic,
                               jnx_flow_cfg_t  * flow_cfg)
{
    jnx_flow_config_type_t op_type = JNX_FLOW_MSG_CONFIG_ADD;
    uint32_t  msg_len = 0, msg_count = 0, sub_len = 0;
    jnx_flow_rule_node_t*   rule_node = NULL;
    jnx_flow_rule_list_t*   rule;
    jnx_flow_msg_header_info_t * msg_hdr;
    jnx_flow_msg_sub_header_info_t * sub_hdr = NULL;

    msg_hdr = (typeof(msg_hdr))jnx_flow_mgmt.send_buf;

    memset(msg_hdr, 0, sizeof(*msg_hdr));

    msg_hdr->msg_type = JNX_FLOW_MSG_CONFIG_RULE_INFO;
    msg_len = sizeof(*msg_hdr);
    msg_count = 0;

    sub_hdr = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));

    JNX_FLOW_MGMT_CFG_FIRST_RULE_NODE(flow_cfg, rule_node);

    for (; (rule_node); 
         JNX_FLOW_MGMT_CFG_NEXT_RULE_NODE(flow_cfg, rule_node)){

        if ((msg_count > 250) || ((msg_len + sub_len) > JNX_FLOW_BUF_SIZE)) {
            msg_hdr->msg_len   = htons(msg_len);
            msg_hdr->msg_count = msg_count;
            jnx_flow_mgmt_send_config_msg(pdata_pic, msg_hdr, 
                                          msg_hdr->msg_type, msg_len);
            msg_hdr = (typeof(msg_hdr))jnx_flow_mgmt.send_buf;
            msg_len = sizeof(*msg_hdr);
            sub_hdr = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));
            msg_count = 0;
        }


        if ((sub_len = jnx_flow_mgmt_frame_rule_msg(rule_node,
                                                    sub_hdr, op_type))) {
            msg_len +=  sub_len;
            msg_count++;
            sub_hdr = (typeof(sub_hdr))((uint8_t *)sub_hdr + sub_len);
        }
    }

    if (msg_count) {
        msg_hdr->msg_len   = htons(msg_len);
        msg_hdr->msg_count = msg_count;
        jnx_flow_mgmt_send_config_msg(pdata_pic, msg_hdr,
                                      msg_hdr->msg_type, msg_len);
    }

     return EOK;
}

/**
 * This function deletes a rule entry from the active data base
 */
void
jnx_flow_mgmt_delete_rule(jnx_flow_rule_node_t * rule_node)
{
    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "rule <\"%s\", %d> delete",
                 rule_node->rule.rule_name, rule_node->rule_id);

    if (!patricia_delete (&jnx_flow_mgmt.rule_db, &rule_node->node)) {
        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,
                     "%s:%d:rule <\"%s\", %d> patricia delete failed",
                     __func__, __LINE__,
                     rule_node->rule.rule_name, rule_node->rule_id);
    }
    jnx_flow_mgmt.rule_count--;
    free(rule_node);
}

/**
 * This function adds a rule to the candidate config list
 */
static void
jnx_flow_add_rule_to_list(jnx_flow_cfg_t* flow_cfg, jnx_flow_rule_list_t* rule)
{
    rule->next = flow_cfg->rule_list;
    flow_cfg->rule_list = rule;
    return;
}

/**
 * This function generates a rule number for a new rule entry
 */
static int 
jnx_flow_get_rule_id()
{
    static int rule_id = 0;
    return rule_id++;
}

/**
 * This function searches for rule entry in the candidate configuration
 * list
 */
static jnx_flow_rule_list_t*
jnx_flow_get_rule_from_list(jnx_flow_cfg_t* flow_cfg, 
                             char*  rule_name)
{

    jnx_flow_rule_list_t* tmp = NULL;

    for (tmp = flow_cfg->rule_list;
         tmp != NULL;
         tmp = tmp->next) {

        if(memcmp (tmp->rule.rule_name, rule_name, JNX_FLOW_STR_SIZE) == 0) {
            break;
        }
    }
    return tmp;
}

/*
 * $Id: jnx-flow-mgmt_rule.h 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * jnx-flow-mgmt_rule.h - config data structures.
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
 * @file : jnx-flow-mgmt_rule.h
 * @brief
 * This file contains the jnx-flow-mgmt rule entry
 * configuration data structures
 */
#ifndef _JNX_FLOW_MGMT_RULE_H_
#define _JNX_FLOW_MGMT_RULE_H_

#include "jnx-flow-mgmt_config.h"


typedef struct jnx_flow_rule_s {

    char                        rule_name[JNX_FLOW_STR_SIZE];
    jnx_flow_rule_match_t       match;
    int32_t                     match_dir;
    int32_t                     action;
}jnx_flow_rule_t;

typedef struct jnx_flow_rule_node_s {
    
    patnode                 node;
    jnx_flow_rule_t         rule;
    uint16_t               rule_id;
    uint8_t                use_count;
}jnx_flow_rule_node_t;

typedef struct jnx_flow_rule_list_s {
    struct jnx_flow_rule_list_s*    next;
    jnx_flow_rule_t                 rule;
    jnx_flow_rule_node_t           *rule_node;
    jnx_flow_config_type_t          op_type;  /* used in rule db */
}jnx_flow_rule_list_t;

typedef struct jnx_flow_rule_name_list_s {
    struct jnx_flow_rule_name_list_s*   next;
    jnx_flow_config_type_t              op_type; /* used in service set rule set */
    uint32_t                           rule_index;
    char                                rule_name[JNX_FLOW_STR_SIZE];
    jnx_flow_rule_node_t*               rule_node; 
}jnx_flow_rule_name_list_t;

#endif

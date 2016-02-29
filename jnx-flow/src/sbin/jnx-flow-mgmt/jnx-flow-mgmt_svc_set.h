/*
 * $Id: jnx-flow-mgmt_svc_set.h 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * jnx-flow-mgmt_svc_set.h - config data structures.
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
 * @file : jnx-flow-mgmt_svc_set.h
 * @brief
 * This file contains the jnx-flow-mgmt service set entry
 * configuration data structures
 */

#ifndef _JNX_FLOW_MGMT_SVC_SET_H_
#define _JNX_FLOW_MGMT_SVC_SET_H_

#include <jnx/if_shared_pub.h>
#include "jnx-flow-mgmt_rule.h"

typedef struct jnx_flow_intf_svc_s {

    char    svc_intf[JNX_FLOW_STR_SIZE]; 

}jnx_flow_intf_svc_t;

typedef struct jnx_flow_next_hop_svc_s {

    char    inside_interface[IFNAMELEN];
    char    outside_interface[IFNAMELEN];

}jnx_flow_next_hop_svc_t;

typedef struct jnx_flow_svc_info_s {
    
    jnx_flow_svc_type_t     svc_type;

    union {
        jnx_flow_intf_svc_t     intf_svc;
        jnx_flow_next_hop_svc_t next_hop_svc;
    }info;
    
}jnx_flow_svc_info_t;

typedef struct jnx_flow_svc_set_s {

    char                       svc_set_name[JNX_FLOW_STR_SIZE];
    jnx_flow_svc_info_t        svc_info; 
    jnx_flow_rule_name_list_t* rule_list;
    
}jnx_flow_svc_set_t;


typedef struct jnx_flow_svc_set_node_s {

   patnode                node; 
   jnx_flow_svc_set_t     svc_set;
   nh_idx_t               in_nh_idx;
   nh_idx_t               out_nh_idx;
   uint16_t              svc_set_id;
}jnx_flow_svc_set_node_t;

typedef struct jnx_flow_svc_set_list_s {

    struct jnx_flow_svc_set_list_s*     next;
    uint32_t                            is_jnx_flow;
    uint32_t                            rule_index;
    jnx_flow_svc_set_t                  svc_set;
    jnx_flow_svc_set_node_t            *svc_set_node;
    jnx_flow_config_type_t              op_type;
    jnx_flow_rule_name_list_t          *rule_list;
}jnx_flow_svc_set_list_t;

#define JNX_FLOW_SVC_SET_ID_UNASSIGNED      0xffff
#define JNX_FLOW_NEXTHOP_IDX_UNASSIGNED     0xffffffff

#endif

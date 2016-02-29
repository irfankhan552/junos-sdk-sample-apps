/*
 * $Id: jnx-flow-mgmt_config.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-flow-mgmt_config.h - config data structures.
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
 * @file : jnx-flow-mgmt_config.h
 * @brief
 * This file contains the jnx-flow-mgmt configuration handler
 * data structures and configuration macros
 */
#ifndef __JNX_FLOW_MGMT_CONFIG_H__
#define __JNX_FLOW_MGMT_CONFIG_H__

#include <jnx/jnx-flow.h>
#include "jnx-flow-mgmt_rule.h"
#include "jnx-flow-mgmt_svc_set.h"
#include "jnx-flow-mgmt.h"

typedef struct jnx_flow_cfg_s {
    uint32_t                   check;

    jnx_flow_rule_list_t*      rule_list;    /** < List of Rules configured 
                                               for jnx-flow */
    jnx_flow_svc_set_list_t*   svc_set_list; /** < List of Service Sets 
                                               configured for jnx-flow 
                                               service */
} jnx_flow_cfg_t;

extern jnx_flow_cfg_t  jnx_flow_cfg;

#define JNX_FLOW_MGMT_CFG_FIRST_SVC_SET_NODE(flow_cfg, svc_set) \
    ({\
    if (flow_cfg) {\
        svc_set = flow_cfg->svc_set_list;\
        svc_set_node = (svc_set) ? svc_set->svc_set_node : NULL;\
        op_type = (svc_set) ? \
            svc_set->op_type : JNX_FLOW_MSG_CONFIG_INVALID;\
    } else {\
        svc_set = NULL;\
        svc_set_node = (typeof(svc_set_node))\
            patricia_find_next(&jnx_flow_mgmt.svc_set_db, NULL);\
    } })

#define JNX_FLOW_MGMT_CFG_NEXT_SVC_SET_NODE(flow_cfg, svc_set) \
    ({if (flow_cfg) { \
     svc_set = svc_set->next;\
     svc_set_node = (svc_set) ? svc_set->svc_set_node : NULL;\
     op_type = (svc_set) ? \
         svc_set->op_type : JNX_FLOW_MSG_CONFIG_INVALID;\
     } else {\
     svc_set_node = (typeof(svc_set_node))\
     patricia_find_next(&jnx_flow_mgmt.svc_set_db, &svc_set_node->node);\
     }})

#define JNX_FLOW_MGMT_CFG_FIRST_RULE_NODE(flow_cfg, rule_node) \
    ({\
     if ((flow_cfg) && (flow_cfg->rule_list)) {\
     rule  = flow_cfg->rule_list;\
     rule_node = rule->rule_node;\
     op_type = (rule) ? rule->op_type: JNX_FLOW_MSG_CONFIG_INVALID;\
     } else {\
     rule  = NULL;\
     rule_node = (typeof(rule_node))\
     patricia_find_next(&jnx_flow_mgmt.rule_db, NULL);\
     }})

#define JNX_FLOW_MGMT_CFG_NEXT_RULE_NODE(flow_cfg, rule_node) \
    ({\
     if (flow_cfg) {\
     rule      = rule->next;\
     rule_node = (rule) ? rule->rule_node : NULL;\
     op_type = (rule) ? rule->op_type: JNX_FLOW_MSG_CONFIG_INVALID;\
     } else {\
     rule_node = (typeof(rule_node))\
     patricia_find_next(&jnx_flow_mgmt.rule_db,\
                        &rule_node->node);\
     }})

/* Function to initialise the config info related trees */
extern int jnx_flow_mgmt_config_init(void);

/* Function to read the config */
extern int jnx_flow_mgmt_config_read(int check);

extern int jnx_flow_mgmt_read_rule_config(int check, jnx_flow_cfg_t* flow_cfg);

extern int jnx_flow_mgmt_update_rule_db(jnx_flow_cfg_t* flow_cfg);

extern void jnx_flow_mgmt_free_cfg(jnx_flow_cfg_t*  flow_cfg);

extern int jnx_flow_mgmt_read_service_set_config(int check, jnx_flow_cfg_t* flow_cfg);

extern int jnx_flow_mgmt_update_service_set_db(jnx_flow_cfg_t* flow_cfg);

extern uint32_t 
jnx_flow_mgmt_frame_svc_msg(jnx_flow_svc_set_node_t * psvc_set_node,
                            jnx_flow_msg_sub_header_info_t * sub_hdr,
                            uint32_t force);
extern uint32_t 
jnx_flow_mgmt_frame_svc_rule_msg(jnx_flow_msg_sub_header_info_t * sub_hdr,
                                 jnx_flow_svc_set_node_t * psvc_set,
                                 jnx_flow_rule_name_list_t * prule_name,
                                 uint32_t  op_type, uint32_t force);
extern uint32_t 
jnx_flow_mgmt_frame_rule_msg(jnx_flow_rule_node_t * prule_node,
                             jnx_flow_msg_sub_header_info_t * sub_hdr,
                             uint32_t force);
extern status_t
jnx_flow_mgmt_send_svc_config(jnx_flow_mgmt_data_session_t * pdata_pic,
                              jnx_flow_cfg_t  * flow_cfg);

extern status_t
jnx_flow_mgmt_send_svc_rule_config(jnx_flow_mgmt_data_session_t * pdata_pic,
                                   jnx_flow_cfg_t  * flow_cfg, uint32_t flag);
extern status_t
jnx_flow_mgmt_send_rule_config(jnx_flow_mgmt_data_session_t * pdata_pic,
                               jnx_flow_cfg_t  * flow_cfg);
extern status_t
jnx_flow_mgmt_send_svc_set_rule_config(jnx_flow_mgmt_data_session_t * pdata_pic,
                                       jnx_flow_svc_set_node_t * svc_set_node,
                                       uint32_t op_type);
extern status_t
jnx_flow_mgmt_send_svc_set_config(jnx_flow_mgmt_data_session_t * pdata_pic,
                                  jnx_flow_svc_set_node_t    * svc_set_node,
                                  uint32_t op_type);
extern void 
jnx_flow_mgmt_delete_svc_set(jnx_flow_svc_set_node_t * svc_set_node);
extern void 
jnx_flow_mgmt_delete_rule(jnx_flow_rule_node_t * rule_node);

extern int
jnx_flow_mgmt_update_svc_set_list(jnx_flow_cfg_t * flow_cfg);
int 
jnx_flow_mgmt_update_rule_list(jnx_flow_cfg_t * flow_cfg);
#endif /* __JNX_FLOW_MGMT_CONFIG_H__ */

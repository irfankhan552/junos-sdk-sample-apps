/*
 *$Id: jnx-flow_msg.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 *jnx-flow_msg.h - Definition of message type & structures
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
 * @file jnx-flow.h
 * @brief This file describes the various common structures & message
 * formats used across JNX-FLOW-DATA, JNX_FLOW-MGMT
 */

#ifndef _JNX_FLOW_MSG_H_
#define _JNX_FLOW_MSG_H_


/*****************************************************************
 *              MESSAGE HEADER MESSAGE TYPE VALUES               *
 *****************************************************************/

typedef enum jnx_flow_msg_type {
    /*
     * configuration command message types 
     */
    JNX_FLOW_MSG_INVALID_MSG = 0,         /**< invalid message  */
    JNX_FLOW_MSG_CONFIG_SVC_INFO,         /**< service message  */
    JNX_FLOW_MSG_CONFIG_RULE_INFO,        /**< rule info message*/
    JNX_FLOW_MSG_CONFIG_SVC_RULE_INFO,    /**< service rule message*/
    /*
     * opearational command message types 
     */
    JNX_FLOW_MSG_FETCH_FLOW_INFO,         /**< flow fetch message */
    JNX_FLOW_MSG_FETCH_RULE_INFO,         /**< rule fetch message */
    JNX_FLOW_MSG_FETCH_SVC_INFO,          /**< service fetch message */
    /*
     * clear command message types 
     */
    JNX_FLOW_MSG_CLEAR_INFO               /**< clear flow message */
} jnx_flow_msg_type_t;


/*****************************************************************
 *              MESSAGE SUB-HEADER OP-CODE TYPE VALUES           *
 *****************************************************************/

/*
 * configuration operation types
 */
typedef enum jnx_flow_config_type {
    JNX_FLOW_MSG_CONFIG_INVALID = 0, /**< invalid op code */
    JNX_FLOW_MSG_CONFIG_ADD,         /**< config add */
    JNX_FLOW_MSG_CONFIG_DELETE,      /**< config delete */
    JNX_FLOW_MSG_CONFIG_CHANGE       /**< config change */
} jnx_flow_config_type_t;

/*
 * operational command operation types
 */
typedef enum jnx_flow_fetch_svc_type {
    JNX_FLOW_MSG_FETCH_SVC_INVALID = 0, /**< invalid op code */
    JNX_FLOW_MSG_FETCH_SVC_ENTRY,       /**< service entry */
    JNX_FLOW_MSG_FETCH_SVC_SUMMARY,     /**< service summary */
    JNX_FLOW_MSG_FETCH_SVC_EXTENSIVE    /**< service extensive */
} jnx_flow_fetch_svc_type_t;

typedef enum jnx_flow_fetch_flow_type {
    JNX_FLOW_MSG_FETCH_FLOW_INVALID  = 0, /**< invalid op code */
    JNX_FLOW_MSG_FETCH_FLOW_ENTRY,        /**< flow entry */
    JNX_FLOW_MSG_FETCH_FLOW_SUMMARY,      /**< flow summary */
    JNX_FLOW_MSG_FETCH_FLOW_EXTENSIVE     /**< flow flow extensive */
} jnx_flow_fetch_flow_type_t;

typedef enum jnx_flow_fetch_rule_type {
    JNX_FLOW_MSG_FETCH_RULE_INVALID = 0, /**< invalid op code */
    JNX_FLOW_MSG_FETCH_RULE_ENTRY,       /**< rule entry */
    JNX_FLOW_MSG_FETCH_RULE_SUMMARY,     /**< rule summary */
    JNX_FLOW_MSG_FETCH_RULE_EXTENSIVE,   /**< rule extensive */
} jnx_flow_fetch_rule_type_t;

/*
 * clear command operation types
 */
typedef enum {
    JNX_FLOW_MSG_CLEAR_FLOW_INVALID = 0,  /**< invalid op code */
    JNX_FLOW_MSG_CLEAR_FLOW_ALL,          /**< all flow */
    JNX_FLOW_MSG_CLEAR_FLOW_ENTRY,        /**< flow entry */
    JNX_FLOW_MSG_CLEAR_FLOW_FOR_RULE,     /**< rule flow */
    JNX_FLOW_MSG_CLEAR_FLOW_FOR_SERVICE,  /**< service flow */
    JNX_FLOW_MSG_CLEAR_FLOW_SERVICE_TYPE, /**< service-type flow */
    JNX_FLOW_MSG_CLEAR_FLOW_MAX_TYPE      /**< clear max op code */
} jnx_flow_msg_clear_type_t;

/*****************************************************************
 *         CONFIGURATON MESSAGE PAYLOAD STRUCTURES               *
 *****************************************************************/

/*
 * configuration service information message structure 
 */
typedef struct jnx_flow_msg_svc_info {
    uint32_t   svc_index;                   /**< service set index */
    uint8_t    svc_name[JNX_FLOW_STR_SIZE]; /**< service set name */
    uint8_t    svc_intf[JNX_FLOW_STR_SIZE]; /**< service interface */
    uint8_t    svc_flags;                   /**< serivice_flags */
    uint8_t    svc_type;                    /**< service set style */
    uint16_t   svc_rule_count;              /**< service set rule count */
    uint32_t   svc_in_subunit;              /**< ingress subunit */
    uint32_t   svc_out_subunit;             /**< egress subunit */
} jnx_flow_msg_svc_info_t;

/*
 * configuration rule information message structure 
 */
typedef struct jnx_flow_msg_rule_info {
    uint32_t             rule_index;                   /**< rule index */
    uint8_t              rule_name[JNX_FLOW_STR_SIZE]; /**< rule name */
    uint8_t              rule_action;                  /**< action type */
    uint8_t              rule_match;                   /**< rule match type */
    uint8_t              rule_direction;               /**< rule direction */
    uint8_t              rule_flags;                   /**< rule action */
    uint32_t             rule_src_mask;                /**< source mask */
    uint32_t             rule_dst_mask;                /**< destination mask */
    jnx_flow_flow_info_t rule_flow;                    /**< rule flow info  */
} jnx_flow_msg_rule_info_t;

/*
 * configuration service-rule set information message structure 
 */
typedef struct jnx_flow_msg_svc_rule_info {
    uint32_t   svc_index;           /**< service set index */
    uint32_t   svc_svc_rule_index;  /**< service set rule index */
    uint32_t   svc_rule_index;      /**< rule index */
} jnx_flow_msg_svc_rule_info_t; 

/*****************************************************************
 *         OPERATIONAL COMMAND MESSAGE PAYLOAD STRUCTURES        *
 *****************************************************************/

/*
 * op-command flow summary information message structure
 */
typedef struct jnx_flow_msg_stat_flow_summary_info {
    jnx_flow_data_stat_t  stats;    /**< data module summary flow stats */
} jnx_flow_msg_stat_flow_summary_info_t;

/*
 * op-command flow information message structure
 */
typedef struct jnx_flow_msg_stat_flow_info {
    jnx_flow_flow_info_t  flow_info;   /**< flow information */
    jnx_flow_flow_stats_t flow_stats;  /**< flow statistics */
} jnx_flow_msg_stat_flow_info_t;

/*
 * op-command service summary information message structure
 */
typedef struct jnx_flow_msg_stat_svc_summary_info {
    uint32_t              svc_set_count; /**< service set count */
    jnx_flow_svc_stats_t  stats;         /**< service set statistics */
} jnx_flow_msg_stat_svc_summary_info_t;

/*
 * op-command service information message structure
 */
typedef struct jnx_flow_msg_stat_svc_set_info {
    uint8_t              svc_type;  /**< service type */
    uint8_t              svc_flags; /**< service flags */
    uint16_t             svc_pad;   /**< service resv */
    uint32_t             svc_index; /**< service index */
    uint8_t              svc_name[JNX_FLOW_STR_SIZE]; /**< service name */
    jnx_flow_svc_stats_t svc_stats; /**< service statistics */
} jnx_flow_msg_stat_svc_set_info_t;

/*
 * op-command rule summary information message structure
 */
typedef struct jnx_flow_msg_stat_rule_summary_info {
    uint32_t                rule_count; /**< rule count */
    jnx_flow_rule_stats_t   rule_stats; /**< rule statistics */
} jnx_flow_msg_stat_rule_summary_info_t;

/*
 * op-command rule entry information message structure
 */
typedef struct jnx_flow_msg_stat_rule_info {
    uint8_t                 rule_name[JNX_FLOW_STR_SIZE];/**< rule name */
    uint32_t                rule_index;                  /**< rule index */
    jnx_flow_rule_stats_t   rule_stats;                  /**< rule statistics */
} jnx_flow_msg_stat_rule_info_t;


/*
 * op-command data application summary information message structure
 */
typedef struct jnx_flow_msg_stat_summary_info {
    jnx_flow_msg_stat_rule_summary_info_t  rule_info;    /**< rule info*/
    jnx_flow_msg_stat_flow_summary_info_t  flow_info;    /**< flow info*/
} jnx_flow_msg_stat_summary_info_t;

/*****************************************************************
 *            CLEAR COMMAND MESSAGE PAYLOAD STRUCTURES           *
 *****************************************************************/
/*
 * clear command message information structure
 */
typedef struct jnx_flow_msg_clear_info {
    /*
     * the filter can be any of these, as determined by sub-header
     * opcode value
     */
    union {
        uint32_t             rule_id;    /**< rule index */
        uint32_t             svc_set_id; /**< service set index */
        jnx_flow_flow_info_t flow_info;  /**< flow information */
    };
} jnx_flow_msg_clear_info;

/*****************************************************************
 *                 TOP-LEVEL MESSAGE HEADER STRUCTURES           *
 *****************************************************************/
/*
 * message sub-header information structure 
 */
typedef struct jnx_flow_msg_sub_header_info {
    u_int8_t    msg_type;    /**< sub-header message type */
    u_int8_t    err_code;    /**< error code, in the response message */
    u_int16_t   msg_len;     /**< length of the message subheader + payload */
} jnx_flow_msg_sub_header_info_t;

/*
 * message header information structure 
 */
typedef struct jnx_flow_msg_header_info {
    uint8_t    msg_type;       /**< message type */
    uint8_t    msg_count;      /**< sub-header count in the message */
    uint16_t   msg_len;        /**< length of the message header + payload */
    uint16_t   msg_id;         /**< sequence ID of the message */
#if BYTE_ORDER == BIG_ENDIAN
    u_int8_t   more:1,         /**< more bit, more messages are expected*/
               rsvd:7;         /**< reserved bits */
#elif BYTE_ORDER == LITTLE_ENDIAN
    u_int8_t   rsvd:7,         /**< reserved bits */
               more:1;         /**< more bit, more messages are expected*/
#endif                         
    u_int8_t   rsvd1;          /**< reserved for future use */ 
} jnx_flow_msg_header_info_t;

#endif /* _JNX_FLOW_MSG_H_ */

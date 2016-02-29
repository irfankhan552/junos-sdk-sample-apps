/*
 *$Id: jnx-flow.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 *jnx-flow.h - Definition of the various common structures
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
 * @brief This file describes the various common structures used across
 * JNX-FLOW-DATA &  JNX-FLOW-MGMT
 */

#ifndef _JNX_FLOW_H_
#define _JNX_FLOW_H_

#include <sys/types.h>
#include <signal.h>
#include <syslog.h>
#include <jnx/trace.h>


/* 
 * Generic jnx-flow application speicific macro definitions
 */

/* 
 * The server sockets can be the same, as running on different entities(RE/PIC)
 */

#define JNX_FLOW_MGMT_PORT         3000 /**< server port on management */
#define JNX_FLOW_DATA_PORT         3000 /**< server port on data */
#define JNX_FLOW_DATA_MAX_CONN_IDX    1 /**< max data connection index */
#define JNX_FLOW_TS_SECOND_OFFSET    28 /**< get the seconds from the hw ts */


/*
 * structure field offset macros
 */

#define fldsiz(name, field)  (sizeof(((name *)0)->field))
#define fldoff(name, field)  ((int)&(((name *)0)->field))

#define JNX_FLOW_STR_SIZE           128  /**< length of the named entities */
#define JNX_FLOW_BUF_SIZE           1460 /**< buffer size */

#define JNX_FLOW_MGMT_MAX_DATA_IDX  10   /**< Maximum data connections to RE */

#define JNX_FLOW_RULE_IP_ADDR_ANY   0    /**< Ip address wild card */
#define JNX_FLOW_RULE_PORT_ANY      0    /**< Tcp/udp Port wild card */
#define JNX_FLOW_RULE_PROTO_ANY     0    /**< IP Protocol wild card */

/*
 * error codes
 */
typedef enum jnx_flow_err_type {
    JNX_FLOW_ERR_NO_ERROR  = 0,     /**< no error */
    JNX_FLOW_ERR_ALLOC_FAIL,        /**< malloc error */
    JNX_FLOW_ERR_FREE_FAIL,         /**< free error */
    JNX_FLOW_ERR_ENTRY_OP_FAIL,     /**< entry operation error */
    JNX_FLOW_ERR_ENTRY_INVALID,     /**< invalid entry error */
    JNX_FLOW_ERR_ENTRY_EXISTS,      /**< absent entry error */
    JNX_FLOW_ERR_ENTRY_ABSENT,      /**< absent entry error */
    JNX_FLOW_ERR_MESSAGE_INVALID,   /**< invalid message error */
    JNX_FLOW_ERR_OPERATION_INVALID, /**< invalid opeartion error */
    JNX_FLOW_ERR_CONFIG_INVALID     /**< invalid configuation error */
} jnx_flow_err_type_t;

/*
 * service style types
 * TBD(XXX) needs to be inherited from msp-lib
 */
typedef enum jnx_flow_svc_type {
    JNX_FLOW_SVC_TYPE_INVALID = 0, /**< invalid service style */
    JNX_FLOW_SVC_TYPE_INTERFACE,   /**< interface style */
    JNX_FLOW_SVC_TYPE_NEXTHOP,     /**< nexthop style */
    JNX_FLOW_SVC_TYPE_MAX          /**< style max */
} jnx_flow_svc_type_t;

/*
 * rule match types
 */
typedef enum jnx_flow_rule_match_type {
    JNX_FLOW_RULE_MATCH_INVALID = 0,  /**< invalid rule match */
    JNX_FLOW_RULE_MATCH_5TUPLE,       /**< 5 tuple rule match */
    JNX_FLOW_RULE_MATCH_MAX           /**< max rule match */
} jnx_flow_rule_match_type_t;

/*
 * rule action types
 */
typedef enum jnx_flow_rule_action_type {
    JNX_FLOW_RULE_ACTION_INVALID = 0,  /**< invalid rule action */
    JNX_FLOW_RULE_ACTION_ALLOW,        /**< allow rule action */
    JNX_FLOW_RULE_ACTION_DROP,         /**< drop rule action*/
    JNX_FLOW_RULE_ACTION_MAX,          /**< maximum rule action */
} jnx_flow_rule_action_type_t;

/*
 * Service set direction types
 */
typedef enum jnx_flow_rule_dir_type {
    JNX_FLOW_RULE_DIRECTION_INVALID = 0, /**< invalid rule direction */
    JNX_FLOW_RULE_DIRECTION_INPUT,       /**< input rule direction   */
    JNX_FLOW_RULE_DIRECTION_OUTPUT,      /**< output rule direction  */
    JNX_FLOW_RULE_DIRECTION_MAX          /**< maximum rule direction */
} jnx_flow_rule_dir_type_t;


/*****************************************************************
 *                   GENERIC JNX-FLOW STRUCTURES                 *
 *****************************************************************/

/*
 * service-set key information structure
 */
typedef struct jnx_flow_svc_key {
    uint8_t            svc_type;   /**< service type */
    uint8_t            svc_resv;   /**< service type */
    uint16_t           svc_pad;    /**< service type */
    union {
        uint32_t       svc_id;     /**< service id */
        uint32_t       svc_iif;    /**< service ingress interface */
        uint32_t       svc_key;    /**< service key */
    };
} jnx_flow_svc_key_t;

/*
 * flow session information structure
 */
typedef struct jnx_flow_session {
    uint32_t           src_addr;   /**< source ip address */
    uint32_t           dst_addr;   /**< destination ip address */
    uint16_t           src_port;   /**< source port */
    uint16_t           dst_port;   /**< destination port */
    uint8_t            proto;      /**< protocol type */
} jnx_flow_session_t;

/*
 * flow session key information structure
 */
typedef struct jnx_flow_session_key {
    jnx_flow_svc_key_t   svc_key;  /**< service key info */
    jnx_flow_session_t   session;  /**< session information */
} jnx_flow_session_key_t;

/*
 * rule match information structure
 */
typedef struct jnx_flow_rule_match {
    uint32_t              src_mask;     /**< source mask */
    uint32_t              dst_mask;     /**< destination mask */
    jnx_flow_session_t    session;      /**< session information */
} jnx_flow_rule_match_t;

/*
 * flow entry information message structure
 */
typedef struct jnx_flow_flow_info {
    uint8_t    svc_set_name[JNX_FLOW_STR_SIZE]; /**< service-set name */
    uint32_t   src_addr;       /**< source ip address */
    uint32_t   dst_addr;       /**< destination ip address */
    uint16_t   src_port;       /**< source port*/
    uint16_t   dst_port;       /**< destination port */
    uint32_t   svc_id;         /**< svc set id */
    uint8_t    proto;          /**< procol type */
    uint8_t    svc_type;       /**< service type */
    uint8_t    flow_dir;       /**< flow dirction */
    uint8_t    flow_action;    /**< flow action */
} jnx_flow_flow_info_t;

/*
 * flow statistics information message structure
 */
typedef struct jnx_flow_flow_stats {
    uint32_t   pkts_in;        /**< flow pkts in */
    uint32_t   pkts_out;       /**< flow pkts out */
    uint32_t   pkts_dropped;   /**< flow pkts dropped */
    uint32_t   bytes_in;       /**< flow bytes in */
    uint32_t   bytes_out;      /**< flow bytes out */
    uint32_t   bytes_dropped;  /**< flow bytes dropped */
} jnx_flow_flow_stats_t;

/*
 * rule statistics information message structure
 */
typedef struct jnx_flow_rule_stats {
    uint32_t    rule_svc_ref_count;     /**< rule service set ref. count */
    uint32_t    rule_applied_count;     /**< rule applied count */
    uint32_t    rule_total_flow_count;  /**< rule total flow count */
    uint32_t    rule_active_flow_count; /**< rule active flow count */
} jnx_flow_rule_stats_t;

/*
 * service-set statistics information message structure
 */
typedef struct jnx_flow_svc_stats {
    uint32_t    rule_count;              /**< total rule count */
    uint32_t    applied_rule_count;      /**< applied rule count */
    uint32_t    total_flow_count;        /**< total flow count */
    uint32_t    total_allow_flow_count;  /**< total allow flow count */
    uint32_t    total_drop_flow_count;   /**< total drop flow count */
    uint32_t    active_flow_count;       /**< active flow count */
    uint32_t    active_allow_flow_count; /**< active allow flow count */
    uint32_t    active_drop_flow_count;  /**< active drop flow count */
} jnx_flow_svc_stats_t;

/*
 * jnx-flow data application control block statistics structure 
 */
typedef struct jnx_flow_data_stat  {
    uint32_t        svc_set_count;            /**< service set count */
    uint32_t        rule_count;               /**< rule count */
    uint32_t        applied_rule_count;       /**< rule application count */
    uint32_t        total_flow_count;         /**< total flow count */
    uint32_t        total_allow_flow_count;   /**< total allow flow count */
    uint32_t        total_drop_flow_count;    /**< total drop flow count */
    uint32_t        active_flow_count;        /**< active flow count */
    uint32_t        active_allow_flow_count;  /**< active allow flow count */
    uint32_t        active_drop_flow_count;   /**< active drop flow count */
} jnx_flow_data_stat_t;

extern const char * jnx_flow_err_str[];
extern const char * jnx_flow_svc_str[];
extern const char * jnx_flow_match_str[];
extern const char * jnx_flow_action_str[];
extern const char * jnx_flow_dir_str[];
extern const char * jnx_flow_mesg_str[];
extern const char * jnx_flow_config_op_str[];
extern const char * jnx_flow_fetch_op_str[];
extern const char * jnx_flow_clear_op_str[];

#endif /* __JNX_FLOW_H__ */


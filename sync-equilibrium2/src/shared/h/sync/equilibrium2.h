/*
 * $Id: equilibrium2.h 418048 2010-12-30 18:58:42Z builder $
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
 * @file equilibrium2.h
 * 
 * @brief Contains function
 * 
 */

#ifndef __EQUILIBRIUM2_H__
#define __EQUILIBRIUM2_H__

#include <stdbool.h>
#include <sys/types.h>
#include <sys/queue.h>

/*** Constants ***/

#define MAX_NAME_LEN 64        /**< maximum length of name string */

/** balance service name */
#define EQ2_BALANCE_SVC_NAME    "equilibrium2-balance"

/** classify service name */
#define EQ2_CLASSIFY_SVC_NAME   "equilibrium2-classify"

#define EQ2_BALANCE_SVC      1                /**< balance service ID */
#define EQ2_CLASSIFY_SVC     2                /**< classify service ID */

#define BALANCE_PLUGIN       EQ2_BALANCE_SVC  /**< balance service plugin ID */
#define CLASSIFY_PLUGIN      EQ2_CLASSIFY_SVC /**< classify service plugin ID */

#define TERM_FROM_SVC_TYPE   1     /**< term match condition service type */
#define TERM_FROM_SVC_GATE   2     /**< term match condition service gate */
#define TERM_FROM_EXCEPT     0x100 /**< term match condition except */

#define TERM_THEN_ACCEPT     1     /**< term action accept */
#define TERM_THEN_DISCARD    2     /**< term action discard */
#define TERM_THEN_SVC_GATE   3     /**< term action service gate */
#define TERM_THEN_SVR_GROUP  4     /**< term action server group */

#define CONFIG_BLOB_SVR_GROUP       1  /**< config blob server group */
#define CONFIG_BLOB_SVC_SET         2  /**< config blob service-set */

#define EQ2_MGMT_SERVER_PORT          8100     /**< balance service TCP port */
#define EQ2_CLIENT_RETRY              10       /**< client retry number */
#define EQ2_BALANCE_MSG_SVR_GROUP     1        /**< server group message */
#define EQ2_SVC_INFO_MSG_LEN          256      /**< maximum message length */

/** Data structures **/

/** List item of server address. */
typedef struct svr_addr_s {
    LIST_ENTRY(svr_addr_s)  entry;           /**< list entry */
    in_addr_t               addr;            /**< IPv4 address */
    uint16_t                addr_ssn_count;  /**< number of sessions */
    bool                    addr_new;        /**< flag of new address */
} svr_addr_t;

/** List head of server address. */
typedef LIST_HEAD(svr_addr_head_s, svr_addr_s) svr_addr_head_t;

/** List item of server group. */
typedef struct svr_group_s {
    LIST_ENTRY(svr_group_s) entry;              /**< list entry */
    char                    group_name[MAX_NAME_LEN]; /**< group name */
    svr_addr_head_t         group_addr_head;    /**< head of address list */
    int                     group_addr_count;   /**< number of addresses */
} svr_group_t;

/** List head of server group. */
typedef LIST_HEAD(svr_group_head_s, svr_group_s) svr_group_head_t;

/** Blob key of configuration blob. */
typedef struct config_blob_key_s {
    char     key_name[MAX_NAME_LEN];  /**< key name */
    uint8_t  key_tag;                 /**< key tag */
    uint8_t  key_plugin_id;           /**< plugin ID */
} config_blob_key_t;

/** Blob item of server group. */
typedef struct blob_svr_group_s {
    char         group_name[MAX_NAME_LEN];  /**< server group name */
    uint16_t     group_addr_count;     /**< number of address in the group */
    in_addr_t    group_addr[0];        /**< address of server group */
} blob_svr_group_t;

/** Blob item of server group set. */
typedef struct blob_svr_group_set_s {
    uint16_t         gs_cksum;       /**< checksum of server group blob */
    uint16_t         gs_size;        /**< blob size */
    uint16_t         gs_count;       /**< number of server groups */
    blob_svr_group_t gs_group[0];    /**< pointer to server group */
} blob_svr_group_set_t;

/** Blob item of rule term. */
typedef struct blob_term_s {
    char         term_name[MAX_NAME_LEN];  /**< term name */
    uint8_t      term_match_id;            /**< match ID */
    uint16_t     term_match_port;          /**< match port */
    in_addr_t    term_match_addr;          /**< match address */
    uint8_t      term_act_id;              /**< action ID */
    in_addr_t    term_act_addr;            /**< action address */
    char         term_act_group_name[MAX_NAME_LEN];
                                           /**< action server group name */
} blob_term_t;

/** Blob item of rule. */
typedef struct blob_rule_s {
    char         rule_name[MAX_NAME_LEN];  /**< rule name */
    uint16_t     rule_term_count;          /**< number of terms */
    blob_term_t  rule_term[0];             /**< pointer to terms */
} blob_rule_t;

/** Blob item of service-set. */
typedef struct blob_svc_set_s {
    uint16_t     ss_cksum;              /**< service-set blob checksum */
    uint16_t     ss_id;                 /**< service set id */
    uint32_t     ss_svc_id;             /**< service-set service ID */
    uint32_t     ss_gen_num;            /**< service-set generation number */
    uint16_t     ss_size;                   /**< size of blob */
    char         ss_name[MAX_NAME_LEN];     /**< service set name */
    char         ss_if_name[MAX_NAME_LEN];  /**< service interface name */
    uint8_t      ss_eq2_svc_id;             /**< Equilibrium II service ID */
    uint16_t     ss_rule_count;             /**< number of rules */
    blob_rule_t  ss_rule[0];                /**< pointer to rules */
} blob_svc_set_t;

/** List item of service-set blob. */
typedef struct blob_svc_set_node_s {
    LIST_ENTRY(blob_svc_set_node_s) entry;         /**< entry node */
    blob_svc_set_t                  *ss;           /**< service set */
} blob_svc_set_node_t;

/** List head of service-set blob. */
typedef LIST_HEAD(blob_ss_head_s, blob_svc_set_node_s) blob_ss_head_t;

/** Message item of server address. */
typedef struct msg_svr_addr_s {
    in_addr_t    addr;            /**< IPv4 address */
    uint16_t     addr_ssn_count;  /**< number of sessions */
} msg_svr_addr_t;

/** Message item of server group. */
typedef struct msg_svr_group_s {
    char         group_name[MAX_NAME_LEN];  /**< group name */
    uint16_t     group_addr_count;          /**< number of addresses */
    uint16_t     pad;                       /**< pad struct size to 68 to make
                                                 following msg_svr_addr_t 
                                                 4-byte alignment */
    char         group_addr[0];             /**< pointer to addresses */
} msg_svr_group_t;

#endif /* __EQUILIBRIUM2_H__ */


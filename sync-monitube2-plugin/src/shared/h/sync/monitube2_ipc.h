/*
 * $Id: monitube2_ipc.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2009, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file monitube2_ipc.h
 *
 * @brief Constants and types used for SYNC Monitube2 IPC
 *
 * Constants for understanding and building request and reply
 * IPC messages between the mgmt & data components.
 */

#ifndef __MONITUBE2_IPC_H__
#define __MONITUBE2_IPC_H__


/*** Constants ***/


/**
 * Port that the monitube2-mgmt listens on:
 * (Same as MoniTube 1 b/c they won't be installed at the same time)
 */
#define MONITUBE_PORT_NUM 7081

#define MONITUBE2_MGMT_STRLEN 256  ///< Application name length

/*** Data Structures ***/


/**
 * Applicable IPC Message subtypes for messages FROM the mgmt component
 * TO the data component:
 */
typedef enum {
    MSG_DELETE_ALL = 1,       ///< delete all configured policies, no msg data
    MSG_DELETE_SS,            ///< delete policies for a service set
    MSG_APPLY_RULE,           ///< apply rule to a service set
    MSG_REMOVE_RULE,          ///< remove rule from service set
    MSG_CONF_RULE_ACTION,     ///< create/update rule's action,
    MSG_DELETE_RULE,          ///< delete rule and remove from any ss
    MSG_CONF_RULE_MATCH_ADDR, ///< create/update rule's match address
    MSG_DELETE_RULE_MATCH_ADDR ///< delete rule's match address
} msg_type_e;


/**
 * Message containing info about the service set to delete (MSG_DELETE_SS)
 */
typedef struct del_ss_s {
    uint16_t   ss_id;      ///< Service set id
    uint32_t   gen_num;    ///< generation number
    uint32_t   svc_id;     ///< service id
} del_ss_t;

/**
 * Message containing info about the rule to apply/remove (MSG_APPLY_RULE, MSG_REMOVE_RULE)
 */
typedef struct apply_rule_s {
    uint16_t   ss_id;         ///< Service set id
    uint32_t   gen_num;       ///< generation number
    uint32_t   svc_id;        ///< service id
    uint16_t   rule_name_len; ///< Rule name length
    char       rule_name[0];  ///< Rule name
} apply_rule_t;


/**
 * Message containing info about a rule to update (MSG_CONF_RULE_ACTION)
 */
typedef struct update_rule_action_s {
    uint32_t   rate;          ///< monitoring rate
    in_addr_t  redirect;      ///< mirror to address
    uint16_t   rule_name_len; ///< Rule name length
    char       rule_name[0];  ///< Rule name
} update_rule_action_t;


/**
 * Message containing info rule to delete (MSG_DELETE_RULE)
 */
typedef struct delete_rule_s {
    uint16_t    rule_name_len; ///< Rule name length
    char        rule_name[0];  ///< Rule name
} delete_rule_t;


/**
 * Message containing info about a rule address (MSG_CONF_RULE_MATCH_ADDR, MSG_DELETE_RULE_MATCH_ADDR)
 */
typedef struct rule_addr_s {
    in_addr_t   addr;          ///< address
    in_addr_t   mask;          ///< mask
    uint16_t    rule_name_len; ///< Rule name length
    char        rule_name[0];  ///< Rule name
} rule_addr_t;


/**
 * Applicable IPC Message subtypes for messages TO the mgmt component
 * FROM the data component:
 */
typedef enum {
    MSG_FLOW_STAT_UPDATE = 1         ///< update of flow MDI statistics
} update_type_e;


/**
 * Message containing info about the server status for an application
 */
typedef struct flow_stat_s {
    in_addr_t   flow_addr;    ///< Flow address
    uint32_t    pad;          ///< improve mem alignment, get = sizeof on re/pic
    uint64_t    mdi_df;       ///< MDI Delay factor (really a double)
    uint32_t    mdi_mlr;      ///< MDI Media loss rate
    uint16_t    flow_port;    ///< Flow port number
    uint16_t    ss_id;        ///< Service set id
} flow_stat_t;

#endif

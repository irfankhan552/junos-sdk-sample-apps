/*
 * $Id: dpm_ipc.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm_ipc.h
 * 
 * @brief Constants and types used for SYNC DPM IPC
 * 
 * Constants for understanding and building request and reply 
 * IPC messages between the mgmt & ctrl components.
 */

#ifndef __DPM_IPC_H__
#define __DPM_IPC_H__


/*** Constants ***/

#define MAX_POL_NAME 63     ///< max policer name length
#define MAX_INT_NAME 255    ///< max interface/pattern name length
#define MAX_SUB_NAME 255    ///< max subscriber/pw name length
#define MAX_CLASS_NAME 255  ///< max class name length

#define DPM_PORT_NUM 7082   ///< Port that the dpm-mgmt listens on


/*** Data Structures ***/


/**
 * Applicable IPC Message subtypes for messages FROM the mgmt component
 * TO the ctrl component:
 */
typedef enum {
    MSG_CONF_RESET = 1,   ///< delete all configuration and set filter mode
    MSG_CONF_COMP,        ///< all configuration data has been sent (no data)
    MSG_POLICER,          ///< configure a policer
    MSG_INTERFACE,        ///< configure a default interface policy
    MSG_SUBSCRIBER        ///< configure a subscriber policy
} conf_msg_type_e;


/**
 * Message containing info about the reset
 */
typedef struct reset_info_s {
    uint8_t    use_classic_filters;  ///< Use classic filters
} reset_info_t;


/**
 * A policer's configuration from the 'if-exceeding' section
 * 
 * @see policer_info_t
 * 
 * @note Because this is used in XLR and contained in another struct we will 
 *       pad to have 8-byte alignment
 */
typedef struct pol_conf_s {
    uint32_t pad1;                      ///< pads to align to 8-byte boundary
    uint16_t pad2;                      ///< unused
    uint8_t  pad3;                      ///< unused

    uint8_t  bw_in_percent;             ///< is bw_u union in percent
    union {
        uint64_t bandwidth_limit;       ///< bandwidth limit in bps
        uint32_t bandwidth_percent;     ///< bandwidth limit in %
    } bw_u;                             ///< bandwidth limit union
    uint64_t burst_size_limit;          ///< burst size limit
} pol_conf_t;


/**
 * A policer's action from the 'then' section
 * 
 * @see policer_info_t
 * 
 * @note Because this is used in XLR and contained in another struct we will 
 *       pad to have 8-byte alignment
 */
typedef struct pol_action_s {
    uint8_t  discard;             ///< discard all packets
    uint8_t  loss_priority;       ///< specify a loss priority
    uint8_t  forwarding_class;    ///< specify a forwarding class
    uint8_t  pad1;                ///< unused
    uint32_t pad2;                ///< pad to align to 8-byte boundary
} pol_action_t;


/**
 * Message containing info about the policer to configure
 */
typedef struct policer_info_s {
    char           name[MAX_POL_NAME + 1];  ///< policer name
    pol_conf_t     if_exceeding;            ///< if-exceeding conf
    pol_action_t   action;                  ///< action conf
} policer_info_t;


/**
 * Message containing info about the default interface policy to configure
 */
typedef struct int_info_s {
    char         name[MAX_INT_NAME + 1];       ///< interface name pattern
    uint32_t     subunit;                      ///< subunit
    uint32_t     index;                        ///< index
    char         input_pol[MAX_POL_NAME + 1];  ///< input policer name
    char         output_pol[MAX_POL_NAME + 1]; ///< output policer name
} int_info_t;

/**
 * Message containing info about the subscriber policy to configure
 */
typedef struct sub_info_s {
    char   name[MAX_SUB_NAME + 1];        ///< Subscriber name
    char   password[MAX_SUB_NAME + 1];    ///< Subscriber password
    char   class[MAX_CLASS_NAME + 1];     ///< Subscriber's class name
    char   policer[MAX_POL_NAME + 1];     ///< Class's policer name
} sub_info_t;


/**
 * Applicable IPC Message subtypes for (subscriber_status_t) messages 
 * TO the mgmt component FROM the ctrl component:
 */
typedef enum {
    MSG_SUBSCRIBER_LOGIN = 1,       ///< update of a subscriber login
    MSG_SUBSCRIBER_LOGOUT,          ///< update of a subscriber logout
} status_msg_type_e;


/**
 * Message containing info about the subscriber's status
 */
typedef struct subscriber_status_s {
    char   name[MAX_SUB_NAME + 1];    ///< Subscriber name
    char   class[MAX_CLASS_NAME + 1]; ///< Subscriber's class name
} subscriber_status_t;

#endif

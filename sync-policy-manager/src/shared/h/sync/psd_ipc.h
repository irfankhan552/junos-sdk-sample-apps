/*
 * $Id: psd_ipc.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file psd_ipc.h
 * 
 * @brief Constants and types used for SYNC IPC.
 * 
 * Constants for understanding and building request and reply 
 * IPC messages to/from psd. Also this includes connection information
 * like the address to use to connect.
 */

#ifndef SYNC_IPC_H_
#define SYNC_IPC_H_

/*** Constants ***/

/*
 * We reserve IPC types values from 0 to 1023 for IPC messages interpreted
 * by the kernel and pfeman. 1024 will be kept reserved for the maximum value
 * of IPC_MSG_TYPE_MAX.
 *
 * The IPC types used outside the kernel PIC to PIC, PIC to APP, APP to APP
 * will use numbers starting from 1025 to 60000. Types from 60001 to 65535
 * are reserved for third party applications.
 */
#define IPC_MSG_TYPE_PSD 60001   ///< IPC message type to use with psd

/*
 * Port that the PSD listens on:
 */
#define PSD_PORT_NUM 7077   ///< port psd is listening on

/*
 * IP Address for the clients to connect to:
 * 
 * Must be defined.
 * As seen in inet0 (default routing instance/table):
 * this should be the address of the RE 
 */
#define PSD_CONNECT_ADDRESS "127.0.0.1" ///< IP used to connect to the psd

/*
 * Constants:
 */
#define MAX_IF_NAME_LEN     64   ///< Max interface name length
#define MAX_POLICY_NAME_LEN 64   ///< Max policy name length
#define MAX_FILTER_NAME_LEN 64   ///< Max filter name length
#define MAX_COND_NAME_LEN   64   ///< Max condition name length


/*** Data Structures ***/


/**
 * Structure of policy request message.
 * The unit number is included in ifname, like "fd-1/0/0.0".
 */
typedef struct policy_req_msg_s {
    char        ifname[MAX_IF_NAME_LEN + 1];     ///< Interface name
    uint8_t     af;                              ///< Adress family
} policy_req_msg_t;


/**
 * Structure of policy filter message.
 * Since the reply message is asynchronous to request,
 * ifname and af are included in the message to tell
 * client where to apply the policy.
 */
typedef struct policy_filter_msg_s {
    char        ifname[MAX_IF_NAME_LEN + 1];     ///< Interface name
    uint8_t     af;                              ///< Adress family
    char        input_filter[MAX_FILTER_NAME_LEN + 1];   ///< input filter to apply on interface
    char        output_filter[MAX_FILTER_NAME_LEN + 1];  ///< output filter to apply on interface
} policy_filter_msg_t;


/**
 * Structure of policy route message.
 * Since the reply message is asynchronous to request,
 * ifname and af are included in the message to tell
 * client where to apply the policy.
 */
typedef struct policy_route_msg_s {
    char        ifname[MAX_IF_NAME_LEN + 1]; ///< Interfame name
    uint8_t     af;                          ///< Address family
    in_addr_t   route_addr;                  ///< Route address
    size_t      route_addr_prefix;           ///< Route address prefix
    in_addr_t   nh_addr;                     ///< Next-hop address
    uint8_t     nh_type;                     ///< Next-hop type
    uint32_t    metrics;                     ///< Route metrics
    uint32_t    preferences;                 ///< Route Preference
} policy_route_msg_t;


/**
 * Applicable IPC Message subtypes:
 */
typedef enum {
    MSG_POLICY_REQ = 1, ///< Request a policy
    MSG_FILTER,         ///< Filter data message
    MSG_ROUTE,          ///< Route data message
    MSG_POLICY_NA,      ///< Policy is not available
    MSG_POLICY_UPDATE,  ///< Policy need to be updated
    MSG_UPDATE_DONE,    ///< Policy update is done
    MSG_HB              ///< Heartbeat message
} msg_type_e;

#endif

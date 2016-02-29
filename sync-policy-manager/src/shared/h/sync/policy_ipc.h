/*
 * $Id: policy_ipc.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file policy_ipc.h
 * 
 * @brief Constants and types used for SYNC Policy app IPC.
 * 
 * Constants for understanding and building request and reply 
 * IPC messages between the mgmt (ped), ctrl (cpd), and data (pfd) components
 */

#ifndef _POLICY_IPC_H_
#define _POLICY_IPC_H_

/*** Constants ***/

/**
 * Port that the PED listens on for connection from ctrl/data components
 */
#define PED_PORT_NUM 7079

/**
 * Port that the CPD listens on for connection from the data component
 */
#define CPD_PORT_NUM 7079


/*** Data Structures ***/

/**
 * Applicable IPC Message subtypes when talking to the PED
 */
typedef enum {
    MSG_ID = 1,   ///< Message contains the component ID (sent to PED by CPD & PFD) 
    MSG_PEER,     ///< Message contains the other PIC's peer info (sent by PED to PFD) 
    MSG_ADDRESSES ///< Message contains the PFD and CPD addresses (sent by PED to CPD & PFD)
} ped_msg_type_e;

/**
 * Applicable IPC Message subtypes when talking to the CPD
 */
typedef enum {
    MSG_GET_AUTH_LIST,  ///< Message requests the authorized users and contains no data (sent by PFD to CPD) 
    MSG_AUTH_ENTRY_ADD, ///< Message contains an authorized user (sent by CPD to PFD)
    MSG_AUTH_ENTRY_DEL  ///< Message contains an unauthorized user (previously authorized) (sent by CPD to PFD)    
} cpd_msg_type_e;

/*
 * Data types:
 */

/**
 * The applicable component IDs
 */
typedef enum {
    ID_PED = 1, ///< The ID value for the management component (PED)
    ID_CPD,     ///< The ID value for the control component (CPD)
    ID_PFD      ///< The ID value for the data component (PFD)
} component_id_e;
// note: enums should compile to an int so we should use htonl/ntohl

/*
 * pconn_peer_info_t is the data value for the MSG_PEER message
 */

/*
 * The address data value type for CPD/PFD addresses is in_addr_t (4-bytes)
 * The order of the data is CPD address first, then PFD address.
 */

/**
 * The user address data value type for auth/unauth'd users
 * exchanged between the CPD and PFD is also in_addr_t
 */

#endif

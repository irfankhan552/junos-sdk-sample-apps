/*
 * $Id: hellopics_ipc.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file hellopics_ipc.h
 * 
 * @brief Constants and types used for SYNC Hellopics IPC
 * 
 * Constants for understanding and building request and reply 
 * IPC messages between the mgmt/ctrl/data components.
 */

#ifndef _HELLOPICS_IPC_H_
#define _HELLOPICS_IPC_H_

/*** Constants ***/

/**
 * Port that the hellopics-mgmt and hellopics-ctrl listen on:
 */
#define HELLOPICS_PORT_NUM 7078

/*** Data Structures ***/

/**
 * Applicable IPC Message subtypes:
 */
typedef enum {
    MSG_ID = 1,    ///< Message contains the component ID 
    MSG_READY,     ///< Message idicates that the sending component is ready
    MSG_GET_PEER,  ///< Message idicates a request for the other PIC's peer info
    MSG_PEER,      ///< Message contains the other PIC's peer info
    MSG_HELLO    ///< Message idicates a HELLO message cycled between components
} msg_type_e;

/**
 * The applicable component IDs
 */
typedef enum {
    HELLOPICS_ID_MGMT = 1, ///< The ID value for the management component
    HELLOPICS_ID_CTRL,     ///< The ID value for the control component
    HELLOPICS_ID_DATA      ///< The ID value for the data component
} component_id_e;

#endif

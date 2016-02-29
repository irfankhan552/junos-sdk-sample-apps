/*
 * $Id: monitube_ipc.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube_ipc.h
 *
 * @brief Constants and types used for SYNC Monitube IPC
 *
 * Constants for understanding and building request and reply
 * IPC messages between the mgmt & data components.
 */

#ifndef __MONITUBE_IPC_H__
#define __MONITUBE_IPC_H__


/*** Constants ***/


/**
 * Port that the monitube-mgmt listens on:
 */
#define MONITUBE_PORT_NUM 7081


/*** Data Structures ***/


/**
 * Applicable IPC Message subtypes for messages FROM the mgmt component
 * TO the data component:
 */
typedef enum {
    MSG_DELETE_ALL_MON = 1,  ///< delete all monitors, no message data sent
    MSG_DELETE_ALL_MIR,      ///< delete all mirrors, no message data sent
    MSG_DELETE_MON,          ///< delete monitor, del_mon_info_t sent
    MSG_DELETE_MIR,          ///< delete mirror, del_mir_info_t sent
    MSG_DELETE_MON_ADDR,     ///< delete monitor address, maddr_info_t sent
    MSG_CONF_MON,            ///< update monitor, update_mon_info_t sent
    MSG_CONF_MIR,            ///< update mirror, update_mir_info_t sent
    MSG_CONF_MON_ADDR,       ///< update monitor address, maddr_info_t sent
    MSG_CONF_MASTER,         ///< set as master, no message data sent
    MSG_CONF_SLAVE,          ///< set as slave, slave_info_t is sent
    MSG_REP_INFO             ///< set replication interval, replication_info_t is sent
} msg_type_e;


/**
 * Message containing info about the monitor to delete
 */
typedef struct del_mon_info_s {
    uint16_t    mon_name_len;  ///< Monitor name length
    char        mon_name[0];   ///< monitor name
} del_mon_info_t;

/**
 * Message containing info about the monitor to update
 */
typedef struct update_mon_info_s {
    uint32_t    rate;          ///< monitoring group media bps rate
    uint16_t    mon_name_len;  ///< Monitor name length
    char        mon_name[0];   ///< monitor name
} update_mon_info_t;


/**
 * Message containing info about the mirror to delete
 */
typedef struct del_mir_info_s {
    in_addr_t    mirror_from;   ///< mirror from address
} del_mir_info_t;

/**
 * Message containing info about the mirror to update
 */
typedef struct update_mir_info_s {
    in_addr_t    mirror_from;   ///< mirror from address
    in_addr_t    mirror_to;     ///< mirror to address
} update_mir_info_t;


/**
 * Message containing info about the monitor address to add/delete
 */
typedef struct maddr_info_s {
    in_addr_t   addr;          ///< address
    in_addr_t   mask;          ///< mask
    uint16_t    mon_name_len;  ///< Monitor name length
    char        mon_name[0];   ///< monitor name
} maddr_info_t;


/**
 * Message containing info about the monitor to delete
 */
typedef struct slave_info_s {
    in_addr_t    master_address;   ///< address of master data component
} slave_info_t;


/**
 * Message containing info about the monitor to delete
 */
typedef struct replication_info_s {
    uint8_t    interval;   ///< address of master data component
} replication_info_t;


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
    uint16_t    mon_name_len; ///< Monitor name length
    char        mon_name[0];  ///< Applicable monitor name
} flow_stat_t;

#endif

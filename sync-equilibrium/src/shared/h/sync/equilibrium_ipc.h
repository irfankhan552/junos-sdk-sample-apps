/*
 * $Id: equilibrium_ipc.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium_ipc.h
 * 
 * @brief Constants and types used for SYNC Equilibrium IPC
 * 
 * Constants for understanding and building request and reply 
 * IPC messages between the mgmt & data components.
 */

#ifndef __EQUILIBRIUM_IPC_H__
#define __EQUILIBRIUM_IPC_H__


/*** Constants ***/


/**
 * Port that the equilibrium-mgmt listens on:
 */
#define EQUILIBRIUM_PORT_NUM 7080


/*** Data Structures ***/


/**
 * Applicable IPC Message subtypes for messages FROM the mgmt component
 * TO the data component:
 */
typedef enum {
    MSG_DELETE_ALL = 1,      ///< delete everything, no message data sent
    MSG_DELETE_SS,           ///< delete service set, del_svcset_info_t sent
    MSG_DELETE_APP,          ///< delete application, del_app_info_t sent
    MSG_DELETE_SERVER,       ///< delete server, server_info_t sent
    MSG_DELETE_ALL_SERVERS,  ///< delete all servers, del_app_info_t sent
    MSG_CONF_APPLICATION,    ///< update application, update_app_info_t sent
    MSG_CONF_SERVER          ///< update server, server_info_t sent
} msg_type_e;


/**
 * Message containing info about the service set to delete 
 */
typedef struct del_svcset_info_s {
    uint16_t    svc_set_id;    ///< service-set id
} del_svcset_info_t;


/**
 * Message containing info about the application to delete
 */
typedef struct del_app_info_s {
    uint16_t    svc_set_id;    ///< service-set id
    uint16_t    app_name_len;  ///< application name's length
    char        app_name[0];   ///< application name
} del_app_info_t;


/**
 * Message containing info about the server (to add or delete)
 * for an application
 */
typedef struct server_info_s {
    in_addr_t   server_addr;   ///< server IP address
    uint16_t    svc_set_id;    ///< service-set id
    uint16_t    app_name_len;  ///< application name's length
    char        app_name[0];   ///< application name
} server_info_t;


/**
 * Message containing info about the application
 * Note, when connection_interval = 0, it indicates no monitor should be used
 */
typedef struct update_app_info_s {
    in_addr_t   app_addr;            ///< application's facade IP address
    uint16_t    app_port;            ///< application's port
    uint16_t    session_timeout;     ///< application's session timeout
    uint16_t    connection_interval; ///< server-connection interval (sec)
    uint16_t    connection_timeout;  ///< server-connection timeout (sec)
    uint8_t     timeouts_allowed;    ///< server timeouts allowed #
    uint8_t     pad;                 ///< ignored
    uint16_t    down_retry_interval; ///< down server connection interval (sec)
    uint16_t    svc_set_id;          ///< service-set id
    uint16_t    app_name_len;        ///< application name's length
    char        app_name[0];         ///< application name
} update_app_info_t;


/**
 * Applicable IPC Message subtypes for messages TO the mgmt component
 * FROM the data component:
 */
typedef enum {
    MSG_SERVER_UPDATE = 1,    ///< update server status, server_status_t
    MSG_STATUS_UPDATE         ///< update active # of sessions
} update_type_e;


#define SERVER_STATUS_DOWN   0    ///< server status is down
#define SERVER_STATUS_UP     1    ///< server status is up

/**
 * Message containing info about the server status for an application
 */
typedef struct server_status_s {
    uint8_t     server_status;  ///< server status (uses the defines above)
    uint8_t     pad;            ///< ignored
    uint16_t    pad2;           ///< ignored
    in_addr_t   server_addr;    ///< server IP address
    uint16_t    svc_set_id;     ///< service-set id
    uint16_t    app_name_len;   ///< application name's length
    char        app_name[0];    ///< application name
} server_status_t;


/**
 * Message containing info about the server status for an application
 */
typedef struct sessions_status_s {
    uint32_t    active_sessions;  ///< number of active sessions for this app/ss
    uint16_t    svc_set_id;       ///< service-set id
    uint16_t    app_name_len;     ///< application name's length
    char        app_name[0];      ///< application name
} sessions_status_t;

#endif

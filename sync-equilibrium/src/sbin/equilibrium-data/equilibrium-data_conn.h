/*
 * $Id: equilibrium-data_conn.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-data_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __EQUILIBRIUM_DATA_CONN_H__
#define __EQUILIBRIUM_DATA_CONN_H__

#include <sync/equilibrium_ipc.h>

/*** Constants ***/


/*** Data structures ***/



/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the connection to the mgmt component
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_connections(evContext ctx);


/**
 * Terminate connection to the mgmt component
 */
void
close_connections(void);


/**
 * Notify the mgmt component about a server changing status
 * 
 * @param[in] svc_set_id
 *      service set id
 * 
 * @param[in] app_addr
 *      the application address
 * 
 * @param[in] app_port
 *      the application port
 * 
 * @param[in] server_addr
 *      the server address
 * 
 * @param[in] status
 *      the new status
 */
void
notify_server_status(uint16_t svc_set_id,
                     in_addr_t app_addr,
                     uint16_t app_port,
                     in_addr_t server_addr,
                     uint8_t status);


/**
 * Notify the mgmt component about the number of sessions for an application
 * 
 * @param[in] svc_set_id
 *      service set id
 * 
 * @param[in] app_addr
 *      the application address
 * 
 * @param[in] app_port
 *      the application port
 * 
 * @param[in] session_count
 *      the number of sessions for this application
 */
void
notify_application_sessions(uint16_t svc_set_id,
                            in_addr_t app_addr,
                            uint16_t app_port,
                            uint32_t session_count);

#endif


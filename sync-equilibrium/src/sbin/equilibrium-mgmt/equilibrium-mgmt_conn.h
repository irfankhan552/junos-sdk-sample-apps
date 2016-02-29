/*
 * $Id: equilibrium-mgmt_conn.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-mgmt_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __EQUILIBRIUM_MGMT_CONN_H__
#define __EQUILIBRIUM_MGMT_CONN_H__

/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the server socket connection
 * 
 * @param[in] ctx
 *     Newly created event context 
 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_server(evContext ctx);


/**
 * Close existing connections and shutdown server
 */
void
close_connections(void);


/**
 * Notification about an MS-PIC interface going down
 * 
 * @param[in] name
 *      name of interface that has gone down
 */
void
mspic_offline(const char * name);


/**
 * Try to process all notification requests that have been buffered.
 * This should be called when a configuration load is complete. 
 */
void
process_notifications(void);


/**
 * Enqueue a message to go to the data component about an application update
 * 
 * @param[in] svc_set_name
 *      Service set name
 * 
 * @param[in] app_name
 *      Application name
 * 
 * @param[in] address
 *      Application address
 * 
 * @param[in] port
 *      Application port
 * 
 * @param[in] session_timeout
 *      Application session timeout
 * 
 * @param[in] connection_interval
 *      Application server monitoring connection interval
 * 
 * @param[in] connection_timeout
 *      Application server monitoring connection timeout
 * 
 * @param[in] timeouts_allowed
 *      Application server monitoring connection timeouts allowed
 * 
 * @param[in] down_retry_interval
 *      Application server monitoring, down servers' retry connection interval
 */
void
notify_application_update(const char * svc_set_name,
                          const char * app_name,
                          in_addr_t address,
                          uint16_t port,
                          uint16_t session_timeout,
                          uint16_t connection_interval,
                          uint16_t connection_timeout,
                          uint8_t timeouts_allowed,
                          uint16_t down_retry_interval);


/**
 * Enqueue a message to go to the data component about a server update
 * 
 * @param[in] svc_set_name
 *      Service set name
 * 
 * @param[in] app_name
 *      Application name
 * 
 * @param[in] address
 *      Application server address
 */
void
notify_server_update(const char * svc_set_name,
                     const char * app_name,
                     in_addr_t address);


/**
 * Enqueue a message to go to the data component about a server deletion
 * 
 * @param[in] svc_set_name
 *      Service set name
 * 
 * @param[in] app_name
 *      Application name
 * 
 * @param[in] address
 *      Application server address
 */
void
notify_server_delete(const char * svc_set_name,
                     const char * app_name,
                     in_addr_t address);


/**
 * Enqueue a message to go to the data component about an application deletion
 * 
 * @param[in] svc_set_name
 *      Service set name
 * 
 * @param[in] app_name
 *      Application name
 */
void
notify_application_delete(const char * svc_set_name,
                          const char * app_name);


/**
 * Enqueue a message to go to the data component about a service set deletion
 * 
 * @param[in] svc_set_name
 *      Service set name
 * 
 * @param[in] svc_set_id
 *      Service set ID
 *      (We need it now for deletions since it won't be available later)
 */
void
notify_serviceset_delete(const char * svc_set_name, uint16_t svc_set_id);


/**
 * Enqueue a message to go to the data component about a deletion of all servers
 * 
 * @param[in] svc_set_name
 *      Service set name
 * 
 * @param[in] app_name
 *      Application name
 */
void
notify_delete_all_servers(const char * svc_set_name,
                          const char * app_name);


/**
 * Enqueue a message to go to the data component about a deletion of all apps
 * 
 * @param[in] svc_set_name
 *      Service set name
 * 
 * @param[in] svc_set_id
 *      Service set ID
 *      (We need it now for deletions since it won't be available later)
 */
void
notify_delete_all_applications(const char * svc_set_name, uint16_t svc_set_id);


/**
 * Enqueue a message to go to the data component about a deletion of all config
 */
void
notify_delete_all(void);


#endif

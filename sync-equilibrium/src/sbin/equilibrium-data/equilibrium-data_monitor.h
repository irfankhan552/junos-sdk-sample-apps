/*
 * $Id: equilibrium-data_monitor.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-data_monitor.h
 * @brief Relating to server monitoring
 * 
 * Relating to server monitoring / probing
 */
 
#ifndef __EQUILIBRIUM_DATA_MONITOR_H__
#define __EQUILIBRIUM_DATA_MONITOR_H__


/*** Constants ***/


/*** Data Structures ***/


/*** GLOBAL/EXTERNAL Functions ***/



/**
 * Init the data structures that will store configuration info
 * 
 * @param[in] ctx
 *      event context
 * 
 * @param[in] cpu
 *      cpu to use or MSP_NEXT_END run on current event context and cpu
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_monitor(evContext ctx, int cpu);


/**
 * Shutdown the monitor
 */
void
shutdown_monitor(void);


/**
 * Add a server to monitor.
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 * 
 * @param[in] server_addr
 *      The server address, of the server within that application
 *
 * @param[in] monitor
 *      The server monitoring parameters (all servers in the same 
 *      application must have the same monitor). If NULL, then server 
 *      is not monitored and assumed UP.
 */
void
monitor_add_server(uint16_t ss_id,
                   in_addr_t app_addr,
                   uint16_t app_port,
                   in_addr_t server_addr,
                   eq_smon_t * monitor);


/**
 * Remove the server from the monitor's configuration.
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 * 
 * @param[in] server_addr
 *      The server address, of the server within that application
 */
void
monitor_remove_server(uint16_t ss_id,
                      in_addr_t app_addr,
                      uint16_t app_port,
                      in_addr_t server_addr);


/**
 * Remove all servers from the monitor's configuration matching this application
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 */
void
monitor_remove_all_servers_in_app(uint16_t ss_id,
                                  in_addr_t app_addr,
                                  uint16_t app_port);


/**
 * Remove all servers from the monitor's configuration matching this service set
 * 
 * @param[in] ss_id
 *      service-set id
 */
void
monitor_remove_all_servers_in_service_set(uint16_t ss_id);


/**
 * Change the monitoring parameters for all servers.
 * Does not free the pointer to the old monitor.
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 * 
 * @param[in] monitor
 *      The server monitoring parameters (all servers in the same 
 *      application use the same monitor). If NULL, then server 
 *      is not monitored and assumed UP.
 */
void
change_monitoring_config(uint16_t ss_id,
                         in_addr_t app_addr,
                         uint16_t app_port,
                         eq_smon_t * monitor);


/**
 * Get the server with the least load for this application
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 * 
 * @return the server address if the application exists and has any up servers.
 *    returns 0 if the application does not exist and,
 *            (in_addr_t)-1 if there are no "up" servers but the app exists
 */
in_addr_t
monitor_get_server_for(uint16_t ss_id,
                       in_addr_t app_addr,
                       uint16_t app_port);


/**
 * Find the server in the app, and reduce the load by one
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 * 
 * @param[in] server_addr
 *      Server address
 */
void
monitor_remove_session_for_server(uint16_t ss_id,
                                  in_addr_t app_addr,
                                  uint16_t app_port,
                                  in_addr_t server_addr);


/**
 * Send server/application load stats to the mgmt component 
 */
void
monitor_send_stats(void);

#endif

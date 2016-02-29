/*
 * $Id: equilibrium-data_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-data_config.h
 * @brief Relating to getting and setting the configuration data 
 *        
 * 
 * These functions will store and provide access to the 
 * configuration data which is essentailly coming from the mgmt component.
 */
 
#ifndef __EQUILIBRIUM_DATA_CONFIG_H__
#define __EQUILIBRIUM_DATA_CONFIG_H__


/*** Constants ***/

#define MAX_APP_NAME    256  ///< Application name length

/*** Data Structures ***/

/**
 * The structure we use to store our server monitor info
 */
typedef struct eq_smon_s {
    uint16_t    connection_interval;  ///< server connection interval (sec)
    uint16_t    connection_timeout;   ///< server connection timeout (sec)
    uint8_t     timeouts_allowed;     ///< server timeouts allowed #
    uint16_t    down_retry_interval;  ///< down server connection interval (sec)
} eq_smon_t;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_config(evContext ctx);

/**
 * Clear the configuration data
 */
void
clear_config(void);


/**
 * Get the currently cached time
 * 
 * @return
 *      Current time
 */
time_t
get_current_time(void);


/**
 * Reset all of the configuration
 */
void
reset_configuration(void);


/**
 * Delete the configuration for a service set
 * 
 * @param[in] svc_set_id
 *      The service-set id of the service set to delete
 */
void
delete_service_set(uint16_t svc_set_id);


/**
 * Delete an application from the configuration
 * 
 * @param[in] svc_set_id
 *      The service-set id of the service set associated with the application
 * 
 * @param[in] app_name
 *      The application name
 */
void
delete_application(uint16_t svc_set_id,
                   char * app_name);


/**
 * Delete all server associated with an application from the configuration
 * 
 * @param[in] svc_set_id
 *      The service-set id of the service set associated with the application
 * 
 * @param[in] app_name
 *      The application name
 */
void
delete_all_servers(uint16_t svc_set_id,
                   char * app_name);


/**
 * Delete a server associated with an application from the configuration
 * 
 * @param[in] svc_set_id
 *      The service-set id of the service set associated with the application
 * 
 * @param[in] app_name
 *      The application name of the application associated with the server
 * 
 * @param[in] server_addr
 *      The server address of the server to delete
 */
void
delete_server(uint16_t svc_set_id,
              char * app_name,
              in_addr_t server_addr);


/**
 * Update or add an application to the configuration
 * 
 * @param[in] svc_set_id
 *      The service-set id of the service set associated with the application
 * 
 * @param[in] app_name
 *      The application name
 * 
 * @param[in] app_addr
 *      The application facade's address (unique to the application)
 * 
 * @param[in] app_port
 *      The application port
 * 
 * @param[in] session_timeout
 *      The session timeout of sessions falling into this application
 * 
 * @param[in] connection_interval
 *      The connection interval of the server monitoring parameters,
 *      or zero for no monitoring
 * 
 * @param[in] connection_timeout
 *      The connection timeout afterwhich the connection attempt is 
 *      counted as failed if not already complete
 *
 * @param[in] timeouts_allowed
 *      The number of connection timeouts that we can observe before we mark a 
 *      server belonging to this application as down. (The number of retries)
 *
 * @param[in] down_retry_interval
 *      The time to wait before retrying a probe (connection attempt) once a 
 *      server is marked down.
 */
void
update_application(uint16_t svc_set_id,
                   char * app_name,
                   in_addr_t app_addr,
                   uint16_t app_port,
                   uint16_t session_timeout, 
                   uint16_t connection_interval,
                   uint16_t connection_timeout,
                   uint8_t timeouts_allowed,
                   uint16_t down_retry_interval);


/**
 * Add a server to an application in the configuration
 * 
 * @param[in] svc_set_id
 *      The service-set id of the service set associated with the application
 * 
 * @param[in] app_name
 *      The application name of the application associated with the server
 * 
 * @param[in] server_addr
 *      The server's address
 */
void
add_server(uint16_t svc_set_id,
           char * app_name,
           in_addr_t server_addr);


/**
 * Search for the application with the given parameters,
 * and if it exists return its name
 * 
 * @param[in] svc_set_id
 *      The service-set id of the service set associated with the application
 * 
 * @param[in] addr
 *      The application facade address
 * 
 * @param[in] port
 *      The application port
 * 
 * @return the session timeout or 0 if no application meeting 
 *      the parameters is found
 */
char *
get_app_name(uint16_t svc_set_id, in_addr_t addr, uint16_t port);


/**
 * Search for the application with the given parameters,
 * and if it exists return its session timeout
 * 
 * @param[in] svc_set_id
 *      The service-set id of the service set associated with the application
 * 
 * @param[in] addr
 *      The application facade address
 * 
 * @param[in] port
 *      The application port
 * 
 * @return the session timeout or 0 if no application meeting 
 *      the parameters is found
 */
uint16_t
get_app_session_timeout(uint16_t svc_set_id, in_addr_t addr, uint16_t port);

#endif

/*
 * $Id: equilibrium-mgmt_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-mgmt_config.h
 * @brief Relating to loading the configuration data
 * 
 * These functions will parse and load the configuration data.
 */
 
#ifndef __EQUILIBRIUM_MGMT_CONFIG_H__
#define __EQUILIBRIUM_MGMT_CONFIG_H__

#include <sys/queue.h>
#include <jnx/patricia.h>
#include <jnx/junos_kcom_pub_blob.h>

/*** Constants ***/

#define SERVER_DOWN 0 ///< server status value indicates DOWN
#define SERVER_UP   1 ///< server status value indicates UP

#define MAX_SVC_SET_NAME  JNX_EXT_SVC_SET_NAME_LEN  ///< Service set name length
#define MAX_SVC_NAME                           256  ///< Service name length
#define MAX_SVC_INT_NAME                        64  ///< Service interface name length
#define MAX_APP_NAME                           256  ///< Application name length

/*** Data structures ***/


/**
 * The structure we use to store our server monitor info
 */
typedef struct eq_smon_s {
    uint16_t    connection_interval;  ///< server connection interval (sec)
    uint16_t    connection_timeout;   ///< server connection timeout (sec)
    uint8_t     timeouts_allowed;     ///< server timeouts allowed #
    uint16_t    down_retry_interval;  ///< down server connection interval (sec)
} eq_smon_t;


/**
 * An item in a server set (tailq)
 */
typedef struct eq_server_s {
    in_addr_t                  server_addr; ///< server IP address
    uint8_t                    status;      ///< server status
    TAILQ_ENTRY(eq_server_s)   entries;     ///< list entries
} eq_server_t;


/**
 * A list/set of servers type as a tailq
 */
typedef TAILQ_HEAD(server_list_s, eq_server_s) server_list_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each application
 */
typedef struct eq_app_s {
    patnode     node;              ///< Tree node in applications (comes first)
    char        application_name[MAX_APP_NAME]; ///< application name
    in_addr_t   application_addr;  ///< application IP address
    uint16_t    application_port;  ///< application port
    uint16_t    session_timeout;   ///< session timeout
    eq_smon_t * server_mon_params; ///< server monitoring parameters
    server_list_t * servers;       ///< server list
    uint32_t    session_count;     ///< last reported number of active sessions
} eq_app_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each service set
 */
typedef struct eq_serviceset_s {
    patnode    node;              ///< Tree node in services_conf (comes first)
    char       svc_set_name[MAX_SVC_SET_NAME];  ///< service set name
    int        svc_set_id;                      ///< service set id (only available when SSRB info was deleted)
    char       svc_int_name[MAX_SVC_INT_NAME];  ///< service interface name
    char       ext_svc_name[MAX_SVC_NAME];      ///< extention service name
    patroot *  applications;                    ///< applications Pat. tree
} eq_serviceset_t;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structure that will store configuration info,
 * or in other words, the condition(s)
 */
void
init_config(void);


/**
 * Clear the (non-SSRB) configuration info
 */
void
clear_config(void);


/**
 * Clear the SSRB configuration info
 */
void
clear_ssrb_config(void);


/**
 * Add an SSRB to the configuration, may not exist prior
 * 
 * @param[in] ssrb
 *     The new SSRB data
 * 
 * @return
 *      SUCCESS when added, or EFAIL if it already exists, or upon add failure
 */
int
add_ssrb(junos_kcom_pub_ssrb_t * ssrb);


/**
 * Get an SSRB from the configuration by name
 * 
 * @param[in] name
 *     Service-set name
 * 
 * @return
 *      The ssrb data, or NULL if not found
 */
junos_kcom_pub_ssrb_t *
get_ssrb_by_name(char * name);


/**
 * Get an SSRB from the configuration by id
 * 
 * @param[in] svc_set_id
 *     Service-set id
 * 
 * @return
 *      The ssrb data, or NULL if not found
 */
junos_kcom_pub_ssrb_t *
get_ssrb_by_id(uint16_t svc_set_id);


/**
 * Remove an SSRB from the configuration by name
 * 
 * @param[in] name
 *     Service-set name
 */
void
delete_ssrb_by_name(char * name);


/**
 * Remove an SSRB from the configuration by id
 * 
 * @param[in] id
 *     Service-set id
 */
void
delete_ssrb_by_id(uint16_t svc_set_id);


/**
 * Set server status
 * 
 * @param[in] svc_set_id
 *     Service-set id
 * 
 * @param[in] app_name
 *     Application name
 * 
 * @param[in] server
 *     Server in the application
 * 
 * @param[in] status
 *     The updated status, according to the servers monitor  
 */
void
set_server_status(uint16_t svc_set_id,
                  char * app_name,
                  in_addr_t server,
                  uint8_t status);


/**
 * Set the number of active sessions for an application
 * 
 * @param[in] svc_set_id
 *     Service-set id
 * 
 * @param[in] app_name
 *     Application name
 * 
 * @param[in] session_count
 *     Number of active sessions
 */
void
set_app_sessions(uint16_t svc_set_id,
                 char * app_name,
                 uint32_t session_count);


/**
 * Get the next service set in configuration given the previously returned   
 * data (from next_service_set).
 * 
 * @param[in] data
 *      previously returned data, should be NULL first time 
 * 
 * @return pointer to the first service set if one exists, o/w NULL
 */
eq_serviceset_t *
next_service_set(eq_serviceset_t * data);


/**
 * Get the next application in configuration given the previously returned   
 * data (from next_application).
 *
 * @param[in] ss
 *      The service set of the application
 *
 * @param[in] data
 *      previously returned data, should be NULL first time 
 * 
 * @return pointer to the first service set if one exists, o/w NULL
 */
eq_app_t *
next_application(eq_serviceset_t * ss, eq_app_t * data);


/**
 * Get the service-set information by name
 * 
 * @param[in] name
 *      service-set name 
 * 
 * @return The service set if one exists with the matching name, o/w NULL
 */
eq_serviceset_t *
find_service_set(const char * name);


/**
 * Get the application information in a service-set by name
 * 
 * @param[in] ss
 *      The service set to search
 * 
 * @param[in] name
 *      Application name 
 * 
 * @return The application if one exists with the matching name, o/w NULL
 */
eq_app_t *
find_application(const eq_serviceset_t * ss, const char * name);


/**
 * Read daemon configuration from the database.
 * (nothing to do except traceoptions)
 * 
 * @param[in] check
 *     1 if this function being invoked because of a commit check
 * 
 * @return SUCCESS (0) successfully loaded, EFAIL if not
 * 
 * @note Do not use ERRMSG/LOG during config check normally.
 */
int
equilibrium_config_read(int check);

#endif


/*
 * $Id: equilibrium-data_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-data_config.c
 * @brief Relating to getting and setting the configuration data 
 *        
 * 
 * These functions will store and provide access to the 
 * configuration data which is essentailly coming from the mgmt component.
 */

#include "equilibrium-data_main.h"
#include "equilibrium-data_config.h"
#include "equilibrium-data_monitor.h"


/*** Constants ***/


/*** Data structures ***/

/**
 * An item in a server set (tailq)
 */
typedef struct eq_server_s {
    in_addr_t                  server_addr; ///< server IP address
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
    in_addr_t   application_addr;  ///< application IP address (network byte-order)
    uint16_t    application_port;  ///< application port (network byte-order)
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
    uint16_t   svc_set_id;        ///< service set id
    patroot *  applications;      ///< applications Pat. tree
} eq_serviceset_t;

static patroot services_conf; ///< patricia tree root

/**
 * The current cached time. Cache it to prevent many system time() calls by the
 * data threads.
 */
static atomic_uint_t current_time;


/*** STATIC/INTERNAL Functions ***/


/**
 * Generate the service_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(service_entry, eq_serviceset_t, node)


/**
 * Generate the app_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(app_entry, eq_app_t, node)


/**
 * Update cached time
 * 
 * @param[in] ctx
 *     The event context for this application
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] due
 *     The absolute time when the event is due (now)
 * 
 * @param[in] inter
 *     The period; when this will next be called 
 */
static void
update_time(evContext ctx __unused,
            void * uap  __unused,
            struct timespec due __unused,
            struct timespec inter __unused)
{
    atomic_add_uint(1, &current_time);
}


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
init_config(evContext ctx)
{
    patricia_root_init(&services_conf, FALSE, sizeof(uint16_t), 0);
                   // root, is key ptr, key size, key offset

    current_time = (atomic_uint_t)(time(NULL) - 1);
    
    // cached system time
    if(evSetTimer(ctx, update_time, NULL,
        evNowTime(), evConsTime(1, 0), NULL)) {

        LOG(LOG_EMERG, "%s: Failed to initialize an eventlib timer to generate "
            "the cached time", __func__);
        return EFAIL;
    }
    
    return SUCCESS;
}

/**
 * Clear the configuration data
 */
void
clear_config(void)
{
    reset_configuration();
}


/**
 * Get the currently cached time
 * 
 * @return
 *      Current time
 */
time_t
get_current_time(void)
{
    return (time_t)current_time;
}


/**
 * Reset all of the configuration
 */
void
reset_configuration(void)
{
    eq_serviceset_t * ss;    
   
    while((ss= service_entry(patricia_find_next(&services_conf, NULL)))!= NULL){
        delete_service_set(ss->svc_set_id);      
    }
}


/**
 * Delete the configuration for a service set
 * 
 * @param[in] svc_set_id
 *      The service-set id of the service set to delete
 */
void
delete_service_set(uint16_t svc_set_id)
{
    eq_app_t * app;
    eq_serviceset_t * ss;
    eq_server_t * server;
    
    // get this service-set
    ss = service_entry(
            patricia_get(&services_conf, sizeof(svc_set_id), &svc_set_id));
    
    if(ss == NULL) {
        LOG(LOG_WARNING, "%s: unknown service set", __func__);
        return;
    }
    
    // Remove ss from patricia tree of the service sets
    if (!patricia_delete(&services_conf, &ss->node)) {
        LOG(LOG_ERR, "%s: Removing service set from the configuration",
                __func__);
        return;
    }
    
    if(ss->applications == NULL) {
        free(ss);
        return;
    }
    
    monitor_remove_all_servers_in_service_set(svc_set_id);
    
    while((app= app_entry(patricia_find_next(ss->applications, NULL))) != NULL){
        
        if(app->servers != NULL) {
            while((server = TAILQ_FIRST(app->servers)) != NULL) {
                TAILQ_REMOVE(app->servers, server, entries);
                free(server);
            }
        }
        
        if(app->server_mon_params != NULL) {
            free(app->server_mon_params);
        }
        
        // Remove app from patricia tree of the service set
        if (!patricia_delete(ss->applications, &app->node)) {
            LOG(LOG_ERR, "%s: Removing application from the service set failed",
                    __func__);
        }
        free(app);        
    }
    
    patricia_root_delete(ss->applications);
    free(ss);
}


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
                   char * app_name)
{
    eq_app_t * app;
    eq_serviceset_t * ss;
    eq_server_t * server;
    
    // get this service-set
    ss = service_entry(
            patricia_get(&services_conf, sizeof(svc_set_id), &svc_set_id));
    
    if(ss == NULL) {
        LOG(LOG_WARNING, "%s: unknown service set", __func__);
        return;
    }
    
    if(ss->applications == NULL) {
        return;
    }
    
    app = app_entry(
            patricia_get(ss->applications, strlen(app_name) + 1, app_name));
    
    if(app == NULL) {
        return;
    }
    
    monitor_remove_all_servers_in_app(
            svc_set_id, app->application_addr, app->application_port);
    
    while((server = TAILQ_FIRST(app->servers)) != NULL) {
        TAILQ_REMOVE(app->servers, server, entries);
        free(server);
    }
    
    if(app->server_mon_params != NULL) {
        free(app->server_mon_params);
    }
    
    // Remove app from patricia tree of the service set
    if (!patricia_delete(ss->applications, &app->node)) {
        LOG(LOG_ERR, "%s: Removing application from the service set failed",
                __func__);
        return;
    }
    free(app);
}


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
                   char * app_name)
{
    eq_app_t * app;
    eq_serviceset_t * ss;
    eq_server_t * server;
    
    // get this service-set
    ss = service_entry(
            patricia_get(&services_conf, sizeof(svc_set_id), &svc_set_id));
    
    if(ss == NULL) {
        LOG(LOG_WARNING, "%s: unknown service set", __func__);
        return;
    }
    
    // check if any applications config exists
    if(ss->applications == NULL) {
        LOG(LOG_WARNING, "%s: unknown application", __func__);
        return;
    }
    
    app = app_entry(
            patricia_get(ss->applications, strlen(app_name) + 1, app_name));
    
    if(app == NULL) {
        LOG(LOG_WARNING, "%s: unknown application", __func__);
        return;
    }
    
    monitor_remove_all_servers_in_app(
            svc_set_id, app->application_addr, app->application_port);
    
    while((server = TAILQ_FIRST(app->servers)) != NULL) {
        TAILQ_REMOVE(app->servers, server, entries);
        free(server);
    }
}


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
              in_addr_t server_addr)
{
    eq_app_t * app;
    eq_serviceset_t * ss;
    eq_server_t * server;
    
    // get this service-set
    ss = service_entry(
            patricia_get(&services_conf, sizeof(svc_set_id), &svc_set_id));
    
    if(ss == NULL) {
        LOG(LOG_WARNING, "%s: server in unknown service set", __func__);
        return;
    }
    
    // check if any applications config exists
    if(ss->applications == NULL) {
        LOG(LOG_WARNING, "%s: server in unknown application", __func__);
        return;
    }
    
    app = app_entry(
            patricia_get(ss->applications, strlen(app_name) + 1, app_name));
    
    if(app == NULL) {
        LOG(LOG_WARNING, "%s: server in unknown application", __func__);
        return;
    }
    
    server = TAILQ_FIRST(app->servers);
    while(server != NULL) {
        if(server->server_addr == server_addr) {
            TAILQ_REMOVE(app->servers, server, entries); // got it
            
            // tell the monitor about this server being removed
            monitor_remove_server(svc_set_id, app->application_addr,
                    app->application_port, server->server_addr);
            
            free(server);
            return;
        }
        server = TAILQ_NEXT(server, entries);
    }
    LOG(LOG_WARNING, "%s: server is already not configured", __func__);
}


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
                   uint16_t down_retry_interval)
{
    eq_app_t * app;
    eq_serviceset_t * ss;
    eq_smon_t * smon = NULL;
    eq_server_t * server = NULL;
    
    // check if we know about this service-set
    ss = service_entry(
            patricia_get(&services_conf, sizeof(svc_set_id), &svc_set_id));
    
    if(ss == NULL) {
        ss = calloc(1, sizeof(eq_serviceset_t));
        INSIST_ERR(ss != NULL);
        ss->svc_set_id = svc_set_id;

        // Add to patricia tree (without node length init) since it is fixed
        if(!patricia_add(&services_conf, &ss->node)) {
            LOG(LOG_ERR, "%s: Failed to add service set %d to configuration",
                    __func__, svc_set_id);
            return;
        }
    }
    
    // check if any applications config exists
    if(ss->applications == NULL) {
        // if not, init it
        ss->applications = patricia_root_init(NULL, FALSE, MAX_APP_NAME, 0);
        INSIST_ERR(ss->applications != NULL);
    }
    
    app = app_entry(
            patricia_get(ss->applications, strlen(app_name) + 1, app_name));
    
    if(connection_interval != 0) {
        smon = calloc(1, sizeof(eq_smon_t));
        INSIST_ERR(smon != NULL);
        smon->connection_interval = connection_interval;
        smon->connection_timeout = connection_timeout;
        smon->timeouts_allowed = timeouts_allowed;
        smon->down_retry_interval = down_retry_interval;
    }
    
    if(app == NULL) {
        app = calloc(1, sizeof(eq_app_t));
        INSIST_ERR(app != NULL);
        strncpy(app->application_name, app_name, MAX_APP_NAME);

        // Add to patricia tree
        patricia_node_init_length(&app->node, strlen(app_name) + 1);

        if(!patricia_add(ss->applications, &app->node)) {
            LOG(LOG_ERR, "%s: Failed to add application %s to "
                    "configuration", __func__, app_name);
            return;
        }
        
        app->servers = malloc(sizeof(server_list_t));
        INSIST_ERR(app->servers != NULL);
        TAILQ_INIT(app->servers);
        
        app->server_mon_params = smon;
        app->application_port = app_port;
        app->application_addr = app_addr;
        app->session_timeout = session_timeout;
        
        LOG(LOG_INFO, "%s: Added new application <%d,%s> to configuration",
                __func__, svc_set_id, app_name);
        
        return;
    }
    
    // it is now new, so we need to check for changes
    
    app->session_timeout = session_timeout;
    
    if(app->application_port != app_port || app->application_addr != app_addr) {
        
        // delete server and re-add them
        monitor_remove_all_servers_in_app(
                svc_set_id, app->application_addr, app->application_port);
        
        app->application_port = app_port;
        app->application_addr = app_addr;
        
        server = TAILQ_FIRST(app->servers);
        while(server != NULL) {

            monitor_add_server(svc_set_id, app->application_addr,
                    app->application_port, server->server_addr,
                    app->server_mon_params);
            
            server = TAILQ_NEXT(server, entries);
        }
    }
    
    if(app->server_mon_params == NULL) {
        if(smon != NULL) { // added
            change_monitoring_config(svc_set_id, app_addr, app_port, smon);
            app->server_mon_params = smon;
        }
    } else { // existing monitor, look for a change
        if(smon == NULL) { // removed
            change_monitoring_config(svc_set_id, app_addr, app_port, smon);
            free(app->server_mon_params);
            app->server_mon_params = NULL;
        } else {
            if(memcmp(smon, app->server_mon_params, sizeof(eq_smon_t)) != 0) {
                change_monitoring_config(svc_set_id, app_addr, app_port, smon);
                free(app->server_mon_params);
                app->server_mon_params = smon;
            } else {
                free(smon); // no change to existing monitor parameters
            }
        }
    }
}


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
           in_addr_t server_addr)
{
    eq_app_t * app;
    eq_serviceset_t * ss;
    eq_server_t * server = NULL;
    
    // get this service-set
    ss = service_entry(
            patricia_get(&services_conf, sizeof(svc_set_id), &svc_set_id));
    
    if(ss == NULL) {
        LOG(LOG_ERR, "%s: server in unknown service set %d", __func__,
                svc_set_id);
        return;
    }
    
    // check if any applications config exists
    if(ss->applications == NULL) {
        LOG(LOG_ERR, "%s: server in unknown application", __func__);
        return;
    }
    
    app = app_entry(
            patricia_get(ss->applications, strlen(app_name) + 1, app_name));
    
    if(app == NULL) {
        LOG(LOG_ERR, "%s: server in unknown application", __func__);
        return;
    }
    
    server = TAILQ_FIRST(app->servers);
    while(server != NULL) {
        if(server->server_addr == server_addr) {
            return; // already know about this server
        }
        server = TAILQ_NEXT(server, entries);            
    }
    
    server = malloc(sizeof(eq_server_t));
    INSIST_ERR(server != NULL);
    server->server_addr = server_addr;
    TAILQ_INSERT_TAIL(app->servers, server, entries);
    
    // tell the monitor about this new server
    monitor_add_server(svc_set_id, app->application_addr, app->application_port,
            server_addr, app->server_mon_params);
}


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
get_app_name(uint16_t svc_set_id, in_addr_t addr, uint16_t port)
{
    eq_app_t * app;
    eq_serviceset_t * ss;
    
    // get this service-set
    ss = service_entry(
            patricia_get(&services_conf, sizeof(svc_set_id), &svc_set_id));
    
    if(ss == NULL || ss->applications == NULL) {
        return NULL;
    }
    
    // get first application
    app = app_entry(patricia_find_next(ss->applications, NULL));
    
    while(app != NULL) {
        
        if(app->application_addr == addr && app->application_port == port) {
            return app->application_name;
        }
        
        app = app_entry(patricia_find_next(ss->applications, &app->node));       
    }
    
    return NULL;
}


/**
 * Search for the application with the given parameters,
 * and if it exists return its session timeout.
 * Always only called by main thread.
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
get_app_session_timeout(uint16_t svc_set_id, in_addr_t addr, uint16_t port)
{
    eq_app_t * app;
    eq_serviceset_t * ss;
    
    // get this service-set
    ss = service_entry(
            patricia_get(&services_conf, sizeof(svc_set_id), &svc_set_id));
    
    if(ss == NULL || ss->applications == NULL) {
        return 0;
    }
    
    // get first application
    app = app_entry(patricia_find_next(ss->applications, NULL));
    
    while(app != NULL) {
        
        if(app->application_addr == addr && app->application_port == port) {
            return app->session_timeout;
        }
        
        app = app_entry(patricia_find_next(ss->applications, &app->node));       
    }
    
    return 0;
}

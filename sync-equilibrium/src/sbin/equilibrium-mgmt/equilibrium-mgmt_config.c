/*
 * $Id: equilibrium-mgmt_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-mgmt_config.c
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions will parse and load the configuration data.
 */

#include <sync/common.h>
#include <ddl/dax.h>
#include "equilibrium-mgmt_config.h"
#include "equilibrium-mgmt_conn.h"
#include "equilibrium-mgmt_logging.h"

#include EQUILIBRIUM_OUT_H

/*** Constants ***/

#define MAX_SVC_SETID                       0xFFFF  ///< Max Service set id

#define DDLNAME_SERVICE_SET_IDENT "service-set-name" ///< service-set name's attribute
#define DDLNAME_EXTENTION_SERVICE_IDENT "service-name" ///< extension-service name's attribute
#define DDLNAME_INTERFACE_SERVICE "interface-service" ///< interface-service object
#define DDLNAME_INTERFACE_SERVICE_SVC_INT "service-interface" ///< service-interface attribute


/*** Data Structures ***/

   
/**
 * Mandatory prefix for any service name belonging to equilibrium
 */
const char * equilibrium_service_prefix = "equilibrium-";

/**
 * The structure we use to bundle the app address and port.
 * We keep a list of these during the configuration check 
 * to ensure no duplicates within a single service set.
 */
typedef struct eq_app_key_s {
    in_addr_t addr;   ///< The application address
    uint16_t  port;   ///< The application port
    TAILQ_ENTRY(eq_app_key_s) entries; ///< next/prev entries
} eq_app_key_t;

/**
 * A set of eq_app_key_t
 */
typedef TAILQ_HEAD(, eq_app_key_s) eq_app_key_set_t;

/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for an SSRB
 */
typedef struct eq_ssrb_s {
    patnode    node;              ///< Tree node in ssrb_conf (comes first)
    junos_kcom_pub_ssrb_t ssrb;   ///< SSRB info
} eq_ssrb_t;

static patroot   services_conf;      ///< Pat. root for services config
static patroot   ssrb_conf;          ///< Pat. root for ssrb config (lookup by name)

static eq_serviceset_t * current_svc_set = NULL; ///< current service set while loading configuration
static eq_app_t *        current_app = NULL;  ///< current application while loading configuration


/*** STATIC/INTERNAL Functions ***/


/**
 * Generate the ssrb_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(ssrb_entry, eq_ssrb_t, node)


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
 * Update a service set. If it doesn't exist, then create it,
 * and add it to the config tree
 * 
 * @param[in] ss_name
 *      service-set name
 * 
 * @param[in] svc_name
 *      service name
 * 
 * @param[in] svc_int_name
 *      service interface name
 * 
 * @return
 *      The service set
 */
static eq_serviceset_t *
update_service_set(const char * ss_name,
                   const char * svc_name,
                   const char * svc_int_name)
{
    eq_serviceset_t * service_set = NULL;
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    // check if we know about this service-set
    service_set = service_entry(patricia_get(&services_conf,
                    strlen(ss_name) + 1, ss_name));
    
    if(service_set == NULL) {
        service_set = calloc(1, sizeof(eq_serviceset_t));
        INSIST(service_set != NULL);
        service_set->svc_set_id = -1; // set it to be invalid
        strncpy(service_set->svc_set_name, ss_name, MAX_SVC_SET_NAME);

        // Add to patricia tree
        patricia_node_init_length(&service_set->node, strlen(ss_name) + 1);

        if(!patricia_add(&services_conf, &service_set->node)) {
            junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s: Failed to add "
                    "service set to configuration", __func__);
        }
    }
    
    strncpy(service_set->svc_int_name, svc_int_name, MAX_SVC_INT_NAME);
    strncpy(service_set->ext_svc_name, svc_name, MAX_SVC_NAME);
    
    return service_set;
}


/**
 * Update or add an application for the current service set
 * 
 * @param[in] name
 *      The application name
 * 
 * @param[in] address
 *      The application facade address
 * 
 * @param[in] port
 *      The application port
 * 
 * @param[in] server_monitor
 *      NULL for no monitor or the server monitor parameters
 * 
 * @param[in] session_timeout
 *      The session timeout for the application
 * 
 * @return
 *      The added application
 */
static eq_app_t *
update_application(const char * name,
                   in_addr_t address,
                   uint16_t port,
                   eq_smon_t * server_monitor,
                   uint16_t session_timeout)
{
    eq_app_t * app;
    
    INSIST_ERR(current_svc_set != NULL);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    // check if any applications config exists
    if(current_svc_set->applications == NULL) {
        // if not, init it
        current_svc_set->applications = 
            patricia_root_init(NULL, FALSE, MAX_APP_NAME, 0);
    }
    
    app = app_entry(patricia_get(current_svc_set->applications,
            strlen(name) + 1, name));
    
    if(app == NULL) {
        app = calloc(1, sizeof(eq_app_t));
        INSIST(app != NULL);
        strncpy(app->application_name, name, MAX_APP_NAME);

        // Add to patricia tree
        patricia_node_init_length(&app->node, strlen(name) + 1);

        if(!patricia_add(current_svc_set->applications, &app->node)) {
            junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s: Failed to add "
                    "application to configuration", __func__);
            return NULL;
        }
    }
    
    app->application_addr = address;
    app->application_port = port;
    app->session_timeout = session_timeout;
    
    if(app->server_mon_params == NULL) {
        app->server_mon_params = server_monitor;
        
        if(server_monitor == NULL) {
            notify_application_update(current_svc_set->svc_set_name,
                name, address, port, session_timeout, 0, 0, 0, 0);
        } else {
            notify_application_update(current_svc_set->svc_set_name,
                name, address, port, session_timeout,
                server_monitor->connection_interval,
                server_monitor->connection_timeout,
                server_monitor->timeouts_allowed,
                server_monitor->down_retry_interval);
        }
    } else {
        if(server_monitor == NULL) {
            free(app->server_mon_params);
            app->server_mon_params = NULL;
            
            notify_application_update(current_svc_set->svc_set_name,
                name, address, port, session_timeout, 0, 0, 0, 0);
        } else {
            app->server_mon_params->connection_interval = 
                server_monitor->connection_interval;
            app->server_mon_params->connection_timeout = 
                server_monitor->connection_timeout;
            app->server_mon_params->timeouts_allowed = 
                server_monitor->timeouts_allowed;
            app->server_mon_params->down_retry_interval = 
                server_monitor->down_retry_interval;
            
            notify_application_update(current_svc_set->svc_set_name,
                name, address, port, session_timeout,
                server_monitor->connection_interval,
                server_monitor->connection_timeout,
                server_monitor->timeouts_allowed,
                server_monitor->down_retry_interval);
        }
    }
    
    return app;
}


/**
 * Add a server to the set for the current application
 * 
 * @param[in] server_address
 *      The address of the server to add
 */
static void
update_server(in_addr_t server_address)
{
    eq_server_t * server = NULL;
    
    INSIST_ERR(current_app != NULL);
    INSIST_ERR(current_svc_set != NULL);

    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    if(current_app->servers == NULL) {
        current_app->servers = malloc(sizeof(server_list_t));
        INSIST(current_app->servers != NULL);
        TAILQ_INIT(current_app->servers);
    }
    
    server = calloc(1, sizeof(eq_server_t));
    INSIST(server != NULL);
    
    server->server_addr = server_address;
    TAILQ_INSERT_TAIL(current_app->servers, server, entries);
    
    notify_server_update(current_svc_set->svc_set_name,
            current_app->application_name, server->server_addr);
}


/**
 * Delete a server from the set of servers for the current application
 * 
 * @param[in] server_address
 *      The address of the server to delete
 */
static void
delete_server(in_addr_t server_address)
{
    eq_server_t * server = NULL;
    
    INSIST_ERR(current_app != NULL);
    INSIST_ERR(current_svc_set != NULL);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    if(current_app->servers != NULL) {
        server = TAILQ_FIRST(current_app->servers);
        while(server != NULL) {
            if(server->server_addr == server_address) {
                TAILQ_REMOVE(current_app->servers, server, entries);
                
                notify_server_delete(current_svc_set->svc_set_name,
                        current_app->application_name, server->server_addr);
                
                free(server);
                break;
            }
            server = TAILQ_NEXT(server, entries);
        }
    }
}


/**
 * Delete an application from the configuration
 * 
 * @param[in] apps
 *      The Pat tree root containing the application
 * 
 * @param[in] application
 *      Pointer to the application to delete
 * 
 * @param[in] notify
 *      Whether or not to notify data component,
 *      if not it should be done by caller
 */
static void
delete_application(patroot * apps, eq_app_t ** application, boolean notify)
{
    eq_app_t * app = *application;
    eq_server_t * server = NULL;
    
    if(notify) {
        INSIST_ERR(current_svc_set != NULL);
    }
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    // Remove app from patricia tree
    if (!patricia_delete(apps, &app->node)) {
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONF,
                "%s: Deleting application failed", __func__);
        return;
    }
    
    if(app->server_mon_params != NULL) {
        free(app->server_mon_params);
    }
    
    if(app->servers != NULL) {
        while((server = TAILQ_FIRST(app->servers)) != NULL) {
            TAILQ_REMOVE(app->servers, server, entries);
            free(server);
        }
    }
    
    if(notify) {
        notify_application_delete(current_svc_set->svc_set_name,
            app->application_name);
    }
    
    free(*application);
    *application = NULL;
}


/**
 * Delete a service set from the configuration
 * 
 * @param[in] service_set
 *      Pointer to the service set to delete
 * 
 * @param[in] notify
 *      Whether or not to notify data component,
 *      if not it should be done by caller
 */
static void
delete_service_set(eq_serviceset_t ** service_set, boolean notify)
{
    eq_serviceset_t * ss = *service_set;
    eq_app_t * app;
    junos_kcom_pub_ssrb_t * ssrb;
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    // Remove service set from patricia tree
    if (!patricia_delete(&services_conf, &ss->node)) {
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONF,
                "%s: Deleting service set failed", __func__);
        return;
    }
    
    if(ss->applications != NULL) {
        while(NULL != (app = 
            app_entry(patricia_find_next(ss->applications, NULL)))) {
            
            delete_application(ss->applications, &app, FALSE);
        }
        patricia_root_delete(ss->applications);
    }
    
    if(notify) {
        if(ss->svc_set_id != -1) {
            // ssrb was deleted already
            notify_serviceset_delete(ss->svc_set_name,(uint16_t)ss->svc_set_id);
        } else if((ssrb = get_ssrb_by_name(ss->svc_set_name)) != NULL){
            // ssrb was not yet deleted
            notify_serviceset_delete(ss->svc_set_name, ssrb->svc_set_id);
        } else {
            // can't find the service set id
            junos_trace(EQUILIBRIUM_TRACEFLAG_CONF,
                    "%s: Cannot find the service-set id to delete "
                    "the %s service set", __func__, ss->svc_set_name);
        }
    }
    
    free(*service_set);
    *service_set = NULL;
}


/**
 * Delete all servers from the current application's configuration
 */
static void
delete_all_servers(void)
{
    // NOTE use current_app as context

    eq_server_t * server = NULL;
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    if(current_app->servers != NULL) {
        while((server = TAILQ_FIRST(current_app->servers)) != NULL) {
            TAILQ_REMOVE(current_app->servers, server, entries);
            free(server);
        }
        notify_delete_all_servers(current_svc_set->svc_set_name,
                current_app->application_name);
    }
}


/**
 * Delete all applications from the current service set's configuration
 */
static void
delete_all_applications(void)
{
    // NOTE use current_svc_set as context
    eq_app_t * app;
    junos_kcom_pub_ssrb_t * ssrb;
    
    INSIST_ERR(current_svc_set != NULL);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    if(current_svc_set->applications != NULL) {
        while((app = app_entry(patricia_find_next(
                        current_svc_set->applications, NULL))) != NULL) {
            delete_application(current_svc_set->applications, &app, FALSE);
        }
        patricia_root_delete(current_svc_set->applications);
        current_svc_set->applications = NULL;
        
        if(current_svc_set->svc_set_id != -1) {
            // ssrb was deleted already
            notify_delete_all_applications(current_svc_set->svc_set_name,
                    current_svc_set->svc_set_id);            
        } else if((ssrb = get_ssrb_by_name(current_svc_set->svc_set_name))
                        != NULL) {
            // ssrb was not yet deleted
            notify_delete_all_applications(current_svc_set->svc_set_name,
                    ssrb->svc_set_id);
        } else {
            // can't find the service set id
            junos_trace(EQUILIBRIUM_TRACEFLAG_CONF,
                    "%s: Cannot find the service-set id to delete "
                    "the %s service set's applications", __func__,
                    current_svc_set->svc_set_name);
        }
    }
}


/**
 * Delete all service sets from the configuration
 */
static void
delete_all_service_sets(void)
{
    eq_serviceset_t * ss;
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    while((ss = service_entry(patricia_find_next(&services_conf, NULL)))
            != NULL) {
        delete_service_set(&ss, FALSE);
    }
    
    notify_delete_all();
}


/**
 * Handler for dax_walk_list to parse each configured server knob
 * 
 * @param[in] dwd
 *      Opaque dax data
 * 
 * @param[in] dop
 *      DAX Object Pointer for server object
 * 
 * @param[in] action
 *      The action on the given server object
 * 
 * @param[in] data
 *      User data passed to handler (check flag)
 * 
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int 
parse_server(dax_walk_data_t * dwd,
             ddl_handle_t * dop,
             int action,
             void * data)
{
    // NOTE use current_svc_set and current_app as context
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr tmp;
    in_addr_t server_addr;
    int addr_fam;
    int check = *((int *)data);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    if(action == DAX_ITEM_DELETE_ALL) {
        // All servers were deleted
        delete_all_servers();
        return DAX_WALK_OK;
    }
    
    INSIST(dop != NULL);
    
    switch(action) {

    case DAX_ITEM_DELETE:
        // a server was deleted
        
        // get the (deleted) server adderss
        // for deleted items we can only get a string form:
        if (!dax_get_stringr_by_dwd_ident(dwd, NULL, 0,
                ip_str, sizeof(ip_str))) {
            dax_error(dop, "Failed to parse server IP");
            return DAX_WALK_ABORT;
        }
        
        if(inet_aton(ip_str, &tmp) != 1) { // if failed
            dax_error(dop, "Failed to parse server IP: %s", ip_str);
            return DAX_WALK_ABORT;
        }
        server_addr = tmp.s_addr;
        
        if(!check) {
            delete_server(server_addr);
            
            // check if any servers left, o/w delete list/set
            if(TAILQ_FIRST(current_app->servers) == NULL) {
                free(current_app);
                current_app->servers = NULL;
            }
        }
        
        break;
        
    case DAX_ITEM_CHANGED:
        // a server was added

        if(!dax_get_ipaddr_by_name(dop, DDLNAME_APP_SERVERS_SERVER,
                &addr_fam, &server_addr, sizeof(server_addr)) ||
                addr_fam != AF_INET) {
            dax_error(dop, "Failed to parse server IP");
            return DAX_WALK_ABORT;
        }
        
        if(!check) {
            update_server(server_addr);
        }
        
        break;
        
    case DAX_ITEM_UNCHANGED:
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, 
                "%s: DAX_ITEM_UNCHANGED observed", __func__);
        break;
    
    default:
        break;
    }

    return DAX_WALK_OK;
}


/**
 * Check the uniqueness of the application's address and port given 
 * a list of existing applications' addresses and ports. If it is unique 
 * we add it to the list. This list is only maintained temporarily
 * 
 * @param[in] dwd
 *      Opaque dax data
 * 
 * @param[in] dop
 *      DAX Object Pointer for application object
 * 
 * @param[in] action
 *      The action on the given application object
 * 
 * @param[in] data
 *      User data passed to handler
 *      (list of existing application addresses and ports)
 * 
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int 
check_applications(dax_walk_data_t * dwd __unused,
                   ddl_handle_t * dop,
                   int action __unused,
                   void * data)
{
    // NOTE use current_svc_set as context

    eq_app_key_set_t * app_key_set  = (eq_app_key_set_t *)data;
    eq_app_key_t * key, * tmp;
    int addr_fam;
    
    INSIST_ERR(dop != NULL);
    INSIST_ERR(app_key_set != NULL);
    
    key = calloc(1, sizeof(eq_app_key_t));
    INSIST_ERR(key != NULL);
        
    // read in the attributes of the address and port into the new app key
    if (!dax_get_ipaddr_by_name(dop,
            DDLNAME_SVCS_SVC_SET_EXT_SERVICE_APP_APPLICATION_ADDRESS,
            &addr_fam, &key->addr, sizeof(key->addr)) ||
            addr_fam != AF_INET) {
        dax_error(dop, "Failed to parse application address");
        return DAX_WALK_ABORT;
    }
    
    if (!dax_get_ushort_by_name(dop,
            DDLNAME_SVCS_SVC_SET_EXT_SERVICE_APP_APPLICATION_PORT,
            &key->port)) {
        dax_error(dop, "Failed to parse application port");
        return DAX_WALK_ABORT;
    }
    
    // ensure uniqueness before adding to this list
    
    tmp = TAILQ_FIRST(app_key_set);
    while(tmp != NULL) {
        if(tmp->addr == key->addr && tmp->port == key->port) {
            dax_error(dop, "Found another application with the same "
                    "facade address and port within the same service set. "
                    "This is not allowed.");
            return DAX_WALK_ABORT;
        }
        tmp = TAILQ_NEXT(tmp, entries);  
    }
    
    TAILQ_INSERT_TAIL(app_key_set, key, entries);
    
    return DAX_WALK_OK;
}


/**
 * Handler for dax_walk_list to parse each configured application knob
 * 
 * @param[in] dwd
 *      Opaque dax data
 * 
 * @param[in] dop
 *      DAX Object Pointer for application object
 * 
 * @param[in] action
 *      The action on the given application object
 * 
 * @param[in] data
 *      User data passed to handler (check flag)
 * 
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int 
parse_application(dax_walk_data_t * dwd,
                  ddl_handle_t * dop,
                  int action,
                  void * data)
{
    // NOTE use current_svc_set as context

    char app_name[MAX_APP_NAME];
    in_addr_t app_addr;
    int addr_fam;
    uint16_t app_port;
    uint16_t app_session_timeout;
    eq_smon_t * app_s_monitor;
    eq_app_t * app;
    ddl_handle_t * smon_dop;
    ddl_handle_t * servers_dop;
    int check = *((int *)data);

    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    switch (action) {
    
    case DAX_ITEM_DELETE_ALL:
        
        // All applications were deleted
        delete_all_applications();
        return DAX_WALK_OK;
        
        break;

    case DAX_ITEM_DELETE:
        
        // get the (deleted) application name
        if (!dax_get_stringr_by_dwd_ident(dwd, NULL, 0,
                app_name, sizeof(app_name))) {
            dax_error(dop, "Failed to parse a service set name");
            return DAX_WALK_ABORT;
        }
        
        if(check) {
            break; // nothing to do
        }
        
        app = app_entry(patricia_get(current_svc_set->applications,
                                    strlen(app_name) + 1, app_name));
        if(app != NULL) {
            delete_application(current_svc_set->applications, &app, TRUE);
        }
        
        // check if any applications left, o/w delete patroot for applications
        if((patricia_find_next(current_svc_set->applications, NULL)) == NULL) {
            patricia_root_delete(current_svc_set->applications);
            current_svc_set->applications = NULL;
        }
        
        break;

    case DAX_ITEM_CHANGED:

        INSIST_ERR(dop != NULL);
        
        // get the application name
        
        if (!dax_get_stringr_by_name(dop, 
                DDLNAME_SVCS_SVC_SET_EXT_SERVICE_APP_APPLICATION_NAME,
                app_name, sizeof(app_name))) {
            dax_error(dop, "Failed to parse application name");
            return DAX_WALK_ABORT;
        }
        
        // read in some of the attributes
        
        if (!dax_get_ipaddr_by_name(dop,
                DDLNAME_SVCS_SVC_SET_EXT_SERVICE_APP_APPLICATION_ADDRESS,
                &addr_fam, &app_addr, sizeof(app_addr)) ||
                addr_fam != AF_INET) {
            dax_error(dop, "Failed to parse application address");
            return DAX_WALK_ABORT;
        }
        
        if (!dax_get_ushort_by_name(dop,
                DDLNAME_SVCS_SVC_SET_EXT_SERVICE_APP_APPLICATION_PORT,
                &app_port)) {
            dax_error(dop, "Failed to parse application port");
            return DAX_WALK_ABORT;
        }
        
        if (!dax_get_ushort_by_name(dop,
                DDLNAME_SVCS_SVC_SET_EXT_SERVICE_APP_SESSION_TIMEOUT,
                &app_session_timeout)) {
            dax_error(dop, "Failed to parse session timeout");
            return DAX_WALK_ABORT;
        }
        
        if(!dax_get_object_by_name(dop, DDLNAME_APP_SMON, &smon_dop, FALSE)) {
            app_s_monitor = NULL;
        } else {
            app_s_monitor = malloc(sizeof(eq_smon_t));
            INSIST(app_s_monitor != NULL);
            
            if (!dax_get_ushort_by_name(smon_dop,
                    DDLNAME_APP_SMON_SERVER_CONNECTION_INTERVAL,
                    &app_s_monitor->connection_interval)) {
                dax_error(smon_dop, "Failed to parse connection interval");
                dax_release_object(&smon_dop);
                return DAX_WALK_ABORT;
            }
            
            if (!dax_get_ushort_by_name(smon_dop,
                    DDLNAME_APP_SMON_SERVER_CONNECTION_TIMEOUT,
                    &app_s_monitor->connection_timeout)) {
                dax_error(smon_dop, "Failed to parse connection timeout");
                dax_release_object(&smon_dop);
                return DAX_WALK_ABORT;
            }
            
            if (!dax_get_ubyte_by_name(smon_dop,
                    DDLNAME_APP_SMON_SERVER_TIMEOUTS_ALLOWED,
                    &app_s_monitor->timeouts_allowed)) {
                dax_error(smon_dop, "Failed to parse timeouts allowed");
                dax_release_object(&smon_dop);
                return DAX_WALK_ABORT;
            }
            
            if (!dax_get_ushort_by_name(smon_dop,
                    DDLNAME_APP_SMON_SERVER_DOWN_RETRY_INTERVAL,
                    &app_s_monitor->down_retry_interval)) {
                dax_error(smon_dop, "Failed to parse down retry interval");
                dax_release_object(&smon_dop);
                return DAX_WALK_ABORT;
            }
            dax_release_object(&smon_dop);
            
            if(check) {
                free(app_s_monitor);
            }
        }
        
        // update
        if(!check) {
            current_app = update_application(app_name, app_addr, app_port,
                    app_s_monitor, app_session_timeout);
        }
        
        // check servers
        if(!dax_get_object_by_name(dop, DDLNAME_APP_SERVERS, &servers_dop, 
                FALSE)) {
            dax_error(dop, "Failed to parse servers");
            return DAX_WALK_ABORT;
            
        } else if(dax_walk_list(servers_dop, DAX_WALK_DELTA, parse_server, data)
                    != DAX_WALK_OK) {

                dax_release_object(&servers_dop);
                current_app = NULL;
                return DAX_WALK_ABORT; 
        }
        current_app = NULL;
        dax_release_object(&servers_dop);
        
        break;
        
    case DAX_ITEM_UNCHANGED:
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, 
                "%s: DAX_ITEM_UNCHANGED observed", __func__);
        break;

    default:
        break;
    }

    return DAX_WALK_OK;
}


/**
 * Handler for dax_walk_list to parse each configured service-set knob
 * 
 * @param[in] dwd
 *      Opaque dax data
 * 
 * @param[in] dop
 *      DAX Object Pointer for service-set object
 * 
 * @param[in] action
 *      The action on the given service-set object
 * 
 * @param[in] data
 *      User data passed to handler (check flag)
 * 
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int 
parse_service_set(dax_walk_data_t * dwd,
                  ddl_handle_t * dop,
                  int action,
                  void * data)
{
    const char * service_config[] = 
        {DDLNAME_SVCS_SVC_SET_EXT_SERVICE, NULL};
    const char * applications_config[] = 
        {DDLNAME_SVCS_SVC_SET_EXT_SERVICE_APP, NULL};
    const char * service_int_config[] = 
        {DDLNAME_INTERFACE_SERVICE, NULL};

    char svc_set_name[MAX_SVC_SET_NAME];
    char service_name[MAX_SVC_NAME];
    char service_int_name[MAX_SVC_INT_NAME];
    eq_serviceset_t * service_set = NULL;
    eq_app_key_set_t app_key_set;
    eq_app_key_t * key;
    ddl_handle_t * service_container_dop = NULL;
    ddl_handle_t * service_dop = NULL;
    ddl_handle_t * applications_dop = NULL;
    ddl_handle_t * svc_int_dop = NULL;
    int check = *((int *)data);
    int found = 0;

    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s", __func__);
    
    switch (action) {

    case DAX_ITEM_DELETE_ALL:

        // All items under service-set were deleted
        delete_all_service_sets();
        return DAX_WALK_OK;
        
        break;
    
    case DAX_ITEM_DELETE:
        
        // One item got deleted
        
        // get the (deleted) service set name
        if (!dax_get_stringr_by_dwd_ident(dwd, NULL, 0,
                svc_set_name, sizeof(svc_set_name))) {
            dax_error(dop, "Failed to parse a service set name");
            return DAX_WALK_ABORT;
        }
        
        if(!check) {
            // check if we know about this one
            service_set = service_entry(patricia_get(&services_conf,
                                        strlen(svc_set_name) + 1, svc_set_name));
            
            if(service_set != NULL) {
                // add a delete action to action list &
                // remove service-set from config
                delete_service_set(&service_set, TRUE);
            }
        }
        
        break;

    case DAX_ITEM_CHANGED:
        
        // service set added or renamed
        // everything under it will be marked changed too

        INSIST_ERR(dop != NULL);
        
        // get the service set name
        if (!dax_get_stringr_by_name(dop, DDLNAME_SERVICE_SET_IDENT,
                svc_set_name, sizeof(svc_set_name))) {
            dax_error(dop, "Failed to parse a service set name");
            return DAX_WALK_ABORT;
        }
        
        // check for our service in the service-set
        // i.e. extension-service with "equilibrium-" prefixed service-name
        
        if (dax_get_object_by_path(dop, service_config,
                &service_container_dop, FALSE)) {
        
            // some extension-service config exists
            // see if it is an equilibrium service
            
            while (dax_visit_container(service_container_dop, &service_dop)) {
                
                if (!dax_get_stringr_by_name(service_dop,
                        DDLNAME_EXTENTION_SERVICE_IDENT, service_name, 
                        sizeof(service_name))) {
                    dax_error(service_dop,
                            "Failed to parse an extension-service name");
                    return DAX_WALK_ABORT;
                }
                
                // check chosen service name for prefix:
                if(strstr(service_name, equilibrium_service_prefix) == NULL) {
                    continue;
                }
                
                // found equilibrium service!
                
                if(found) { // already found...error (caught in check)
                    dax_error(service_dop, "More than one equilibrium "
                        "service per service set is not allowed");
                    dax_release_object(&service_dop);
                    dax_release_object(&service_container_dop);
                    return DAX_WALK_ABORT;
                }
                found = 1;
                
                // get changes in the service-set's info
                
                // update service interface info
                if(!dax_get_object_by_path(dop, service_int_config,
                        &svc_int_dop, FALSE)) {
                    
                    dax_error(service_dop, "Equilibrium service has no "
                            "service interface configured in service set");
                    dax_release_object(&service_dop);
                    dax_release_object(&service_container_dop);
                    return DAX_WALK_ABORT;
                }
                
                // get service-int name
                if (!dax_get_stringr_by_name(svc_int_dop,
                        DDLNAME_INTERFACE_SERVICE_SVC_INT,
                        service_int_name, sizeof(service_int_name))) {
                    
                    dax_error(svc_int_dop,
                            "Failed to parse the service-interface name");
                    dax_release_object(&service_dop);
                    dax_release_object(&svc_int_dop);
                    dax_release_object(&service_container_dop);
                    return DAX_WALK_ABORT;
                }
                
                dax_release_object(&svc_int_dop);
                
                // update service set's info
                if(!check) {
                    current_svc_set = update_service_set(
                            svc_set_name, service_name, service_int_name);
                }
                
                // check applications
                if(!dax_get_object_by_path(service_dop, applications_config,
                        &applications_dop, FALSE)) {
                    
                    dax_error(service_dop, "Equilibrium "
                        "service has no applications configured");
                    dax_release_object(&service_dop);
                    dax_release_object(&service_container_dop);
                    return DAX_WALK_ABORT;
                }
                
                if(dax_walk_list(applications_dop, DAX_WALK_DELTA, 
                        parse_application, data) != DAX_WALK_OK) {
                    
                    dax_release_object(&service_dop);
                    dax_release_object(&service_container_dop);
                    current_svc_set = NULL;
                    return DAX_WALK_ABORT; 
                }
                if(check) {
                    // check that address and port combination is unique 
                    // within the service set
                    
                    TAILQ_INIT(&app_key_set);
                    
                    if(dax_walk_list(applications_dop, DAX_WALK_CONFIGURED, 
                            check_applications, &app_key_set) != DAX_WALK_OK) {
                        
                        dax_release_object(&service_dop);
                        dax_release_object(&service_container_dop);
                        current_svc_set = NULL;
                        // empty the set
                        while((key = TAILQ_FIRST(&app_key_set)) != NULL) {
                            TAILQ_REMOVE(&app_key_set, key, entries);  
                            free(key);
                        }
                        return DAX_WALK_ABORT; 
                    }
                    
                    // empty the set
                    while((key = TAILQ_FIRST(&app_key_set)) != NULL) {
                        TAILQ_REMOVE(&app_key_set, key, entries);  
                        free(key);
                    }
                }
                current_svc_set = NULL;
                
            } // end while
            dax_release_object(&service_container_dop);
            
            if(found) {
                return DAX_WALK_OK;
            }
            // no equilibrium service configured in service set if we reach here
        }
        
        if(!check) {
            // check if previously knew about this service-set
            service_set = service_entry(patricia_get(&services_conf,
                                       strlen(svc_set_name) + 1, svc_set_name));
            
            if(service_set != NULL) { // if so,
                delete_service_set(&service_set, TRUE); // delete it
            }
        }
        
        break;
        
    case DAX_ITEM_UNCHANGED:
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, 
                "%s: DAX_ITEM_UNCHANGED observed", __func__);
        break;

    default:
        break;
    }

    return DAX_WALK_OK;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structure that will store configuration info,
 * or in other words, the condition(s)
 */
void
init_config(void)
{
                        // root, is key ptr, key size, key offset
    patricia_root_init(&services_conf, FALSE, MAX_SVC_SET_NAME, 0);
    patricia_root_init(&ssrb_conf, FALSE, MAX_SVC_SET_NAME, 0);
}


/**
 * Clear the (non-SSRB) configuration info
 */
void
clear_config(void)
{
    delete_all_service_sets();
    
    clear_ssrb_config();
}


/**
 * Clear the SSRB configuration info
 */
void
clear_ssrb_config(void)
{
    eq_ssrb_t * s;
    while((s = ssrb_entry(patricia_find_next(&ssrb_conf, NULL))) != NULL) {
        if(!patricia_delete(&ssrb_conf, &s->node)) {
            LOG(TRACE_LOG_ERR, "%s: patricia delete FAILED!",
                __func__);
        }
        free(s);
    }
}


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
add_ssrb(junos_kcom_pub_ssrb_t * ssrb)
{
    eq_ssrb_t * s;
    
    // check for existance
    if(get_ssrb_by_name(ssrb->svc_set_name) != NULL)
        return EFAIL;
    
    // create data struct
    s = malloc(sizeof(eq_ssrb_t));
    INSIST(s != NULL);
    
    // copy data into new struct with node, init node, and add to tree
    strncpy(s->ssrb.svc_set_name, ssrb->svc_set_name, MAX_SVC_SET_NAME); 
    s->ssrb.svc_set_id = ssrb->svc_set_id;
    s->ssrb.in_nh_idx = ssrb->in_nh_idx;
    s->ssrb.out_nh_idx = ssrb->out_nh_idx;
    
    patricia_node_init_length(&s->node, strlen(ssrb->svc_set_name) +1);
    if(!patricia_add(&ssrb_conf, &s->node)) {
        LOG(TRACE_LOG_ERR, "%s: failed to add SSRB to configuration",
                __func__);
        return EFAIL;
    }
    return SUCCESS;
    
}


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
get_ssrb_by_name(char * name)
{
    eq_ssrb_t * s;
    
    s = ssrb_entry(patricia_get(&ssrb_conf, strlen(name) + 1, name));
    if(s != NULL)
        return &s->ssrb;
    return NULL;
}


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
get_ssrb_by_id(uint16_t svc_set_id)
{
    eq_ssrb_t * s;
    
    s = ssrb_entry(patricia_find_next(&ssrb_conf, NULL));
    while(s != NULL) {
        if(s->ssrb.svc_set_id == svc_set_id) {
            return &s->ssrb;
        }
        s = ssrb_entry(patricia_find_next(&ssrb_conf, &s->node));
    }
    return NULL;
}


/**
 * Remove an SSRB from the configuration by name
 * 
 * @param[in] name
 *     Service-set name
 */
void
delete_ssrb_by_name(char * name)
{
    eq_ssrb_t * s;
    eq_serviceset_t * service_set;
    
    if(name == NULL)
        return;
    
    s = ssrb_entry(patricia_get(&ssrb_conf, strlen(name) + 1, name));
    
    if(s == NULL)
        return;
    
    /*
     * We are deleting the SSRB (and namely the service-set ID), so it 
     * won't be available for lookup later. If the service set is still 
     * configured at this time, that means it will get deleted later, but the 
     * SSRB delete came first. So we store the id in the configured service-set
     * so that it is available there later upon the delete at
     * configuration-read time.
     */

    // check if this service-set is still configured
    
    service_set = service_entry(patricia_get(&services_conf,
                                        strlen(name) + 1, name));
    if(service_set != NULL) {
        service_set->svc_set_id = s->ssrb.svc_set_id;
    }
    
    /* remove data from patricia tree */
    if (!patricia_delete(&ssrb_conf, &s->node)) {
        LOG(TRACE_LOG_ERR, "%s: patricia delete FAILED!",
            __func__);
    }
    free(s);
}


/**
 * Remove an SSRB from the configuration by id
 * 
 * @param[in] svc_set_id
 *     Service-set id
 */
void
delete_ssrb_by_id(uint16_t svc_set_id)
{
    delete_ssrb_by_name((get_ssrb_by_id(svc_set_id))->svc_set_name);
}


/**
 * Set server status
 * 
 * @param[in] svc_set_id
 *     Service-set id
 * 
 * @param[in] app_name
 *     Application name
 * 
 * @param[in] server_addr
 *     Server in the application
 * 
 * @param[in] status
 *     The updated status, according to the servers monitor  
 */
void
set_server_status(uint16_t svc_set_id,
                  char * app_name,
                  in_addr_t server_addr,
                  uint8_t status)
{
    junos_kcom_pub_ssrb_t * ssrb;
    eq_serviceset_t * service_set = NULL;
    eq_app_t * app = NULL;
    eq_server_t * server = NULL;
    
    INSIST_ERR(app_name != NULL);
    
    // get service set name
    if((ssrb = get_ssrb_by_id(svc_set_id)) == NULL) {
        return;
    }
    
    // get service set
    service_set = service_entry(patricia_get(&services_conf,
                    strlen(ssrb->svc_set_name) + 1, ssrb->svc_set_name));
    
    if(service_set == NULL || service_set->applications == NULL) { 
        return;
    }
    
    // get application
    app = app_entry(patricia_get(service_set->applications,
            strlen(app_name) + 1, app_name));
    
    if(app == NULL || app->servers == NULL) { 
        return;
    }
    
    // find matching server
    server = TAILQ_FIRST(app->servers);
    while(server != NULL) {
        if(server->server_addr == server_addr) {
            server->status = status; // update status
            return;
        }
        server = TAILQ_NEXT(server, entries);
    }
    // if here, no matching server was found
}


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
                 uint32_t session_count)
{
    junos_kcom_pub_ssrb_t * ssrb;
    eq_serviceset_t * service_set = NULL;
    eq_app_t * app = NULL;
    
    INSIST_ERR(app_name != NULL);
    
    // get service set name
    if((ssrb = get_ssrb_by_id(svc_set_id)) == NULL) {
        return;
    }
    
    // get service set
    service_set = service_entry(patricia_get(&services_conf,
                    strlen(ssrb->svc_set_name) + 1, ssrb->svc_set_name));
    
    if(service_set == NULL || service_set->applications == NULL) { 
        return;
    }
    
    // get application
    app = app_entry(patricia_get(service_set->applications,
            strlen(app_name) + 1, app_name));
    
    if(app == NULL) { 
        return; // if here, no matching app was found
    }
    
    app->session_count = session_count;
}


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
next_service_set(eq_serviceset_t * data)
{
    return service_entry(
            patricia_find_next(&services_conf, (data ? &(data->node) : NULL)));
}


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
next_application(eq_serviceset_t * ss, eq_app_t * data)
{
    return app_entry(patricia_find_next(ss->applications,
                                    (data ? &(data->node) : NULL)));
}


/**
 * Get the service-set information by name
 * 
 * @param[in] name
 *      service-set name 
 * 
 * @return The service set if one exists with the matching name, o/w NULL
 */
eq_serviceset_t *
find_service_set(const char * name)
{
    return service_entry(patricia_get(&services_conf, strlen(name) + 1, name));
}


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
find_application(const eq_serviceset_t * ss, const char * name)
{
    return app_entry(patricia_get(ss->applications, strlen(name) + 1, name));
}


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
equilibrium_config_read(int check)
{
    const char * services_config[] = {DDLNAME_SVCS, DDLNAME_SVCS_SVC_SET, NULL};
    
    ddl_handle_t * top = NULL;

    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s: Starting equilibrium "
            "configuration load", __func__);
    
    // Load the rest of the configuration under services service-set
    
    if (!dax_get_object_by_path(NULL, services_config, &top, FALSE))  {

        // service-set config does not exist
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, 
                "%s: Cleared equilibrium services configuration", __func__);

        delete_all_service_sets();
        return SUCCESS;
    }
    if (!dax_is_changed(top)) { // if not changed
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s: No change in equilibrium"
            " services configuration", __func__);
        return SUCCESS;
    }

    // services configuration has changed
    
    if(dax_walk_list(top, DAX_WALK_DELTA, parse_service_set, &check)
            != DAX_WALK_OK) {
        dax_release_object(&top);
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, "%s: walk service list failed"
            " services configuration", __func__);
        return EFAIL;
    }
    
    if(!check) {
        process_notifications();
    }
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONF, 
            "%s: Loaded equilibrium configuration", __func__);

    return SUCCESS;
}

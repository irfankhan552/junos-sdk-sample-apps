/*
 * $Id: equilibrium-mgmt_conn.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-mgmt_conn.c
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */

#include <sync/common.h>
#include <jnx/pconn.h>
#include <sync/equilibrium_ipc.h>
#include "equilibrium-mgmt_config.h"
#include "equilibrium-mgmt_conn.h"
#include "equilibrium-mgmt_logging.h"

#include EQUILIBRIUM_OUT_H

/*** Constants ***/

#define EQUILIBRIUM_MGMT_SERVER_MAX_CONN 1  ///< max # cnxs (for data)

#define RETRY_INTERVAL 5 ///< retry processing notification messages

/*** Data Structures ***/

static evContext eq_ctx;   ///< Event context for equilibrium.

static pconn_server_t     * mgmt_server;  ///< the server connection info
static pconn_session_t    * data_session; ///< the session to the data component
static pconn_peer_info_t  * data_info;    ///< info about the connection
static evTimerID  timer_id;               ///< timer for process_notifications retry

/**
 * A notification message to store in the queue of message to go out to the 
 * data component when SSRB info is ready and when the data component is 
 * connected
 */
typedef struct notification_msg_s {
    msg_type_e   action;        ///< action to send
    char *       svc_set_name;  ///< service-set name to be resolved to an ID
    uint16_t     message_len;   ///< length of message to be sent
    void *       message;       ///< data to send (depends on action)
    TAILQ_ENTRY(notification_msg_s) entries;    ///< ptrs to next/prev
} notification_msg_t;


/**
 * List of notification messages to buffer while SSRB info becomes available 
 * and while data component isn't connected
 */
static TAILQ_HEAD(notification_buffer_s, notification_msg_s) notification_msgs = 
            TAILQ_HEAD_INITIALIZER(notification_msgs);


/*** STATIC/INTERNAL Functions ***/


/**
 * Connection handler for new, dying, or failed connections
 * 
 * @param[in] session
 *      The session information for the source peer
 * 
 * @param[in] event
 *      The event (established, or shutdown are the ones we care about)
 * 
 * @param[in] cookie
 *      The cookie we passed in. This is the eventlib context here.
 */
static void
receive_connection(pconn_session_t * session,
                   pconn_event_t event,
                   void * cookie __unused)
{
    int rc;
    eq_serviceset_t * ss;
    eq_app_t * app;
    eq_server_t * server;
    
    switch (event) {

    case PCONN_EVENT_ESTABLISHED:
            
        if(data_info == NULL) {
            data_info = malloc(sizeof(pconn_peer_info_t));
            INSIST(data_info != NULL);
        } else {
            LOG(TRACE_LOG_ERR, "%s: data_info already present when receiving "
                "incoming connection", __func__);
        }
        
        rc = pconn_session_get_peer_info(session, data_info);
        
        if(rc != PCONN_OK) {
            LOG(TRACE_LOG_ERR, "%s: Cannot retrieve peer info "
                "for session. Error: %d", __func__, rc);
        } else {
            INSIST_ERR(data_info->ppi_peer_type == PCONN_PEER_TYPE_PIC);
            
            junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
                "%s: Accepted connection from ms-%d/%d/0",
                __func__, data_info->ppi_fpc_slot, data_info->ppi_pic_slot);
        }
        
        data_session = session;

        // Send all configuration to the data component, we neede this 
        // especially in the case of the data component loosing and 
        // regaining the connection (possibly from it restarting)

        // first empty out all the messages and send it a reset
        notify_delete_all();
        
        // go thru all service sets
        ss = next_service_set(NULL);
        while(ss != NULL) {
            // send all applications of this service set
            app = next_application(ss, NULL);
            while(app != NULL) {
                
                if(app->server_mon_params != NULL) {
                    notify_application_update(
                            ss->svc_set_name, app->application_name,
                            app->application_addr, app->application_port,
                            app->session_timeout,
                            app->server_mon_params->connection_interval,
                            app->server_mon_params->connection_timeout,
                            app->server_mon_params->timeouts_allowed,
                            app->server_mon_params->down_retry_interval);
                } else {
                    notify_application_update(
                            ss->svc_set_name, app->application_name,
                            app->application_addr, app->application_port,
                            app->session_timeout, 0, 0, 0, 0);
                }
                
                // send all servers config
                if(app->servers) {
                    server = TAILQ_FIRST(app->servers);
                    while(server != NULL) {
                        notify_server_update(ss->svc_set_name,
                                app->application_name, server->server_addr);
                        
                        server = TAILQ_NEXT(server, entries);
                    }
                }
                
                app = next_application(ss, app);
            }
            ss = next_service_set(ss);
        }
        process_notifications();
        
        break;

    case PCONN_EVENT_SHUTDOWN:
        
        if(session == data_session) {
            data_session = NULL;
            
            if(data_info != NULL) {
                free(data_info);
                data_info = NULL;
            }
            
            junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION, "%s: Connection"
                " from the data component was shutdown.", __func__);
                
        } else {
            LOG(TRACE_LOG_ERR, "%s: Cannot find an existing "
                "peer session to shutdown", __func__);
        }

        break;

    case PCONN_EVENT_FAILED:
        
        if(session == data_session) {
            data_session = NULL;
            
            if(data_info != NULL) {
                free(data_info);
                data_info = NULL;
            }
            
            junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION, "%s: Connection from "
                "the data component was shutdown (failure).", __func__);
        }
        
        break;
        
    default:
        
        LOG(TRACE_LOG_ERR, "%s: Received an unknown event", __func__);
        
        break;
    }
}


/**
 * Message handler for open connections. We receive all kinds of 
 * messages (msg_type_e) except MSG_PEER.
 * 
 * @param[in] session
 *      The session information for the source peer
 * 
 * @param[in] msg
 *      The inbound message
 * 
 * @param[in] cookie
 *      The cookie we passed in. This is the eventlib context here.
 * 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
static status_t
receive_message(pconn_session_t * session,
                ipc_msg_t * msg,
                void * cookie __unused)
{
    server_status_t * server_status = NULL;
    sessions_status_t * sessions_status = NULL;
    struct in_addr addr;
    
    
    INSIST_ERR(session == data_session);
    
    switch(msg->subtype) {
    
    case MSG_SERVER_UPDATE:
        
        server_status = (server_status_t *)msg->data;
        // check message length
        INSIST_ERR(msg->length == 
            sizeof(server_status_t) + ntohs(server_status->app_name_len));
        
        set_server_status(ntohs(server_status->svc_set_id),
                          server_status->app_name,
                          server_status->server_addr,
                          server_status->server_status);
        
        addr.s_addr = server_status->server_addr;
        
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION, "%s: got update that "
            "server %s is %s for app %s.", __func__, inet_ntoa(addr),
            ((server_status->server_status) ? "up" : "down"),
            server_status->app_name);
        
        break;

    case MSG_STATUS_UPDATE:
    
        sessions_status = (sessions_status_t *)msg->data;
        
        // check message length
        INSIST_ERR(msg->length == 
            sizeof(sessions_status_t) + ntohs(sessions_status->app_name_len));
        
        set_app_sessions(ntohs(sessions_status->svc_set_id),
                         sessions_status->app_name,
                         ntohl(sessions_status->active_sessions));
        
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION, "%s: got update that %d "
            "sessions are active for app %s.", __func__,
            ntohl(sessions_status->active_sessions), sessions_status->app_name);
        
        break;
        
    default:
    
        LOG(TRACE_LOG_ERR,
            "%s: Received an unknown message type (%d) from the "
            "data component", __func__, msg->subtype);
        break;
    }

    return SUCCESS;
}


/**
 * Go through the buffered messages and send them to the data component 
 * if it's connected. If it's not we recall this function in RETRY_INTERVAL
 * seconds. Also we only process messages for which we can resolve the 
 * service set ID from the name.
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
process_notifications_messages(evContext ctx __unused, void * uap  __unused,
            struct timespec due __unused, struct timespec inter __unused)
{
    
    notification_msg_t * msg;
    junos_kcom_pub_ssrb_t * ssrb;
    int rc = 0;
    
    if(data_session == NULL) { // don't bother doing anything yet
        return;
    }
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION, "%s", __func__);
    
    evClearTimer(eq_ctx, timer_id);
    evInitID(&timer_id);
    
    while((msg = TAILQ_FIRST(&notification_msgs)) != NULL) {
        
        if(msg->action == MSG_DELETE_ALL) {
            rc = pconn_server_send(data_session, msg->action, NULL, 0);
            if(rc != PCONN_OK) {
                 goto failed_send;
            }
        } else {
            
            // all other msg type need the svc_set_id
            
            switch(msg->action) {
            
            case MSG_DELETE_SS: // svc set or all apps were deleted
            {
                del_svcset_info_t * data = (del_svcset_info_t *)(msg->message);
                // already have the service-set id in this case,
                // see: notify_serviceset_delete
                
                rc = pconn_server_send(data_session, msg->action, 
                        data, msg->message_len);
                
                break;
            }
            case MSG_DELETE_APP:
            case MSG_DELETE_ALL_SERVERS:
            {
                del_app_info_t * data = (del_app_info_t *)(msg->message);
                if((ssrb = get_ssrb_by_name(msg->svc_set_name)) == NULL) {
                    goto reset_timer;
                } else {
                    data->svc_set_id = htons(ssrb->svc_set_id);                        
                }
                
                rc = pconn_server_send(data_session, msg->action, 
                        data, msg->message_len);
                
                break;
            }   
            case MSG_DELETE_SERVER:
            case MSG_CONF_SERVER:
            {
                server_info_t * data = (server_info_t *)(msg->message);
                if((ssrb = get_ssrb_by_name(msg->svc_set_name)) == NULL) {
                    goto reset_timer;
                } else {
                    data->svc_set_id = htons(ssrb->svc_set_id);                        
                }
                
                rc = pconn_server_send(data_session, msg->action, 
                        data, msg->message_len);
                
                break;
            }
            case MSG_CONF_APPLICATION:
            {   
                update_app_info_t * data = (update_app_info_t *)(msg->message);
                if((ssrb = get_ssrb_by_name(msg->svc_set_name)) == NULL) {
                    
                    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,"%s: Waiting "
                        "for SSRB: %s", __func__, msg->svc_set_name);
                    
                    goto reset_timer;
                } else {
                    data->svc_set_id = htons(ssrb->svc_set_id);                    
                }
                
                rc = pconn_server_send(data_session, msg->action, 
                        data, msg->message_len);
                
                break;
            }
            default: break; // ignore warnings for not handling all enum values
            }
            
            if(rc != PCONN_OK) {
                 goto failed_send;
            }
            
            free(msg->svc_set_name);
            free(msg->message);
        }
        
        TAILQ_REMOVE(&notification_msgs, msg, entries);
        free(msg);
        
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
            "%s: Dequeued message sent to the data component", __func__);
    }
    
    return;
    
failed_send:

    LOG(TRACE_LOG_ERR, "%s: Failed to send %d to data component. Error: %d",
        __func__, msg->action, rc);
    
reset_timer:
    
    if(evSetTimer(eq_ctx, process_notifications_messages, NULL,
            evAddTime(evNowTime(), evConsTime(RETRY_INTERVAL, 0)),
            evConsTime(RETRY_INTERVAL, 0), &timer_id)) {

        LOG(LOG_EMERG, "%s: evSetTimer() failed! Will not be "
                "able to process buffered notifications", __func__);
    }
}


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
init_server(evContext ctx)
{
    pconn_server_params_t params;
    
    eq_ctx = ctx;
    evInitID(&timer_id);
    
    // init the sessions
    data_session = NULL;
    data_info = NULL;
    
    bzero(&params, sizeof(pconn_server_params_t));
    
    // setup the server args
    params.pconn_port            = EQUILIBRIUM_PORT_NUM;
    params.pconn_max_connections = EQUILIBRIUM_MGMT_SERVER_MAX_CONN;
    params.pconn_event_handler   = receive_connection;
    
    // bind
    mgmt_server = pconn_server_create(&params, eq_ctx, receive_message, NULL);
    
    if(mgmt_server == NULL) {
        LOG(TRACE_LOG_ERR, "%s: Failed to initialize the pconn server"
            " on port %d.", __func__, EQUILIBRIUM_PORT_NUM);
        return EFAIL;
    }
    
    LOG(TRACE_LOG_INFO, "%s: Successfully initialized "
            "the pconn server on port %d.", __func__, EQUILIBRIUM_PORT_NUM);
    
    return SUCCESS;
}


/**
 * Close existing connections and shutdown server
 */
void
close_connections(void)
{
    notification_msg_t * msg;
    
    if(data_session) {
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
            "%s: Closing the session to the data component.", __func__);
        pconn_session_close(data_session);
        data_session = NULL;
        if(data_info != NULL) {
            free(data_info);
            data_info = NULL;
        }
    }
    
    if(mgmt_server) {
        junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
            "%s: Shuting down the server on port %d.",
            __func__, EQUILIBRIUM_PORT_NUM);
        pconn_server_shutdown(mgmt_server);
        mgmt_server = NULL;
    }
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
        "%s: Deleting the queue of notification messages.", __func__);
    
    while((msg = TAILQ_FIRST(&notification_msgs)) != NULL) {
        TAILQ_REMOVE(&notification_msgs, msg, entries);
        if(msg->action != MSG_DELETE_ALL) {
            free(msg->svc_set_name);
            free(msg->message);
        }
        free(msg);
    }
}


/**
 * Notification about an MS-PIC interface going down
 * 
 * @param[in] name
 *      name of interface that has gone down
 */
void
mspic_offline(const char * name)
{
    char current_int_prefix[64];
    
    if(data_info != NULL) {
    
        sprintf(current_int_prefix,
            "ms-%d/%d/0", data_info->ppi_fpc_slot, data_info->ppi_pic_slot);
        
        if(strstr(name, current_int_prefix) != NULL) {
            pconn_session_close(data_session);
            data_session = NULL;
            free(data_info);
            data_info = NULL;
            
            junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION, "%s: Connection from "
                "the data component was shutdown (forced).", __func__);
        }
    }
}


/**
 * Try to process all notification requests that have been buffered.
 * This should be called when a configuration load is complete. 
 */
void
process_notifications(void)
{
    if(!evTestID(timer_id) &&
        evSetTimer(eq_ctx, process_notifications_messages, NULL,
            evConsTime(0,0), evConsTime(RETRY_INTERVAL, 0), &timer_id)) {

        LOG(LOG_EMERG, "%s: evSetTimer() failed! Will not be "
                "able to process buffered notifications", __func__);
    }
}


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
                          uint16_t down_retry_interval)
{
    notification_msg_t * msg;
    update_app_info_t * data;
    uint16_t len;
    
    len = strlen(app_name) + 1;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);
    
    msg->action = MSG_CONF_APPLICATION;
    
    msg->svc_set_name = strdup(svc_set_name);
    INSIST(msg->svc_set_name != NULL);
    msg->message_len = sizeof(update_app_info_t) + len;
    
    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->app_addr = address;
    data->app_port = htons(port);
    data->session_timeout = htons(session_timeout);
    data->connection_interval = htons(connection_interval);
    data->connection_timeout = htons(connection_timeout);
    data->timeouts_allowed = timeouts_allowed;
    data->down_retry_interval = htons(down_retry_interval);
    data->app_name_len = htons(len);
    strcpy(data->app_name, app_name);
    
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
                     in_addr_t address)
{
    notification_msg_t * msg;
    server_info_t * data;
    uint16_t len;
    
    len = strlen(app_name) + 1;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);
    
    msg->action = MSG_CONF_SERVER;
    msg->svc_set_name = strdup(svc_set_name);
    INSIST(msg->svc_set_name != NULL);
    msg->message_len = sizeof(server_info_t) + len;
    
    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->server_addr = address;
    data->app_name_len = htons(len);
    strcpy(data->app_name, app_name);
    
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
                     in_addr_t address)
{
    notification_msg_t * msg;
    server_info_t * data;
    uint16_t len;
    
    len = strlen(app_name) + 1;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);
    
    msg->action = MSG_DELETE_SERVER;
    msg->svc_set_name = strdup(svc_set_name);
    INSIST(msg->svc_set_name != NULL);
    msg->message_len = sizeof(server_info_t) + len;
    
    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->server_addr = address;
    data->app_name_len = htons(len);
    strcpy(data->app_name, app_name);
    
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
                          const char * app_name)
{
    notification_msg_t * msg, * tmp, * tmp2;
    del_app_info_t * data;
    uint16_t len;
    uint8_t match;
    
    len = strlen(app_name) + 1;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);
    
    msg->action = MSG_DELETE_APP;
    msg->svc_set_name = strdup(svc_set_name);
    INSIST(msg->svc_set_name != NULL);
    msg->message_len = sizeof(del_app_info_t) + len;
    
    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->app_name_len = htons(len);
    strcpy(data->app_name, app_name);
    
    // Go thru messages and delete any with the same app name.
    // They are useless now, and while the data component isn't connected this 
    // prevents tons of useless messages from piling up and consuming memory
    tmp = TAILQ_FIRST(&notification_msgs);
    while(tmp != NULL) {
        
        // match the service-set name first
        if(tmp->svc_set_name != NULL &&
                strcmp(tmp->svc_set_name, svc_set_name) == 0) {
            
            // match the applicaiton name:
            match = 0;
            switch(tmp->action) {
            case MSG_DELETE_APP:
            case MSG_DELETE_ALL_SERVERS:
            {
                del_app_info_t * dat = (del_app_info_t *)tmp->message;
                if(strcmp(dat->app_name, app_name) == 0) {
                    match = 1;
                }
                break;
            }
            case MSG_CONF_SERVER:
            case MSG_DELETE_SERVER:
            {
                server_info_t * dat = (server_info_t *)tmp->message;
                if(strcmp(dat->app_name, app_name) == 0) {
                    match = 1;
                }
                break;
            }
            case MSG_CONF_APPLICATION:
            {
                update_app_info_t * dat = (update_app_info_t *)tmp->message;
                if(strcmp(dat->app_name, app_name) == 0) {
                    match = 1;
                }
                break;
            }
            default: break;
            }
            
            if(match) { // this is one with same app_name
                tmp2 = tmp;
                tmp = TAILQ_NEXT(tmp, entries);
                TAILQ_REMOVE(&notification_msgs, tmp2, entries);
                
                junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION, "%s: Cleaning up "
                        "message for the data component", __func__);
            } else {
                tmp = TAILQ_NEXT(tmp, entries);                
            }
        } else {
            tmp = TAILQ_NEXT(tmp, entries);
        }
    }
    
    // insert the new message
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
notify_serviceset_delete(const char * svc_set_name, uint16_t svc_set_id)
{
    notification_msg_t * msg, * tmp, * tmp2;
    del_svcset_info_t * data;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);
    
    msg->action = MSG_DELETE_SS;
    msg->svc_set_name = strdup(svc_set_name);
    INSIST(msg->svc_set_name != NULL);
    msg->message_len = sizeof(del_svcset_info_t);
    
    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->svc_set_id = htons(svc_set_id); // save the ID; we can't look it up later
    
    // go thru message and delete any with the same service-set name
    // becase they won't be able to get the SS ID (and they are useless now)
    tmp = TAILQ_FIRST(&notification_msgs);
    while(tmp != NULL) {
        if(tmp->svc_set_name != NULL &&
                strcmp(tmp->svc_set_name, svc_set_name) == 0) {
            tmp2 = tmp;
            tmp = TAILQ_NEXT(tmp, entries);
            TAILQ_REMOVE(&notification_msgs, tmp2, entries);
            
            junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION, "%s: Cleaning up "
                    "message for the data component", __func__);
        } else {
            tmp = TAILQ_NEXT(tmp, entries);
        }
    }
    
    // insert the new message
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
                          const char * app_name)
{
    notification_msg_t * msg;
    del_app_info_t * data;
    uint16_t len;
    
    len = strlen(app_name) + 1;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);
    
    msg->action = MSG_DELETE_ALL_SERVERS;
    msg->svc_set_name = strdup(svc_set_name);
    INSIST(msg->svc_set_name != NULL);
    msg->message_len = sizeof(del_app_info_t) + len;
    
    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->app_name_len = htons(len);
    strcpy(data->app_name, app_name);
    
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
notify_delete_all_applications(const char * svc_set_name, uint16_t svc_set_id)
{
    notification_msg_t * msg;
    del_svcset_info_t * data;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);
    
    msg->action = MSG_DELETE_SS; // just delete the service set at this point
    msg->svc_set_name = strdup(svc_set_name);
    INSIST(msg->svc_set_name != NULL);
    msg->message_len = sizeof(del_svcset_info_t);
    
    msg->message = data = calloc(1, msg->message_len);
    INSIST(msg->message != NULL);
    data->svc_set_id = htons(svc_set_id); // save the ID; we can't look it up later
    
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


/**
 * Enqueue a message to go to the data component about a deletion of all config
 */
void
notify_delete_all(void)
{
    notification_msg_t * msg;

    // override any other messages, so delete all others 
    while((msg = TAILQ_FIRST(&notification_msgs)) != NULL) {
        if(msg->action != MSG_DELETE_ALL) {
            free(msg->svc_set_name);
            free(msg->message);
        }
        TAILQ_REMOVE(&notification_msgs, msg, entries);
        free(msg);
    }
    
    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);
    
    msg->action = MSG_DELETE_ALL; 
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(EQUILIBRIUM_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}



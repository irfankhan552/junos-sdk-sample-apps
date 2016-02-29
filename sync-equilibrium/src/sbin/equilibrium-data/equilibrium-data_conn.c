/*
 * $Id: equilibrium-data_conn.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-data_conn.c
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */


#include "equilibrium-data_main.h"
#include <jnx/pconn.h>
#include "equilibrium-data_config.h"
#include "equilibrium-data_conn.h"


/*** Constants ***/

/**
 * Retry connecting to a component every RETRY_CONNECT if connection is lost
 */
#define RETRY_CONNECT 60
#define CONNECT_RETRIES    1  ///< max number of connect retries


/*** Data Structures ***/

/**
 * A notification message to store in the queue of message to go out to the 
 * mgmt component
 */
typedef struct notification_msg_s {
    msg_type_e   action;        ///< action to send
    in_addr_t    app_addr;      ///< application address
    in_addr_t    app_port;      ///< application port
    void *       message;       ///< data to send (depends on action)
    TAILQ_ENTRY(notification_msg_s) entries;    ///< ptrs to next/prev
} notification_msg_t;


/**
 * List of notification messages to buffer until main thread processes msgs
 */
static TAILQ_HEAD(notification_buffer_s, notification_msg_s) notification_msgs = 
            TAILQ_HEAD_INITIALIZER(notification_msgs);

static msp_spinlock_t msgs_lock;     ///< lock for accessing notification_msgs

static pconn_client_t * mgmt_client; ///< client cnx to mgmt component

static evTimerID  mgmt_timer_id;     ///< timerID for retrying cnx to mgmt

static evContext  main_ctx;          ///< event context for main thread


/*** STATIC/INTERNAL Functions ***/


/**
 * Fwd declaration of function to setup the connection to the mgmt component
 */
static void connect_mgmt(evContext ctx, void * uap,
    struct timespec due, struct timespec inter);


/**
 * Go through the buffered messages and send them to the mgmt component 
 * if it's connected.
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
process_notifications_messages(evContext ctx UNUSED, void * uap  UNUSED,
            struct timespec due UNUSED, struct timespec inter UNUSED)
{
    notification_msg_t * msg;
    sessions_status_t * session_status, * session_data;
    server_status_t * server_status, * server_data;
    void * data;
    char * app_name;
    int rc = 0, len;
    
    if(mgmt_client == NULL) { // don't bother doing anything yet
        return;
    }
    
    INSIST_ERR(msp_spinlock_lock(&msgs_lock) == MSP_OK);
    
    while((msg = TAILQ_FIRST(&notification_msgs)) != NULL) {
        
        TAILQ_REMOVE(&notification_msgs, msg, entries);
        
        INSIST_ERR(msp_spinlock_unlock(&msgs_lock) == MSP_OK);
        
        if(msg->action == MSG_STATUS_UPDATE) {
            session_status = (sessions_status_t *)(msg->message);

            app_name = get_app_name(
                    session_status->svc_set_id, msg->app_addr, msg->app_port);
            
            len = sizeof(sessions_status_t) + strlen(app_name) + 1;
            session_data = calloc(1, len);
            INSIST_ERR(session_data != NULL);
            
            session_data->active_sessions = session_status->active_sessions;
            session_data->svc_set_id = session_status->svc_set_id;
            session_data->app_name_len = strlen(app_name) + 1;
            strcpy(session_data->app_name, app_name);
            
            data = session_data;
            
            LOG(LOG_INFO, "%s: Sending application status update to "
                    "mgmt component.", __func__);

        } else {
            server_status = (server_status_t *)(msg->message);

            app_name = get_app_name(
                    server_status->svc_set_id, msg->app_addr, msg->app_port);
            
            len = sizeof(server_status_t) + strlen(app_name) + 1;
            server_data = calloc(1, len);
            INSIST_ERR(server_data != NULL);
            
            server_data->server_status = server_status->server_status;
            server_data->svc_set_id = server_status->svc_set_id;
            server_data->server_addr = server_status->server_addr;
            server_data->app_name_len = strlen(app_name) + 1;
            strcpy(server_data->app_name, app_name);
            
            data = server_data;
            
            LOG(LOG_INFO, "%s: Sending server status update to "
                    "mgmt component.", __func__);
        }
        
        rc = pconn_client_send(mgmt_client, msg->action, data, len);
      
        if(rc != PCONN_OK) {
            // put message back in and process soon
            
            LOG(LOG_ERR, "%s: Failed to send (%d) to mgmt component."
                    " Error: %d", __func__, msg->action, rc);

            INSIST_ERR(msp_spinlock_lock(&msgs_lock) == MSP_OK);
            
            TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
            
            INSIST_ERR(msp_spinlock_unlock(&msgs_lock) == MSP_OK);
            
            if(evSetTimer(main_ctx, process_notifications_messages, NULL,
                    evAddTime(evNowTime(), evConsTime(5, 0)),
                    evConsTime(0, 0), NULL)) {

                LOG(LOG_EMERG, "%s: evSetTimer() failed! Will not be "
                        "able to process buffered notifications", __func__);
            }
        } else {
            free(msg->message);
            free(msg);
        }
        free(data);
        
        INSIST_ERR(msp_spinlock_lock(&msgs_lock) == MSP_OK);
    }
    
    INSIST_ERR(msp_spinlock_unlock(&msgs_lock) == MSP_OK);
    
    return;
}


/**
 * Try to process all notification requests that have been buffered.
 * This should be called with a notify* call below 
 */
static void
process_notifications(void)
{
    if(evSetTimer(main_ctx, process_notifications_messages, NULL,
            evConsTime(0,0), evConsTime(0, 0), NULL)) {

        LOG(LOG_EMERG, "%s: evSetTimer() failed! Will not be "
                "able to process buffered notifications", __func__);
    }
}

/**
 * Connection handler for new and dying connections to the mgmt component
 * 
 * @param[in] client
 *      The opaque client information for the source peer
 * 
 * @param[in] event
 *      The event (established, or shutdown are the ones we care about)
 * 
 * @param[in] cookie
 *      user data passed back
 */
static void
mgmt_client_connection(pconn_client_t * client,
                      pconn_event_t event,
                      void * cookie UNUSED)
{
    INSIST_ERR(client == mgmt_client);

    switch (event) {

    case PCONN_EVENT_ESTABLISHED:
        
        // clear the retry timer
        if(evTestID(mgmt_timer_id)) {
            evClearTimer(main_ctx, mgmt_timer_id);
            evInitID(&mgmt_timer_id);
        }
        
        LOG(LOG_INFO, "%s: Connected to the equilibrium-mgmt component "
                "on the RE", __func__);
        
        break;

    case PCONN_EVENT_SHUTDOWN:
        
        if(mgmt_client != NULL) {
            clear_config();
            LOG(LOG_INFO, "%s: Disconnected from the equilibrium-mgmt component"
                    " on the RE", __func__);
        }
        
        mgmt_client = NULL; // connection will be closed
        
        // Reconnect to it if timer not going
        if(!evTestID(mgmt_timer_id) &&
           evSetTimer(main_ctx, connect_mgmt, NULL, evConsTime(0,0),
               evConsTime(RETRY_CONNECT, 0), &mgmt_timer_id)) {

            LOG(LOG_EMERG, "%s: Failed to initialize a re-connect timer to "
                "reconnect to the mgmt component", __func__);
        }
        
        break;

    case PCONN_EVENT_FAILED:

        LOG(LOG_ERR, "%s: Received a PCONN_EVENT_FAILED event", __func__);
        
        if(mgmt_client != NULL) {
            clear_config();
            LOG(LOG_INFO, "%s: Disconnected from the equilibrium-mgmt component"
                    " on the RE", __func__);
        }
        
        mgmt_client = NULL; // connection will be closed
        
        // Reconnect to it if timer not going
        if(!evTestID(mgmt_timer_id) &&
           evSetTimer(main_ctx, connect_mgmt, NULL, evConsTime(0,0),
               evConsTime(RETRY_CONNECT, 0), &mgmt_timer_id)) {

            LOG(LOG_EMERG, "%s: Failed to initialize a re-connect timer to "
                "reconnect to the mgmt component", __func__);
        }
        
        break;
        
    default:
        LOG(LOG_ERR, "%s: Received an unknown pconn event", __func__);
    }
}


/**
 * Message handler for open connection to the mgmt component
 * 
 * @param[in] session
 *      The session information for the source peer
 * 
 * @param[in] msg
 *      The inbound message
 * 
 * @param[in] cookie
 *      The cookie we passed in.
 * 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
static status_t
mgmt_client_message(pconn_client_t * session,
                   ipc_msg_t * msg,
                   void * cookie UNUSED)
{
    INSIST_ERR(session == mgmt_client);
    INSIST_ERR(msg != NULL);

    switch(msg->subtype) { // a msg_type_e
    
    case MSG_DELETE_ALL:
        
        INSIST_ERR(msg->length == 0);
        
        LOG(LOG_INFO, "%s: Received a configuration update: "
                "reset configuration", __func__);
        
        reset_configuration();
        
        break;
        
    case MSG_DELETE_SS:
    {
        del_svcset_info_t * data = (del_svcset_info_t *)msg->data;
        INSIST_ERR(msg->length == sizeof(del_svcset_info_t));
        
        LOG(LOG_INFO, "%s: Received a configuration update: delete service set",
                __func__);
        
        delete_service_set(ntohs(data->svc_set_id));
        
        break;
    }
    case MSG_DELETE_APP:
    {
        del_app_info_t * data = (del_app_info_t *)msg->data;
        INSIST_ERR(msg->length == 
            sizeof(del_app_info_t) + ntohs(data->app_name_len));
        
        LOG(LOG_INFO, "%s: Received a configuration update: delete application",
                __func__);
        
        delete_application(ntohs(data->svc_set_id), data->app_name);
        
        break;
    }
    case MSG_DELETE_SERVER:
    {
        server_info_t * data = (server_info_t *)msg->data;
        INSIST_ERR(msg->length == 
            sizeof(server_info_t) + ntohs(data->app_name_len));
        
        LOG(LOG_INFO, "%s: Received a configuration update: delete server",
                __func__);
        
        delete_server(ntohs(data->svc_set_id), data->app_name,
                data->server_addr);
        
        break;
    }   
    case MSG_DELETE_ALL_SERVERS:
    {
        del_app_info_t * data = (del_app_info_t *)msg->data;
        INSIST_ERR(msg->length == 
            sizeof(del_app_info_t) + ntohs(data->app_name_len));
        
        LOG(LOG_INFO, "%s: Received a configuration update: delete all servers",
                __func__);
        
        delete_all_servers(ntohs(data->svc_set_id), data->app_name);
        
        break;
    }   
    case MSG_CONF_APPLICATION:
    {
        update_app_info_t * data = (update_app_info_t *)msg->data;
        INSIST_ERR(msg->length == 
            sizeof(update_app_info_t) + ntohs(data->app_name_len));
        
        LOG(LOG_INFO, "%s: Received a configuration update: update application (%s)",
            __func__, data->app_name);
        
        update_application(ntohs(data->svc_set_id),
               data->app_name, data->app_addr, data->app_port,
               ntohs(data->session_timeout), ntohs(data->connection_interval),
               ntohs(data->connection_timeout), ntohs(data->timeouts_allowed),
               ntohs(data->down_retry_interval));
        
        break;
    }
    case MSG_CONF_SERVER:
    {
        server_info_t * data = (server_info_t *)msg->data;
        INSIST_ERR(msg->length == 
            sizeof(server_info_t) + ntohs(data->app_name_len));
        
        LOG(LOG_INFO, "%s: Received a configuration update: update server (%s)",
            __func__, data->app_name);
        
        add_server(ntohs(data->svc_set_id), data->app_name,
                data->server_addr);
        
        break;
    }   
    default:
        LOG(LOG_ERR, "%s: Received an unknown message type (%d) from the "
            "mgmt component", __func__, msg->subtype);
        return EFAIL;
    }
    
    return SUCCESS;
}


/**
 * Setup the connection to the mgmt component
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
connect_mgmt(evContext ctx,
            void * uap  UNUSED,
            struct timespec due UNUSED,
            struct timespec inter UNUSED)
{
    pconn_client_params_t params;
    
    bzero(&params, sizeof(pconn_client_params_t));
    
    // setup the client args
    params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_RE;
    params.pconn_port                    = EQUILIBRIUM_PORT_NUM;
    params.pconn_num_retries             = CONNECT_RETRIES;
    params.pconn_event_handler           = mgmt_client_connection;
    
    if(mgmt_client) {
        // Then this is the second time in a row this is called even though it
        // didn't fail. We haven't received an event like ESTABLISHED yet.
        pconn_client_close(mgmt_client);
    }
    
    // connect
    mgmt_client = pconn_client_connect_async(
                    &params, ctx, mgmt_client_message, NULL);
    
    if(mgmt_client == NULL) {
        LOG(LOG_ERR, "%s: Failed to initialize the pconn client connection "
            "to the mgmt component", __func__);
    }
    
    LOG(LOG_INFO, "%s: Trying to connect to the mgmt component", __func__);
}


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
init_connections(evContext ctx)
{
    mgmt_client = NULL;
    
    main_ctx = ctx;
    
    evInitID(&mgmt_timer_id);
    msp_spinlock_init(&msgs_lock);
    
    // Connect to the MGMT component
    if(evSetTimer(ctx, connect_mgmt, NULL, evNowTime(),
        evConsTime(RETRY_CONNECT, 0), &mgmt_timer_id)) {

        LOG(LOG_EMERG, "%s: Failed to initialize a connect timer to connect "
            "to the MGMT component", __func__);
        return EFAIL;
    }

    return SUCCESS;
}


/**
 * Terminate connection to the mgmt component
 */
void
close_connections(void)
{
    notification_msg_t * msg;
    
    if(mgmt_client) {
        pconn_client_close(mgmt_client);
        mgmt_client = NULL;
    }

    INSIST_ERR(msp_spinlock_lock(&msgs_lock) == MSP_OK);

    while((msg = TAILQ_FIRST(&notification_msgs)) != NULL) {
        TAILQ_REMOVE(&notification_msgs, msg, entries);
        free(msg->message);
        free(msg);
    }
    INSIST_ERR(msp_spinlock_unlock(&msgs_lock) == MSP_OK);
}


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
                     uint8_t status)
{
    notification_msg_t * msg;
    server_status_t * server_status;
    
    if(mgmt_client == NULL) { // don't bother doing anything yet
        return;
    }
    
    msg = calloc(1, sizeof(notification_msg_t));
    INSIST_ERR(msg != NULL);
    
    msg->action = MSG_SERVER_UPDATE;
    msg->app_addr = app_addr;
    msg->app_port = app_port;
    msg->message = server_status = calloc(1, sizeof(server_status_t));
    INSIST_ERR(server_status != NULL);
    
    server_status->server_addr = server_addr;
    server_status->server_status = status;
    server_status->svc_set_id = svc_set_id;
    
    INSIST_ERR(msp_spinlock_lock(&msgs_lock) == MSP_OK);
    
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
    
    INSIST_ERR(msp_spinlock_unlock(&msgs_lock) == MSP_OK);
    
    process_notifications();
}


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
                            uint32_t session_count)
{
    notification_msg_t * msg;
    sessions_status_t * status;
    
    if(mgmt_client == NULL) { // don't bother doing anything yet
        return;
    }
        
    msg = calloc(1, sizeof(notification_msg_t));
    INSIST_ERR(msg != NULL);
    
    msg->action = MSG_STATUS_UPDATE;
    msg->app_addr = app_addr;
    msg->app_port = app_port;
    msg->message = status = calloc(1, sizeof(sessions_status_t));
    INSIST_ERR(status != NULL);
    
    status->active_sessions = session_count;
    status->svc_set_id = svc_set_id;
    
    INSIST_ERR(msp_spinlock_lock(&msgs_lock) == MSP_OK);
    
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
    
    INSIST_ERR(msp_spinlock_unlock(&msgs_lock) == MSP_OK);

    process_notifications();
}


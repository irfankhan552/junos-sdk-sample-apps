/*
 * $Id: dpm-ctrl_conn.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-ctrl_conn.c
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */


#include "dpm-ctrl_main.h"
#include <jnx/pconn.h>
#include "dpm-ctrl_config.h"
#include "dpm-ctrl_conn.h"
#include <sync/dpm_ipc.h>

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
    conf_msg_type_e                 action;     ///< action to send
    subscriber_status_t             message;    ///< data to send
    TAILQ_ENTRY(notification_msg_s) entries;    ///< ptrs to next/prev
} notification_msg_t;


/**
 * List of notification messages to buffer until main thread processes msgs
 */
static TAILQ_HEAD(notification_buffer_s, notification_msg_s) notification_msgs = 
            TAILQ_HEAD_INITIALIZER(notification_msgs);

static pthread_mutex_t  msgs_lock;     ///< lock for accessing notification_msgs
static pconn_client_t * mgmt_client;   ///< client cnx to mgmt component
static evTimerID        mgmt_timer_id; ///< timerID for retrying cnx to mgmt
static evContext        main_ctx;      ///< event context for main thread

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
    int rc = 0;
    
    if(mgmt_client == NULL) { // don't bother doing anything yet
        return;
    }
    
    INSIST_ERR(pthread_mutex_lock(&msgs_lock) == 0);
    
    while((msg = TAILQ_FIRST(&notification_msgs)) != NULL) {
        
        TAILQ_REMOVE(&notification_msgs, msg, entries);
        
        INSIST_ERR(pthread_mutex_unlock(&msgs_lock) == 0);
        
        rc = pconn_client_send(mgmt_client, msg->action, &msg->message, 
                sizeof(subscriber_status_t));
      
        if(rc != PCONN_OK) {
            // put message back in and process soon
            
            LOG(LOG_ERR, "%s: Failed to send message to mgmt component."
                    " Error: %d", __func__, rc);

            INSIST_ERR(pthread_mutex_lock(&msgs_lock) == 0);
            
            TAILQ_INSERT_HEAD(&notification_msgs, msg, entries);
            
            INSIST_ERR(pthread_mutex_unlock(&msgs_lock) == 0);
            
            if(evSetTimer(main_ctx, process_notifications_messages, NULL,
                    evAddTime(evNowTime(), evConsTime(5, 0)),
                    evConsTime(0, 0), NULL)) {

                LOG(LOG_EMERG, "%s: evSetTimer() failed! Will not be "
                        "able to process buffered notifications", __func__);
            }
        } else {
            free(msg);
        }
        
        INSIST_ERR(pthread_mutex_lock(&msgs_lock) == 0);
    }
    
    INSIST_ERR(pthread_mutex_unlock(&msgs_lock) == 0);
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
        
        LOG(LOG_INFO, "%s: Connected to the dpm-mgmt component "
                "on the RE", __func__);
        
        break;

    case PCONN_EVENT_SHUTDOWN:
        
        if(mgmt_client != NULL) {
            clear_configuration();
            LOG(LOG_INFO, "%s: Disconnected from the dpm-mgmt component"
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
            clear_configuration();
            LOG(LOG_INFO, "%s: Disconnected from the dpm-mgmt component"
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
 *      The cookie we passed in
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

    switch(msg->subtype) { // a conf_msg_type_e
    
    case MSG_CONF_RESET:
        
        INSIST_ERR(msg->length == sizeof(reset_info_t));
        
        LOG(LOG_INFO, "%s: Received a configuration update: "
                "reset configuration (%d)", __func__, 
                ((reset_info_t *)msg->data)->use_classic_filters);
        
        reset_configuration(((reset_info_t *)msg->data)->use_classic_filters);
        
        break;
        
    case MSG_CONF_COMP:
        
        INSIST_ERR(msg->length == 0);
        
        LOG(LOG_INFO, "%s: Received a configuration update: "
                "configuration complete", __func__);
        
        configuration_complete();
        
        break;
        
    case MSG_POLICER:
    {
        policer_info_t * data = (policer_info_t *)msg->data;
        
        INSIST_ERR(msg->length == sizeof(policer_info_t));
        
        LOG(LOG_INFO, "%s: Received a configuration update: "
                "configure policer %s", __func__, data->name);
        
        // change to host byte order
        data->if_exceeding.burst_size_limit = 
            ntohq(data->if_exceeding.burst_size_limit);
        
        if(data->if_exceeding.bw_in_percent) {
            data->if_exceeding.bw_u.bandwidth_percent = 
                ntohl(data->if_exceeding.bw_u.bandwidth_percent);
        } else {
            data->if_exceeding.bw_u.bandwidth_limit = 
                ntohq(data->if_exceeding.bw_u.bandwidth_limit);
        }
        
        configure_policer(data);
        
        break;
    }
    case MSG_INTERFACE:
    {
        int_info_t * data = (int_info_t *)msg->data;
        INSIST_ERR(msg->length == sizeof(int_info_t));
        
        data->subunit = ntohl(data->subunit);
        data->index = ntohl(data->index);
        
        LOG(LOG_INFO, "%s: Received a configuration update: "
                "configure managed interface %s.%d", __func__,
                data->name, data->subunit);
        
        configure_managed_interface(data);
        
        break;
    }
    case MSG_SUBSCRIBER:
    {
        sub_info_t * data = (sub_info_t *)msg->data;
        INSIST_ERR(msg->length == sizeof(sub_info_t));
        
        LOG(LOG_INFO, "%s: Received a configuration update: "
                "configure subscriber %s", __func__, data->name);
        
        configure_subscriber(data);
        
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
    params.pconn_port                    = DPM_PORT_NUM;
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
    pthread_mutex_init(&msgs_lock, NULL);
    
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

    INSIST_ERR(pthread_mutex_lock(&msgs_lock) == 0);

    while((msg = TAILQ_FIRST(&notification_msgs)) != NULL) {
        TAILQ_REMOVE(&notification_msgs, msg, entries);
        free(msg);
    }
    
    INSIST_ERR(pthread_mutex_unlock(&msgs_lock) == 0);
    
    pthread_mutex_destroy(&msgs_lock);
}


/**
 * Notify the mgmt component about a subscriber status update
 * 
 * @param[in] action
 *      login/logout action
 * 
 * @param[in] mdi_mlr
 *      the subscriber name
 * 
 * @param[in] class_name
 *      the subscriber class name
 */
void
notify_status_update(status_msg_type_e action,
                     char * subscriber_name,
                     char * class_name)
{
    notification_msg_t * msg;
    
    if(mgmt_client == NULL) { // don't bother doing anything yet
        return;
    }
    
    msg = calloc(1, sizeof(notification_msg_t));
    INSIST_ERR(msg != NULL);
    
    msg->action = action;
    strcpy(msg->message.name, subscriber_name);
    strcpy(msg->message.class, class_name);

    INSIST_ERR(pthread_mutex_lock(&msgs_lock) == 0);
    
    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);
    
    INSIST_ERR(pthread_mutex_unlock(&msgs_lock) == 0);
    
    process_notifications();
}

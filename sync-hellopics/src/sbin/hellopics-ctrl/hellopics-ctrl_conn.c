/*
 * $Id: hellopics-ctrl_conn.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file hellopics-ctrl_conn.c
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */


#include <string.h>
#include <jnx/aux_types.h>
#include <jnx/trace.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jnx/pconn.h>
#include <sync/hellopics_ipc.h>
#include "hellopics-ctrl_conn.h"
#include "hellopics-ctrl_logging.h"


/*** Constants ***/
#define RETRY_CONNECT_INTERVAL 200
#define MGMT_CLIENT_CONNECT_RETRIES    3  ///< max number of connect retries
#define HELLOPICS_CTRL_SERVER_MAX_CONN 1  ///< max # cnxs


/*** Data Structures ***/

static pconn_server_t     * ctrl_server;     ///< the server connection info (to data)
static pconn_session_t    * data_session;    ///< the session to the data component
static pconn_client_t     * mgmt_client;     ///< client cnx to management component
static boolean            mgmt_connected;    ///< the mgmt_client cnx is established
static boolean            data_ready;        ///< the data component is ready
static evTimerID  ctrl_timer_id;     ///< timerID for retrying cnx to mgmt
static evContext  ev_ctx;          ///< event context for main thread


/*** STATIC/INTERNAL Functions ***/

static void
connect_ctrl_pic(evContext ctx,
            void * uap  __unused,
            struct timespec due __unused,
            struct timespec inter __unused);
/**
 * Connection handler for new and dying connections
 * 
 * @param[in] session
 *      The session information for the source peer
 * 
 * @param[in] event
 *      The event (established, or shutdown are the ones we care about)
 * 
 * @param[in] cookie
 *      user data passed back
 */
static void
client_connection(pconn_client_t * session,
                  pconn_event_t event,
                  void * cookie __unused)
{
    int rc;
    uint32_t id;

    INSIST_ERR(session == mgmt_client); // there is only one client connection

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:
        // clear the retry timer
        if(evTestID(ctrl_timer_id)) {
            evClearTimer(ev_ctx, ctrl_timer_id);
            evInitID(&ctrl_timer_id);
        }
        LOG(LOG_INFO, "%s: Connected to the management component",
                __func__);
        
        id = htonl(HELLOPICS_ID_CTRL);
        rc = pconn_client_send(mgmt_client, MSG_ID, &id, sizeof(id));
                          
        if(rc != PCONN_OK) {
            LOG(LOG_ERR, "%s: Failed to send MSG_ID to the mgmt component. "
                    "Error: %d", __func__, rc);
            break;
        }
            
        mgmt_connected = TRUE;

        // Send ready if data component is already ready too 
        if(data_ready) {
            rc = pconn_client_send(mgmt_client, MSG_READY, NULL, 0);
            if(rc != PCONN_OK) {
               LOG(LOG_ERR, "%s: Failed to send MSG_READY to the mgmt "
                   "component. Error: %d", __func__, rc);
            }
        }
        break;

        case PCONN_EVENT_SHUTDOWN:
        if(mgmt_client != NULL) {
            LOG(LOG_INFO, "%s: Disconnected from the hellopics-mgmt component"
                    " on the RE", __func__);
        }
        mgmt_client = NULL;
        // Reconnect to it if timer not going
        if(!evTestID(ctrl_timer_id) &&
           evSetTimer(ev_ctx, connect_ctrl_pic, NULL, evConsTime(0,0),
               evConsTime(RETRY_CONNECT_INTERVAL, 0), &ctrl_timer_id)) {

            LOG(LOG_EMERG, "%s: Failed to initialize a re-connect timer to "
                "reconnect to the mgmt component", __func__);
        }
            break;
    case PCONN_EVENT_FAILED:

        LOG(LOG_ERR, "%s: Received a PCONN_EVENT_FAILED event", __func__);
        if(mgmt_client != NULL) {
            LOG(LOG_INFO, "%s: Disconnected from the hellopics-mgmt component"
                    " on the RE", __func__);
        }
        mgmt_client = NULL; // connection will be closed
        // Reconnect to it if timer not going
        if(!evTestID(ctrl_timer_id) &&
           evSetTimer(ev_ctx, connect_ctrl_pic, NULL, evConsTime(0,0),
               evConsTime(RETRY_CONNECT_INTERVAL, 0), &ctrl_timer_id)) {
            LOG(LOG_EMERG, "%s: Failed to initialize a re-connect timer to "
                "reconnect to the mgmt component", __func__);
        }
        
        default:
            LOG(LOG_ERR, "%s: Received an unknown or PCONN_EVENT_FAILED event",
                 __func__);
            break;
    }
}


/**
 * Message handler for open connections. We receive all only HELLO messages. 
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
client_message(pconn_client_t * session,
               ipc_msg_t * msg,
               void * cookie __unused)
{
    int rc;
    
    INSIST_ERR(session == mgmt_client); // there is only one client connection
    INSIST_ERR(msg != NULL);

    switch(msg->subtype) {
        
        case MSG_HELLO:
            // Received a HELLO from the mgmt component, so
            // pass it along to the data component
            
            INSIST_ERR(msg->length == sizeof(uint32_t));
            LOG(LOG_INFO, "%s: Forwarding HELLO packet from mgmt to data",
                __func__);
            rc = pconn_server_send(data_session, MSG_HELLO,
                                   msg->data, sizeof(uint32_t));
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send HELLO to data component. "
                    "Error: %d", __func__, rc);
            }
            break;
        
        default:
        
            LOG(LOG_ERR, "%s: Received an unknown message type (%d) from the "
                "management component", __func__, msg->subtype);
            break;
    }

    
    return SUCCESS;
}


/**
 * Connection handler for new and dying connections
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
    pconn_peer_info_t info;
    int rc;
    
    rc = pconn_session_get_peer_info(session, &info);
    
    if(rc != PCONN_OK) {
        LOG(LOG_ERR, "%s: Cannot retrieve peer info for session. Error: %d",
             __func__, rc);
         return;
    } else {
        INSIST_ERR(info.ppi_peer_type == PCONN_PEER_TYPE_PIC);
    }

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:
            data_session = session;
            break;

        case PCONN_EVENT_SHUTDOWN:
            
            if(session == data_session) {
                data_session = NULL;
                
                LOG(LOG_INFO, "%s: Connection from the data component shutdown",
                    __func__);
                
            } else {
                LOG(LOG_ERR, "%s: Cannot find an existing peer session "
                    "to shutdown", __func__);
            }
                      
            break;

        default:
            LOG(LOG_ERR, "%s: Received an unknown or PCONN_EVENT_FAILED event",
                 __func__);
            break;
    }
}


/**
 * Message handler for open connections. We receive MSG_ID, MSG_READY, and 
 * MSG_HELLO messages (msg_type_e).
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
    int rc;
    uint32_t id;
    
    INSIST_ERR(session == data_session);

    switch(msg->subtype) {
        
        case MSG_READY:

            data_ready = TRUE;
            
            // Send ready if mgmt component is connected 
            if(mgmt_connected) {
                rc = pconn_client_send(mgmt_client, MSG_READY, NULL, 0);
                if(rc != PCONN_OK) {
                    LOG(LOG_ERR, "%s: Failed to send MSG_READY to mgmt "
                        "component. Error %d", __func__, rc);
                }
            }

            break;
            
        case MSG_HELLO:
            
            // Received a HELLO from the data component, so
            // pass it along to the mgmt component
            
            INSIST_ERR(msg->length == sizeof(uint32_t));
            LOG(LOG_INFO, "%s: Forwarding HELLO packet from data to mgmt",
                __func__);
            rc = pconn_client_send(mgmt_client, MSG_HELLO,
                                   msg->data, sizeof(uint32_t));
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send HELLO to mgmt component. "
                    "Error: %d", __func__, rc);
            }

            break;
        
        case MSG_ID:
        
            INSIST_ERR(msg->length == sizeof(uint32_t));
        
            id = ntohl(*((uint32_t *)msg->data));
            if(id != HELLOPICS_ID_DATA) {
                LOG(LOG_ERR, "%s: Received an ID (%d) that is not the "
                    "data component", __func__, id);
                break;
            }
            LOG(LOG_INFO, "%s: Received a connection from the data component",
                __func__);
        
            break;
        
        default:
        
            LOG(LOG_ERR, "%s: Received an unknown message type (%d) from the "
                "data component", __func__, msg->subtype);
            break;
    }
     
    return SUCCESS;
}

/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Create server scoket for the data component and create
 * a new connection to the mgmt server.
 * 
 * @param[in] ctx
 *      event context
 */
static void
connect_ctrl_pic(evContext ctx,
            void * uap  __unused,
            struct timespec due __unused,
            struct timespec inter __unused)
{
    pconn_server_params_t s_params;
    pconn_client_params_t c_params;
    
    // init the sessions
    data_session = NULL;
    
    // init the client in case it doesn't get init'd below
    mgmt_client = NULL;
    
    // init module's other variables
    data_ready = FALSE;
    mgmt_connected = FALSE;

    bzero(&s_params, sizeof(pconn_server_params_t));
    
    // setup the server args
    s_params.pconn_port            = HELLOPICS_PORT_NUM;
    s_params.pconn_max_connections = HELLOPICS_CTRL_SERVER_MAX_CONN;
    s_params.pconn_event_handler   = receive_connection;
    
    // bind
    ctrl_server = pconn_server_create(&s_params, ctx, receive_message, NULL);
    
    if(ctrl_server == NULL) {
        LOG(LOG_ERR, "%s: Failed to initialize the pconn server on port %d.",
            __func__, HELLOPICS_PORT_NUM);
    }
    

    bzero(&c_params, sizeof(pconn_client_params_t));
    
    // setup the client args
    c_params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_RE;
    c_params.pconn_port                    = HELLOPICS_PORT_NUM;
    c_params.pconn_num_retries             = MGMT_CLIENT_CONNECT_RETRIES;
    c_params.pconn_event_handler           = client_connection;
    
    // connect
    mgmt_client = pconn_client_connect_async(
                    &c_params, ctx, client_message, NULL);
    
    if(mgmt_client == NULL) {
        LOG(LOG_ERR, "%s: Failed to initialize the pconn client connection "
            "to the management component", __func__);
    }
}
/**
 * Initiate connections to the data component and the mgmt component
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
    
    ev_ctx = ctx;
    
    evInitID(&ctrl_timer_id);
    
    // Connect to the MGMT component
    if(evSetTimer(ctx, connect_ctrl_pic, NULL, evNowTime(),
        evConsTime(RETRY_CONNECT_INTERVAL, 0), &ctrl_timer_id)) {

        LOG(LOG_EMERG, "%s: Failed to initialize a connect timer to connect "
            "to the MGMT component", __func__);
        return EFAIL;
    }

    return SUCCESS;
}


/**
 * Terminate all the connections
 */
void
close_connections(void)
{
    if(data_session) {
        pconn_session_close(data_session);
        data_session = NULL;
    }
    
    if(ctrl_server) {
        pconn_server_shutdown(ctrl_server);
        ctrl_server = NULL;
    }

    if(mgmt_client) {
        pconn_client_close(mgmt_client);
        mgmt_client = NULL;
    }
}

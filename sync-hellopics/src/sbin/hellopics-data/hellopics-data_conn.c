/*
 * $Id: hellopics-data_conn.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file hellopics-data_conn.c
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
#include "hellopics-data_conn.h"
#include "hellopics-data_logging.h"

/*** Constants ***/
#define RETRY_CONNECT_INTERVAL 200
#define MGMT_CLIENT_CONNECT_RETRIES    3  ///< max number of connect retries
#define CTRL_CLIENT_CONNECT_RETRIES    3  ///< max number of connect retries

/*** Data Structures ***/

static pconn_client_t     * ctrl_client;     ///< client cnx to control component
static pconn_client_t     * mgmt_client;     ///< client cnx to management component
static evContext ev_ctx; ///< event context
static evTimerID  mgmt_timer_id;     ///< timerID for retrying cnx to mgmt


/*** STATIC/INTERNAL Functions ***/

static void
connect_mgmt(evContext ctx,
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
ctrl_client_connection(pconn_client_t * session,
                       pconn_event_t event,
                       void * cookie __unused)
{
    int rc;
    uint32_t id;

    INSIST_ERR(session == ctrl_client);

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:

            LOG(LOG_INFO, "%s: Connected to the control component",
                __func__);
            
            id = htonl(HELLOPICS_ID_DATA);
            
            rc = pconn_client_send(ctrl_client, MSG_ID, &id, sizeof(id));
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send MSG_ID to the control "
                    "component. Error: %d", __func__, rc);
                break;
            }

            rc = pconn_client_send(mgmt_client, MSG_READY, NULL, 0);
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send MSG_READY to the management "
                    "component. Error: %d", __func__, rc);
                break;
            }

            rc = pconn_client_send(ctrl_client, MSG_READY, NULL, 0);
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send MSG_READY to the control "
                    "component. Error: %d", __func__, rc);
                break;
            }

            break;

        case PCONN_EVENT_SHUTDOWN:
            ctrl_client = NULL;
            break;

        default:
            LOG(LOG_ERR, "%s: Received an unknown or PCONN_EVENT_FAILED event",
                 __func__);
            break;
    }
}


/**
 * Message handler for open connections. We receive only HELLO messages.
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
ctrl_client_message(pconn_client_t * session,
                    ipc_msg_t * msg,
                    void * cookie __unused)
{
    int rc;
    
    INSIST_ERR(session == ctrl_client);
    INSIST_ERR(msg != NULL);

    switch(msg->subtype) {
        
        case MSG_HELLO:
            // Received a HELLO from the ctrl component, so
            // pass it along to the mgmt component
            
            INSIST_ERR(msg->length == sizeof(uint32_t));
            LOG(LOG_INFO, "%s: Forwarding HELLO packet from ctrl to mgmt",
                __func__);
            rc = pconn_client_send(mgmt_client, MSG_HELLO,
                                   msg->data, sizeof(uint32_t));
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send HELLO to mgmt component. "
                    "Error: %d", __func__, rc);
                return EFAIL;
            }

            break;
        
        default:
        
            LOG(LOG_ERR, "%s: Received an unknown message type (%d) from the "
                "control component", __func__, msg->subtype);
            return EFAIL;
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
 *      user data passed back
 */
static void
mgmt_client_connection(pconn_client_t * session,
                       pconn_event_t event,
                       void * cookie __unused)
{
    int rc;
    uint32_t id;

    INSIST_ERR(session == mgmt_client);

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:
        // clear the retry timer
        if(evTestID(mgmt_timer_id)) {
            evClearTimer(ev_ctx, mgmt_timer_id);
            evInitID(&mgmt_timer_id);
        }
            LOG(LOG_INFO, "%s: Connected to the management component",
                __func__);
            
            id = htonl(HELLOPICS_ID_DATA);
            
            rc = pconn_client_send(mgmt_client, MSG_ID, &id, sizeof(id));
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send MSG_ID to the mgmt component. "
                    "Error: %d", __func__, rc);
                break;
            }
            
            rc = pconn_client_send(mgmt_client, MSG_GET_PEER, NULL, 0);
            
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send MSG_GET_PEER to the mgmt "
                    "component. Error: %d", __func__, rc);
                break;
            }
            
            break;

        case PCONN_EVENT_SHUTDOWN:
        if(mgmt_client != NULL) {
            LOG(LOG_INFO, "%s: Disconnected from the hellopics-mgmt component"
                    " on the RE", __func__);
        }
            mgmt_client = NULL;
        // Reconnect to it if timer not going
        if(!evTestID(mgmt_timer_id) &&
           evSetTimer(ev_ctx, connect_mgmt, NULL, evConsTime(0,0),
               evConsTime(RETRY_CONNECT_INTERVAL, 0), &mgmt_timer_id)) {

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
        if(!evTestID(mgmt_timer_id) &&
           evSetTimer(ev_ctx, connect_mgmt, NULL, evConsTime(0,0),
               evConsTime(RETRY_CONNECT_INTERVAL, 0), &mgmt_timer_id)) {

            LOG(LOG_EMERG, "%s: Failed to initialize a re-connect timer to "
                "reconnect to the mgmt component", __func__);
        }
        
        break;
        default:
            LOG(LOG_ERR, "%s: Received an unknown or PCONN_EVENT_FAILED event",
                 __func__);
            break;
    }
}


/**
 * Message handler for open connections. We receive MSG_HELLO and MSG_PEER 
 * messages (msg_type_e).
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
mgmt_client_message(pconn_client_t * session,
                    ipc_msg_t * msg,
                    void * cookie __unused)
{
    int rc;
    pconn_client_params_t c_params;
    pconn_peer_info_t * info;
    
    INSIST_ERR(session == mgmt_client);
    INSIST_ERR(msg != NULL);

    switch(msg->subtype) {
        
        case MSG_HELLO:
            // Received a HELLO from the mgmt component, so
            // pass it along to the ctrl component
            
            INSIST_ERR(msg->length == sizeof(uint32_t));
            LOG(LOG_INFO, "%s: Forwarding HELLO packet from mgmt to ctrl",
                __func__);
            rc = pconn_client_send(ctrl_client, MSG_HELLO,
                                   msg->data, sizeof(uint32_t));
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send HELLO to ctrl component. "
                    "Error: %d", __func__, rc);
                return EFAIL;
            }

            break;
        
        case MSG_PEER:
            
            LOG(LOG_INFO, "%s: Received peer info. Connecting to control "
                "component", __func__);
            
            INSIST_ERR(msg->length == sizeof(pconn_peer_info_t));
            info = (pconn_peer_info_t *)msg->data; // the ctrl component's info
            
            bzero(&c_params, sizeof(pconn_client_params_t));
            
            // setup the client args
            c_params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_PIC;
            c_params.pconn_peer_info.ppi_fpc_slot = ntohl(info->ppi_fpc_slot);
            c_params.pconn_peer_info.ppi_pic_slot = ntohl(info->ppi_pic_slot);
            
            c_params.pconn_port          = HELLOPICS_PORT_NUM;
            c_params.pconn_num_retries   = CTRL_CLIENT_CONNECT_RETRIES;
            c_params.pconn_event_handler = ctrl_client_connection;
            
            // connect

            ctrl_client = pconn_client_connect_async(&c_params, ev_ctx,
                ctrl_client_message, NULL);
            
            if(ctrl_client == NULL) {
                LOG(LOG_ERR, "%s: Failed to initialize the pconn client "
                    "connection to the control component", __func__);
                return EFAIL;
            }

            break;
        
        default:
            LOG(LOG_ERR, "%s: Received an unknown message type (%d) from the "
                "management component", __func__, msg->subtype);
            return EFAIL;
    }

    
    return SUCCESS;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Connect to the mgmt component
 * 
 * @param[in] ctx
 *      event context
 */
static void
connect_mgmt(evContext ctx,
            void * uap  __unused,
            struct timespec due __unused,
            struct timespec inter __unused)
{
    pconn_client_params_t m_params;
    
    // init this client we connect later
    ctrl_client = NULL;
    
    ev_ctx = ctx;
    
    bzero(&m_params, sizeof(pconn_client_params_t));
    
    // setup the client args
    m_params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_RE;
    m_params.pconn_port                    = HELLOPICS_PORT_NUM;
    m_params.pconn_num_retries             = MGMT_CLIENT_CONNECT_RETRIES;
    m_params.pconn_event_handler           = mgmt_client_connection;
    
    // connect
    mgmt_client = pconn_client_connect_async(
                    &m_params, ctx, mgmt_client_message, NULL);
    
    if(mgmt_client == NULL) {
        LOG(LOG_ERR, "%s: Failed to initialize the pconn client connection "
            "to the management component", __func__);
    }
}
/**
 * Initiate connection establishement
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
    
    evInitID(&mgmt_timer_id);
    
    // Connect to the MGMT component
    if(evSetTimer(ctx, connect_mgmt, NULL, evNowTime(),
        evConsTime(RETRY_CONNECT_INTERVAL, 0), &mgmt_timer_id)) {

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
    if(ctrl_client) {
        pconn_client_close(ctrl_client);
        ctrl_client = NULL;
    }

    if(mgmt_client) {
        pconn_client_close(mgmt_client);
        mgmt_client = NULL;
    }
}

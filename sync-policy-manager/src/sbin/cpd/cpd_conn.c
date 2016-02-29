/*
 * $Id: cpd_conn.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file cpd_conn.c
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
#include <sync/policy_ipc.h>
#include "cpd_conn.h"
#include "cpd_config.h"
#include "cpd_logging.h"
#include "cpd_http.h"
#include "cpd_kcom.h"


/*** Constants ***/

/**
 * @brief initialize the evTimerID
 */
#ifndef evInitID
#define evInitID(id) ((id)->opaque = NULL)
#endif

/**
 * @brief Test if the evTimerID has been initialized
 */
#ifndef evTestID
#define evTestID(id) ((id).opaque != NULL)
#endif


#define PED_CLIENT_CONNECT_RETRIES  1  ///< max number of connect retries
#define CPD_CPD_SERVER_MAX_CONN     1  ///< max # cnxs

/**
 * Retry connecting to a component every RETRY_CONNECT if connection is lost
 */
#define RETRY_CONNECT 60

/*** Data Structures ***/

static pconn_server_t  * cpd_server;  ///< the server connection info (to data)
static pconn_session_t * pfd_session; ///< the session to the data component
static pconn_client_t  * ped_client;  ///< client cnx to management component
static evTimerID       ped_timer_id;  ///< timer ID for retrying PED connection
static evContext       main_ctx;      ///< event context for main thread

/*** STATIC/INTERNAL Functions ***/

/** Forward declaration **/ 
static void connect_ped(evContext ctx, void * uap,
    struct timespec due, struct timespec inter);


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
                  void * cookie UNUSED)
{
    int rc;
    uint32_t id;

    INSIST_ERR(session == ped_client); // there is only one client connection

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:
            
            // clear the retry timer
            if(evTestID(ped_timer_id)) {
                evClearTimer(main_ctx, ped_timer_id);
                evInitID(&ped_timer_id);
            }
        
            id = htonl(ID_CPD);
            rc = pconn_client_send(ped_client, MSG_ID, &id, sizeof(id));
            
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send MSG_ID to the PED. "
                    "Error: %d", __func__, rc);
                break;
            }
            
            break;

        case PCONN_EVENT_SHUTDOWN:
            ped_client = NULL;

            // Reconnect to the PED if timer not going
            if(!evTestID(ped_timer_id) &&
                evSetTimer(main_ctx, connect_ped, NULL, evNowTime(),
                    evConsTime(RETRY_CONNECT, 0), &ped_timer_id)) {

                LOG(LOG_ERR, "%s: Failed to initialize a re-connect timer to "
                    "reconnect to the PED", __func__);
            }
            
            break;

        default:
            LOG(LOG_ERR, "%s: Received an unknown or PCONN_EVENT_FAILED event",
                 __func__);
            
            ped_client = NULL;

            // Reconnect to the PED if timer not going
            if(!evTestID(ped_timer_id) &&
                evSetTimer(main_ctx, connect_ped, NULL, evNowTime(),
                    evConsTime(RETRY_CONNECT, 0), &ped_timer_id)) {

                LOG(LOG_ERR, "%s: Failed to initialize a re-connect timer to "
                    "reconnect to the PED", __func__);
            }
            
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
 *      The cookie we passed in.
 * 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
static status_t
client_message(pconn_client_t * session,
               ipc_msg_t * msg,
               void * cookie UNUSED)
{
    struct in_addr tmp;
    in_addr_t * addr;
    in_addr_t old_cpd_addr;
    
    INSIST_ERR(session == ped_client); // there is only one client connection
    INSIST_ERR(msg != NULL);

    switch(msg->subtype) {
        
        case MSG_ADDRESSES:
            
            INSIST_ERR(msg->length == sizeof(in_addr_t) * 2);
            
            old_cpd_addr = get_cpd_address();
            
            addr = (in_addr_t *)msg->data;
            set_cpd_address(*addr);
            ++addr;
            set_pfd_address(*addr);
            
            if(old_cpd_addr != get_cpd_address()) {
                tmp.s_addr = get_cpd_address();
                LOG(LOG_INFO, "%s: Receiving new HTTP server address: %s",
                        __func__, inet_ntoa(tmp));
                
                // if the address is configured on the PIC side
                // we start the server
                check_address_status();
            }
            
            break;
        
        default:
        
            LOG(LOG_ERR, "%s: Received an unknown message type (%d) from the "
                "PED", __func__, msg->subtype);
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
 *      The cookie we passed in.
 */
static void
receive_connection(pconn_session_t * session,
                   pconn_event_t event,
                   void * cookie UNUSED)
{
    pconn_peer_info_t info;
    int rc;
    
    rc = pconn_session_get_peer_info(session, &info);
    
    if(rc != PCONN_OK) {
        LOG(LOG_ERR, "%s: Cannot retrieve peer info for session. Error: %d",
             __func__, rc);
    } else {
        INSIST_ERR(info.ppi_peer_type == PCONN_PEER_TYPE_PIC);
    }

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:
            pfd_session = session;
            break;

        case PCONN_EVENT_SHUTDOWN:
            
            if(session == pfd_session) {
                pfd_session = NULL;
            } else {
                LOG(LOG_ERR, "%s: Cannot find an existing peer session "
                    "to shutdown", __func__);
            }
            
            // pconn_session_close() is called internally
            break;

        default:
            LOG(LOG_ERR, "%s: Received an unknown or PCONN_EVENT_FAILED event",
                 __func__);
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
 *      The cookie we passed in.
 * 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
static status_t
receive_message(pconn_session_t * session,
                ipc_msg_t * msg,
                void * cookie UNUSED)
{
    int rc;
    in_addr_t addr;
    cpd_auth_user_t * user = NULL;
    
    INSIST_ERR(session == pfd_session);

    switch(msg->subtype) {
        
        case MSG_GET_AUTH_LIST:

            while((user = get_next_auth_user(user)) != NULL) {
                
                addr = htonl(user->address);
                rc = pconn_server_send(session,
                        MSG_AUTH_ENTRY_ADD, &addr, sizeof(addr));
                
                if(rc != PCONN_OK) {
                    LOG(LOG_ERR, "%s: Failed to send MSG_AUTH_ENTRY_ADD "
                        "to the PFD. Error: %d", __func__, rc);
                    break;
                }
            }
            
            break;
            
        default:
        
            LOG(LOG_ERR, "%s: Received an unknown message type (%d) from the "
                "PFD", __func__, msg->subtype);
            break;
    }
     
    return SUCCESS;
}


/**
 * Setup the connection to the PED
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
connect_ped(evContext ctx,
            void * uap  UNUSED,
            struct timespec due UNUSED,
            struct timespec inter UNUSED)
{
    pconn_client_params_t params;
    
    bzero(&params, sizeof(pconn_client_params_t));
    
    // setup the client args
    params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_RE;
    params.pconn_port                    = PED_PORT_NUM;
    params.pconn_num_retries             = PED_CLIENT_CONNECT_RETRIES;
    params.pconn_event_handler           = client_connection;
    
    if(ped_client) {
        // Then this is the second time in a row this is called even though it
        // didn't fail. We haven't received an event like ESTABLISHED yet.
        pconn_client_close(ped_client);
    }
    
    // connect
    ped_client = pconn_client_connect_async(&params, ctx, client_message, NULL);
    
    if(ped_client == NULL) {
        LOG(LOG_ERR, "%s: Failed to initialize the pconn client connection "
            "to the PED", __func__);
    }
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the server socket connection and client
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
    pconn_server_params_t params;
    
    main_ctx = ctx;
    
    // init the sessions
    pfd_session = NULL;
    
    // init the client in case it doesn't get init'd below
    ped_client = NULL;
    
    bzero(&params, sizeof(pconn_server_params_t));
    
    // setup the server args
    params.pconn_port            = CPD_PORT_NUM;
    params.pconn_max_connections = CPD_CPD_SERVER_MAX_CONN;
    params.pconn_event_handler   = receive_connection;
    
    // bind
    cpd_server = pconn_server_create(&params, ctx, receive_message, NULL);
    
    if(cpd_server == NULL) {
        LOG(LOG_ERR, "%s: Failed to initialize the pconn server on port %d.",
            __func__, CPD_PORT_NUM);
        return EFAIL;
    }
    
    evInitID(&ped_timer_id);
    
    // Connect to the PED
    if(evSetTimer(ctx, connect_ped, NULL, evNowTime(),
        evConsTime(RETRY_CONNECT, 0), &ped_timer_id)) {

        LOG(LOG_ERR, "%s: Failed to initialize a connect timer to connect "
            "to the PED", __func__);
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
    if(pfd_session) {
        pconn_session_close(pfd_session);
        pfd_session = NULL;
    }
    
    if(cpd_server) {
        pconn_server_shutdown(cpd_server);
        cpd_server = NULL;
    }

    if(ped_client) {
        pconn_client_close(ped_client);
        ped_client = NULL;
    }
}


/**
 * Send the PFD the new authorized user
 *  
 * @param[in] addr
 *      The user's IP address in network byte order
 */
void
send_authorized_user(in_addr_t addr)
{
    int rc;
    
    if(!pfd_session) return;

    rc = pconn_server_send(
            pfd_session, MSG_AUTH_ENTRY_ADD, &addr, sizeof(addr));
    
    if(rc != PCONN_OK) {
        LOG(LOG_ERR, "%s: Failed to send MSG_AUTH_ENTRY_ADD "
            "to the PFD. Error: %d", __func__, rc);
    }
}


/**
 * Send the PFD the new authorized user
 *  
 * @param[in] addr
 *      The user's IP address in network byte order
 */
void
send_repudiated_user(in_addr_t addr)
{
    int rc;
    
    if(!pfd_session) return;

    rc = pconn_server_send(
            pfd_session, MSG_AUTH_ENTRY_DEL, &addr, sizeof(addr));
    
    if(rc != PCONN_OK) {
        LOG(LOG_ERR, "%s: Failed to send MSG_AUTH_ENTRY_DEL "
            "to the PFD. Error: %d", __func__, rc);
    }
}

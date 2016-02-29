/*
 * $Id: pfd_conn.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file pfd_conn.c
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */


#include <string.h>
#include <pthread.h>
#include <jnx/aux_types.h>
#include <jnx/trace.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jnx/pconn.h>
#include <sync/policy_ipc.h>
#include "pfd_conn.h"
#include "pfd_kcom.h"
#include "pfd_packet.h"
#include "pfd_main.h"
#include "pfd_logging.h"
#include "pfd_config.h"


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


#define PED_CLIENT_CONNECT_RETRIES    1  ///< max number of connect retries
#define CPD_CLIENT_CONNECT_RETRIES    1  ///< max number of connect retries

/**
 * Retry connecting to a component every RETRY_CONNECT if connection is lost
 */
#define RETRY_CONNECT 60

/*** Data Structures ***/

boolean            cpd_ready;     ///< cnx to CPD is established

static pconn_client_t     * ped_client;  ///< client cnx to management component
static pconn_client_t     * cpd_client;  ///< client cnx to control component


static evTimerID  ped_timer_id;    ///< timer ID for retrying connection to PED
static evTimerID  cpd_timer_id;    ///< timer ID for retrying connection to CPD

static pconn_peer_info_t cpd_info; ///< current CPD connection info
static evContext         main_ctx; ///< event context for main thread


/**
 * Update messages for threads' config. Defined in the packet module
 */
extern thread_message_t update_messages[MAX_CPUS];


/*** STATIC/INTERNAL Functions ***/


/** Forward declarations **/

/**
 * Fwd declaration of function to setup the connection to the PED
 */
static void connect_ped(evContext ctx, void * uap,
    struct timespec due, struct timespec inter);

/**
 * Fwd declaration of function to setup the connection to the CPD
 */
static void connect_cpd(evContext ctx, void * uap,
    struct timespec due, struct timespec inter);


/**
 * Connection handler for new and dying connections to CPD
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
cpd_client_connection(pconn_client_t * session,
                      pconn_event_t event,
                      void * cookie UNUSED)
{
    int rc;

    INSIST_ERR(session == cpd_client);

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:
            
            // clear the retry timer
            if(evTestID(cpd_timer_id)) {
                evClearTimer(main_ctx, cpd_timer_id);
                evInitID(&cpd_timer_id);
            }            
        
            rc = pconn_client_send(cpd_client, MSG_GET_AUTH_LIST, NULL, 0);
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send MSG_GET_AUTH_LIST to the CPD."
                    " Error: %d", __func__, rc);
                break;
            }
            
            cpd_ready = TRUE;
            if(get_cpd_address() && get_pfd_address() && vrf_ready) {
                init_packet_loops();
            }

            break;

        case PCONN_EVENT_SHUTDOWN:

            cpd_client = NULL;
            cpd_ready = FALSE;
            clear_config();
            
            // Reconnect to the CPD if timer not going
            if(!evTestID(cpd_timer_id) &&
               evSetTimer(main_ctx, connect_cpd, NULL,
                   evNowTime(), evConsTime(RETRY_CONNECT, 0), &cpd_timer_id)) {
                
                LOG(LOG_ALERT, "%s: Failed to initialize a connect timer to "
                    "connect to the CPD", __func__);
                pfd_shutdown();
            }
            
            break;

        default:
            LOG(LOG_ERR, "%s: Received an unknown or PCONN_EVENT_FAILED event",
                 __func__);
            
            cpd_client = NULL;
            cpd_ready = FALSE;
            clear_config();
            
            // Reconnect to the CPD if timer not going
            if(!evTestID(cpd_timer_id) &&
               evSetTimer(main_ctx, connect_cpd, NULL,
                   evNowTime(), evConsTime(RETRY_CONNECT, 0), &cpd_timer_id)) {
                
                LOG(LOG_ALERT, "%s: Failed to initialize a connect timer to "
                    "connect to the CPD", __func__);
                pfd_shutdown();
            }
            
            break;
    }
}


/**
 * Message handler for open connection to CPD. We receive MSG_AUTH_ENTRY_ADD 
 * and MSG_AUTH_ENTRY_DEL messages.
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
cpd_client_message(pconn_client_t * session,
                   ipc_msg_t * msg,
                   void * cookie UNUSED)
{
    INSIST_ERR(session == cpd_client);
    INSIST_ERR(msg != NULL);

    switch(msg->subtype) {
        
        case MSG_AUTH_ENTRY_ADD:
        
            INSIST_ERR(msg->length == sizeof(in_addr_t));
            
            // Don't put it in host byte-order form
            add_auth_user_addr(*(in_addr_t *)msg->data);
            
            break;

        case MSG_AUTH_ENTRY_DEL:
            
            INSIST_ERR(msg->length == sizeof(in_addr_t));
            
            // Don't put it in host byte-order form
            delete_auth_user_addr(*(in_addr_t *)msg->data);            
            
            break;
            
        default:
        
            LOG(LOG_ERR, "%s: Received an unknown message type (%d) from the "
                "CPD", __func__, msg->subtype);
            return EFAIL;
    }
    
    return SUCCESS;
}


/**
 * Connection handler for new and dying connections to the PED
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
ped_client_connection(pconn_client_t * session,
                      pconn_event_t event,
                      void * cookie UNUSED)
{
    int rc;
    component_id_e id;

    INSIST_ERR(session == ped_client);

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:
            
            // clear the retry timer
            if(evTestID(ped_timer_id)) {
                evClearTimer(main_ctx, ped_timer_id);
                evInitID(&ped_timer_id);
            }
            
            id = htonl(ID_PFD);
            
            rc = pconn_client_send(ped_client, MSG_ID, &id, sizeof(id));
            if(rc != PCONN_OK) {
                LOG(LOG_ERR, "%s: Failed to send MSG_ID to the PED. Error: %d",
                    __func__, rc);
                break;
            }
            
            break;

        case PCONN_EVENT_SHUTDOWN:
            
            ped_client = NULL; // connection will be closed
            
            // Reconnect to the PED if timer not going
            if(!evTestID(ped_timer_id) &&
               evSetTimer(main_ctx, connect_ped, NULL, evNowTime(),
                   evConsTime(RETRY_CONNECT, 0), &ped_timer_id)) {

                LOG(LOG_ERR, "%s: Failed to initialize a re-connect timer to "
                    "reconnect to the PED", __func__);
                pfd_shutdown();
            }
            
            break;

        default:
            LOG(LOG_ERR, "%s: Received an unknown or PCONN_EVENT_FAILED event",
                 __func__);
            
            ped_client = NULL; // connection will be closed
            
            // Reconnect to the PED if timer not going
            if(!evTestID(ped_timer_id) &&
               evSetTimer(main_ctx, connect_ped, NULL, evNowTime(),
                   evConsTime(RETRY_CONNECT, 0), &ped_timer_id)) {

                LOG(LOG_ERR, "%s: Failed to initialize a re-connect timer to "
                    "reconnect to the PED", __func__);
                pfd_shutdown();
            }
            
            break;
    }
}


/**
 * Message handler for open connection to the PED. We receive MSG_ADDRESSES, 
 * and MSG_PEER messages.
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
ped_client_message(pconn_client_t * session,
                   ipc_msg_t * msg,
                   void * cookie UNUSED)
{
    pconn_peer_info_t * info;
    in_addr_t * addr;
    struct in_addr cpd_addr, pfd_addr;
    int i;
    
    INSIST_ERR(session == ped_client);
    INSIST_ERR(msg != NULL);

    switch(msg->subtype) {
        
        case MSG_ADDRESSES:
            
            INSIST_ERR(msg->length == sizeof(in_addr_t) * 2);
            addr = (in_addr_t *)msg->data;
            cpd_addr.s_addr = *addr;
            ++addr;
            pfd_addr.s_addr = *addr;

            LOG(LOG_INFO, "%s: Received PFD and CPD addresses.", __func__);

            LOG(LOG_INFO, "%s: CPD address: %s",
                __func__, inet_ntoa(cpd_addr));
            
            LOG(LOG_INFO, "%s: PFD address: %s",
                __func__, inet_ntoa(pfd_addr));
            
            set_cpd_address(cpd_addr.s_addr);
            set_pfd_address(pfd_addr.s_addr);

            // Tell all threads to load new CPD and PFD address
            
            for(i = 0; i < MAX_CPUS; ++i) {
                pthread_mutex_lock(&update_messages[i].lock);
                update_messages[i].update = TRUE;
                update_messages[i].addresses.pfd_addr = pfd_addr.s_addr;
                update_messages[i].addresses.cpd_addr = cpd_addr.s_addr;
                pthread_mutex_unlock(&update_messages[i].lock);
            }
            
            if(cpd_ready && cpd_addr.s_addr && pfd_addr.s_addr && vrf_ready) {
                init_packet_loops();
            }

            break;
        
        case MSG_PEER:
            
            INSIST_ERR(msg->length == sizeof(pconn_peer_info_t));
            info = (pconn_peer_info_t *)msg->data; // the CPD's peer info
            
            if(cpd_client == NULL) {
                
                // save this info
                cpd_info.ppi_fpc_slot = ntohl(info->ppi_fpc_slot);
                cpd_info.ppi_pic_slot = ntohl(info->ppi_pic_slot);
                
                // Connect to the CPD
                if(evTestID(cpd_timer_id)) {
                    evClearTimer(main_ctx, cpd_timer_id);
                    evInitID(&cpd_timer_id);
                }
                
                if(evSetTimer(main_ctx, connect_cpd, NULL,
                        evNowTime(), evConsTime(RETRY_CONNECT, 0),
                        &cpd_timer_id)) {
                    
                    LOG(LOG_ERR, "%s: Failed to initialize a connect timer to "
                        "connect to the CPD", __func__);
                    pfd_shutdown();
                }
            } else if(cpd_info.ppi_fpc_slot != ntohl(info->ppi_fpc_slot) || 
                cpd_info.ppi_pic_slot != ntohl(info->ppi_pic_slot)) {

                /*
                 * Check that it's the same as where we're already connected
                 * this would only happen if the PED went down, but the CPD
                 * did not
                 * 
                 * ** Don't think this should happen in practise **
                 */
                
                LOG(LOG_WARNING, "%s: Connected to CPD on wrong PIC. "
                    "Reconnecting with info sent by the PED", __func__);
                
                pconn_client_close(cpd_client); // disconnect from CPD
                cpd_client = NULL;
                
                // save this info
                cpd_info.ppi_fpc_slot = ntohl(info->ppi_fpc_slot);
                cpd_info.ppi_pic_slot = ntohl(info->ppi_pic_slot);
                
                // Reconnect to the CPD
                if(evTestID(cpd_timer_id)) {
                    evClearTimer(main_ctx, cpd_timer_id);
                    evInitID(&cpd_timer_id);
                }
                
                if(evSetTimer(main_ctx, connect_cpd, NULL,
                    evNowTime(), evConsTime(RETRY_CONNECT, 0), &cpd_timer_id)) {
                    
                    LOG(LOG_ERR, "%s: Failed to initialize a connect timer"
                        " to connect to the CPD", __func__);
                    pfd_shutdown();
                }
            }
            // else we're already connected to the CPD correctly
            
            break;
        
        default:
            LOG(LOG_ERR, "%s: Received an unknown message type (%d) from the "
                "PED", __func__, msg->subtype);
            return EFAIL;
    }
    
    return SUCCESS;
}


/**
 * Setup the connection to the CPD
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
connect_cpd(evContext ctx,
            void * uap  UNUSED,
            struct timespec due UNUSED,
            struct timespec inter UNUSED)
{
    pconn_client_params_t params;
    
    bzero(&params, sizeof(pconn_client_params_t));
    
    // setup the client args
    params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_PIC;
    params.pconn_peer_info.ppi_fpc_slot = cpd_info.ppi_fpc_slot;
    params.pconn_peer_info.ppi_pic_slot = cpd_info.ppi_pic_slot;
    
    params.pconn_port          = CPD_PORT_NUM;
    params.pconn_num_retries   = CPD_CLIENT_CONNECT_RETRIES;
    params.pconn_event_handler = cpd_client_connection;
    
    if(cpd_client) {
        // Then this is the second time in a row this is called even though it
        // didn't fail. We haven't received an event like ESTABLISHED yet.
        pconn_client_close(cpd_client);
    }
    
    // connect
    cpd_client = pconn_client_connect_async(
                    &params, ctx, cpd_client_message, NULL);
    
    if(cpd_client == NULL) {
        LOG(LOG_ERR, "%s: Failed to initialize the pconn client "
            "connection to the CPD", __func__);
        return;
    }
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
    params.pconn_event_handler           = ped_client_connection;
    
    if(ped_client) {
        // Then this is the second time in a row this is called even though it
        // didn't fail. We haven't received an event like ESTABLISHED yet.
        pconn_client_close(ped_client);
    }
    
    // connect
    ped_client = pconn_client_connect_async(
                    &params, ctx, ped_client_message, NULL);
    
    if(ped_client == NULL) {
        LOG(LOG_ERR, "%s: Failed to initialize the pconn client connection "
            "to the PED", __func__);
    }
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the connections
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
    main_ctx = ctx;
    
    cpd_ready = FALSE;
    cpd_client = NULL;
    ped_client = NULL;
    
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
    if(cpd_client) {
        pconn_client_close(cpd_client);
        cpd_client = NULL;
    }

    if(ped_client) {
        pconn_client_close(ped_client);
        ped_client = NULL;
    }
}

/*
 * $Id: dpm-mgmt_conn.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-mgmt_conn.c
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */

#include <sync/common.h>
#include <sync/dpm_ipc.h>
#include <jnx/pconn.h>
#include "dpm-mgmt_config.h"
#include "dpm-mgmt_conn.h"
#include "dpm-mgmt_logging.h"

#include DPM_OUT_H

/*** Constants ***/

#define DPM_MGMT_SERVER_MAX_CONN 1  ///< max # cnxs (for ctrl)

/*** Data Structures ***/

static evContext m_ctx;   ///< Event context for dpm

static pconn_server_t     * mgmt_server;  ///< the server connection info
static pconn_session_t    * ctrl_session; ///< the session to the ctrl component
static pconn_peer_info_t  * ctrl_info;    ///< info about the connection


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
    
    switch (event) {

    case PCONN_EVENT_ESTABLISHED:
            
        if(ctrl_info == NULL) {
            ctrl_info = malloc(sizeof(pconn_peer_info_t));
            INSIST(ctrl_info != NULL);
        } else {
            LOG(TRACE_LOG_ERR, "%s: ctrl_info already present when receiving "
                "incoming connection", __func__);
        }
        
        rc = pconn_session_get_peer_info(session, ctrl_info);
        
        if(rc != PCONN_OK) {
            LOG(TRACE_LOG_ERR, "%s: Cannot retrieve peer info "
                "for session. Error: %d", __func__, rc);
        } else {
            INSIST_ERR(ctrl_info->ppi_peer_type == PCONN_PEER_TYPE_PIC);
            
            junos_trace(DPM_TRACEFLAG_CONNECTION,
                "%s: Accepted connection from ms-%d/%d/0",
                __func__, ctrl_info->ppi_fpc_slot, ctrl_info->ppi_pic_slot);
        }
        
        ctrl_session = session;
        
        // send it all out to the ctrl component
        send_configuration();
        
        break;

    case PCONN_EVENT_SHUTDOWN:
        
        if(session == ctrl_session) {
            ctrl_session = NULL;
            
            if(ctrl_info != NULL) {
                free(ctrl_info);
                ctrl_info = NULL;
            }
            
            junos_trace(DPM_TRACEFLAG_CONNECTION, "%s: Connection"
                " from the ctrl component was shutdown.", __func__);
                
        } else {
            LOG(TRACE_LOG_ERR, "%s: Cannot find an existing "
                "peer session to shutdown", __func__);
        }

        break;

    case PCONN_EVENT_FAILED:
        
        if(session == ctrl_session) {
            ctrl_session = NULL;
            
            if(ctrl_info != NULL) {
                free(ctrl_info);
                ctrl_info = NULL;
            }
            
            junos_trace(DPM_TRACEFLAG_CONNECTION, "%s: Connection from "
                "the ctrl component was shutdown (due to a failure).",
                __func__);
        }
        
        break;
        
    default:
        
        LOG(TRACE_LOG_ERR, "%s: Received an unknown event", __func__);
        
        break;
    }
}


/**
 * Message handler for open connections to the ctrl component.
 * Called when we receive a message.
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
    subscriber_status_t * data;
    
    INSIST_ERR(session == ctrl_session);
    
    data = (subscriber_status_t *)msg->data;
    INSIST_ERR(msg->length == sizeof(subscriber_status_t)); // check msg len
    
    switch(msg->subtype) {

    case MSG_SUBSCRIBER_LOGIN:

        subscriber_login(data->name, data->class);
        
        break;
        
    case MSG_SUBSCRIBER_LOGOUT:
        
        subscriber_logout(data->name, data->class);
        
        break;
        
    default:
    
        LOG(TRACE_LOG_ERR,
            "%s: Received an unknown message type (%d) from the "
            "ctrl component", __func__, msg->subtype);
        break;
    }

    return SUCCESS;
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
init_conn_server(evContext ctx)
{
    pconn_server_params_t params;
    
    m_ctx = ctx;
    
    // init the sessions
    ctrl_session = NULL;
    ctrl_info = NULL;
    
    bzero(&params, sizeof(pconn_server_params_t));
    
    // setup the server args
    params.pconn_port            = DPM_PORT_NUM;
    params.pconn_max_connections = DPM_MGMT_SERVER_MAX_CONN;
    params.pconn_event_handler   = receive_connection;
    
    // bind
    mgmt_server = pconn_server_create(&params, m_ctx, receive_message, NULL);
    
    if(mgmt_server == NULL) {
        LOG(TRACE_LOG_ERR, "%s: Failed to initialize the pconn server"
            " on port %d.", __func__, DPM_PORT_NUM);
        return EFAIL;
    }
    
    LOG(TRACE_LOG_INFO, "%s: Successfully initialized "
            "the pconn server on port %d.", __func__, DPM_PORT_NUM);
    
    return SUCCESS;
}


/**
 * Close existing connections and shutdown server
 */
void
shutdown_conn_server(void)
{
    if(ctrl_session) {
        junos_trace(DPM_TRACEFLAG_CONNECTION,
            "%s: Closing the session to the ctrl component.", __func__);
        pconn_session_close(ctrl_session);
        ctrl_session = NULL;
        if(ctrl_info != NULL) {
            free(ctrl_info);
            ctrl_info = NULL;
        }
    }
    
    if(mgmt_server) {
        junos_trace(DPM_TRACEFLAG_CONNECTION,
            "%s: Shuting down the server on port %d.",
            __func__, DPM_PORT_NUM);
        pconn_server_shutdown(mgmt_server);
        mgmt_server = NULL;
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
    
    if(ctrl_info != NULL) {
    
        sprintf(current_int_prefix,
            "ms-%d/%d/0", ctrl_info->ppi_fpc_slot, ctrl_info->ppi_pic_slot);
        
        if(strstr(name, current_int_prefix) != NULL) {
            pconn_session_close(ctrl_session);
            ctrl_session = NULL;
            free(ctrl_info);
            ctrl_info = NULL;
            
            junos_trace(DPM_TRACEFLAG_CONNECTION, "%s: Connection from "
                "the ctrl component was shutdown (forced by %s down).",
                __func__, current_int_prefix);
            
            clear_logins();
        }
    }
}


/**
 * Send a message to the ctrl component about a configuration reset
 * 
 * @param[in] use_classic
 *      Use classic filters
 */
void
notify_configuration_reset(boolean use_classic)
{
    reset_info_t data;
    int rc;
    
    if(ctrl_session == NULL)
        return;
    
    junos_trace(DPM_TRACEFLAG_CONNECTION, "%s", __func__);

    data.use_classic_filters = use_classic;

    rc = pconn_server_send(ctrl_session, MSG_CONF_RESET, &data, sizeof(data));
    if(rc != PCONN_OK) {
         LOG(TRACE_LOG_ERR, "%s: sending msg to ctrl component failed (%s)",
                 __func__);
    }
}


/**
 * Send a message to the ctrl component that configuration is complete 
 */
void
notify_configuration_complete(void)
{
    int rc;
    
    if(ctrl_session == NULL)
        return;
    
    junos_trace(DPM_TRACEFLAG_CONNECTION, "%s", __func__);

    rc = pconn_server_send(ctrl_session, MSG_CONF_COMP, NULL, 0);
    if(rc != PCONN_OK) {
         LOG(TRACE_LOG_ERR, "%s: sending msg to ctrl component failed (%s)",
                 __func__);
    }
}


/**
 * Send a message to the ctrl component about a policer configuration
 * 
 * @param[in] pol
 *      The policer configuration to send
 */
void
notify_policy_add(policer_info_t * pol)
{
    int rc;
    
    if(ctrl_session == NULL)
        return;
    
    junos_trace(DPM_TRACEFLAG_CONNECTION, "%s", __func__);
    
    // change to network byte order
    pol->if_exceeding.burst_size_limit = 
        htonq(pol->if_exceeding.burst_size_limit);
    
    if(pol->if_exceeding.bw_in_percent) {
        pol->if_exceeding.bw_u.bandwidth_percent = 
            htonl(pol->if_exceeding.bw_u.bandwidth_percent);
    } else {
        pol->if_exceeding.bw_u.bandwidth_limit = 
            htonq(pol->if_exceeding.bw_u.bandwidth_limit);
    }

    rc = pconn_server_send(ctrl_session, MSG_POLICER, pol,
            sizeof(policer_info_t));
    
    if(rc != PCONN_OK) {
         LOG(TRACE_LOG_ERR, "%s: sending msg to ctrl component failed (%s)",
                 __func__);
    }
    
    // change BACK to host byte order
    pol->if_exceeding.burst_size_limit = 
        ntohq(pol->if_exceeding.burst_size_limit);
    
    if(pol->if_exceeding.bw_in_percent) {
        pol->if_exceeding.bw_u.bandwidth_percent = 
            ntohl(pol->if_exceeding.bw_u.bandwidth_percent);
    } else {
        pol->if_exceeding.bw_u.bandwidth_limit = 
            ntohq(pol->if_exceeding.bw_u.bandwidth_limit);
    }
}


/**
 * Send a message to the ctrl component about a default interface configuration
 * 
 * @param[in] int_name
 *      The interface name
 * 
 * @param[in] ifl_subunit
 *      The interface IFL subunit
 * 
 * @param[in] interface
 *      The interface default policy info
 * 
 * @param[in] ifl_index
 *      The interface IFL index
 */
void
notify_interface_add(char * int_name,
                     if_subunit_t ifl_subunit,
                     int_def_t * interface,
                     ifl_idx_t ifl_index)
{
    int_info_t data;
    int rc;
    
    if(ctrl_session == NULL)
        return;
    
    junos_trace(DPM_TRACEFLAG_CONNECTION, "%s", __func__);
    
    bzero(&data, sizeof(int_info_t));
    strcpy(data.name, int_name);
    data.subunit = htonl((uint32_t)ifl_subunit);
    data.index = htonl((uint32_t)ifl_idx_t_getval(ifl_index));
    strcpy(data.input_pol, interface->input_pol);
    strcpy(data.output_pol, interface->output_pol);

    rc = pconn_server_send(ctrl_session, MSG_INTERFACE, &data,
            sizeof(int_info_t));
    
    if(rc != PCONN_OK) {
         LOG(TRACE_LOG_ERR, "%s: sending msg to ctrl component failed (%s)",
                 __func__);
    }
}


/**
 * Send a message to the ctrl component about a subscriber configuration
 * 
 * @param[in] sub
 *      The subscriber configuration to send
 */
void
notify_subscriber_add(sub_info_t * sub)
{
    int rc;
    
    if(ctrl_session == NULL)
        return;
    
    junos_trace(DPM_TRACEFLAG_CONNECTION, "%s", __func__);

    rc = pconn_server_send(ctrl_session, MSG_SUBSCRIBER, sub,
            sizeof(sub_info_t));
    
    if(rc != PCONN_OK) {
         LOG(TRACE_LOG_ERR, "%s: sending msg to ctrl component failed (%s)",
                 __func__);
    }
}

/*
 * $Id: hellopics-mgmt_conn.c 366969 2010-03-09 15:30:13Z taoliu $
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
 * @file hellopics-mgmt_conn.c
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */

#include <string.h>
#include <jnx/aux_types.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jnx/pconn.h>
#include <ddl/ddl.h>
#include <sync/hellopics_ipc.h>
#include "hellopics-mgmt_logging.h"
#include "hellopics-mgmt_conn.h"
#include "hellopics-mgmt_ha.h"

#include HELLOPICS_OUT_H

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



#define HELLOPICS_MGMT_SERVER_MAX_CONN 2  ///< max # cnxs (1-4-ctrl & 1-4-data)
#define HELLOPICS_HELLO_MSG_INTERVAL  60  ///< # of seconds between sending HELLOs


extern volatile boolean is_master;
/*** Data Structures ***/

hellopics_stats_t  hellopics_stats;       ///< global message stats
static pconn_server_t     * mgmt_server;  ///< the server connection info
static pconn_session_t    * ctrl_session; ///< the session to the control component
static pconn_session_t    * data_session; ///< the session to the data component
static boolean            ctrl_ready;     ///< the control component is ready
static boolean            data_ready;     ///< the data component is ready
static evTimerID          timer_id;       ///< timer ID used for setting off a HELLO
static uint32_t           hello_seq_num;  ///< Next HELLO sequence number
static boolean            received;       ///< received the last sent hello back
static evContext          ev_ctx;         ///< event context

/*** STATIC/INTERNAL Functions ***/


/**
 * Send a HELLO message to the ctrl or data component. The component it is sent
 * to alternates each time this is called starting with the data component.
 * 
 * @param[in] ctx
 *      The event context
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
sendHelloMessage(evContext ctx __unused,
                 void * uap  __unused,
                 struct timespec due __unused,
                 struct timespec inter __unused)
{
    static component_id_e direction = HELLOPICS_ID_DATA;
    
    int rc;
    uint32_t hello_data = htonl(++hello_seq_num);
    
    if(!received) {
        ++hellopics_stats.msgs_missed;
    }
    
    if(direction == HELLOPICS_ID_DATA) {

        rc = pconn_server_send(data_session, MSG_HELLO,
                               &hello_data, sizeof(hello_data));
        if(rc != PCONN_OK) {
            ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                "%s: Failed to send HELLO to data component. Error: %d",
                __func__, rc);
        }
        
        junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
            "%s: Sent a HELLO message to the data component (%d)",
            __func__, hello_seq_num);

        direction = HELLOPICS_ID_CTRL; // switch direction
    } else {
        rc = pconn_server_send(ctrl_session, MSG_HELLO,
                               &hello_data, sizeof(hello_data));
        if(rc != PCONN_OK) {
            ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                "%s: Failed to send HELLO to control component. Error: %d",
                __func__, rc);
        }
        
        junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
            "%s: Sent a HELLO message to the control component (%d)",
            __func__,  hello_seq_num);
        
        direction = HELLOPICS_ID_DATA; // switch direction
    }
    
    ++hellopics_stats.msgs_sent;
    update_replication_entry(&hellopics_stats); 
    received = FALSE;
}


/**
 * Start the event timer which will generate HELLO messages periodically
 * 
 * @param[in] ctx
 *      The event context
 */
static void
startMessageCycle(void)
{
    if(evTestID(timer_id)) {
        evClearTimer(ev_ctx, timer_id);
        evInitID(&timer_id);
    }
    
    if(evSetTimer(ev_ctx, sendHelloMessage, NULL, evConsTime(0, 0),
            evConsTime(HELLOPICS_HELLO_MSG_INTERVAL, 0), &timer_id)) {

        ERRMSG(PED_LOG_TAG, TRACE_LOG_ERR,
            "%s: evSetTimer() FAILED! Cannot continue processing "
            "periodic events", __func__);
    }
}
/**
 * Stop the event timer which is generating HELLO messages
 * 
 * @param[in] ctx
 *      The event context
 */
static void
stopMessageCycle(void)
{
    if(evTestID(timer_id)) {
        evClearTimer(ev_ctx, timer_id);
        evInitID(&timer_id);
    }
}


/**
 * A helper function to send PIC peer information to the other PIC.
 * Either ctrl component info is sent to the data component or vice versa.
 * 
 * @param[in] to_session
 *      The peer to which we are send the information to
 * 
 * @param[in] for_session
 *      The established session for which we will extract 
 *      the connection information, for sending to the to_session peer
 * 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
static status_t
send_peer_info(pconn_session_t * to_session,
               pconn_session_t * for_session)
{
    pconn_peer_info_t info;
    int rc;
    
    rc = pconn_session_get_peer_info(for_session, &info);
    
    if(rc != PCONN_OK) {
        ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
            "%s: Cannot retrieve peer info for given session. "
            "Error: %d", __func__, rc);
         return EFAIL;
    }
    
    info.ppi_peer_type = htonl(info.ppi_peer_type);
    info.ppi_fpc_slot = htonl(info.ppi_fpc_slot);
    info.ppi_pic_slot = htonl(info.ppi_pic_slot);
    
    
    // got the peer info for the data_session, so send it
    rc = pconn_server_send(to_session, MSG_PEER,
                           &info, sizeof(pconn_peer_info_t));
    
    if(rc != PCONN_OK) {
        ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
            "%s: Failed to send peer info to other component. "
            "Error: %d", __func__, rc);
         return EFAIL;
    }
    
    junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
        "%s: Sent the PIC peer info to other PIC peer", __func__);

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
        ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
            "%s: Cannot retrieve peer info for session. Error: %d",
             __func__, rc);
    } else {
        INSIST_ERR(info.ppi_peer_type == PCONN_PEER_TYPE_PIC);
        
        junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
            "%s: Received a %s event from ms-%d/%d/0",
            __func__, 
            ((event == PCONN_EVENT_ESTABLISHED) ? "CONENCT" : "DISCONNECT"),
            info.ppi_fpc_slot, info.ppi_pic_slot);
    }

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:
            // nothing to do
            break;

        case PCONN_EVENT_SHUTDOWN:
            
            if(session == ctrl_session) {
                    ctrl_session = NULL;
                    stopMessageCycle();
                    
                    junos_trace(HELLOPICS_TRACEFLAG_CONNECTION, "%s: Connection"
                        " from the control component was shutdown.",
                        __func__);
                    
            } else if(session == data_session) {
                    data_session = NULL;
                    stopMessageCycle();
                    
                    junos_trace(HELLOPICS_TRACEFLAG_CONNECTION, "%s: Connection"
                        " from the data component was shutdown.",
                        __func__);
                    
            } else {
                ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                    "%s: Cannot find an existing peer session to shutdown",
                     __func__);
            }

            break;

        default:
            ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                "%s: Received an unknown or PCONN_EVENT_FAILED event",
                 __func__);
            
            if(session == ctrl_session) {
                    ctrl_session = NULL;
                    stopMessageCycle();
                    
                    junos_trace(HELLOPICS_TRACEFLAG_CONNECTION, "%s: Connection"
                        " from the control component was shutdown.",
                        __func__);
                    
            } else if(session == data_session) {
                    data_session = NULL;
                    stopMessageCycle();
                    
                    junos_trace(HELLOPICS_TRACEFLAG_CONNECTION, "%s: Connection"
                        " from the data component was shutdown.",
                        __func__);
                    
            } else {
                ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                    "%s: Cannot find an existing peer session to shutdown",
                     __func__);
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
    static component_id_e get_peer_todo = 0;
    
    int rc;
    uint32_t id, helloNum;
    
    if(session == ctrl_session) {

        switch(msg->subtype) {
            
            case MSG_READY:
            
                junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
                    "%s: Control component is ready", __func__);
                        
                ctrl_ready = TRUE;
                if(data_ready) { // if both are ready
                    startMessageCycle();
                }
                break;
                
            case MSG_GET_PEER:     // we only expect this from the data-session
            
                junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
                    "%s: Control component is requesting peer info",
                    __func__);
                    
                if(!data_session) {
                    // don't have this info yet, but when we do we send it
                    get_peer_todo = HELLOPICS_ID_DATA;
                } else {
                    rc = send_peer_info(session, data_session);
                    if(rc == EFAIL) {
                        return rc;
                    }
                }
                break;

            case MSG_HELLO:
            
                INSIST_ERR(msg->length == sizeof(uint32_t));
                
                helloNum = ntohl(*((uint32_t *)msg->data));
                
                junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
                    "%s: Received a HELLO from the control component (%d)",
                    __func__, helloNum);
                
                if(helloNum == hello_seq_num) {
                    ++hellopics_stats.msgs_received;
                    received = TRUE;
                } else {
                    ++hellopics_stats.msgs_badorder;
                }
                update_replication_entry(&hellopics_stats); 
                break;
            
            case MSG_ID:
            
                break; // do nothing we already have it (shouldn't happen)
                       // otherwise we wouldn't know session == ctrl_session
            
            default:
            
                ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                    "%s: Received an unknown message type (%d) from the "
                    "control component", __func__, msg->subtype);
                break;
        }
    } else if(session == data_session) {

        switch(msg->subtype) {
            
            case MSG_READY:
            
                junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
                    "%s: Data component is ready", __func__);
                        
                data_ready = TRUE;
                if(ctrl_ready) { // if both are ready
                    startMessageCycle();
                }
                break;
                
            case MSG_GET_PEER:   // we expect to get this from the data_session
            
                junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
                    "%s: Data component is requesting peer info",
                    __func__);
                    
                if(!ctrl_session) {
                    // don't have this info yet, but when we do we send it
                    get_peer_todo = HELLOPICS_ID_CTRL;
                } else {
                    rc = send_peer_info(session, ctrl_session);
                    if(rc == EFAIL) {
                        return rc;
                    }
                }
                break;

            case MSG_HELLO:
                
                INSIST_ERR(msg->length == sizeof(uint32_t));
                
                helloNum = ntohl(*((uint32_t *)msg->data));
                
                junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
                    "%s: Received a HELLO from the data component (%d)",
                    __func__, helloNum);
                
                if(helloNum == hello_seq_num) {
                    ++hellopics_stats.msgs_received;
                    received = TRUE;
                } else {
                    ++hellopics_stats.msgs_badorder;
                }
                break;
            
            case MSG_ID:
            
                break; // do nothing we already have it (shouldn't happen)
                       // otherwise we wouldn't know session == data_session
            
            default:
            
                ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                    "%s: Received an unknown message type (%d) from the "
                    "data component", __func__, msg->subtype);
                break;
        }
    } else {
        // It must be an identifier message to tell us who it is
        if(msg->subtype == MSG_ID) {
            
            INSIST_ERR(msg->length == sizeof(uint32_t));
            id = ntohl(*(uint32_t *)msg->data);
            
            switch(id) {
                
                case HELLOPICS_ID_CTRL:
                    if(ctrl_session == NULL) {
                        // this peer is the control peer
                        ctrl_session = session;
                        
                        junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
                            "%s: Received ID from the control component.",
                            __func__);
                        
                        // check for an existing req. for this peer's info
                        if(get_peer_todo == HELLOPICS_ID_CTRL) {
                             // check requester (data_session) is still up
                            if(data_session) {
                                junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
                                    "%s: Sending delayed ctrl peer info "
                                    "to data component", __func__);
                                send_peer_info(data_session, session);
                            } else {
                                get_peer_todo = 0;
                            }
                        }
                    } else { // ctrl_session != session
                        ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                            "%s: Received ID for another control component "
                            "when a session to a control component is "
                            "already established", __func__);
                    }
                    break;
                    
                case HELLOPICS_ID_DATA:
                    if(data_session == NULL) {
                        // this peer is the data peer
                        data_session = session;
                        
                        junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
                            "%s: Received ID from the data component.",
                            __func__);
                        
                        // check for an existing req. for this peer's info
                        if(get_peer_todo == HELLOPICS_ID_DATA) {
                             // check requester (ctrl_session) is still up
                            if(ctrl_session) {
                                junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
                                    "%s: Sending delayed data peer info "
                                    "to ctrl component", __func__);
                                send_peer_info(ctrl_session, session);
                            } else {
                                get_peer_todo = 0;
                            }
                        }
                    } else { // data_session != session
                        ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                            "%s: Received ID for another data component "
                            "when a session to a data component is "
                            "already established", __func__);
                    }
                    break;
                    
                default:
                    ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                        "%s: Received an ID (%d) that is not the ctrl or "
                        "data component", __func__, id);
                    break;
            }
        } else {
            ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
                "%s: Received a message (%d) for an unknown component. "
                "ID is expected", __func__, msg->subtype);
        }
    }
    
    return SUCCESS;
}

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Initialize the statistic variables 
 */
void
init_connection_stats(void)
{
    bzero(&hellopics_stats, sizeof(hellopics_stats_t));    
}
/**
 * Create the server socket connection
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
start_server(evContext ctx)
{
    pconn_server_params_t params;
    
    ev_ctx = ctx;
    
    // init the sessions
    ctrl_session = NULL;
    data_session = NULL;
    
    // init module's other variables
    ctrl_ready = FALSE;
    data_ready = FALSE;
    received = TRUE;
    hello_seq_num = 0;
    evInitID(&timer_id);

    bzero(&params, sizeof(pconn_server_params_t));
    
    // setup the server args
    params.pconn_port            = HELLOPICS_PORT_NUM;
    params.pconn_max_connections = HELLOPICS_MGMT_SERVER_MAX_CONN;
    params.pconn_event_handler   = receive_connection;
    
    // bind
    mgmt_server = pconn_server_create(&params, ctx, receive_message, NULL);
    
    if(mgmt_server == NULL) {
        ERRMSG(HELLOPICS_MGMT, TRACE_LOG_ERR,
            "%s: Failed to initialize the pconn server on port %d.",
            __func__, HELLOPICS_PORT_NUM);
        return EFAIL;
    }
    
    junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
        "%s: Successfully initialized the pconn server on port %d.",
        __func__, HELLOPICS_PORT_NUM);
    
    return SUCCESS;
}
/**
 * Initialize the server if it is the master.
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful;
 */
status_t
init_server(evContext ctx)
{
   if(is_master == TRUE) {
      junos_trace(HELLOPICS_TRACEFLAG_NORMAL, 
            "%s: Master,starting server", __func__);
      return start_server(ctx);
   }
   return SUCCESS;
}


/**
 * Close existing connections and shutdown server
 */
void
close_connections(void)
{
    if(data_session) {
        junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
            "%s: Closing the session to the data component.", __func__);
        pconn_session_close(data_session);
        data_session = NULL;
    }
    
    if(ctrl_session) {
        junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
            "%s: Closing the session to the control component.", __func__);
        pconn_session_close(ctrl_session);
        ctrl_session = NULL;
    }
    
    if(mgmt_server) {
        junos_trace(HELLOPICS_TRACEFLAG_CONNECTION,
            "%s: Shuting down the server on port %d.",
            __func__, HELLOPICS_PORT_NUM);
        pconn_server_shutdown(mgmt_server);
        mgmt_server = NULL;
    }
}

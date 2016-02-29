/*
 * $Id: ped_conn.c 366969 2010-03-09 15:30:13Z taoliu $
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
 * @file ped_conn.c
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections to peers (PFD & CPD).
 */

#include <sync/common.h>
#include <sync/policy_ipc.h>
#include <jnx/pconn.h>
#include "ped_config.h"
#include "ped_conn.h"
#include "ped_logging.h"

#include PE_OUT_H

/*** Constants ***/

#define PED_SERVER_MAX_CONN 2  ///< max # cnxs (just 2 = ctrl & data)

#define IF_NAME_SIZE  64 ///< max chars for an MS PIC interface name

/*** Data Structures ***/

static pconn_server_t   * mgmt_server; ///< the server connection info
static pconn_session_t  * cpd_session; ///< the session to the control component (CPD)
static pconn_session_t  * pfd_session; ///< the session to the data component (PFD)
static char cpd_pic_name[IF_NAME_SIZE]; /// < the IFD name of the PIC running the CPD
static char pfd_pic_name[IF_NAME_SIZE]; /// < the IFD name of the PIC running the PFD


/*** STATIC/INTERNAL Functions ***/


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
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Cannot retrieve peer info for given session. "
            "Error: %d", __func__, rc);
         return EFAIL;
    }
    
    info.ppi_peer_type = htonl(info.ppi_peer_type);
    info.ppi_fpc_slot = htonl(info.ppi_fpc_slot);
    info.ppi_pic_slot = htonl(info.ppi_pic_slot);
    
    // got the peer info for the for_session, so send it
    rc = pconn_server_send(to_session, MSG_PEER,
                           &info, sizeof(pconn_peer_info_t));
    
    if(rc != PCONN_OK) {
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Failed to send peer info to peer component. "
            "Error: %d", __func__, rc);
         return EFAIL;
    }
    
    junos_trace(PED_TRACEFLAG_CONNECTION,
        "%s: Sent the PIC peer info to a PIC peer", __func__);

    return SUCCESS;
}


/**
 * A helper function to send a PIC the configured addresses. Does not send 
 * anything, if the addresses are zero.
 * 
 * @param[in] to_session
 *      The peer to which we are send the information to
 * 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
static status_t
send_addresses(pconn_session_t * to_session)
{
    int rc;
    struct {
        in_addr_t cpd_address;
        in_addr_t pfd_address;
    } addresses;
    
    addresses.pfd_address = get_pfd_address();
    addresses.cpd_address = get_cpd_address();
        
    if(!addresses.cpd_address || !addresses.pfd_address) {
        junos_trace(PED_TRACEFLAG_CONNECTION,
            "%s: Skipping because addresses are not set yet", __func__);
        return SUCCESS; // don't send when not set
    }

    // got the peer info for the pfd_session, so send it
    rc = pconn_server_send(to_session, MSG_ADDRESSES,
                           &addresses, sizeof(addresses));
    
    if(rc != PCONN_OK) {
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Failed to send addresses to peer component. "
            "Error: %d", __func__, rc);
         return EFAIL;
    }
    
    junos_trace(PED_TRACEFLAG_CONNECTION,
        "%s: Sent the addresses info to a PIC peer", __func__);

    return SUCCESS;
}


/**
 * A helper function to send configuration down to PICs (CPD and PFD)
 * 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
static status_t
configure_pics(void)
{
    int rc;
    
    junos_trace(PED_TRACEFLAG_CONNECTION, "%s: Sending CPD peer info to PFD",
        __func__);
    
    rc = send_peer_info(pfd_session, cpd_session);
    if(rc != SUCCESS) {
        return rc;
    }

    junos_trace(PED_TRACEFLAG_CONNECTION, "%s: Sending addresses to the CPD",
        __func__);
    
    rc = send_addresses(cpd_session);
    if(rc != SUCCESS) {
        return rc;
    }
    
    junos_trace(PED_TRACEFLAG_CONNECTION, "%s: Sending addresses to the PFD",
        __func__);
    
    return send_addresses(pfd_session);
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
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Cannot retrieve peer info for session. Error: %d",
             __func__, rc);
    } else {
        INSIST_ERR(info.ppi_peer_type == PCONN_PEER_TYPE_PIC);
        
        junos_trace(PED_TRACEFLAG_CONNECTION,
            "%s: Received a %s event from ms-%d/%d/0", __func__, 
            ((event == PCONN_EVENT_ESTABLISHED) ? "CONENCT" : "DISCONNECT"),
            info.ppi_fpc_slot, info.ppi_pic_slot);
    }

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:
            // nothing to do
            break;

        case PCONN_EVENT_SHUTDOWN:
            
            if(session == cpd_session) {
                cpd_session = NULL;
                cpd_pic_name[0] = '\0';
            } else if(session == pfd_session) {
                pfd_session = NULL;
                pfd_pic_name[0] = '\0';
            } else {
                ERRMSG(PED, TRACE_LOG_ERR,
                    "%s: Cannot find an existing peer session to shutdown",
                     __func__);
            }
                      
            break;

        default:
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Received an unknown or PCONN_EVENT_FAILED event",
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
    uint32_t id;
    pconn_peer_info_t info;
    int rc;
    
    if(session == cpd_session) {

        switch(msg->subtype) {
            
            case MSG_ID:
            
                break; // do nothing we already have it (shouldn't happen)
                       // otherwise we wouldn't know session == cpd_session
            
            default:
            
                ERRMSG(PED, TRACE_LOG_ERR,
                    "%s: Received an unknown message type (%d) from the "
                    "control component", __func__, msg->subtype);
                break;
        }
    } else if(session == pfd_session) {

        switch(msg->subtype) {
            
            case MSG_ID:
            
                break; // do nothing we already have it (shouldn't happen)
                       // otherwise we wouldn't know session == pfd_session
            
            default:
            
                ERRMSG(PED, TRACE_LOG_ERR,
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
                
                case ID_CPD:
                    if(cpd_session == NULL) {

                        cpd_session = session;
                        
                        rc = pconn_session_get_peer_info(session, &info);
                        
                        if(rc != PCONN_OK) {
                            ERRMSG(PED, TRACE_LOG_ERR,
                                "%s: Cannot retrieve peer info for session. "
                                "Error: %d", __func__, rc);
                        } else {
                            // save PIC IFD name
                            snprintf(cpd_pic_name, IF_NAME_SIZE, "ms-%d/%d/0",
                                info.ppi_fpc_slot, info.ppi_pic_slot);                        
                        }
                        
                        // only when we have both cnxs do we configure them
                        if(pfd_session) {
                            configure_pics();
                        }
                    } else { // cpd_session != session
                        ERRMSG(PED, TRACE_LOG_ERR,
                            "%s: Received ID for another CPD when a session to "
                            "a CPD is already established", __func__);
                    }
                    break;
                    
                case ID_PFD:
                    if(pfd_session == NULL) {

                        pfd_session = session;
                        
                        rc = pconn_session_get_peer_info(session, &info);
                        
                        if(rc != PCONN_OK) {
                            ERRMSG(PED, TRACE_LOG_ERR,
                                "%s: Cannot retrieve peer info for session. "
                                "Error: %d", __func__, rc);
                        } else {
                            // save PIC IFD name
                            snprintf(pfd_pic_name, IF_NAME_SIZE, "ms-%d/%d/0",
                                info.ppi_fpc_slot, info.ppi_pic_slot);                        
                        }
                        
                        // only when we have both cnxs do we configure them
                        if(cpd_session) {
                            configure_pics();
                        }
                    } else { // pfd_session != session
                        ERRMSG(PED, TRACE_LOG_ERR,
                            "%s: Received ID for another PFD when a session to "
                            "a PFD is already established", __func__);
                    }
                    break;
                    
                default:
                    ERRMSG(PED, TRACE_LOG_ERR,
                        "%s: Received an ID (%d) that is not the CPD or PFD",
                         __func__, id);
                    break;
            }
        } else {
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Received a message (%d) for an unknown component. "
                "ID is expected", __func__, msg->subtype);
        }
    }
    
    return SUCCESS;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the server socket connection
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_server(evContext ctx)
{
    pconn_server_params_t params;
    
    // init the sessions
    cpd_session = NULL;
    pfd_session = NULL;
    
    bzero(&params, sizeof(pconn_server_params_t));
    
    // setup the server args
    params.pconn_port            = PED_PORT_NUM;
    params.pconn_max_connections = PED_SERVER_MAX_CONN;
    params.pconn_event_handler   = receive_connection;
    
    // bind
    mgmt_server = pconn_server_create(&params, ctx, receive_message, &ctx);
    
    if(mgmt_server == NULL) {
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Failed to initialize the pconn server on port %d.",
            __func__, PED_PORT_NUM);
        return EFAIL;
    }
    
    junos_trace(PED_TRACEFLAG_CONNECTION,
        "%s: Successfully initialized the pconn server on port %d.",
        __func__, PED_PORT_NUM);
    
    return SUCCESS;
}


/**
 * Close existing connections and shutdown server
 */
void
close_connections(void)
{
    if(pfd_session) {
        junos_trace(PED_TRACEFLAG_CONNECTION,
            "%s: Closing the session to the data component.", __func__);
        pconn_session_close(pfd_session);
        pfd_session = NULL;
        pfd_pic_name[0] = '\0';
    }
    
    if(cpd_session) {
        junos_trace(PED_TRACEFLAG_CONNECTION,
            "%s: Closing the session to the control component.", __func__);
        pconn_session_close(cpd_session);
        cpd_session = NULL;
        cpd_pic_name[0] = '\0';
    }
    
    if(mgmt_server) {
        junos_trace(PED_TRACEFLAG_CONNECTION, "%s: Shuting down the "
            "server on port %d.", __func__, PED_PORT_NUM);
        pconn_server_shutdown(mgmt_server);
        mgmt_server = NULL;
    }
}


/**
 * Reconfigure CPD and PFD peers with new addresses in case of a change to any 
 * of the two addresses.
 */
void
reconfigure_peers(void)
{
    if(!cpd_session || !pfd_session) {
        return;
    }

    junos_trace(PED_TRACEFLAG_CONNECTION, "%s: Sending addresses to the CPD",
        __func__);
    
    send_addresses(cpd_session);
    
    junos_trace(PED_TRACEFLAG_CONNECTION, "%s: Sending addresses to the PFD",
        __func__);
    
    send_addresses(pfd_session);
}


/**
 * Called when the KCOM module detects that an ms-* pic has gone offline
 * 
 * @param[in] intername_name
 *      Name of the IFD going down
 */
void
mspic_offline(char * intername_name)
{
    if(strcmp(intername_name, cpd_pic_name) == 0 && cpd_session) {

        junos_trace(PED_TRACEFLAG_CONNECTION,
            "%s: Closing the session to the control component.", __func__);
        pconn_session_close(cpd_session);
        cpd_session = NULL;
        cpd_pic_name[0] = '\0';
        
    }
    
    if(strcmp(intername_name, pfd_pic_name) == 0 && pfd_session) {

        junos_trace(PED_TRACEFLAG_CONNECTION,
            "%s: Closing the session to the data component.", __func__);
        pconn_session_close(pfd_session);
        pfd_session = NULL;
        pfd_pic_name[0] = '\0';
    }
}

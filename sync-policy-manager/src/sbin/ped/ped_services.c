/*
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * $Id: ped_services.c 366969 2010-03-09 15:30:13Z taoliu $
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file ped_services.c
 * 
 * @brief Routines related to talking to psd 
 * 
 * Functions for connecting to psd and requesting policies from it
 * 
 */
#include <sync/common.h>
#include <sync/psd_ipc.h>
#include "ped_config.h"
#include "ped_services.h"
#include "ped_schedule.h"
#include "ped_logging.h"
#include "ped_ssd.h"
#include "ped_kcom.h"
#include "ped_policy_table.h"
#include "ped_ssd.h"
#include "ped_snmp.h"
#include "ped_script.h"

#include PE_OUT_H
#include PE_SEQUENCE_H

/*** Constants ***/

/**
 * @brief initialize the evFileID
 */
#ifndef evInitID
#define evInitID(id) ((id)->opaque = NULL)
#endif

/**
 * @brief Test if the evFileID has been initialized
 */
#ifndef evTestID
#define evTestID(id) ((id).opaque != NULL)
#endif


/**
 * @brief Send buffer size in bytes for any pipes / sockets
 */
#define BUFFER_SIZE         1024


/*** Data Structures ***/

static int psd_sock = -1;                   ///< socket to psd -1 => not connected
static ipc_pipe_t * psd_pipe = NULL;        ///< pipe to psd
static evFileID psd_read_id;                ///< fd read ID
static boolean psd_hb = FALSE;              ///< got heart-beat
static boolean psd_conn = FALSE;            ///< state of PSD connection
static boolean need_policy_update = FALSE;  ///< PSD or PED config changed 
static struct timespec psd_conn_time;       ///< the up time of psd connection
extern evContext ped_ctx;                   ///< event context for ped


/*** STATIC/INTERNAL Functions ***/


/**
 * Send a request to the policy server for this interface name and
 * address family.
 * 
 * @param[in] req_type
 *     Type of message to send
 * 
 * @param[in] ifname
 *     The name of the interface for which we want to know of policies
 * 
 * @param[in] af
 *     The address family
 */
static void
send_psd_req(msg_type_e req_type, char * ifname, uint8_t af)
{
    ipc_msg_t req;
    policy_req_msg_t req_data;

    // Check connection.
    if(!psd_pipe) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: No pipe!", __func__);
        return;
    }

    bzero(&req, sizeof(ipc_msg_t));
    req.type = IPC_MSG_TYPE_PSD;  // we're talking with the  PSD
    req.subtype = req_type;
    req.opcode = 0;                   // unused
    req.error = 0;

    switch(req_type) {
        
        case MSG_HB:
        
            junos_trace(PED_TRACEFLAG_NORMAL,
                    "%s: Sending request MSG_HB to psd.", __func__); 
            req.length = 0;
            break;
            
        case MSG_POLICY_REQ:
        
            junos_trace(PED_TRACEFLAG_NORMAL,
                    "%s: Sending request MSG_POLICY_REQ to psd.", __func__);
        
            // ifname should have already been checked
            strcpy(req_data.ifname, ifname);
            req_data.af = af;
            req.length = sizeof(policy_req_msg_t);
            
            junos_trace(PED_TRACEFLAG_NORMAL,
                    "%s: Sending policy request for ifname=%s family=%d.",
                    __func__, ifname, af);  
            break;
            
        case MSG_UPDATE_DONE:
        
            junos_trace(PED_TRACEFLAG_NORMAL,
                    "%s: Sending request MSG_UPDATE_DONE to psd.",
                    __func__);
                    
            req.length = 0;
            break;
            
        default:
        
            ERRMSG(PED, TRACE_LOG_ERR,
                    "%s: Unknown request type %d.", __func__, req_type);
            return;
    }

    // write the message to the pipe
    if(ipc_msg_write(psd_pipe, &req, &req_data) != SUCCESS) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Write message FAILED!", __func__);
    } else if(ipc_pipe_write(psd_pipe, 0) != SUCCESS) {
        // write out everything in the pipe (send the message)
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Write pipe FAILED!", __func__);
    }
    
    ipc_pipe_write_clean(psd_pipe);
}


/**
 * Read messages from Policy Server Daemon (psd).
 */
static void
psd_ipc_read(evContext ctx __unused, void *param __unused,
              int fd __unused, int evmask __unused)
{
    static boolean dont_clean = FALSE;
    ipc_msg_t *reply;
    policy_req_msg_t *req;

    if(ipc_pipe_read(psd_pipe) < 0) { // different errno
        
        /*
         * When errno is EAGAIN don't shutdown the connection.
         * EAGAIN is received when only partial msg is read.
         * So bail out and try later.
         */
        if(errno == EAGAIN || errno == EINTR) //ignore
            return;
            
        if(errno != 0) { // problem
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Null ipc read causing connection shutdown due to: %m",
                __func__);

            // if cannot read anything, shutdown the connection
            disconnect_psd();
        } else {
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Remote side of pipe was closed!", __func__);
            ipc_pipe_read_clean(psd_pipe);
            disconnect_psd();
        }
        return;
    }
    
    // get the message back out of the pipe
    while((reply = ipc_msg_read(psd_pipe)) != NULL) {

        // Examine at the reply message:
        // if we got a null message, then complain 
        if(reply->type == 0 && reply->subtype == 0 && reply->length == 0 &&
                reply->opcode == 0 && reply->error == 0) {

            ERRMSG(PED, TRACE_LOG_ERR,
                    "%s: Null message!", __func__);

            ipc_pipe_read_clean(psd_pipe);
            return;
        }

        // sanity checks: we complain, but still handle the message
        INSIST(reply->type == IPC_MSG_TYPE_PSD); 

        switch(reply->subtype) {
            
            case MSG_POLICY_NA: // no match on policy server
                
                req = (policy_req_msg_t *)reply->data; //psd echo'd request
                junos_trace(PED_TRACEFLAG_NORMAL,
                        "%s: Got Message, no policy for %s.",
                        __func__, req->ifname);
                // clear the policy or create an empty one 
                policy_table_clear_policy(req->ifname, req->af);
                break;
                
            case MSG_ROUTE:
                
                INSIST(reply->length == sizeof(policy_route_msg_t));
                junos_trace(PED_TRACEFLAG_NORMAL,
                        "%s: Got Message, route.", __func__);
                policy_table_add_route((policy_route_msg_t *)(reply->data));
                break;
                
            case MSG_FILTER:
                
                INSIST(reply->length == sizeof(policy_filter_msg_t));
                junos_trace(PED_TRACEFLAG_NORMAL,
                        "%s: Got Message, filter.", __func__);
                policy_table_add_filter((policy_filter_msg_t *)(reply->data));
                break;
                
            case MSG_HB:
                
                junos_trace(PED_TRACEFLAG_HB,
                        "%s: Got Message, heartbeat.", __func__);
                
                psd_hb = TRUE;
                
                if(!psd_conn) {
                    psd_conn = TRUE;
                    ped_notify_psd_state(psd_conn);
                }
                
                if(!dont_clean) {
                    policy_table_clean();
                }
                
                if(need_policy_update &&
                   get_ssd_ready() && get_ssd_idle() &&
                   policy_table_unverify_all()) {

                    // call update_interface on all interfaces w/ REFRESH
                    update_all_interfaces();
                    send_psd_req(MSG_UPDATE_DONE, NULL, 0);
                    // stop cleaning until we get MSG_UPDATE_DONE back 
                    dont_clean = TRUE;
                    need_policy_update = FALSE;
                }
                
                break;
                
            case MSG_UPDATE_DONE:
                
                junos_trace(PED_TRACEFLAG_NORMAL,
                        "%s: Got Message, update done.", __func__);

                // should be the last thing we get back from the PSD after a
                // whole update to all policies (because we sent this echo 
                // request last)
                
                dont_clean = FALSE; // resume cleaning
                policy_table_clean();
                
                break;
                
            case MSG_POLICY_UPDATE:
                
                junos_trace(PED_TRACEFLAG_NORMAL,
                        "%s: Message, update all interfaces.", __func__);
                
                // we need to update all policies because of changes on server 
                need_policy_update = TRUE; // will do this upon receiving HB

                break;
                
            default:
                
                ERRMSG(PED, TRACE_LOG_ERR,
                        "%s: Unknown message from policy server!", __func__);
                break;
        }
    }
    ipc_pipe_read_clean(psd_pipe);
}


/**
 * Establish a connection with the  Policy Server Daemon (psd)  
 * and create/setup necessary infrastructure for communicating with it.
 * 
 * @return TRUE if the connection was successfully established, otherwise FALSE
 */
static boolean
connect_psd(void)
{
    struct sockaddr_in saddr;

    // options
    const int sndbuf = BUFFER_SIZE;           // send buffer
    const int on = 1;                         // to turn an option on

    if(psd_sock > -1) // socket and connection already exists
        return TRUE;

    junos_trace(PED_TRACEFLAG_NORMAL,
            "%s: Connecting to psd...", __func__);

    psd_sock = socket(AF_INET, SOCK_STREAM, 0);
    INSIST(psd_sock >= 0);

    /*
     * Set some socket options
     *  ... if any fail we should eventually call disconnect_psd()
     */
    if(setsockopt(psd_sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof (on))) {
        close(psd_sock);
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: setsockopt SO_REUSEADDR FAILED!", __func__);
        return FALSE;
    }
    if(setsockopt(psd_sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf))) {
        close(psd_sock);
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: setsockopt SO_SNDBUF FAILED!", __func__);
        return FALSE;
    }
    if(vrf_setsocketbyvrfname(psd_sock, JUNOS_SDK_IRS_TABLE_NAME, NULL)) {
        close(psd_sock);
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Set socket to VRF FAILED!", __func__);
        return FALSE;
    }

    bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(PSD_PORT_NUM);
    saddr.sin_len = sizeof(saddr);
    saddr.sin_addr.s_addr = INADDR_ANY;

    // set IP address
    if(!inet_aton(PSD_CONNECT_ADDRESS, &(saddr.sin_addr)) ||
            !(saddr.sin_addr.s_addr)) {

        saddr.sin_addr.s_addr = INADDR_ANY;
        // won't work to connect, but at least it's valid
        
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Set address %s FAILED! Default 0.0.0.0", __func__,
                PSD_CONNECT_ADDRESS);
    }

    // try to connect
    if(connect(psd_sock, (struct sockaddr*)&saddr, sizeof(saddr)) < 0) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Connect to PSD FAILED!", __func__);
        disconnect_psd();
        return FALSE;
    }

    psd_pipe = ipc_pipe_create(BUFFER_SIZE);
    if(psd_pipe == NULL) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Create pipe FAILED!", __func__);
        disconnect_psd();
        return FALSE;
    }
    
    if(ipc_pipe_attach_socket(psd_pipe, psd_sock) < 0) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Attach pipe to socket FAILED!", __func__);
        disconnect_psd();
        return FALSE;
    }
    
    evInitID(&psd_read_id);
    
    if(evSelectFD(ped_ctx, psd_sock, EV_READ, psd_ipc_read, 
            NULL, &psd_read_id) < 0) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: evSelectFD FAILED!", __func__);
        disconnect_psd();
        return FALSE;
    }
    
    need_policy_update = TRUE;

    junos_trace(PED_TRACEFLAG_NORMAL, "%s: Connected to psd", __func__);
    clock_gettime(CLOCK_REALTIME, &psd_conn_time);
    return TRUE;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Check interface name against the configured conditions.
 * If it's match update this interface in the table of 
 * managed interfaces (associated policies) and if needed 
 * request a policy from the PSD.
 * 
 * @param[in] ifname
 *     interface name
 *
 * @param[in] unit
 *     interface (IFL) unit number
 *
 * @param[in] af
 *     address family
 *
 * @param[in] op
 *     operation to this interface
 */
void
update_interface(char * ifname, uint32_t unit, uint8_t af, interface_op_e op)
{
    char full_ifname[MAX_IF_NAME_LEN + 1]; 

    INSIST(strlen(ifname) <= MAX_IF_NAME_LEN - 6); // upto 5 digits & a dot too
    INSIST(unit < 100000); // only have space for 5 digits in string
    
    sprintf(full_ifname, "%s.%d", ifname, unit);
    
    // does this interface match a condition? (is it valid?)
    if(find_matching_condition(full_ifname, af)) {
        
        junos_trace(PED_TRACEFLAG_NORMAL, 
                "%s: Match %s, op %d", __func__, full_ifname, op);

        switch(op) {
            case INTERFACE_ADD:
            case INTERFACE_REFRESH:
                send_psd_req(MSG_POLICY_REQ, full_ifname, af); // get policy
                break;
                
            case INTERFACE_DELETE:
                policy_table_delete_policy(full_ifname, af, TRUE);
                break;
                
            default:
                ERRMSG(PED, TRACE_LOG_ERR, "%s: Unknown operation %d.",
                    __func__, op);
                return;
        }
    } else { // didn't match a condition
        if(op == INTERFACE_DELETE) {
            policy_table_delete_policy(full_ifname, af, TRUE);
        } else {
            policy_table_delete_policy(full_ifname, af, FALSE);
        }
    }
}


/**
 * Check heartbeat from PSD. This function is routinely called by ped_periodic()
 * If not received then disconnect and reconnect. This also establishes the 
 * connection when the application starts.
 * 
 * @return TRUE if a heartbeat was sent to the PSD, FALSE if the connection to 
 *      the PSD is down and cannot be re-established.
 */
boolean
check_psd_hb(void)
{
    if(psd_hb) {
        junos_trace(PED_TRACEFLAG_HB, "%s: Connection is alive.", __func__);
        psd_hb = FALSE;
    } else {
        junos_trace(PED_TRACEFLAG_NORMAL, "%s: Connection is down, reconnect.",
            __func__);
        disconnect_psd();
        if(!connect_psd()) {
            return FALSE; // if connect fails no point in sending
        }
    }
    
    junos_trace(PED_TRACEFLAG_HB, "%s: Send message to PSD, heartbeat.",
        __func__);
    
    send_psd_req(MSG_HB, NULL, 0);
    
    return TRUE;
}


/**
 * Update policies
 */
void
update_policies(void)
{
    need_policy_update = TRUE;
}


/**
 * Disconnect the connection to the  Policy Server Daemon (psd).
 * Something's gone wrong...so we'll schedule a reconnect using a heart beat
 * failure flag (psd_hb).
 * 
 */
void
disconnect_psd(void)
{
    junos_trace(PED_TRACEFLAG_NORMAL, "%s", __func__);

    // Deattach event handler.

    // if select failed then don't deselect
    if(evTestID(psd_read_id)) {
        if(evDeselectFD(ped_ctx, psd_read_id) < 0) {  // Detach event.
            ERRMSG(PSD_LOG_TAG, TRACE_LOG_ERR,
                "%s: evDeselect FAILED!", __func__);
        }
        evInitID(&psd_read_id); // need to call this after deselecting
    }

    // Clear attached pipe
    if(psd_pipe != NULL) {
        ipc_pipe_detach_socket(psd_pipe, psd_sock);
        ipc_pipe_destroy(psd_pipe);
        psd_pipe = NULL;
    }

    // Clear socket
    if(psd_sock != -1) {
        close(psd_sock);
        psd_sock = -1;
    }

    psd_hb = FALSE;
    if(psd_conn == TRUE) {
        psd_conn = FALSE;
        ped_notify_psd_state(psd_conn);
    }

    // Need to update all policies when we reconnect to PSD.
    need_policy_update = TRUE;
}


/**
 * Get the state of PSD connection.
 * 
 * @return
 *      state of psd connection T=up, F=down
 */
boolean
psd_conn_state(void)
{
    return psd_conn;
}


/**
 * Get the number of seconds since that the connection to the PSD has been up
 * 
 * @return
 *      the number of seconds since that the connection to the PSD has been up
 * 
 */
int
get_psd_conn_time(void)
{
    struct timespec psd_time;

    clock_gettime(CLOCK_REALTIME, &psd_time);

    junos_trace(PED_TRACEFLAG_NORMAL, "%s: cur: %d, start %d",
            __func__, psd_time.tv_sec, psd_conn_time.tv_sec);
    return(psd_time.tv_sec - psd_conn_time.tv_sec);
}

/*
 * $Id: ped_ssd.c 366969 2010-03-09 15:30:13Z taoliu $
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
 * @file ped_ssd.c
 * 
 * @brief Routines related to talking to SDK Service Daemon 
 * 
 * Functions for connecting to SSD and adding or deleting route. 
 * 
 */
#include <sync/common.h>
#include <jnx/ssd_ipc.h>
#include <jnx/ssd_ipc_msg.h>
#include <limits.h>
#include "ped_ssd.h"

#include PE_OUT_H
#include PE_SEQUENCE_H

/*
 * README:
 * 
 * Interacting with SSD to add/remove route involves FIVE 
 * setup steps noted in the code below as STEP #0 - 4
 */


/*** Constants ***/

#define RT_TBL_NAME         "inet.0" ///< routing table we wish to add routes to

/**
 * Number of requests we accept without responses back from SSD
 */
#define REQUESTS_BUFFER_SIZE 64

/**
 * The client context to give to libssd when a request is to be unconfirmed
 * (which only applies to deletes herein)
 */
#define UNCONFIRMED_CONTEXT  REQUESTS_BUFFER_SIZE

/**
 * Max value for next_unconfirmed where upon we reset it to UNCONFIRMED_CONTEXT
 */
#define MAX_UNCONFIRMED_CONTEXT  UINT_MAX

/**
 * Retry connecting to SSD every RETRY_CONNECT seconds until connection is made
 */
#define RETRY_CONNECT 60

/*** Data structures ***/


static boolean ssd_ready = FALSE;   ///< The state of connection.
static int ssd_server_fd = -1;      ///< socket to SSD
static int client_id = 0;           ///< client id with SSD
static uint32_t rt_tbl_id = 0;     ///< routing instance to which we add routes
static evTimerID  timer_id;          ///< timer ID for retrying connection to SSD

// Need these forward declarations for ssd_client_ft, the function table:

static int ssd_client_connection(int fd);
static void ssd_client_connection_close(int fd, int cause);
static void ssd_client_msg(int fd, struct ssd_ipc_msg *cmsg);

/**
 * @brief the function table for SSD callbacks
 */
static struct ssd_ipc_ft ssd_client_ft = {
    ssd_client_connection,           // init connection handler
    ssd_client_connection_close,     // close connection handler
    ssd_client_msg                   // message handler
};

extern evContext ped_ctx;   ///< Event context for ped

/**
 * Structure to hold callback and user data to notify the 
 * client when the SSD route add or delete has succeeded or failed
 */
typedef struct {
    ssd_reply_notification_handler reply_handler; ///< callback
    void * user_data;   ///< user data to pass to callback
} ssd_request_t;

static ssd_request_t ssd_requests[REQUESTS_BUFFER_SIZE]; ///< buffer of requests
static uint32_t next_buffer = 0; ///< index of next buffer to use up for a request
static uint32_t next_unconfirmed = UNCONFIRMED_CONTEXT; ///< number of requests outstanding in buffer
static int outstanding_requests = 0; ///< number of requests outstanding in buffer 


/*** STATIC/INTERNAL Functions ***/


/**
 * Setup the connection to SSD.
 * This also sets the function table of callbacks to use between
 * ped and libssd. We periodically call this until the connection setup
 * is successful.
 *
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
connect_ssd(evContext ctx __unused,
            void * uap  __unused,
            struct timespec due __unused,
            struct timespec inter __unused)
{
    int rt_table_id;

    if(ssd_server_fd >= 0) {
        return;
    }

    // STEP #0 

    rt_table_id = ssd_get_routing_instance();
    ssd_server_fd = ssd_ipc_connect_rt_tbl(NULL, &ssd_client_ft,
                                     rt_table_id, ped_ctx);
    
    if (ssd_server_fd < 0) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Opening socket to SSD failed! Will retry.", __func__);
        return;
    }
    
    evClearTimer(ped_ctx, timer_id);
    
    junos_trace(PED_TRACEFLAG_ROUTING, "%s: Connected to SSD", __func__);
}


/**
 * The callback function called after first connecting to SSD.
 * We also ask SSD for a client ID.
 */
static int
ssd_client_connection(int fd)
{
    // STEP #0 (finished)
    
    if(ssd_get_client_id(fd, 0) < 0) {  // STEP #1
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Error requesting client ID from SSD %m",
            __func__);
        
        return -1;
    }

    junos_trace(PED_TRACEFLAG_ROUTING,
            "%s: SSD connected.", __func__);
    
    return SUCCESS;
}


/**
 * The callback function to handle the connection close from SSD
 * 
 * @param[in] fd
 *     SSD server socket
 *
 * @param[in] cause
 *     The cause of the connection close
 */
static void
ssd_client_connection_close(int fd __unused, int cause __unused)
{
    ssd_server_fd = -1;
    ssd_ready = FALSE;
    
    /* TRY: RE-CONNECT
     * Set the SSD connect retry timer:
     * immediate one-shot event now, and periodocally afterward every 
     * RETRY_CONNECT seconds
     * 
     * calls connect_ssd()
     */
    if(evSetTimer(ped_ctx, connect_ssd, NULL, evNowTime(),
        evConsTime(RETRY_CONNECT, 0), &timer_id)) {

        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: evSetTimer() FAILED! Will not be able to connect "
                "to the SSD", __func__);
    }
    
    junos_trace(PED_TRACEFLAG_ROUTING,
            "%s: SSD disconnected.", __func__);

    junos_trace(PED_TRACEFLAG_ROUTING, 
        "PED's SSD module is NOT accepting "
        "route add/delete requests");
}


/**
 * The callback function to handle getting messages from SSD
 *
 * @param[in] fd
 *     SSD server fd.
 *
 * @param[in] cmsg
 *     The pointer to the message from SSD.
 */
static void
ssd_client_msg (int fd, struct ssd_ipc_msg * cmsg)
{
    char * rt_tb_name = NULL;
    unsigned int context;
    
    switch (cmsg->ssd_ipc_cmd) {

    case SSD_SESSION_ID_REPLY:
    
        if(SSD_SESSION_ID_REPLY_CHECK_STATUS(cmsg)) {
            
            client_id = SSD_SESSION_ID_REPLY_GET_CLIENT_ID(cmsg); // STEP #1 (finished)
            
            junos_trace(PED_TRACEFLAG_ROUTING, 
                    "%s: Received client id from SSD : %u",
                    __func__, client_id);
            
            // Requesting route service setup...
            
            if(ssd_setup_route_service(fd) < 0) {  // STEP #2
                ERRMSG(PED, TRACE_LOG_ERR, "%s: Error requesting route "
                    "service setup from SSD %m", __func__);
            }
        } else {
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Error getting client ID from SSD", __func__);
        }
        break;
        
    case SSD_ROUTE_SERVICE_REPLY:
    
        if(SSD_ROUTE_SERVICE_REPLY_CHECK_STATUS(cmsg)) {
            
            rt_tb_name = strdup(RT_TBL_NAME); // STEP #2 (finished)
            INSIST(rt_tb_name != NULL); // errno=ENOMEM if this fails
            
            junos_trace(PED_TRACEFLAG_ROUTING,
                "%s: Route service has been setup properly",
                 __func__);
                 
            // Requesting for a route table id...
            
            if(ssd_request_route_table_lookup(
                fd, rt_tb_name, 0) < 0) {  // STEP #3

                ERRMSG(PED, TRACE_LOG_ERR, "%s: Error requesting route "
                    "table lookup from SSD %m", __func__);
            }
        } else {
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Error setting up route service with SSD", __func__);
        }
        break;
        
    case SSD_ROUTE_TABLE_LOOKUP_REPLY:
    
        if(SSD_ROUTE_TABLE_LOOKUP_REPLY_CHECK_STATUS(cmsg)) {
            
            rt_tbl_id = SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_RTTID(cmsg); // STEP #3 (finished)
            
            junos_trace(PED_TRACEFLAG_ROUTING, 
                "%s: Route table is found id %u",
                __func__, rt_tbl_id);
            
            // Our SSD module is ready to accept route add/delete requests
            
            ssd_ready = TRUE;                     // STEP #4
            
            junos_trace(PED_TRACEFLAG_ROUTING, 
                "PED's SSD module is ready to accept "
                "route add/delete requests");
        } else {
            ERRMSG(PED, TRACE_LOG_ERR, 
                "%s: Error getting route table from SSD %d",
                __func__, SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_ERROR_CODE(cmsg));
        }
        break;
        
    case SSD_ROUTE_ADD_REPLY:

        context = SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(cmsg);
        
        // we only have issued contexts in this range
        INSIST(context < REQUESTS_BUFFER_SIZE);
                
        if(SSD_ROUTE_ADD_REPLY_CHECK_STATUS(cmsg)) {
            junos_trace(PED_TRACEFLAG_ROUTING, 
                    "%s: Route add success", __func__);
                    
            ssd_requests[context].reply_handler(
                ADD_SUCCESS, ssd_requests[context].user_data);
            
        } else {
            junos_trace(PED_TRACEFLAG_ROUTING, 
                    "%s: Route add failed", __func__);
                    
            ssd_requests[context].reply_handler(
                ADD_FAILED, ssd_requests[context].user_data);
        }
        
        // free up this request buffer
        ssd_requests[context].reply_handler = NULL;
        ssd_requests[context].user_data = NULL;
        --outstanding_requests;
        
        break;
        
    case SSD_ROUTE_DELETE_REPLY:
    
        context = SSD_ROUTE_DELETE_REPLY_GET_CLIENT_CTX(cmsg);
    
        if(SSD_ROUTE_DELETE_REPLY_CHECK_STATUS(cmsg)) {

            if(context < REQUESTS_BUFFER_SIZE) { // gotta confirm
                
                junos_trace(PED_TRACEFLAG_ROUTING, "%s: Route delete success "
                    "(with confirmation)", __func__);
                
                // call confirmation handler
                ssd_requests[context].reply_handler(
                    DELETE_SUCCESS, ssd_requests[context].user_data);
                    
                // free up this request buffer
                ssd_requests[context].reply_handler = NULL;
                ssd_requests[context].user_data = NULL;
                --outstanding_requests;
                
            } else {
                junos_trace(PED_TRACEFLAG_ROUTING, "%s: Route delete success "
                    "(without confirmation)", __func__);                
            }
        } else {
            if(context < REQUESTS_BUFFER_SIZE) { // gotta confirm
                
                junos_trace(PED_TRACEFLAG_ROUTING, "%s: Route delete failed "
                    "(with confirmation)", __func__);                
                
                // call confirmation handler
                ssd_requests[context].reply_handler(
                    DELETE_FAILED, ssd_requests[context].user_data);
                    
                // free up this request buffer
                ssd_requests[context].reply_handler = NULL;
                ssd_requests[context].user_data = NULL;
                --outstanding_requests;
                
            } else {
                ERRMSG(PED, TRACE_LOG_ERR, 
                    "%s: Route delete failed (without confirmation)."
                    "WARNING: dangling route.", __func__);
            }
        }
        
        break;
        
    case SSD_ROUTE_SERVICE_CLEAN_UP_REPLY:
        // only issued clean-up while shutting down anyway
        break;
        
    default:
    
        ERRMSG(PED, TRACE_LOG_ERR, "%s: Unknown message type %d",
            __func__, cmsg->ssd_ipc_args[0]);
            
    }
}


/**
 * A callback/handler passed into junos_kcom_ifa_get_all 
 * from ssd_client_add_route_request
 * 
 * @param[in] msg
 *     message with ifa info
 *
 * @param[in] user_info
 *     user data passed to junos_kcom_ifa_get_all
 * 
 * @return SUCCESS or 1 if there's an SSD issue
 */
static int
ssd_client_ifa_handler(kcom_ifa_t * msg, void * user_info )
{
    policy_route_msg_t *route = (policy_route_msg_t *)user_info;
    ssd_sockaddr_un route_addr;
    ssd_sockaddr_un route_nh;
    ssd_sockaddr_un route_local;
    struct ssd_route_parms rtp;
    struct in_addr  addr;

    // Initialize the information for route add
    
    bzero(&route_addr, sizeof(route_addr));
    ssd_setsocktype(&route_addr, SSD_GF_INET);
    route_addr.in.gin_addr = route->route_addr;

    bzero(&route_nh, sizeof(route_nh));
    ssd_setsocktype(&route_nh, SSD_GF_INET);
    route_nh.in.gin_addr = route->nh_addr;

    bzero(&route_local, sizeof(route_local));
    ssd_setsocktype(&route_local, SSD_GF_INET);
    route_local.in.gin_addr = *(in_addr_t *)(msg->ifa_lprefix);

    // Setup route parameters for SSD
    
    bzero(&rtp, sizeof(rtp));
    rtp.rta_dest = &route_addr;
    rtp.rta_prefixlen = route->route_addr_prefix;
    
    if(route->preferences) {
        rtp.rta_preferences.rtm_val[0] = route->preferences;
        rtp.rta_preferences.rtm_type[0] = SSD_RTM_VALUE_PRESENT;
    }
    
    if(route->metrics) {
        rtp.rta_metrics.rtm_val[0] = route->metrics;
        rtp.rta_metrics.rtm_type[0] = SSD_RTM_VALUE_PRESENT;
    }
    
    
    rtp.rta_rtt = rt_tbl_id;
    rtp.rta_nhtype = route->nh_type;
    if(route->nh_addr) {
        rtp.rta_n_gw = 1;
        rtp.rta_gateway[0].rtg_gwaddr = &route_nh;
        rtp.rta_gateway[0].rtg_ifl = msg->ifa_index;
        rtp.rta_gateway[0].rtg_lcladdr = &route_local;
    }

    addr.s_addr = route->route_addr;
    
    junos_trace(PED_TRACEFLAG_ROUTING,
            "%s: route address: %s/%d", __func__,
            inet_ntoa(addr), route->route_addr_prefix);

    addr.s_addr = *(in_addr_t *)(msg->ifa_lprefix);
    
    junos_trace(PED_TRACEFLAG_ROUTING,
            "%s: local address: %s, ifl: %d", __func__,
            inet_ntoa(addr), msg->ifa_index);
            
    addr.s_addr = route->nh_addr;
    
    junos_trace(PED_TRACEFLAG_ROUTING,
            "%s: nh: %s, m: %d, p: %d", __func__,
            inet_ntoa(addr), route->metrics, route->preferences);

    // Add the route
    if(ssd_request_route_add(ssd_server_fd, &rtp, next_buffer) < 0) {

        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Error requesting route add from SSD: %m", __func__);
        
        junos_kcom_msg_free(msg);
        
        return 1; // not SUCCESS
    }
    
    junos_kcom_msg_free(msg);
    
    return SUCCESS;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize client
 */
void
ped_ssd_init(void)
{
    int i = 0;
    
    ssd_server_fd = -1;
    client_id = 0;
    rt_tbl_id = 0;
    next_buffer = 0;
    next_unconfirmed = UNCONFIRMED_CONTEXT;
    outstanding_requests = 0;
    
    for(; i < REQUESTS_BUFFER_SIZE; ++i) {
        ssd_requests[i].reply_handler = NULL;
        ssd_requests[i].user_data = NULL;        
    }
    
    /*
     * Set the SSD connect retry timer:
     * immediate one-shot event now, and periodocally afterward every 
     * RETRY_CONNECT seconds
     * 
     * calls connect_ssd()
     */
    if(evSetTimer(ped_ctx, connect_ssd, NULL, evNowTime(),
        evConsTime(RETRY_CONNECT, 0), &timer_id)) {

        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: evSetTimer() FAILED! Will not be able to connect "
                "to the SSD", __func__);
    }
}


/**
 * Shutdown client
 */
void
ped_ssd_shutdown(void)
{
    if(ssd_server_fd != -1) {
        ssd_ipc_close(ssd_server_fd);
    }
}


/**
 * Check the state of SSD connection.
 * 
 * @return
 *      TRUE if connected to SSD, otherwise FALSE.
 */
boolean
get_ssd_ready(void)
{
    return ssd_ready;
}


/**
 * Are there any outstanding requests to SSD for which we soon expect replies
 * 
 * @return
 *      TRUE if there are NOT outstanding requests;
 *      FALSE if there are outstanding requests
 */
boolean
get_ssd_idle(void)
{
    return outstanding_requests < 1; // should be zero obviously
}


/**
 * Send message to SSD to add a route.
 * 
 * @param[in] route
 *      Pointer to route data.
 * 
 * @param[in] func
 *      function to callback when result is available
 * 
 * @param[in] user_data
 *      user data to echo in callback
 * 
 * @return TRUE if request was accepted;
 * FALSE if not due to no more buffers to store the request,
 *       or a KCOM problem, or ssd_request_route_add fails
 */
boolean
ssd_client_add_route_request(
        policy_route_msg_t *route,
        ssd_reply_notification_handler func,
        void * user_data)
{
    kcom_ifl_t ifl;
    char * dot;
    if_subunit_t unit = -1;
    long tmp;
    int rc;
    
    INSIST(route != NULL);
    INSIST(ssd_ready);
    INSIST(func != NULL); // adding requires confirmation in our app
    
    // check if there anything still in this spot of the buffers
    if(ssd_requests[next_buffer].reply_handler != NULL) {
        
        // yes there is
        
        ERRMSG(PED, TRACE_LOG_WARNING,
                "%s: Warning: PED's ssd module has run "
                "out of space to hold more requests! Either it "
                "is being flooded or SSD responses are very delayed!",
                __func__);
                
        return FALSE;
    }
    
    // Add this request in the circular buffer of requests
    ssd_requests[next_buffer].reply_handler = func;
    ssd_requests[next_buffer].user_data = user_data;

    junos_trace(PED_TRACEFLAG_ROUTING,
            "%s: Adding route", __func__);

    // Remove unit number from interface name.
    
    if((dot = strchr(route->ifname, '.')) != NULL) {
        *dot = '\0'; // now terminate here
        unit = ((tmp = atol(++dot)) < 1) ? 0 : (if_subunit_t)tmp; // set unit
    } else {
        unit = 0;
    }

    if(junos_kcom_ifl_get_by_name(route->ifname, unit, &ifl)) { // KCOM problem
        
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Get ifl by name FAILED!", __func__);

        ssd_requests[next_buffer].reply_handler = NULL;
        ssd_requests[next_buffer].user_data = NULL;

        return FALSE;
    }

    if((rc = junos_kcom_ifa_get_all(ssd_client_ifa_handler,
                ifl.ifl_index, route->af, route)) > SUCCESS) {
        
        ssd_requests[next_buffer].reply_handler = NULL;
        ssd_requests[next_buffer].user_data = NULL;

        return FALSE;
        
    } else if(rc < SUCCESS) { // KCOM problem
        
        ssd_requests[next_buffer].reply_handler = NULL;
        ssd_requests[next_buffer].user_data = NULL;
        
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Could not go through IFAs to add route", __func__);
        
        return FALSE;        
    }

    ++next_buffer;
    ++outstanding_requests;
    
    if(next_buffer >= REQUESTS_BUFFER_SIZE) // wrap around
        next_buffer = 0;

    return TRUE;
}


/**
 * Send message to SSD to delete a route. This should be done with confirmation
 * (func should handle the result). However, if our SSD confirmation-of-request
 * buffers run out, then we return FALSE. In case of this, the caller can try
 * calling the function again with func and user_data both NULL, but beware
 * that no confirmation of the result will be provided, and the function will
 * return TRUE unless libssd fails to accept it.   
 *
 * @param[in] route
 *      Pointer to route data.
 * 
 * @param[in] func
 *      function to callback when result is available. NULL if trying 
 *      to delete without confirmation (caller will never know result
 *      if SSD accepts the reqest)
 * 
 * @param[in] user_data
 *      user data to echo in callback
 * 
 * @return TRUE if request was accepted;
 * FALSE if not due to no more buffers to store the request,
 *       or a KCOM problem, or ssd_request_route_delete fails
 */
boolean
ssd_client_del_route_request(
        policy_route_msg_t *route,
        ssd_reply_notification_handler func,
        void * user_data)
{
    ssd_sockaddr_un route_addr;
    struct ssd_rt_delete_parms rtp;
    struct in_addr  addr;
    
    INSIST(route != NULL);
    INSIST(ssd_ready);
    // if func is NULL then user_data must be:
    INSIST(func != NULL || user_data == NULL);
    
    
    addr.s_addr = route->route_addr;
    
    junos_trace(PED_TRACEFLAG_ROUTING,
            "%s: deleting route with address: %s/%d", __func__,
            inet_ntoa(addr), route->route_addr_prefix);

    bzero(&route_addr, sizeof(route_addr));
    ssd_setsocktype(&route_addr, SSD_GF_INET);
    route_addr.in.gin_addr = route->route_addr;

    rtp.rtd_dest = &route_addr;
    rtp.rtd_prefixlen = route->route_addr_prefix;
    rtp.rtd_rtt = rt_tbl_id;
    
    
    if(func != NULL) { //requesting confirmation
        
        // check if there anything still in this spot of the buffers    
        if(ssd_requests[next_buffer].reply_handler != NULL) {
        
            // yes there is
            
            ERRMSG(PED, TRACE_LOG_WARNING,
                "%s: Warning: PED's ssd module has run "
                "out of space to hold more requests! Either it "
                "is being flooded or SSD responses are very delayed!",
                __func__);
    
            return FALSE;
        }
        
        // Add this request in the circular buffer of requests
        ssd_requests[next_buffer].reply_handler = func;
        ssd_requests[next_buffer].user_data = user_data;
        
        // send request to libssd
        if(ssd_request_route_delete(ssd_server_fd, &rtp, next_buffer)) {
            
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Error requesting route delete from SSD: %m", __func__);
    
            ssd_requests[next_buffer].reply_handler = NULL;
            ssd_requests[next_buffer].user_data = NULL;
    
            return FALSE;
        }
    
        ++next_buffer;
        ++outstanding_requests;
        
        if(next_buffer >= REQUESTS_BUFFER_SIZE) // wrap around
            next_buffer = 0;
        
    } else {
        if(next_unconfirmed >= MAX_UNCONFIRMED_CONTEXT) {
            next_unconfirmed = UNCONFIRMED_CONTEXT; // wrap around range of type
        }
        
        if(ssd_request_route_delete(ssd_server_fd, &rtp, next_unconfirmed)) {
            
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Error requesting route delete from SSD: %m", __func__);

            return FALSE;
        }
    }
    
    return TRUE;
}


/**
 * Clean up all routes created and shutdown this module
 */
void
clean_routes(void)
{
    int rc;
    
    if(ssd_ready) {
        junos_trace(PED_TRACEFLAG_ROUTING,
            "%s: SSD module is shutting down", __func__);
        
        rc = ssd_clean_up_route_service(ssd_server_fd, MAX_UNCONFIRMED_CONTEXT);
        if(rc == -1) {
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Route service could not issue clean-up due to error: %m",
                __func__);
        }
    }
}

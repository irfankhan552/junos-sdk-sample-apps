/*
 * $Id: ped_service_route.c 437848 2011-04-21 07:35:27Z amudha $
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
 * @file ped_service_route.c
 * 
 * @brief Routines related to adding and deleting service routes
 * 
 * Functions for connecting to SSD stricly for adding and deleting service 
 * routes and next hops.
 */

#include <sync/common.h>
#include <jnx/ssd_ipc.h>
#include <jnx/ssd_ipc_msg.h>
#include "ped_service_route.h"

#include PE_OUT_H
#include PE_SEQUENCE_H

/*
 * README:
 * 
 * Interacting with SSD to add/remove routes involves FIVE 
 * setup steps noted in the code below as STEP #0 - 4
 */


/*** Constants ***/

/**
 * The routing table we wish to add routes to in the default routing instance
 */
#define RT_TBL_NAME "inet.0"

/**
 * The routing table we wish to add routes to in the pfd_forwarding RI
 */
#define PFD_RT_TBL_NAME "pfd_forwarding.inet.0"

/**
 * Retry connecting to SSD every RETRY_CONNECT seconds until connection is made
 */
#define RETRY_CONNECT 60

#define DEFAULT_ROUTE_ADDRESS 0 ///< Default route address
#define DEFAULT_ROUTE_MASK    0 ///< Default route mask
#define FULL_ROUTE_MASK      32 ///< Full route mask

#define SERVICE_ROUTE_PREF   10 ///< Default route preference for service routes

#define INT_NAME_STR_SIZE    64 ///< string size for interface names 

/*** Data structures ***/


static boolean ssd_ready = FALSE;  ///< The state of connection
static int ssd_server_fd;          ///< socket to SSD
static int client_id;              ///< client id with SSD
static int client_nexthop_id;      ///< a counter to provide unique next-hop IDs
static uint32_t client_ctx;        ///< context echoed from SSD in callbacks
static uint32_t rt_tbl_id;         ///< routing instance to which we add routes
static uint32_t pfd_rt_tbl_id;     ///< PFD RI to which we add routes
static evTimerID  timer_id;        ///< timer ID for retrying connection to SSD

// Need these forward declarations for ssd_client_ft, the function table:

static int serv_rt_client_connection(int fd);
static void serv_rt_client_connection_close(int fd, int cause);
static void serv_rt_client_msg(int fd, struct ssd_ipc_msg *cmsg);

/**
 * @brief the function table for SSD callbacks
 */
static struct ssd_ipc_ft ssd_client_ft = {
    serv_rt_client_connection,           ///< init connection handler
    serv_rt_client_connection_close,     ///< close connection handler
    serv_rt_client_msg                   ///< message handler
};

/**
 * Information we store for each route
 */
typedef struct route_info_s {
    uint32_t  rt_table_id;  ///< which route table this route pertains to
    uint32_t  client_ctx;   ///< Last client context used in an action on route
    int       client_nh_id; ///< The unique id given with the next-hop
    ifl_idx_t ifl_index;    ///< IFL index
    nh_idx_t  nh_index;     ///< NH index
    in_addr_t address;      ///< route address
    in_addr_t mask;         ///< route mask
    TAILQ_ENTRY(route_info_s) entries;
} route_info_t;

/**
 * List of route_info of service route under management
 */
static TAILQ_HEAD(route_list_s, route_info_s) route_list = 
            TAILQ_HEAD_INITIALIZER(route_list);


enum action {
    ADD_PFD_ROUTE,         ///< Add a PFD service route
    ADD_NORMAL_ROUTE,      ///< Add a normal service route
    DELETE_PFD_ROUTE,      ///< Delete a PFD service route
    DELETE_NORMAL_ROUTE    ///< Delete a normal service route
};

/**
 * Information we store when we must buffer request until ssd_ready=TRUE
 */
typedef struct request_info_s {
    enum action req_action;                 ///< requested action
    char interface_name[INT_NAME_STR_SIZE]; ///< interface name
    in_addr_t address;                      ///< route address
    TAILQ_ENTRY(request_info_s) entries;    ///< ptrs to next/prev
} request_info_t;


/**
 * List of request to buffer in case they're made before ssd_ready=TRUE
 */
static TAILQ_HEAD(request_buffer_s, request_info_s) requests = 
            TAILQ_HEAD_INITIALIZER(requests);


/*** STATIC/INTERNAL Functions ***/


/**
 * Setup the connection to SSD.
 * This also sets the function table of callbacks to use between
 * ped and libssd. We periodically call this until the connection setup
 * is successful.
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
connect_ssd(evContext ctx,
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
                                     rt_table_id, ctx);
    
    if (ssd_server_fd < 0) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s%s: Opening socket to the SSD failed! Will retry.",
                __func__, __FILE__);
        return;
    }
    
    evClearTimer(ctx, timer_id);
    
    junos_trace(PED_TRACEFLAG_ROUTING,
        "%s: Service route module connected to the SSD", __func__);
}


/**
 * Get the route under manager by client context id
 * 
 * @param[in] client_context
 *      context used in lookup
 * 
 * @return 
 *      A pointer the the route_info_t or NULL if not found
 */
static route_info_t *
get_route_by_ctx(uint32_t client_context)
{
    route_info_t * route_info = TAILQ_FIRST(&route_list);
    
    while(route_info) {
        if(client_context == route_info->client_ctx) { // found it
            return route_info;
        }
        route_info = TAILQ_NEXT(route_info, entries);
    }
    
    return NULL;
}


/**
 * Get the IFL index of an interface
 *  
 * @param[in] interface_name
 *      The IFL formatted interface name for which the IFL index is desired
 * 
 * @param[out] ifl_index
 *      The IFL index structure that where the IFL index will be stored
 * 
 * @return
 *      SUCCESS or EFAIL on parsing error (logged to syslog)
 */
static status_t
get_ifl_index(char * interface_name, ifl_idx_t * ifl_index)
{
    kcom_ifl_t ifl_info;
    char * dot;
    int rc;
    
    // Parse the interface name for the IFD name and the logical unit #
    
    if((dot = strchr(interface_name, '.')) == NULL) {
        ERRMSG(PED, TRACE_LOG_ERR, "%s: Cannot parse interface name",
            __func__);
        return EFAIL;
    }
    
    *dot = '\0'; // separate strings at the dot
    ++dot; // move to the start of next string indicating the logical unit #
    
    // Get the IFL index usign KCOM
    rc = junos_kcom_ifl_get_by_name(
        interface_name, (if_subunit_t)atoi(dot), &ifl_info);
    
    // restore dot
    --dot;
    *dot = '.';

    if (rc != KCOM_OK) {
        return ENOENT;
    }
    
    ifl_idx_t_set_equal(*ifl_index, ifl_info.ifl_index);
    
    return SUCCESS;
}


/**
 * Add a service route given that the next-hop is already added
 * 
 * @param[in] route_info
 *      Information about the route and next-hop
 */
static void
add_service_route_helper(route_info_t * route_info)
{
    struct ssd_route_parms rtp;
    ssd_sockaddr_un route_addr;
    int rc;

    /* Initialize the information for route add  */
    bzero(&route_addr, sizeof(route_addr));
    ssd_setsocktype(&route_addr, SSD_GF_INET);
    route_addr.in.gin_addr = route_info->address;

    bzero(&rtp, sizeof(rtp));
    rtp.rta_dest = &route_addr;
    rtp.rta_prefixlen = route_info->mask;
    rtp.rta_nhtype = SSD_RNH_SERVICE; // flag it as a service route
    rtp.rta_preferences.rtm_val[0] = SERVICE_ROUTE_PREF;
    rtp.rta_preferences.rtm_type[0] = SSD_RTM_VALUE_PRESENT;
    rtp.rta_rtt = route_info->rt_table_id;
    rtp.rta_gw_handle = 0;
    rtp.rta_n_gw = 1;
    rtp.rta_gateway[0].rtg_ifl = route_info->ifl_index;
    rtp.rta_gateway[0].rtg_nhindex = route_info->nh_index;

    rc = ssd_request_route_add(ssd_server_fd, &rtp, ++client_ctx);
    
    if(rc == -1) {
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Failed to request service route addition: %m", __func__);
    }
    
    route_info->client_ctx = client_ctx;
}


/**
 * Process all requests that have been buffered now that ssd_ready = TRUE
 */
static void
process_requests(void)
{
    request_info_t * req;
    
    while((req = TAILQ_FIRST(&requests))) {
        switch(req->req_action) {
            case ADD_PFD_ROUTE:
                add_pfd_service_route(req->interface_name);
                break;
            case DELETE_PFD_ROUTE:
                delete_pfd_service_route(req->interface_name);
                break;
            case ADD_NORMAL_ROUTE:
                add_service_route(req->interface_name, req->address);
                break;
            case DELETE_NORMAL_ROUTE:
                delete_service_route(req->interface_name, req->address);
                break;
        }
        TAILQ_REMOVE(&requests, req, entries);
        free(req);
    }
}


/**
 * The callback function called after first connecting to SSD.
 * We also ask SSD for a client ID.
 */
static int
serv_rt_client_connection(int fd)
{
    // STEP #0 (finished)
    
    if(ssd_get_client_id(fd, 0) < 0) {  // STEP #1
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Service route module ERROR requesting "
            "client ID from the SSD %m", __func__);
        
        return -1;
    }

    junos_trace(PED_TRACEFLAG_ROUTING,
            "%s: SSD acknowledged connection.", __func__);
    
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
serv_rt_client_connection_close(int fd __unused, int cause __unused)
{
    ssd_server_fd = -1;
    ssd_ready = FALSE;
    
    junos_trace(PED_TRACEFLAG_ROUTING, "%s: SSD disconnected from "
        "the service route module.", __func__);

    junos_trace(PED_TRACEFLAG_ROUTING, 
       "PED's Service route module is NOT accepting route add/delete requests");
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
serv_rt_client_msg (int fd, struct ssd_ipc_msg * cmsg)
{
    char * rt_tb_name = NULL;
    route_info_t * route_info;
    
    switch (cmsg->ssd_ipc_cmd) {

    case SSD_SESSION_ID_REPLY:
    
        if(SSD_SESSION_ID_REPLY_CHECK_STATUS(cmsg)) {
            
            client_id = SSD_SESSION_ID_REPLY_GET_CLIENT_ID(cmsg); // STEP #1 (finished)
            
            junos_trace(PED_TRACEFLAG_ROUTING, 
                    "%s: Received client id from SSD : %u",
                    __func__, client_id);
            
            // Requesting route service setup...
            
            if(ssd_setup_route_service(fd) < 0) {  // STEP #2
                ERRMSG(PED, TRACE_LOG_ERR,
                    "%s: Error requesting route service setup from SSD %m",
                    __func__);
            }
        }
        
        break;
        
    case SSD_ROUTE_SERVICE_REPLY:
    
        if(SSD_ROUTE_SERVICE_REPLY_CHECK_STATUS(cmsg)) {
            
            junos_trace(PED_TRACEFLAG_ROUTING, // STEP #2 (finished)
                "%s: Route service has been setup properly",
                 __func__);
            
            if(rt_tbl_id && pfd_rt_tbl_id) {
                // Only falls here after clean route service was performed and
                // we have to re-call ssd_setup_route_service()
                
                // Service route module is ready to accept requests
                
                ssd_ready = TRUE;
                
                junos_trace(PED_TRACEFLAG_ROUTING, 
                    "PED's Service route module is ready to accept route "
                    "add/delete requests again after routes were purged");
                
                process_requests(); // process existing requests
                
                break;
            }
            
            // Request route table IDs for both routing tables       STEP #3
            // that we'll beadding service routes to
            
            rt_tb_name = strdup(RT_TBL_NAME);
            INSIST(rt_tb_name != NULL);
                 
            if(ssd_request_route_table_lookup(fd, rt_tb_name, 0) < 0) {

                ERRMSG(PED, TRACE_LOG_ERR,
                    "%s: Error requesting route table lookup from SSD %m",
                    __func__);
            }
            
            rt_tb_name = strdup(PFD_RT_TBL_NAME);
            INSIST(rt_tb_name != NULL);
                 
            if(ssd_request_route_table_lookup(fd, rt_tb_name, 1) < 0) {

                ERRMSG(PED, TRACE_LOG_ERR,
                    "%s: Error requesting route table lookup from SSD %m",
                    __func__);
            }
        }
        break;
        
    case SSD_ROUTE_TABLE_LOOKUP_REPLY:
    
        if(SSD_ROUTE_TABLE_LOOKUP_REPLY_CHECK_STATUS(cmsg)) {
            
            // STEP #3 (finished)
            // retrieve the routing table IDs from the message
            
            if(SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_CLIENT_CTX(cmsg) == 0) {
                rt_tbl_id = SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_RTTID(cmsg);
                
                junos_trace(PED_TRACEFLAG_ROUTING, "%s: Route table "
                    "found, id: %u", __func__, rt_tbl_id);
                
            } else if(SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_CLIENT_CTX(cmsg) == 1) {
                pfd_rt_tbl_id = SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_RTTID(cmsg);
                
                junos_trace(PED_TRACEFLAG_ROUTING, "%s: Route table "
                    "found, id: %u", __func__, pfd_rt_tbl_id);
            } else {
                ERRMSG(PED, TRACE_LOG_ERR, "%s: Received a route table"
                    " ID for an unknown route table", __func__);
            }
            
            if(rt_tbl_id && pfd_rt_tbl_id) { // if we've got both back
                // Service route module is ready to accept requests
                
                ssd_ready = TRUE;                     // STEP #4
                
                junos_trace(PED_TRACEFLAG_ROUTING, 
                    "PED's Service route module is ready to accept "
                    "route add/delete requests");
                
                process_requests(); // process existing requests
            }
        } else {
            if(SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_CLIENT_CTX(cmsg) == 1) {
                ERRMSG(PED, TRACE_LOG_WARNING, "%s: Route table ID "
                    "lookup for %s has failed. The service route module is not "
                    "available and is waiting for notification to try the "
                    "request again.", __func__, PFD_RT_TBL_NAME);
            } else {
                ERRMSG(PED, TRACE_LOG_ERR, "%s: Route table ID lookup "
                    "for %s has failed. The service route module will be "
                    "offline.", __func__, RT_TBL_NAME);
            }
        }
        
        break;

    case SSD_NH_ADD_REPLY:
        
        route_info =
            get_route_by_ctx(SSD_NEXTHOP_ADD_REPLY_GET_CLIENT_CTX(cmsg));
        
        if (SSD_NEXTHOP_ADD_REPLY_CHECK_STATUS(cmsg)) {
            
            if(route_info) {
                
                // SAVE the next-hop id and ADD the route
                
                junos_trace(PED_TRACEFLAG_ROUTING, 
                    "%s: Successfully added next-hop, adding service route",
                    __func__);
                
                nh_idx_t_setval(route_info->nh_index,
                        SSD_NEXTHOP_ADD_REPLY_GET_NH_ID(cmsg));
                
                add_service_route_helper(route_info);
                
            } else {
                ERRMSG(PED, TRACE_LOG_ERR, "%s: Couldn't find route "
                    "to add after next-hop add succeeded.", __func__);
            }
        } else {
            
            ERRMSG(PED, TRACE_LOG_ERR, "%s: Failed to add next-hop",
                __func__);
            
            // Take it out of the list
            
            if(route_info) {
                TAILQ_REMOVE(&route_list, route_info, entries);
                free(route_info);
            } else {
                ERRMSG(PED, TRACE_LOG_ERR, "%s: Couldn't find route "
                   "to remove after next-hop add failed.", __func__);
            }
        }
        break;
        
    case SSD_ROUTE_ADD_REPLY:

        if(SSD_ROUTE_ADD_REPLY_CHECK_STATUS(cmsg)) {
            junos_trace(PED_TRACEFLAG_ROUTING, 
                    "%s: Successfully added service route", __func__);
        } else {
            ERRMSG(PED, TRACE_LOG_ERR, 
                    "%s: Failed to add service route", __func__);

            // Delete the next-hop associated with it
            
            route_info =
                get_route_by_ctx(SSD_NEXTHOP_ADD_REPLY_GET_CLIENT_CTX(cmsg));
            
            if(route_info) {
                ssd_request_nexthop_delete(ssd_server_fd,
                    route_info->client_nh_id, route_info->nh_index,
                    ++client_ctx);
                
                route_info->client_ctx = client_ctx;
            } else {
                ERRMSG(PED, TRACE_LOG_ERR, "%s: Couldn't find route "
                   "to delete next-hop after route add failed.", __func__);
            }
        }
        
        break;
        
    case SSD_ROUTE_DELETE_REPLY:
    
        if (SSD_ROUTE_DELETE_REPLY_CHECK_STATUS(cmsg)) {
            junos_trace(PED_TRACEFLAG_ROUTING, "%s: Successfully deleted " 
                "service route, removing next hop", __func__);  
        } else {
            ERRMSG(PED, TRACE_LOG_ERR, "%s: Failed to delete service "
                "route, removing next hop", __func__);  
        }

        route_info =
            get_route_by_ctx(SSD_NEXTHOP_ADD_REPLY_GET_CLIENT_CTX(cmsg));
        
        // Delete the next-hop associated with it
        
        if(route_info) {
            ssd_request_nexthop_delete(ssd_server_fd,
                route_info->client_nh_id, route_info->nh_index,
                ++client_ctx);
            
            route_info->client_ctx = client_ctx;
        } else {
            ERRMSG(PED, TRACE_LOG_ERR, "%s: Could not find route in "
                "list to delete the next hop", __func__);
        }
        
        break;
        
    case SSD_NH_DELETE_REPLY:
        
        if (SSD_NEXTHOP_DELETE_REPLY_CHECK_STATUS(cmsg)) {
            junos_trace(PED_TRACEFLAG_ROUTING, 
                "%s: Successfully deleted next-hop", __func__);  
        } else {
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Failed to delete next-hop", __func__);  
        }
        
        // Remove route from list
        
        route_info =
            get_route_by_ctx(SSD_NEXTHOP_ADD_REPLY_GET_CLIENT_CTX(cmsg));
        
        if(route_info) {
            junos_trace(PED_TRACEFLAG_ROUTING, 
                "%s: Successfully deleted route from management", __func__);
            
            TAILQ_REMOVE(&route_list, route_info, entries);
            free(route_info);
        } else {
            ERRMSG(PED, TRACE_LOG_ERR, "%s: Could not find route to "
                "remove in list", __func__);
        }
        
        break;
        
    case SSD_ROUTE_SERVICE_CLEAN_UP_REPLY:
        
        if (SSD_ROUTE_SERVICE_CLEAN_UP_REPLY_CHECK_STATUS(cmsg)) {
            junos_trace(PED_TRACEFLAG_ROUTING, 
                "%s: Route service cleanup succeeded", __func__);  
        } else {
            junos_trace(PED_TRACEFLAG_ROUTING,
                "%s: Route service cleanup failure", __func__);  
        }
        
        // Requesting route service setup...
        
        if(ssd_setup_route_service(fd) < 0) {
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Error requesting route service setup from SSD %m",
                __func__);
        }
        
        break;

    default:
        ERRMSG(PED, TRACE_LOG_ERR, "%s: Unknown message type %d",
            __func__, cmsg->ssd_ipc_args[0]);
    }
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize client
 * 
 * @param[in] ctx
 *     The event context for this application
 */
void
service_route_init(evContext ctx)
{
    ssd_server_fd = -1;
    client_id = -1;
    client_ctx = 2; // we use 0 and 1 in route table lookups
    rt_tbl_id = 0;
    pfd_rt_tbl_id = 0;
    client_nexthop_id = 0;

    /*
     * Set the SSD connect retry timer:
     * immediate one-shot event now, and periodocally afterward every 
     * RETRY_CONNECT seconds
     * 
     * calls connect_ssd()
     */
    if(evSetTimer(ctx, connect_ssd, NULL, evNowTime(),
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
service_route_shutdown(void)
{
    if(ssd_server_fd != -1) {
        ssd_ipc_close(ssd_server_fd);
    }
}


/**
 * If the module is not ready it is liely because the PFD route table doesn't
 * exist and we haven't been able to get its table ID. If another module knows
 * that the RI has been created, then we try getting it again.
 */
void
pfd_ri_created(void)
{
    char * rt_tb_name;
    
    if(client_id == -1) { // haven't got a connection to the SSD yet 
        return;
    }
    
    junos_trace(PED_TRACEFLAG_ROUTING, "%s: trying to get the route table ID "
        "for %s again.", __func__, PFD_RT_TBL_NAME);
    
    if(!ssd_ready) {
        rt_tb_name = strdup(PFD_RT_TBL_NAME);
        INSIST(rt_tb_name != NULL);
             
        if(ssd_request_route_table_lookup(ssd_server_fd, rt_tb_name, 1) < 0) {
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Error requesting route table lookup from SSD %m",
                __func__);
        }
    }
}


/**
 * Check the state
 * 
 * @return
 *      TRUE if connected to SSD and ready to accept requests, otherwise FALSE
 */
boolean
get_serviceroute_ready(void)
{
    return ssd_ready;
}


/**
 * Adds a default service route in the pfd_forwarding routing instance
 * 
 * @param[in] interface_name
 *      The name of the interface in the pfd_forwarding routing instance that 
 *      the PFD uses
 */
void
add_pfd_service_route(char * interface_name)
{
    route_info_t * route_info;
    request_info_t * req;
    struct ssd_nh_add_parms nh_params;
    int rc;
    
    INSIST(interface_name != NULL);
    
    if(!ssd_ready) {
        // save request for later
        req = calloc(1, sizeof(request_info_t));
        INSIST(req != NULL);
        req->req_action = ADD_PFD_ROUTE;
        strncpy(req->interface_name, interface_name, INT_NAME_STR_SIZE); 
        TAILQ_INSERT_TAIL(&requests, req, entries);
        return;
    }
    
    bzero(&nh_params, sizeof(nh_params));
    
    // Set the IFL index
    rc = get_ifl_index(interface_name, &nh_params.ifl);
    
    if(rc != SUCCESS || ifl_idx_t_is_equal(nh_params.ifl, null_ifl_idx)) {
        
        ERRMSG(PED, TRACE_LOG_ERR, "%s: Cannot parse or get a valid IFL"
            " index for %s", __func__, interface_name);
        return;
    }
    
    // Use round-robin packet distribution on PFD
    nh_params.pkt_dist_type = SSD_NEXTHOP_PKT_DIST_RR;

    // Set next hop address family
    nh_params.af = SSD_GF_INET;
    
    // Add next hop
    rc = ssd_request_nexthop_add(
        ssd_server_fd, ++client_nexthop_id, &nh_params, ++client_ctx);
    
    if(rc == -1) {
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Could not request next-hop addition.", __func__);
        return;
    }
    
    route_info = calloc(1, sizeof(route_info_t));
    INSIST(route_info != NULL);
    route_info->rt_table_id = pfd_rt_tbl_id;
    route_info->address = DEFAULT_ROUTE_ADDRESS;
    route_info->mask = DEFAULT_ROUTE_MASK;
    route_info->client_ctx = client_ctx;
    route_info->client_nh_id = client_nexthop_id;
    ifl_idx_t_set_equal(nh_params.ifl, route_info->ifl_index);
    
    TAILQ_INSERT_TAIL(&route_list, route_info, entries);
}


/**
 * Deletes a default service route in the pfd_forwarding routing instance
 * 
 * @param[in] interface_name
 *      The name of the interface in the pfd_forwarding routing instance that 
 *      the PFD uses
 */
void
delete_pfd_service_route(char * interface_name)
{
    route_info_t * route_info;
    request_info_t * req;
    struct ssd_rt_delete_parms rtp;
    ssd_sockaddr_un route_addr;
    ifl_idx_t ifl_index;
    int rc;

    INSIST(interface_name != NULL);

    if(!ssd_ready) {
        // save request for later
        req = calloc(1, sizeof(request_info_t));
        INSIST(req != NULL);
        req->req_action = DELETE_PFD_ROUTE;
        strncpy(req->interface_name, interface_name, INT_NAME_STR_SIZE); 
        TAILQ_INSERT_TAIL(&requests, req, entries);
        return;
    }

    bzero(&route_addr, sizeof(route_addr));
    bzero(&rtp, sizeof(rtp));
    
    get_ifl_index(interface_name, &ifl_index); // used to search for the route

    route_info = TAILQ_FIRST(&route_list);
    while(route_info) {
        if(ifl_idx_t_is_equal(ifl_index, route_info->ifl_index)) {
            
            ssd_setsocktype(&route_addr, SSD_GF_INET);
            route_addr.in.gin_addr = route_info->address;
            
            rtp.rtd_dest = &route_addr;
            rtp.rtd_prefixlen = route_info->mask;
            rtp.rtd_rtt = route_info->rt_table_id;
            rtp.rtd_gw_handle = 0;
            
            rc = ssd_request_route_delete(ssd_server_fd, &rtp, ++client_ctx);
            
            route_info->client_ctx = client_ctx;
            
            if(rc == -1) {
                ERRMSG(PED, TRACE_LOG_ERR,
                    "%s: Request to delete service route failed", __func__);
                
                // Remove the route from the list
                TAILQ_REMOVE(&route_list, route_info, entries);
                free(route_info);
            }
            
            return;
        }
        route_info = TAILQ_NEXT(route_info, entries);
    }
    
    ERRMSG(PED, TRACE_LOG_ERR, "%s: Could not find route to delete",
        __func__);
}


/**
 * Adds a service route to the default routing instance given the address 
 * assuming a /32 mask
 * 
 * @param[in] interface_name
 *      The name of the interface (next-hop) in the route used by the PFD when 
 *      NAT'ing to CPD     
 *
 * @param[in] address
 *      The IP address used to create the service route with the /32 mask
 */
void
add_service_route(char * interface_name, in_addr_t address)
{
    route_info_t * route_info;
    request_info_t * req;
    struct ssd_nh_add_parms nh_params;
    int rc;
    
    INSIST(interface_name != NULL);

    if(!ssd_ready) {
        // save request for later
        req = calloc(1, sizeof(request_info_t));
        INSIST(req != NULL);
        req->req_action = ADD_NORMAL_ROUTE;
        req->address = address;
        strncpy(req->interface_name, interface_name, INT_NAME_STR_SIZE); 
        TAILQ_INSERT_TAIL(&requests, req, entries);
        return;
    }
    
    bzero(&nh_params, sizeof(nh_params));
    
    // Set the IFL index
    rc = get_ifl_index(interface_name, &nh_params.ifl);
    
    if(rc != SUCCESS || ifl_idx_t_is_equal(nh_params.ifl, null_ifl_idx)) {
        
        ERRMSG(PED, TRACE_LOG_ERR, "%s: Cannot parse or get a valid IFL"
            " index for %s", __func__, interface_name);
        return;
    }
    
    // Use round-robin packet distribution on PFD
    nh_params.pkt_dist_type = SSD_NEXTHOP_PKT_DIST_RR;
    
    // Add next hop
    rc = ssd_request_nexthop_add(
        ssd_server_fd, ++client_nexthop_id, &nh_params, ++client_ctx);
    
    if(rc == -1) {
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Could not request next-hop addition.", __func__);
        return;
    }

    route_info = calloc(1, sizeof(route_info_t));
    INSIST(route_info != NULL);
    route_info->rt_table_id = rt_tbl_id;
    route_info->address = address;
    route_info->mask = FULL_ROUTE_MASK;
    route_info->client_ctx = client_ctx;
    route_info->client_nh_id = client_nexthop_id;
    ifl_idx_t_set_equal(nh_params.ifl, route_info->ifl_index);
    
    TAILQ_INSERT_TAIL(&route_list, route_info, entries);
}


/**
 * Deletes a service route to the default routing instance given the address 
 * assuming a /32 mask
 * 
 * @param[in] interface_name
 *      The name of the interface (next-hop) in the route 
 *
 * @param[in] address
 *      The IP address used in the service route with the /32 mask
 */
void
delete_service_route(char * interface_name, in_addr_t address)
{
    route_info_t * route_info;
    request_info_t * req;
    struct ssd_rt_delete_parms rtp;
    ssd_sockaddr_un route_addr;
    ifl_idx_t ifl_index;
    int rc;

    INSIST(interface_name != NULL);

    if(!ssd_ready) {
        // save request for later
        req = calloc(1, sizeof(request_info_t));
        INSIST(req != NULL);
        req->req_action = DELETE_NORMAL_ROUTE;
        req->address = address;
        strncpy(req->interface_name, interface_name, INT_NAME_STR_SIZE); 
        TAILQ_INSERT_TAIL(&requests, req, entries);
        return;
    }
    
    bzero(&route_addr, sizeof(route_addr));
    bzero(&rtp, sizeof(rtp));
    
    get_ifl_index(interface_name, &ifl_index); // used to search for the route

    route_info = TAILQ_FIRST(&route_list);
    while(route_info) {
        if(ifl_idx_t_is_equal(ifl_index, route_info->ifl_index)) {
            
            if(address != route_info->address) {
                ERRMSG(PED, TRACE_LOG_ERR, "%s: Address does not match "
                    "the address associated with the service route on the "
                    "interface %s", __func__, interface_name);
                return;
            }
            
            ssd_setsocktype(&route_addr, SSD_GF_INET);
            route_addr.in.gin_addr = address;
            
            rtp.rtd_dest = &route_addr;
            rtp.rtd_prefixlen = route_info->mask;
            rtp.rtd_rtt = route_info->rt_table_id;
            rtp.rtd_gw_handle = 0;
            
            rc = ssd_request_route_delete(ssd_server_fd, &rtp, ++client_ctx);
            
            route_info->client_ctx = client_ctx;
            
            if(rc == -1) {
                ERRMSG(PED, TRACE_LOG_ERR,
                    "%s: Request to delete service route failed", __func__);
                
                // Remove the route from the list
                TAILQ_REMOVE(&route_list, route_info, entries);
                free(route_info);
            }
            
            return;
        }
        route_info = TAILQ_NEXT(route_info, entries);
    }
    
    ERRMSG(PED, TRACE_LOG_ERR, "%s: Could not find route to delete",
        __func__);
}


/**
 * Clean up all service routes created
 */
void
clean_service_routes(void)
{
    int rc;
    
    if(ssd_ready) {
        junos_trace(PED_TRACEFLAG_ROUTING,
            "%s: Route service module is purging routes", __func__);
        ssd_ready = FALSE;
        rc = ssd_clean_up_route_service(ssd_server_fd, ++client_ctx);
        
        if(rc == -1) {
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Route service could not issue clean-up due to error: %m",
                __func__);
        }

    }
}

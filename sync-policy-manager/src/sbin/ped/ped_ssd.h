/*
 * $Id: ped_ssd.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_ssd.h
 * 
 * @brief Routines related to talking to SDK Services Deamon 
 * 
 * Functions for connecting to SSD and add/delete route. 
 */
#ifndef __PED_SSD_H__
#define __PED_SSD_H__

#include <sync/psd_ipc.h>

/*** Constants ***/


/*** Data structures ***/


/**
 * @brief applicable operations on interfaces
 */
typedef enum {
    ADD_SUCCESS,        ///< request to add route succeeded
    DELETE_SUCCESS,     ///< request to delete route succeeded
    ADD_FAILED,         ///< request to add route failed
    DELETE_FAILED       ///< request to delete route failed
} ssd_reply_e;


/**
 * @brief a callback function called when the result 
 * of a request to add or delete a route is available
 * user_data is echoed.
 */
typedef void (* ssd_reply_notification_handler)(
                    ssd_reply_e reply,
                    void * user_data);


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize client
 *
 */
void ped_ssd_init(void);


/**
 * Shutdown client
 */
void ped_ssd_shutdown(void);


/**
 * Check the state of SSD connection.
 * 
 * @return
 *      TRUE if connected to SSD, otherwise FALSE.
 */
boolean get_ssd_ready(void);


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
boolean ssd_client_add_route_request(
        policy_route_msg_t *route,
        ssd_reply_notification_handler func,
        void * user_data);


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
boolean ssd_client_del_route_request(
        policy_route_msg_t *route,
        ssd_reply_notification_handler func,
        void * user_data);


/**
 * Are there any outstanding requests to SSD for which we soon expect replies
 * 
 * @return
 *      TRUE if there are NOT outstanding requests;
 *      FALSE if there are outstanding requests
 */
boolean get_ssd_idle(void);


/**
 * Adds a default service route in the pfd_forwarding routing instance
 * 
 * @param[in] interface_name
 *      The name of the interface in the pfd_forwarding routing instance that 
 *      the PFD uses
 */
void add_pfd_service_route(char * interface_name);


/**
 * Deletes a default service route in the pfd_forwarding routing instance
 * 
 * @param[in] interface_name
 *      The name of the interface in the pfd_forwarding routing instance that 
 *      the PFD uses
 */
void delete_pfd_service_route(char * interface_name);


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
void add_service_route(char * interface_name, in_addr_t address);


/**
 * Deletes a service route to the default routing instance given the address 
 * assuming a /32 mask
 * 
 * @param[in] interface_name
 *      The name of the interface (next-hop) in the route 
 *
 * @param[in] address
 *      The IP address used to in the service route with the /32 mask
 */
void delete_service_route(char * interface_name, in_addr_t address);


/**
 * Clean up all routes created and shutdown this module
 */
void clean_routes(void);

#endif

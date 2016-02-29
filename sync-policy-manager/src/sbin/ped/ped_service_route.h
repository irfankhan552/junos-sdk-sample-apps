/*
 * $Id: ped_service_route.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_service_route.h
 * 
 * @brief Routines related to adding and deleting service routes
 * 
 * Functions for connecting to SSD stricly for adding and deleting service 
 * routes and next hops 
 */
#ifndef __PED_SERVICE_ROUTE_H__
#define __PED_SERVICE_ROUTE_H__

#include <sync/common.h>

/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize client
 * 
 * @param[in] ctx
 *     The event context for this application
 */
void service_route_init(evContext ctx);


/**
 * Shutdown client
 */
void service_route_shutdown(void);


/**
 * If the module is not ready it is liely because the PFD route table doesn't
 * exist and we haven't been able to get its table ID. If another module knows
 * that the RI has been created, then we try getting it again.
 */
void pfd_ri_created(void);


/**
 * Check the state
 * 
 * @return
 *      TRUE if connected to SSD and ready to accept requests, otherwise FALSE
 */
boolean get_serviceroute_ready(void);


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
 * Clean up all service routes created
 */
void clean_service_routes(void);

#endif

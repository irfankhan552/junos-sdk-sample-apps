/*
 * $Id: cpd_conn.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file cpd_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __CPD_CONN_H__
#define __CPD_CONN_H__

#include <sys/types.h>
#include <isc/eventlib.h>

/*** Constants ***/


/*** Data structures ***/


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
status_t init_connections(evContext ctx);


/**
 * Terminate all the connections
 */
void close_connections(void);


/**
 * Send the PFD the new authorized user
 *  
 * @param[in] addr
 *      The user's IP address in network byte order
 */
void send_authorized_user(in_addr_t addr);


/**
 * Send the PFD the new authorized user
 *  
 * @param[in] addr
 *      The user's IP address in network byte order
 */
void send_repudiated_user(in_addr_t addr);

#endif


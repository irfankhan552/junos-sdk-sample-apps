/*
 * $Id: ped_conn.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __PED_CONN_H__
#define __PED_CONN_H__

#include <isc/eventlib.h>

/*** Constants ***/


/*** Data structures ***/


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
status_t init_server(evContext ctx);


/**
 * Close existing connections and shutdown server
 */
void close_connections(void);


/**
 * Reconfigure CPD and PFD peers with new addresses in case of a change to any 
 * of the two addresses.
 */
void reconfigure_peers(void);

/**
 * Called when the KCOM module detects that an ms-* pic has gone offline
 * 
 * @param[in] intername_name
 *      Name of the IFD going down
 */
void mspic_offline(char * intername_name);

#endif


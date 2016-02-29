/*
 * $Id: hellopics-data_conn.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file hellopics-data_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __HELLOPICS_DATA_CONN_H__
#define __HELLOPICS_DATA_CONN_H__


#include <isc/eventlib.h>
#include <jnx/aux_types.h>

/*** Constants ***/


/*** Data structures ***/



/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the connections
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


#endif


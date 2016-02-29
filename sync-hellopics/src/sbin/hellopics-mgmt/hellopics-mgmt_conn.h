/*
 * $Id: hellopics-mgmt_conn.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file hellopics-mgmt_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __HELLOPICS_MGMT_CONN_H__
#define __HELLOPICS_MGMT_CONN_H__

#include <sys/types.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>

/*** Constants ***/


/*** Data structures ***/

/**
 * Structure for holding the stats about messages
 */
typedef struct hellopics_stats_s {
    uint32_t msgs_sent;     ///< total number of messages sent to PICs
    uint32_t msgs_received; ///< total number of messages received from PICs
    uint32_t msgs_missed;   ///< total number of messages expected but never received from PICs
    uint32_t msgs_badorder; ///< total number of messages received out of order
} hellopics_stats_t;



/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the statistic variables 
 */
void init_connection_stats(void);


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
status_t
start_server(evContext ctx);
#endif


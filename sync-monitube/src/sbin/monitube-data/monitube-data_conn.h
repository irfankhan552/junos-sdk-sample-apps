/*
 * $Id: monitube-data_conn.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file monitube-data_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __MONITUBE_DATA_CONN_H__
#define __MONITUBE_DATA_CONN_H__

#include <sync/monitube_ipc.h>

/*** Constants ***/


/*** Data structures ***/



/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the connection to the mgmt component
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_connections(evContext ctx);


/**
 * Terminate connection to the mgmt component
 */
void
close_connections(void);


/**
 * Notify the mgmt component about a statistic update
 * 
 * @param[in] flow_addr
 *      flow address (id)
 * 
 * @param[in] flow_dport
 *      flow dst port (in net. byte-order) (id)
 * 
 * @param[in] mdi_df
 *      the delay factor
 * 
 * @param[in] mdi_mlr
 *      the media loss rate
 * 
 * @param[in] monitor_name
 *      the monitor name
 */
void
notify_stat_update(in_addr_t flow_addr,
                   uint16_t flow_dport,
                   double mdi_df,
                   uint32_t mdi_mlr,
                   char * monitor_name);

#endif


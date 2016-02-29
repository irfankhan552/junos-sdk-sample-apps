/*
 * $Id: monitube2-data_conn.h 347265 2009-11-19 13:55:39Z kdickman $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2009, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file monitube2-data_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __MONITUBE2_DATA_CONN_H__
#define __MONITUBE2_DATA_CONN_H__

#include <sync/monitube2_ipc.h>

/*** Constants ***/


/*** Data structures ***/



/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the connection to the mgmt component
 */
void
init_connections(void);


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
 * @param[in] ssid
 *      service set it
 */
void
notify_stat_update(in_addr_t flow_addr,
                   uint16_t flow_dport,
                   double mdi_df,
                   uint32_t mdi_mlr,
                   uint16_t ssid);

#endif


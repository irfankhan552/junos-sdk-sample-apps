/*
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
 * @file hellopics-mgmt_ha.h
 * @brief High Availability module 
 *        
 * 
 * These functions will manage the packet statistics replication for a master/slave  
 * management component.
 */
 
#ifndef __HELLOPICS_MGMT_HA_H__
#define __HELLOPICS_MGMT_HA_H__


/*** Constants ***/
#define HELLOPICS_MASTER_ADDR 0x80000004


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 * 
 * @param[in] master_address
 *      Address of master, or 0 if this is the master
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_replication(in_addr_t master_address);


/**
 * Pass replication data to add or update to the slave
 * 
 * @param[in] data
 *      The data to replicate to the slave
 */
void
update_replication_entry(hellopics_stats_t * data);


/**
 * Stop and shutdown all the replication subsystem
 */
void
stop_replication(void);
void
set_mastership(boolean state, in_addr_t master_addr);

#endif

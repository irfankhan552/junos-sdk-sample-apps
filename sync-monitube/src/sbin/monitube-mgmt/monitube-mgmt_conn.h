/*
 * $Id: monitube-mgmt_conn.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-mgmt_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __MONITUBE_MGMT_CONN_H__
#define __MONITUBE_MGMT_CONN_H__

/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the server socket connection
 * 
 * @param[in] ctx
 *     Newly created event context 
 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_server(evContext ctx);


/**
 * Close existing connections and shutdown server
 */
void
close_connections(void);


/**
 * Notification about an MS-PIC interface going down
 * 
 * @param[in] name
 *      name of interface that has gone down
 */
void
mspic_offline(const char * name);


/**
 * Enqueue a message to go to the data component about a monitor update
 * 
 * @param[in] mon_name
 *      Monitor's name
 * 
 * @param[in] rate
 *      Configuration group bps rate
 */
void
notify_monitor_update(const char * mon_name, uint32_t rate);


/**
 * Enqueue a message to go to the data component about an address update
 * 
 * @param[in] mon_name
 *      Monitor's name
 * 
 * @param[in] address
 *      Address in the monitor
 * 
 * @param[in] mask
 *      Address mask
 */
void
notify_address_update(const char * mon_name, in_addr_t address, in_addr_t mask);


/**
 * Enqueue a message to go to the data component about a mirror update
 * 
 * @param[in] mirror_from
 *      Mirror from address
 * 
 * @param[in] mirror_to
 *      Mirror to address
 */
void
notify_mirror_update(in_addr_t mirror_from, in_addr_t mirror_to);


/**
 * Enqueue a message to go to the data component about a monitor delete
 * 
 * @param[in] mon_name
 *      Monitor's name
 */
void
notify_monitor_delete(const char * mon_name);


/**
 * Enqueue a message to go to the data component about an address delete
 * 
 * @param[in] mon_name
 *      Monitor's name
 * 
 * @param[in] address
 *      Address in the monitor
 * 
 * @param[in] mask
 *      Address mask
 */
void
notify_address_delete(const char * mon_name, in_addr_t address, in_addr_t mask);


/**
 * Enqueue a message to go to the data component about an mirror delete
 * 
 * @param[in] mirror_from
 *      Mirror from address
 */
void
notify_mirror_delete(in_addr_t mirror_from);


/**
 * Enqueue a message to go to the data component to delete all monitors
 */
void
notify_delete_all_monitors(void);


/**
 * Enqueue a message to go to the data component to delete all mirrors
 */
void
notify_delete_all_mirrors(void);


/**
 * Enqueue a message to go to the data component to set the replication interval
 * 
 * @param[in] r_int
 *      New replication interval
 */
void
notify_replication_interval(uint8_t r_int);


/**
 * Go through the buffered messages and send them to the data component 
 * if it's connected.
 */
void
process_notifications(void);


/**
 * Switch to this new master PIC for the data component
 * 
 * @param[in] addr
 *      The IRI2 interface address of the new master PIC
 */
void
set_master_address(in_addr_t addr);

#endif

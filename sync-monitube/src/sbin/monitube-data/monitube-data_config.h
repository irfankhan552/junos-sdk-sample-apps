/*
 * $Id: monitube-data_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-data_config.h
 * @brief Relating to getting and setting the configuration data
 *
 *
 * These functions will store and provide access to the
 * configuration data which is essentially coming from the mgmt component.
 */

#ifndef __MONITUBE_DATA_CONFIG_H__
#define __MONITUBE_DATA_CONFIG_H__


/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 *
 * @param[in] ctx
 *      event context
 *
 * @return
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_config(evContext ctx);


/**
 * Configure mastership of this data component
 *
 * @param[in] state
 *      Mastership state (T=Master, F=Slave)
 *
 * @param[in] master_addr
 *      Master address (only present if state=F/slave)
 */
void
set_mastership(boolean state, in_addr_t master_addr);


/**
 * Set the replication interval
 *
 * @param[in] interval
 *      Interval in second between replication data updates
 */
void
set_replication_interval(uint8_t interval);


/**
 * Get the replication interval
 *
 * @return  The configured replication interval
 */
uint8_t
get_replication_interval(void);


/**
 * Get the currently cached time
 *
 * @return
 *      Current time
 */
time_t
get_current_time(void);


/**
 * Clear the configuration data
 */
void
clear_config(void);


/**
 * Delete all configured monitors
 */
void
clear_monitors_configuration(void);


/**
 * Delete all configured mirrors
 */
void
clear_mirrors_configuration(void);


/**
 * Find and delete a monitor given its name
 *
 * @param[in] name
 *      The monitor's name
 */
void
delete_monitor(char * name);

/**
 * Find and delete a mirror given its from address
 *
 * @param[in] from
 *      The mirror's from address
 */
void
delete_mirror(in_addr_t from);


/**
 * Delete a given address from a monitor's config
 *
 * @param[in] name
 *      The monitor's name
 *
 * @param[in] addr
 *      Address
 *
 * @param[in] mask
 *      Address mask
 */
void
delete_address(char * name, in_addr_t addr, in_addr_t mask);


/**
 * Add an address prefix to a monitor's config
 *
 * @param[in] name
 *      The monitor's name
 *
 * @param[in] addr
 *      Address
 *
 * @param[in] mask
 *      Address mask
 */
void
add_address(char * name, in_addr_t addr, in_addr_t mask);


/**
 * Update or add a monitoring config
 *
 * @param[in] name
 *      The monitor name
 *
 * @param[in] rate
 *      The monitored group's bps (media) rate
 */
void
update_monitor(char * name, uint32_t rate);


/**
 * Update or add a mirroring config
 *
 * @param[in] from
 *      The mirror from address
 *
 * @param[in] to
 *      The mirror to address
 */
void
update_mirror(in_addr_t from, in_addr_t to);


/**
 * Find the monitoring rate for an address if one exists
 *
 * @param[in] address
 *      The destination address
 *
 * @param[out] name
 *      The monitor name associated with the rate (set if found)
 *
 * @return the monitoring rate; or zero if no match is found
 */
uint32_t
get_monitored_rate(in_addr_t address, char ** name);


/**
 * Find the redirect for a mirrored address if one exists
 *
 * @param[in] address
 *      The original destination
 *
 * @return the address to mirror to; or 0 if none found
 */
in_addr_t
get_mirror(in_addr_t address);


#endif

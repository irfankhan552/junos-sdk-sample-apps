/*
 * $Id: monitube2-data_config.h 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file monitube2-data_config.h
 * @brief Relating to getting and setting the configuration data
 *
 *
 * These functions will store and provide access to the
 * configuration data which is essentially coming from the mgmt component.
 */

#ifndef __MONITUBE2_DATA_CONFIG_H__
#define __MONITUBE2_DATA_CONFIG_H__


/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Init the data structures that will store configuration info
 * 
 * Should only be called on startup (one time)
 */
void
init_config(void);


/**
 * Get the currently cached time
 *
 * @return
 *      Current time
 */
time_t
get_current_time(void);


/**
 * Clear the stored configuration data
 */
void
clear_config(void);


/**
 * Update/create a rule
 *
 * @param[in] name
 *      rule name
 *
 * @param[in] rate
 *      monitoring rate
 *      
 * @param[in] redirect
 *       address for mirroring
 */
void
update_rule(char * name, uint32_t rate, in_addr_t redirect);


/**
 * Delete rule
 *
 * @param[in] name
 *      rule name
 *      
 * @return 0 on success; -1 o/w
 */
int
delete_rule(char * name);


/**
 * Add an address to the rule
 *
 * @param[in] name
 *      rule name
 *
 * @param[in] addr
 *      Address
 *
 * @param[in] mask
 *      Mask to apply to @c addr
 */
void
add_address(char * name, in_addr_t addr, in_addr_t mask);


/**
 * Delete an address from the rule
 *
 * @param[in] name
 *      rule name
 *
 * @param[in] addr
 *      Address
 *
 * @param[in] mask
 *      Mask to apply to @c addr
 */
void
delete_address(char * name, in_addr_t addr, in_addr_t mask);


/**
 * Apply rule on a service set
 *
 * @param[in] name
 *      rule name
 *
 * @param[in] ssid
 *      service set id
 *
 * @param[in] gennum
 *      Service set generation number needed for PDB
 *      
 * @param[in] svcid
 *      Service ID needed for PDB
 */
void
apply_rule(char * name, uint16_t ssid, uint32_t gennum, uint32_t svcid);


/**
 * Remove a rule from a service set
 *
 * @param[in] name
 *      rule name
 *
 * @param[in] ssid
 *      service set id
 *
 * @param[in] gennum
 *      Service set generation number needed for PDB
 *      
 * @param[in] svcid
 *      Service ID needed for PDB
 */
void
remove_rule(char * name, uint16_t ssid, uint32_t gennum, uint32_t svcid);


/**
 * Delete service set
 *
 * @param[in] ssid
 *      service set id
 */
void
delete_serviceset(uint16_t ssid, uint32_t gennum, uint32_t svcid);


/**
 * Find the monitoring rate and redirect address a rule-match happen one exists.
 * This will be run in the fast path.
 *
 * @param[in] ssid
 *      The service set id
 * 
 * @param[in] svc_id
 *      The service id
 *
 * @param[in] address
 *      The destination address
 *      
 * @param[out] rate
 *      If null on input, this is ignored. The monitoring rate; 0 if no rate 
 *
 * @param[out] redirect_addr
 *      If null on input, this is ignored. The mirror addr; 0 if no mirroring
 *
 * @return 0 if found a valid rule; or -1 if no match is found
 */
int
get_monitored_rule_info(uint16_t ssid,
		                uint32_t svc_id,
		                in_addr_t address,
		                uint32_t * rate,
		                uint32_t * redirect_addr);
#endif

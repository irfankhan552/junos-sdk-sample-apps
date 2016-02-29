/*
 * $Id: dpm-ctrl_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-ctrl_config.h
 * @brief Relating to getting and setting the configuration data 
 *        
 * 
 * These functions will store and provide access to the 
 * configuration data which is essentailly coming from the mgmt component.
 */
 
#ifndef __DPM_CTRL_CONFIG_H__
#define __DPM_CTRL_CONFIG_H__

#include <sync/dpm_ipc.h>

/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 * 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_configuration(void);


/**
 * Clear the configuration data
 */
void
clear_configuration(void);


/**
 * Reset the configuration
 */
void
reset_configuration(uint8_t filter_mode);


/**
 * All configuration should now be received
 */
void
configuration_complete(void);


/**
 * Configure a policer
 * 
 * @param[in] policer
 *      The policer configuration received from the mgmt component
 */
void
configure_policer(policer_info_t * policer);


/**
 * Configure a managed interface
 * All policers should be configured first
 * 
 * @param[in] interface
 *      The interface configuration received from the mgmt component
 */
void
configure_managed_interface(int_info_t * interface);


/**
 * Configure a subscriber
 * All policers and managed interfaces should be configured first
 * 
 * @param[in] subscriber
 *      The subscriber configuration received from the mgmt component
 */
void
configure_subscriber(sub_info_t * subscriber);


/**
 * Check for a match with the username-password pair.
 * 
 * @param[in] username
 *      The subscriber's name
 * 
 * @param[in] password
 *      The subscriber's password
 * 
 * @return TRUE if there is a match, FALSE otherwise
 */
boolean
validate_credentials(char * username, char * password);


/**
 * Check for a match with the IP address
 * 
 * @param[in] address
 *      The user's IP address
 * 
 * @return TRUE if there is a user logged in with this IP, FALSE otherwise
 */
boolean
user_logged_in(in_addr_t address);


/**
 * Apply the policy for this user, and add them to the logged in users
 * 
 * @param[in] username
 *      The subscriber's name
 * 
 * @param[in] address
 *      The user's IP address
 * 
 * @return SUCCESS upon sucessfully applying the policy; EFAIL otherwise
 */
status_t
apply_policy(char * username, in_addr_t address);


/**
 * Remove the policy for this user, and remove them from the logged in users
 *
 * @param[in] address
 *      The user's IP address
 */
void
remove_policy(in_addr_t address);


#endif

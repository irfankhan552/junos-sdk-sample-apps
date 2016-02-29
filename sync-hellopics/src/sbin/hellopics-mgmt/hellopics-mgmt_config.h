/*
 * $Id: hellopics-mgmt_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file hellopics-mgmt_config.h
 * @brief Relating to loading the configuration data
 * 
 * These functions will parse and load the configuration data.
 */
 
#ifndef __HELLOPICS_MGMT_CONFIG_H__
#define __HELLOPICS_MGMT_CONFIG_H__

/*** Constants ***/

#define HELLOPICS_HA_MASTER_ADDR 0x80000004
/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Read daemon configuration from the database
 * 
 * @param[in] check
 *     1 if this function being invoked because of a commit check?
 * 
 * @return SUCCESS (0) if successfully loaded; -1 in case of error
 * 
 * @note Do not use ERRMSG during config check.
 */
int hellopics_config_read (int check);
void init_config(void);
#endif


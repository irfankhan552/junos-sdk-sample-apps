/*
 * $Id: ped_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_config.h
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions and variables will parse, load, hold, and return
 * the configuration data.
 */
#ifndef __PED_CONFIG_H__
#define __PED_CONFIG_H__

#include <jnx/patricia.h>
#include <sync/psd_ipc.h>

/*** Constants ***/

/*** Data Structures ***/

/**
 * The structure we use to store our configuration 
 * information in the patricia tree:
 */
typedef struct ped_cond_s {
    patnode    node;                           ///< Tree node (comes first)
    char       condition_name[MAX_COND_NAME_LEN + 1]; ///< Condition name
    char       ifname[MAX_IF_NAME_LEN + 1];    ///< Interface name
    uint8_t    af;                             ///< Address family
} ped_cond_t;

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Init the data structure that will store configuration info,
 * or in other words, the condition(s)
 */
void init_config(void);


/**
 * Clear the configuration info completely from the patricia tree
 */
void clear_config(void);


/**
 * Get the first condition from configuration.
 * 
 * @return
 *      The first condition, or NULL if no conditions.
 */
ped_cond_t *first_condition(void);


/**
 * Get (iterator-style) the next condition from configuration.
 *
 * @param[in] data
 *     The previously returned data from this function or
 *     NULL for the first condition.
 * 
 * @return
 *      The next condition, or NULL if no more conditions.
 */
ped_cond_t *next_condition(ped_cond_t *data);


/**
 * Tells whether or not a condition matches this interface name/family pair
 * 
 * @param[in] ifname
 *     interface name to match
 * 
 * @param[in] af
 *     address family to match
 * 
 * @return TRUE if and only if this interface name matches a configured 
 *     condition's matching expression for interface names AND 
 *     if the family matches the configured family for that condition 
 *     unless the family is marked as any (in which case just the name matters);
 *     otherwise FALSE
 */
boolean find_matching_condition(char * ifname, uint8_t af);


/**
 * Get the PFD interface
 * 
 * @return
 *      The PFD interface (IFL)
 */
char * get_pfd_interface(void);


/**
 * Get the PFD NAT interface
 * 
 * @return
 *      The PFD NAT interface (IFL)
 */
char * get_pfd_nat_interface(void);


/**
 * Get the PFD address
 * 
 * @return
 *      The PFD address in network-byte order
 */
in_addr_t get_pfd_address(void);


/**
 * Get the CPD address
 * 
 * @return
 *      The CPD address in network-byte order
 */
in_addr_t get_cpd_address(void);


/**
 * Read configuration from the database.
 * 
 * @param[in] check
 *      SDK_CONFIG_READ_CHECK(1) if this function was called
 *      to check configuration, or SDK_CONFIG_READ_LOAD(0)
 *      if it's called to load configuration.
 *
 * @return
 *      SUCCESS, or -1 on error.
 * 
 * @note
 *      Do not use ERRMSG during config check.
 */
int ped_config_read(int check);

#endif

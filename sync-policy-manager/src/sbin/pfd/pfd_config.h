/*
 * $Id: pfd_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file pfd_config.h
 * @brief Relating to getting and setting the configuration data 
 *        coming from the CPD
 * 
 * These functions will store and provide access to the 
 * configuration data which is the set of authorized users.
 */
 
#ifndef __PFD_CONFIG_H__
#define __PFD_CONFIG_H__

#include <time.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <netinet/in.h>


/*** Constants ***/


/*** Data Structures ***/


/**
 * The structure we use to store our configuration 
 * information in the patricia tree:
 */
typedef struct {
    patnode    node;    ///< tree node (comes first in struct)
    in_addr_t  address; ///< authorized address
} pfd_auth_user_t;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structure that will store configuration info,
 * or in other words, the set of authorized users and pre-filters
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t init_config(evContext ctx);


/**
 * Clear the configuration data
 */
void clear_config(void);


/**
 * Add an authorized user from the configured set
 * 
 * @param[in] addr
 *      The user's IP address in network byte order
 */
void add_auth_user_addr(in_addr_t addr);


/**
 * Delete an authorized user from the configured set
 * 
 * @param[in] addr
 *      The user's IP address in network byte order
 */
void delete_auth_user_addr(in_addr_t addr);


/**
 * Is a user authorized
 * 
 * @param[in] addr
 *      The user's IP address in network byte order
 * 
 * @return TRUE if authorized or FALSE otherwise
 */
boolean is_auth_user(in_addr_t addr);


/**
 * Get the PFD address
 * 
 * @return
 *      The PFD address in network-byte order
 */
in_addr_t get_pfd_address(void);


/**
 * Set the PFD address
 * 
 * @param[in] addr
 *      The new PFD address in network-byte order
 */
void set_pfd_address(in_addr_t addr);


/**
 * Get the CPD address
 * 
 * @return
 *      The CPD address in network-byte order
 */
in_addr_t get_cpd_address(void);


/**
 * Set the CPD address
 * 
 * @param[in] addr
 *      The new CPD address in network-byte order
 */
void set_cpd_address(in_addr_t addr);


/**
 * Get the currently cached time
 * 
 * @return
 *      Current time
 */
time_t get_current_time(void);

#endif

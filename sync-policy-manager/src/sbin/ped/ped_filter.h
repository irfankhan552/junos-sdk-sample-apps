/*
 * $Id: ped_filter.h 365138 2010-02-27 10:16:06Z builder $
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
 * @file ped_filter.h
 * @brief Works with filters
 * 
 * Create PFD filter and applies filters to interfaces
 */

#ifndef __PED_FILTER_H__
#define __PED_FILTER_H__

#include "ped_policy_table.h"

/*** Constants ***/

#define PFD_FILTER_NAME "pfd_filter"  ///< pfd_filter applied to all interfaces

/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Is the PFD filter on
 */
boolean
is_pfd_filter_on(void);


/**
 * Apply PFD filter automatically
 */
void
turn_on_pfd_filter(void);


/**
 * Don't PFD filter automatically
 */
void
turn_off_pfd_filter(void);


/**
 * Initialize the connection to the dfwd
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      0 if successful; otherwise -1 with an error message.
 */
int
init_dfw(evContext ctx);


/**
 * Close down and free all resources
 */
void
shutdown_dfw(void);


/**
 * Create the configuration necessary for the PFD service routes to work
 * 
 * @param[in] interface_name
 *     Name of interface in the PFD routing instance
 * 
 * @return
 *      TRUE if successful; FALSE otherwise
 */
boolean
init_pfd_filter(char * interface_name);


/**
 * Apply PFD filters on an interface
 * 
 * @param[in] interface_name
 *     Name of interface to apply filters on
 * 
 * @return
 *      TRUE if successful; FALSE otherwise
 */
boolean
apply_pfd_filter_to_interface(char * interface_name);



/**
 * Remove PFD filter from an interface
 * 
 * @param[in] interface_name
 *     Name of interface to remove filters on
 */
void
remove_pfd_filter_from_interface(char * interface_name);


/**
 * Apply filters on an interface
 * 
 * @param[in] interface_name
 *     Name of interface to apply filters on
 * 
 * @param[in] filters
 *     The filters to apply
 * 
 * @return
 *      TRUE if successful; FALSE otherwise
 */
boolean
apply_filters_to_interface(
            char * interface_name,
            ped_policy_filter_t * filters);


/**
 * Remove all configured filters from an interface
 * 
 * @param[in] interface_name
 *     Name of interface to remove filters on
 * 
 * @return
 *      TRUE if successful; FALSE otherwise
 */
boolean
remove_filters_from_interface(char * interface_name);
#endif

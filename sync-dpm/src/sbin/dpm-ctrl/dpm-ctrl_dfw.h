/*
 * $Id: dpm-ctrl_dfw.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-ctrl_dfw.h
 * @brief Relating to managing the firewall filters
 * 
 * These functions and types will manage the firewall filters.
 */
 
#ifndef __DPM_CTRL_DFW_H__
#define __DPM_CTRL_DFW_H__

#include <sync/dpm_ipc.h>

/*** Constants ***/


/*** Data structures ***/



/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the connection to the dfwd
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_dfw(evContext ctx);


/**
 * Close down and free all resources
 */
void
shutdown_dfw(void);


/**
 * Is the module ready to start using (sending requests)
 * 
 * @return TRUE or FALSE
 */
boolean
dfw_ready(void);


/**
 * Purge filters and reset mode 
 * 
 * @param[in] new_filter_mode
 *      Use classic filters
 */
void
reset_all_filters(boolean new_filter_mode);


/**
 * Create a policer using the DFWD
 * 
 * @param[in] policer
 *      The policer's information 
 */
void
create_policer(policer_info_t * policer);


/**
 * Add an ingress and egress filter to the interface using the given policiers 
 * as actions
 * 
 * @param[in] int_name
 *      The interface (IFL) name
 * 
 * @param[in] ingress_pol
 *      The policier to use in the ingress filter
 * 
 * @param[in] egress_pol
 *      The policier to use in the egress filter 
 */
void
apply_default_int_policy(const char * int_name,
                         const char * ingress_pol,
                         const char * egress_pol);


/**
 * Add an ingress and egress filter to the interface using the given policiers 
 * as actions
 * 
 * @param[in] int_name
 *      The interface the subscriber's traffic gets routed through
 * 
 * @param[in] sub_name
 *      The subscriber name
 * 
 * @param[in] address
 *      The subscriber's address
 * 
 * @param[in] pol_name
 *      The policier to apply on subscriber traffic
 * 
 * @return 0 upon success, -1 on failure (with error logged)
 */
int
apply_subscriber_policer(const char * int_name,
                         const char * sub_name,
                         in_addr_t address,
                         const char * pol_name);


/**
 * Delete terms in the ingress and egress filter where the subscriber's 
 * traffic gets routed thru in order to police their traffic with their 
 * specific policer  
 * 
 * @param[in] int_name
 *      The interface the subscriber's traffic gets routed through
 * 
 * @param[in] sub_name
 *      The subscriber name
 * 
 * @return 0 upon success, -1 on failure (with error logged)
 */
int
revoke_subscriber_policer(const char * int_name,
                          const char * sub_name);


#endif


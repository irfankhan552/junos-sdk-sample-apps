/*
 * $Id: psd_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file psd_config.h
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions and variables will parse, load, hold, and return
 * the configuration data.
 */
#ifndef __PSD_CONFIG_H__
#define __PSD_CONFIG_H__

#include <sync/psd_ipc.h>
#include <jnx/patricia.h>


/*** Constants ***/

/*** Data Structures ***/


/**
 * Structure holds filter data.
 * Using the same structure as the filter message to avoid
 * memory copy when sending message, but it needs a little
 * extra memory to store interface name and address family
 * in each data structure. Considering system performance
 * and memory space, the former has the priority.
 */
typedef struct psd_policy_filter_s {
    policy_filter_msg_t     filter_data;    ///< The filter message to send
} psd_policy_filter_t;


/**
 * Structure holds route data.
 * Using the same structure as the route message to avoid
 * memory copy when sending message, but it needs a little
 * extra memory to store interface name and address family
 * in each data structure. Considering system performance
 * and memory space, the former has the priority.
 */
typedef struct psd_policy_route_s {
    policy_route_msg_t        route_data; ///< The route message to send
    struct psd_policy_route_s *next;      ///< Pointer to next route in list
} psd_policy_route_t;


/**
 * The structure we use to store our configuration 
 * information in the patricia tree.
 */
typedef struct psd_policy_s {
    patnode              node;  ///< Tree node (not to be touched)
    char                 policy_name[MAX_POLICY_NAME_LEN + 1];  ///< Policy name
    char                 ifname[MAX_IF_NAME_LEN + 1];    ///< Interface name
    uint8_t              af;                             ///< Address family
    psd_policy_filter_t *filter;                        ///< The filter
    psd_policy_route_t  *route;                         ///< The routes
} psd_policy_t;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structure that will store configuration info,
 * or in other words, the policies.
 */
void init_config(void);


/**
 * Get the first policy from configuration.
 *
 * @return
 *      The first policy in configuration,
 *      or NULL if no policy is in configuration.
 */
psd_policy_t *first_policy(void);


/**
 * Get (iterator-style) the next policy from configuration.
 *
 * @param[in] data
 *      The previously returned policy from this function,
 *      or NULL for the first policy.
 *
 * @return
 *      The next policy in configuration,
 *      or NULL if data is the last one in configuration.
 */
psd_policy_t *next_policy(psd_policy_t *data);


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
int psd_config_read(int check);

#endif

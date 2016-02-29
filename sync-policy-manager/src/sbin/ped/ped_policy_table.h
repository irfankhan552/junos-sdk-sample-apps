/*
 * $Id: ped_policy_table.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_policy_table.h
 * @brief For accessing table of managed interfaces/policies
 * 
 * Functions for accessing the table of managed interface/family pairs.
 * Each interface and family pair is associated a policy.
 * A policy consists of an input and an output filter that we (ped)
 * apply on the interface/family (iff).
 */
#ifndef __PED_POLICY_TABLE_H_
#define __PED_POLICY_TABLE_H_

#include <sync/psd_ipc.h>

/*** Constants ***/


/*** Data structures ***/


/**
 * @brief States of a filter in a policy
 */
typedef enum {
    FILTER_UNVERIFIED,    ///< an added filter needs to be verified that it is still contained within the policy
    FILTER_ADDED,         ///< filter has been applied
    FILTER_FAILED,        ///< filters have not been applied
    FILTER_PENDING        ///< request to add filters is pending
} policy_filter_status_e;


/**
 * @brief States of a route in a policy
 */
typedef enum {
    ROUTE_UNVERIFIED,   ///< an added route needs to be verified that it is still contained within the policy
    ROUTE_ADDED,        ///< route has been applied
    ROUTE_FAILED,       ///< route has not been applied
    ROUTE_PENDING       ///< request to add route is pending
} policy_route_status_e;


/**
 * Structure to hold filter data in the table.
 */
typedef struct ped_policy_filter_s {
    policy_filter_msg_t     filter_data; ///< filter data (directly from msg)
    policy_filter_status_e  status;      ///< status of this filter
} ped_policy_filter_t;


/**
 * Structure to hold route data in the table.
 */
typedef struct ped_policy_route_s {
    policy_route_msg_t         route_data; ///< route data (directly from message)
    policy_route_status_e      status;     ///< status of this route
    struct ped_policy_route_s  * next;     ///< link in list to next route in policy
} ped_policy_route_t;


/**
 * The value for the hashtable
 */
typedef struct policy_table_entry_s {
    char                 ifname[MAX_IF_NAME_LEN + 1]; ///< Interface name
    uint8_t              af;                          ///< Address family
    ped_policy_filter_t *filter;     ///< The filter to apply on the interface
    ped_policy_route_t  *route;      ///< The routes to apply on the interface
    boolean              broken;     ///< TRUE when a route add has failed
    boolean              pfd_filter; ///< TRUE when pfd_filter has been applied
} policy_table_entry_t;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initializes the table for first use.
 */
void init_table(void);


/**
 * Destroy the table. It should be empty or this will cause a memory leak.
 */
void destroy_table(void);


/**
 * Reset the iterator. This should always be called before 
 * using policy_table_next() to start iterating over entries.
 */
void policy_table_iterator_reset(void);


/**
 * Use this function iterator-style to go through the entries
 * in the table of managed interfaces. You should call 
 * policy_table_iterator_reset before iterating over entries with 
 * this function.
 * 
 *
 * @return
 *      Pointer to policy table entry if successful, otherwise NULL.
 */
policy_table_entry_t *policy_table_next(void);


/**
 * Get the number of entries in the table of managed interfaces.
 * 
 * @return The number of entries in the table
 */
int policy_table_entry_count(void);


/**
 * Adds a filter with the given interface name and address family to the table.
 * Only adds it if it does not yet exist in the table.
 * 
 * @param[in] filter_data
 *     Filter part of policy, containing interface name and address family
 */
void policy_table_add_filter(policy_filter_msg_t * filter_data);


/**
 * Adds a route with the given interface name and address family to the table.
 * Only adds it if it does not yet exist in the table.
 * 
 * @param[in] route_data
 *     Route data
 */
void policy_table_add_route(policy_route_msg_t * route_data);


/**
 * Clear everything except for the pfd_filter (no PSD policy for interface).
 * The policy will have no routes or filters afterward. If the policy does not 
 * exist it will be created.
 * 
 * @param[in] ifname
 *     The interface name of the policy
 * 
 * @param[in] af
 *      The address family for the interface name of the policy
 */
void policy_table_clear_policy(char *ifname, uint8_t af);


/**
 * Removes an interface with the given name from the table
 * 
 * @param[in] ifname
 *     Interface name
 * 
 * @param[in] af
 *     Address family
 * 
 * @param[in] interface_exists 
 *      TRUE if the interface still exists (we need to remvoe filters from it);
 *      FALSE if it was deleted so we don't need to worry about deleting filters
 */
void
policy_table_delete_policy(char *ifname, uint8_t af, boolean interface_exists);


/**
 * Clean policy table, remove all UNVERIFIED filters and routes.
 * Anything left in the UNVERIFIED state (status) will be deleted.
 */
void policy_table_clean(void);


/**
 * Mark all policies in the table UNVERIFIED.
 * 
 * @return
 *      TRUE upon success;
 *      FALSE if aborted because there's still changes pending 
 *            in which case all route statuses stay the same
 */
boolean policy_table_unverify_all(void);


#endif

/*
 * $Id: ped_policy_table.c 366969 2010-03-09 15:30:13Z taoliu $
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
 * @file ped_policy_table.c
 * @brief helper to keep track of managed interfaces (policy table)
 * 
 * We introduce functions to wrap-up and hide the hashtable implementation 
 * of the table storing all managed interfaces. With each interface and family
 * pair are associated a policy. A policy consists of an input and an output 
 * filter that we (ped) apply on the interface/family (iff).  
 */
#include <sync/common.h>
#include <hashtable.h>
#include <hashtable_itr.h>
#include "ped_policy_table.h"
#include "ped_filter.h"
#include "ped_ssd.h"

#include PE_OUT_H

/*** Constants ***/


/*** Data Structures ***/

/**
 * Table contains policy information with managed interfaces
 */
static struct hashtable *if_table = NULL; 


/**
 * Iterator over table for public use
 */
static struct hashtable_itr *itr = NULL;


/**
 * Is the iterator broken, or in others words, need re-initializing
 */
static boolean itr_broken = TRUE;

/**
 * How many changes are pending within the table (for now, just routes)
 */
static uint32_t changes_pending = 0;

/**
 * Cleaning of the table is necessary
 */
static boolean clean_table = FALSE;

// HASHTABLE STUFF (key and value)

/**
 * The key for the hashtable
 */
typedef struct key_s {
    char       ifname[MAX_IF_NAME_LEN + 1]; ///< Interface name
    uint8_t    af;                          ///< Address family
} hash_key_t;


/*** STATIC/INTERNAL Functions ***/


// Define functions ht_insert, ht_get, and ht_remove

/**
 * We use ht_insert to insert a (key,value) safely into the hashtable
 */
static DEFINE_HASHTABLE_INSERT(ht_insert, hash_key_t, policy_table_entry_t);

/**
 * We use ht_get to get a (key,value) safely from the hashtable
 */
static DEFINE_HASHTABLE_SEARCH(ht_get, hash_key_t, policy_table_entry_t);

/**
 * We use ht_remove to remove a (key,value) safely from the hashtable
 */
static DEFINE_HASHTABLE_REMOVE(ht_remove, hash_key_t, policy_table_entry_t);


/**
 * Returns the hash value of a key:
 * 
 * @param[in] ky
 *    The key to be typecasted to (hash_key_t *)
 * 
 * @return a hash of the key (key's contents)
 */
static unsigned int
hashFromKey(void * ky)
{
    hash_key_t *k = (hash_key_t *)ky;    
    unsigned int hashcode = 0, c;
    char *str;
    
    INSIST(k != NULL);
    str = k->ifname;

    while((c = *str++)) // requires str to be '\0' terminated
        hashcode = c + (hashcode << 6) + (hashcode << 16) - hashcode;

    hashcode += k->af;

    return hashcode;
}


/**
 * Compare two keys:
 * 
 * @param[in] k1
 *    First key
 * 
 * @param[in] k2
 *    Second key
 * 
 * @return 1 is keys are equal, 0 otherwise
 */
static int
equalKeys(void * k1, void * k2)
{
    INSIST(k1 != NULL);
    INSIST(k2 != NULL);

    return(((hash_key_t *)k1)->af == ((hash_key_t *)k2)->af &&
            (0 == strcmp(((hash_key_t *)k1)->ifname, ((hash_key_t *)k2)->ifname)));
}


/**
 * Compare two routes:
 * 
 * @param[in] r1
 *    First route
 * 
 * @param[in] r2
 *    Second route
 * 
 * @return TRUE if routes are equal, FALSE otherwise
 */
static boolean
equalRoutes(policy_route_msg_t * r1, policy_route_msg_t * r2)
{ 
    return((r1->route_addr == r2->route_addr) &&
           (r1->preferences == r2->preferences));
}


/**
 * Get or create the policy in the table
 * 
 * @param[in] ifname
 *    Interface name
 * 
 * @param[in] af
 *    Address family
 * 
 * @return the entry in the table
 */
static policy_table_entry_t *
get_or_create_policy(char * ifname, uint8_t af)
{
    hash_key_t * k;
    policy_table_entry_t * v;
    
    // Create key
    k = (hash_key_t *)malloc(sizeof(hash_key_t));
    INSIST(k != NULL);
    
    // Copy interface name and address family.
    strcpy(k->ifname, ifname);
    k->af = af;
    
    if((v = ht_get(if_table, k)) == NULL) {
        v = (policy_table_entry_t *)malloc(sizeof(policy_table_entry_t));
        INSIST(v != NULL);
        bzero(v, sizeof(policy_table_entry_t));
        strcpy(v->ifname, ifname);
        v->af = af;
        ht_insert(if_table, k, v);
    } else {
        free(k);
    }
    
    return v;
}


/**
 * Change the status of a route. If status is becoming failed, then
 * we will also break the policy (set broken to true).
 * 
 * @param[in] route
 *     Route data containing ifname and family as well
 * 
 * @param[in] status
 *     New status
 * 
 * @return
 *      TRUE if successfully changed;
 *      otherwise FALSE (policy or route not found)
 */
static boolean
mark_route(policy_route_msg_t * route, policy_route_status_e status)
{
    hash_key_t k;
    policy_table_entry_t * pol;
    ped_policy_route_t * r;

    INSIST(if_table != NULL);
    INSIST(route != NULL);
    INSIST(strlen(route->ifname) <= MAX_IF_NAME_LEN);
    
    junos_trace(PED_TRACEFLAG_HT, "%s", __func__);

    strcpy(k.ifname, route->ifname);
    k.af = route->af;

    pol = ht_get(if_table, &k);

    if(pol) { // if policy exists
        
        // Search routes for route
        
        for(r = pol->route; pol != NULL; r = r->next) {
            if(equalRoutes(route, &r->route_data)) {
                if(r->status == ROUTE_PENDING && status != ROUTE_PENDING) {
                    --changes_pending;
                }
                if(status == ROUTE_FAILED) {
                    // break it but don't delete it
                    // because we could get more info coming in for this policy
                    // and we have to know that it is broken
                    pol->broken = TRUE;
                    clean_table = TRUE;
                }
                r->status = status;
                return TRUE;
            }
        }
    }
    
    return FALSE;
}


/**
 * Callback when making a request of our SSD module. If the request was 
 * accepted, then this function will be called when the request status has
 * changed. In other words, a request to add or delete a route has succeeded
 * or failed.
 * 
 * @param[in] reply
 *     the status of the request
 * 
 * @param[in] user_data
 *     user_data passed to ssd_client_add/delete_route_request that's getting
 *     passed back here. This is a (policy_route_msg_t *) that we need to free.
 *     (Will be NULL when associated policy was broken). 
 */
static void
ssd_request_status(ssd_reply_e reply, void * user_data)
{
    policy_route_msg_t * route_data;
    struct in_addr  addr; // for printing nicely
    
    route_data = (policy_route_msg_t *)user_data;
    
    if(route_data)
        addr.s_addr = route_data->route_addr;
    
    // process reply
    
    switch(reply) {

    case ADD_SUCCESS:
        
        INSIST(route_data != NULL);
            
        junos_trace(PED_TRACEFLAG_NORMAL,
            "%s: route %s/%d was successfully added through SSD",
            __func__, inet_ntoa(addr), route_data->route_addr_prefix);
            
        mark_route(route_data, ROUTE_ADDED);

        break;
        
    case ADD_FAILED:
    
        INSIST(route_data != NULL);
    
        junos_trace(PED_TRACEFLAG_NORMAL,
            "%s: route %s/%d was not successfully added through SSD, "
            "so policy will be marked for removal.",
            __func__, inet_ntoa(addr), route_data->route_addr_prefix);
        
        // we need to mark this policy and route as bad
        // we can't delete it because it could be re-created, but without 
        // this route meaning it would be incorrect
        
        mark_route(route_data, ROUTE_FAILED);
        
        // we are breaking a policy so it should be cleaned up eventually
        // when our ssd module becomes idle or else future req on this policy
        // won't work because the broken policy will still be around
        clean_table = TRUE;

        break;
        
    case DELETE_SUCCESS:
            
        if(route_data == NULL) {
            // route deleted from a broken policy
            junos_trace(PED_TRACEFLAG_NORMAL,
                "%s: route was successfully deleted through SSD", __func__);
        } else {
            junos_trace(PED_TRACEFLAG_NORMAL,
                "%s: route %s/%d was successfully deleted through SSD",
                __func__, inet_ntoa(addr), route_data->route_addr_prefix);
        }
            
        // nothing to do on policy table because the policy 
        // no longer stores this route anyway
            
        break;
        
    case DELETE_FAILED:

        if(route_data == NULL) {
            // route deleted from a broken policy
            
            ERRMSG(PED, TRACE_LOG_WARNING,
                "%s: Request to delete a route from a broken policy FAILED. "
                "WARNING: may have a dangling route if this route was " 
                "previously installed (may not if route was not installed).",
                __func__);
        } else {
            // route deleted from a policy because route 
            // is no longer part of the policy
            
            ERRMSG(PED, TRACE_LOG_WARNING,
                "%s: Request to delete route %s/%d from a policy FAILED. "
                "WARNING: may have a dangling route if this route was " 
                "previously installed (may not if route was not installed). "
                "Retrying delete without SSD confirmation",
                __func__, inet_ntoa(addr), route_data->route_addr_prefix);

            // try again without confirmation
            // (unreliable but better than nothing)
            ssd_client_del_route_request(route_data, NULL, NULL);
        }

        break;
        
    default:
    
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Didn't understand the type of reply", __func__);
    }
    
    if(route_data)
        free(route_data);
}


/**
 * Add or change a route through our SSD module
 *
 * @param[in] route_data
 *     The pointer to route data
 * 
 * @return
 *      TRUE if route could be added and confirmation will be received upon
 *      a respose from SSD; FALSE if the SSD module would bto accept the route-
 *      add request
 */
static boolean
add_route(policy_route_msg_t * route_data)
{
    policy_route_msg_t * route;
    
    // make a copy of the route data passed in
    
    route = (policy_route_msg_t *)malloc(sizeof(policy_route_msg_t));
    INSIST(route != NULL);
    memcpy(route, route_data, sizeof(policy_route_msg_t));
    
    if(!ssd_client_add_route_request(route_data, ssd_request_status, route)) {
            
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: SSD did not accept request to add a route.", __func__);
            
        // Request to add route was NOT accepted
        return FALSE;            
    }
    
    return TRUE;
}


/**
 * Remove a route through our SSD module. If we can't request confirmation, we
 * log a warning message and attempt removal without a callback for confirmation
 *
 * @param[in] route
 *     The pointer to route in the table
 */
static void
remove_route(ped_policy_route_t * route)
{
    policy_route_msg_t * route_data;
        
    route_data = (policy_route_msg_t *)malloc(sizeof(policy_route_msg_t));
    INSIST(route_data != NULL);
    memcpy(route_data, &route->route_data, sizeof(policy_route_msg_t));
    
    // Delete route from SSD
    if(!ssd_client_del_route_request(
        &route->route_data, ssd_request_status, route_data)) {

        // try without confirmation
        // (unreliable but better than nothing)
        ssd_client_del_route_request(
            &route->route_data, NULL, NULL);

        ERRMSG(PED, TRACE_LOG_WARNING,
            "%s: Cannot remove route because request to delete "
            "the route with confirmation was not accepted. "
            "Trying without, but route may be left dangling "
            "if operation fails.", __func__);

        free(route_data);
    }
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Adds a filter with the given interface name and address family to the table.
 * Only adds it if it does not yet exist in the table.
 * 
 * @param[in] filter_data
 *     Filter part of policy, containing interface name and address family
 */
void
policy_table_add_filter(policy_filter_msg_t * filter_data)
{
    policy_table_entry_t * policy;

    INSIST(if_table != NULL);
    INSIST(filter_data != NULL);
    INSIST(strlen(filter_data->ifname) <= MAX_IF_NAME_LEN);

    junos_trace(PED_TRACEFLAG_HT, "%s: %s, af: %d", __func__,
        filter_data->ifname, filter_data->af);

    policy = get_or_create_policy(filter_data->ifname, filter_data->af);

    if(policy->broken) {
        // if the policy is broken then this whole policy 
        // will be deleted because something's wrong with it
        // ...So, don't add the filter
        return;
    }
    
    if(!policy->pfd_filter) {
        policy->pfd_filter = apply_pfd_filter_to_interface(policy->ifname);
    }
    
    if(!policy->filter) {
        // Create filter if it doesn't exist
        policy->filter = (ped_policy_filter_t *)malloc(sizeof(ped_policy_filter_t));
        INSIST(policy->filter != NULL);
    }

    // Update filter data
    memcpy(&(policy->filter->filter_data), filter_data,
            sizeof(policy_filter_msg_t));

    policy->filter->status = FILTER_PENDING;
    
    if(apply_filters_to_interface(policy->ifname, policy->filter)) {
        policy->filter->status = FILTER_ADDED;
    } else {
        policy->filter->status = FILTER_FAILED;
        policy->broken = TRUE; // break it so it gets cleaned
        clean_table = TRUE;
        
        ERRMSG(PED, TRACE_LOG_ERR, "%s: Failed to apply filters"
            "on interface", __func__, policy->ifname);
    }
}


/**
 * Adds a route with the given interface name and address family to the table.
 * Only adds it if it does not yet exist in the table.
 * 
 * @param[in] route_data
 *     Route data
 */
void
policy_table_add_route(policy_route_msg_t * route_data)
{
    policy_table_entry_t * policy;
    ped_policy_route_t *route = NULL;
    ped_policy_route_t *route_end = NULL;

    INSIST(if_table != NULL);
    INSIST(route_data != NULL);
    INSIST(strlen(route_data->ifname) <= MAX_IF_NAME_LEN);

    junos_trace(PED_TRACEFLAG_HT,
            "%s: %s, %d", __func__, route_data->ifname, route_data->af);

    policy = get_or_create_policy(route_data->ifname, route_data->af);

    if(policy->broken) {
        // if the policy is broken then this whole policy 
        // will be deleted because something's wrong with it
        // ...So, don't add the route
        return;
    }
    
    if(!policy->pfd_filter) {
        policy->pfd_filter = apply_pfd_filter_to_interface(policy->ifname);
    }
    
    // Search route list for same route
    route_end = route = policy->route;
    
    while(route) {
        if(equalRoutes(route_data, &route->route_data)) {
            break;
        }
        route_end = route;
        route = route->next;
    }
    
    if(!route) { // No match, so create route data
        route = (ped_policy_route_t *)malloc(sizeof(ped_policy_route_t));
        INSIST(route != NULL);
        route->next = NULL;

        // Add route data to the list.
        if(route_end) {
            route_end->next = route;
        } else {
            policy->route = route;
        }
    }

    // Copy in route data.
    memcpy(&(route->route_data), route_data, sizeof(policy_route_msg_t));
    
    // Add or change the route itself 
    if(add_route(route_data)) {
        route->status = ROUTE_PENDING;
        ++changes_pending;
    } else {
        route->status = ROUTE_FAILED;
        policy->broken = TRUE;
        clean_table = TRUE;
    }
}


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
void
policy_table_clear_policy(char *ifname, uint8_t af)
{
    policy_table_entry_t * policy;
    ped_policy_route_t  * route = NULL, * route_tmp = NULL;

    INSIST(if_table != NULL);
    INSIST(ifname != NULL);
    INSIST(strlen(ifname) <= MAX_IF_NAME_LEN);
    
    junos_trace(PED_TRACEFLAG_HT, "%s(%s, %d)", __func__, ifname, af);

    policy = get_or_create_policy(ifname, af);

    // just remove anything but the pfd filter

    if(policy->broken) {
        return;
    }
    
    if(policy->filter) {
        free(policy->filter);
        policy->filter = NULL;
    }
        
    if(!remove_filters_from_interface(ifname)) {
        policy->broken = TRUE; // break it so it gets cleaned
        
        ERRMSG(PED, TRACE_LOG_ERR, "%s: Failed to remove "
            "filters on interface ", __func__, ifname);
    }
    
    route = policy->route;
    policy->route = NULL;
    
    while(route) {
        route_tmp = route;      // Save pointer to current route
        route = route->next;    // Move to the next route
        remove_route(route_tmp);
        if(route->status == ROUTE_PENDING) {
            --changes_pending;
        }
        free(route_tmp);        // Free the current route data.
    }
}


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
policy_table_delete_policy(char *ifname, uint8_t af, boolean interface_exists)
{
    hash_key_t k;
    policy_table_entry_t * policy;
    ped_policy_route_t  * route = NULL, * route_tmp = NULL;

    INSIST(if_table != NULL);
    INSIST(ifname != NULL);
    INSIST(strlen(ifname) <= MAX_IF_NAME_LEN);
    
    junos_trace(PED_TRACEFLAG_HT, "%s(%s, %d)", __func__, ifname, af);

    strcpy(k.ifname, ifname);
    k.af = af;

    policy = ht_remove(if_table, &k); // frees the key

    if(policy == NULL) return;
     
    // remove and delete filters only if the interface still exists
    // (if interface is deleted so are all attached filters)
    if(interface_exists) {
        if(policy->filter) {
            remove_filters_from_interface(ifname);
            free(policy->filter);
        }
        
        if(policy->pfd_filter) {
            remove_pfd_filter_from_interface(policy->ifname);
            policy->pfd_filter = FALSE;
        }
    }
    
    // remove and delete routes
    
    route = policy->route;
    while(route) {
        route_tmp = route;      // Save pointer to current route.
        route = route->next;    // Move to the next route.
        if(route->status != ROUTE_FAILED) {
            remove_route(route_tmp);
            if(route->status == ROUTE_PENDING) {
                --changes_pending;
            }
        }
        free(route_tmp);        // Free the current route data.
    }

    free(policy);
}


/**
 * Mark all policies in the table UNVERIFIED. Since we can't delete routes or
 * filters, all that are left unverified (i.e. they don't get 
 * added again) when calling policy_table_clean() get removed.
 * 
 * @return
 *      TRUE upon success;
 *      FALSE if aborted because there's still changes pending 
 *            in which case all route statuses stay the same
 */
boolean
policy_table_unverify_all(void)
{
    policy_table_entry_t * pol = NULL;
    ped_policy_route_t * route = NULL;
    struct hashtable_itr * iterator = NULL;
    
    if(hashtable_count(if_table) < 1)
        return TRUE; // nothing to do
        
    if(changes_pending > 0)
        return FALSE; // don't do anything if there's still changes pending

    junos_trace(PED_TRACEFLAG_HT, "%s", __func__);

    iterator = hashtable_iterator(if_table); // mallocs
    INSIST(iterator != NULL);

    // go through all policies
    do {
        pol = hashtable_iterator_value(iterator);
        
        // for all filters and routes in route lists set status to UNVERIFIED
        
        if(!pol->broken) {
            if(pol->filter) {
                pol->filter->status = FILTER_UNVERIFIED;
            }
            if((route = pol->route)) {
                while(route) {
                    // changes_pending is wrong if this fails
                    INSIST(route->status != ROUTE_PENDING);
                    route->status = ROUTE_UNVERIFIED;
                    route = route->next;
                }
            }
        }
        
    } while (hashtable_iterator_advance(iterator));

    free(iterator);
    
    clean_table = TRUE;
    
    return TRUE;
}


/**
 * Clean policy table, remove all UNVERIFIED filters and routes.
 * Anything left in the UNVERIFIED state (status) will be deleted.
 */
void
policy_table_clean(void)
{
    policy_table_entry_t * policy = NULL;
    ped_policy_route_t * route_tmp = NULL, * route = NULL;
    struct hashtable_itr * iterator = NULL;
    
    if(hashtable_count(if_table) < 1 || !clean_table ||
       !get_ssd_ready() || !get_ssd_idle()) {
        return; // nothing to do or don't clean yet
    }
    
    junos_trace(PED_TRACEFLAG_HT, "%s", __func__);
    
    iterator = hashtable_iterator(if_table); // mallocs
    INSIST(iterator != NULL);

    // go through all policies
    do {
        policy = hashtable_iterator_value(iterator);
        
        // if we have a broken policy, then remove it
        if(policy->broken) {
            
            // remove and delete filters
            if(policy->filter) {
                remove_filters_from_interface(policy->ifname);
                free(policy->filter);
            }
            
            if(policy->pfd_filter) {
                remove_pfd_filter_from_interface(policy->ifname);
                policy->pfd_filter = FALSE;
            }
            
            // remove and delete routes
            
            route = policy->route;
            while(route) {
                route_tmp = route;      // Save pointer to current route.
                route = route->next;    // Move to the next route.
                if(route_tmp->status != ROUTE_FAILED) {
                    remove_route(route_tmp);
                    if(route->status == ROUTE_PENDING) {
                        --changes_pending;
                    }
                }
                free(route_tmp);        // Free the current route data.
            }

            free(policy);
            
            if(hashtable_iterator_remove(iterator))
                continue;
            else
                break;
        }
        
        // else: policy is not broken 
        // but we will check for routes or filters that don't belong
        
        
        // Check filter:
        if(policy->filter && policy->filter->status == FILTER_UNVERIFIED) {
            free(policy->filter);
            policy->filter = NULL;
            remove_filters_from_interface(policy->ifname);
        }
        
        if(!policy->pfd_filter) {
            policy->pfd_filter = apply_pfd_filter_to_interface(policy->ifname);
        }

        // Check route(s):        
        route = policy->route;
        route_tmp = NULL;
        policy->route = NULL;  // Detach route list temporarily
        
        while(route) {
            
            // check we don't get FAILED routes (in non-broken policies)
            if(route->status == ROUTE_FAILED) {
                // log and crash because this shouldn't happen
                ERRMSG(PED, TRACE_LOG_EMERG, 
                    "%s: Found a route with status=FAILED in a non-broken "
                    "policy. This should not happen.", __func__);
                // abort() is called in the above ERRMSG
            }
            
            // its status should probably be ADDED...
            // unless it's not part of the PSD policy anymore it will be:
            // UNVERIFIED
            if(route->status == ROUTE_UNVERIFIED) {
                // we've received all routes from the PSD, so it is  
                // no longer part of the policy, and we need to delete it
                
                // copy route data into new mem to pass to callback
                // we choose to do this only when policy is not broken
                
                if(route_tmp) { // route_tmp points to last kept route

                    route_tmp->next = route->next;
                    
                    remove_route(route);
                    free(route);
                    
                    // make route point to the next route (in tmp->next) and
                    route = route_tmp->next;
                    
                } else {  // No previous routes that we've kept yet
                    
                    // use tmp ptr to save next
                    route_tmp = route->next;
                    
                    remove_route(route);
                    free(route);
                    
                    // restore route to next route (in tmp) and tmp to NULL
                    route = route_tmp;
                    route_tmp = NULL;
                }
            } else { // a route we are keeping

                if(policy->route == NULL) {
                    policy->route = route;  // First route in the new list
                }
                
                route_tmp = route; // move route_tmp to end
                route = route->next;
            }
        }
        
        // Done checking route(s)
        
    } while (hashtable_iterator_advance(iterator));

    free(iterator);
    
    clean_table = FALSE;
}


/**
 * Initializes the table for first use.
 */
void
init_table(void)
{
    junos_trace(PED_TRACEFLAG_HT, "%s", __func__);
    
    if(if_table)
        destroy_table();

    if_table = create_hashtable(16, hashFromKey, equalKeys);

    INSIST(if_table != NULL);
}


/**
 * Destroy the table. It should be empty or this will cause a memory leak.
 */
void
destroy_table(void)
{
    junos_trace(PED_TRACEFLAG_HT, "%s", __func__);
    
    hashtable_destroy(if_table, TRUE);
    if_table = NULL;
}


/**
 * Get the number of entries in the table of managed interfaces.
 * 
 * @return The number of entries in the table
 */
int
policy_table_entry_count(void)
{
    INSIST(if_table != NULL);
    return hashtable_count(if_table);
}


/**
 * Reset the iterator. This should always be called before 
 * using policy_table_next() to start iterating over entries.
 */
void
policy_table_iterator_reset(void)
{
    itr_broken = TRUE;
}


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
policy_table_entry_t *
policy_table_next(void)
{
    policy_table_entry_t *v;

    INSIST(if_table != NULL);
    if(hashtable_count(if_table) < 1)
        return NULL;

    if(itr_broken) {
        if(itr)
            free(itr);
        itr = hashtable_iterator(if_table); // mallocs
        INSIST(itr != NULL);
        itr_broken = FALSE;
        // now it's at the first element
    } else if (!hashtable_iterator_advance(itr)){
        // no more
        free(itr);
        itr = NULL;
        itr_broken = TRUE; // will reset next time around

        return NULL;
    }

    v = hashtable_iterator_value(itr);

    return v;
}

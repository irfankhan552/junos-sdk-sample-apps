/*
 * $Id: psd_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file psd_config.c
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions and variables will parse, load, hold, and return
 * the configuration data.
 */
#include <sync/common.h>
#include <sync/psd_ipc.h>
#include <ddl/dax.h>
#include "psd_server.h"
#include "psd_config.h"

#include PS_OUT_H

/*** Constants ***/

#define EMPTY -3               ///< empty config return code
#define DEFAULT_ROUTE_METRIC 5 ///< default route metric
#define DEFAULT_ROUTE_PREF   5 ///< default route preference

/*** Data structures: ***/


static patroot root_policy; ///< patricia tree root


/*** STATIC/INTERNAL Functions ***/


/**
 * Generate the policy_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(policy_entry, psd_policy_t, node)


/**
 * Remove a policy from the configuration, used only internally
 * when emptying the patricia tree (in clear_config)
 * 
 * @param[in] policy
 *     Policy to be deleted from the configuration data storage
 */
static void
delete_policy(psd_policy_t *policy)
{
    psd_policy_route_t *route, *route_tmp;

    //Free filter policy data.
    free(policy->filter);

    //Free the list of route policy data.
    route = policy->route;
    while(route) {
        route_tmp = route->next;  // Save pointer to next route.
        free(route);  // Free current route.
        route = route_tmp;  // Move to next route.
    }

    //Remove policy from patricia tree.
    if (!patricia_delete(&root_policy, &policy->node)) {
        junos_trace(PSD_TRACEFLAG_NORMAL, 
                "%s: Delete policy FAILED!", __func__);
    }
}


/**
 * Clear the configuration info completely from the patricia tree
 */
static void
clear_config_policy(void)
{
    psd_policy_t *policy = NULL;

    while((policy = first_policy()) != NULL) {
        delete_policy(policy);  // Remove policy from configuration
        free(policy);  // Free policy
    }
}


/**
 * Read route configuration
 *
 * @param[in] policy_op
 *      DDL object pointer.
 *
 * @param[in] policy
 *      Pointer to policy.
 *
 * @return
 *      SUCCESS, or EFAIL on error; EMPTY when no routes
 */
static int
config_read_route(ddl_handle_t *policy_op, psd_policy_t *policy)
{
    ddl_handle_t *route_list_op = NULL;
    ddl_handle_t *route_op = NULL;
    psd_policy_route_t *route = NULL;
    psd_policy_route_t *route_end = NULL;
    int val_af; // we assume IPv4 is always returned
    struct in_addr addr;
    int status = SUCCESS;

    policy->route = NULL;

    if(!dax_get_object_by_aid(policy_op, DDLAID_SYNC_PS_POLICY_ROUTE,          
            &route_list_op, FALSE)) {
        return EMPTY;  // No route configuration, no complain.
    }

    while(dax_visit_container(route_list_op, &route_op)) {

        // Create route data.
        route = calloc(1, sizeof(psd_policy_route_t));
        INSIST(route != NULL);

        // Read route address, support IPv4 only for now.
        if(!dax_get_ipprefix_by_aid(route_op, ROUTE_ADDRESS, &val_af,
                &(route->route_data.route_addr), 4,
                &(route->route_data.route_addr_prefix))) {
            dax_error(route_op, "Read route address FAILED!");
            status = EFAIL;
            free(route);
            break;
        }

        if(!dax_get_uint_by_aid(route_op, METRICS,
                &(route->route_data.metrics))) {
            route->route_data.metrics = DEFAULT_ROUTE_METRIC;
        }
        if(!dax_get_uint_by_aid(route_op, PREFERENCES,
                &(route->route_data.preferences))) {
            route->route_data.preferences = DEFAULT_ROUTE_PREF;
        }
        if(!dax_get_ubyte_by_aid(route_op, NH_TYPE,
                &(route->route_data.nh_type))) {
            dax_error(route_op, "Read next-hop address FAILED!");
            status = EFAIL;
            free(route);
            break;
        }

        addr.s_addr = route->route_data.route_addr;
        
        junos_trace(PSD_TRACEFLAG_CONFIG,
                "%s: route address: %s/%d", __func__,
                inet_ntoa(addr), route->route_data.route_addr_prefix);
                
        junos_trace(PSD_TRACEFLAG_CONFIG, "%s: m: %d, p: %d",
                __func__, route->route_data.metrics,
                route->route_data.preferences);
                
        junos_trace(PSD_TRACEFLAG_CONFIG, "%s: next-hop type: %d",
                __func__, route->route_data.nh_type);

        // if we have a nh address, type is unicast
        dax_get_ipaddr_by_aid(route_op, NH_ADDRESS,
                &val_af, &(route->route_data.nh_addr), 4);

        if(route->route_data.nh_type == PSD_NH_TYPE_UNICAST) {
            addr.s_addr = route->route_data.nh_addr;
            junos_trace(PSD_TRACEFLAG_CONFIG, "%s: next-hop address: %s",
                __func__, inet_ntoa(addr));
        }

        // Copy interface name and address family to route data.
        strcpy(route->route_data.ifname, policy->ifname);
        route->route_data.af = policy->af;
        route->next = NULL;

        if(policy->route) {
            // Add route data to the end of the list.
            route_end->next = route;
        } else {
            // This is the first route data in the list.
            policy->route = route;
        }

        // Set pointer to the last route in the list.
        route_end = route;
    }
    // note: if status is EFAIL, then route list will be free later

    dax_release_object(&route_list_op);
    dax_release_object(&route_op);
    return status;
}


/**
 * Read filter configuration
 *
 * @param[in] policy_op
 *     DDL object pointer.
 *
 * @param[in] policy
 *     Pointer to policy.
 *
 * @return
 *      SUCCESS; EFAIL on error; EMPTY when no routes
 */
static int
config_read_filter(ddl_handle_t *policy_op, psd_policy_t *policy)
{
    ddl_handle_t *filter_op = NULL;
    psd_policy_filter_t *filter = NULL;
    int status = SUCCESS;

    policy->filter = NULL;
    if(!dax_get_object_by_aid(policy_op, DDLAID_SYNC_PS_POLICY_FILTER,          
            &filter_op, FALSE)) {
        return EMPTY;  // No filter configuration, no complain.
    }

    // Create filter data.
    filter = calloc(1, sizeof(psd_policy_filter_t));
    INSIST(filter != NULL);

    // Read input filter and output filter.
    dax_get_stringr_by_aid(filter_op, POLICY_INPUT_FILTER,
            filter->filter_data.input_filter, MAX_FILTER_NAME_LEN);
    dax_get_stringr_by_aid(filter_op, POLICY_OUTPUT_FILTER, 
            filter->filter_data.output_filter, MAX_FILTER_NAME_LEN);

    junos_trace(PSD_TRACEFLAG_CONFIG,
            "%s: Input filter: %s, Output filter: %s", __func__,
            filter->filter_data.input_filter,
            filter->filter_data.output_filter);

    // At least one filter must be specified
    if(filter->filter_data.input_filter[0] ||
            filter->filter_data.output_filter[0]) {
        
        // Copy interface name and address family to filter data.
        strcpy(filter->filter_data.ifname, policy->ifname);
        filter->filter_data.af = policy->af;
        // Set filter pointer in policy data.
        policy->filter = filter;
    } else {
        free(filter);
        policy->filter = NULL;
        status = EFAIL;
    }
    
    dax_release_object(&filter_op);
    return status;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structure that will store configuration info,
 * or in other words, the policies.
 */
void
init_config(void)
{
    patricia_root_init(&root_policy, FALSE, MAX_POLICY_NAME_LEN + 1, 0);
                      // root, is key ptr, key size, key offset
}


/**
 * Get the first policy from configuration.
 *
 * @return
 *      The first policy in configuration,
 *      or NULL if no policy is in configuration.
 */
psd_policy_t *
first_policy(void)
{
    return policy_entry(patricia_find_next(&root_policy, NULL));
}

/**
 * Get (iterator-style) the next policy from configuration.
 *
 * @param[in] policy
 *      The previously returned policy from this function,
 *      or NULL for the first policy.
 *
 * @return
 *      The next policy in configuration,
 *      or NULL if data is the last one in configuration.
 */
psd_policy_t *
next_policy(psd_policy_t *policy)
{
    return policy_entry(patricia_find_next(&root_policy,
            (policy ? &(policy->node) : NULL)));
}

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
int
psd_config_read(int check)
{
    ddl_handle_t *policy_op = NULL;
    ddl_handle_t *policy_list_op = NULL;
    const char   *psd_policy[] =    {DDLNAME_SYNC,
                                     DDLNAME_SYNC_PS,
                                     DDLNAME_SYNC_PS_POLICY, NULL};
    psd_policy_t *policy = NULL;
    psd_policy_route_t *route = NULL;
    psd_policy_route_t *route_tmp = NULL;
    int status = 0;

    junos_trace(PSD_TRACEFLAG_CONFIG,
            "%s: Load configuration, check %d",
            __func__, check);

    // Read policy configuration, get the pointer the policy list.
    //                               (FALSE flag = regardless of if was changed)
    if(!dax_get_object_by_path(NULL, psd_policy, &policy_list_op, FALSE)) {
        
        // clear stored config data because we have no configure data
        clear_config_policy();
        junos_trace(PSD_TRACEFLAG_CONFIG,
                "%s: Nothing to configure", __func__);
        return SUCCESS;  // No policy, no complain.
    }

	// Load policy only if something changed.
    if(dax_is_changed(policy_list_op)) {

        // Clear policy configuration, get ready for new data.
        clear_config_policy();

        // Get pointer to policy object.
        while(dax_visit_container(policy_list_op, &policy_op)) {

            // Create and clear policy data.
            policy = calloc(1, sizeof(psd_policy_t));
            INSIST(policy != NULL);

            // Initialize pointers.
            policy->filter = NULL;
            policy->route = NULL;

            // Read policy name.
            if(!dax_get_stringr_by_aid(policy_op, POLICY_NAME,
                    policy->policy_name, sizeof(policy->policy_name))) {
                dax_error(policy_op, "Read policy name FAILED!");
                goto error;
            }

            // Read interface name.
            if(!dax_get_stringr_by_aid(policy_op, POLICY_INTERFACE_NAME,
                    policy->ifname, sizeof(policy->ifname))) {
                dax_error(policy_op, "Read interface name FAILED!");
                goto error;
            }

            // Read address family.
            if(!dax_get_ubyte_by_aid(policy_op, POLICY_ADDRESS_FAMILY,
                    &(policy->af))) {
                dax_error(policy_op, "Read address family FAILED!");
                goto error;
            }

            if((status = config_read_filter(policy_op, policy)) == EFAIL) {
                dax_error(policy_op, "Read filter object FAILED!");
                goto error;
                
            } else if(status == EMPTY) { // no filter
                if((status = config_read_route(policy_op, policy)) == EFAIL) {
                    dax_error(policy_op, "Read list of route objects FAILED!");
                    goto error;
                } else if(status == EMPTY) { // no routes (and no filter)
                    // empty policy
                    dax_error(policy_op,
                        "Policy contains no filter and no route(s)!");
                    goto error;
                }
                
            } else {
                if((status = config_read_route(policy_op, policy)) == EFAIL) {
                    dax_error(policy_op, "Read list of route objects FAILED!");
                    goto error;
                }                
            }
            

            // Add policy data to patricia tree.
            patricia_node_init_length(&policy->node,
                    strlen(policy->policy_name) + 1);

            if(!patricia_add(&root_policy, &policy->node)) {
                dax_error(policy_op, 
                    "Adding policy to loaded configuration FAILED!");
               goto error;
            }
        }
        junos_trace(PSD_TRACEFLAG_CONFIG,
                "%s: Load configuration DONE.", __func__);
    } else {
        junos_trace(PSD_TRACEFLAG_CONFIG,
                "%s: No configuration change", __func__);
    }

    dax_release_object(&policy_op);
    dax_release_object(&policy_list_op);
    
    if(!check) {
        notify_all_clients(); // Tell PED to update
    }
    return SUCCESS;

error:
    if(policy) {
        if(policy->filter) {
            free(policy->filter);
        }
        route = policy->route;
        while(route) {
            route_tmp = route->next;
            free(route);
            route = route_tmp;
        }
        free(policy);
    }
    dax_release_object(&policy_op);
    dax_release_object(&policy_list_op);
    return -1;
}

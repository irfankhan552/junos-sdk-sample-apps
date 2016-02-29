/*
 * $Id: dpm-ctrl_config.c 365138 2010-02-27 10:16:06Z builder $
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
 * @file dpm-ctrl_config.c
 * @brief Relating to getting and setting the configuration data 
 *        
 * 
 * These functions will store and provide access to the 
 * configuration data which is essentailly coming from the mgmt component.
 */

#include "dpm-ctrl_main.h"
#include <jnx/vrf_util_pub.h>
#include <jnx/rt_shared_pub.h>
#include <jnx/ipc_types.h>
#include <jnx/ipc_msp_pub.h>
#include "dpm-ctrl_dfw.h"
#include "dpm-ctrl_http.h"
#include "dpm-ctrl_config.h"


/*** Constants ***/

#ifndef VRFINDEX_ERROR
#define VRFINDEX_ERROR -1 ///< Error code when not able to get VRF index
#endif

/*** Data structures ***/

/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each policer
 */
typedef struct policer_s {
    patnode        node;         ///< Tree node
    policer_info_t policer __attribute__ ((packed));      ///< policer
} policer_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each interface default policy
 */
typedef struct int_pol_s {
    patnode  node;                         ///< Tree node in int_conf
    char     ifl_name[MAX_INT_NAME + 1];   ///< IFL interface name (w/ subunit)
    patnode  node2;                        ///< Tree node in int_conf2
    uint32_t index;                        ///< interface index
    char     input_pol[MAX_POL_NAME + 1];  ///< input policer name
    char     output_pol[MAX_POL_NAME + 1]; ///< output policer name
} int_pol_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each subscriber
 */
typedef struct subscriber_s {
    patnode    node;        ///< Tree node
    sub_info_t subscriber;  ///< subscriber
    policer_t  * policer;   ///< policer in policy
} subscriber_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each user (subscriber that is logged in)
 */
typedef struct user_s {
    patnode       node;        ///< Tree node
    in_addr_t     address;     ///< subscriber
    subscriber_t  * sub;       ///< policer in policy
    int_pol_t     * interface; ///< where policy is applied
} user_t;

extern msp_fdb_handle_t fdb_handle; ///< handle for FDB (forwarding DB)
static uint8_t use_classic_filters; ///< filter application mode (T/F value)
static int     vrf_default;         ///< VRF index of default routing instance
static patroot sub_conf;            ///< Pat. root for subscribers
static patroot user_conf;           ///< Pat. root for logged in subscribers
static patroot pol_conf;            ///< Pat. root for policers

/**
 * @brief Pat. root for interface def. policies
 * Patricia tree root for interface default policies indexed by interface name
 */
static patroot int_conf;

/**
 * @brief Pat. root for interface def. policies
 * Patricia tree root for interface default policies indexed by interface index
 */
static patroot int_conf2;


/*** STATIC/INTERNAL Functions ***/

/**
 * Generate the pol_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(pol_entry, policer_t, node)


/**
 * Generate the int_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 * 
 * Only use with int_conf NOT int_conf2
 */
PATNODE_TO_STRUCT(int_entry, int_pol_t, node)


/**
 * Generate the int_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 * 
 * Only use with int_conf2 NOT int_conf
 */
PATNODE_TO_STRUCT(int_entry2, int_pol_t, node2)


/**
 * Generate the sub_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(sub_entry, subscriber_t, node)


/**
 * Generate the user_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(user_entry, user_t, node)


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 * 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_configuration(void)
{
    use_classic_filters = 0;
    
    patricia_root_init(&pol_conf, FALSE, MAX_POL_NAME + 1, 0);
    patricia_root_init(&int_conf, FALSE, MAX_INT_NAME + 1, 0);
    patricia_root_init(&int_conf2, FALSE, sizeof(uint32_t), 0);
    patricia_root_init(&sub_conf, FALSE, MAX_SUB_NAME + 1, 0);
    patricia_root_init(&user_conf, FALSE, sizeof(in_addr_t), 0);
    
    vrf_default = vrf_getindexbyvrfname(
                    JUNOS_SDK_TABLE_DEFAULT_NAME, NULL, AF_INET);
    
    if(vrf_default == VRFINDEX_ERROR) {
        LOG(LOG_EMERG, "%s: Failed to retrieve VRF index for %s due to %m",
            __func__, JUNOS_SDK_TABLE_DEFAULT_NAME);
    }
    
    return SUCCESS;
}


/**
 * Clear the configuration data
 */
void
clear_configuration(void)
{
    policer_t    * pol;
    int_pol_t    * ip;
    subscriber_t * sub;
    user_t       * user;
    
    use_classic_filters = 0;
    
    while(NULL != (pol = pol_entry(patricia_find_next(&pol_conf, NULL)))) {

        if(!patricia_delete(&pol_conf, &pol->node)) {
            LOG(LOG_ERR, "%s: Deleting policer failed", __func__);
            continue;
        }
        free(pol);
    }
    
    while(NULL != (ip = int_entry(patricia_find_next(&int_conf, NULL)))) {

        if(!patricia_delete(&int_conf, &ip->node)) {
            LOG(LOG_ERR, "%s: Deleting interface policy failed", __func__);
            continue;
        }
        
        if(!patricia_delete(&int_conf2, &ip->node2)) {
            LOG(LOG_ERR, "%s: Deleting interface policy2 failed", __func__);
            continue;
        }
        free(ip);
    }
    
    while(NULL != (sub = sub_entry(patricia_find_next(&sub_conf, NULL)))) {

        if(!patricia_delete(&sub_conf, &sub->node)) {
            LOG(LOG_ERR, "%s: Deleting subscriber failed", __func__);
            continue;
        }
        free(sub);
    }
    
    while(NULL != (user = user_entry(patricia_find_next(&user_conf, NULL)))) {

        if(!patricia_delete(&user_conf, &user->node)) {
            LOG(LOG_ERR, "%s: Deleting user failed", __func__);
            continue;
        }
        free(user);
    }
}


/**
 * Reset the configuration
 */
void
reset_configuration(uint8_t filter_mode)
{
    suspend_http_server();
    clear_configuration();
    use_classic_filters = filter_mode;
}


/**
 * All configuration should now be received
 */
void
configuration_complete(void)
{
    policer_t * pol;
    int_pol_t * ip;
    
    reset_all_filters(use_classic_filters);
    
    // Create all policiers thru dfw module
    
    pol = pol_entry(patricia_find_next(&pol_conf, NULL));
    while(pol != NULL) {
        create_policer(&pol->policer);
        pol = pol_entry(patricia_find_next(&pol_conf, &pol->node));
    }

    // Apply managed interfaces' default policiers thru dfw module
    
    ip = int_entry(patricia_find_next(&int_conf, NULL));
    while(ip != NULL) {
        apply_default_int_policy(ip->ifl_name, ip->input_pol, ip->output_pol);
        pol = pol_entry(patricia_find_next(&pol_conf, &pol->node));
    }
    
    resume_http_server(); 
}


/**
 * Configure a policer
 * 
 * @param[in] policer
 *      The policer configuration received from the mgmt component
 */
void
configure_policer(policer_info_t * policer)
{
    policer_t * pol = NULL;
    
    INSIST_ERR(policer != NULL);
    
    pol = calloc(1, sizeof(policer_t));
    INSIST_ERR(pol != NULL);

    // copy to new policer_t
    memcpy(&pol->policer, policer, sizeof(policer_info_t));
    
    // add it to the configuration
    patricia_node_init_length(&pol->node, strlen(pol->policer.name) + 1);

    if(!patricia_add(&pol_conf, &pol->node)) {
        LOG(LOG_ERR, "%s: Failed to add policer to configuration",
                __func__);
        free(pol);
    }
}


/**
 * Configure a managed interface
 * All policers should be configured first
 * 
 * @param[in] interface
 *      The interface configuration received from the mgmt component
 */
void
configure_managed_interface(int_info_t * interface)
{
    int_pol_t * intpol;
    
    INSIST_ERR(interface != NULL);
    
    intpol = calloc(1, sizeof(int_pol_t));
    INSIST_ERR(intpol != NULL);
    
    // copy data to new int_pol_t
    sprintf(intpol->ifl_name, "%s.%d",
            interface->name, interface->subunit);
    
    intpol->index = interface->index;
            
    strcpy(intpol->input_pol, interface->input_pol);
    strcpy(intpol->output_pol, interface->output_pol);
    
    // add it to the configuration
    patricia_node_init_length(&intpol->node, strlen(intpol->ifl_name) + 1);

    // tree by name
    if(!patricia_add(&int_conf, &intpol->node)) {
        LOG(LOG_ERR, "%s: Failed to add interface default policy "
                "to configuration", __func__);
        free(intpol);
    }
    
    // tree by index
    if(!patricia_add(&int_conf2, &intpol->node2)) {
        LOG(LOG_ERR, "%s: Failed to add interface default policy "
                "to configuration (2)", __func__);
        
        if(!patricia_delete(&int_conf, &intpol->node)) {
            LOG(LOG_ERR, "%s: Deleting interface policy failed", __func__);
        }
        free(intpol);
    }
}


/**
 * Configure a subscriber
 * All policers and managed interfaces should be configured first
 * 
 * @param[in] subscriber
 *      The subscriber configuration received from the mgmt component
 */
void
configure_subscriber(sub_info_t * subscriber)
{
    subscriber_t * sub = NULL;
    
    INSIST_ERR(subscriber != NULL);
    
    sub = calloc(1, sizeof(subscriber_t));
    INSIST_ERR(sub != NULL);
    
    // copy to new subscriber_t
    memcpy(&sub->subscriber, subscriber, sizeof(sub_info_t));
    
    // find the policer to point to directly
    sub->policer = pol_entry(patricia_get(&pol_conf,
            strlen(sub->subscriber.policer) + 1, sub->subscriber.policer));
    
    if(sub->policer == NULL) {
        LOG(LOG_ERR, "%s: Failed to find policer %s for subscriber %s",
                __func__, sub->subscriber.policer, sub->subscriber.name);
        free(sub);
        return;
    }
    
    // add it to the configuration
    patricia_node_init_length(&sub->node, strlen(sub->subscriber.name) + 1);

    if(!patricia_add(&sub_conf, &sub->node)) {
        LOG(LOG_ERR, "%s: Failed to add subscriber to configuration",
                __func__);
        free(sub);
    }
}


/**
 * Check for a match with the username-password pair
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
validate_credentials(char * username, char * password)
{
    subscriber_t * sub = NULL;

    // find the subscriber
    sub = sub_entry(patricia_get(&sub_conf, strlen(username) + 1, username));
    
    if(sub != NULL && strcmp(password, sub->subscriber.password) == 0) {
        return TRUE;
    }
    
    return FALSE;
}


/**
 * Check for a match with the IP address
 * 
 * @param[in] address
 *      The user's IP address
 * 
 * @return TRUE if there is a user logged in with this IP, FALSE otherwise
 */
boolean
user_logged_in(in_addr_t address)
{
    return (patricia_get(&user_conf, sizeof(in_addr_t), &address) != NULL);
}


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
apply_policy(char * username, in_addr_t address)
{
    subscriber_t * sub = NULL;
    user_t * user = NULL;
    int_pol_t * ip;
    msp_fdb_rt_query_t rt_query;
    msp_fdb_rt_info_t rt_res;
    
    // find the subscriber
    sub = sub_entry(patricia_get(&sub_conf, strlen(username) + 1, username));
    
    if(sub == NULL) {
        LOG(LOG_ERR, "%s: Cannot find subscriber", __func__);
        return EFAIL;
    }
    if(sub->policer) {
        LOG(LOG_ERR, "%s: Cannot find subscriber's policy", __func__);
        return EFAIL;
    }
    
    INSIST_ERR(user != NULL);
    
    user = calloc(1, sizeof(user_t));
    INSIST_ERR(user != NULL);
    
    // copy to new user_t
    user->address = address;
    user->sub = sub;
    
    if(!patricia_add(&user_conf, &user->node)) {
        LOG(LOG_ERR, "%s: Failed to add user to configuration", __func__);
        free(user);
        return EFAIL;
    }
    
    // Find the output interface index associated with the IP address
    
    rt_query.rt_addr_family = PROTO_IPV4;
    rt_query.rt_vrf_key_type = MSP_FDB_RT_QUERY_KEY_RT_INDEX;
    rt_query.rt_vrf.rt_idx = vrf_default;
    memcpy(rt_query.rt_dest_addr, &address, sizeof(address));

    msp_fdb_get_route_record(fdb_handle, &rt_query, &rt_res);
    
    LOG(LOG_INFO, "%s: Found output interface index %d",
            __func__, rt_res.rt_oif);
    
    // Check this IFL matches one of the default IFLs
    ip = int_entry2(patricia_get(&int_conf2, sizeof(uint32_t), &rt_res.rt_oif));
    if(ip == NULL) {
        if(!patricia_delete(&user_conf, &user->node)) {
            LOG(LOG_ERR, "%s: Deleting user from configuration failed",
                    __func__);
        }
        free(user);
        return EFAIL;
    }
    
    user->interface = ip;
    
    // Apply this subscriber's policy (policier)
    // Filter is applied to the IFL that his traffic gets routed through
    if(apply_subscriber_policer(ip->ifl_name, sub->subscriber.name,
            address, sub->policer->policer.name)) {
        LOG(LOG_ERR, "%s: Could not apply subscriber policy", __func__);
        return EFAIL;
    }
    
    return SUCCESS;
}


/**
 * Remove the policy for this user, and remove them from the logged in users
 *
 * @param[in] address
 *      The user's IP address
 */
void
remove_policy(in_addr_t address)
{
    user_t * user = NULL;

    // find the user
    user = user_entry(patricia_get(&user_conf, sizeof(in_addr_t), &address));
    
    if(user != NULL) {
        if(!patricia_delete(&user_conf, &user->node)) {
            LOG(LOG_ERR, "%s: Deleting user failed", __func__);
            return;
        }
        
        // Revoke this subscriber's policy (policier)
        // Filter is applied to the IFL that his traffic gets routed through
        if(revoke_subscriber_policer(user->interface->ifl_name, 
                user->sub->subscriber.name)) {
            LOG(LOG_ERR, "%s: Could not revoke subscriber policy",__func__);
        }
        
        free(user);
    } else {
        LOG(LOG_ERR, "%s: Cannot find user", __func__);
    }
}


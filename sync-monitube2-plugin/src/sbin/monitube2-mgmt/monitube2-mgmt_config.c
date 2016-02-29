/*
 * $Id: monitube2-mgmt_config.c 407596 2010-10-29 10:52:37Z sunilbasker $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2009, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file monitube2-mgmt_config.c
 * @brief Relating to loading and storing the configuration data
 *
 * These functions will parse and load the configuration data.
 */

#include <sync/common.h>
#include <ddl/dax.h>
#include <jnx/pconn.h>
#include <jnx/parse_interface.h>
#include <jnx/patricia.h>
#include <jnx/junos_kcom_pub_blob.h>
#include <jnx/ssrb_parse.h>
#include "monitube2-mgmt_config.h"
#include "monitube2-mgmt_conn.h"
#include "monitube2-mgmt_logging.h"

#include MONITUBE2_OUT_H

/*** Constants ***/

#define MAX_FLOW_AGE 300  ///< max flow statistic age in seconds (5 mins)

/*** Data Structures ***/

static patroot   rules;               ///< Pat. root for rules config
static patroot   ssets;               ///< Pat. root for service sets config
static patroot   flow_stats;          ///< Pat. root for flow stats
static rule_t *  current_rule = NULL; ///< current monitor set while loading configuration
static evContext m_ctx;               ///< event context

/*** STATIC/INTERNAL Functions ***/

/**
 * Generate the ss_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(ss_entry, ss_info_t, node)

/**
 * Generate the rule_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(rule_entry, rule_t, node)


/**
 * Generate the address_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(address_entry, address_t, node)


/**
 * Generate the fs_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(fs_entry, flowstat_t, node)


/**
 * Generate the fs_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(fss_entry, flowstat_ss_t, node)


/**
 * Delete all items from a list
 *
 * @param[in] list
 *      The list to empty out
 */
static void
delete_list(list_t * list)
{
    list_item_t * item;

    while((item = TAILQ_FIRST(list)) != NULL) {
       TAILQ_REMOVE(list, item, entries);
       free(item);
    }
}


/**
 * Delete a service-set
 *
 * @param[in] ssi
 *      The service set info
 */
static void
delete_serviceset(ss_info_t * ssi)
{
    rule_t * rule;
    list_item_t * item, * item2;
    
    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    if(!patricia_delete(&ssets, &ssi->node)) {
        LOG(LOG_ERR, "%s: Deleting sset failed", __func__);
        return;
    }
    
    if(evTestID(ssi->timer)) {
        evClearTimer(m_ctx, ssi->timer);
        evInitID(&ssi->timer);
    }
    
    notify_delete_serviceset(ssi->id, ssi->gen_num, ssi->svc_id,
    		ssi->fpc_slot, ssi->pic_slot);
    
    // Go through the all rules for this service set, and
    // for each rule remove the reference to this particular service set
    
    item = TAILQ_FIRST(&ssi->rules);
    while(item != NULL) {
        rule = (rule_t *)item->item;
        
        // find this particular service set reference in the rule
        item2 = TAILQ_FIRST(&rule->ssets);
        while(item2 != NULL) {
            if(item2->item == ssi) {
                // found it, remove it
                TAILQ_REMOVE(&rule->ssets, item2, entries);
                free(item2);
                break;
            }
            item2 = TAILQ_NEXT(item2, entries);
        }
        
        item = TAILQ_NEXT(item, entries);
    }

    delete_list(&ssi->rules);

    free(ssi);
}


/**
 * Delete all service sets
 */
static void
delete_all_servicesets(void)
{
    ss_info_t * ssi;
    rule_t * rule;
    list_item_t * item;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);
    
    notify_delete_all_policy();

    while(NULL != (ssi = ss_entry(patricia_find_next(&ssets, NULL)))) {
        
        if(!patricia_delete(&ssets, &ssi->node)) {
            LOG(LOG_ERR, "%s: Deleting sset failed", __func__);
            continue;
        }
        
        // Go through the all rules for this service set, 
        // delete the entire rule's ssets list
        
        while((item = TAILQ_FIRST(&ssi->rules)) != NULL) {
            rule = (rule_t *)item->item;
            delete_list(&rule->ssets);
            TAILQ_REMOVE(&ssi->rules, item, entries);
            free(item);
        }
        free(ssi);
    }
}


/**
 * Delete a given address from a rule's config (uses @c current_rule)
 *
 * @param[in] rule
 *      The rule in which the address lies
 *      
 * @param[in] addr
 *      Address
 *
 * @param[in] mask
 *      Address mask
 */
static void
delete_address(rule_t * rule, in_addr_t addr)
{
    address_t * address = NULL;

    address = address_entry(patricia_get(rule->addresses,
                    sizeof(in_addr_t), &addr));

    if(address == NULL) {
        LOG(LOG_ERR, "%s: Could not find address to delete",__func__);
        return;
    }

    if(!patricia_delete(rule->addresses, &address->node)) {
        LOG(LOG_ERR, "%s: Deleting address failed", __func__);
        return;
    }

    free(address);
}


/**
 * Delete all address from a monitor's configuration
 *
 * @param[in] notify
 *      Whether or not to notify the data component
 */
static void
delete_all_addresses(void)
{
    address_t * add = NULL;

    INSIST_ERR(current_rule != NULL);

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    while(NULL != (add =
        address_entry(patricia_find_next(current_rule->addresses, NULL)))) {

        if(!patricia_delete(current_rule->addresses, &add->node)) {
            LOG(LOG_ERR, "%s: Deleting address failed", __func__);
            continue;
        }

        free(add);
    }
}


/**
 * Delete a rule
 *
 * @param[in] rule
 *      The rule
 */
static void
delete_rule(rule_t * rule)
{
    ss_info_t * ssi;
    list_item_t * item, * item2;
    
    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    if(!patricia_delete(&rules, &rule->node)) {
        LOG(LOG_ERR, "%s: Deleting rule failed", __func__);
        return;
    }
    
    current_rule = rule;
    delete_all_addresses();
    patricia_root_delete(rule->addresses);
    current_rule = NULL;
    
    // Go through the all service sets using this rule, and
    // for any that do, remove the reference to this particular rule
    
    item = TAILQ_FIRST(&rule->ssets);
    while(item != NULL) {
        ssi = (ss_info_t *)item->item;
        
        // find this particular rule
        item2 = TAILQ_FIRST(&ssi->rules);
        while(item2 != NULL) {
            if(item2->item == rule) {
                // found it, remove it
                TAILQ_REMOVE(&ssi->rules, item2, entries);
                free(item2);
                
                // notify PIC with this service set to delete rule
                notify_delete_rule(rule->name, ssi->fpc_slot, ssi->pic_slot);
                break;
            }
            item2 = TAILQ_NEXT(item2, entries);
        }
        
        item = TAILQ_NEXT(item, entries);
    }

    delete_list(&rule->ssets);

    free(rule);
}


/**
 * Delete all configured rules and service sets as well since no service 
 * sets could refer to any rules and we only care about service sets referring 
 * to some MoniTube rule.
 */
static void
delete_all_rules(void)
{
    rule_t * rule;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);
    
    // we delete all service sets too since none we care about could refer 
    // to any rules anyway, delete_rule will be faster after calling this too
    delete_all_servicesets();

    while(NULL != (rule = rule_entry(patricia_find_next(&rules, NULL)))) {
        delete_rule(rule);
    }
}


/**
 * A change has happened to this service set and we need to notify the PIC
 * 
 * @param[in] ctx
 *     The event context for this application
 *
 * @param[in] uap
 *     The user data for this callback
 *
 * @param[in] due
 *     The absolute time when the event is due (now)
 *
 * @param[in] inter
 *     The period; when this will next be called
 */
static void
ss_change(evContext ctx UNUSED,
          void * uap,
          struct timespec due UNUSED,
          struct timespec inter UNUSED)
{
    ss_info_t * ssi = (ss_info_t *)uap;
    rule_t * rule;
    list_item_t * item;
    
    junos_trace(MONITUBE_TRACEFLAG_SSRB, "%s: %s", __func__, ssi->name);
    
    item = TAILQ_FIRST(&ssi->rules);
    while(item != NULL) {
        rule = (rule_t *)item->item;
        
        notify_apply_rule(rule->name, ssi->id, ssi->gen_num, ssi->svc_id,
                ssi->fpc_slot, ssi->pic_slot);
        
        item = TAILQ_NEXT(item, entries);
    }
}


/**
 * Inform us of a change in a service set
 * 
 * @param[in] ssrb
 *      Service set resolution blob
 *      
 * @param[in,out] data
 *      Data to store with ssrb info
 *      
 * @param[in,out] datalen
 *      Length of data
 *      
 * @param[in] operation
 *      Operation pertaining to @c ssrb
 */
static void
notify_ssrb_change(junos_kcom_pub_ssrb_t * ssrb,
                   void ** data UNUSED,
                   size_t *datalen UNUSED,
                   junos_kcom_pub_blob_msg_opcode_t operation)
{
    ss_info_t * ssi;

    switch(operation) {
    
    case JUNOS_KCOM_PUB_BLOB_MSG_ADD:
        junos_trace(MONITUBE_TRACEFLAG_SSRB, "%s: add %s", __func__,
                ssrb->svc_set_name);

        // Get service set

        ssi = ss_entry(patricia_get(&ssets,
                strlen(ssrb->svc_set_name) + 1, ssrb->svc_set_name));
        
        if(ssi != NULL) {
            // save some info
            ssi->id = ssrb->svc_set_id;
            ssi->gen_num = ssrb->gen_num;
            ssi->svc_id = ssrb->svc_id;
            
            // Notify change with 2s delay...
            // This delay is not needed, but in practice if there's a 
            // "catastropic" service set change causing the delete+add here,
            // then we will usually be notified via config too.
            // If this is before the 
            // config-read this short buffer usually makes time for the config
            // read to happen first causing one less delete+add on the PIC b/c
            // we don't do it now and just do it after at config-read time
            
            if(evTestID(ssi->timer)) {
                evClearTimer(m_ctx, ssi->timer);
                evInitID(&ssi->timer);
            }
            
            if(evSetTimer(m_ctx, ss_change, ssi, evAddTime(evNowTime(), 
                    evConsTime(2, 0)), evConsTime(0, 0), &ssi->timer)) {
                LOG(LOG_EMERG, "%s: Failed to initialize a timer (Error: %m)",
                        __func__);
            }
        }
        // else we will get the info later when reading the configuration
        
        break;
        
    case JUNOS_KCOM_PUB_BLOB_MSG_DEL:
        junos_trace(MONITUBE_TRACEFLAG_SSRB, "%s: delete %s", __func__,
                ssrb->svc_set_name);
        
        // we will handle the rest later when we get the configuration delete
        
        break;
        
    case JUNOS_KCOM_PUB_BLOB_MSG_CHANGE:
        junos_trace(MONITUBE_TRACEFLAG_SSRB, "%s: change %s", __func__,
                ssrb->svc_set_name);
        
        // a service order change (do nothing)
        // or a next-hop change (handled in the configuration read)
        
        break;
        
    default:
        LOG(LOG_ERR, "%s: Received an unexpected/unhandled event", __func__);
    }
}


/**
 * Handler for dax_query to get the changed service interfaces (in service sets)
 * $ss contains the service-set name
 *
 * @param[in] dwd
 *      Opaque dax data
 *
 * @param[in] dop
 *      DAX Object Pointer for sampling-service object
 *
 * @param[in] action
 *      The action on the given server object
 *
 * @param[in] data
 *      User data passed to handler (check flag)
 *
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int
verify_service_interfaces(dax_walk_data_t * dwd,
                          ddl_handle_t * dop,
                          int action,
                          void * data)
{
    int check = *((int *)data);
    char ss_name[MONITUBE2_MGMT_STRLEN]; // service set name
    char int_name[MONITUBE2_MGMT_STRLEN]; // service interface name
    ss_info_t * ssi;
    rule_t * rule;
    address_t * a;
    intf_parm_t int_info;
    list_item_t * item;
    
    if (action == DAX_ITEM_CHANGED) {

        if (!dax_get_stringr_by_dwd_ident(dwd, "$ss", 0, ss_name,
                sizeof(ss_name))) {
            dax_error(dop, "Failed to parse service-set name");
            return DAX_WALK_ABORT;
        }
        
        if (!dax_get_stringr_by_name(dop, "service-interface", int_name,
                sizeof(int_name))) {
            dax_error(dop, "Failed to retrieve service interface name");
            return DAX_WALK_ABORT;
        }
        
        if (parse_interface(int_name, 0, 0, &int_info, NULL, 0)) {
            dax_error(dop,"Failed to parse retrieved service interface name %s",
                    int_name);
            return DAX_WALK_ABORT;
        }
        
        if (!check) {
            // Get service set            
            ssi = ss_entry(patricia_get(&ssets, strlen(ss_name) + 1, ss_name));
            
            if (ssi != NULL && (ssi->fpc_slot != int_info.ipi_fpc || 
                        ssi->pic_slot != int_info.ipi_pic)) {
                    
                junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: change: %s : %s",
                        __func__, ss_name, int_name);
                
                if (strstr(int_info.ipi_name_prefix, "ms") == NULL) {
                    dax_error(dop, "Failed to parse service interface name %s. "
                            "Not an ms interface (%s)", int_name,
                            int_info.ipi_name_prefix);
                    return DAX_WALK_ABORT;
                }
                
                // Tell old PIC to delete policies for this service set
                notify_delete_serviceset(ssi->id, ssi->gen_num, ssi->svc_id, 
                		ssi->fpc_slot, ssi->pic_slot);
                
                // Set new PIC info so info will get sent there
                ssi->fpc_slot = int_info.ipi_fpc;
                ssi->pic_slot = int_info.ipi_pic;
                
                // Send info to the new PIC
                item = TAILQ_FIRST(&ssi->rules);
                while(item != NULL) {
                    rule = (rule_t *)item->item;
                    
                    notify_config_rule(rule->name, rule->rate,
                            rule->redirect, ssi->fpc_slot, ssi->pic_slot);
                    
                    a = next_address(rule, NULL);
                    while(a != NULL) {
                        notify_config_rule_prefix(rule->name, a->address,
                            a->mask, false, ssi->fpc_slot, ssi->pic_slot);
                        a = next_address(rule, a);
                    }
                    
                    notify_apply_rule(rule->name, ssi->id, ssi->gen_num, 
                            ssi->svc_id, ssi->fpc_slot, ssi->pic_slot);
                    
                    item = TAILQ_NEXT(item, entries);
                }
            }
        }
    }
    
    return DAX_WALK_OK;
}


/**
 * Handler for dax_query to get the applied rules (in service sets)
 * $ss contains the service-set name
 * $es contain the extension name
 * $rule contains the rule name
 *
 * @param[in] dwd
 *      Opaque dax data
 *
 * @param[in] dop
 *      DAX Object Pointer for rule object
 *
 * @param[in] action
 *      The action on the given server object
 *
 * @param[in] data
 *      User data passed to handler (check flag)
 *
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int
apply_rules(dax_walk_data_t * dwd,
            ddl_handle_t * dop,
            int action,
            void * data)
{
    int check = *((int *)data);
    ddl_handle_t * tmp_dop = NULL;
    char ss_name[MONITUBE2_MGMT_STRLEN]; // service set name
    char es_name[MONITUBE2_MGMT_STRLEN]; // extension service name within ss
    char rule_name[MONITUBE2_MGMT_STRLEN]; // monitube2 rule
    char int_name[MONITUBE2_MGMT_STRLEN]; // service interface name
    intf_parm_t int_info;
    ss_info_t * ssi, * ssi2;
    rule_t * rule;
    ssrb_node_t * ssrb;
    list_item_t * item;
    address_t * a;
    
    if (action == DAX_ITEM_DELETE_ALL) { // only happens when !check
        
    	junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: delete all",  __func__);
        
        if (!dax_get_stringr_by_dwd_ident(dwd, "$ss", 0, ss_name,
                sizeof(ss_name))) {
            
            junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: delete all service sets",
                    __func__);
            
            delete_all_servicesets();
            return DAX_WALK_OK;
        }
        
        ssi = ss_entry(patricia_get(&ssets, strlen(ss_name) + 1, ss_name));
        
        if (ssi == NULL) {
        	junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: ignore",  __func__);
            return DAX_WALK_OK; // ignore, we have nothing stored for it
            
            // it is a new service set and we are getting a delete-all
            // because it's new before we get a change for the stuff under it
        }
        
        // $rule would never be retrievable b/c it is the outmost container
        // $es could be retrievable if all under it was deleted
        
        if (dax_get_stringr_by_dwd_ident(dwd, "$es", 0, es_name,
                sizeof(es_name))) {
            if(strcmp(ssi->es, es_name) == 0) {
                junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: delete all under "
                        "extension-service %s of service-set %s", __func__,
                        es_name, ss_name);
                
                delete_serviceset(ssi);
                
            } // else not our es
        } else {
            // all ext-svcs gone
            junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: delete all under service-"
                    "set %s", __func__, ss_name);
            
            delete_serviceset(ssi);
        }
        
        return DAX_WALK_OK;
    }
    
    switch (action) {

    case DAX_ITEM_DELETE: // only happens when !check
        
        if (!dax_get_stringr_by_dwd_ident(dwd, "$ss", 0, ss_name,
                sizeof(ss_name))) {
            dax_error(dop, "Failed to parse service-set name");
            return DAX_WALK_ABORT;
        }
        
        if (!dax_get_stringr_by_dwd_ident(dwd, "$es", 0, es_name,
                sizeof(es_name))) {
            // Can't get the es, so all ext-svcs/rules were deleted from this 
            // service set; since it has no MoniTube rules delete it
            
            ssi = ss_entry(patricia_get(&ssets, strlen(ss_name) + 1, ss_name));
            
            if (ssi != NULL) {
                junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: delete service set "
                        "%s", __func__, ss_name);
                delete_serviceset(ssi);
            }
            return DAX_WALK_OK;
        }
        
        if (!dax_get_stringr_by_dwd_ident(dwd, "$rule", 0, rule_name,
                sizeof(rule_name))) {
            
            // The entire extension-service object was deleted
            
            // we don't support more than one monitube es per ss however,
            // so we can delete the service set and assume the new es will be a
            // DAX_ITEM_CHANGED
            
            ssi = ss_entry(patricia_get(&ssets, strlen(ss_name) + 1, ss_name));
            
            if (ssi != NULL) {
                
                if (strcmp(ssi->es, es_name) != 0) {
                    return DAX_WALK_OK; // it is not monitube related, ignore
                }
                
                junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: delete extension-"
                        "service %s resulted in a delete of the service set %s "
                        "because only one MoniTube extension-service per "
                        "service-set", __func__, es_name, ss_name);
                
                delete_serviceset(ssi);
                return DAX_WALK_OK;
            }
            // else this was an extension-service without monitube rules
            return DAX_WALK_OK;
        }
        
        // delete of a single rule:
        
        junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: delete: %s : %s : %s",
                __func__, ss_name, es_name, rule_name);
        
        // Get rule
        rule = rule_entry(patricia_get(&rules, strlen(rule_name) + 1,
                rule_name));
        
        if (rule == NULL) {
            // Rule was deleted too previously
            break; // should have been taken care of already
        }
        
        // Get service set

        ssi = ss_entry(patricia_get(&ssets, strlen(ss_name) + 1,
                ss_name));
        
        if (ssi == NULL) {
            LOG(LOG_ERR, "%s: Cannot find saved service set %s for %s",
                    __func__, ss_name, rule_name);
            return DAX_WALK_ABORT;
        }

        // Remove this rule from the service set
        
        // find this particular rule
        item = TAILQ_FIRST(&ssi->rules);
        while(item != NULL) {
            if(item->item == rule) {
                // found it, remove it
                TAILQ_REMOVE(&ssi->rules, item, entries);
                break;
            }
            item = TAILQ_NEXT(item, entries);
        }
        
        if(item == NULL) {
            LOG(LOG_ERR, "%s: Cannot find saved reference of rule %s in "
                    "service set %s", __func__, rule_name, ss_name);
            return DAX_WALK_ABORT;
        }
        free(item);
        
        // Remove this service-set from the rule
        item = TAILQ_FIRST(&rule->ssets);
        while(item != NULL) {
            if(item->item == ssi) {
                // found it, remove it
                TAILQ_REMOVE(&rule->ssets, item, entries);
                break;
            }
            item = TAILQ_NEXT(item, entries);
        }
        
        if(item == NULL) {
            LOG(LOG_ERR, "%s: Cannot find saved reference of rule %s in "
                    "service set %s", __func__, rule_name, ss_name);
            return DAX_WALK_ABORT;
        }
        free(item);
        
        if (ssi->id) { // if not set yet we haven't applied it
            notify_remove_rule(rule->name, ssi->id, ssi->gen_num, ssi->svc_id,
                    ssi->fpc_slot, ssi->pic_slot);
        }
        
        break;

    case DAX_ITEM_CHANGED:

        if (!dax_get_stringr_by_dwd_ident(dwd, "$ss", 0, ss_name,
                sizeof(ss_name))) {
            dax_error(dop, "Failed to parse service-set name");
            return DAX_WALK_ABORT;
        }
        
        if (!dax_get_stringr_by_dwd_ident(dwd, "$es", 0, es_name,
                sizeof(es_name))) {
            dax_error(dop, "Failed to parse extension-service name");
            return DAX_WALK_ABORT;
        }
        
        if (!dax_get_stringr_by_dwd_ident(dwd, "$rule", 0, rule_name,
                sizeof(rule_name))) {
            dax_error(dop, "Failed to parse applied rule's name");
            return DAX_WALK_ABORT;
        }
        
        if (!dax_get_object_by_dwd(dwd, "$ss", "sampling-service", &tmp_dop, 0)) {
            dax_error(dop, "Failed to parse sampling-service");
            return DAX_WALK_ABORT;
        }
        
        if (!dax_get_stringr_by_name(tmp_dop, "service-interface",
                int_name, sizeof(int_name))) {
            dax_error(dop, "Failed to retrieve service interface name");
            return DAX_WALK_ABORT;
        }
        
        if(parse_interface(int_name, 0, 0, &int_info, NULL, 0)) {
            dax_error(dop, "Failed to parse retrieved service interface name %s",
                    int_name);
            return DAX_WALK_ABORT;
        }
        
        if(strstr(int_info.ipi_name_prefix, "ms") == NULL) {
            dax_error(dop, "Failed to parse service interface name %s. Not "
                    "an ms interface (%s)", int_name, int_info.ipi_name_prefix);
            return DAX_WALK_ABORT;
        }
        
        junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: change: %s : %s : %s : %s",
                __func__, ss_name, es_name, rule_name, int_name);
       
        if (!check) {
            
            // Get rule
            rule = rule_entry(patricia_get(&rules, strlen(rule_name) + 1,
                    rule_name));
            
            if (rule == NULL) {
                // Shouldn't happen due to DDL must dependencies
                LOG(LOG_ERR, "%s: Cannot find rule configuration for %s",
                        __func__, rule_name);
                return DAX_WALK_ABORT;
            }
            
            // Get or create service set
            
            ssi = ss_entry(patricia_get(&ssets, strlen(ss_name) + 1, ss_name));
            
            if (ssi == NULL) {
                ssi = calloc(1, sizeof(ss_info_t));
                INSIST(ssi != NULL);

                strlcpy(ssi->name, ss_name, sizeof(ssi->name));
                strlcpy(ssi->es, es_name, sizeof(ssi->es));
                
                TAILQ_INIT(&ssi->rules);

                // Add to patricia tree
                patricia_node_init_length(&ssi->node, strlen(ss_name) + 1);

                if (!patricia_add(&ssets, &ssi->node)) {
                    LOG(LOG_ERR, "%s: Cannot add service set info for %s to "
                        "corresponding rule %s", __func__, ss_name, rule_name);
                    free(ssi);
                    return DAX_WALK_ABORT;
                }
                ssi->fpc_slot = int_info.ipi_fpc;
                ssi->pic_slot = int_info.ipi_pic;
            } else {
                if (ssi->fpc_slot != int_info.ipi_fpc || 
                        ssi->pic_slot != int_info.ipi_pic) {
                    
                    // Tell old PIC to delete policies for this service set
                    notify_delete_serviceset(ssi->id, ssi->gen_num, ssi->svc_id,
                    		ssi->fpc_slot, ssi->pic_slot);
                    
                    // Set new PIC info so info will get sent there
                    ssi->fpc_slot = int_info.ipi_fpc;
                    ssi->pic_slot = int_info.ipi_pic;
                }
            }

            // Check rules list in ssi

            item = TAILQ_FIRST(&ssi->rules);
            while(item != NULL) {
                if (item->item == rule) {
                    // already in list
                    break;
                }
                 item = TAILQ_NEXT(item, entries);
            }
            
            if (item == NULL) { // it wasn't found so add it
                item = calloc(1, sizeof(list_item_t));
                item->item = rule;
                TAILQ_INSERT_TAIL(&ssi->rules, item, entries);
            }
            
            // Check ssets list in rule
            
            item = TAILQ_FIRST(&rule->ssets);
            while(item != NULL) {
                if (item->item == ssi) {
                    // already in list
                    break;
                }
                 item = TAILQ_NEXT(item, entries);
            }
            
            if (item == NULL) { // it wasn't found so add it
                item = calloc(1, sizeof(list_item_t));
                item->item = ssi;
                TAILQ_INSERT_TAIL(&rule->ssets, item, entries);
            }
            
            // Extract any SSRB info we can if possible

            ssrb = ssrb_get_node_by_name(ss_name);
            if (ssrb != NULL) {
                ssi->id = ssrb->sset_id;
                ssi->gen_num = ssrb->gen_num;
                ssi->svc_id = ssrb->ssrb->svc_id;
            } else {
                // we will get the ssrb info sometime later probably
            }
            
            // Notify PIC that it will need the rule's config (action+addresses)
            // but if another service set on the same PIC uses this rule, then
            // don't bother sending the rule again, because it could be a lot
            ssi2 = next_serviceset(NULL);
            while(ssi2 != NULL) {
                if (ssi2 == ssi) {
                    ssi2 = next_serviceset(ssi2);
                    continue;
                }
                
                if (ssi2->fpc_slot == ssi->fpc_slot && 
                        ssi2->pic_slot == ssi->pic_slot) {
                    // service set is running on the same PIC
                    // check if it uses the same rule
                    
                    item = TAILQ_FIRST(&ssi2->rules);
                    while(item != NULL) {
                        if (item->item == rule) {
                            // This service set has a reference to that rule;
                            // thus, rule's config should have already been sent 
                            break;
                        }
                        item = TAILQ_NEXT(item, entries);
                    }
                    if (item != NULL) { // found it 
                        break;
                    }
                }
                ssi2 = next_serviceset(ssi2);
            }
            
            if (ssi2 == NULL) { // no luck finding rule in another service set
                // Send PIC the rule's config (action+addresses)
                notify_config_rule(rule->name, rule->rate, rule->redirect,
                                        ssi->fpc_slot, ssi->pic_slot);
                
                a = next_address(rule, NULL);
                while(a != NULL) {
                    notify_config_rule_prefix(rule->name, a->address, a->mask,
                                           false, ssi->fpc_slot, ssi->pic_slot);
                    a = next_address(rule, a);
                }
            }
            
            if (ssi->id) { // if still 0 we haven't got ssrb info
                notify_apply_rule(rule->name, ssi->id, ssi->gen_num,ssi->svc_id,
                        ssi->fpc_slot, ssi->pic_slot);
            }
        }

        break;

    case DAX_ITEM_UNCHANGED:
        junos_trace(MONITUBE_TRACEFLAG_CONF,
                "%s: DAX_ITEM_UNCHANGED observed", __func__);
        break;

    default:
        break;
    }

    return DAX_WALK_OK;
}

    
/**
 * Handler for dax_walk_list to parse each configured address in a rule's
 * "from destination-addresses"
 *
 * @param[in] dwd
 *      Opaque dax data
 *
 * @param[in] dop
 *      DAX Object Pointer for server object
 *
 * @param[in] action
 *      The action on the given server object
 *
 * @param[in] data
 *      User data passed to handler (check flag)
 *
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int
parse_addresses(dax_walk_data_t * dwd,
                ddl_handle_t * dop,
                int action,
                void * data)
{
    char ip_str[32], * tmpstr, *tmpip;
    struct in_addr tmp;
    in_addr_t addr, prefix;
    int check = *((int *)data), mask;
    address_t * address = NULL;
    ss_info_t * ssi;
    list_item_t * item;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    if(action == DAX_ITEM_DELETE_ALL) {
        // All addresses were deleted
        delete_all_addresses();
        
        // We shouldn't get this if the rule was deleted or often, but we may be
        // out-of-sync with config db (or starting up) and then multiple CHANGE
        // events would come for all configured addresses
        
        // Rather than deleting each prefix, we delete the rule and re-add it 
        // with no prefixes. If this is happening at startup, then rule would be
        // new, then rule's ssets will be empty anyway
        
        item = TAILQ_FIRST(&current_rule->ssets);
        while(item != NULL) {
            ssi = (ss_info_t *)item->item;
            
            notify_delete_rule(current_rule->name, ssi->fpc_slot,ssi->pic_slot);
            notify_config_rule(current_rule->name, current_rule->rate,
                    current_rule->redirect, ssi->fpc_slot, ssi->pic_slot);
            
            item = TAILQ_NEXT(item, entries);
        }
        
        return DAX_WALK_OK;
    }

    switch(action) {

    case DAX_ITEM_DELETE:
        // an address was deleted

        // get the (deleted) address
        // for deleted items we can only get a string form:
        if(!dax_get_stringr_by_dwd_ident(dwd, NULL, 0, ip_str, sizeof(ip_str))){
            dax_error(dop, "Failed to parse IP address and prefix on delete");
            return DAX_WALK_ABORT;
        }

        // break off prefix
        tmpip = strdup(ip_str);
        tmpstr = strtok(tmpip, "/");

        if(inet_aton(tmpstr, &tmp) != 1) { // if failed
            dax_error(dop, "Failed to parse prefix address portion: %s",
                    ip_str);
            return DAX_WALK_ABORT;
        }
        addr = tmp.s_addr;

        // parse prefix
        tmpstr = strtok(NULL, "/");

        errno = 0;
        mask = (int)strtol(tmpstr, (char **)NULL, 10);
        if(errno) {
            dax_error(dop, "Failed to parse prefix mask portion: %s", ip_str);
            return DAX_WALK_ABORT;
        }
        if(mask < 0 || mask > 32) {
            dax_error(dop, "Mask bits must be in range of 0-32: %s", ip_str);
            return DAX_WALK_ABORT;
        }

        if(mask == 32) {
            prefix = 0xFFFFFFFF;
        } else {
            prefix = (1 << mask) - 1;
            prefix = htonl(prefix);
        }

        free(tmpip);

        if(!check) {
            delete_address(current_rule, addr);
            
            // Rule's addresses have changed
            // If any ss is using the rule notify the PICs of the change
            
            item = TAILQ_FIRST(&current_rule->ssets);
            while(item != NULL) {
                ssi = (ss_info_t *)item->item;
                
                notify_config_rule_prefix(current_rule->name, addr, prefix,
                        true, ssi->fpc_slot, ssi->pic_slot);
                
                item = TAILQ_NEXT(item, entries);
            }
        }

        break;

    case DAX_ITEM_CHANGED:
        // a server was added

        if(!dax_get_ipv4prefixmandatory_by_name(dop,
                DDLNAME_ADDRESSES_ADDRESS, &addr, &prefix)) {
            dax_error(dop, "Failed to parse IP address and prefix");
            return DAX_WALK_ABORT;
        }

        if(((~prefix) & addr) != 0) {
            dax_error(dop, "Host bits in address must be zero");
            return DAX_WALK_ABORT;
        }

        if(!check) {
            address = calloc(1, sizeof(address_t));
            INSIST(address != NULL);

            address->address = addr;
            address->mask = prefix;

            // Add to patricia tree
            
            patricia_node_init(&address->node);

            if(!patricia_add(current_rule->addresses, &address->node)) {
                tmp.s_addr = addr;
                tmpstr = strdup(inet_ntoa(tmp));
                tmp.s_addr = prefix;
                LOG(LOG_ERR, "%s: Cannot add address %s/%s as it conflicts with"
                    " another address in the same group (%s)", __func__, tmpstr,
                    inet_ntoa(tmp), current_rule->name);
                free(tmpstr);
                free(address);
            }

            // Rule's addresses have changed
            // If any ss is using the rule notify the PICs of the change
            // If this is a new rule, then rule's ssets will be empty anyway
            
            item = TAILQ_FIRST(&current_rule->ssets);
            while(item != NULL) {
                ssi = (ss_info_t *)item->item;
                
                notify_config_rule_prefix(current_rule->name, addr, prefix,
                        false, ssi->fpc_slot, ssi->pic_slot);
                
                item = TAILQ_NEXT(item, entries);
            }
        }

        break;

    case DAX_ITEM_UNCHANGED:
        junos_trace(MONITUBE_TRACEFLAG_CONF,
                "%s: DAX_ITEM_UNCHANGED observed", __func__);
        break;

    default:
        break;
    }

    return DAX_WALK_OK;
}


/**
 * Handler for dax_walk_list to parse each configured rule
 *
 * @param[in] dwd
 *      Opaque dax data
 *
 * @param[in] dop
 *      DAX Object Pointer for application object
 *
 * @param[in] action
 *      The action on the given application object
 *
 * @param[in] data
 *      User data passed to handler (check flag)
 *
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int
parse_rules(dax_walk_data_t * dwd,
            ddl_handle_t * dop,
            int action,
            void * data)
{
    ddl_handle_t * addresses_dop, * tmp_dop;
    int check = *((int *)data);
    char rule_name[MONITUBE2_MGMT_STRLEN];
    uint32_t rate = 0;
    in_addr_t mirror_to = 0;
    int addr_fam;
    rule_t * rule = NULL;
    bool is_rule_new = false;
    ss_info_t * ssi;
    list_item_t * item;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    switch (action) {

    case DAX_ITEM_DELETE_ALL:

        // All rules were deleted
        delete_all_rules();
        return DAX_WALK_OK;

        break;

    case DAX_ITEM_DELETE:

        // get the (deleted) rule name
        if (!dax_get_stringr_by_dwd_ident(dwd, NULL, 0,
                rule_name, sizeof(rule_name))) {
            dax_error(dop, "Failed to parse a deleted rule's name");
            return DAX_WALK_ABORT;
        }

        if(check) {
            break; // nothing to do
        }

        rule = rule_entry(patricia_get(&rules, strlen(rule_name) + 1,
                rule_name));
        if(rule != NULL) {
            delete_rule(rule);
        }

        break;

    case DAX_ITEM_CHANGED:

        // get the rule name

        if (!dax_get_stringr_by_name(dop, DDLNAME_RULE_RULE_NAME,
                rule_name, sizeof(rule_name))) {
            dax_error(dop, "Failed to parse rule name");
            return DAX_WALK_ABORT;
        }

        // create rule if necessary
        
        if(!check) {
            rule = rule_entry(patricia_get(
                    &rules, strlen(rule_name) + 1, rule_name));
            
            if(rule == NULL) {
                rule = calloc(1, sizeof(rule_t));
                INSIST(rule != NULL);
                strlcpy(rule->name, rule_name, sizeof(rule->name));

                // Add to patricia tree
                patricia_node_init_length(&rule->node, strlen(rule->name) + 1);

                if(!patricia_add(&rules, &rule->node)) {
                    LOG(LOG_ERR, "%s: Failed to add rule to "
                            "configuration", __func__);
                    free(rule);
                    return DAX_WALK_ABORT;
                }

                // init addresses
                rule->addresses = patricia_root_init(
                        NULL, FALSE, sizeof(in_addr_t), 0);
                
                // init ssets
                TAILQ_INIT(&rule->ssets);
                
                is_rule_new = true;
                // Because the rule is new, we don't notify any PICs of the rule
                // If any service sets refer to this rule, then only there do we 
                // send the rule's info (done in apply_rules)
            }
            
            // save so that when parsing addresses
            // we know to which rule to associate them
            current_rule = rule;
        }
        

        // Get "from" params:
        
        if(!dax_get_object_by_name(dop, DDLNAME_RULE_FROM,
                &tmp_dop, FALSE)) {

            dax_error(dop, "Failed to parse from (required)");
            return DAX_WALK_ABORT;
        }
        
        if(!dax_get_object_by_name(tmp_dop, DDLNAME_ADDRESSES,
                &addresses_dop, FALSE)) {

            dax_error(dop, "Failed to parse destination-addresses (required)");
            dax_release_object(&tmp_dop);
            return DAX_WALK_ABORT;
        }

        // parse the destination addresses
        if(dax_is_changed(addresses_dop) &&
            dax_walk_list(addresses_dop, DAX_WALK_DELTA, parse_addresses, data)
                    != DAX_WALK_OK) {

            dax_release_object(&addresses_dop);
            dax_release_object(&tmp_dop);
            return DAX_WALK_ABORT;
        }
        
        dax_release_object(&tmp_dop);
        dax_release_object(&addresses_dop);
        current_rule = NULL;
        
        // Get "then" params:
        
        if(!dax_get_object_by_name(dop, DDLNAME_RULE_THEN,
                &tmp_dop, FALSE)) {

            dax_error(dop, "Failed to parse then (required)");
            return DAX_WALK_ABORT;
        }
        
        if(!dax_is_changed(tmp_dop)) {
            dax_release_object(&tmp_dop);
            break;
        }
        
        // read in some of the "then" attributes
        dax_get_uint_by_name(tmp_dop,
                DDLNAME_RULE_THEN_MONITORING_RATE, &rate);
        
        if(dax_get_ipaddr_by_name(tmp_dop, DDLNAME_RULE_THEN_MIRROR_TO,
            &addr_fam, &mirror_to, sizeof(mirror_to)) && addr_fam != AF_INET) {
            
            dax_error(dop, "Failed to parse mirror destination IPv4 address");
            dax_release_object(&tmp_dop);
            return DAX_WALK_ABORT;
        }
        
        // check one or the other and INET below
        if(!(rate || mirror_to)) {
            dax_error(dop, "Either a %s or %s must be configured", 
                    DDLNAME_RULE_THEN_MONITORING_RATE, 
                    DDLNAME_RULE_THEN_MIRROR_TO);
            dax_release_object(&tmp_dop);
            return DAX_WALK_ABORT;
        } else if (!check) {
            
            if(is_rule_new) {
                rule->rate = rate;
                rule->redirect = mirror_to;
            } else {
                if(rule->rate != rate || rule->redirect != mirror_to) {
                    
                    // Existing rule's actions have changed
                    // If any ss is using the rule notify the PICs of the change
                    
                    item = TAILQ_FIRST(&rule->ssets);
                    while(item != NULL) {
                        ssi = (ss_info_t *)item->item;
                        notify_config_rule(rule->name, rate, mirror_to,
                                ssi->fpc_slot, ssi->pic_slot);
                        item = TAILQ_NEXT(item, entries);
                    }
                }
                rule->rate = rate;
                rule->redirect = mirror_to;
            }
        }
        
        dax_release_object(&tmp_dop);
        break;

    case DAX_ITEM_UNCHANGED:
        junos_trace(MONITUBE_TRACEFLAG_CONF,
                "%s: DAX_ITEM_UNCHANGED observed", __func__);
        break;

    default:
        break;
    }

    return DAX_WALK_OK;
}


/**
 * Delete the flow statistics because they have timed out
 *
 * @param[in] ctx
 *     The event context for this application
 *
 * @param[in] uap
 *     The user data for this callback
 *
 * @param[in] due
 *     The absolute time when the event is due (now)
 *
 * @param[in] inter
 *     The period; when this will next be called
 */
static void
age_out_flow_stat(evContext ctx UNUSED,
		          void * uap,
		          struct timespec due UNUSED,
		          struct timespec inter UNUSED)
{
    flowstat_t * fs = (flowstat_t *)uap;

    INSIST_ERR(fs != NULL);

    if(evTestID(fs->timer_id)) {
       evClearTimer(m_ctx, fs->timer_id);
       evInitID(&fs->timer_id);
    }

    patricia_delete(fs->ssi->stats, &fs->node);
    
    if(patricia_isempty(fs->ssi->stats)) { // clean if sset tree is empty
    	patricia_root_delete(fs->ssi->stats);
    	patricia_delete(&flow_stats, &fs->ssi->node);
    	free(fs->ssi);
    }

    free(fs);
}

/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 */
void
init_config(evContext ctx)
{
    m_ctx = ctx;

    patricia_root_init(&rules, FALSE, MONITUBE2_MGMT_STRLEN, 0);
    patricia_root_init(&ssets, FALSE, MONITUBE2_MGMT_STRLEN, 0);
    patricia_root_init(&flow_stats, FALSE, sizeof(uint16_t), 0);

    if (ssrb_config_init()) {
        LOG(LOG_EMERG,"%s: service management initialization failed", __func__);
    }
    
    if (svcset_ssrb_async(notify_ssrb_change, NULL)) {
        LOG(LOG_EMERG, "%s: Cannot register for SSRB notifications", __func__);
    }
}


/**
 * Clear and reset the entire configuration, freeing all memory store for policy
 */
void
clear_config(void)
{
    delete_all_rules();
    process_notifications();
    clear_stats();
}


/**
 * Get the next rule in configuration given the previously returned data
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to a rule if one more exists, o/w NULL
 */
rule_t *
next_rule(rule_t * data)
{
    return rule_entry(patricia_find_next(&rules,(data ? &(data->node) : NULL)));
}


/**
 * Given a rule, get the next address in configuration given the previously
 * returned data
 *
 * @param[in] rule
 *      Rule's configuration
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to an address if one more exists, o/w NULL
 */
address_t *
next_address(rule_t * rule, address_t * data)
{
    return address_entry(patricia_find_next(rule->addresses,
                                    (data ? &(data->node) : NULL)));
}


/**
 * Get the next service set in configuration given the previously returned data
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to a service set if one more exists, o/w NULL
 */
ss_info_t *
next_serviceset(ss_info_t * data)
{
    return ss_entry(patricia_find_next(&ssets, (data ? &(data->node) : NULL)));
}


/**
 * Get the next flowstat service set in configuration given the previously
 * returned data
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to a flowstat if one more exists, o/w NULL
 */
flowstat_ss_t *
next_flowstat_ss(flowstat_ss_t * data)
{
    return fss_entry(patricia_find_next(&flow_stats,
                                    (data ? &(data->node) : NULL)));
}

/**
 * Get the next flowstat record in configuration given the previously
 * returned data
 *
 * @param[in] fss
 *      A flow stats service set 
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to a flowstat if one more exists, o/w NULL
 */
flowstat_t *
next_flowstat(flowstat_ss_t * fss, flowstat_t * data)
{
    return fs_entry(patricia_find_next(fss->stats,
                                    (data ? &(data->node) : NULL)));
}

/**
 * Clear all statistics
 */
void
clear_stats(void)
{
	flowstat_ss_t * fss;
	flowstat_t * fs;
	
    // delete all statistics data
    
    while((fss = fss_entry(patricia_find_next(&flow_stats, NULL))) != NULL) {
    	
        while((fs = fs_entry(patricia_find_next(fss->stats, NULL))) != NULL) {
        	
            if(evTestID(fs->timer_id)) {
               evClearTimer(m_ctx, fs->timer_id);
            }
        	
        	patricia_delete(fss->stats, &fs->node);
        	free(fs);
        }
    	
    	patricia_root_delete(fss->stats);
    	patricia_delete(&flow_stats, &fss->node);
    	free(fss);
    }
}


/**
 * Given a monitor's name and flow address add or update its statistics record
 *
 * @param[in] fpc_slot
 *      FPC slot #
 *
 * @param[in] pic_slot
 *      PIC slot #
 *
 * @param[in] ssid
 *      Service set id
 *
 * @param[in] flow_addr
 *      flow address (ID)
 *
 * @param[in] flow_port
 *      flow port (ID)
 *
 * @param[in] mdi_df
 *      MDI delay factor
 *
 * @param[in] mdi_mlr
 *      MDI media loss rate
 */
void
set_flow_stat(uint16_t fpc_slot,
              uint16_t pic_slot,
              uint16_t ssid,
              in_addr_t flow_addr,
              uint16_t flow_port,
              double mdi_df,
              uint32_t mdi_mlr)
{
	flowstat_ss_t * fss;
    flowstat_t * fs;
    bool isnew = false;
    
    struct key_s {
        uint16_t  fpc;
        uint16_t  pic;
        in_addr_t address;
        uint16_t  port;
    } key;
    
    int key_size = sizeof(in_addr_t) + (3 * sizeof(uint16_t));

    // get monitor
    fss = fss_entry(patricia_get(&flow_stats, sizeof(ssid), &ssid));

    if(fss == NULL) {

    	fss = calloc(1, sizeof(flowstat_ss_t));
    	INSIST(fss != NULL);
        
        fss->ssid = ssid;
        fss->stats = patricia_root_init(NULL, FALSE, key_size, 0);
        
        // Add to patricia tree
        
        patricia_node_init(&fss->node);

        if(!patricia_add(&flow_stats, &fss->node)) {
            LOG(LOG_ERR, "%s: Failed to add a flow stat service set entry for "
            		"%d", __func__, ssid);
            free(fss);
            return;
        }
        isnew = true;
    }
    
    // get flow stat

    key.fpc = fpc_slot;
    key.pic = pic_slot;
    key.address = flow_addr;
    key.port = flow_port;

    fs = fs_entry(patricia_get(fss->stats, key_size, &key));

    if(fs == NULL) { // gotta create it
        fs = calloc(1, sizeof(flowstat_t));
        INSIST(fs != NULL);
        fs->fpc_slot = fpc_slot;
        fs->pic_slot = pic_slot;
        fs->flow_addr = flow_addr;
        fs->flow_port = flow_port;
        evInitID(&fs->timer_id);
        fs->ssi = fss;

        if(!patricia_add(fss->stats, &fs->node)) {
            LOG(LOG_ERR, "%s: Failed to add a flow stat to configuration",
                    __func__);
            free(fs);
            
            if(isnew) { // more clean up
            	patricia_delete(&flow_stats, &fss->node);
            	patricia_root_delete(fss->stats);
            	free(fss);
            }
            return;
        }

    } else { // it already exists
        if(evTestID(fs->timer_id)) {
           evClearTimer(m_ctx, fs->timer_id);
           evInitID(&fs->timer_id);
        }
    }

    fs->last_mdi_df[fs->window_position] = mdi_df;
    fs->last_mdi_mlr[fs->window_position] = mdi_mlr;
    fs->reports++;
    fs->window_position++;
    if(fs->window_position == STATS_BASE_WINDOW) {
        fs->window_position = 0;
    }

    // reset age timer

    if(evSetTimer(m_ctx, age_out_flow_stat, fs,
            evAddTime(evNowTime(), evConsTime(MAX_FLOW_AGE, 0)),
            evConsTime(0, 0), &fs->timer_id)) {

        LOG(LOG_EMERG, "%s: evSetTimer() failed! Will not be "
                "able to age out flow statistics", __func__);
    }
}


/**
 * Read daemon configuration from the database
 *
 * @param[in] check
 *     1 if this function being invoked because of a commit check
 *
 * @return SUCCESS (0) successfully loaded, EFAIL if not
 *
 * @note Do not use ERRMSG/LOG during config check normally.
 */
int
monitube_config_read(int check)
{
    const char * monitube_rules_config[] =
        {DDLNAME_SERVICES, DDLNAME_SYNC, DDLNAME_MONITUBE, DDLNAME_RULE, NULL};

    ddl_handle_t * top = NULL;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: Starting monitube "
            "configuration load", __func__);

    // Load the main configuration under sync monitube (if changed)...

    if (!dax_get_object_by_path(NULL, monitube_rules_config, &top, FALSE))  {

        junos_trace(MONITUBE_TRACEFLAG_CONF,
                "%s: Cleared monitube configuration", __func__);

        clear_config(); // No rules
        return SUCCESS;
    }
    
    if(dax_is_changed(top)) { // a rule change happened
        if(dax_walk_list(top, DAX_WALK_DELTA, parse_rules, &check)
                != DAX_WALK_OK) {

            junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: walking rules "
                "list failed", __func__);
            dax_release_object(&top);
            return EFAIL;
        }
    }
    
    dax_release_object(&top);
    
    // Apply rules/detect changes
    dax_query(NULL, "services service-set $ss extension-service $es "
        "monitube2-rules $rule", DAX_WALK_DELTA, apply_rules, &check);
    
    // Detect changes in the service interface
    dax_query(NULL, "services service-set $ss sampling-service",
            DAX_WALK_CHANGED, verify_service_interfaces, &check);
    
    // send to the PIC(s) if !check
    if (!check) {
        process_notifications();
    }

    junos_trace(MONITUBE_TRACEFLAG_CONF,
            "%s: Loaded monitube configuration", __func__);

    return SUCCESS;
}

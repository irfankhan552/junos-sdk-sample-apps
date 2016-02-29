/*
 * $Id: dpm-mgmt_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-mgmt_config.c
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions will parse and load the configuration data.
 */

#include <sync/common.h>
#include <fnmatch.h>
#include <ddl/dax.h>
#include "dpm-mgmt_config.h"
#include "dpm-mgmt_conn.h"
#include "dpm-mgmt_logging.h"

#include DPM_OUT_H

/*** Constants ***/


/*** Data Structures ***/

/**
 * Path in configuration DB to DPM configuration 
 */
const char * dpm_config[] = {DDLNAME_SYNC, DDLNAME_SYNC_DPM, NULL};

/* Configuration DS: */
static boolean     classic_filters;       ///< Using classic filters
static patroot     pol_conf;              ///< Pat. root for policers
static patroot     sub_conf;              ///< Pat. root for subscribers
static TAILQ_HEAD(, int_def_s) int_conf;  ///< List of interface defaults
class_list_t logged_in;                   ///< List with login records (classes)


/*** STATIC/INTERNAL Functions ***/

/**
 * Generate the pol_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(pol_entry, policer_t, node)


/**
 * Generate the sub_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(sub_entry, subscriber_t, node)


/**
 * Handler for dax_walk_list to parse each configured class item
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
parse_class(dax_walk_data_t * dwd __unused,
                  ddl_handle_t * dop,
                  int action __unused,
                  void * data)
{
    subscriber_t * sub = NULL;
    ddl_handle_t * conf_op = NULL, * sub_op = NULL;
    int rc = DAX_WALK_ABORT;
    int check = *((int *)data);
    char class_name[MAX_CLASS_NAME + 1];
    char pol_name[MAX_POL_NAME + 1];

    junos_trace(DPM_TRACEFLAG_CONF, "%s", __func__);
    
    INSIST_ERR(dop != NULL);
    
    if (!dax_get_stringr_by_name(dop, DDLNAME_CLASS_CLASS_NAME,
            class_name, sizeof(class_name))) {
        dax_error(dop, "Failed to parse class name");
        goto failed;
    }
    
    if (!dax_get_stringr_by_name(dop, DDLNAME_CLASS_POLICER,
            pol_name, sizeof(pol_name))) {
        dax_error(dop, "Failed to parse class policer");
        goto failed;
    }
    
    // go thru all subscribers in the class...
    
    if(!dax_get_object_by_name(dop, DDLNAME_SUBSCRIBER, &conf_op, FALSE)) {
        dax_error(dop, "Failed to find any subscribers in the class");
        goto failed;
    }
    
    while (dax_visit_container(conf_op, &sub_op)) {
        
        sub = calloc(1, sizeof(subscriber_t));
        INSIST_ERR(sub != NULL);
        
        if (!dax_get_stringr_by_name(sub_op, DDLNAME_SUBSCRIBER_SUBSCRIBER_NAME,
                sub->subscriber.name, sizeof(sub->subscriber.name))) {
            dax_error(sub_op, "Failed to parse subscriber name");
            goto failed;
        }
        
        if (!dax_get_stringr_by_name(sub_op, DDLNAME_SUBSCRIBER_PASSWORD,
                sub->subscriber.password, sizeof(sub->subscriber.password))) {
            dax_error(sub_op, "Failed to parse subscriber password");
            goto failed;
        }
        
        if(check) {
            free(sub);
            sub = NULL;
        } else {
            // add it to the configuration
            
            strcpy(sub->subscriber.class, class_name);
            strcpy(sub->subscriber.policer, pol_name);
            
            patricia_node_init_length(&sub->node,
                    strlen(sub->subscriber.name) + 1);

            if(!patricia_add(&sub_conf, &sub->node)) {
                LOG(TRACE_LOG_ERR, "%s: Failed to add "
                        "subscriber to configuration", __func__);
                goto failed;
            }
        }
    }
    
    rc = DAX_WALK_OK;
    
failed:
    if(conf_op)
        dax_release_object(&conf_op);
    
    if(sub_op)
        dax_release_object(&sub_op);

    if(rc != DAX_WALK_OK && sub != NULL)
        free(sub);
    
    return rc;
}


/**
 * Handler for dax_walk_list to parse each configured IFL pattern item
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
parse_int_default(dax_walk_data_t * dwd __unused,
                  ddl_handle_t * dop,
                  int action __unused,
                  void * data)
{
    int_def_t * id = NULL;
    int rc = DAX_WALK_ABORT;
    int check = *((int *)data);

    junos_trace(DPM_TRACEFLAG_CONF, "%s", __func__);
    
    INSIST_ERR(dop != NULL);
    
    id = calloc(1, sizeof(int_def_t));
    INSIST_ERR(id != NULL);
    
    if (!dax_get_stringr_by_name(dop, DDLNAME_IFL_PATTERN_INTERFACE_PATTERN,
            id->name_pattern, sizeof(id->name_pattern))) {
        dax_error(dop, "Failed to interface default name pattern");
        goto failed;
    }
    
    if (!dax_get_stringr_by_name(dop, DDLNAME_IFL_PATTERN_INPUT_POLICER,
            id->input_pol, sizeof(id->input_pol))) {
        dax_error(dop, "Failed to interface default input policer");
        goto failed;
    }
    
    if (!dax_get_stringr_by_name(dop, DDLNAME_IFL_PATTERN_OUTPUT_POLICER,
            id->output_pol, sizeof(id->output_pol))) {
        dax_error(dop, "Failed to interface default output policer");
        goto failed;
    }
    
    if(check) {
        free(id);
    } else {
        // add it to the configuration
        TAILQ_INSERT_TAIL(&int_conf, id, entries);
    }

    rc = DAX_WALK_OK;
    
failed:

    if(rc != DAX_WALK_OK && id != NULL)
        free(id);
    
    return rc;
}


/**
 * Handler for dax_walk_list to parse each configured policer item
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
parse_policer(dax_walk_data_t * dwd __unused,
              ddl_handle_t * dop,
              int action __unused,
              void * data)
{
    policer_t * pol = NULL;
    ddl_handle_t * conf_op = NULL;
    int rc = DAX_WALK_ABORT;
    char value;
    int check = *((int *)data);

    junos_trace(DPM_TRACEFLAG_CONF, "%s", __func__);
    
    INSIST_ERR(dop != NULL);
    
    pol = calloc(1, sizeof(policer_t));
    INSIST_ERR(pol != NULL);
    
    if (!dax_get_stringr_by_name(dop, DDLNAME_POL_POLICER_NAME,
            pol->policer.name, sizeof(pol->policer.name))) {
        dax_error(dop, "Failed to parse policer name");
        goto failed;
    }
    
    if(dax_get_object_by_name(dop, DDLNAME_POL_IF_EXCEEDING, &conf_op, FALSE)) {
        
        if(!dax_get_uint64_by_name(conf_op,
                DDLNAME_POL_IF_EXCEEDING_BURST_SIZE_LIMIT,
                &pol->policer.if_exceeding.burst_size_limit)) {
            dax_error(dop, "Failed to parse burst size limit");
            goto failed;
        }
        
        if(dax_get_uint_by_name(conf_op,
                DDLNAME_POL_IF_EXCEEDING_BANDWIDTH_PERCENT,
                &pol->policer.if_exceeding.bw_u.bandwidth_percent)) {

            pol->policer.if_exceeding.bw_in_percent = 1;   
        } else {
            dax_get_uint64_by_name(conf_op,
                    DDLNAME_POL_IF_EXCEEDING_BANDWIDTH_LIMIT,
                    &pol->policer.if_exceeding.bw_u.bandwidth_limit);
        }
                
        if(pol->policer.if_exceeding.bw_u.bandwidth_percent == 0 && 
                pol->policer.if_exceeding.bw_u.bandwidth_limit == 0) {
            dax_error(dop, "Failed to parse either a bandwidth limit "
                    "or a bandwidth percent");
            goto failed;
        }
        
        dax_release_object(&conf_op);
    }
    
    if(dax_get_object_by_name(dop, DDLNAME_POL_THEN, &conf_op, FALSE)) {

        if(dax_get_toggle_by_name(conf_op, DDLNAME_POL_THEN_DISCARD,
                &value)) {
            pol->policer.action.discard = 1;
        }
        
        dax_get_ubyte_by_name(conf_op, DDLNAME_POL_THEN_LOSS_PRIORITY,
                &pol->policer.action.loss_priority);
                
        dax_get_ubyte_by_name(conf_op, DDLNAME_POL_THEN_FORWARDING_CLASS,
                &pol->policer.action.forwarding_class);
                
        if(pol->policer.action.discard == 0 && 
                pol->policer.action.loss_priority == 0 && 
                pol->policer.action.forwarding_class == 0) {
            dax_error(dop, "Failed to parse an action in the 'then' stanza");
            goto failed;
        }
    }
    
    if(check) {
        free(pol);
    } else {
        // add it to the configuration
        
        patricia_node_init_length(&pol->node, strlen(pol->policer.name) + 1);

        if(!patricia_add(&pol_conf, &pol->node)) {
            LOG(TRACE_LOG_ERR, "%s: Failed to add "
                    "policer to configuration", __func__);
            goto failed;
        }
    }

    rc = DAX_WALK_OK;
    
failed:

    if(conf_op)
        dax_release_object(&conf_op);

    if(rc != DAX_WALK_OK && pol != NULL)
        free(pol);
    
    return rc;
}


/**
 * Find a default policy for the given IFL if one exists
 * 
 * @param[in] ifl
 *      An IFL that exists in the system
 * 
 * @param[in] cookie
 *      User data passed to handler
 * 
 * @return
 *      KCOM_OK upon success
 */
static int
match_ifl(kcom_ifl_t * ifl, void * cookie __unused)
{
    int_def_t * id;
    char full_iflname[MAX_INT_NAME + 1]; 

    sprintf(full_iflname, "%s.%d", ifl->ifl_name, ifl->ifl_subunit);

    // search for a match in all the interface name patterns
    id = TAILQ_FIRST(&int_conf);
    while(id) {
        
        if(fnmatch(id->name_pattern, full_iflname, 0) == 0) {
            // Send the default policy for this interface
            notify_interface_add(ifl->ifl_name, ifl->ifl_subunit, 
                    id, ifl->ifl_index);
            return KCOM_OK;
        }
        
        id = TAILQ_NEXT(id, entries);
    }
    
    return KCOM_OK;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 */
void
init_config(void)
{
    classic_filters = FALSE;
    
    patricia_root_init(&pol_conf, FALSE, MAX_POL_NAME + 1, 0);
    patricia_root_init(&sub_conf, FALSE, MAX_SUB_NAME + 1, 0);
    TAILQ_INIT(&int_conf);
    TAILQ_INIT(&logged_in);
}


/**
 * Clear and reset the entire configuration, freeing all memory.
 */
void
clear_config(void)
{
    policer_t * pol;
    int_def_t * id;
    subscriber_t * sub;
    
    junos_trace(DPM_TRACEFLAG_CONF, "%s", __func__);
    
    classic_filters = FALSE;
    
    while(NULL != (pol = pol_entry(patricia_find_next(&pol_conf, NULL)))) {

        if(!patricia_delete(&pol_conf, &pol->node)) {
            LOG(TRACE_LOG_ERR, "%s: Deleting policer failed", __func__);
            continue;
        }
        free(pol);
    }
    
    while((id = TAILQ_FIRST(&int_conf)) != NULL) {
        TAILQ_REMOVE(&int_conf, id, entries);
        free(id);
    }
    
    while(NULL != (sub = sub_entry(patricia_find_next(&sub_conf, NULL)))) {

        if(!patricia_delete(&sub_conf, &sub->node)) {
            LOG(TRACE_LOG_ERR, "%s: Deleting subscriber failed", __func__);
            continue;
        }
        free(sub);
    }
    
    clear_logins();
}


/**
 * Clear status of all logins
 */
void
clear_logins(void)
{
    login_class_t * lc;
    login_sub_t * ls;
    
    while((lc = TAILQ_FIRST(&logged_in)) != NULL) {
        
        TAILQ_REMOVE(&logged_in, lc, entries);
        
        while((ls = TAILQ_FIRST(&lc->subs)) != NULL) {
            TAILQ_REMOVE(&lc->subs, ls, entries);
            free(ls);
        }
        free(lc);
    }
}


/**
 * Send the entire configuration to the ctrl component
 */
void
send_configuration(void)
{
    policer_t * pol;
    subscriber_t * sub;
    
    junos_trace(DPM_TRACEFLAG_CONF, "%s", __func__);
    
    notify_configuration_reset(classic_filters);
    
    pol = pol_entry(patricia_find_next(&pol_conf, NULL));
    while(pol != NULL) {
        notify_policy_add(&pol->policer);
        pol = pol_entry(patricia_find_next(&pol_conf, &pol->node));
    }
    
    if(junos_kcom_ifl_get_all(match_ifl, NULL, NULL) != KCOM_OK) {
        LOG(TRACE_LOG_ERR, "%s: Failed to go through IFLs", __func__);
    }
    
    sub = sub_entry(patricia_find_next(&sub_conf, NULL));
    while(sub != NULL) {
        notify_subscriber_add(&sub->subscriber);
        sub = sub_entry(patricia_find_next(&sub_conf, &sub->node));
    }
    
    notify_configuration_complete();
}


/**
 * Record the subscriber login
 * 
 * @param[in] s_name
 *     subscriber name
 * 
 * @param[in] c_name
 *     subscriber's class name
 */
void
subscriber_login(char * s_name, char * c_name)
{
    login_class_t * lc;
    login_sub_t * ls;

    // find the class first
    lc = TAILQ_FIRST(&logged_in);
    while(lc != NULL) {
        if(strcmp(lc->class, c_name) == 0) { // found the same class
            // insert subscriber
            ls = calloc(1, sizeof(login_sub_t));
            INSIST_ERR(ls != NULL);
            strcpy(ls->name, s_name);
            TAILQ_INSERT_TAIL(&lc->subs, ls, entries);
            return;
        }
        
        lc = TAILQ_NEXT(lc, entries);
    }
    
    // didn't find a matching class, so create it
    lc = calloc(1, sizeof(login_class_t));
    INSIST_ERR(lc != NULL);
    
    ls = calloc(1, sizeof(login_sub_t));
    INSIST_ERR(ls != NULL);
    
    TAILQ_INIT(&lc->subs);
    strcpy(lc->class, c_name);
    strcpy(ls->name, s_name);
    
    TAILQ_INSERT_TAIL(&logged_in, lc, entries);
    TAILQ_INSERT_TAIL(&lc->subs, ls, entries);
}


/**
 * Remove the record of the previous subscriber login
 * 
 * @param[in] s_name
 *     subscriber name
 * 
 * @param[in] c_name
 *     subscriber's class name
 */
void
subscriber_logout(char * s_name, char * c_name)
{
    login_class_t * lc;
    login_sub_t * ls;

    // find the class first
    lc = TAILQ_FIRST(&logged_in);
    while(lc != NULL) {
        if(strcmp(lc->class, c_name) == 0) { // found the same class

            // find the subscriber to remove
            ls = TAILQ_FIRST(&lc->subs);
            while(ls != NULL) {
                if(strcmp(ls->name, s_name) == 0) { // found the same subscriber
                    TAILQ_REMOVE(&lc->subs, ls, entries);
                    free(ls);
                    // we don't bother cleaning up lc even if lc->subs is empty
                    return;
                }
                ls = TAILQ_NEXT(ls, entries);                
            }
            
            junos_trace(DPM_TRACEFLAG_CONF, "%s: (Warning) Didn't find a "
                    "matching subscriber", __func__);
            return;
        }
        
        lc = TAILQ_NEXT(lc, entries);
    }
    
    junos_trace(DPM_TRACEFLAG_CONF, "%s: (Warning) Didn't find a "
            "matching class", __func__);
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
dpm_config_read(int check)
{
    char value;
    ddl_handle_t * top = NULL, * dop = NULL;
    ddl_handle_t * pol_dop = NULL, * int_dop = NULL, * sub_dop = NULL;
    int rc = EFAIL;

    junos_trace(DPM_TRACEFLAG_CONF, "%s: Starting dpm "
            "configuration load", __func__);
    
    // Load the main configuration ...
    
    if (!dax_get_object_by_path(NULL, dpm_config, &top, FALSE))  {

        junos_trace(DPM_TRACEFLAG_CONF, 
                "%s: Cleared dpm configuration", __func__);

        if(!check)
            clear_config();
        return SUCCESS;
    }
    
    if (!dax_is_changed(top)) { // if not changed
        junos_trace(DPM_TRACEFLAG_CONF, "%s: No change in DPM"
            " configuration", __func__);
        dax_release_object(&top);
        return SUCCESS;
    }
    
    if(!check)
        clear_config(); // clear everything and start fresh
    
    if(dax_get_toggle_by_name(top, DDLNAME_SYNC_DPM_USE_CLASSIC_FILTERS,
            &value)) {
        classic_filters = TRUE;
    }

    if(dax_get_object_by_name(top, DDLNAME_POLICER, &pol_dop, TRUE)) {
        if(dax_walk_list(pol_dop, DAX_WALK_CONFIGURED, parse_policer, &check)
                != DAX_WALK_OK) {

            junos_trace(DPM_TRACEFLAG_CONF, "%s: walk policers "
                "list in DPM configuration failed", __func__);
            
            goto failed;
        }
    }

    if(dax_get_object_by_name(top, DDLNAME_INT_DEFAULTS, &int_dop, TRUE) &&
        dax_get_object_by_name(int_dop, DDLNAME_IFL_PATTERN, &dop, TRUE)) {
        
        if(dax_walk_list(dop, DAX_WALK_CONFIGURED, parse_int_default, &check)
                != DAX_WALK_OK) {

            junos_trace(DPM_TRACEFLAG_CONF, "%s: walk interface defaults "
                "list in DPM configuration failed", __func__);
            
            goto failed;
        }
        dax_release_object(&dop);
    }
    
    if(dax_get_object_by_name(top, DDLNAME_SUBSCRIBER_DB, &sub_dop, TRUE) &&
        dax_get_object_by_name(sub_dop, DDLNAME_CLASS, &dop, TRUE)) {
        
        if(dax_walk_list(dop, DAX_WALK_CONFIGURED, parse_class, &check)
                != DAX_WALK_OK) {

            junos_trace(DPM_TRACEFLAG_CONF, "%s: walk subscriber classes "
                "list in DPM configuration failed", __func__);
            
            goto failed;
        }
    }

    if(!check)
        send_configuration();
    
    junos_trace(DPM_TRACEFLAG_CONF, 
            "%s: Loaded dpm configuration", __func__);

    rc = SUCCESS;

failed:

    if(pol_dop)
        dax_release_object(&pol_dop);

    if(sub_dop)
        dax_release_object(&sub_dop);
    
    if(int_dop)
        dax_release_object(&int_dop);
    
    if(dop)
        dax_release_object(&dop);
    
    if(top)
        dax_release_object(&top);
    
    return rc;
}

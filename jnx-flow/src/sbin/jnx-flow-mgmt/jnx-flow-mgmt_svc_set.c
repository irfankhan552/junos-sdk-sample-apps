/*
 * $Id: jnx-flow-mgmt_svc_set.c 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * jnx-flow-mgmt_svc_set.c - svc set related routines.
 *
 * Vivek Gupta, Aug 2007
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file jnx-flow-mgmt_svc_set.c
 * @brief
 * This file contains all the service set maintenance routines
 * Also contains the service set configuration dispatch routine
 * to the data agent
 */
#include <stdlib.h>
#include <sys/param.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <ctype.h>

#include <ddl/dax.h>

#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include JNX_FLOW_MGMT_OUT_H
#include <jnx/junos_trace.h>
#include <jnx/parse_ip.h>

#include <jnx/jnx-flow.h>
#include <jnx/jnx-flow_msg.h>

#include "jnx-flow-mgmt.h"
#include "jnx-flow-mgmt_config.h"
#include "jnx-flow-mgmt_rule.h"
#include "jnx-flow-mgmt_ssrb.h"

static int 
jnx_flow_parse_service_set (dax_walk_data_t *dwd __unused, ddl_handle_t *dop,
                     int action __unused, void *data);

static int
jnx_flow_parse_svc_set_rules(dax_walk_data_t *dwd __unused, ddl_handle_t *dop,
                             int action __unused, void *data);

static void
jnx_flow_add_svc_set_to_list(jnx_flow_cfg_t* flow_cfg, 
                             jnx_flow_svc_set_list_t* svc_set);

static void
jnx_flow_add_rule_name_list_to_svc_set(jnx_flow_svc_set_list_t* svc_set,
                                       jnx_flow_rule_name_list_t* rule_list,
                                       int check);

static  jnx_flow_svc_set_list_t* 
jnx_flow_get_svc_set_from_list( jnx_flow_svc_set_list_t* svc_set_list,
                                char* name);
static void 
jnx_flow_free_svc_list(jnx_flow_svc_set_list_t * svc_set_list);

/**
 * Read the service set configuration specific to jnx-flow application
 */
int 
jnx_flow_mgmt_read_service_set_config(int check, jnx_flow_cfg_t* flow_cfg)
{
    ddl_handle_t *top = NULL;
    uint32_t status = TRUE;
    const char* jnx_flow_svc_set_path[] = {"services", "service-set", NULL};

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"read service set config");

    if (!dax_get_object_by_path(NULL, jnx_flow_svc_set_path, &top, FALSE)) {

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "service set is absent");
    }  else {

        /* Check if the config has changed */
        if (!check && dax_is_changed(top) == FALSE) {
            dax_release_object(&top);
            jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"config not changed");
            return status;
        }

        if (dax_walk_list (top, DAX_WALK_CONFIGURED,
                           jnx_flow_parse_service_set, flow_cfg)) {

            dax_release_object(&top);
            jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d:walk service list failed",
                         __func__, __LINE__);
            return status; 
        }
    }

    return jnx_flow_mgmt_update_svc_set_list(flow_cfg);
}


/**
 * Update the service set configuration list 
 * Mark the service entries, deleted on the new configuration
 */
int
jnx_flow_mgmt_update_svc_set_list(jnx_flow_cfg_t * flow_cfg)
{
    jnx_flow_svc_set_list_t *svc_set = NULL;
    jnx_flow_svc_set_node_t *svc_set_node = NULL;
    /*
     * now check if any service set is deleted 
     * for every service set present in the list
     * check if it is there in the candidate config
     */
    for (svc_set_node = (typeof(svc_set_node))
         patricia_find_next( &jnx_flow_mgmt.svc_set_db, NULL);
         svc_set_node != NULL;
         svc_set_node = (typeof(svc_set_node))
         patricia_find_next(&jnx_flow_mgmt.svc_set_db, 
                            &svc_set_node->node)) {

        /* svc set is present */
        if ((jnx_flow_get_svc_set_from_list
             (flow_cfg->svc_set_list, svc_set_node->svc_set.svc_set_name))
            != NULL) {
            continue;
        }

        /* 
         * Implies this service set has been deleted from the config, 
         */
        if ((svc_set = calloc (1, sizeof(*svc_set))) == NULL) {

            jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d:calloc failed",
                         __func__, __LINE__);
            return TRUE;
        }

        svc_set->op_type = JNX_FLOW_MSG_CONFIG_DELETE;
        svc_set->svc_set_node = svc_set_node;

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"service set is marked delete %s",
                     svc_set_node->svc_set.svc_set_name);
    }
    return FALSE;
}

/**
 * parse the jnx-flow extension service set rules
 */
static int 
jnx_flow_parse_ext_service_set (dax_walk_data_t *dwd __unused,
                                ddl_handle_t *dop,
                                int action __unused, void *data)
{
    jnx_flow_svc_set_list_t*   svc_set_list = (typeof(svc_set_list))data;
    ddl_handle_t               *rule_obj;
    char                       ext_svc_name[JNX_FLOW_STR_SIZE];
    const char*                jnx_flow_svc_set_strings[] = {
        "service-set-name", "interface-service", 
        "nexthop-service", "extension-service", 
        "inside-service-interface", "outside-service-interface",
        "service-interface", "service-name", 
        "rules", NULL}; 

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"read rule config for \"%s\"",
                   svc_set_list->svc_set.svc_set_name);
    /*
     * Extension service is configured, so check if the service name
     * jnx-flow. If it is something else then this service set is for some
     * other extension service and we will just return from here.
     */
    if (!dax_get_stringr_by_name (dop, jnx_flow_svc_set_strings[7],
                                  ext_svc_name, JNX_FLOW_STR_SIZE)) {

        return  DAX_WALK_ABORT;
    }

    if (strcmp(ext_svc_name, "jnx-flow"))  {

        dax_release_object(&dop);
        svc_set_list->is_jnx_flow = FALSE;
        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d: \"%s\" is not jnx-flow service",
                     __func__, __LINE__,
                     svc_set_list->svc_set.svc_set_name);
        return DAX_WALK_OK;
    }


    /* did not find any rules congifured for the service set, return */
    if (!dax_get_object_by_name (dop, jnx_flow_svc_set_strings[8],
                                 &rule_obj, FALSE)) {
        return DAX_WALK_OK;
    }

    svc_set_list->rule_index = 0;

    if (dax_walk_list (rule_obj, DAX_WALK_CONFIGURED, 
                       jnx_flow_parse_svc_set_rules, 
                       svc_set_list)) {
        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d: config walk for \"%s\" failed",
                     __func__, __LINE__,
                     svc_set_list->svc_set.svc_set_name);

        dax_release_object(&rule_obj);
        return DAX_WALK_ABORT;
    }

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "<\"%s\", %d> config read successful",
                   svc_set_list->svc_set.svc_set_name,
                 svc_set_list->svc_set_node->svc_set_id);
    return DAX_WALK_OK;
}

/**
 * parse the service sets
 */
static int 
jnx_flow_parse_service_set (dax_walk_data_t *dwd __unused, 
                            ddl_handle_t *dop,
                     int action __unused, void *data)
{
    jnx_flow_rule_name_list_t * rule_list = NULL,
                              * prev = NULL, * next = NULL;
    jnx_flow_svc_set_list_t*   svc_set_list;
    jnx_flow_svc_set_node_t*   svc_set_node = NULL, *old_svc_set_node = NULL;
    ddl_handle_t               *ext_svc_obj, *svc_type_obj;
    jnx_flow_cfg_t*            flow_cfg = (jnx_flow_cfg_t*)data;
    const char*                jnx_flow_svc_set_strings[] = {
        "service-set-name", "interface-service", 
        "next-hop-service", "extension-service", 
        "inside-service-interface", "outside-service-interface",
        "service-interface", "service-name", 
        "rules", NULL}; 

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"parse service set configuration");

    /* Check if extension-service is configured for this service-set */
    if (!dax_get_object_by_name (dop, jnx_flow_svc_set_strings[3],
                                 &ext_svc_obj, FALSE)) {
        /* 
         * extension-service is not configured, so we will just return 
         */
        return DAX_WALK_OK;
    } 

    if ((svc_set_list = calloc (1, sizeof(*svc_set_list))) == NULL) {
        return DAX_WALK_ABORT;
    }

    if (!dax_get_stringr_by_name (dop, jnx_flow_svc_set_strings[0],
                                  svc_set_list->svc_set.svc_set_name,
                                  JNX_FLOW_STR_SIZE)) {
        free(svc_set_list);
        return DAX_WALK_ABORT;
    }

    if (dax_get_object_by_name (dop, jnx_flow_svc_set_strings[1],
                                &svc_type_obj, FALSE)) {

        svc_set_list->svc_set.svc_info.svc_type = JNX_FLOW_SVC_TYPE_INTERFACE;

        if (!dax_get_stringr_by_name 
            (svc_type_obj, jnx_flow_svc_set_strings[6],
             svc_set_list->svc_set.svc_info.info.intf_svc.svc_intf,
             IFNAMELEN)) {

            dax_release_object(&svc_type_obj);
            free(svc_set_list);
            return DAX_WALK_ABORT;
        }

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"service-set \"%s\""
                     " service interface \"%s\"",
                     svc_set_list->svc_set.svc_set_name,
                     svc_set_list->svc_set.svc_info.info.intf_svc.svc_intf);

    } else if (dax_get_object_by_name (dop, jnx_flow_svc_set_strings[2],
                                &svc_type_obj, FALSE)) {

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"service-set \"%s\""
                     " service-type \"nexthop\"",
                     svc_set_list->svc_set.svc_set_name);

        svc_set_list->svc_set.svc_info.svc_type = JNX_FLOW_SVC_TYPE_NEXTHOP;

        if (!dax_get_stringr_by_name (svc_type_obj, jnx_flow_svc_set_strings[4],
                                      svc_set_list->svc_set.svc_info.
                                      info.next_hop_svc.inside_interface, 
                                      IFNAMELEN)) {

            dax_release_object(&svc_type_obj);
            free(svc_set_list);
            return DAX_WALK_ABORT;
        }

        if (!dax_get_stringr_by_name (svc_type_obj, jnx_flow_svc_set_strings[5],
                                      svc_set_list->svc_set.svc_info.
                                      info.next_hop_svc.outside_interface, 
                                      IFNAMELEN)) {

            dax_release_object(&svc_type_obj);
            free(svc_set_list);
            return DAX_WALK_ABORT;
        }

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"service-set \"%s\""
                     " input-interface \"%s\" output-interface \"%s\"",
                     svc_set_list->svc_set.svc_set_name,
                     svc_set_list->svc_set.svc_info.info.
                     next_hop_svc.inside_interface,
                     svc_set_list->svc_set.svc_info.info.
                     next_hop_svc.outside_interface);

    } else {
        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s\" service-type get failed",
                     svc_set_list->svc_set.svc_set_name);
        return DAX_WALK_ABORT;
    }

    dax_release_object(&svc_type_obj);

    svc_set_list->op_type = JNX_FLOW_MSG_CONFIG_INVALID;
    svc_set_list->is_jnx_flow = TRUE;

    /* now svc set is configured */

    if ((old_svc_set_node = (typeof(svc_set_node))
         patricia_get(&jnx_flow_mgmt.svc_set_db,
                      strlen(svc_set_list->svc_set.svc_set_name) +1,
                      svc_set_list->svc_set.svc_set_name))) {

        svc_set_list->svc_set_node = old_svc_set_node;

        /* 
         * This service set exists in the db, compare the existing config to
         * the new one and find out what all changed.
         */
        if (memcmp(&old_svc_set_node->svc_set.svc_info,
                   &svc_set_list->svc_set.svc_info, 
                   sizeof(jnx_flow_svc_info_t))) {

            svc_set_list->op_type = JNX_FLOW_MSG_CONFIG_CHANGE;
        }
    } else {

        if (!(svc_set_node = calloc(1, sizeof(*svc_set_node)))) {
            jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d:alloc failed",
                         __func__, __LINE__);
            return DAX_WALK_ABORT;
        }


        svc_set_list->svc_set_node = svc_set_node;

        memcpy(&svc_set_node->svc_set, &svc_set_list->svc_set, 
               sizeof(jnx_flow_svc_set_t));

        svc_set_list->op_type = JNX_FLOW_MSG_CONFIG_ADD;
        svc_set_list->svc_set.rule_list = NULL;
    }

    if (dax_walk_list (ext_svc_obj, DAX_WALK_CONFIGURED,
                       jnx_flow_parse_ext_service_set, svc_set_list)) {
        dax_release_object(&dop);
        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"service-set \"%s\" walk config failed",
                     svc_set_list->svc_set.svc_set_name);
        jnx_flow_free_svc_list(svc_set_list);
        return DAX_WALK_ABORT; 
    }

    dax_release_object(&dop);

    if (svc_set_list->is_jnx_flow == FALSE) {
        jnx_flow_free_svc_list(svc_set_list);
        return DAX_WALK_OK;
    }

    /* now reverse the rule list in the service set entry */
    rule_list = svc_set_list->rule_list;
    if (rule_list) {
        prev = NULL;
        next = rule_list->next;

        while (next) {
            rule_list->next = prev;
            prev = rule_list;
            rule_list = next;
            next = next->next;
        }
        rule_list->next = prev;

        svc_set_list->rule_list = rule_list;
    }

    jnx_flow_add_svc_set_to_list(flow_cfg, svc_set_list);
    return DAX_WALK_OK;
}

/**
 * parse the service set extension rules
 */
static int
jnx_flow_parse_svc_set_rules(dax_walk_data_t *dwd __unused, ddl_handle_t *dop,
                             int action __unused, void *data)
{
    jnx_flow_rule_list_t       *prule_list;
    jnx_flow_rule_name_list_t  *prule_name;
    jnx_flow_svc_set_list_t    *svc_set_list = (jnx_flow_svc_set_list_t*)data;

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"%s:%d:<\"%s\", %d>",
                 __func__, __LINE__, svc_set_list->svc_set.svc_set_name,
                 svc_set_list->svc_set_node->svc_set_id);

    if ((prule_name = calloc (1, sizeof(jnx_flow_rule_name_list_t))) == NULL) {

        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d:calloc failed", __func__, __LINE__);
        return DAX_WALK_ABORT;
    }

    if (!dax_get_stringr_by_name (dop, "rule-name",
                                  prule_name->rule_name, JNX_FLOW_STR_SIZE)) {

        free(prule_name);
        return DAX_WALK_ABORT;
    }

    /*
     * get the rule entry from the candidate config
     */
    prule_list = jnx_flow_cfg.rule_list;

    if (prule_list == NULL) {

        /* 
         * Implies that no rule were present in the candidate config and hence
         * look in the rule-db
         */
         if ((prule_name->rule_node = (jnx_flow_rule_node_t*)patricia_get(&jnx_flow_mgmt.rule_db,
                      strlen(prule_name->rule_name) + 1, 
                      prule_name->rule_name)) == NULL) {

            free(prule_name);
            return DAX_WALK_ABORT;
         } 
        
    }else {

    for (;(prule_list); prule_list = prule_list->next) {
        if (strcmp(prule_list->rule.rule_name,
                   prule_name->rule_name)) {
            continue;
        }
        prule_name->rule_node = prule_list->rule_node;
        break;
    }

    if (!prule_list) {
        free(prule_name);
        return DAX_WALK_ABORT;
    }
    }

    svc_set_list->rule_index++;
    prule_name->rule_index = svc_set_list->rule_index;

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"service-rule<<\"%s\", %d>, <\"%s\", %d>, %d>",
                 svc_set_list->svc_set.svc_set_name,
                 svc_set_list->svc_set_node->svc_set_id,
                 prule_name->rule_name, prule_name->rule_node->rule_id,
                 prule_name->rule_index);

    jnx_flow_add_rule_name_list_to_svc_set(svc_set_list, prule_name, TRUE);
    return DAX_WALK_OK;
}

/**
 * update the service set in the database according to the 
 * new configuration
 */
int
jnx_flow_mgmt_update_service_set_db(jnx_flow_cfg_t* flow_cfg)
{
    jnx_flow_svc_set_list_t*   svc_set_list = flow_cfg->svc_set_list;
    jnx_flow_svc_set_node_t*   svc_set_node = NULL;
    jnx_flow_rule_name_list_t* old = NULL, *new = NULL, * prev = NULL;
    jnx_flow_rule_node_t*      old_rule = NULL, * new_rule = NULL;
    jnx_flow_ssrb_node_t*      ssrb_node = NULL;


    for (svc_set_list = flow_cfg->svc_set_list; svc_set_list != NULL;
         svc_set_list = svc_set_list->next) {


        if (!(svc_set_node = svc_set_list->svc_set_node)) {
            jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"\"%s\" node is absent "
                         " in the config list",
                         svc_set_list->svc_set.svc_set_name);
            assert(0);
            continue;
        }

        switch (svc_set_list->op_type) {

            case JNX_FLOW_MSG_CONFIG_ADD:

                /* 
                 * Check in the SSRB DB to see if the Name to ID
                 * has been resolved.
                 */ 
                if ((ssrb_node = (typeof(ssrb_node))
                     patricia_get (&jnx_flow_mgmt.ssrb_db,
                                   strlen(svc_set_node->svc_set.svc_set_name)
                                   + 1,
                                   svc_set_node->svc_set.svc_set_name))
                    != NULL) {

                    svc_set_node->svc_set_id = ssrb_node->svc_set_id;

                    svc_set_node->in_nh_idx = ssrb_node->in_nh_idx;
                    svc_set_node->out_nh_idx = ssrb_node->out_nh_idx;
                } else {
                    svc_set_node->svc_set_id = JNX_FLOW_SVC_SET_ID_UNASSIGNED;

                    nh_idx_t_setval(svc_set_node->in_nh_idx, 
                                    JNX_FLOW_NEXTHOP_IDX_UNASSIGNED);

                    nh_idx_t_setval(svc_set_node->out_nh_idx, 
                                    JNX_FLOW_NEXTHOP_IDX_UNASSIGNED);
                }

                jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"service-set <\"%s\", %d>",
                             svc_set_node->svc_set.svc_set_name,
                             svc_set_node->svc_set_id);

                patricia_node_init_length (&svc_set_node->node, 
                                           strlen(svc_set_node->svc_set.
                                                  svc_set_name) + 1 );

                if (!patricia_add(&jnx_flow_mgmt.svc_set_db,
                                  &svc_set_node->node)) {
                    jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d: service-set <\"%s\",  %d>"
                                 " patricia add failed",
                                 __func__, __LINE__,
                                 svc_set_node->svc_set.svc_set_name,
                                 svc_set_node->svc_set_id);
                }
                jnx_flow_mgmt.sset_count++;
                break;


            case JNX_FLOW_MSG_CONFIG_DELETE:
                break;

            case JNX_FLOW_MSG_CONFIG_CHANGE:
                svc_set_node->svc_set.svc_info = svc_set_list->svc_set.svc_info;

            default:
                break;
        }

        /*
         * now check the service rule sets,
         */
        old = svc_set_node->svc_set.rule_list;
        new = svc_set_list->rule_list;

        for (prev = NULL;(old); prev = old, old = (old) ? old->next : NULL,
             new = (new) ? new->next : NULL) {

            old_rule = (typeof(old_rule))
                patricia_get(&jnx_flow_mgmt.rule_db,
                             strlen(old->rule_name) + 1,
                             old->rule_name);

            old->op_type = JNX_FLOW_MSG_CONFIG_INVALID;

            /*
             * no more entries in the candidate list,
             * mark the existing entries in the active
             * list as delete
             */

            if (new == NULL) {

                if (old_rule) {
                    old_rule->use_count--;
                }

                old->op_type = JNX_FLOW_MSG_CONFIG_DELETE;

                /*
                 * delete from active rule list
                 */
                if (prev) {
                    prev->next = old->next;
                } else {
                    svc_set_node->svc_set.rule_list = old->next;
                }

                old->next = NULL;

                /*
                 * add to the candidate list
                 */
                jnx_flow_add_rule_name_list_to_svc_set(svc_set_list, old, TRUE);

                old = prev;
                continue;
            }

            if (!strcmp(old->rule_name, new->rule_name)) {
                continue;
            }

            /* rule names did not match */

            if (!(new_rule = (typeof(new_rule))
                  patricia_get(&jnx_flow_mgmt.rule_db,
                               strlen(new->rule_name) + 1,
                               new->rule_name))) {
                return TRUE;
            }

            /*
             * mark the entry as changed, & copy the rule name
             * adjust the rule reference count
             */

            if (old_rule) {
                old_rule->use_count--;
            }

            old->rule_node = new_rule;
            strncpy(old->rule_name, new->rule_name, sizeof(old->rule_name));

            /*
             * populate the new rule node 
             */

            new->op_type = JNX_FLOW_MSG_CONFIG_CHANGE;
            new->rule_node = new_rule;

            new_rule->use_count++;

        }

        /*
         * mark the newly added rule names as config-add,
         * add them to the current active rule list
         */

        for (;(new); prev = old, new = new->next) {

            new->op_type = JNX_FLOW_MSG_CONFIG_ADD;

            if (!(new_rule = (typeof(new_rule))
                  patricia_get(&jnx_flow_mgmt.rule_db,
                               strlen(new->rule_name) + 1,
                               new->rule_name))) {
                return TRUE;
            }

            if ((old = calloc (1, sizeof(*old))) == NULL) {

                return TRUE;
            }

            new->rule_node  = new_rule;
            old->rule_node  = new_rule;
            old->rule_index = new->rule_index;
            strncpy(old->rule_name, new->rule_name, sizeof(old->rule_name));
            new_rule->use_count++;

            if (prev) {
                prev->next = old;
            } else {
                svc_set_node->svc_set.rule_list = old;
            }

            jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"<\"%s\", \"%s\", %d> \"%s\"",
                         svc_set_list->svc_set.svc_set_name,
                         new->rule_name, new->rule_index,
                         jnx_flow_config_op_str[new->op_type]);
        }

        /*
         * now candidate list has all the change/add/delete rules
         * information,
         * and the rule list has all the updated information
         * nothing is needed, to be done..
         * just take care that the candidate list is freed
         * at end
         */
    }

    jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"service db update successful");

    return FALSE;
}

/**
 * frame a service set configuaration message for the 
 * data agent
 */
uint32_t 
jnx_flow_mgmt_frame_svc_msg(jnx_flow_svc_set_node_t * svc_set_node,
                            jnx_flow_msg_sub_header_info_t * sub_hdr,
                            uint32_t op_type)
{
    uint32_t tmp, in, out;
    jnx_flow_svc_set_t * psvc_set = NULL;
    jnx_flow_msg_svc_info_t * psvc_set_msg = NULL;

    if (op_type == JNX_FLOW_MSG_CONFIG_INVALID) {
        return 0;
    }

    psvc_set = &svc_set_node->svc_set;

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"<\"%s\", %d> \"%s\"",
                 svc_set_node->svc_set.svc_set_name, 
                 svc_set_node->svc_set_id, 
                 jnx_flow_config_op_str[op_type]);

    sub_hdr->msg_type = op_type;
    sub_hdr->err_code = JNX_FLOW_ERR_NO_ERROR;
    sub_hdr->msg_len  = htons(sizeof(*sub_hdr) + sizeof(*psvc_set_msg));

    psvc_set_msg = (typeof(psvc_set_msg))
        ((uint8_t *)sub_hdr + sizeof(*sub_hdr));

    memset(psvc_set_msg, 0, sizeof(*psvc_set_msg));

    strncpy(psvc_set_msg->svc_name, psvc_set->svc_set_name,
            sizeof(psvc_set_msg->svc_name));

    psvc_set_msg->svc_type  = psvc_set->svc_info.svc_type;
    psvc_set_msg->svc_index = htonl(svc_set_node->svc_set_id);

    if (psvc_set->svc_info.svc_type == JNX_FLOW_SVC_TYPE_INTERFACE) {
        strncpy(psvc_set_msg->svc_intf,
                psvc_set->svc_info.info.intf_svc.svc_intf,
                sizeof(psvc_set_msg->svc_intf));
    } else {
        sscanf(psvc_set->svc_info.info.next_hop_svc.inside_interface,
               "ms-%d/%d/%d.%d",  &tmp, &tmp, &tmp, &in);
        sscanf(psvc_set->svc_info.info.next_hop_svc.outside_interface,
               "ms-%d/%d/%d.%d",  &tmp, &tmp, &tmp, &out);
        psvc_set_msg->svc_out_subunit = htonl(out);
        psvc_set_msg->svc_in_subunit = htonl(in);
    }

    return (sizeof(*sub_hdr) + sizeof(*psvc_set_msg));
}

/**
 * frame a service set rule configuaration message for the 
 * data agent
 */
uint32_t 
jnx_flow_mgmt_frame_svc_rule_msg(jnx_flow_msg_sub_header_info_t * sub_hdr,
                                 jnx_flow_svc_set_node_t * psvc_set_node,
                                 jnx_flow_rule_name_list_t * rule_list,
                                 jnx_flow_config_type_t op_type,
                                 uint32_t flag)
{
    jnx_flow_msg_svc_rule_info_t * psvc_rule_msg;
    jnx_flow_rule_node_t * rule_node = NULL;

    if (rule_list == NULL) {
        assert(0);
    }

    rule_node = rule_list->rule_node;

    if (flag) {
        if (op_type != JNX_FLOW_MSG_CONFIG_DELETE) {
            return 0;
        }
    } else {
        if (op_type == JNX_FLOW_MSG_CONFIG_DELETE) {
            return 0;
        }
    }

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"< \"%s\" %d, \"%s\" %d, %d> \"%s\"",
                 psvc_set_node->svc_set.svc_set_name,
                 psvc_set_node->svc_set_id,
                 rule_list->rule_name, 
                 rule_node->rule_id,
                 rule_list->rule_index,
                 jnx_flow_config_op_str[op_type]);

    sub_hdr->msg_type = op_type;
    sub_hdr->err_code = JNX_FLOW_ERR_NO_ERROR;
    sub_hdr->msg_len  = htons(sizeof(*sub_hdr) + sizeof(*psvc_rule_msg));

    psvc_rule_msg     = (typeof(psvc_rule_msg))
        ((uint8_t *)sub_hdr + sizeof(*sub_hdr));

    psvc_rule_msg->svc_index          = htonl(psvc_set_node->svc_set_id);
    psvc_rule_msg->svc_rule_index     = htonl(rule_node->rule_id);
    psvc_rule_msg->svc_svc_rule_index = htonl(rule_list->rule_index);

    return (sizeof(*sub_hdr) + sizeof(*psvc_rule_msg));
}

/**
 * frame and send the service set configuration messages to
 * the data agent
 */
status_t
jnx_flow_mgmt_send_svc_config(jnx_flow_mgmt_data_session_t * pdata_pic,
                              jnx_flow_cfg_t  * flow_cfg)
{
    jnx_flow_config_type_t op_type = JNX_FLOW_MSG_CONFIG_ADD;
    uint32_t msg_len = 0, msg_count = 0, sub_len = 0;
    jnx_flow_svc_set_node_t    * svc_set_node = NULL;
    jnx_flow_svc_set_list_t    * svc_set = NULL;
    jnx_flow_msg_header_info_t * msg_hdr = NULL;
    jnx_flow_msg_sub_header_info_t * sub_hdr = NULL;

    /* send service set information */

    msg_hdr = (typeof(msg_hdr))jnx_flow_mgmt.send_buf;

    memset(msg_hdr, 0, sizeof(*msg_hdr));

    msg_hdr->msg_type = JNX_FLOW_MSG_CONFIG_SVC_INFO;

    msg_len   = sizeof(*msg_hdr);
    msg_count = 0;

    JNX_FLOW_MGMT_CFG_FIRST_SVC_SET_NODE(flow_cfg, svc_set); 

    sub_hdr = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));

    for (; (svc_set_node); 
         JNX_FLOW_MGMT_CFG_NEXT_SVC_SET_NODE(flow_cfg, svc_set)) {

        if (svc_set_node->svc_set_id == JNX_FLOW_SVC_SET_ID_UNASSIGNED) {
            continue;
        }

        if ((msg_count > 250) ||
            ((msg_len + sub_len) > JNX_FLOW_BUF_SIZE)) {
            msg_hdr->msg_len   = htons(msg_len);
            msg_hdr->msg_count = msg_count;
            jnx_flow_mgmt_send_config_msg(pdata_pic, msg_hdr,
                                          msg_hdr->msg_type,
                                          msg_len);
            msg_hdr   = (typeof(msg_hdr))jnx_flow_mgmt.send_buf;
            sub_hdr   = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));
            msg_len   = sizeof(*msg_hdr);
            msg_count = 0;
        }

        if ((sub_len = jnx_flow_mgmt_frame_svc_msg(svc_set_node,
                                                   sub_hdr, op_type))) {
            msg_len +=  sub_len;
            msg_count++;
            sub_hdr = (typeof(sub_hdr))((uint8_t *)sub_hdr + sub_len);
        }
    }

    if (msg_count) {
        msg_hdr->msg_len   = htons(msg_len);
        msg_hdr->msg_count = msg_count;
        jnx_flow_mgmt_send_config_msg(pdata_pic, msg_hdr, msg_hdr->msg_type,
                                      msg_len);
    }
    return EOK;
}

/**
 * frame and send a single service set configuration message to
 * the data agent
 */
status_t
jnx_flow_mgmt_send_svc_set_config(jnx_flow_mgmt_data_session_t * pdata_pic,
                                  jnx_flow_svc_set_node_t    * svc_set_node,
                                  jnx_flow_config_type_t op_type)
{
    uint32_t msg_len = 0, msg_count = 0, sub_len = 0;
    jnx_flow_msg_header_info_t * msg_hdr;
    jnx_flow_msg_sub_header_info_t * sub_hdr = NULL;

    /* send service information */
    msg_hdr = (typeof(msg_hdr))jnx_flow_mgmt.send_buf;

    memset(msg_hdr, 0, sizeof(*msg_hdr));

    msg_hdr->msg_type = JNX_FLOW_MSG_CONFIG_SVC_INFO;

    msg_len   = sizeof(*msg_hdr);
    msg_count = 0;

    sub_hdr = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));

    if ((sub_len = jnx_flow_mgmt_frame_svc_msg(svc_set_node,
                                               sub_hdr, op_type))) {
        msg_len +=  sub_len;
        msg_count++;
    }

    if (msg_count) {
        msg_hdr->msg_len   = htons(msg_len);
        msg_hdr->msg_count = msg_count;
        jnx_flow_mgmt_send_config_msg(pdata_pic, msg_hdr, msg_hdr->msg_type,
                                      msg_len);
    }

    return EOK;
}

/**
 * frame and send service set rule configuration messages to
 * the data agent
 */
status_t
jnx_flow_mgmt_send_svc_rule_config(jnx_flow_mgmt_data_session_t * pdata_pic,
                                   jnx_flow_cfg_t  * flow_cfg, uint32_t flag)
{
    jnx_flow_config_type_t op_type = JNX_FLOW_MSG_CONFIG_ADD;
    uint32_t msg_len = 0, msg_count = 0, sub_len = 0;
    jnx_flow_svc_set_node_t    * svc_set_node = NULL;
    jnx_flow_svc_set_list_t    * svc_set_list;
    jnx_flow_rule_name_list_t  * prule_name;
    jnx_flow_msg_header_info_t * msg_hdr;
    jnx_flow_msg_sub_header_info_t * sub_hdr = NULL;

    /* send service rule information */

    msg_hdr = (typeof(msg_hdr))jnx_flow_mgmt.send_buf;

    memset(msg_hdr, 0, sizeof(*msg_hdr));

    msg_hdr->msg_type = JNX_FLOW_MSG_CONFIG_SVC_RULE_INFO;

    msg_len = sizeof(*msg_hdr);
    msg_count = 0;

    JNX_FLOW_MGMT_CFG_FIRST_SVC_SET_NODE(flow_cfg, svc_set_list); 

    sub_hdr = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));

    for (; (svc_set_node) ;
         JNX_FLOW_MGMT_CFG_NEXT_SVC_SET_NODE(flow_cfg, svc_set_list)) {

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"%s:%d:<\"%s\", %d>", 
                        __func__, __LINE__, svc_set_node->svc_set.svc_set_name,
                     svc_set_node->svc_set_id);

        if (svc_set_node->svc_set_id == JNX_FLOW_SVC_SET_ID_UNASSIGNED) {
            continue;
        }

        if (flow_cfg) {
            prule_name = svc_set_list->rule_list;
        } else {
            prule_name = svc_set_node->svc_set.rule_list;
        }

        for (; (prule_name); prule_name = prule_name->next) {

            if ((msg_count > 250) || 
                (msg_len + sub_len) > JNX_FLOW_BUF_SIZE) {
                msg_hdr->msg_len   = htons(msg_len);
                msg_hdr->msg_count = msg_count;
                jnx_flow_mgmt_send_config_msg(pdata_pic, msg_hdr,
                                              msg_hdr->msg_type,
                                              msg_len);
                msg_hdr = (typeof(msg_hdr))jnx_flow_mgmt.send_buf;
                msg_len = sizeof(*msg_hdr);
                sub_hdr = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));
                msg_count = 0;
            }

            if (flow_cfg) {
                op_type = prule_name->op_type;
                if (op_type == JNX_FLOW_MSG_CONFIG_INVALID) {
                    continue;
                }
            } else {
                op_type = JNX_FLOW_MSG_CONFIG_ADD;
            }

            if ((sub_len =
                 jnx_flow_mgmt_frame_svc_rule_msg(sub_hdr, svc_set_node,
                                                  prule_name, op_type, flag)))
            {
                msg_count++;
                msg_len += sub_len;
                sub_hdr = (typeof(sub_hdr))((uint8_t *)sub_hdr + sub_len);
            }
        }
    }

    if (msg_count) {
        msg_hdr->msg_len   = htons(msg_len);
        msg_hdr->msg_count = msg_count;
        jnx_flow_mgmt_send_config_msg(pdata_pic, msg_hdr, msg_hdr->msg_type,
                                      msg_len);
    }
    return EOK;
}

/**
 * frame and send a service set rule configuration message to
 * the data agent
 */
status_t
jnx_flow_mgmt_send_svc_set_rule_config(jnx_flow_mgmt_data_session_t * pdata_pic,
                                       jnx_flow_svc_set_node_t * svc_set_node,
                                       uint32_t op_type)
{
    uint32_t msg_len = 0, msg_count = 0, sub_len = 0;
    jnx_flow_rule_name_list_t  * prule_name;
    jnx_flow_msg_header_info_t * msg_hdr;
    jnx_flow_msg_sub_header_info_t * sub_hdr = NULL;

    /* send service information */

    msg_hdr = (typeof(msg_hdr))jnx_flow_mgmt.send_buf;

    memset(msg_hdr, 0, sizeof(*msg_hdr));

    msg_hdr->msg_type = JNX_FLOW_MSG_CONFIG_SVC_RULE_INFO;

    msg_len   = sizeof(*msg_hdr);
    msg_count = 0;

    sub_hdr = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));

    for (prule_name = svc_set_node->svc_set.rule_list;
         (prule_name); prule_name = prule_name->next) {

        if ((msg_count > 250) || 
            (msg_len + sub_len) > JNX_FLOW_BUF_SIZE) {
            msg_hdr->msg_len   = htons(msg_len);
            msg_hdr->msg_count = msg_count;
            jnx_flow_mgmt_send_config_msg(pdata_pic, msg_hdr,
                                          msg_hdr->msg_type,
                                          msg_len);
            msg_hdr = (typeof(msg_hdr))jnx_flow_mgmt.send_buf;
            sub_hdr = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));
            msg_len = sizeof(*msg_hdr);
            msg_count = 0;
        }

        if ((sub_len =
             jnx_flow_mgmt_frame_svc_rule_msg(sub_hdr, svc_set_node,
                                              prule_name, op_type, FALSE))) {
            msg_count++;
            msg_len += sub_len;
            sub_hdr = (typeof(sub_hdr))((uint8_t *)sub_hdr + sub_len);
        }
    }

    if (msg_count) {
        msg_hdr->msg_len   = htons(msg_len);
        msg_hdr->msg_count = msg_count;
        jnx_flow_mgmt_send_config_msg(pdata_pic, msg_hdr, msg_hdr->msg_type,
                                      msg_len);
    }

    return EOK;
}

/**
 * delete a service set 
 */
void 
jnx_flow_mgmt_delete_svc_set(jnx_flow_svc_set_node_t * svc_set_node)
{
    jnx_flow_rule_name_list_t  *rule_list = NULL, * next_rule_list = NULL;

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "svc-set <\"%s\", %d> delete",
                 svc_set_node->svc_set.svc_set_name,
                 svc_set_node->svc_set_id);

    /* delete from the patricia tree */
    if (!patricia_delete (&jnx_flow_mgmt.svc_set_db, &svc_set_node->node)) {
        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"%s:%d: <\"%s\",  %d> patricia delete failed",
                     __func__, __LINE__,
                     svc_set_node->svc_set.svc_set_name,
                     svc_set_node->svc_set_id);
    }
    jnx_flow_mgmt.sset_count--;

    /* clear the rule list for the service set */
    rule_list = svc_set_node->svc_set.rule_list;
    for (;(rule_list); rule_list = next_rule_list) {
        next_rule_list = rule_list->next;;
        rule_list->rule_node->use_count--;
        free(rule_list);
    }

    /* free the service set node */
    free(svc_set_node);
}

/**
 * add a rule entry to the service set rule list
 */
static void 
jnx_flow_add_rule_name_list_to_svc_set(jnx_flow_svc_set_list_t* svc_set,
                                       jnx_flow_rule_name_list_t* rule_list,
                                       int check)
{
    if (check) {
        rule_list->next = svc_set->rule_list;
        svc_set->rule_list = rule_list;
    } else {
        rule_list->next  = svc_set->svc_set.rule_list;
        svc_set->svc_set.rule_list = rule_list;
    }
    return;
}

/**
 * free a service set list entry
 */
static void 
jnx_flow_free_svc_list(jnx_flow_svc_set_list_t * svc_set_list)
{
    jnx_flow_rule_name_list_t  * rule_name_list = NULL,
                               * next_rule_name_list = NULL;
    /*
     * free all the rule entries in the service set
     */
    for (rule_name_list = svc_set_list->rule_list; 
         (rule_name_list != NULL);
         rule_name_list = next_rule_name_list) {

        next_rule_name_list = rule_name_list->next;
        free(rule_name_list);
    }

    svc_set_list->rule_list = NULL; 

    if (svc_set_list->op_type == JNX_FLOW_MSG_CONFIG_ADD) {
        free(svc_set_list->svc_set_node);
    }

    free(svc_set_list);
}

/**
 * add a service set to the configuration list
 */
static void 
jnx_flow_add_svc_set_to_list(jnx_flow_cfg_t* flow_cfg, 
                             jnx_flow_svc_set_list_t* svc_set) 
{
    svc_set->next = flow_cfg->svc_set_list;
    flow_cfg->svc_set_list = svc_set;
}

/**
 * find  a service set from the configuration list
 */
static  jnx_flow_svc_set_list_t*
jnx_flow_get_svc_set_from_list(jnx_flow_svc_set_list_t* svc_set_list,
                                char* name)
{
    jnx_flow_svc_set_list_t* tmp;

    for ( tmp = svc_set_list;
          tmp != NULL;
          tmp = tmp->next) {

        if (memcmp(tmp->svc_set.svc_set_name, name, JNX_FLOW_STR_SIZE) == 0) {
            break;
        }
    }
    return tmp;
}

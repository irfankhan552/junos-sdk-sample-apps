/*
 * $Id: jnx-flow-mgmt_config.c 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * jnx-flow-mgmt_config.c - config read routines.
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

/* 
 * @file jnx-flow-mgmt_config.c
 *    These routines read the configuration file for the 
 *    sample gateway application confugration items. 
 * There are three main configuration items,
 * 1. service set configuration
 * 2. rules & rule set related configuration
 *
 * These are maintained as patricia trees in the
 * main global management control block
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
#include <jnx/pconn.h>
#include <jnx/junos_kcom.h>
#include JNX_FLOW_MGMT_OUT_H
#include <jnx/junos_trace.h>
#include <jnx/parse_ip.h>

#include <jnx/jnx-flow.h>
#include <jnx/jnx-flow_msg.h>

#include "jnx-flow-mgmt.h"
#include "jnx-flow-mgmt_config.h"
#include "jnx-flow-mgmt_rule.h" 

jnx_flow_cfg_t  jnx_flow_cfg;

const char *jnx_flow_config_path[] = { "sdk", "jnpr", "jnx-flow", NULL};

/**
 * read the configuration items for the jnx-flow application
 */
int
jnx_flow_mgmt_config_read(int check) 
{
    uint32_t           status = FALSE;
    ddl_handle_t      *top = NULL;

    const char *jnx_flow_service_path[] =
             { "services", NULL};

    memset(&jnx_flow_cfg, 0, sizeof(jnx_flow_cfg));

    jnx_flow_cfg.check = check;

    /*
     * read the jnx-flow rule data base
     */
    if (!dax_get_object_by_path(NULL, jnx_flow_config_path, &top, FALSE))  {

        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"rule config is absent");

        jnx_flow_mgmt_update_rule_list(&jnx_flow_cfg);
    } else if ( check || dax_is_changed (top)) {

        if (jnx_flow_mgmt_read_rule_config(check, &jnx_flow_cfg)) {

            jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"rule config read failed");
            /* mark it as check */
            jnx_flow_cfg.check = TRUE;
            jnx_flow_mgmt_free_cfg(&jnx_flow_cfg);
            dax_release_object(&top);
            return TRUE;
        }
        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "rule config read successful");
    }

    /*
     * read the jnx-flow service-set data base
     */
    if (!dax_get_object_by_path(NULL, jnx_flow_service_path, &top, FALSE))  {

        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"service-set config is absent");

        jnx_flow_mgmt_update_svc_set_list(& jnx_flow_cfg);

    } else if (check || dax_is_changed (top)) {

        if (jnx_flow_mgmt_read_service_set_config(check, &jnx_flow_cfg)) {
            jnx_flow_log(JNX_FLOW_CONFIG,LOG_INFO,"service-set config read failed");
            /* mark it as check */
            jnx_flow_cfg.check = TRUE;
            jnx_flow_mgmt_free_cfg(&jnx_flow_cfg);
            dax_release_object(&top);
            return TRUE;
        }
    }

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"service-set config read successful");

    /*
     * if check flag set, return without updating the
     * local database
     */
    if (check) {
        jnx_flow_mgmt_free_cfg(&jnx_flow_cfg);
        return status;
    }

    /*
     * update the rule database
     */
    if (jnx_flow_mgmt_update_rule_db(&jnx_flow_cfg)) {

        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"update rule db failed");
        /* mark it as check */
        jnx_flow_cfg.check = TRUE;
        jnx_flow_mgmt_free_cfg(&jnx_flow_cfg);
        dax_release_object(&top);
        return TRUE;
    }

    /*
     * update the service-set database
     */
    if (jnx_flow_mgmt_update_service_set_db(&jnx_flow_cfg)) {

        jnx_flow_log(JNX_FLOW_CONFIG, LOG_INFO,"update service-set db failed");
        /* mark it as check */
        jnx_flow_cfg.check = TRUE;
        jnx_flow_mgmt_free_cfg(&jnx_flow_cfg);
        dax_release_object(&top);
        return TRUE;
    }

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"update service-set db successful");
    /* 
     * Now, prepare the list of updates which needs to be sent to the
     * data-pic. Updates include:
     * a. Rules Added, Deleted or Modified, 
     * b. Service Set: Added, deleted or modified.
     */
    jnx_flow_mgmt_send_svc_rule_config(NULL, &jnx_flow_cfg, TRUE);
    jnx_flow_mgmt_send_rule_config(NULL, &jnx_flow_cfg);
    jnx_flow_mgmt_send_svc_config(NULL, &jnx_flow_cfg);
    jnx_flow_mgmt_send_svc_rule_config(NULL, &jnx_flow_cfg, FALSE);

    jnx_flow_mgmt_free_cfg(&jnx_flow_cfg);

    /* release the acquired object */
    if (top) dax_release_object(&top);

    return status;
}


/**
 * initialize the data bases
 */
int 
jnx_flow_mgmt_config_init()
{
    patricia_root_init (&jnx_flow_mgmt.rule_db, FALSE,
                        JNX_FLOW_STR_SIZE, 0);
    patricia_root_init (&jnx_flow_mgmt.svc_set_db, FALSE,
                        JNX_FLOW_STR_SIZE, 0);
    patricia_root_init (&jnx_flow_mgmt.ssrb_db, FALSE,
                        JNX_FLOW_STR_SIZE, 0);
    
    return 0;
}

/**
 * free a candidate configuration list 
 */
void 
jnx_flow_mgmt_free_cfg(jnx_flow_cfg_t*  flow_cfg)
{
    jnx_flow_rule_list_t       * rule_list = NULL,
                               * next_rule_list = NULL;
    jnx_flow_svc_set_list_t    * svc_set_list = NULL,
                               * next_svc_set_list = NULL;
    jnx_flow_rule_name_list_t  * rule_name_list = NULL,
                               * next_rule_name_list = NULL;

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG,"free candidate config");

    /*
     * for config check case, config-read fail case
     * clear all the list entries, & add entries
     */

    if (flow_cfg->check) {

        for (rule_list = flow_cfg->rule_list; 
             (rule_list != NULL); rule_list = next_rule_list) {
            next_rule_list = rule_list->next;
            /* 
             * free the rule node
             */
            if (rule_list->op_type == JNX_FLOW_MSG_CONFIG_ADD) {
                jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "free rule list entry <\"%s\", %d>", 
                             rule_list->rule_node->rule.rule_name,
                             rule_list->rule_node->rule_id);
                free(rule_list->rule_node);
            }
            free(rule_list);
        }

        flow_cfg->rule_list = NULL; 

        for (svc_set_list = flow_cfg->svc_set_list; (svc_set_list != NULL);
             svc_set_list = next_svc_set_list) {
            next_svc_set_list = svc_set_list->next;

            /*
             * free all the rule entries in the service set
             */
            for (rule_name_list = svc_set_list->rule_list; 
                 (rule_name_list != NULL);
                 rule_name_list = next_rule_name_list) {
                jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "free service set <\"%s\", %d>"
                             " rule list entry <\"%s\", %d>", 
                             svc_set_list->svc_set_node->svc_set.svc_set_name,
                             svc_set_list->svc_set_node->svc_set_id,
                             rule_name_list->rule_node->rule.rule_name,
                             rule_name_list->rule_node->rule_id);
                next_rule_name_list = rule_name_list->next;
                free(rule_name_list);
            }

            svc_set_list->rule_list = NULL; 

            /* 
             * free the service set node
             */
            if (svc_set_list->op_type == JNX_FLOW_MSG_CONFIG_ADD) {
                jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "free svc-set list entry <\"%s\", %d>", 
                             svc_set_list->svc_set_node->svc_set.svc_set_name,
                             svc_set_list->svc_set_node->svc_set_id);
                free(svc_set_list->svc_set_node);
                svc_set_list->svc_set_node = NULL;
            }

            free(svc_set_list);
        }
        flow_cfg->svc_set_list = NULL; 
        return;
    }

    /* 
     * Now check if any delete entries are there, delete them
     * free the list entries for every thing
     */

    /*
     * first process the service set
     */
    for (svc_set_list = flow_cfg->svc_set_list; svc_set_list != NULL;
         svc_set_list = next_svc_set_list) {
        next_svc_set_list = svc_set_list->next;

        /*
         * free all the rule entries in the service set
         */
        for (rule_name_list = svc_set_list->rule_list; 
             (rule_name_list != NULL); rule_name_list = next_rule_name_list) {

            next_rule_name_list = rule_name_list->next;
            jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "free service set <\"%s\", %d>"
                         " rule list entry <\"%s\", %d>", 
                         svc_set_list->svc_set_node->svc_set.svc_set_name,
                         svc_set_list->svc_set_node->svc_set_id,
                         rule_name_list->rule_node->rule.rule_name,
                         rule_name_list->rule_node->rule_id);
            free(rule_name_list);
        }

        svc_set_list->rule_list = NULL; 

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "free service set <\"%s\", %d>",
                     svc_set_list->svc_set_node->svc_set.svc_set_name,
                     svc_set_list->svc_set_node->svc_set_id);
        /* 
         * free the deleted service set node
         */
        if (svc_set_list->op_type == JNX_FLOW_MSG_CONFIG_DELETE) {
            jnx_flow_mgmt_delete_svc_set(svc_set_list->svc_set_node);
        }

        free(svc_set_list);
    }

    flow_cfg->svc_set_list = NULL; 

    /*
     * then process the rule set
     */

    for (rule_list = flow_cfg->rule_list; 
         (rule_list != NULL); rule_list = next_rule_list) {
        next_rule_list = rule_list->next;

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_CONFIG, "free rule list entry <\"%s\", %d>", 
                     rule_list->rule_node->rule.rule_name,
                     rule_list->rule_node->rule_id);
        /* 
         * free the deleted rule node
         */
        if (rule_list->op_type == JNX_FLOW_MSG_CONFIG_DELETE) {
            jnx_flow_mgmt_delete_rule(rule_list->rule_node);
        }
        free(rule_list);
    }

    flow_cfg->rule_list = NULL; 

    return;
}

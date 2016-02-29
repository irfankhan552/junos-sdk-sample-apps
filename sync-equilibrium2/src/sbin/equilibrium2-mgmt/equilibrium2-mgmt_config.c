/*
 * $Id: equilibrium2-mgmt_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium2-mgmt_config.c
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions will parse and load the configuration data.
 */

#include <sync/equilibrium2.h>
#include "equilibrium2-mgmt.h"

#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>
#include <ddl/ddl.h>
#include <ddl/dax.h>

#include EQUILIBRIUM2_OUT_H

/*** Constants ***/

#define DDLNAME_SVC_SET_NAME    "service-set-name"
                                    /**< service-set name's attribute */
#define DDLNAME_EXT_SVC         "extension-service"
                                    /**< extension-service object */
#define DDLNAME_EXT_SVC_NAME    "service-name"
                                    /**< extension-service name's attribute */
#define DDLNAME_IF_SVC          "interface-service"
                                    /**< interface-service object */
#define DDLNAME_IF_SVC_IF_NAME  "service-interface"
                                    /**< service-interface attribute */

/*** Data Structures ***/

static svc_gate_head_t  svc_gate_head;   /**< list head of service gates */
static svc_type_head_t  svc_type_head;   /**< list head of service types */
static svr_group_head_t svr_group_head;  /**< list head of server groups */
static int              svr_group_count; /**< number of server groups */
static svc_set_head_t   ss_head;         /**< list head of service sets */
static ssrb_head_t      ssrb_head;       /**< list head of SSRB */
static blob_ss_head_t   blob_ss_head;    /**< list head of service-set blob */
static svc_rule_head_t       balance_rule_head;
                                 /**< list head of balance service rules */
static svc_rule_head_t       classify_rule_head;
                                 /**< list head of classify service rules */
static blob_svr_group_set_t  *blob_svr_group_set;
                                 /**< pointer to server group set */

/*** STATIC/INTERNAL Functions ***/

/**
 * @brief
 * Clear the list of service interfaces.
 */
static void
clear_svc_if (void)
{
    svc_if_t *svc_if;

    while ((svc_if = LIST_FIRST(&svc_if_head))) {
        LIST_REMOVE(svc_if, entry);
        free(svc_if);
    }
    svc_if_count = 0;
}

/**
 * @brief
 * Add a service interface.
 *
 * @param[in] name
 *      Service interface name
 */
static void
add_svc_if (char *name)
{
    svc_if_t *svc_if;

    /* If interface name exists, return. */
    LIST_FOREACH(svc_if, &svc_if_head, entry) {
        if (strcmp(svc_if->if_name, name) == 0) {
            return;
        }
    }
    svc_if = malloc(sizeof(svc_if_t));
    INSIST_ERR(svc_if != NULL);
    svc_if->if_name = name;
    LIST_INSERT_HEAD(&svc_if_head, svc_if, entry);
    svc_if_count++;
}

/**
 * @brief
 * Get a service rule by name.
 *
 * @param[in] head
 *      Pointer to the head of service rules
 *
 * @param[in] name
 *      The name of service rule
 *
 * @return
 *      The pointer to service rule on success, NULL on failure
 */
static svc_rule_t *
get_eq2_svc_rule (svc_rule_head_t *head, char *name)
{
    svc_rule_t *rule;

    if ((head == NULL) || (name == NULL)) {
        return NULL;
    }
    LIST_FOREACH(rule, head, entry) {
        if (strcmp(rule->rule_name, name) == 0) {
            break;
        }
    }
    return rule;
}

/**
 * @brief
 * Get a service-set blob by name and service ID.
 *
 * @param[in] name
 *      Service-set name
 *
 * @param[in] eq2_svc
 *      Equilibrium II service ID
 *
 * @return
 *      The pointer to service-set blob on success, NULL on failure
 */
static blob_svc_set_node_t *
get_svc_set_blob_node (char *name, int eq2_svc)
{
    blob_svc_set_node_t *blob_ss_node;

    LIST_FOREACH(blob_ss_node, &blob_ss_head, entry) {
        if ((strcmp(blob_ss_node->ss->ss_name, name) == 0) &&
                (blob_ss_node->ss->ss_eq2_svc_id == eq2_svc)) {
            break;
        }
    }
    return blob_ss_node;
}

/**
 * @brief
 * Free a service rule and all attached terms.
 *
 * @param[in] rule
 *      Pointer to the rule to free
 */
static void
free_eq2_svc_rule (svc_rule_t *rule)
{
    svc_term_t *term;

    if (rule == NULL) {
	return;
    }

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s %s", __func__, rule->rule_name);  

    /* Clear all terms in this rule. */
    while ((term = LIST_FIRST(&rule->rule_term_head))) {
	LIST_REMOVE(term, entry);
	free(term);
    }
    free(rule);
}

/**
 * @brief
 * Delete a service rule and all attached terms by pointer.
 *
 * The rule will first be removed from the service rule list that it belongs
 * to and then be freed.
 *
 * @param[in] rule
 *       Pointer to the rule to be deleted
 */
static void
del_eq2_svc_rule (svc_rule_t *rule)
{
    if (rule == NULL) {
        return;
    }

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s %s", __func__, rule->rule_name);

    /* Remove the service rule from the list it belongs to */
    LIST_REMOVE(rule, entry);

    /* Free the service rule */
    free_eq2_svc_rule(rule);
}

/**
 * @brief
 * Clear service rules.
 *
 * @param[in] head
 *      Pointer to the head of service rules
 */
static void
clear_eq2_svc_rule (svc_rule_head_t *head)
{
    svc_rule_t *rule;

    if (head == NULL) {
        return;
    }
    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s", __func__);

    /* Remove each service rule in the list and free it. */
    while ((rule = LIST_FIRST(head))) {
        LIST_REMOVE(rule, entry);
        free_eq2_svc_rule(rule);
    }
}

/**
 * @brief
 * Delete a service set by pointer.
 *
 * @param[in] ss
 *      Pointer to service set.
 */
static void
del_svc_set (svc_set_t *ss)
{
    svc_set_rule_t *rule;

    if (ss == NULL) {
        return;
    }

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s %s", __func__, ss->ss_name);

    LIST_REMOVE(ss, entry);

    /* Clear all rules in this service set. */
    while ((rule = LIST_FIRST(&ss->ss_rule_head))) {
        LIST_REMOVE(rule, entry);
        free(rule);
    }
    free(ss);
}

/**
 * @brief
 * Delete all service sets.
 */
static void
clear_svc_set (void)
{
    svc_set_t *ss;

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s", __func__);

    while ((ss = LIST_FIRST(&ss_head))) {
        del_svc_set(ss);
    }
}

/**
 * @brief
 * Get an SSRB from the configuration.
 * 
 * @param[in] name
 *      Service-set name
 * 
 * @return
 *      Pointer to the SSRB data on success, NULL on failure
 */
static ssrb_node_t *
config_get_ssrb (char *name)
{
    ssrb_node_t *ssrb;

    if (name == NULL) {
        return NULL;
    }
    LIST_FOREACH(ssrb, &ssrb_head, entry) {
        if (strcmp(ssrb->ssrb.svc_set_name, name) == 0) {
            break;
        }
    }
    return ssrb;
}

/**
 * @brief
 * Read service rule under service set.
 *
 * @param[in] dop
 *      DAX Object Pointer to external service
 *
 * @param[in] head
 *      Pointer to the head of service-set rules
 *
 * @return
 *      number of rules on success, -1 on failure
 */
static int
read_svc_set_rule (ddl_handle_t *dop, svc_set_rule_head_t *head)
{
    const char *rule_config[] = { "rule", NULL };
    ddl_handle_t *dop_rule_head = NULL;
    ddl_handle_t *dop_rule = NULL;
    char rule_name[MAX_NAME_LEN];
    svc_set_rule_t *rule;
    int count = 0;

    if (!dax_get_object_by_path(dop, rule_config, &dop_rule_head, FALSE)) {
        dax_error(dop, "No rules configured!");
        dax_release_object(&dop_rule_head);
        return -1;
    }
    while (dax_visit_container(dop_rule_head, &dop_rule)) {
        if (!dax_get_stringr_by_name(dop_rule, "name", rule_name,
                sizeof(rule_name))) {
            continue;
        }
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Read rule %s.", __func__, rule_name);
        rule = malloc(sizeof(svc_set_rule_t));
        INSIST_ERR(rule != NULL);
        strlcpy(rule->rule_name, rule_name, sizeof(rule->rule_name));
        LIST_INSERT_HEAD(head, rule, entry);
        count++;
    }
    dax_release_object(&dop_rule_head);
    return count;
}

/**
 * @brief
 * Read service-set configuration.
 * 
 * @param[in] dop_ss
 *      DDL handle for a service-set
 *
 * @return
 *      0 on success, -1 on failure
 */
static int 
read_svc_set (ddl_handle_t *dop_ss)
{
    const char *svc_if_config[] = { DDLNAME_IF_SVC, NULL };
    const char *ext_svc_config[] = { DDLNAME_EXT_SVC, NULL };
    ddl_handle_t *dop_svc_if = NULL;
    ddl_handle_t *dop_ext_svc_head = NULL;
    ddl_handle_t *dop_ext_svc = NULL;
    char svc_if_name[MAX_NAME_LEN];
    char ext_svc_name[MAX_NAME_LEN];
    char ss_name[MAX_NAME_LEN];
    svc_set_t *ss;
    svc_set_rule_head_t balance_rl_head;
    svc_set_rule_head_t classify_rl_head;
    int balance_rule_count = 0;
    int classify_rule_count = 0;
    ssrb_node_t *ssrb_node;

    /* Read service-set name. */
    if (!dax_get_stringr_by_name(dop_ss, DDLNAME_SVC_SET_NAME, ss_name,
            sizeof(ss_name))) {
        dax_error(dop_ss, "Parse a service set name ERROR!");
        return -1;
    }

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Reading %s.", __func__, ss_name);

    /* Get the head object of extension-service list. */
    if (!dax_get_object_by_path(dop_ss, ext_svc_config, &dop_ext_svc_head,
            FALSE)) {
        /* No external service configured. */
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: No external service.", __func__);
        return -1;
    }

    /* Read interface-service. */
    if (!dax_get_object_by_path(dop_ss, svc_if_config, &dop_svc_if, FALSE)) {
        dax_error(dop_ss, "Service interface is not configured!");
        return -1;
    }

    /* Read service-interface. */
    if (!dax_get_stringr_by_name(dop_svc_if, DDLNAME_IF_SVC_IF_NAME,
            svc_if_name, sizeof(svc_if_name))) {
        dax_error(dop_svc_if, "Read service-interface name ERROR!");
        dax_release_object(&dop_svc_if);
        return -1;
    }
    dax_release_object(&dop_svc_if);

    LIST_INIT(&balance_rl_head);
    balance_rule_count = 0;
    LIST_INIT(&classify_rl_head);
    classify_rule_count = 0;
    while (dax_visit_container(dop_ext_svc_head, &dop_ext_svc)) {
        if (!dax_get_stringr_by_name(dop_ext_svc, DDLNAME_EXT_SVC_NAME,
                ext_svc_name, sizeof(ext_svc_name))) {
            dax_error(dop_ext_svc, "Read external service name ERROR!");
            dax_release_object(&dop_ext_svc);
            dax_release_object(&dop_ext_svc_head);
            return -1;
        }

        if (strcmp(ext_svc_name, EQ2_BALANCE_SVC_NAME) == 0) {
            EQ2_TRACE(EQ2_TRACEFLAG_CONF,
                        "%s: Equilibrium2 balance service.", __func__);
            balance_rule_count = read_svc_set_rule(dop_ext_svc,
                        &balance_rl_head);
            if (balance_rule_count <= 0) {

                /* Read rules error or no rules. */
                dax_error(dop_ext_svc, "Read balance rules ERROR!");
                dax_release_object(&dop_ext_svc);
                dax_release_object(&dop_ext_svc_head);
                return -1;
            }
        }
        if (strcmp(ext_svc_name, EQ2_CLASSIFY_SVC_NAME) == 0) {
            EQ2_TRACE(EQ2_TRACEFLAG_CONF,
                    "%s: Equilibrium2 classify service.", __func__);
            classify_rule_count = read_svc_set_rule(dop_ext_svc,
                    &classify_rl_head);
            if (classify_rule_count <= 0) {

                /* Read rules error or no rules. */
                dax_error(dop_ext_svc, "Read classify rules ERROR!");
                dax_release_object(&dop_ext_svc);
                dax_release_object(&dop_ext_svc_head);
                return -1;
            }
        }
    }
    dax_release_object(&dop_ext_svc_head);

    if (balance_rule_count > 0) {
        ss = calloc(1, sizeof(svc_set_t));
        INSIST_ERR(ss != NULL);
        strlcpy(ss->ss_name, ss_name, sizeof(ss->ss_name));
        strlcpy(ss->ss_if_name, svc_if_name, sizeof(ss->ss_if_name));
        ss->ss_eq2_svc_id = EQ2_BALANCE_SVC;

        LIST_INSERT_HEAD(&ss->ss_rule_head, LIST_FIRST(&balance_rl_head),
            entry);
        ss->ss_rule_count = balance_rule_count;
        LIST_INSERT_HEAD(&ss_head, ss, entry);
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Read %d balance rules.",
                __func__, balance_rule_count);
    }
    if (classify_rule_count > 0) {
        ss = calloc(1, sizeof(svc_set_t));
        INSIST_ERR(ss != NULL);
        strlcpy(ss->ss_name, ss_name, sizeof(ss->ss_name));
        strlcpy(ss->ss_if_name, svc_if_name, sizeof(ss->ss_if_name));
        ss->ss_eq2_svc_id = EQ2_CLASSIFY_SVC;

        LIST_INSERT_HEAD(&ss->ss_rule_head, LIST_FIRST(&classify_rl_head),
                entry);
        ss->ss_rule_count = classify_rule_count;
        LIST_INSERT_HEAD(&ss_head, ss, entry);
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Read %d classify rules.",
                __func__, classify_rule_count);
    }

    /* If this service-set is changed and it contains Equilibrium II service,
     * the associated SSRB is expected to be updated as well.
     * If the SSRB state is 'updated', meaning SSRB update was received before
     * reading config, then do nothing.
     * Otherwise, it means SSRB update was not received yet and is expected
     * to be received after reading config, then mark the SSRB 'pending'.
     */
    if (dax_is_changed(dop_ss) && (balance_rule_count > 0 ||
            classify_rule_count >0)) {
        ssrb_node = config_get_ssrb(ss_name);
        if (ssrb_node && (ssrb_node->ssrb_state != SSRB_UPDATED)) {
            ssrb_node->ssrb_state = SSRB_PENDING;
        }
    }

    return 0;
}

/**
 * @brief
 * Add a service gate to the list.
 *
 * @param[in] name
 *      Pointer to the service gate name string
 *
 * @param[in] addr
 *      Pointer to the service gate address string
 */
static void
add_eq2_svc_gate (char *name, char *addr)
{
    svc_gate_t *gate;

    if ((name == NULL) || (addr == NULL)) {
        return;
    }
    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s %s: %s", __func__, name, addr);

    gate = malloc(sizeof(svc_gate_t));
    INSIST_ERR(gate != NULL);

    strlcpy(gate->gate_name, name, sizeof(gate->gate_name));
    gate->gate_addr = inet_addr(addr);

    LIST_INSERT_HEAD(&svc_gate_head, gate, entry);
}

/**
 * @brief
 * Get gate address by service gate name.
 *
 * @param[in] name
 *      Pointer to the service gate name string
 *
 * @return
 *      valid gate address on success, 0 on failure
 */
static in_addr_t
get_eq2_svc_gate (char *name)
{
    svc_gate_t *gate;

    if (name == NULL) {
        return 0;
    }
    LIST_FOREACH(gate, &svc_gate_head, entry) {
        if (strcmp(gate->gate_name, name) == 0) {
            break;
        }
    }
    return (gate ? gate->gate_addr : INADDR_ANY);
}

/**
 * @brief
 * Add a service type to the list.
 *
 * @param[in] name
 *      Pointer to the service type name string
 *
 * @param[in] port
 *      Port number
 */
static void
add_eq2_svc_type (char *name, uint16_t port)
{
    svc_type_t *type;

    if ((name == NULL) || (port == 0)) {
        return;
    }
    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s %s: %d", __func__, name, port);

    type = malloc(sizeof(svc_type_t));
    INSIST_ERR(type != NULL);

    strlcpy(type->type_name, name, sizeof(type->type_name));
    type->type_port = port;

    LIST_INSERT_HEAD(&svc_type_head, type, entry);
}

/**
 * @brief
 * Get port number by service type name.
 *
 * @param[in] name
 *      Pointer to the service type name string
 *
 * @return
 *      valid port number on success, 0 on failure
 */
static uint16_t
get_eq2_svc_type (char *name)
{
    svc_type_t *type;

    if (name == NULL) {
        return 0;
    }
    LIST_FOREACH(type, &svc_type_head, entry) {
        if (strcmp(type->type_name, name) == 0) {
            break;
        }
    }
    return (type ? type->type_port : 0);
}

/**
 * @brief
 * Add a server group to the list.
 *
 * @param[in] name
 *      Pointer to the server group name string
 *
 * @param[in] head
 *      Pointer to the head of address list
 *
 * @param[in] addr_count
 *      Number of addresses in this group
 */
static void
add_eq2_svr_group (char *name, svr_addr_head_t *head, int addr_count)
{
    svr_group_t *group;
    svr_addr_t *addr;

    if ((name == NULL) || (head == NULL)) {
        return;
    }
    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: %s", __func__, name);

    group = calloc(1, sizeof(svr_group_t));
    INSIST_ERR(group != NULL);

    LIST_INIT(&group->group_addr_head);
    strlcpy(group->group_name, name, sizeof(group->group_name));

    /* Move addresses from temp list to group address list. */
    while ((addr = LIST_FIRST(head))) {
        LIST_REMOVE(addr, entry);
        LIST_INSERT_HEAD(&group->group_addr_head, addr, entry);
    }
    group->group_addr_count = addr_count;

    /* Add group to the list. */
    LIST_INSERT_HEAD(&svr_group_head, group, entry);
    svr_group_count++;
}

/**
 * @brief
 * Clear the list of service gate.
 */
static void
clear_eq2_svc_gate (void)
{
    svc_gate_t *gate;

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s", __func__);

    while ((gate = LIST_FIRST(&svc_gate_head))) {
        LIST_REMOVE(gate, entry);
        free(gate);
    }
}

/**
 * @brief
 * Clear the list of service type.
 */
static void
clear_eq2_svc_type (void)
{
    svc_type_t *type;

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s", __func__);
    while ((type = LIST_FIRST(&svc_type_head)) != NULL) {
        LIST_REMOVE(type, entry);
        free(type);
    }
}

/**
 * @brief
 * Clear the list of server group.
 */
static void
clear_eq2_svr_group (void)
{ 
    svr_group_t *group;
    svr_addr_t *addr;

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s", __func__);
    while ((group = LIST_FIRST(&svr_group_head))) {
        LIST_REMOVE(group, entry);
        while ((addr = LIST_FIRST(&group->group_addr_head))) {
            LIST_REMOVE(addr, entry);
            free(addr);
        }
        free(group);
    } 
    svr_group_count = 0;
}

/**
 * @brief
 * Read service gates configuration.
 */
static void
read_eq2_svc_gate (void)
{
    ddl_handle_t *cop = NULL;
    ddl_handle_t *dop = NULL;
    const char *svc_gate_config[] = { "sync", "equilibrium2", "service-gate",
            NULL };
    char name_str[MAX_NAME_LEN];
    char addr_str[MAX_NAME_LEN];

    if (!dax_get_object_by_path(NULL, svc_gate_config, &cop, FALSE)) {
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: No service-gate.", __func__);
        clear_eq2_svc_gate();
        return;
    }
    if (!dax_is_changed(cop)) {
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: No change.", __func__);
        goto exit_success;
    }

    /* CLI config is changed, clear local config and read new config. */
    clear_eq2_svc_gate();
    while (dax_visit_container(cop, &dop)) {
        if (!dax_get_stringr_by_name(dop, "name", name_str, sizeof(name_str))) {
            dax_error(dop, "Read name ERROR!");
            dax_release_object(&dop);
            break;
        }
        if (!dax_get_stringr_by_name(dop, "address", addr_str,
                sizeof(addr_str))) {
            dax_error(cop, "Read address ERROR!");
            dax_release_object(&dop);
            break;
        }
        add_eq2_svc_gate(name_str, addr_str);
    }

exit_success:
    dax_release_object(&cop);
}

/**
 * @brief
 * Read service type configuration.
 */
static void
read_eq2_svc_type (void)
{
    ddl_handle_t *cop = NULL;
    ddl_handle_t *dop = NULL;
    const char *svc_type_config[] = { "sync", "equilibrium2", "service-type",
            NULL };
    char name_str[MAX_NAME_LEN];
    in_port_t port;

    if (!dax_get_object_by_path(NULL, svc_type_config, &cop, FALSE)) {
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: No service-type.", __func__);
        clear_eq2_svc_type();
        return;
    }
    if (!dax_is_changed(cop)) {
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: No change.", __func__);
        goto exit_success;
    }

    /* CLI config is changed, clear local config and read new config. */
    clear_eq2_svc_type();
    while (dax_visit_container(cop, &dop)) {
        if (!dax_get_stringr_by_name(dop, "name", name_str, sizeof(name_str))) {
            dax_error(dop, "Read name ERROR!");
            dax_release_object(&dop);
            break;
        }
        if (!dax_get_ushort_by_name(dop, "port", &port)) {
            dax_error(dop, "Read port ERROR!");
            dax_release_object(&dop);
            break;
        }
        add_eq2_svc_type(name_str, port);
    }

exit_success:
    dax_release_object(&cop);
}

/**
 * @brief
 * Read server groups configuration.
 */
static void
read_eq2_svr_group (void)
{
    ddl_handle_t *cop_group = NULL;
    ddl_handle_t *dop_group = NULL;
    ddl_handle_t *cop_addr = NULL;
    ddl_handle_t *dop_addr = NULL;
    const char *svr_group_config[] = { "sync", "equilibrium2", "server-group",
            NULL };
    const char *svr_config[] = { "servers", NULL };
    char name_str[MAX_NAME_LEN];
    char addr_str[MAX_NAME_LEN];
    svr_addr_head_t addr_head;
    svr_addr_t *addr;
    int addr_count = 0;

    if (!dax_get_object_by_path(NULL, svr_group_config, &cop_group, FALSE)) {
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: No server-group.", __func__);
        clear_eq2_svr_group();
        return;
    }
    if (!dax_is_changed(cop_group)) {
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: No change.", __func__);
        goto exit_success;
    }

    /* CLI config is changed, clear local config and read new config. */
    clear_eq2_svr_group();
    LIST_INIT(&addr_head);
    while (dax_visit_container(cop_group, &dop_group)) {
        if (!dax_get_stringr_by_name(dop_group, "name", name_str,
                sizeof(name_str))) {
            dax_error(dop_group, "Read name ERROR!");
            dax_release_object(&dop_group);
            goto exit_success;
        }
        if (!dax_get_object_by_path(dop_group, svr_config, &cop_addr, FALSE)) {
            dax_error(cop_addr, "Read address list ERROR!");
            dax_release_object(&dop_group);
            goto exit_success;
        }
        addr_count = 0;
        while (dax_visit_container(cop_addr, &dop_addr)) {
            if (!dax_get_stringr_by_name(dop_addr, "address", addr_str,
                    sizeof(addr_str))) {
                dax_error(dop_addr, "Read address list ERROR!");
                dax_release_object(&dop_addr);
                dax_release_object(&cop_addr);
                goto exit_success;
            }
            addr = calloc(1, sizeof(svr_addr_t));
            INSIST_ERR(addr != NULL);
            addr->addr = inet_addr(addr_str);
            LIST_INSERT_HEAD(&addr_head, addr, entry);
            addr_count++;
            EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s %s: %s", __func__,
                    name_str, addr_str);
        }
        add_eq2_svr_group(name_str, &addr_head, addr_count);
    }

exit_success:
    dax_release_object(&cop_group);
}

/**
 * @brief
 * Read term configuration in the rule.
 * 
 * @param[in] top
 *      DAX Object Pointer for rule object
 * 
 * @param[in] rule
 *      Pointer to the rule
 * 
 * @return
 *      0 on success, -1 on failure
 */
static int 
read_eq2_term (ddl_handle_t *top, svc_rule_t *rule)
{
    const char *term_config[] = { "term", NULL };
    const char *from_config[] = { "from", NULL };
    const char *then_config[] = { "then", NULL };
    ddl_handle_t *cop_term = NULL;
    ddl_handle_t *dop_term = NULL;
    ddl_handle_t *dop_from = NULL;
    ddl_handle_t *dop_then = NULL;
    char term_name[MAX_NAME_LEN];
    char buf[MAX_NAME_LEN];
    char tog;
    svc_term_t *term;

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s", __func__);

    if (!dax_get_object_by_path(top, term_config, &cop_term, FALSE)) {
        dax_error(top, "Read term ERROR.");
        return -1;
    }

    while (dax_visit_container(cop_term, &dop_term)) {
        if (!dax_get_stringr_by_name(dop_term, "name", term_name,
                sizeof(term_name))) {
            dax_error(dop_term, "Read term name ERROR!");
            dax_release_object(&dop_term);
            dax_release_object(&cop_term);
            return -1;
        }

        term = calloc(1, sizeof(svc_term_t));
        INSIST_ERR(term != NULL);
        strlcpy(term->term_name, term_name, sizeof(term->term_name));
        LIST_INSERT_HEAD(&rule->rule_term_head, term, entry);
        rule->rule_term_count++;

        /* Read term match condition. */
        if (dax_get_object_by_path(dop_term, from_config, &dop_from, FALSE)) {
            if (dax_get_stringr_by_name(dop_from, "service-type", buf,
                    sizeof(buf))) {
                term->term_match = TERM_FROM_SVC_TYPE;
                strlcpy(term->term_match_val, buf,
                        sizeof(term->term_match_val));
            }
            if (dax_get_stringr_by_name(dop_from, "service-gate", buf,
                    sizeof(buf))) {
                term->term_match = TERM_FROM_SVC_GATE;
                strlcpy(term->term_match_val, buf,
                        sizeof(term->term_match_val));
            }
            if (dax_get_toggle_by_name(dop_from, "except", &tog)) {
                term->term_match |= TERM_FROM_EXCEPT;
            }
            dax_release_object(&dop_from);
        }

        /* Read term action. */
        if (dax_get_object_by_path(dop_term, then_config, &dop_then, FALSE)) {
            if (dax_get_toggle_by_name(dop_then, "accept", &tog)) {
                term->term_act = TERM_THEN_ACCEPT;
            } else if (dax_get_toggle_by_name(dop_then, "discard", &tog)) {
                term->term_act = TERM_THEN_DISCARD;
            } else if (dax_get_stringr_by_name(dop_then, "server-group", buf,
                    sizeof(buf))) {
                term->term_act = TERM_THEN_SVR_GROUP;
                strlcpy(term->term_act_val, buf, sizeof(term->term_act_val));
            } else if (dax_get_stringr_by_name(dop_then, "service-gate", buf,
                    sizeof(buf))) {
                term->term_act = TERM_THEN_SVC_GATE;
                strlcpy(term->term_act_val, buf, sizeof(term->term_act_val));
            }
            dax_release_object(&dop_then);
        }
    }
    dax_release_object(&cop_term);
    return 0;
}

/**
 * @brief
 * Handler for dax_walk_list to read service rule.
 * 
 * @param[in] dwd
 *      Opaque dax data
 * 
 * @param[in] dop
 *      DAX Object Pointer for rule object
 * 
 * @param[in] action
 *      The action on the given rule object
 * 
 * @param[in] data
 *      User data passed to handler
 * 
 * @return
 *      DAX_WALK_OK on success, DAX_WALK_ABORT on failure
 */
static int 
read_eq2_rule (dax_walk_data_t *dwd, ddl_handle_t *dop, int action, void *data)
{
    char rule_name[MAX_NAME_LEN];
    svc_rule_head_t *rule_head = data;
    svc_rule_t *rule;

    switch (action) {
    case DAX_ITEM_DELETE_ALL: /* All items was deleted. */
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Delete all rules.", __func__);
        clear_eq2_svc_rule(rule_head);
        return DAX_WALK_OK;
        break;

    case DAX_ITEM_DELETE: /* One item was deleted. */
        if (!dax_get_stringr_by_dwd_ident(dwd, NULL, 0, rule_name,
                sizeof(rule_name))) {
            dax_error(dop, "Read rule name ERROR!");
            return DAX_WALK_ABORT;
        }
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Delete rule %s.", __func__,
                rule_name);
        del_eq2_svc_rule(get_eq2_svc_rule(rule_head, rule_name));
        break;

    case DAX_ITEM_CHANGED:
        INSIST_ERR(dop != NULL);
        if (!dax_get_stringr_by_name(dop, "name", rule_name,
                sizeof(rule_name))) {
            dax_error(dop, "Read rule name ERROR!");
            return DAX_WALK_ABORT;
        }
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Change rule %s.", __func__,
                rule_name);

        /* Clear the whole rule and reread it if anything is changed in it. */
        del_eq2_svc_rule(get_eq2_svc_rule(rule_head, rule_name));

        /* Create a new rule. */
        rule = calloc(1, sizeof(svc_rule_t));
        INSIST_ERR(rule != NULL);
        strlcpy(rule->rule_name, rule_name, sizeof(rule->rule_name));
        LIST_INSERT_HEAD(rule_head, rule, entry);

        if (read_eq2_term(dop, rule) < 0) {
            dax_error(NULL, "Read term ERROR!");
            return DAX_WALK_ABORT;
        }
        break;

    case DAX_ITEM_UNCHANGED:
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: No change.", __func__);
        break;

    default:
        break;
    }

    return DAX_WALK_OK;
}

/**
 * @brief
 * Generate blob checksum.
 *
 * @param[in] buf
 *      pointer to the buffer
 *
 * @param[in] len
 *      number of bytes
 *
 * @return
 *      checksum of the buffer
 */
static uint16_t
blob_cksum (void *buf, int len)
{
    uint8_t *p = buf;
    uint32_t sum = 0;

    while (len > 1) {
        sum += (*p << 8) + *(p + 1);
        len -= 2;
        p += 2;
    }
    if (len == 1) {
        sum += (*p << 8);
    }

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    sum = ~sum;

    return (uint16_t)sum;
}

/**
 * @brief
 * Pack server group blob.
 */
static void
pack_svr_group_blob (void)
{
    svr_group_t *group;
    blob_svr_group_t *blob_svr_group;
    in_addr_t *blob_addr;
    svr_addr_t *addr;
    int addr_count = 0;
    int blob_size = 0;

    /* Go through server group list to get total number of addresses. */
    LIST_FOREACH(group, &svr_group_head, entry) {
        addr_count += group->group_addr_count;
    }
    blob_size = sizeof(blob_svr_group_set_t) +
            svr_group_count * sizeof(blob_svr_group_t) +
            addr_count * sizeof(in_addr_t);
    blob_svr_group_set = calloc(1, blob_size);
    INSIST_ERR(blob_svr_group_set != NULL);

    /* Fill blob_svr_group_set. */
    blob_svr_group_set->gs_cksum = 0;
    blob_svr_group_set->gs_size = htons(blob_size);
    blob_svr_group_set->gs_count = htons(svr_group_count);

    /* Fill blob_svr_group. */
    blob_svr_group = blob_svr_group_set->gs_group;
    LIST_FOREACH(group, &svr_group_head, entry) {
        strlcpy(blob_svr_group->group_name, group->group_name,
                sizeof(blob_svr_group->group_name));
        blob_svr_group->group_addr_count = htons(group->group_addr_count);
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Group %s with %d addresses.",
            __func__, group->group_name, group->group_addr_count);

        /* Fill address list. */
        blob_addr = blob_svr_group->group_addr;
        LIST_FOREACH(addr, &group->group_addr_head, entry) {
            *blob_addr = addr->addr;
            EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s:      0x%08x", __func__,
                    addr->addr);
            blob_addr++;
        }
        blob_svr_group = (blob_svr_group_t *)blob_addr;
    }
    /* Not change byte order for checksum, cause it's only for RE. */
    blob_svr_group_set->gs_cksum = blob_cksum(blob_svr_group_set, blob_size);

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: size %d, count %d.", __func__,
            blob_size, svr_group_count);
}

/**
 * @brief
 * Pack service-set blob.
 *
 * @param[in] ss
 *      Pointer to the service-set
 *
 * @return
 *      Pointer to the service-set blob on success, NULL on failure
 */
static blob_svc_set_t *
pack_svc_set_blob (svc_set_t *ss)
{
    svc_rule_head_t *svc_rule_head;
    svc_set_rule_t *ss_rule;
    svc_rule_t *rule;
    svc_term_t *term;
    int rule_count;
    int term_count;
    blob_svc_set_t *blob_ss = NULL;
    blob_rule_t *blob_rule;
    blob_term_t *blob_term;
    int blob_size = 0;

    switch (ss->ss_eq2_svc_id) {
    case EQ2_BALANCE_SVC:
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: %s for balance service.", __func__,
                ss->ss_name);
        svc_rule_head = &balance_rule_head;
        break;
    case EQ2_CLASSIFY_SVC:
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: %s for classify service.", __func__,
                ss->ss_name);
        svc_rule_head = &classify_rule_head;
        break;
    default:
        EQ2_LOG(TRACE_LOG_ERR, "%s: Unknown service %d!",
                __func__, ss->ss_eq2_svc_id);
        return NULL;
    }

    /* Go through all rules to get the total number of terms. */
    term_count = 0;
    LIST_FOREACH(ss_rule, &ss->ss_rule_head, entry) {
        rule = get_eq2_svc_rule(svc_rule_head, ss_rule->rule_name);
        INSIST_ERR(rule != NULL);
        term_count += rule->rule_term_count;
    }
    rule_count = ss->ss_rule_count;
    blob_size = sizeof(blob_svc_set_t) + rule_count * sizeof(blob_rule_t) +
            term_count * sizeof(blob_term_t);
    blob_ss = calloc(1, blob_size);
    INSIST_ERR(blob_ss != NULL);

    /* Fill up service-set. */
    strlcpy(blob_ss->ss_name, ss->ss_name, sizeof(blob_ss->ss_name));
    strlcpy(blob_ss->ss_if_name, ss->ss_if_name, sizeof(blob_ss->ss_if_name));
    blob_ss->ss_size = htons(blob_size);
    blob_ss->ss_id = htons(ss->ss_id);
    blob_ss->ss_eq2_svc_id = ss->ss_eq2_svc_id;
    blob_ss->ss_rule_count = htons(rule_count);

    /* Fill up rules. */
    blob_rule = blob_ss->ss_rule;
    LIST_FOREACH(ss_rule, &ss->ss_rule_head, entry) {
        rule = get_eq2_svc_rule(svc_rule_head, ss_rule->rule_name);
        INSIST_ERR(rule != NULL);
        strlcpy(blob_rule->rule_name, ss_rule->rule_name,
                sizeof(blob_rule->rule_name));
        blob_rule->rule_term_count = htons(rule->rule_term_count);

        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Rule %s with %d terms.",
                __func__, ss_rule->rule_name, rule->rule_term_count);

        /* Fill up terms. */
        blob_term = blob_rule->rule_term;
        LIST_FOREACH(term, &rule->rule_term_head, entry) {

            /* Term name. */
            strlcpy(blob_term->term_name, term->term_name,
                    sizeof(blob_term->term_name));

            /* Term match condition. */
            blob_term->term_match_id = term->term_match;
            switch (blob_term->term_match_id) {
            case TERM_FROM_SVC_TYPE:
                blob_term->term_match_port = htons(get_eq2_svc_type(
                        term->term_match_val));
                EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Term %s, value %s, port %d.",
                        __func__, term->term_name, term->term_match_val,
                        get_eq2_svc_type(term->term_match_val));
                break;
            case TERM_FROM_SVC_GATE:
                blob_term->term_match_addr = get_eq2_svc_gate(
                        term->term_match_val);
                EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Term %s, match value %s, "
                        "addr %x.",
                        __func__, term->term_name, term->term_match_val,
                        get_eq2_svc_gate(term->term_match_val));
                break;
            default:
                EQ2_LOG(TRACE_LOG_ERR, "%s: Unknown match ID!", __func__);
            }

            /* Term action. */
            blob_term->term_act_id = term->term_act;
            switch (blob_term->term_act_id) {
            case TERM_THEN_SVC_GATE:
                blob_term->term_act_addr = get_eq2_svc_gate(
                        term->term_act_val);
                EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Term %s, act value %s, "
                        "addr %x.",
                        __func__, term->term_name, term->term_act_val,
                        get_eq2_svc_gate(term->term_act_val));
                break;
            case TERM_THEN_SVR_GROUP:
                strlcpy(blob_term->term_act_group_name, term->term_act_val,
                        sizeof(blob_term->term_act_group_name));
                EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Term %s, act value %s",
                        __func__, term->term_name, term->term_act_val);
                break;
            default:
                EQ2_LOG(TRACE_LOG_ERR, "%s: Unknown action ID!", __func__);
            }
            blob_term++;
        }
        blob_rule = (blob_rule_t *)blob_term;
    }

    /* Not change byte order for checksum, cause it's only for RE. */
    blob_ss->ss_cksum = blob_cksum(blob_ss, blob_size);
    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: blob size %d, rule %d.", __func__,
            blob_size, rule_count);
    return blob_ss;
}

/**
 * @brief
 * Merge SSRB info into service-set blob and add servie-set blob to kernel.
 *
 * @param[in] blob_ss_node
 *      Pointer to service-set blob
 *
 * @param[in] ssrb
 *      Pointer to SSRB
 */
static void
add_svc_set_blob (blob_svc_set_node_t *blob_ss_node,
        junos_kcom_pub_ssrb_t *ssrb)
{
    config_blob_key_t key;

    blob_ss_node->ss->ss_id = ntohs(ssrb->svc_set_id);
    blob_ss_node->ss->ss_gen_num = ntohl(ssrb->gen_num);
    blob_ss_node->ss->ss_svc_id = ntohl(ssrb->svc_id);

    bzero(&key, sizeof(key));
    strlcpy(key.key_name, blob_ss_node->ss->ss_name, sizeof(key.key_name));
    key.key_tag = CONFIG_BLOB_SVC_SET;
    key.key_plugin_id = blob_ss_node->ss->ss_eq2_svc_id;
    kcom_add_config_blob(&key, blob_ss_node->ss);

    /* Remove service-set config blob from the list and free it. */
    LIST_REMOVE(blob_ss_node, entry);
    free(blob_ss_node->ss);
    free(blob_ss_node);
}

/**
 * @brief
 * Post process of reading configuration,
 * update configuration on MS-PIC.
 *
 */
static void
config_post_proc (void)
{
    svc_set_t *ss;
    blob_svc_set_t *blob_ss;
    blob_svc_set_node_t *blob_ss_node, *blob_ss_node_tmp;
    ssrb_node_t *ssrb_node;
    config_blob_key_t key;

    /* Rebuild the list of service interfaces. Server group config needs
     * to be sent to all service interfaces that are running balance
     * service.
     */
    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Build the list of service interface.",
            __func__);
    clear_svc_if();
    LIST_FOREACH(ss, &ss_head, entry) {
        add_svc_if(ss->ss_if_name);
    }

    /* Pack server group blob. */
    pack_svr_group_blob();

    /* Pack service-set blobs without SSRB info (service-set ID, service ID
     * and service-set generation number).
     */
    LIST_FOREACH(ss, &ss_head, entry) {
        blob_ss = pack_svc_set_blob(ss);
        if (blob_ss) {
            blob_ss_node = calloc(1, sizeof(blob_svc_set_node_t));
            INSIST_ERR(blob_ss_node != NULL);
            blob_ss_node->ss = blob_ss;
            LIST_INSERT_HEAD(&blob_ss_head, blob_ss_node, entry);
        }
    }

    /* Get all blobs in kernel and process them.
     * config_svc_set_blob_proc() and config_svr_group_blob_proc()
     * will be called to process respective blob.
     */
    kcom_get_config_blob();

    /* Check server group local blob, if it's still there, that means
     * either the server group configuration doesn't exist, or it has been
     * changed and the blob in kernel has been removed,
     * so add it to the kernel.
     */
    if (blob_svr_group_set) {
        bzero(&key, sizeof(key));
        strlcpy(key.key_name, "server-group", sizeof(key.key_name));
        key.key_tag = CONFIG_BLOB_SVR_GROUP;
        key.key_plugin_id = EQ2_BALANCE_SVC;
        kcom_add_config_blob(&key, blob_svr_group_set);
        free(blob_svr_group_set);
        blob_svr_group_set = NULL;
    }

    /* Add all packed service-set blobs that are still there. */
    LIST_FOREACH_SAFE(blob_ss_node, &blob_ss_head, entry, blob_ss_node_tmp) {

        /* SSRB info is needed here. */
        ssrb_node = config_get_ssrb(blob_ss_node->ss->ss_name);

        if (ssrb_node == NULL || ssrb_node->ssrb_state == SSRB_PENDING) {

            /* If SSRB doesn't exist, meaning this is a new service-set,
             * if SSRB is in 'pending' state, meaning service-set is changed,
             * an updated SSRB is expected. So just return for now,
             * this service-set blob will be added when receiving SSRB
             * by async handler later.
             */ 
            continue;
        }

        /* SSRB was received before reading config,
         * get info and add service-set blob.
         */
        add_svc_set_blob(blob_ss_node, &ssrb_node->ssrb);
    }

    /* Mark all updated SSRB as 'idle'. */
    LIST_FOREACH(ssrb_node, &ssrb_head, entry) {
        if (ssrb_node->ssrb_state == SSRB_UPDATED) {
            ssrb_node->ssrb_state = SSRB_IDLE;
        }
    }
}

/**
 * @brief
 * Check SSRB.
 * Only add SSRB that contains Equilibrium II service.
 *
 * @param[in] ssrb
 *      pointer to the SSRB
 *
 * @return
 *      true if SSRB contains Equilibrium II service, otherwise false
 */
static bool
check_ssrb (junos_kcom_pub_ssrb_t *ssrb)
{
    ssrb_svc_info_t *info;
    int i;

    /* Check service order list. */
    if (!ssrb->svc_order.fwd_flow_svc_order ||
            !ssrb->svc_order.rev_flow_svc_order) {
        return false;
    }
    info = ssrb->svc_order.fwd_flow_svc_order->elems;
    for (i = 0; i < ssrb->svc_order.fwd_flow_svc_order->num_elems; i++) {
        if ((strcmp(info->name, EQ2_BALANCE_SVC_NAME) == 0) ||
                (strcmp(info->name, EQ2_CLASSIFY_SVC_NAME) == 0)) {
            return true;
        }
        info++;
    }
    info = ssrb->svc_order.rev_flow_svc_order->elems;
    for (i = 0; i < ssrb->svc_order.rev_flow_svc_order->num_elems; i++) {
        if ((strcmp(info->name, EQ2_BALANCE_SVC_NAME) == 0) ||
                (strcmp(info->name, EQ2_CLASSIFY_SVC_NAME) == 0)) {
            return true;
        }
        info++;
    }
    return false;
}

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * @brief
 * Init the data structure that will store configuration info,
 * or in other words, the condition(s)
 */
void
config_init (void)
{
    EQ2_TRACE(EQ2_TRACEFLAG_NORMAL, "%s: Initialize configuration.", __func__);

    LIST_INIT(&svc_gate_head);
    LIST_INIT(&svr_group_head);
    svr_group_count = 0;
    LIST_INIT(&svc_type_head);
    LIST_INIT(&balance_rule_head);
    LIST_INIT(&classify_rule_head);
    LIST_INIT(&ss_head);
    LIST_INIT(&ssrb_head);
    LIST_INIT(&blob_ss_head);
    LIST_INIT(&svc_if_head);
    svc_if_count = 0;
    blob_svr_group_set = NULL;
}

/**
 * @brief
 * Clear the configuration info when done.
 */
void
config_clear (void)
{
    clear_eq2_svc_gate();
    clear_eq2_svc_type();
    clear_eq2_svr_group();
    clear_eq2_svc_rule(&balance_rule_head);
    clear_eq2_svc_rule(&classify_rule_head);
    clear_svc_set();
    clear_ssrb_config();
}

/**
 * @brief
 * Clear the SSRB configuration info
 */
void
clear_ssrb_config (void)
{
    ssrb_node_t *ssrb;

    while((ssrb = LIST_FIRST(&ssrb_head))) {
        LIST_REMOVE(ssrb, entry);
        free(ssrb);
    }
}

/**
 * @brief
 * Add/delete/update SSRB in the configuration.
 *
 * @param[in] ssrb
 *      The SSRB data
 *
 * @param[in] op
 *      SSRB operation
 *
 * @return
 *      0 on success, -1 on failure
 */
int
config_ssrb_op (junos_kcom_pub_ssrb_t *ssrb, config_ssrb_op_t op)
{
    ssrb_node_t *ssrb_node;
    blob_svc_set_node_t *blob_ss_node;

    if (!check_ssrb(ssrb)) {
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM,
                "%s: SSRB doesn't contain Equilibrium II service.", __func__); 
        return -1;
    }

    switch (op) {
    case CONFIG_SSRB_ADD:
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: Add SSRB %s, ID %d", __func__,
                ssrb->svc_set_name, ssrb->svc_set_id); 

        /* Check for existance. */
        ssrb_node = config_get_ssrb(ssrb->svc_set_name);
        if (ssrb_node) {
            EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: SSRB exists.", __func__); 
            return -1;
        }

        /* Create SSRB config node and add it to the list. */
        ssrb_node = calloc(1, sizeof(ssrb_node_t));
        INSIST_ERR(ssrb_node != NULL);

        LIST_INSERT_HEAD(&ssrb_head, ssrb_node, entry);
        bcopy(ssrb, &ssrb_node->ssrb, sizeof(ssrb_node->ssrb));

        /* Fall to SSRB change. */
    case CONFIG_SSRB_CHANGE:
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: Change SSRB %s, ID %d", __func__,
                ssrb->svc_set_name, ssrb->svc_set_id); 

        /* Check for existance. */
        ssrb_node = config_get_ssrb(ssrb->svc_set_name);
        if (!ssrb_node) {
            EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: SSRB doesn't exist.", __func__); 
            return -1;
        }

        /* Update SSRB. */
        bcopy(ssrb, &ssrb_node->ssrb, sizeof(ssrb_node->ssrb));
        ssrb_node->ssrb_state = SSRB_UPDATED;

        /* Get service-set blob containing balance rules.
         * If service-set blob doesn't exit, meaning this SSRB update was
         * received before reading config, then leave it there.
         * If service-set blob exists, meaning this SSRB update was received
         * after reading config and it's being expected, then add service-set
         * blob to kernel.
         */
        blob_ss_node = get_svc_set_blob_node(ssrb->svc_set_name,
                EQ2_BALANCE_SVC);
        if (blob_ss_node) {
            add_svc_set_blob(blob_ss_node, ssrb);
            ssrb_node->ssrb_state = SSRB_IDLE;
        }

        /* Get service-set blob containing classify rules. */
        blob_ss_node = get_svc_set_blob_node(ssrb->svc_set_name,
                EQ2_CLASSIFY_SVC);
        if (blob_ss_node) {
            add_svc_set_blob(blob_ss_node, ssrb);
            ssrb_node->ssrb_state = SSRB_IDLE;
        }

        break;
    case CONFIG_SSRB_DEL:
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: Delete SSRB %s, ID %d", __func__,
                ssrb->svc_set_name, ssrb->svc_set_id); 

        /* Check for existance. */
        ssrb_node = config_get_ssrb(ssrb->svc_set_name);
        if (!ssrb_node) {
            EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: SSRB doesn't exist.", __func__); 
            return -1;
        }

        /* Remove SSRB from the list and free it. */
        LIST_REMOVE(ssrb_node, entry);
        free(ssrb_node);
        break;
    }

    return 0;
}

/**
 * @brief
 * Process server group blob.
 *
 * @param[in] key
 *      Pointer to the configuration blob key
 *
 * @param[in] blob
 *      Pointer to the blob
 */
void
config_svr_group_blob_proc (config_blob_key_t *key, void *blob)
{
    blob_svr_group_set_t *blob_sg_set = blob;

    if (!blob_svr_group_set) {
        return;
    }

    if (blob_svr_group_set->gs_cksum == blob_sg_set->gs_cksum) {
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: Blob %s is not changed.",
                __func__, key->key_name);

        /* Clear local server group blob, so it won't be added later. */
        free(blob_svr_group_set);
        blob_svr_group_set = NULL;
    } else {
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: Blob %s is changed %d -> %d.",
                __func__, key->key_name, blob_sg_set->gs_cksum,
                blob_svr_group_set->gs_cksum);

        /* Delete server group blob in kernel, new blob will be added later. */
        kcom_del_config_blob(key);
    }
}

/**
 * @brief
 * Process service-set config blob.
 *
 * @param[in] key
 *      Pointer to the configuration blob key
 *
 * @param[in] blob
 *      Pointer to the blob
 */
void
config_svc_set_blob_proc (config_blob_key_t *key, void *blob)
{
    blob_svc_set_t *blob_ss = blob;
    blob_svc_set_node_t *blob_ss_node;

    /* Get service-set blob from configuration. */
    blob_ss_node = get_svc_set_blob_node(key->key_name, key->key_plugin_id);

    if (blob_ss_node && (blob_ss_node->ss->ss_cksum == blob_ss->ss_cksum)) {
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: Blob %s %d is not changed.",
                __func__, key->key_name, key->key_plugin_id);

        /* Found local service-set blob and no change, delete it. */
        LIST_REMOVE(blob_ss_node, entry);
        free(blob_ss_node->ss);
        free(blob_ss_node);

    } else {
        if (blob_ss_node == NULL) {
            EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: %s %d is deleted.",
                    __func__, key->key_name, key->key_plugin_id);
        } else {
            EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: %s %d is changed %d -> %d.",
                    __func__, key->key_name, key->key_plugin_id,
                    blob_ss->ss_cksum, blob_ss_node->ss->ss_cksum);
        }

        /* Either did not found local service-set blob, which means it's
         * already removed from CLI configuration, then just delete the blob
         * from kernel,
         * or found the local blob but it's changed, then delete the blob from
         * kernel here and add the new blob to kernel after
         * kcom_get_config_blob() is done in config_post_proc().
         */
        kcom_del_config_blob(key);
    }
}

/**
 * @brief
 * Read daemon configuration from the database.
 *
 * @param[in] check
 *      1 if this function being invoked because of a commit check
 * 
 * @return
 *      0 on success, -1 on failure
 * 
 * @note Do not use ERRMSG/EQ2_LOG during config check normally.
 */
int
eq2_config_read (int check)
{
    ddl_handle_t *top = NULL;
    ddl_handle_t *dop = NULL;
    const char *svc_set_config[] = { "services", "service-set", NULL };
    const char *eq2_config[] = { "sync", "equilibrium2", NULL };
    const char *eq2_balance_rules_config[] = { "sync", "equilibrium2",
            "balance-rules", "rule", NULL };
    const char *eq2_classify_rules_config[] = { "sync", "equilibrium2",
            "classify-rules", "rule", NULL };

    /* Read trace options. */
    if (junos_trace_read_config(check, eq2_config)) {
        dax_error(NULL, "Parsing equilibrium2 traceoptions ERROR!");
        return -1;
    }

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Start reading config.", __func__);

    /* Read [sync equilibrium2 service-gate]. */
    read_eq2_svc_gate();

    /* Read [sync equilibrium2 service-group]. */
    read_eq2_svr_group();

    /* Read [sync equilibrium2 service-type]. */
    read_eq2_svc_type();

    /* Read [sync equilibrium2 balance-rules]. */
    if (dax_get_object_by_path(NULL, eq2_balance_rules_config, &top, FALSE)) {
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Read balance rules.", __func__);
        if (dax_walk_list(top, DAX_WALK_DELTA, read_eq2_rule,
                &balance_rule_head) != DAX_WALK_OK) {
            dax_release_object(&top);
            dax_error(NULL, "Walk through equilibrium2 balance rules ERROR!");
            return -1;
        }
        dax_release_object(&top);
    }

    /* Read [sync equilibrium2 classify-rules]. */
    if (dax_get_object_by_path(NULL, eq2_classify_rules_config, &top, FALSE)) {
        EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Read classify rules.", __func__);
        if (dax_walk_list(top, DAX_WALK_DELTA, read_eq2_rule,
                &classify_rule_head) != DAX_WALK_OK) {
            dax_release_object(&top);
            dax_error(NULL, "Walk through equilibrium2 classify rules ERROR!");
            return -1;
        }
        dax_release_object(&top);
    }

    /* Read service-set. */
    /* Clear all service-set before reading. */
    clear_svc_set();

    /* Get the head object of service-set list. */
    if (dax_get_object_by_path(NULL, svc_set_config, &top, FALSE)) {
        while (dax_visit_container(top, &dop)) {

            /* Always read all service-sets and load the service-sets that
             * contain EQ2 service, no matter they are changed or not,
             * cause application doesn't keep any service-set configuration.
             */
            read_svc_set(dop);
        }
        dax_release_object(&top);
    }

    if (!check) {
        config_post_proc();
    }

    EQ2_TRACE(EQ2_TRACEFLAG_CONF, "%s: Configuration was loaded.", __func__);

    return 0;
}


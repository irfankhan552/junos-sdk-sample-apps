/*
 * $Id: ped_snmp.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_snmp.c
 * 
 * @brief Routines related to SNMP
 * 
 * Functions to intialize snmp sub-agent and process snmp requests. 
 * 
 */
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <net-snmp/agent/agent_trap.h>
#include <net-snmp/library/snmp_jnx_ext.h>
#include <jnx/netsnmp_trap.h>
#include <agent/snmpd.h>
#include <isc/eventlib.h>
#include <snmp/snmp_pathnames.h>
#include <sync/common.h>
#include "ped_snmp.h"
#include "ped_daemon_name.h"
#include "ped_policy_table.h"
#include "ped_services.h"
#include "ped_kcom.h"

#include PE_OUT_H

/*** Constants ***/


/*** Data Structures ***/

extern evContext ped_ctx;               ///< event context for ped

static oid if_total_oid[] = {PE_MIB_IF_TOTAL_OID};    ///< OID of the number of total interfaces
static oid if_table_oid[] = {PE_MIB_IF_TABLE_OID};    ///< OID of the policy table
static oid if_count_oid[] = {PE_MIB_IF_COUNT_OID};    ///< OID of the number of interfaces under management
static oid snmp_ver_oid[] = {PE_MIB_SNMP_VER_OID};    ///< OID of the net-snmp version
static oid psd_state_oid[] = {PE_MIB_PSD_STATE_OID};  ///< OID of the state of PSD connection
static oid psd_time_oid[] = {PE_MIB_PSD_TIME_OID};    ///< OID of the up time of PSD connection
static oid notify_psd_state_oid[] = {PE_MIB_NOTIFY_PSD_STATE_OID};    ///< OID of notification of the state of PSD connection


/*** STATIC/INTERNAL Functions ***/

/**
 * Set SNMP default storage (configuration).
 * 
 */
static void
init_netsnmp_ds(void)
{
    netsnmp_ds_set_string(NETSNMP_DS_LIBRARY_ID,
            NETSNMP_DS_LIB_APPTYPE, DNAME_PED);

    netsnmp_ds_set_string(NETSNMP_DS_APPLICATION_ID, 
            NETSNMP_DS_AGENT_X_SOCKET, PATH_MASTER_AGENTX);

    netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID,
            NETSNMP_DS_AGENT_ROLE, SUB_AGENT);
}


/**
 * Get the number of routes.
 * 
 * @param[in] route
 *      Pointer to route data in policy table.
 * 
 * @return
 *      The number of routes.
 *
 */
static int
get_route_count(ped_policy_route_t *route)
{
    int count = 0;

    while(route) {
        count++;
        route = route->next;
    }
    return count;
}


/**
 * Get next entry in the interface table.
 * 
 * @param[in] loop_context
 *      Pointer to loop context pointer.
 * 
 * @param[in] data_context
 *      Pointer to data context pointer.
 * 
 * @param[out] put_index_data
 *      Pointer to index data.
 *
 * @param[in] data
 *      Pointer to iterator info.
 *
 * @return
 *      Pointer to index data if found valid entry, otherwise NULL.
 *
 */
static netsnmp_variable_list *
table_get_next_data_point(
    void **loop_context,
    void **data_context,
    netsnmp_variable_list *put_index_data,
    netsnmp_iterator_info *data __unused)
{
    policy_table_entry_t *entry;

    entry = policy_table_next();
    if(entry == NULL) {
        return NULL;
    }

    *loop_context = (void *)entry;
    *data_context = (void *)entry;

    snmp_set_var_typed_value(put_index_data, ASN_OCTET_STR,
            entry->ifname, strlen(entry->ifname));

    junos_trace(PED_TRACEFLAG_SNMP,
            "%s: index: %s", __func__, entry->ifname);  

    return put_index_data;
}


/**
 * Get the first entry in the interface table.
 * 
 * @param[in] loop_context
 *      Pointer to loop context pointer.
 * 
 * @param[in] data_context
 *      Pointer to data context pointer.
 * 
 * @param[out] put_index_data
 *      Pointer to index data.
 *
 * @param[in] data
 *      Pointer to iterator info.
 *
 * @return
 *      Pointer to index data if found valid entry, otherwise NULL.
 *
 */
static netsnmp_variable_list *
table_get_first_data_point(
    void **loop_context,
    void **data_context,
    netsnmp_variable_list *put_index_data,
    netsnmp_iterator_info *data)
{
    policy_table_iterator_reset();

    return table_get_next_data_point(loop_context, data_context,
            put_index_data, data);
}


/**
 * Handler to process interface table requests.
 * 
 * @param[in] handler
 *      Pointer to mib handler.
 * 
 * @param[in] reginfo
 *      Pointer to handler registration info.
 * 
 * @param[in] reqinfo
 *      Pointer to agent request info.
 *
 * @param[in,out] requests
 *      Pointer to requests.
 *
 * @return
 *      SNMP_ERR_NOERROR.
 *
 */
static int
handler_if_table (
    netsnmp_mib_handler             *handler __unused,
    netsnmp_handler_registration    *reginfo __unused,
    netsnmp_agent_request_info      *reqinfo,
    netsnmp_request_info            *requests)
{
    netsnmp_request_info       *request;
    netsnmp_table_request_info *table_info;
    policy_table_entry_t *entry;
    int tmp;

    switch (reqinfo->mode) {
        case MODE_GET:
            request = requests;
            while(request) {
                entry = (policy_table_entry_t *)
                        netsnmp_extract_iterator_context(request);
                table_info = netsnmp_extract_table_info(request);

                if(entry == NULL) {
                    request = request->next;
                    continue;
                }

                switch(table_info->colnum) {
                    case 1:
                        snmp_set_var_typed_value(request->requestvb,
                                ASN_OCTET_STR, entry->ifname,
                                strlen(entry->ifname));
                        break;
                    case 2:
                        tmp = (int)entry->af;
                        snmp_set_var_typed_value(request->requestvb,
                                ASN_INTEGER, (const u_char *)&tmp,
                                sizeof(int));
                        break;
                    case 3:
                        if((entry->filter) &&
                                (entry->filter->filter_data.input_filter[0])) {
                            snmp_set_var_typed_value(request->requestvb,
                                    ASN_OCTET_STR,
                                    entry->filter->filter_data.input_filter,
                                    strlen(entry->filter->filter_data.input_filter));
                        } else {
                            snmp_set_var_typed_value(request->requestvb,
                                    ASN_OCTET_STR,
                                    "N/A", strlen("N/A"));
                        }
                        break;
                    case 4:
                        if((entry->filter) &&
                                (entry->filter->filter_data.output_filter[0])) {
                            snmp_set_var_typed_value(request->requestvb,
                                    ASN_OCTET_STR,
                                    entry->filter->filter_data.output_filter,
                                    strlen(entry->filter->filter_data.output_filter));
                        } else {
                            snmp_set_var_typed_value(request->requestvb,
                                    ASN_OCTET_STR,
                                    "N/A", strlen("N/A"));
                        }
                        break;
                    case 5:
                        tmp = get_route_count(entry->route);
                        snmp_set_var_typed_value(request->requestvb,
                                ASN_INTEGER, (const u_char *)&tmp,
                                sizeof(int));
                        break;
                }
                request = request->next;
            }
            break;
        default:
            /* we should never get here, so this is a really bad error */
            return SNMP_ERR_GENERR;
    }
    return SNMP_ERR_NOERROR;
}


/**
 * Handler to process snmp version requests.
 * 
 * @param[in] handler
 *      Pointer to mib handler.
 * 
 * @param[in] reginfo
 *      Pointer to handler registration info.
 * 
 * @param[in] reqinfo
 *      Pointer to agent request info.
 *
 * @param[in,out] requests
 *      Pointer to requests.
 *
 * @return
 *      SNMP_ERR_NOERROR.
 *
 */
static int
handler_snmp_ver(
    netsnmp_mib_handler *handler __unused,
    netsnmp_handler_registration *reginfo __unused,
    netsnmp_agent_request_info   *reqinfo,
    netsnmp_request_info         *requests)
{
    const u_char *netsnmp_version;

    netsnmp_version = netsnmp_get_version();
    switch(reqinfo->mode) {
        case MODE_GET:
            snmp_set_var_typed_value(requests->requestvb, ASN_OCTET_STR,
                    netsnmp_version, strlen(netsnmp_version));
            break;
        default:
            /* we should never get here, so this is a really bad error */
            return SNMP_ERR_GENERR;
    }

    return SNMP_ERR_NOERROR;
}


/**
 * Handler to process the requests of the number of managed interfaces.
 * 
 * @param[in] handler
 *      Pointer to mib handler.
 * 
 * @param[in] reginfo
 *      Pointer to handler registration info.
 * 
 * @param[in] reqinfo
 *      Pointer to agent request info.
 *
 * @param[in,out] requests
 *      Pointer to requests.
 *
 * @return
 *      SNMP_ERR_NOERROR.
 *
 */
static int
handler_if_count(
    netsnmp_mib_handler *handler __unused,
    netsnmp_handler_registration *reginfo __unused,
    netsnmp_agent_request_info   *reqinfo,
    netsnmp_request_info         *requests)
{
    int count;

    count = policy_table_entry_count();
    switch(reqinfo->mode) {
        case MODE_GET:
            snmp_set_var_typed_value(requests->requestvb, ASN_INTEGER,
                    (const u_char *)&count, sizeof(int));
            break;
        default:
            /* we should never get here, so this is a really bad error */
            return SNMP_ERR_GENERR;
    }

    return SNMP_ERR_NOERROR;
}


/**
 * Handler to process the request of total number of interfaces.
 * 
 * @param[in] handler
 *      Pointer to mib handler.
 * 
 * @param[in] reginfo
 *      Pointer to handler registration info.
 * 
 * @param[in] reqinfo
 *      Pointer to agent request info.
 *
 * @param[in,out] requests
 *      Pointer to requests.
 *
 * @return
 *      SNMP_ERR_NOERROR.
 *
 */
static int
handler_if_total(
    netsnmp_mib_handler *handler __unused,
    netsnmp_handler_registration *reginfo __unused,
    netsnmp_agent_request_info   *reqinfo,
    netsnmp_request_info         *requests)
{
    int count;

    count = get_interface_total();
    switch(reqinfo->mode) {
        case MODE_GET:
            snmp_set_var_typed_value(requests->requestvb, ASN_INTEGER,
                    (const u_char *)&count, sizeof(int));
            break;
        default:
            /* we should never get here, so this is a really bad error */
            return SNMP_ERR_GENERR;
    }

    return SNMP_ERR_NOERROR;
}


/**
 * Handler to process the request of PSD connection status.
 * 
 * @param[in] handler
 *      Pointer to mib handler.
 * 
 * @param[in] reginfo
 *      Pointer to handler registration info.
 * 
 * @param[in] reqinfo
 *      Pointer to agent request info.
 *
 * @param[in,out] requests
 *      Pointer to requests.
 *
 * @return
 *      SNMP_ERR_NOERROR.
 *
 */
static int
handler_psd_state(
    netsnmp_mib_handler *handler __unused,
    netsnmp_handler_registration *reginfo __unused,
    netsnmp_agent_request_info   *reqinfo,
    netsnmp_request_info         *requests)
{
    int state;

    state = (int)psd_conn_state();
    switch(reqinfo->mode) {
        case MODE_GET:
            snmp_set_var_typed_value(requests->requestvb, ASN_INTEGER,
                    (const u_char *)&state, sizeof(int));
            break;
        default:
            /* we should never get here, so this is a really bad error */
            return SNMP_ERR_GENERR;
    }

    return SNMP_ERR_NOERROR;
}


/**
 * Handler to process the request of PSD connection time.
 * 
 * @param[in] handler
 *      Pointer to mib handler.
 * 
 * @param[in] reginfo
 *      Pointer to handler registration info.
 * 
 * @param[in] reqinfo
 *      Pointer to agent request info.
 *
 * @param[in,out] requests
 *      Pointer to requests.
 *
 * @return
 *      SNMP_ERR_NOERROR.
 *
 */
static int
handler_psd_time(
    netsnmp_mib_handler *handler __unused,
    netsnmp_handler_registration *reginfo __unused,
    netsnmp_agent_request_info   *reqinfo,
    netsnmp_request_info         *requests)
{
    int psd_time;
    char buf[64];

    psd_time = get_psd_conn_time();
    sprintf(buf, "%02d:%02d:%02d", (psd_time / 3600) % 60,
            (psd_time / 60) % 60, psd_time % 60);
    switch(reqinfo->mode) {
        case MODE_GET:
            snmp_set_var_typed_value(requests->requestvb, ASN_OCTET_STR,
                    buf, strlen(buf));
            break;
        default:
            /* we should never get here, so this is a really bad error */
            return SNMP_ERR_GENERR;
    }

    return SNMP_ERR_NOERROR;
}


/**
 * Initialize MIB policy table.
 * 
 */
static void
init_mib_policy_table(void)
{
    size_t if_table_oid_len = OID_LENGTH(if_table_oid);
    netsnmp_handler_registration    *reg;
    netsnmp_iterator_info           *iinfo;
    netsnmp_table_registration_info *table_info;

    reg = netsnmp_create_handler_registration(
            "peInterfaceTable", handler_if_table,
            if_table_oid, if_table_oid_len, HANDLER_CAN_RONLY);

    table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);
    netsnmp_table_helper_add_indexes(table_info,
                           ASN_OCTET_STR,  /* index: interface name */
                           0);
    table_info->min_column = 1;
    table_info->max_column = 5;

    iinfo = SNMP_MALLOC_TYPEDEF(netsnmp_iterator_info);
    iinfo->get_first_data_point = table_get_first_data_point;
    iinfo->get_next_data_point  = table_get_next_data_point;
    iinfo->table_reginfo        = table_info;

    netsnmp_register_table_iterator(reg, iinfo);
}


/**
 * Initialize mib varables and register handlers.
 * 
 */
static void
ped_init_mib(void)
{
    init_mib_policy_table();

    netsnmp_register_scalar(
            netsnmp_create_handler_registration("PE_PSD_State",
                    handler_if_count, if_count_oid,
                    OID_LENGTH(if_count_oid), HANDLER_CAN_RONLY));

    netsnmp_register_scalar(
            netsnmp_create_handler_registration("PE_SNMP_Version",
                    handler_snmp_ver, snmp_ver_oid,
                    OID_LENGTH(snmp_ver_oid), HANDLER_CAN_RONLY));

    netsnmp_register_scalar(
            netsnmp_create_handler_registration("PE_Interface_Count",
                    handler_psd_state, psd_state_oid,
                    OID_LENGTH(psd_state_oid), HANDLER_CAN_RONLY));

    netsnmp_register_scalar(
            netsnmp_create_handler_registration("PE_Interface_Total",
                    handler_if_total, if_total_oid,
                    OID_LENGTH(if_total_oid), HANDLER_CAN_RONLY));

    netsnmp_register_scalar(
            netsnmp_create_handler_registration("PE_PSD_Time",
                    handler_psd_time, psd_time_oid,
                    OID_LENGTH(psd_time_oid), HANDLER_CAN_RONLY));
}


/*** GLOBAL/EXTERNAL Functions ***/



/**
 * Initialze snmp sub-agent.
 * 
 */
void
netsnmp_subagent_init(void)
{
    /* setup context */
    netsnmp_set_evcontext(ped_ctx);

    /* set netsnmp parameters */
    init_netsnmp_ds();

    /* initialize agent */
    init_agent(DNAME_PED);

    /* register mib modules */
    ped_init_mib();

    /* start library */
    init_snmp(DNAME_PED);
}

/**
 * Send notification of PSD connection status.
 * 
 * @param[in] state
 *      The state of PSD connection.
 * 
 */
void
ped_notify_psd_state(boolean state)
{
    netsnmp_variable_list *var_list;

    junos_trace(PED_TRACEFLAG_SNMP,
            "%s: state: %d", __func__, state);  

    var_list = netsnmp_trap_sdk_request_header(notify_psd_state_oid,
            OID_LENGTH(notify_psd_state_oid));

    snmp_varlist_add_variable(&var_list, psd_state_oid,
            OID_LENGTH(psd_state_oid), ASN_INTEGER,
            (const char *)&state, sizeof(char));

    netsnmp_trap_request_send(var_list);

    snmp_free_varbind(var_list);
}

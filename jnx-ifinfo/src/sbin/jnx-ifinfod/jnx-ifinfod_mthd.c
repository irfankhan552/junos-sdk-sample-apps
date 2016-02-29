/*
 * $Id: jnx-ifinfod_mthd.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 */

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include <jnx/bits.h>
#include <jnx/aux_types.h>
#include <jnx/patricia.h>
#include <jnx/junos_kcom.h>
#include "jnx-ifinfod_config.h"
#include "jnx-ifinfod_snmp.h"
#include "jnx-ifinfod_util.h"



typedef struct ifinfo_snmp_req_s {
    netsnmp_agent_request_info   *reqinfo;
    netsnmp_request_info         *requests;
} ifinfo_snmp_req_t;

/**
 * jnxIfinfoAliasTable get next function 
 */
static netsnmp_variable_list *
jnxIfinfoAliasTable_get_next_data_point (
                          void **my_loop_context,
                          void **my_data_context,
                          netsnmp_variable_list *put_index_data,
                          netsnmp_iterator_info *mydata __unused)
{
    ifinfod_intf_t *entry = (ifinfod_intf_t *) *my_loop_context;
    netsnmp_variable_list *idx = put_index_data;

    if (entry) {
        snmp_set_var_value(idx, (const u_char *) &entry->intf_id,
			   sizeof(entry->intf_id));
        *my_data_context = (void *) entry;
        *my_loop_context = (void *) ifinfod_node_next(entry);
        return put_index_data;
    } else {
        return NULL;
    }
}


/**
 * Ifinfo iterator hook routines - using 'get_next' to do most of the work
 */
static netsnmp_variable_list *
jnxIfinfoAliasTable_get_first_data_point (void **my_loop_context,
                          void **my_data_context,
                          netsnmp_variable_list *put_index_data,
                          netsnmp_iterator_info *mydata)
{
    *my_loop_context = ifinfod_node_first();
    return jnxIfinfoAliasTable_get_next_data_point(my_loop_context,
                                                   my_data_context,
                                                   put_index_data,
                                                   mydata);
}

/**
 * handles requests for the jnxIfinfoAliasTable table 
 */
static int
jnxIfinfoAliasTable_handler (
    netsnmp_mib_handler               *handler __unused,
    netsnmp_handler_registration      *reginfo __unused,
    netsnmp_agent_request_info        *reqinfo,
    netsnmp_request_info              *requests)
{

    netsnmp_request_info       *request;
    netsnmp_table_request_info *table_info;
    ifinfod_intf_t *table_entry;
    kcom_ifdev_t   ifd ;
    int retval;

    switch (reqinfo->mode) {
    case MODE_GET:
        for (request=requests; request; request=request->next) {
            table_entry = (ifinfod_intf_t *)
                          netsnmp_extract_iterator_context(request);
            table_info  = netsnmp_extract_table_info(request);
    
            if (table_entry == NULL)
                continue;

            switch (table_info->colnum) {
            case COLUMN_JNX_IFINFO_INDEX:
                snmp_set_var_typed_value(request->requestvb, ASN_INTEGER,
                                         (const u_char *) &table_entry->intf_id,
                                         sizeof(table_entry->intf_id));
                break;
            case COLUMN_JNX_IFINFO_ALIAS:
                snmp_set_var_typed_value(request->requestvb, ASN_OCTET_STR,
                                         table_entry->intf_alias_name,
                                         strlen(table_entry->intf_alias_name));
                break;
            case COLUMN_JNX_IFINFO_NAME:
		retval = junos_kcom_ifd_get_by_index(table_entry->intf_id,
						     &ifd); 
		if (retval == KCOM_OK) {
                snmp_set_var_typed_value(request->requestvb, ASN_OCTET_STR,
                                         ifd.ifdev_name,
                                         strlen(ifd.ifdev_name));
		}
                break;

            }
        }
        break;

    }
    return SNMP_ERR_NOERROR;
}

/** 
 * Initialize the jnxIfinfoAliasTable table by defining its 
 * contents and how it's structured 
 */
static void
initialize_table_jnxIfinfoAliasTable (void)
{
    static oid jnxIfinfoAliasTable_oid[] = {1,3,6,1,4,1,2636,5,5,2,1};
    size_t jnxIfinfoAliasTable_oid_len   = OID_LENGTH(jnxIfinfoAliasTable_oid);
    netsnmp_handler_registration    *reg;
    netsnmp_iterator_info           *iinfo;
    netsnmp_table_registration_info *table_info;

    reg = netsnmp_create_handler_registration(
              "jnxIfinfoAliasTable",   jnxIfinfoAliasTable_handler,
              jnxIfinfoAliasTable_oid, jnxIfinfoAliasTable_oid_len,
              HANDLER_CAN_RONLY
              );

    table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);
    netsnmp_table_helper_add_indexes(table_info,
                           ASN_INTEGER,  /* index: jnxIfinfoIndex */
                           0);
    table_info->min_column = COLUMN_JNX_IFINFO_ALIAS;
    table_info->max_column = COLUMN_JNX_IFINFO_NAME;
    
    iinfo = SNMP_MALLOC_TYPEDEF(netsnmp_iterator_info);
    iinfo->get_first_data_point = jnxIfinfoAliasTable_get_first_data_point;
    iinfo->get_next_data_point  = jnxIfinfoAliasTable_get_next_data_point;
    iinfo->table_reginfo        = table_info;
    
    netsnmp_register_table_iterator(reg, iinfo);
}

/**
 * Initializes the jnxIfinfoMIB module 
 */
void
init_jnxIfinfoMIB (void)
{
  /* here we initialize all the tables we're planning on supporting */
    initialize_table_jnxIfinfoAliasTable();
}


/**
 * jnxIfinfoInterfaceTable get next function 
 */ 
static netsnmp_variable_list *
jnxIfinfoInterfaceTable_get_next_data_point (
                          void **my_loop_context,
                          void **my_data_context,
                          netsnmp_variable_list *put_index_data,
                          netsnmp_iterator_info *mydata __unused)
{
    kcom_ifds_t *entry = (kcom_ifds_t *) *my_loop_context;
    netsnmp_variable_list *idx = put_index_data;
    
    if (entry) {
        snmp_set_var_value(idx, (const char *) &entry->devptr->ifdev_snmp_id,
			   sizeof(entry->devptr->ifdev_snmp_id));
        *my_data_context = (void *) entry;
        *my_loop_context = (void *) ifinfod_get_ifd_next(entry);
        return put_index_data;
    } else {
        return NULL;
    }
}
    

/**
 * Ifinfo iterator hook routines - using 'get_next' to do most of the work 
 */
static netsnmp_variable_list *
jnxIfinfoInterfaceTable_get_first_data_point (void **my_loop_context,
                          void **my_data_context,
                          netsnmp_variable_list *put_index_data,
                          netsnmp_iterator_info *mydata)
{
    *my_loop_context = ifinfod_get_ifd_first();
    return jnxIfinfoInterfaceTable_get_next_data_point(my_loop_context,
                                                   my_data_context,   
                                                   put_index_data,
                                                   mydata);
}

/**
 * jnxIfinfoInterfaceTable handler
 */
static int
handle_jnxIfinfoInterfaceTable(
    netsnmp_mib_handler               *handler __unused,
    netsnmp_handler_registration      *reginfo __unused,
    netsnmp_agent_request_info        *reqinfo, 
    netsnmp_request_info              *requests)
{

    netsnmp_request_info       *request;
    netsnmp_table_request_info *table_info;
    kcom_ifds_t *table_entry;

    switch (reqinfo->mode) {
    case MODE_GET:
        for (request=requests; request; request=request->next) {
            table_entry = (kcom_ifds_t *)
                          netsnmp_extract_iterator_context(request);
            table_info  = netsnmp_extract_table_info(request);

            if (table_entry == NULL)
                continue;

            switch (table_info->colnum) {
            case COLUMN_JNX_IFINFO_IFD_SNMP_ID:
                snmp_set_var_typed_value(request->requestvb,
					 ASN_INTEGER,
					 (const u_char *)&table_entry->devptr->ifdev_snmp_id,
                                         sizeof(table_entry->devptr->ifdev_snmp_id));
                break;

            case COLUMN_JNX_IFINFO_INTERFACE:
                snmp_set_var_typed_value(request->requestvb, ASN_OCTET_STR,
                                         table_entry->devptr->ifdev_name,
                                         strlen(table_entry->devptr->ifdev_name));
                break;

            }
        }
        break;

    }
    return SNMP_ERR_NOERROR;
}


/**
 * Initializes the jnxIfinfoInterfaces  module 
 */ 
void
init_jnxIfinfoInterfacesTable (void)
{
    static oid jnxIfinfoInterfaces_oid[] = { 1,3,6,1,4,1,2636,5,5,2,3 };
 
    netsnmp_handler_registration    *reg;
    netsnmp_iterator_info           *iinfo;
    netsnmp_table_registration_info *table_info;

    reg = netsnmp_create_handler_registration(
              "jnxIfinfoInterfaceTable",   handle_jnxIfinfoInterfaceTable,
              jnxIfinfoInterfaces_oid, OID_LENGTH(jnxIfinfoInterfaces_oid),
              HANDLER_CAN_RONLY
              );

    /** net-snmp call **/
    table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info); 


    netsnmp_table_helper_add_indexes(table_info,
                           ASN_INTEGER,  /* index: jnxIfinfoInterfaceIndex */
                           0);
    table_info->min_column = COLUMN_JNX_IFINFO_INTERFACE;
    table_info->max_column = COLUMN_JNX_IFINFO_INTERFACE;
   
    iinfo = SNMP_MALLOC_TYPEDEF(netsnmp_iterator_info);
    iinfo->get_first_data_point = jnxIfinfoInterfaceTable_get_first_data_point;
    iinfo->get_next_data_point  = jnxIfinfoInterfaceTable_get_next_data_point;
    iinfo->table_reginfo        = table_info;

    netsnmp_register_table_iterator(reg, iinfo);

}

/**
 * jnxIfinfoNetSnmp version handler
 */
static int
handle_jnxIfinfoNetSnmpVersion (
                          netsnmp_mib_handler *handler __unused,
                          netsnmp_handler_registration *reginfo __unused,
                          netsnmp_agent_request_info   *reqinfo,
                          netsnmp_request_info         *requests)
{
    const u_char *netsnmp_version;
    
    netsnmp_version = netsnmp_get_version();
    switch  (reqinfo->mode) {

    case MODE_GET:
        snmp_set_var_typed_value(requests->requestvb, ASN_OCTET_STR,
                                 (const u_char *) netsnmp_version,
                                 strlen(netsnmp_version));
        break;
    default:
        /* we should never get here, so this is a really bad error */
        return SNMP_ERR_GENERR;
    }

    return SNMP_ERR_NOERROR;
}


/**
 * Initializes the jnxIfinfoNetSnmpVersion module 
 */
void
init_jnxIfinfoNetSnmpVersion(void)
{
    static oid jnxIfinfoNetSnmpVersion_oid[] = { 1,3,6,1,4,1,2636,5,5,2,2 };

    netsnmp_register_scalar(
        netsnmp_create_handler_registration("jnxIfinfoNetSnmpVersion",
                                            handle_jnxIfinfoNetSnmpVersion,
                                            jnxIfinfoNetSnmpVersion_oid,
                                            OID_LENGTH(jnxIfinfoNetSnmpVersion_oid),
                                            HANDLER_CAN_RONLY));
}

/*
 * $Id: jnx-exampled_mthd.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * Copyright (c) 2006-2007, Juniper Networks, Inc.
 * All rights reserved.
 *
 */

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include <jnx/bits.h>
#include <jnx/aux_types.h>
#include <jnx/patricia.h>
#include "jnx-exampled_config.h"
#include "jnx-exampled_snmp.h"

/* get next function */
static netsnmp_variable_list *
jnxExampleDataTable_get_next_data_point (
                          void **my_loop_context,
                          void **my_data_context,
                          netsnmp_variable_list *put_index_data,
                          netsnmp_iterator_info *mydata __unused)
{
    jnx_exampled_data_t *entry = (jnx_exampled_data_t *) *my_loop_context;
    netsnmp_variable_list *idx = put_index_data;

    if (entry) {
        snmp_set_var_value(idx, entry->exd_index, strlen(entry->exd_index));
        *my_data_context = (void *) entry;
        *my_loop_context = (void *) ex_data_next(entry);
        return put_index_data;
    } else {
        return NULL;
    }
}


/* Example iterator hook routines - using 'get_next' to do most of the work */
static netsnmp_variable_list *
jnxExampleDataTable_get_first_data_point (void **my_loop_context,
                          void **my_data_context,
                          netsnmp_variable_list *put_index_data,
                          netsnmp_iterator_info *mydata)
{
    *my_loop_context = ex_data_first();
    return jnxExampleDataTable_get_next_data_point(my_loop_context,
                                                   my_data_context,
                                                   put_index_data,
                                                   mydata);
}

/* handles requests for the jnxExampleDataTable table */
static int
jnxExampleDataTable_handler (
    netsnmp_mib_handler               *handler __unused,
    netsnmp_handler_registration      *reginfo __unused,
    netsnmp_agent_request_info        *reqinfo,
    netsnmp_request_info              *requests)
{

    netsnmp_request_info       *request;
    netsnmp_table_request_info *table_info;
    jnx_exampled_data_t *table_entry;

    switch (reqinfo->mode) {
    case MODE_GET:
        for (request=requests; request; request=request->next) {
            table_entry = (jnx_exampled_data_t *)
                          netsnmp_extract_iterator_context(request);
            table_info  = netsnmp_extract_table_info(request);
    
            if (table_entry == NULL)
                continue;

            switch (table_info->colnum) {
            case COLUMN_JNX_EXAMPLE_DATADESCRIPTION:
                snmp_set_var_typed_value(request->requestvb, ASN_OCTET_STR,
                                         table_entry->exd_descr,
                                         strlen(table_entry->exd_descr));
                break;
            case COLUMN_JNX_EXAMPLE_DATATYPE:
                snmp_set_var_typed_value(request->requestvb, ASN_INTEGER,
                                         (const u_char *) &table_entry->exd_type,
                                         sizeof(table_entry->exd_type));
                break;
            case COLUMN_JNX_EXAMPLE_DATAVALUE:
                snmp_set_var_typed_value(request->requestvb, ASN_OCTET_STR,
                                         table_entry->exd_value,
                                         strlen(table_entry->exd_value));
                break;
            }
        }
        break;

    }
    return SNMP_ERR_NOERROR;
}

/* Initialize the jnxExampleDataTable table by defining its contents and how it's structured */
static void
initialize_table_jnxExampleDataTable (void)
{
    static oid jnxExampleDataTable_oid[] = {1,3,6,1,4,1,2636,5,5,1,1,1};
    size_t jnxExampleDataTable_oid_len   = OID_LENGTH(jnxExampleDataTable_oid);
    netsnmp_handler_registration    *reg;
    netsnmp_iterator_info           *iinfo;
    netsnmp_table_registration_info *table_info;

    reg = netsnmp_create_handler_registration(
              "jnxExampleDataTable",   jnxExampleDataTable_handler,
              jnxExampleDataTable_oid, jnxExampleDataTable_oid_len,
              HANDLER_CAN_RONLY
              );

    table_info = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);
    netsnmp_table_helper_add_indexes(table_info,
                           ASN_OCTET_STR,  /* index: jnxExampleDataName */
                           0);
    table_info->min_column = COLUMN_JNX_EXAMPLE_DATADESCRIPTION;
    table_info->max_column = COLUMN_JNX_EXAMPLE_DATAVALUE;
    
    iinfo = SNMP_MALLOC_TYPEDEF(netsnmp_iterator_info);
    iinfo->get_first_data_point = jnxExampleDataTable_get_first_data_point;
    iinfo->get_next_data_point  = jnxExampleDataTable_get_next_data_point;
    iinfo->table_reginfo        = table_info;
    
    netsnmp_register_table_iterator(reg, iinfo);
}

/* Initializes the jnxExampleMIB module */
void
init_jnxExampleMIB (void)
{
  /* here we initialize all the tables we're planning on supporting */
    initialize_table_jnxExampleDataTable();
}


static int
handle_jnxExampleNetSnmpVersion (
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
                                 (const u_char *) netsnmp_version,
                                 strlen(netsnmp_version));
        break;
    default:
        /* we should never get here, so this is a really bad error */
        return SNMP_ERR_GENERR;
    }

    return SNMP_ERR_NOERROR;
}


/* Initializes the jnxExampleNetSnmpVersion module */
void
init_jnxExampleNetSnmpVersion(void)
{
    static oid jnxExampleNetSnmpVersion_oid[] = { 1,3,6,1,4,1,2636,5,5,1,1,2 };

    netsnmp_register_scalar(
        netsnmp_create_handler_registration("jnxExampleNetSnmpVersion",
                                            handle_jnxExampleNetSnmpVersion,
                                            jnxExampleNetSnmpVersion_oid,
                                            OID_LENGTH(jnxExampleNetSnmpVersion_oid),
                                            HANDLER_CAN_RONLY));
}


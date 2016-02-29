/*
 * $Id: jnx-ifinfod_snmp.c 346460 2009-11-14 05:06:47Z ssiano $
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
#include <agent/snmpd.h>

#include <net-snmp/library/snmp_jnx_ext.h>

#include <isc/eventlib.h>
#include <snmp/snmp_pathnames.h>

#include "jnx-ifinfod_snmp.h"
#define DNAME_JNX_IFINFOD "jnx-ifinfod"

static void
netsnmp_ds_set_init (void)
{
    netsnmp_ds_set_string(NETSNMP_DS_LIBRARY_ID,
                          NETSNMP_DS_LIB_APPTYPE, DNAME_JNX_IFINFOD);

    netsnmp_ds_set_string(NETSNMP_DS_APPLICATION_ID, 
                          NETSNMP_DS_AGENT_X_SOCKET, PATH_MASTER_AGENTX);

    netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_AGENT_ROLE,
                           SUB_AGENT);
}

void
netsnmp_subagent_init (evContext netsnmp_ctx)
{
    /* setup isc context */
    netsnmp_set_evcontext(netsnmp_ctx);

    /* set netsnmp parameters */
    netsnmp_ds_set_init();

    /* initialize agent */
    init_agent(DNAME_JNX_IFINFOD);

    /* register mib modules */
    init_jnxIfinfoMIB();
    init_jnxIfinfoNetSnmpVersion();
    init_jnxIfinfoInterfacesTable();

    /*
     * start library 
     */
    init_snmp(DNAME_JNX_IFINFOD);

}


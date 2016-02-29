/*
 * $Id: jnx-exampled_trap.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * Copyright (c) 2007, Juniper Networks, Inc.
 * All rights reserved.
 *
 */

#include <net-snmp/net-snmp-config.h>

#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <net-snmp/agent/agent_trap.h>

#include <jnx/junos_trace.h>
#include <jnx/netsnmp_trap.h>

#include "jnx-exampled_trap.h"

#define  jnxExampleMIB_OID    SNMP_OID_ENTERPRISES, 2636, 5, 5, 1
#define  jnxExampleNotifications_OID    jnxExampleMIB_OID, 0
#define  jnxExample_Startup 1

static oid  jnxExampleStartup_oid[] = { jnxExampleNotifications_OID, jnxExample_Startup };

#define  jnxExampleNetSnmpVersion_OID   jnxExampleMIB_OID, 1, 2
static oid  jnxExampleNetSnmpVersion_oid[] = { jnxExampleNetSnmpVersion_OID };

void
exampled_trap_send_startup (void)
{
    netsnmp_variable_list *var_list;
    char *vp;

    vp = strdup(netsnmp_get_version());
    INSIST_ERR(vp != NULL);

    var_list = netsnmp_trap_sdk_request_header(jnxExampleStartup_oid,
                                               OID_LENGTH(jnxExampleStartup_oid));

    snmp_varlist_add_variable(&var_list,
                              jnxExampleNetSnmpVersion_oid,
                              OID_LENGTH(jnxExampleNetSnmpVersion_oid),
                              ASN_OCTET_STR,
                              vp,
                              strlen(vp));

    netsnmp_trap_request_send(var_list);

    free(vp);
    snmp_free_varbind(var_list);

}


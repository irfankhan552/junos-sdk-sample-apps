/*
 * $Id: jnx-ifinfod_conn.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-ifinfod_conn.c - Connection management functions
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
 * @file jnx-ifinfod_conn.c
 * @brief Relating to managing the connections
 *
 * These functions and types will manage the connections.
 */

#include <string.h>
#include <jnx/aux_types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jnx/pconn.h>
#include <jnx/trace.h>
#include <ddl/ddl.h>
#include <jnx/junos_trace.h>
#include "jnx-ifinfod_conn.h"

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
#include <agent/snmpd.h>
#include <net-snmp/library/snmp_jnx_ext.h>
#include <jnx/netsnmp_trap.h>
#include <snmp/snmp_pathnames.h>

#include "jnx-ifinfod_snmp.h"

#include JNX_IFINFOD_OUT_H

#define JNX_IFINFO_SERVER_MAX_CONN    2    /* max # cnxs (1-4-ctrl & 1-4-data) */
#define SNMPTRAP_PORT_NUM             7080

static pconn_server_t     * mgmt_server;   /* the server connection info */
static evContext          ev_ctx;          /* event context */

void ifinfod_trap_send_startup (void);

#define     jnxIfinfoMIB_OID    SNMP_OID_ENTERPRISES, 2636, 5, 5, 2
#define     jnxIfinfoNotifications_OID    jnxIfinfoMIB_OID, 0
#define     jnxIfinfo_Startup 1
static oid  jnxIfinfoStartup_oid[] = { jnxIfinfoNotifications_OID, jnxIfinfo_Startup };
#define     jnxIfinfoNetSnmpVersion_OID   jnxIfinfoMIB_OID, 1, 2
static oid  jnxIfinfoNetSnmpVersion_oid[] = { jnxIfinfoNetSnmpVersion_OID };

void
ifinfod_trap_send_startup (void)
{
    netsnmp_variable_list *var_list;
    char *vp;

    vp = strdup(netsnmp_get_version());
    INSIST_ERR(vp != NULL);

    var_list = netsnmp_trap_sdk_request_header(jnxIfinfoStartup_oid,
					       OID_LENGTH(jnxIfinfoStartup_oid));

    snmp_varlist_add_variable(&var_list,
			      jnxIfinfoNetSnmpVersion_oid,
			      OID_LENGTH(jnxIfinfoNetSnmpVersion_oid),
			      ASN_OCTET_STR,
			      vp,
			      strlen(vp));

    netsnmp_trap_request_send(var_list);

    free(vp);
    snmp_free_varbind(var_list);
}


/**
 * Message handler for open connections.
 *
 * @param[in] session
 *      The session information for the source peer
 *
 * @param[in] msg
 *      The inbound message
 *
 * @param[in] cookie
 *      The cookie we passed in. This is the eventlib context here.
 *
 * @return
 *      SUCCESS if successful.
 */

static status_t
receive_message(pconn_session_t * session __unused,
		ipc_msg_t * msg ,
		void * cookie __unused)
{
    uint32_t hello_num;

    hello_num = ntohl(*((uint32_t *)msg->data));

    junos_trace(JNX_IFINFOD_TRACEFLAG_CONN,
		"%s Received message: %d from control component", __func__, hello_num); 

    /* Now send the sample trap */
    ifinfod_trap_send_startup();

    return SUCCESS;

}

/**
 * Connection handler for new and dying connections
 *
 * @param[in] session
 *      The session information for the source peer
 *
 * @param[in] event
 *      The event (established, or shutdown are the ones we care about)
 *
 * @param[in] cookie
 *      The cookie we passed in. This is the eventlib context here.
 */
static void
receive_connection(pconn_session_t * session __unused,
		   pconn_event_t event,
		   void * cookie __unused)
{
    switch (event) {
    case PCONN_EVENT_ESTABLISHED:
	junos_trace(JNX_IFINFOD_TRACEFLAG_CONN,
		    "%s Received PCONN_EVENT_ESTABLISHED event from control component", __func__); 
	break;
    case PCONN_EVENT_SHUTDOWN:
	junos_trace(JNX_IFINFOD_TRACEFLAG_CONN,
		    "%s Received PCONN_EVENT_SHUTDOWN event,"
		    "connection from control component is shutdown", __func__); 
	break;
    default:
	ERRMSG(JNX_IFINFOD_LOG_TAG, TRACE_LOG_ERR,
	       "%s: Received an unknown or PCONN_EVENT_FAILED event",
	       __func__);
    }
}

/**
 * Initialize the server socket connection
 *
 * @param[in] ctx
 *      event context
 *
 * @return
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_server(evContext ctx)
{
    pconn_server_params_t params;

    ev_ctx = ctx;

    bzero(&params, sizeof(pconn_server_params_t));

    /* setup the server args */
    params.pconn_port            = SNMPTRAP_PORT_NUM;
    params.pconn_max_connections = JNX_IFINFO_SERVER_MAX_CONN;
    params.pconn_event_handler   = receive_connection;
 
    /* bind */
    mgmt_server = pconn_server_create(&params, ctx, receive_message, NULL);

    if (mgmt_server == NULL) {
	ERRMSG(SNMPTRAP_MGMT_LOG_TAG, TRACE_LOG_ERR,
	       "%s: Failed to initialize the pconn server on port %d.",
	       __func__, SNMPTRAP_PORT_NUM);
	return EFAIL;
    }

    return SUCCESS;
}

/**
 * Close existing connections and shutdown server
 */
void
close_connections(void)
{
    if (mgmt_server) {
	pconn_server_shutdown(mgmt_server);
	mgmt_server = NULL;
    }
}

/*
 * $Id: jnx-gateway-mgmt_main.c 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-mgmt_main.c - main & signal handlers 
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyrignt (c) 2005-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdlib.h>
#include <sys/param.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>

#include <isc/eventlib.h>
#include <jnx/bits.h>
#include <jnx/vrf_util_pub.h>
#include <jnx/parse_ip.h>
#include <ddl/ddl.h>
#include <jnx/junos_init.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/pconn.h>
#include <jnx/junos_kcom.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>
#include JNX_GATEWAY_MGMT_OUT_H
#include JNX_GATEWAY_MGMT_SEQUENCE_H

#include <jnx/jnx-gateway.h>
#include <jnx/jnx-gateway_msg.h>

#include "jnx-gateway-mgmt.h"
#include "jnx-gateway-mgmt_config.h"

extern const parse_menu_t master_menu[];
extern const char *jnx_gw_config_path[];

jnx_gw_mgmt_t jnx_gw_mgmt;

static void
jnx_gw_mgmt_sigterm_handler(int signo __unused);

/**
 * jnx_gw_mgmt_sigterm_handler
 * handler for SIGTERM signal
 */
static void
jnx_gw_mgmt_sigterm_handler(int signo __unused)
{
    jnx_gw_mgmt_cleanup(jnx_gw_mgmt.ctxt);
    return; 
}

/**
 * jnx_gw_mgmt_init
 * Initializes jnx-gw-management module
 */
static int
jnx_gw_mgmt_init(evContext ctxt)
{
    memset(&jnx_gw_mgmt, 0, sizeof(jnx_gw_mgmt));

    logging_set_level(LOG_INFO);

    jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_INIT, LOG_INFO,
                    "Initializing Gateway");

    /* store the context */
    jnx_gw_mgmt.ctxt = ctxt;

    /* ignore sigpipe signal */
    junos_sig_register(SIGPIPE, SIG_IGN);

    junos_sig_register(SIGTERM, jnx_gw_mgmt_sigterm_handler);

    /* initialize the configuration sub-unit */
    jnx_gw_mgmt_config_init();

    /* initialize the kcom ifl event handlings */
    jnx_gw_mgmt_kcom_init(ctxt); 

    /*
     * initialize server connection setup
     * for listening to control/data application
     * agent register messages
     */
    jnx_gw_mgmt_conn_init(ctxt);

    jnx_gw_mgmt.status = JNX_GW_MGMT_STATUS_INIT;

    return TRUE;
}

/**
 * jnx_gw_mgmt_cleanup
 * cleans up jnx-gw-management module
 */
boolean
jnx_gw_mgmt_cleanup(evContext ctxt)
{
    /* initialize the configuration sub-unit */
    jnx_gw_mgmt_config_cleanup();

    /* initialize the kcom ifl event handlings */
    jnx_gw_mgmt_kcom_cleanup(ctxt); 

    /* initialize server pcon accept setup
     * for listening to control/data application
     * agent connections
     */
    jnx_gw_mgmt_conn_cleanup(ctxt);

    jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_INIT, LOG_INFO,
                    "Gateway shutdown done");

    return TRUE;
}

/**
 * main
 * Registers & spawns the gateway management daemon on RE
 */
int
main (int argc, char **argv)
{
    int ret = 0;
    junos_sdk_app_ctx_t ctx;
    
    /* create an application context */
    ctx = junos_create_app_ctx(argc, argv, DNAME_JNX_GATEWAYD,
                 master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    /* set config read call back */
    if ((ret = junos_set_app_cb_config_read(ctx, 
                                            jnx_gw_mgmt_config_read)) < 0) {
        goto cleanup;
    }

    /* set init call back */
    if ((ret = junos_set_app_cb_init(ctx, jnx_gw_mgmt_init)) < 0) {
        goto cleanup;
    }
    
    /* set trace options DDL path */
    if ((ret = junos_set_app_cfg_trace_path(ctx, jnx_gw_config_path)) < 0) {
        goto cleanup;
    }

    /* now just call the great junos_app_init */
    ret = junos_app_init(ctx);

cleanup:
    /* should not come here unless daemon exiting or init failed, destroy context */
    junos_destroy_app_ctx(ctx); 
    return ret;
}

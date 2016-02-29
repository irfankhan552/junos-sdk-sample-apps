/*
 * $Id: jnx-flow-mgmt_main.c 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * jnx-flow-mgmt_main.c - main & signal handlers 
 *
 * Vivek Gupta, Aug 2007
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
/**
 * @file : jnx-flow-mgmt_main.c
 * @brief
 * This file contains the jnx-flow-mgmt initiation and cleanup routines
 *
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
#include JNX_FLOW_MGMT_SEQUENCE_H
#include JNX_FLOW_MGMT_OUT_H

#include <jnx/jnx-flow.h>
#include <jnx/jnx-flow_msg.h>

#include "jnx-flow-mgmt.h"
#include "jnx-flow-mgmt_config.h"
#include "jnx-flow-mgmt_kcom.h"

extern const parse_menu_t master_menu[];
extern const char *jnx_flow_config_path[];

const char * jnx_flow_err_str[] = {"no error", "alloc fail", "free fail", "entry op fail", "invalid entry", "entry present", "entry absent", "invalid message", "invalid configration"};
const char * jnx_flow_svc_str[] = {"invalid", "interface", "nexthop" };
const char * jnx_flow_match_str[] = {"invalid", "5tuple"};
const char * jnx_flow_action_str[] = {"invalid", "allow", "drop"};
const char * jnx_flow_dir_str[] = {"invalid", "input", "output"};
const char * jnx_flow_mesg_str[] = {"invalid", "service config", "rule config", "service rule config", "fetch flow", "fetch rule", "fetch service", "clear"};
const char * jnx_flow_config_op_str[] = {"invalid", "add", "delete", "change"};
const char * jnx_flow_fetch_op_str[] = {"invalid", "entry", "summary", "extensive"};
const char * jnx_flow_clear_op_str[] = {"invalid", "all", "entry", "rule", "service", "service type" };

jnx_flow_mgmt_t jnx_flow_mgmt;

/**
 * jnx_flow_mgmt_init
 * Initializes jnx-gw-management module
 */
static int
jnx_flow_mgmt_init(evContext ctxt)
{
    memset(&jnx_flow_mgmt, 0, sizeof(jnx_flow_mgmt));

    /* store the context */
    jnx_flow_mgmt.ctxt = ctxt;

    /*
     * set logging level to info, & to syslog only
     */
    logging_set_level(LOG_INFO);

    /* initialize the configuration databases */
    jnx_flow_mgmt_config_init();

    /* initialize the kcom gencfg event handlings */
    jnx_flow_mgmt_kcom_init(ctxt); 

    /* initialize server connection setup
     * for listening to data application
     * register messages
     */
    jnx_flow_mgmt_conn_init(ctxt);

    jnx_flow_log(JNX_FLOW_INIT, LOG_INFO, "management agent initialized");

    return TRUE;
}

/**
 * jnx_flow_mgmt_shutdown
 * Clears up the application
 */
int
jnx_flow_mgmt_shutdown(evContext ctxt __unused)
{
    exit(0);
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

    /* Create an application context */
    ctx = junos_create_app_ctx(argc, argv, DNAME_JNX_FLOWD,
                               master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    /* Set config read call back */
    if ((ret = junos_set_app_cb_config_read(ctx, jnx_flow_mgmt_config_read))) {
        goto cleanup;
    }

    /* Set init call back */
    if ((ret = junos_set_app_cb_init(ctx, jnx_flow_mgmt_init))) {
        goto cleanup;
    }
    
    /* set trace options DDL path */
    if ((ret = junos_set_app_cfg_trace_path(ctx, jnx_flow_config_path)) < 0) {
        goto cleanup;
    }

    /* Calling junos_app_init */
    ret = junos_app_init(ctx);

cleanup:
    /* Destroying context if daemon init/exit failed */
    junos_destroy_app_ctx(ctx);
    return ret;
}

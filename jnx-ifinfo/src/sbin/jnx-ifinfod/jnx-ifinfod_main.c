/*
 * $Id: jnx-ifinfod_main.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-ifinfod_main.c - handles main and signal handlers
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2005-2008, Juniper Networks, Inc.
 * All rights reserved.   
 */

/**
 *
 * @file  jnx-ifinfod_main.c
 * @brief Handles main () and daemon specific initialiation fucntions. 
 *
 */

#include <isc/eventlib.h>
#include <jnx/bits.h>

#include <ddl/ddl.h>

#include JNX_IFINFOD_OUT_H
#include JNX_IFINFOD_SEQUENCE_H

#include <jnx/junos_init.h>

#include <string.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>

#include "jnx-ifinfod_config.h"
#include "jnx-ifinfod_kcom.h"
#include "jnx-ifinfod_snmp.h"
#include "jnx-ifinfod_conn.h"

#define DNAME_JNX_IFINFOD "jnx-ifinfod"

/*
 * ifinfo master menu structure
 */
extern const parse_menu_t master_menu[];


/**
 *
 *Function : ifinfod_init
 *Purpose  : This calls daemon specific initialization function calls 
 *           such as kcom subsystem and configuration
 *           initilization functions.
 *
 * @param[in] ctxt
 *       Event context
 *
 *
 * @return 0 on success or 1 on failure
 */
static int 
ifinfod_init (evContext ctx)
{
    ifinfod_config_init();
    netsnmp_subagent_init(ctx); /* initialize snmp subagent */
    init_server(ctx);           /* initialize server socket connection */

    return (ifinfod_kcom_init(ctx) != 0);
}


/**
 *
 *Function : main
 *Purpose  : main function invokes junos_daemon_init API and 
 *           initializes ifinfo daemon with necessary info.
 *
 * @param[in] argc
 *       Standard argument count
 *
 * @param[in] argv
 *       Standard arguments array
 *
 * @return 1 on exit
 */
int
main (int argc, char **argv)
{
    int ret = 0;
    junos_sdk_app_ctx_t ctx;
    const char *if_trace_path[] = {"jnx-ifinfo", NULL};
    
    /* create an application context */
    ctx = junos_create_app_ctx(argc, argv, DNAME_JNX_IFINFOD,
                 master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    /* set config read call back */
    if ((ret = junos_set_app_cb_config_read(ctx, ifinfod_config_read)) < 0) {
        goto cleanup;
    }

    /* set init call back */
    if ((ret = junos_set_app_cb_init(ctx, ifinfod_init)) < 0) {
        goto cleanup;
    }
    
    /* set trace options DDL path */
    if ((ret = junos_set_app_cfg_trace_path(ctx, if_trace_path)) < 0) {
        goto cleanup;
    }

    /* now just call the great junos_app_init */
    ret = junos_app_init(ctx);

cleanup:
    /* should not come here unless daemon exiting or init failed, destroy context */
    junos_destroy_app_ctx(ctx); 
    return ret;
}

/*end of this file */

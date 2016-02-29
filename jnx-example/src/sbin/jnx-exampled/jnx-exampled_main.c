/*
 * $Id: jnx-exampled_main.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-exampled_main.c - main and signal handlers.
 *
 * Copyright (c) 2005-2008, Juniper Networks, Inc.
 * All rights reserved.
 */


#include <isc/eventlib.h>

#include <jnx/bits.h>

#include <ddl/ddl.h>
#include JNX_EXAMPLED_OUT_H
#include JNX_EXAMPLED_SEQUENCE_H
#include <jnx/junos_init.h>
#include <jnx/junos_trace.h>

#include <string.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>

#include "jnx-exampled.h"
#include "jnx-exampled_config.h"
#include "jnx-exampled_kcom.h"
#include "jnx-exampled_snmp.h"


extern const parse_menu_t master_menu[];
extern const char *ex_config_name[];

static int
jnx_exampled_init (evContext ctx)
{
    junos_trace(JNX_EXAMPLED_TRACEFLAG_INIT, "%s called", __func__);

    ex_data_init();

    /* initialize snmp components */
    netsnmp_subagent_init(ctx);

    return (jnx_exampled_kcom_init(ctx) != 0);
}

static int
jnx_exampled_cmd_opts(char c, char * arg)
{
    /* this is just a stub, expand on opts processing later */
    ERRMSG(JNX_EXAMPLED_COMMAND_OPTIONS, LOG_INFO,
           "jnx_exampled_cmd_opts called with option %u arg %s", 
           (unsigned int)c, arg);    
    return 0;
}

/******************************************************************************
 * Function : main
 * Purpose  : Intializes jnx-exampled's environment.
 *****************************************************************************/
int
main (int argc, char **argv)
{
    int ret = 0;
    junos_sdk_app_ctx_t ctx;

    /* create an application context */
    ctx = junos_create_app_ctx(argc, argv, DNAME_JNX_EXAMPLED,
                 master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    /* set config read call back */
    if ((ret = junos_set_app_cb_config_read(ctx, 
                                            jnx_exampled_config_read)) < 0) {
        goto cleanup;
    }

    /* set init call back */
    if ((ret = junos_set_app_cb_init(ctx, jnx_exampled_init)) < 0) {
        goto cleanup;
    }

    /* set cmd-line opts call back */
    if ((ret = junos_set_app_cb_cmd_line_opts(ctx, 
                                              jnx_exampled_cmd_opts)) < 0) {
        goto cleanup;
    }

    /* set trace options DDL path */
    if ((ret = junos_set_app_cfg_trace_path(ctx, ex_config_name)) < 0) {
        goto cleanup;
    }

    /* now just call the great junos_app_init */
    ret = junos_app_init(ctx);

cleanup:
    /* should not come here unless daemon exiting or init failed, destroy context */
    junos_destroy_app_ctx(ctx); 
    return ret;
}

/* end of file */

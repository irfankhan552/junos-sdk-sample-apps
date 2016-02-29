/*
 * $Id: jnx-msprsmd_main.c 418048 2010-12-30 18:58:42Z builder $
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file
 *     jnx-msprsmd_main.c
 * @brief
 *     'main' and initialization routines.
 *
 * This file contains routines for appropriate initialization
 * operations necessary for this daemon.
 *
 */

#include <string.h>
#include <errno.h>
#include <sys/queue.h>
#include <isc/eventlib.h>
#include <netinet/in.h>

#include <sys/socket.h>
#include <sys/queue.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include <ddl/ddl.h>

#include JNX_MSPRSM_OUT_H
#include JNX_MSPRSM_SEQUENCE_H

#include <jnx/bits.h>
#include <jnx/junos_init.h>
#include <jnx/ssd_ipc_msg.h>
#include <jnx/aux_types.h>
#include <jnx/provider_info.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>
#include <jnx/pconn.h>
#include <jnx/name_len_shared.h>

#include "jnx-msprsmd.h"
#include "jnx-msprsmd_config.h"

/*
 * Global Data
 */
extern const parse_menu_t master_menu[];
extern const char *config_path_msprsmd[];
evContext event_ctx;

/*
 * Global Variable(s)
 */

#define DNAME_JNX_MSPRSMD "jnx-msprsmd" /* Name of daemon */


/**
 * @brief
 *     jnx-msprsm daemon initialization API
 *
 * This API will be called as a callback by the @a junos_daemon_init
 * function.
 *
 * @param[in] ctx
 *     The event context associated with the sdk application
 *
 * @return 0 always in this example
 */
static int
msprsmd_main_init (evContext ctx)
{
    event_ctx = ctx;

    return 0;
}


/**
 * @brief
 * 'main' function for the application.
 *
 * Here, we call the @a junos_app_init with appropriate parameters after
 * setting up the context for our application. It performs required
 * initialization operations.
 *
 * @param[in] argc
 *     Number of arguments passed to @c main
 * @param[in] argv
 *     Pointer to array of argument values passed to @c main()
 *
 * @return 0 on success or error on failure
 */
int
main (int argc, char **argv)
{
    int ret = 0;
    junos_sdk_app_ctx_t ctx;

    /* create an application context */
    ctx = junos_create_app_ctx(argc, argv, DNAME_JNX_MSPRSMD,
                 master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    /* set config read call back */
    if ((ret = junos_set_app_cb_config_read(ctx, msprsmd_config_read)) < 0) {
        goto cleanup;
    }

    /* set init call back */
    if (junos_set_app_cb_init(ctx, msprsmd_main_init)) {
        goto cleanup;
    }
    
    /* set trace options DDL path */
    if ((ret = junos_set_app_cfg_trace_path(ctx, config_path_msprsmd)) < 0) {
        goto cleanup;
    }

    /* now just call the great junos_app_init */
    ret = junos_app_init(ctx);

cleanup:
    /* should not come here unless daemon exiting or init failed, destroy context */
    junos_destroy_app_ctx(ctx);
    return ret;
}

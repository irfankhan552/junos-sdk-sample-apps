/*
 * $Id: jnx-routeserviced_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file 
 *     jnx-routeserviced_main.c
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

#include <ddl/ddl.h>

#include JNX_ROUTESERVICED_OUT_H
#include JNX_ROUTESERVICED_SEQUENCE_H

#include <jnx/bits.h>
#include <jnx/junos_init.h>
#include <jnx/ssd_ipc.h>
#include <jnx/ssd_ipc_msg.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/provider_info.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>

#include "jnx-routeserviced_ssd.h"
#include "jnx-routeserviced_config.h"
#include "jnx-routeserviced_gencfg.h"

/*
 * Global Data
 */
extern const parse_menu_t master_menu[];
extern int jnx_routeserviced_ssd_fd;

/*
 * Global Variable(s)
 */
evContext jnx_routeserviced_event_ctx;  /* Context for the daemon */

#define DNAME_JNX_ROUTESERVICED "jnx-routeserviced" /* Name of daemon */

/**
 * @brief
 *     jnx-routeservice daemon initialization API
 *
 * This API will be called as a callback by the @a junos_daemon_init
 * function. It does the basic initializations and also creates
 * a session connection with state server daemon. 
 *
 * @param[in] ctx
 *     The event context associated with the sdk application
 *
 * @return 
 *     0 indicating success; exits with status -1 otherwise 
 */

static int 
jnx_routeserviced_init (evContext ctx)
{
    provider_id_t provider_id;
    int error;

    /* 
     * Store the context in the base structure
     */
    jnx_routeserviced_event_ctx = ctx;

    /*
     * Initialize patricia tree
     */
    rt_data_init();

    /*
     * Catch SIGTERM and clean up Gencfg client-id blob
     */
    junos_sig_register(SIGTERM, jnx_routeserviced_gencfg_delete_client_id);

    /* 
     * Use the unique provider id for the kcom init
     */
    error = provider_info_get_provider_id(&provider_id); 
    if (error) {
        ERRMSG(JNX_ROUTESERVICED_GET_PROV_ID_FAIL, LOG_ERR,
               "Failed to get provider ID: %s", strerror(errno));
    } 

    error = junos_kcom_init(provider_id, jnx_routeserviced_event_ctx);

    if (error != KCOM_OK) {
        ERRMSG(JNX_ROUTESERVICED_KCOM_INIT_FAIL, LOG_ERR,
               "kcom initialization failure with return code %d", error);
    }

    /*
     * Initialize the JUNOS gencfg subsystem here
     */
    error = jnx_routeserviced_gencfg_init();
    if (error != KCOM_OK) {
        ERRMSG(JNX_ROUTESERVICED_KCOM_GENCFG_FAIL, LOG_ERR, 
               "gencfg subsystem initialization failed with cause %d",
               error);
    }

    /* 
     * Call the SSD initialization routine 
     */
    error = jnx_routeserviced_ssd_init(jnx_routeserviced_event_ctx);
    if (error != 0) {
        ERRMSG(JNX_ROUTESERVICED_SSD_CONN_FAIL, LOG_ERR,
               "connection failure with SSD with return code %d", error);

        /*
         * Start a reconnection timer instead of exiting. Trace it.
         */
        junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                    " SSD connection failed. Shall retry in few seconds...",
                    __func__);

        evSetTimer(jnx_routeserviced_event_ctx, 
                   jnx_routeserviced_ssd_reconnect,
                   NULL, evAddTime(evNowTime(), evConsTime(5,0)),
                   evConsTime(0,0), NULL);

        junos_kcom_shutdown();
        exit(-1);
    }
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
 */

int
main (int argc, char **argv)
{
    int ret = 0;
    junos_sdk_app_ctx_t ctx;
    const char *jnx_routeserviced_config_name[] = { DDLNAME_JNX_ROUTESERVICE, 
                                                    NULL };
    
    /* create an application context */
    ctx = junos_create_app_ctx(argc, argv, DNAME_JNX_ROUTESERVICED,
                 master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    /* set config read call back */
    if ((ret = junos_set_app_cb_config_read(ctx, 
                                        jnx_routeserviced_config_read)) < 0) {
        goto cleanup;
    }

    /* set init call back */
    if ((ret = junos_set_app_cb_init(ctx, jnx_routeserviced_init)) < 0) {
        goto cleanup;
    }
    
    /* set trace options DDL path */
    ret = junos_set_app_cfg_trace_path(ctx, jnx_routeserviced_config_name);
    if (ret < 0) {
        goto cleanup;
    }

    /* now just call the great junos_app_init */
    ret = junos_app_init(ctx);

cleanup:
    /* should not come here unless daemon exiting or init failed, destroy context */
    junos_destroy_app_ctx(ctx); 
    return ret;
}

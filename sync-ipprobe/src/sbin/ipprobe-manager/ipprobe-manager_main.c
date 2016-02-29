/*
 * $Id: ipprobe-manager_main.c 366969 2010-03-09 15:30:13Z taoliu $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file ipprobe-manager_main.c
 * @brief Contains main entry point
 *
 * Contains the main entry point and registers the application
 * as a JUNOS daemon
 */

/**
\mainpage

\section intro_sec Introduction

This is the probe manager and responder running in the probe target.
The manager
- listens to the connection from the probe initiator,
- receives the request from the probe initiator and replies,
- create responder for the requested probe,
- closes the responder when the probe is done.

The responder
- receives probe packets from the probe initiator,
- puts timestamp into the packet when reveiving it and before sending it out,
- sends probe packets back to the probe initiator.

*/

#include "ipprobe-manager.h"

#include IPPROBE_SEQUENCE_H

#define DNAME_PROBE_MANAGER "ipprobe-manager"

/** The interval between each two heartbeats, in second. */
#define PMON_HB_INTERVAL    4

/** The number of successive heartbeats allowed to loss. */
#define PMON_HB_GRACE       8

/*** Data Structures ***/

/**
 * Path to traceoptions configuration 
 */
extern const char *probe_manager_config_path[];


/** The global event context. */
evContext manager_ev_ctx;

/** The context for process monitor. */
static junos_pmon_context_t pmon_ctx = PMON_CONTEXT_INVALID;

/*** STATIC/INTERNAL Functions ***/

/**
 * Close the connection to pmond.
 */
static void
manager_pmon_close (void)
{
    junos_pmon_destroy_context(&pmon_ctx);
    return;
}

/**
 * Signal handler for termination
 *
 * @param[in] signo
 *      signal number
 */
static void
sigterm_handler (int signo UNUSED)
{
    manager_close();
    manager_pmon_close();
    manager_auth_close();
    return;
}

/**
 * Intialize and open the connection to pmond,
 * schedule sending heartbeat to pmond.
 *
 * @return 0 on success, -1 on failure
 */
static int
manager_pmon_open (void)
{
    struct timespec interval;

    pmon_ctx = junos_pmon_create_context();
    if (pmon_ctx == PMON_CONTEXT_INVALID) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Create pmon context!", __func__);
        return -1;
    }

    if (junos_pmon_set_event_context(pmon_ctx, manager_ev_ctx) < 0) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Set pmon context!", __func__);
        return -1;
    }

    if (junos_pmon_select_backend(pmon_ctx, PMON_BACKEND_LOCAL, NULL) < 0) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Select pmon backend!", __func__);
        return -1;
    }

    interval = evConsTime(PMON_HB_INTERVAL, 0);
    if (junos_pmon_heartbeat(pmon_ctx, &interval, PMON_HB_GRACE) < 0) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Schedule sending heatbeat!", __func__);
        return -1;
    }
    return 0;
}

/**
 * Intializes the manager
 *
 * @param[in] ev_ctx
 *      Event context
 *
 * @return 0 on success, -1 on failure
 */
static int
manager_main_init (evContext ev_ctx)
{
    manager_ev_ctx = ev_ctx;

    manager_init();

    if (manager_pmon_open() < 0) {
        return -1;
    }

    if (manager_auth_open() < 0) {
        return -1;
    }
    return 0;
}

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * The main function of the manager.
 *
 * Register user callback functions and initialize daemon environment.
 *
 * @param[in] argc
 *      Number of command line arguments
 *
 * @param[in] argv
 *      String array of command line arguments
 *
 * @return never return upon success or return upon failure
 */
int
main(int argc, char **argv)
{
    int ret = 0;
    junos_sdk_app_ctx_t sdk_app_ctx;

    /* Create an application context. */
    sdk_app_ctx = junos_create_app_ctx(argc, argv, DNAME_PROBE_MANAGER,
            NULL, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (sdk_app_ctx == NULL) {
        return -1;
    }

    junos_sig_register(SIGTERM, sigterm_handler);

    /* Register config read call back. */
    ret = junos_set_app_cb_config_read(sdk_app_ctx, manager_config_read);
    if (ret < 0) {
        goto cleanup;
    }

    /* Register init call back. */
    ret = junos_set_app_cb_init(sdk_app_ctx, manager_main_init);
    if (ret < 0) {
        goto cleanup;
    }
    
    /* set trace options DDL path */
    ret = junos_set_app_cfg_trace_path(sdk_app_ctx, probe_manager_config_path);
    if (ret < 0) {
        goto cleanup;
    }
    

    /* Start application initialization. */
    ret = junos_app_init(sdk_app_ctx);

cleanup:
    /* Should not come here unless daemon exiting or init failed. */
    junos_destroy_app_ctx(sdk_app_ctx);
    return ret;
}

/*
 * $Id: packetproc-data_main.c 365138 2010-02-27 10:16:06Z builder $
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
 * @file packetproc-data_main.c
 * @brief Contains main entry point
 *
 * Contains the main entry point and registers the application
 * as a MultiServices PIC daemon.
 */

/* The Application and This Daemon's Documentation: */

/**

\mainpage

\section intro_sec Introduction
This is an MP-SDK data application running on the MultiServices PIC
as a daemon. It creates an idle timer event needed by the event loop
(which cannot be empty). It uses one of three different ways of creating
a data loop on the data CPU. These options are as follows:

Option 1:  Create data loops with the function msp_data_create_loops,
    which is a single function call to create data loops on all data CPUs.
    It returns an error if any data CPU is unavailable.

Option 2:  Create a data loop with the function msp_data_create_loop_on_cpu,
    which creates a data loop on the specified single data CPU.

Option 3:  Create a data loop as a thread and allocate a FIFO directly with
    pthread library functions and FIFO functions.

*/

#include "packetproc-data.h"

extern packet_thread_t packet_thrd[];

/*** STATIC/INTERNAL Functions ***/
static evTimerID  packetproc_idle_timer;

/**
 * Idle function, does nothing.
 */
static void
packetproc_idle(evContext ctx UNUSED, void * uap UNUSED,
            struct timespec due UNUSED, struct timespec inter UNUSED)
{
    /* Do nothing here, just keep the main process running. */
    return;
}

/**
 * This function quits the application by calling the exit function.
 */
static void 
packetproc_data_quit(int signo __unused)
{
    int cpu;

    /* Send SIGUSR1 to all packet/data threads so they shutdown */
    for (cpu = 0; cpu < MSP_MAX_CPUS; cpu++) {
        if (packet_thrd[cpu].thrd_tid) {
            logging(LOG_INFO, "%s: killing thread %d...", __func__, cpu);
            pthread_kill(packet_thrd[cpu].thrd_tid, SIGUSR1);
        }
    }

    /* Wait for all threads closed. */
    for (cpu = 0; cpu < MSP_MAX_CPUS; cpu++) {
        if (packet_thrd[cpu].thrd_tid) {

            /* Wait here till the thread was killed. */
            pthread_join(packet_thrd[cpu].thrd_tid, NULL);

            packet_thrd[cpu].thrd_tid = NULL;
        }
    }
    logging(LOG_INFO, "%s: packetproc shutting down...", __func__);

    msp_exit();
    exit(0);
}

/**
 * Callback for the first initialization of the SDK application.
 * 
 * @param[in] ctx
 *     Newly created event context 
 * 
 * @return 0 upon successful completion
 */
static int
packetproc_data_init(evContext ctx)
{
    int ret, cpu;

    /* Initialize environment for SDK data path application. */
    ret = msp_init();
    if(ret != MSP_OK) {
        return ret;
    }

    /* Initialize signal handlers. */
    signal(SIGTERM, packetproc_data_quit);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Set logging level and mode, default values are
     * LOG_INFO and LOGGING_SYSLOG respectively.
     */
    logging_set_level(LOG_INFO);
    logging_set_mode(LOGGING_SYSLOG);

    for(cpu = 0; cpu < MSP_MAX_CPUS; cpu++) {
        packet_thrd[cpu].thrd_tid = 0;
    }
    
    init_packet_loop();

    logging(LOG_INFO, "%s: Create packet loops OK!", __func__);

    /* Set idle timer to keep main process running. */
    packetproc_idle_timer.opaque = NULL;
    if(evSetTimer(ctx, packetproc_idle, NULL, evConsTime(0, 0),
            evConsTime(120, 0), &packetproc_idle_timer)) {
        logging(LOG_ERR, "%s: Initialize the idle timer ERROR!",
                __func__);
        return MSP_ERROR;
    }
    return 0;
}

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Initializes environment for packetproc-data.
 * 
 * @param[in] argc
 *     Number of command line arguments
 * 
 * @param[in] argv
 *     String array of command line arguments
 * 
 * @return 0 upon successful exit of the application (should not happen)
 *     or 1 upon failure
 */
int
main(int argc, char** argv)
{
    mp_sdk_app_ctx_t app_ctx;
    int rc;

    app_ctx = msp_create_app_ctx(argc, argv, "packetproc-data");
    msp_set_app_cb_init(app_ctx, packetproc_data_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


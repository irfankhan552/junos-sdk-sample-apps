/*
 * $Id: ipsnooper_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ipsnooper_main.c
 * @brief Contains main entry point
 *
 */

/* The Application and This Daemon's Documentation: */

/**

\mainpage

The application has three components: server process, packet thread
and session thread.

Server process is the main process running on control CPUs. It
  - listens to the connection request from client,
  - creates session thread if it doesn't exist,
  - sends message to session thread by server pipe to add a new session.

Packet threads are running on packet CPUs, one on each, to retrieve
requested information from the traffic. They send traffic information to
all session threads by packet pipe.

Session threads are running on control CPUs, one on each, to send traffic
information to connected clients. One session thread could handle multiple
client sessions who are tied to the control CPU by control flow affinity.

*/

#include "ipsnooper.h"

ssn_thrd_t ssn_thrd[MSP_MAX_CPUS];  /**< session thread data structures */
pkt_thrd_t pkt_thrd[MSP_MAX_CPUS];  /**< packet thread data structures */

static sigset_t old_sig_mask;       /**< signal mask backup */

/*** STATIC/INTERNAL Functions ***/

/**
 * The SIGQUIT signal handler.
 *
 * This is a global signal handler for both packet threads
 * and session threads. The SIGQUIT signal is blocked to main
 * process but open to packet and session threads.
 *
 * @param[in] signo
 *      Signal number
 */
static void
thread_exit (int signo UNUSED)
{
    pthread_t tid;
    int cpu;

    /* Find out who I am. */
    tid = pthread_self();

    for (cpu = 0; cpu < MSP_MAX_CPUS; cpu++) {
        if (tid == pkt_thrd[cpu].pkt_thrd_tid) {
            packet_thread_exit(&pkt_thrd[cpu]);
        }
        if (tid == ssn_thrd[cpu].ssn_thrd_tid) {
            session_thread_exit(&ssn_thrd[cpu]);
        }
    }
}

/**
 * This function quits the application by calling the exit function.
 *
 * @param[in] signo
 *      Signal number
 */
static void
main_exit (int signo UNUSED)
{
    int cpu;

    /* Kill all packet threads. */
    for (cpu = 0; cpu < MSP_MAX_CPUS; cpu++) {
        if (pkt_thrd[cpu].pkt_thrd_tid == NULL) {
            continue;
        }
        syslog(LOG_INFO, "%s: Kill packet thread %d...",
                __func__, cpu);
        pthread_kill(pkt_thrd[cpu].pkt_thrd_tid, SIGQUIT);
    }

    /* Wait for all packet threads closed. */
    for (cpu = 0; cpu < MSP_MAX_CPUS; cpu++) {
        if (pkt_thrd[cpu].pkt_thrd_tid == NULL) {
            continue;
        }

        if (pthread_join(pkt_thrd[cpu].pkt_thrd_tid, NULL) == 0) {
            syslog(LOG_INFO, "%s: Packet thread %d was killed.",
                    __func__, cpu);
        } else {
            syslog(LOG_ERR, "%s: Packet thread %d was NOT killed! %d",
                    __func__, cpu, errno);
        }
        pkt_thrd[cpu].pkt_thrd_tid = NULL;
    }

    /* Close the server, it also kills all session threads. */
    server_close();

    syslog(LOG_INFO, "%s: Bye bye...", __func__);

    sigprocmask(SIG_SETMASK, &old_sig_mask, NULL);
    msp_exit();
    exit(0);
}

/**
 * Callback for the first initialization of the SDK application.
 *
 * @param[in] ctx
 *      Event context created by event library
 *
 * @return 0 on success, -1 on failure
 */
static int
main_init (evContext ctx)
{
    int ret = 0;
    sigset_t sig_mask;

    /* Initialize environment for SDK data application. */
    ret = msp_init();
    if (ret != MSP_OK) {
        syslog(LOG_ERR, "%s: msp_init FAILED! %d", __func__, ret);
        return -1;
    }

    /* Block SIGQUIT to main process. */
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGQUIT);
    pthread_sigmask(SIG_BLOCK, &sig_mask, &old_sig_mask);

    /* Initialize signal handlers. */
    signal(SIGTERM, main_exit);
    signal(SIGQUIT, thread_exit);
    signal(SIGHUP, SIG_IGN);

    /* This signal is delivered when writing to the pipe who's other
     * end has been closed.
     */
    signal(SIGPIPE, SIG_IGN);

    /* Set logging level and mode, default values are
     * LOG_INFO and LOGGING_SYSLOG respectively.
     */
    logging_set_level(LOG_INFO);
    logging_set_mode(LOGGING_SYSLOG);

    /* Initialize IP Snooper server. */
    if (server_init(ctx) < 0) {
        syslog(LOG_ERR, "%s: server_init FAILED!", __func__);
        return -1;
    }

    /* Initialize packet loops to inspect traffic. */
    if (packet_loop_init() < 0) {
        syslog(LOG_ERR, "%s: Packet_loop_init FAILED!", __func__);
        server_close();
        return -1;
    }

    return 0;
}

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Initializes environment for IP Snooper.
 *
 * @param[in] argc
 *     Number of command line arguments
 *
 * @param[in] argv
 *     String array of command line arguments
 *
 * @return 0 on successful exit (should not happen), 1 on failure
 */
int
main (int argc, char** argv)
{
    mp_sdk_app_ctx_t app_ctx;
    int rc;

    app_ctx = msp_create_app_ctx(argc, argv, "ipsnooper");
    msp_set_app_cb_init(app_ctx, main_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


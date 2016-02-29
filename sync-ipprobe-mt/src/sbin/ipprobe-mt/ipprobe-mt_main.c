/*
 * $Id: ipprobe-mt_main.c 347265 2009-11-19 13:55:39Z kdickman $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2009, Juniper Networks, Inc.
 * All rights reserved.
 */

/** 
 * @file ipprobe-mt_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application
 * as a JUNOS daemon 
 */

/* The Application Documentation: */

/**
 
@mainpage

@section overview 1 Overview

This is a sample application to send, respond and receive probe IP packets,
and measure network statistics based on time stamps in the probe packets.

The IP Probe MT application runs on RE and contains three components, the probe
manager, probe threads and responder manager.

The probe manager is responsible for reading CLI configuration, processing CLI
commands and managing probe threads.

The probe thread sends probe packets out, reveives replied probe packets from
the remote responder, calculate statistics and generate probe report.

The responder manager is responsible for managing the responders.

@verbatim
                 +--------+          +-----------+
              +--| Probe  |<-------->| Probe     |--+
              |  | Thread |          | Responder |  |
              |  +--------+          +-----------+  |
 +---------+  |       .                    .        |  +-----------+
 | Probe   |--+       .                    .        +--| Responder |
 | Manager |  |       .                    .        |  | Manager   |
 +---------+  |       .                    .        |  +-----------+
              |  +--------+          +-----------+  |
              +--| Probe  |<-------->| Probe     |--+
                 | Thread |          | Responder |
                 +--------+          +-----------+
@endverbatim
 
@section functionality 2 Functionality

@subsection configuration 2.1 Configuration

IP Probe MT configuration is under [edit sync ipprobe-mt].
pprobe-mt {
    probe <probe name> {
        protocol <IP protocol>;
        packet-size <packet size in byte>;
        packet-count <packet count>;
        packet-interval <packet interval>;
        destination-port <destination port>;
        source-port <source port>;
        tos <TOS value in IP header>;
    }
}

@subsection command 2.2 Command


@subsection manage-probe-threads 2.3 Manage Probe Threads

The probe manager is responsible for managing probe threads. After receiving
a "start probe" command from user, the probe manager either starts a new
thread to run the probe to requested destination if this probe is not running
by any thread, or sends command to the thread that is currently running this
probe to add the requested destination.

@subsection send-and-receive-packets 2.4 Send and Receive Probe Packets

The probe thread is responsible for sending and receiving probe packets.
Before start sending probe packets out, it has to connect to the responder
manager on the destination and tell it some probe information. Once the thread
gets the ACK, it starts sending probe packets out as what user defined in probe
configuration, receives the replied probe packets from the responder and
calculate time differences to generate statstics.

The probe thread exits by itself once the probe is done to all destinations.
The statistics report will stay till user delete it by CLI command.

@subsection manage-responder 2.5 Manage Responder

The responder manager is running on the destination. It opens a responder when
receiving the probe request from the probe thread on the source and
ackwnoledges the request.

@subsection respond-packets 2.6 Respond Probe Packets

The responder receives the probe packet from the source, adds time stamps into
the packet and sends it back.

After replied all probe packets, the responder closes itself.

*/

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <isc/eventlib.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/pconn.h>
#include <jnx/junos_trace.h>
#include <jnx/junos_init.h>
#include "ipprobe-mt.h"
#include IPPROBE_MT_OUT_H
#include IPPROBE_MT_SEQUENCE_H

extern const parse_menu_t master_menu[];

static junos_sdk_app_ctx_t app_ctx;

/**
 * @brief
 * Close and exit.
 * 
 */
static void
main_exit (void)
{
    probe_mngr_close();
    rspd_mngr_close();
    junos_destroy_app_ctx(app_ctx); 
    exit(0);
}

/**
 * @brief
 * Callback for the first initialization of the RE-SDK application
 * 
 * @param[in] ctx
 *      Newly created event context 
 * @return
 *      0 on success, -1 on failure
 */
static int
main_init (evContext ev_ctx)
{
    PROBE_TRACE(PROBE_TF_NORMAL, "%s: Initializing...", __func__);
    probe_mngr_init(ev_ctx);
    rspd_mngr_init(ev_ctx);
    return 0;
}

/**
 * @brief
 * Intializes ipprobe-mt environment.
 * 
 * @param[in] argc
 *      Number of command line arguments
 * @param[in] argv
 *      String array of command line arguments
 * @return
 *      0 on successful exit of the application (shouldn't happen),
 *      non-zero on failure
 */
int
main (int argc, char **argv)
{
    const char *config_path[] = { "sync", "ipprobe-mt", NULL };
    int rc = 0;

    app_ctx = junos_create_app_ctx(argc, argv, "ipprobe-mt",
            master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (app_ctx == NULL) {
        return -1;
    }

    rc = atexit(main_exit);
    if (rc < 0) {
        goto ret_err;
    }
    rc = junos_set_app_cb_config_read(app_ctx, config_read);
    if (rc < 0) {
        goto ret_err;
    }

    rc = junos_set_app_cb_init(app_ctx, main_init);
    if (rc < 0) {
        goto ret_err;
    }

    rc = junos_set_app_cfg_trace_path(app_ctx, config_path);
    if (rc < 0) {
        goto ret_err;
    }

    rc = junos_app_init(app_ctx);

ret_err:
    /* should not come here unless daemon exiting or init failed,
     * destroy context
     */
    return rc;
}


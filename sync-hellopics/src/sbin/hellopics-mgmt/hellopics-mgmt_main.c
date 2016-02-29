/*
 * $Id: hellopics-mgmt_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file hellopics-mgmt_main.c
 * @brief Contains main entry point
 *
 * Contains the main entry point and registers the application as a JUNOS daemon
 */

/* The Application and This Daemon's Documentation: */

/**

\mainpage

\section overview_sec Overview

This is a sample application that is part of the sync-hellopics-apps package
and one of the three parts to the hellopics application which demonstrates
simple JUNOS MP-SDK functionality, with a focus on component communication.
There are generally (but not by requirement) three components to an MP-SDK
application. Two components run on the Multi-Services PIC, namely the data
and control components. These components only have access to the MP-SDK
libraries. The third component, called the management component, runs on the
routing engine (RE) and is in fact an RE-SDK application that may simply
communicate with the other two. It only has access to the RE-SDK libraries.
Of course some libraries are part of both sets like libconn for example.

The purpose of the hellopics application is to pass hello messages in a cycle
between the three components. The management component operates on a
60-second interval timer. It initiates a hello message carrying a sequence
number as data upon the timer expiry event. The first hello message is sent
to the data component, the second to the control component, and the direction
remains alternating.

hellopics-mgmt is a small daemon running on the RE. It is a good starting
point for beginners to the JUNOS MP-SDK. It demonstrates the usual RE-SDK
libraries, and it provides the 'show sync hellopics statistics' command to
show some basic statistics about the hello messages. It does a good job of
demonstrating libconn to communicate between an MP-SDK application's
components.

The management component in particular starts a server listening for a
connection from the data component and from the control component. Upon
establishing a session through this server connection, the session is
unidentified. The first message it expects for a new session is the one
containing the ID of the component that started the session. It will not
process any messages from a connection (session) until it is identified.
Once this happens, it responds to peer-information-request messages by
sending the opposite peer's information. If a connection from the opposite
peer has not been made, it will send the information when it connects. It
also listens for messages indicating that the other components are ready to
receive hello messages. The other components only send these when they are
acting as clients for a connection and they are fully ready. When the
management component has received a ready message from both components it
starts the interval timer to start generating the hello messages.

If the management component is about to send a hello message before the last
one was received, then it increments a messages-missed counter. Upon sending
the hello message, it increments a messages-sent counter. If a hello message
is received and its sequence number matches the last one sent, then it
increments a messages-missed counter. Lastly, if a hello message is received
and its sequence number does not match the last one sent, then it increments
a messages-out-of-order counter. The messages received out of order would,
therefore, also get counted as missed. These four statistics counters are
reported by the command made available by this daemon.

High Availability functionality has been added to the management component.
The purpose of this functionality is to demonstrate the continuation of
RE service even after the Master RE fails or there is a mastership
switchover.
The management component on the Master RE is the master daemon (all the
processing is done by this daemon) and the management component on the
Backup RE is the backup daemon (this daemon remains passive).
The backup daemon just synchronizes its state with the master daemon.
If Routing Engine mastership switchover/failover takes place then the
management component needs to be notified about the mastership switchover.
Here the backup component takes over as master and the master becomes
backup.
\verbatim
user@router> show sync hellopics statistics
Hello PICs Cycled-Message Statistics:
  Sent Message: 2
  Received Messages: 2
  Dropped Messages: 0
  Missed Messages Received Out of Order: 0
\endverbatim

*/

#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/mgmt_sock_pub.h>
#include <jnx/junos_init.h>
#include <jnx/junos_trace.h>
#include "hellopics-mgmt_logging.h"
#include "hellopics-mgmt_config.h"
#include "hellopics-mgmt_conn.h"
#include "hellopics-mgmt_ha.h"

#include HELLOPICS_OUT_H
#include HELLOPICS_SEQUENCE_H

/*** Constants ***/
extern volatile boolean is_master;

/**
 * Constant string for the daemon name
 */
#define DNAME_HELLOPICS_MGMT    "hellopics-mgmt"

/*** Data Structures ***/

/**
 * CMD Mode menu tree of commands:
 * (This is defined at the end of hellopics-mgmt_ui.c)
 */
extern const parse_menu_t master_menu[];


/*** STATIC/INTERNAL Functions ***/
static void hellopics_mastership_switch(bool master);
evContext present_ctx;

/**
 * Callback for the first initialization of the RE-SDK Application
 *
 * @param[in] ctx
 *     Newly created event context
 *
 * @return
 *      SUCCESS if successful; otherwise EFAIL with an error message
 */
static int
hellopics_init (evContext ctx)
{
    init_connection_stats();
    present_ctx = ctx;
    init_config();
    return init_server(ctx);
}


/*** GLOBAL/EXTERNAL Functions ***/



/**
 * Intializes hellopics-mgmt's environment
 *
 * @param[in] argc
 *     Number of command line arguments
 *
 * @param[in] argv
 *     String array of command line arguments
 *
 * @return 0 upon successful exit of the application (shouldn't happen)
 *         or non-zero upon failure
 */
int
main (int argc, char ** argv)
{
    const char * hp_config[] = {DDLNAME_SYNC, DDLNAME_SYNC_HELLOPICS, NULL};
    int ret = 0;
    junos_sdk_app_ctx_t ctx;

    /* Create an application context */
    ctx = junos_create_app_ctx(argc, argv, DNAME_HELLOPICS_MGMT,
                               master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
         return -1;
    }

    /* Set config read call back */
    if ((ret = junos_set_app_cb_config_read(ctx, hellopics_config_read))) {
        goto cleanup;
    }

    /* Set init call back */
    if ((ret = junos_set_app_cb_init(ctx, hellopics_init))) {
        goto cleanup;
    }

    /* set trace options DDL path */
    ret = junos_set_app_cfg_trace_path(ctx, hp_config);
    if (ret < 0) {
        goto cleanup;
    }

    if ((ret = junos_set_app_cb_mastership_switch(ctx,
                    hellopics_mastership_switch))) {
        goto cleanup;
    }
    /* Calling junos_app_init */
    ret = junos_app_init(ctx);

cleanup:
	if (ret != 0)
        LOG(LOG_ERR, "%s: Clean up before the exit ", __func__);

    /* Destroying context if daemon init/exit failed */
    junos_destroy_app_ctx(ctx);
    return ret;
}
/**
 * Callback function, called when the RE switches mastership and 
 * the mastership notification happens.
 *
 * @param[in] master
 *     Indicates master or backup.
 *
 * @return SUCCESS 
 */
static void
hellopics_mastership_switch(bool master __unused)
{
   LOG(LOG_INFO, "%s: Switching the daemon mastership", __func__);
   kill(getpid(),SIGTERM);
}

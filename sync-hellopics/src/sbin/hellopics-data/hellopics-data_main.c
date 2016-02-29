/*
 * $Id: hellopics-data_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file hellopics-data_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as an MSP daemon 
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

hellopics-data is a small daemon running on the data plane (the fast path) 
and intended to be simple. It is a good starting point for beginners to the 
JUNOS MP-SDK. It demonstrates the minimal amount of API usage from libmp-sdk.
In fact, it processes no traffic at all. It does a good job of demonstrating 
libconn to communicate between an MP-SDK application's components. Because it is
a data component-style application (hence the name), it does call msp_init 
and msp_exit to prepare for and wrap up typical data-path-application operations
(like setting up data loops). These are not officially needed here since this
application does not start any data loops, but it is included for demonstration
purposes, serving as a basic starting point for your own application
development where data loops will eventually be created.

The data component in particular starts two client connections. One, first, 
to the management component. Upon establishing this connection, it sends its 
ID and a request for its peer's connection information. The management 
component replies with its peer's connection information (that of the control
component). Upon receiving this information, the data component opens the 
second client connection to the control component. Upon establishing this 
connection, it sends its ID. It then sends a message to both other 
components to inform them that it is ready to receive and pass hello 
messages in the cycle. If the data component receives a hello message from 
the management component it forwards it to the control component and 
vice-versa.

*/

#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <isc/eventlib.h>
#include <jnx/mpsdk.h>
#include <jnx/aux_types.h>
#include "hellopics-data_conn.h"

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_HELLOPICS_DATA    "hellopics-data"

/*** Data Structures ***/


/*** STATIC/INTERNAL Functions ***/


/**
 * This function quits the application does an exit
 */
static void 
hellopics_data_quit(int signo __unused)
{
    close_connections();
    msp_exit();
}


/**
 * Callback for the first initialization of the RE-SDK Application
 * 
 * @param[in] ctx
 *     Newly created event context 
 * 
 * @return SUCCESS (0) upon successful completion
 */
static int
hellopics_data_init (evContext ctx)
{
    msp_init();
    
    // Handle some signals that we may receive:
    msp_sig_register(SIGTERM, hellopics_data_quit); // call quit fnc
    signal(SIGHUP, SIG_IGN); // ignore
    signal(SIGPIPE, SIG_IGN); // ignore
    
    return init_connections(ctx);
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Intializes hellopics-data's environment
 * 
 * @param[in] argc
 *     Number of command line arguments
 * 
 * @param[in] argv
 *     String array of command line arguments
 * 
 * @return 0 upon successful exit of the application (shouldn't happen)
 *         or 1 upon failure
 */
int
main(int32_t argc , char **argv)
{
    mp_sdk_app_ctx_t app_ctx;
    int rc;

    app_ctx = msp_create_app_ctx(argc, argv, DNAME_HELLOPICS_DATA);
    msp_set_app_cb_init(app_ctx, hellopics_data_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


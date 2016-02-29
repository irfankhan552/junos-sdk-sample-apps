/*
 * $Id: hellopics-ctrl_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file hellopics-ctrl_main.c
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

hellopics-ctrl is a small daemon running on the MS PIC and intended to be 
simple. It is a good starting point for beginners to the JUNOS MP-SDK. It 
demonstrates the minimal amount of API usage from libmp-sdk. It does a good 
job of demonstrating libconn to communicate between an MP-SDK application's 
components.

The control component in particular starts a client connection and a server. 
It is a client to the management component. Upon establishing this 
connection, it sends its ID. It is also a server to the data component. 
When the data component connects to it, it will receive the data component's 
ID. After, it should receive a message from the data component, indicating it
is ready. When the control component has both sent its ID to the management 
component and received the ready message from the data component, it then 
sends its ready message to the management component indicating that it is 
also ready to receive and pass hello messages. If the control component 
receives a hello message from the management component it forwards it to the 
data component and vice-versa.

*/

#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <isc/eventlib.h>
#include <jnx/mpsdk.h>
#include <jnx/aux_types.h>
#include <jnx/logging.h>
#include "hellopics-ctrl_conn.h"

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_HELLOPICS_CTRL    "hellopics-ctrl"


/*** Data Structures ***/


/*** STATIC/INTERNAL Functions ***/


/**
 * This function quits the application upon a SIGTERM signal
 */
static void 
hellopics_ctrl_quit(int signo __unused)
{
    close_connections();
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
hellopics_ctrl_init (evContext ctx)
{
    msp_control_init();
    
    // Ignore some signals that we may receive:
    msp_sig_register(SIGTERM, hellopics_ctrl_quit);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    
    logging_set_mode(LOGGING_SYSLOG);
    
    return init_connections(ctx);
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Intializes hellopics-ctrl's environment
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

    app_ctx = msp_create_app_ctx(argc, argv, DNAME_HELLOPICS_CTRL);
    msp_set_app_cb_init(app_ctx, hellopics_ctrl_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


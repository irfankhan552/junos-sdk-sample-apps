/*
 * $Id: helloworldd_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file helloworldd_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as a JUNOS daemon 
 */ 

/* The Application and This Daemon's Documentation: */

/** 

\mainpage

\section overview_sec Overview

This is a sample application that is part of the sync-apps package which 
demonstrates JUNOS RE-SDK functionality.

helloworldd is a small daemon that is intended to be simpler than 
jnx-example. It is a good starting point for newbies to JUNOS daemons. It 
demonstrates DDL, ODL, DAX and the minimal amount of APIs from libjunos-sdk.
It also uses patricia trees from libjuniper to store the configuration.

This application simply allows a message to be configured and provides a 
command to print that message back out.

*/


#include <string.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/mgmt_sock_pub.h>
#include <jnx/junos_init.h>
#include "helloworldd_config.h"

#include HELLOWORLD_SEQUENCE_H

/**
 * Constant string for the daemon name
 */
#define DNAME_HELLOWORLDD    "helloworldd"

/*** Data Structures ***/

/**
 * CMD Mode menu tree of commands:
 * (This is defined at the end of helloworldd_ui.c)
 */
extern const parse_menu_t master_menu[];


/*** STATIC/INTERNAL Functions ***/

/**
 * Callback for the first initialization of the RE-SDK Application
 * 
 * @param[in] ctx
 *     Newly created event context 
 * 
 * @return SUCCESS (0) upon successful completion
 */
static int
helloworld_init (evContext ctx __unused)
{
    init_messages_ds();
    return SUCCESS;
}


/*** GLOBAL/EXTERNAL Functions ***/



/**
 * Intializes helloworldd's environment
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
    int ret = 0;
    junos_sdk_app_ctx_t ctx;

    /* Create an application context */
    ctx = junos_create_app_ctx(argc, argv, DNAME_HELLOWORLDD,
                               master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    /* Set config read call back */
    if ((ret = junos_set_app_cb_config_read(ctx, helloworld_config_read))) {
        goto cleanup;
    }

    /* Set init call back */
    if ((ret = junos_set_app_cb_init(ctx, helloworld_init))) {
        goto cleanup;
    }

    /* Calling junos_app_init */
    ret = junos_app_init(ctx);

cleanup:
    /* Destroying context if daemon init/exit failed */
    junos_destroy_app_ctx(ctx);
    return ret;
}


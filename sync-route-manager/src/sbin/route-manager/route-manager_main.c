/*
 * $Id$
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 */

/** 
 * @file route-manager_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application
 * as a JUNOS daemon 
 */

/* The Application and This Daemon's Documentation: */

/**
 
@mainpage

@section overview 1 Overview

Route Manager is a sample application to demonstrate all functionalities
provided by libssd.

Routes are configured by CLI. After configuration is committed, Route Manager
will update routes in the routing table according to configuration change.

@section configuration 2 Configuration

@verbatim
route-manager {
    routes {
        <destination with prefix length> {
            next-hop-type [unicast | service | routing-table | discard |
                           reject];
            preference <preference>;
            routing-table <routing table name>;
            state [no-install | no-advertise];
            flag [overwrite];
            gateway <gateway name> {
                address <gateway IPv4 address>;
                interface-name <interface name>;
                interface-index <interface index>;
                interface-address <interface address>;
                next-hop <next-hop interface name or routing table name>;
            }
        }
    }
}
@endverbatim

@section command 2 Command

There is no command for Route Manager. Junos operational command "show route"
can be used to check routes in routing table.

@section workflow 3 Working Flow

@subsection start 3.1 Start or Restart

When Route Manager starts, it initializes all components and loads
configuration in the first cycle.

  - Initialization
    - Initialize configuration and KCOM.
    - Load GENCFG blob that stores client ID.
    - Connect to SSD.

  - Load Configuration
    Daemon starts, all routes are new to the daemon, verify and load them to
    local database, and mark them all add-pending.

After the first cycle, the callbacks for SSD reply will take care of the
rest job.

  - Connect reply
    If the connection is setup with the old client ID in restart case, routing
    service will be restored, so no more actions. Otherwise, if a new client
    ID is assigned, request routing service.

  - Routing servce reply
    SSD service is ready, walk through local database to add routes one by one.
    Next-hop will be added or routing table ID will be requested before adding
    route, if it's needed.

  - Adding next-hop reply
    Next-hop is added successfully, conitnue adding route. Otherwise, mark
    route add-error.

  - Routing table ID reply
    Get routing table ID successfully, continue adding route. Otherwise, mark
    route add-error.
  
  - Adding route reply
    Route is added successfully, mark it add-ok. Otherwise, mark it add-error.

@subsection start 3.2 Configuration Commit

After configuration commit, Route Manager will get SIGHUP signal to read
configuration and update local database. In this cycle (signal handler),
Route Manager
  - add route to local database and mark it add-pending if there is new route
    in configuration,
  - mark route delete-pending in local database if the route is deleted from
    configuration,
  - add new route to local database and mark it add-pending, and mark the old
    route delete-pending, if the route is changed in configuration.
 
Then walk through the local database to add or delete routes.

*/


#include <stdlib.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <isc/eventlib.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>
#include <jnx/junos_init.h>
#include <jnx/ssd_ipc_msg.h>
#include "route-manager.h"

#include OUT_H
#include SEQUENCE_H

/*** Constants ***/

/*** Data Structures ***/

extern const parse_menu_t master_menu[];

/*** STATIC/INTERNAL Functions ***/

/**
 * @brief
 * Close and exit.
 * 
 */
static void
main_exit (void)
{
    RM_TRACE(TF_NORMAL, "%s: Route Manager exits.", __func__);
    ssd_close();
    config_clear();
    kcom_close();
    exit(0);
}

/**
 * @brief
 * SIGTERM signal handler.
 *
 * @param[in] signo
 *     Always SIGTERM (ignored)
 */
static void
sigterm (int signo UNUSED)
{
    main_exit();
}

/**
 * @brief
 * Callback for the first initialization
 * 
 * @param[in] ctx
 *     Newly created event context 
 * 
 * @return
 *     0 on success, -1 on failure
 */
static int
main_init (evContext ctx)
{
    RM_TRACE(TF_NORMAL, "%s: Start initializing.", __func__);

    junos_sig_register(SIGTERM, sigterm);
    config_init();
    kcom_init(ctx);
    ssd_open(ctx);

    return 0;
}


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * @brief
 * Intializes environment and starts event loop.
 * 
 * @param[in] argc
 *     Number of command line arguments
 * @param[in] argv
 *     String array of command line arguments
 * 
 * @return
 *     0 on successful exit of the application (shouldn't happen),
 *     non-zero on failure
 */
int
main (int argc, char **argv)
{
    int rc = 0;
    junos_sdk_app_ctx_t app_ctx;
    const char *config_path[] = { "sync", "route-manager", NULL };

    app_ctx = junos_create_app_ctx(argc, argv, "route-manager", NULL,
            PACKAGE_NAME, SEQUENCE_NUMBER);
    if (app_ctx == NULL) {
        return -1;
    }

    rc = junos_set_app_cb_config_read(app_ctx, config_read);
    if (rc < 0) {
        goto error;
    }

    rc = junos_set_app_cb_init(app_ctx, main_init);
    if (rc < 0) {
        goto error;
    }

    rc = junos_set_app_cfg_trace_path(app_ctx, config_path);
    if (rc < 0) {
        goto error;
    }

    rc = junos_app_init(app_ctx);

error:
    /* should not come here unless daemon exiting or init failed,
     * destroy context
     */
    junos_destroy_app_ctx(app_ctx); 
    return rc;
}


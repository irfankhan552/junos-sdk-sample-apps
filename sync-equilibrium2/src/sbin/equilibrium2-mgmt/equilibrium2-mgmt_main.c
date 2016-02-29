/*
 * $Id: equilibrium2-mgmt_main.c 431556 2011-03-20 10:23:34Z sunilbasker $
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
 * @file equilibrium2-mgmt_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application
 * as a JUNOS daemon 
 */

/* The Application and This Daemon's Documentation: */

/**
 
@mainpage

@section overview 1 Overview

This is a sample application that is part of the sync-equilibrium2 package.

The Equilibrium II application overall consists of the development of a 
basic load-balancing system designed to operate quickly in the network data
path on the Juniper Networks MultiServices PIC hardware module. The system
function will be comparable to that of a reverse-proxy load balancer designed
to be deployed adjacent to the server cluster for added performance and high
availability.

The Equilibrium II application contains a manager running on RE and two
services, classify service and balance service, running on MS-PIC.

The manager is responsible for reading CLI configuration, sending configuration
to services and querying the status of services.

The purpose of classify and balance is to allow user access all kinds of
Internet services with one single virtual IP address. The classify service
classifies the traffic by TCP port and direct it to the proper virtual service
gate. The balance service then balances the traffic to a group of real servers.

@verbatim
                                   |-> service
   user                 classify   |    gate     balance  |-> server 1
   traffic   virtual    service    |             service  |
 ----------> service --------------|-> service -----------|-> server 2
             address               |    gate              |
                                   |                      |-> server 3
                                   |-> service
                                        gate
@endverbatim
 
@section functionality 2 Functionality

@subsection config 2.1 Configuration

@subsection eq2_config 2.1.1 Equilibrium II Configuration

Equilibrium II configuration is under [edit sync equilibrium2].

@verbatim
service-gate {
    gate-name {
        address service-gate-IP-address;
    }
server-group {
    group-name {
        servers {
            server-IP-address;
        }
    }
}
service-type {
    type-name {
        port TCP-port;
    }
}
balance-rules {
    rule rule-name {
        term term-name {
            from {
                service-gate service-gate-name;
            }
            then {
                server-group server-group-name;
            }
        }
    }
}
classify-rules {
    rule rule-name {
        term term-name {
            from {
                service-type service-type-name;
            }
            then {
                service-gate service-gate-name;
            }
        }
    }
}
@endverbatim

[edit service-gate] specifies the virtual gate IP address of the service.
@n[edit service-type] specifies the type of Internet service.
@n[edit server-group] specifies the addresses of a group of servers.
@n[edit balance-rules] specifies the rules of balance service. Each rule
contains a list of terms that consists of matching condition and action.
@n[edit classify-rules] specifies the rules of classify service.

@subsection service-set_config 2.1.2 Service-set Configuration

@verbatim
services {
    service-set svc-set-name {
        interface-service {
            service-interface service-interface-name;
        }
        extension-service equilibrium2-balance {
            rule balance-rule-name;
        }
        extension-service equilibrium2-classify {
            rule classify-rule-name;
        }
        service-order {
            forward-flow [ equilibrium2-classify equilibrium2-balance ];
            reverse-flow [ equilibrium2-balance equilibrium2-classify ];
        }
    }
}
@endverbatim

[edit services service-set extention-service equilibrium2-balance]
specifies the balance service rules that are defined in
[edit sync equilibrium2 balance-rules].
@n[edit services service-set extention-service equilibrium2-classify]
specifies the classify service rules that are defined in
[edit sync equilibrium2 classify-rules].
@n[edit services service-set service-order] specifies the order of services for
forwarding flow and reversed flow.

@subsection read_config 2.2 Read Configuration

After the CLI configuration was committed, the manager reads [edit sync
equilibrium2], [services service-set extension-service equilibrium2-balance]
and [services service-set extension-service equilibrium2-classify], if anything
was changed in there.

@subsection read_config 2.3 Receive SSRB

A JUNOS service daemon reads all service-set configuration, generates
Service-Set Resolution Blob (SSRB) for each service-set and pushes them to
kernel by gencfg. The Equilibrium II manager subscribes to receive SSRB.


@subsection send_config 2.4 Create Service-set Blob for Service

The Equilibrium II manager needs both CLI configuration and SSRB to create
service-set blobs and send them to services. SSRB is received asynchronously.
This means that some SSRBs may be received before reading configuration, and
some my be received after.

After receiving a SSRB, the manager checks the services configured in this
service-set first. The SSRB will be discarded if it doesn't contain Equilibrium
II service. Then the manager checks the list of service-set blobs, that are
created after reading configuration. If the service-set blob doesn't exist,
which means this SSRB was received before reading configuration, then the
manager just adds it to the SSRB list. Otherwise, the manager incorporate
SSRB information into service-set blob and sends service-set blob to service.

After reading CLI configuration, when the manager creates service-set blob for
service, it checks the SSRB list. If the SSRB exists, which means this SSRB
has already been received, then the manager incoporate SSRB information into
service-set blob and send it to service. After the service-set blob was sent
out, it's removed from the list. If the SSRB doesn't exit, which means this
SSRB was not received yet, then the manager leaves the service-set blob in the
list.

@subsection send_config 2.5 Send Service-set Blob to Service

The service-set blob is sent to service by adding it to kernel with gencfg API.
The service-set blob is for each service and contains only the specific service
rules. For example, service-set @p svc-set-a is configured and it contains both
classify and balance service rules. Then two service-set blobs will be created
with the same service-set ID, one contains classify rules only, another contains
balance rules only. They will be sent to the proper service respectively.

The service-set blob is only sent to the specified service interface.

Before sending service-set blob out, the manager compares the newly created
blob and the blob already in kernel, and only updates the blob if anything was
changed. To update a service-set blob, the manager has to delete it first, then
add the new one.

@subsection get_status 2.6 Get Service Status

An operational CLI command can be used to get service status. For now, it shows
the number of sessions connected to each server for each server group.

@verbatim
> show sync equilibrium2 status

  Server Group: server-group-name
      Address        Number of Sessions
    xxx.xxx.xxx.xxx        xxx
@endverbatim

*/

#include <sync/equilibrium2.h>
#include "equilibrium2-mgmt.h"

#include <stdlib.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>
#include <jnx/junos_init.h>
#include <jnx/pmon.h>

#include EQUILIBRIUM2_OUT_H
#include EQUILIBRIUM2_SEQUENCE_H

/*** Constants ***/

/*** Data Structures ***/

extern const parse_menu_t master_menu[];

static junos_pmon_context_t pmon; /**< health monitoring handle */

/*** STATIC/INTERNAL Functions ***/

/**
 * @brief
 * Close and exit.
 * 
 */
static void
main_exit (void)
{
    server_close();
    kcom_close();
    config_clear();
    junos_pmon_destroy_context(&pmon);

    EQ2_TRACE(EQ2_TRACEFLAG_NORMAL, "%s: Equilibrium2 exit.", __func__);
    exit(0);
}

/**
 * @brief
 * Shutdown app upon a SIGTERM.
 *
 * @param[in] signo
 *      Always SIGTERM (ignored)
 */
static void
eq2_sigterm (int signo UNUSED)
{
    main_exit();
}

/**
 * @brief
 * Start server upon resync completion.
 *
 * @param[in] ctx
 *      App event context
 */
static void
eq2_start_server (void* ctx)
{
    evContext ev = *(evContext*)ctx;
    if (server_open(ev)) {
        EQ2_LOG(LOG_ERR, "%s: server_open failed", __func__);
        main_exit();
    }
}

/**
 * @brief
 * Callback for the first initialization of the RE-SDK application
 * 
 * @param[in] ctx
 *      Newly created event context 
 * 
 * @return
 *      0 on success, -1 on failure
 */
static int
main_init (evContext ctx)
{
    pmon = NULL;
    struct timespec interval;
    provider_info_origin_description_t desc;

    EQ2_TRACE(EQ2_TRACEFLAG_NORMAL, "%s: Start initializing.", __func__);

    junos_sig_register(SIGTERM, eq2_sigterm);

    provider_info_get_origin_id(&eq2_origin_id);
    eq2_provider_id = provider_info_get_provider_id_from_origin_id(
            eq2_origin_id, desc);
    EQ2_TRACE(EQ2_TRACEFLAG_NORMAL, "%s: Provider ID 0x%08x.", __func__,
            eq2_provider_id);

    config_init();

    if (kcom_init(ctx) != KCOM_OK) {
        goto error;
    }

    if ((pmon = junos_pmon_create_context()) == PMON_CONTEXT_INVALID) {
        EQ2_LOG(LOG_ERR, "%s: Initialize health monitoring ERROR!",
                __func__);
        goto error;
    }
    if (junos_pmon_set_event_context(pmon, ctx)) {
        EQ2_LOG(LOG_ERR, "%s: Setup health monitoring ERROR!",
                __func__);
        goto error;
    }
    if (junos_pmon_select_backend(pmon, PMON_BACKEND_LOCAL, NULL)) {
        EQ2_LOG(LOG_ERR, "%s: Select health monitoring backend ERROR!",
                __func__);
        goto error;
    }

    interval.tv_sec = PMON_HB_INTERVAL;
    interval.tv_nsec = 0;

#if 0
    if (junos_pmon_heartbeat(pmon, &interval, 0)) {
        EQ2_LOG(LOG_ERR, "%s: Set health monitoring heartbeat ERROR!",
                __func__);
        goto error;
    }
#endif

    if (junos_kcom_resync(eq2_start_server,&ctx) != KCOM_OK) {
        EQ2_LOG(LOG_ERR, "%s: KCOM resync failed", __func__);
        goto error;
    }
    return 0;

error:
    kcom_close();
    config_clear();
    if (pmon) {
        junos_pmon_destroy_context(&pmon);
    }

    return -1;
}


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * @brief
 * Intializes equilibrium2-mgmt's environment
 * 
 * @param[in] argc
 *      Number of command line arguments
 * 
 * @param[in] argv
 *      String array of command line arguments
 * 
 * @return
 *      0 on successful exit of the application (shouldn't happen),
 *      non-zero on failure
 */
int
main (int argc, char **argv)
{
    int ret = 0;
    junos_sdk_app_ctx_t app_ctx;
    const char *eq2_config[] = { "sync", "equilibrium2", NULL };

    app_ctx = junos_create_app_ctx(argc, argv, DNAME_EQUILIBRIUM2_MGMT,
            master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (app_ctx == NULL) {
        return -1;
    }

    ret = junos_set_app_cb_config_read(app_ctx, eq2_config_read);
    if (ret < 0) {
        goto error;
    }

    ret = junos_set_app_cb_init(app_ctx, main_init);
    if (ret < 0) {
        goto error;
    }

    ret = junos_set_app_cfg_trace_path(app_ctx, eq2_config);
    if (ret < 0) {
        goto error;
    }

    ret = junos_app_init(app_ctx);

error:
    /* should not come here unless daemon exiting or init failed,
     * destroy context
     */
    junos_destroy_app_ctx(app_ctx); 
    return ret;
}


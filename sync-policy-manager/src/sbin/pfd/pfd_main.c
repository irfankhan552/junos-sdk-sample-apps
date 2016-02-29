/*
 * $Id: pfd_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file pfd_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as a MSP daemon 
 */ 

/* The Application and This Daemon's Documentation: */

/**

\mainpage

\section intro_sec Introduction

The Policy Manager sample application composed of two daemons on the routing 
engine (RE), the Policy Server Daemon (PSD) and the Policy Enforcement Daemon 
(PED). Both daemons will be added to the sync-policy-manager-mgmt package, which
for the fictitious company SDK Your Net Corp. (SYNC), demonstrates how to use 
the correct naming conventions in developing a package to hold RE-SDK daemons. 
As of the 8.5 package release, the sample application also contains two daemons 
running on the MS-PIC or even separate PICs optionally. There is the Packet 
Filtering Daemon (PFD) running as a data application and a Captive Portal Daemon
(CPD), which is a simple HTTP server running as a control application. The whole
application containing all four daemons is contained in the 
sync-policy-manager-bundle package.

The goal of this application is to demonstrate the use of some JUNOS SDK APIs. 
It covers the use of DDL and ODL, respectively, to manage configuration and 
commands, and to control the output of operational commands. It uses event 
control from the eventlib API in libisc2 (a library provided by the Internet 
Software Consortium) to exemplify its use with sockets to provide asynchronous 
communication services. It demonstrates the use of libjunos-sdk in several ways:
Kernel Communication (KCOM) is used to listen for protocol family changes on 
interfaces, and tracing and logging happens using the APIs exposed in the 
junos_trace module. This application also demonstrates the use of libjipc for 
inter-process communication to and from the PSD. Libssd, a major SDK library 
that communicates with the SDK Service Daemon (SSD), is used to manage routes 
associated with policies and install service routes to MS-PICs. Lastly, the 
application demonstrates writing control and data applications for the MP-SDK 
using libconn and libmp-sdk, performing PIC-PIC and RE-PIC communications.

\section pfd_sec Packet Filtering Daemon (PFD)

The PFD is a Services-SDK data application responsible for controlling which 
subscribers (end users) behind a PED-managed interface are allowed to have their
packets routed or not. The case when the traffic is disallowed is handled by 
redirecting traffic to the captive portal (the CPD). Filtering is done based on 
the source IP address and the destination port number. Specifically, we filter 
traffic from all source IP addresses coming in with a destination port of 80, 
the HTTP port.

<b>Note about the PFD implementation:</b>

The PFD demonstrates use of pthread locking structures (mutexes/rwlocks). This
is NOT recommended in a real data application. In the data traffic processing
path, spinlocks should be used. Furthermore the PFD doesn't use OC shared memory
or wired (Big TLB) memory, so these normal allocations on the heap will happen 
more slowly and memory access is susceptible to TLB misses (very slow). Again, 
this is NOT recommended in a real data application's data traffic processing 
path. The implementation is deliberately rudimentary so as to serve as a simple
sample application.

\subsection init_conf_sec Initialization and Configuration

As described in Section 2.2.10, the PED originally must act as the intermediary 
between the PFD and the CPD, so the PFD first connects to the PED. Also the PED 
could not act as the client at this point because it does not know the 
connection information for the PFD. The PED will wait until it knows the 
internal connection information of the CPD also, and then sends this information
to the PFD. The PED is also expected to send the PFD an address to use when
re-sourcing ("NATing") packets and the public address of the CPD. This is 
received so that the PFD can properly steer packets to the CPD as required. The 
PFD then starts a new internal connection to the CPD.

Subsequently, the PFD requests the initial list of authorized users. At that 
point the PFD goes into listening mode where it expects to be notified of new 
authorized or unauthorized users. Essentially, the CPD configures the filtering 
list of the PFD via this channel.

\subsection filtering_sec Filtering Details

The PFD receives packets on its ms—x/y/0.100 and ms—x/y/0.101 data interfaces in
a round-robin fashion. Traffic received on ms-x/y/0.100 should be from end users
and be destined to miscellaneous places, while on ms-x/y/0.101, there should 
only be traffic to the PFD's address from the CPD. All packets received should 
be as a result of the service routes installed by the PED. The data interfaces 
are made by creating FIFO channels and registering them to receive packets from 
these installed service routes.

Packets destined to port 80 originating from an unauthorized subscriber will be 
forwarded to the CPD through header rewrite (a form of network address 
translation [NAT]) and reinsertion into the packet forwarding engine (PFE). A 
packet originating from an authorized user is forwarded out of the router 
normally.

NAT is done on unauthorized traffic coming in on ms-x/y/0.100 to re-source the 
traffic from the IP of the PFD's data interface and to set the destination to 
the CPD. When the CPD replies to the PFD (to the other data interface), the PFD 
will undo the NAT to reset the destination to the original source and reset the 
source to the original destination. This PFD's data interface is IFL 101. We 
just push reply traffic from the CPD to this IFL using a specific service route 
with the configured PFD address.

The one exception to performing NAT on unauthorized traffic is when it is 
destined to the CPD's HTTP server. That is, this traffic will be forwarded as 
normal.

*/

#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <isc/eventlib.h>
#include <jnx/mpsdk.h>
#include <jnx/aux_types.h>
#include <sync/policy_ipc.h>
#include "pfd_conn.h"
#include "pfd_kcom.h"
#include "pfd_logging.h"
#include "pfd_packet.h"
#include "pfd_config.h"
#include "pfd_nat.h"
#include "pfd_main.h"

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_PFD    "pfd"

/*** Data Structures ***/


/*** STATIC/INTERNAL Functions ***/


/**
 * This function quits the application does an exit
 */
static void 
pfd_quit(int signo __unused)
{
    LOG(LOG_INFO, "PFD shutting down.");
    stop_packet_loops();
    close_connections();
    clear_config();
    destroy_packet_loops_config();
    terminate_nat();
    msp_exit();
    exit(0);
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
pfd_init(evContext ctx)
{
    int rc;

    msp_init();
    
    // Handle some signals that we may receive:
    signal(SIGTERM, pfd_quit); // call quit fnc
    signal(SIGHUP, SIG_IGN); // ignore
    signal(SIGPIPE, SIG_IGN); // ignore
    
    // call init function for modules:
    
    rc = init_config(ctx);
    if(rc != SUCCESS) {
        LOG(LOG_ALERT, "%s: Failed to initialize the PFD. "
            "See error log for details ", __func__);
        pfd_shutdown();
    }
    
    init_packet_loops_config();
    init_nat();
    pfd_kcom_init(ctx);
    
    rc = init_connections(ctx);
    if(rc != SUCCESS) {
        LOG(LOG_ALERT, "%s: Failed to initialize the PFD. "
            "See error log for details ", __func__);
        pfd_shutdown();
    }

    return SUCCESS;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Shutdown PFD. Calls exit(1) after cleaning up
 */
void
pfd_shutdown(void)
{
    LOG(LOG_INFO, "PFD shutting down.");
    stop_packet_loops();
    close_connections();
    clear_config();
    destroy_packet_loops_config();
    terminate_nat();
    msp_exit();
    exit(1);
}

/**
 * Initialize pfd's environment
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

    app_ctx = msp_create_app_ctx(argc, argv, DNAME_PFD);
    msp_set_app_cb_init(app_ctx, pfd_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


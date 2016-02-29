/*
 * $Id: cpd_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file cpd_main.c
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

\section cpd_sec The Captive Portal Daemon (CPD)

The CPD is, in essence, just a very simple HTTP server. It is a control (or 
server-style) MP-SDK application. Unauthorized users have an opportunity to 
"authenticate." However, no real authentication is performed. A subscriber will 
simply have the option of clicking a button on a web page to authorize traffic 
and one to revoke such authorization. The list of authorized users is updated 
based on such actions. This list will only be kept in memory and not remembered,
although future versions could place it in persistent storage with KCOM GENCFG.

\subsection init_conf_sec Initialization and Configuration

As described in Section 2.2.10, the PED originally must act as the intermediary 
between the PFD and the CPD, so the CPD first connects to the PED. This way the 
PED will have the CPD's connection information. The CPD receives the address for
its HTTP server, which is also it's (IFL 102) IP address. It also receives the 
address that the PFD uses in NAT. This way it knows all connections to the HTTP 
server from this address are in fact from the PFD having performed NAT on some 
client's packets.

At this point the CPD initializes its HTTP server on port 80. It also awaits a 
connection request from the PFD. The PFD requests the list of authorized 
subscribers, and the CPD replies. Furthermore, the CPD keeps this connection 
open, and sends updates of this list to the PFD.

\subsection mau_sec Managing Authorized Users

When a subscriber (a user's source IP address) is unauthorized, its traffic is 
directed to the CPD by the PFD; however, the CPD will also be directly 
accessible by unauthorized (and authorized) users. When the CPD receives an 
HTTP GET request connection from the PFD's data interface, it will reply with 
an HTTP MOVED (response code 301) redirect message. This redirect URL will 
force the end user's browser to directly connect to the CPD bypassing the PFD's 
NAT because the PFD allows direct connections to the CPD from everyone. Thus, 
we simultaneously lower the load on the PFD. From the user's point of view, 
before the redirect the unauthorized user thinks it is talking to the HTTP 
server (on the internet/network through the outbound interface) that it 
originally requested communication with. After getting the redirect response the
user knows that it is to target the CPD directly.

When the user connects directly to the CPD, the HTTP server presents a page with
a button allowing the user to authorize himself. When clicked, the CPD will add 
the user's source IP to the list of authorized users and send an update to the 
PFD over the internal communication channel.

When a user becomes authorized, he can make connections through the router and 
through other outbound interfaces, and he must directly connect to the CPD to 
click the button to remove his authorization because the PFD will not be 
redirecting anything of his to the CPD.

*/

#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <isc/eventlib.h>
#include <jnx/mpsdk.h>
#include <jnx/aux_types.h>
#include <jnx/logging.h>
#include <jnx/junos_kcom.h>
#include "cpd_conn.h"
#include "cpd_config.h"
#include "cpd_http.h"
#include "cpd_logging.h"
#include "cpd_kcom.h"


/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_CPD    "cpd"


/*** Data Structures ***/


/*** STATIC/INTERNAL Functions ***/

/**
 * This function quits the application upon a SIGTERM signal
 */
static void 
cpd_ctrl_quit(int signo __unused)
{
    LOG(LOG_INFO, "CPD shutting down.");
    junos_kcom_shutdown();
    close_connections();
    clear_config();
    shutdown_http_server();
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
cpd_init (evContext ctx)
{
    msp_control_init();

    // Ignore some signals that we may receive:
    signal(SIGTERM, cpd_ctrl_quit);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    
    logging_set_mode(LOGGING_SYSLOG);
    
    cpd_kcom_init(ctx);
    init_config();
    
    return init_connections(ctx);
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Intialize cpd's environment
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

    app_ctx = msp_create_app_ctx(argc, argv, DNAME_CPD);
    msp_set_app_cb_init(app_ctx, cpd_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


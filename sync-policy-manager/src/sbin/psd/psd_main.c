/*
 * $Id: psd_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file psd_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as a JunOS daemon 
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

\section PSD_sec Policy Server Daemon (PSD)

The PSD is responsible for providing policies, each consisting of a 
pre-existing (configured) input and output (inet) firewall filter name, and a
set of routes for a particular interface. It listens for requests on the RE
via IRI2 (it binds to 0.0.0.0) on a port 7077 (we chose this port number as 
to not conflict with existing Juniper daemons). It accepts connection 
requests from the PED-style clients; however, we intend on only using one 
instance of the PED. It processes the client's messages and replies to them 
as described in this section.

\subsection init_sec Initialization

The PSD creates a server-style socket and binds to its service port (7077).

\subsection conf_sec Configuration

The PSD configuration is actually the policies which will potentially apply 
to specified interfaces. A policy consists of the interface name, address 
family, input filter name, output filter name and routes. The interface name 
is based on an IFL-structured interface name (physical.logical) pattern 
matching expression. Because of this pattern matching expression for the 
interface name, a single policy can be returned in response to many requests
(for a pattern like *, for example, which matches all interface names).

\subsection newcnx_sec Creating New Client Connections

When getting a connection request from a client, the PSD creates a new
connection to the client and registers the message handler for receiving
messages.

\subsection msg_sec Message Process

As a server, the PSD message handler processes the messages received from the
clients and replies. The valid message types it receives are:

\li MSG_HB <br>
The PSD echoes heartbeat messages back to the client without data to notify 
the client that the server is alive.

\li MSG_POLICY_REQ <br>
After receiving policy requests from a client, the PSD looks up a policy in
its configuration to find the policy matching the interface name and address
family included in the request message. It then sends MSG_FILTER and 
MSG_ROUTE messages back to client. If the PSD didn't find a matching policy,
it sends a MSG_POLICY_NA message back.

\li MSG_UPDATE_DONE <br>
This message marks the end of a set of policy request messages. The PSD just 
echoes this message back without data so the PED knows that it has received 
all the asynchronous message replies to its requests.

\subsection shutdown_sec Server Shutdown

Before closing the server socket itself, it closes all client connections 
first.



\section psd_ped_sec Message content between the PSD and the PED

The PED and the PSD use libjipc over TCP sockets to exchange these messages.

\li MSG_HB <br>
The heartbeat message originates from the PED to check the state of connection 
to the PSD. There is no data included in this message. The PSD echoes this 
message back after receiving it.

\li MSG_POLICY_REQ <br>
The policy request message is sent by the PED to request a policy. The data 
included consists of an IFL-formatted interface name and an address family.

\li MSG_FILTER <br>
The filter message is sent by the PSD to reply to the request from the PED. The 
data included consists of an IFL-formatted interface name and an address family, 
which are from the original request, and the names of one input and one output 
filter.

\li MSG_ROUTE <br>
The route message is sent by the PSD to reply to the request from the PED. The 
data included consists of an IFL-formatted interface name and an address family, 
which are from the original request, and route data. The route data consists of 
a route address and prefix, the next-hop address and type, and the route metric 
and preference.

\li MSG_POLICY_NA <br>
The policy-not-available message is sent by the PSD to reply to the request from 
the PED. This message is used when no policy is configured on the server for the 
requested interface name and address family. No data is included in this message 
other than an IFL-formatted interface name and an address family, which are both 
from the original request.

\li MSG_POLICY_UPDATE <br>
The update message is sent by the PSD to notify all connected clients (the PED) 
that the server configuration was updated. From the clients' points of view, 
they may want to update their policies or check for changes if they want to 
remain synchronized with the policy server. This message contains no data.

\li MSG_UPDATE_DONE <br>
The update-done message originates from the PED. It marks the end of a set of 
policy requests. The PSD echoes this message back after receiving it, and no 
data is included in the message. Because communication is asynchronous, this 
helps the PED know that no more responses to policy requests are coming when it 
receives the echo back, since the PSD serves received messages in FIFO order. In 
this case the PED sends this message last and it should get the echo response 
back last as well.


\section figures_sec Figures

\subsection f1_sec Internal Communication Channels between the Daemons (and SSD)

\verbatim
                 +---------+
                 |         |
                 |   PSD   |
                 |         |
                 +----x----+
                      |
                      | IRI2 (libjipc)
                      | 
                      |            ,--> (libssd)
                 +----x----+      /   +---------+
                 |         |   IRI2   |         |
                 |   PED   x----------x   SSD   |
                 |         x\         |         |
                 +----x----+ \        +---------+
\endverbatim


\section sample_conf_sec Sample Configuration

The sample configuration below shows an example of how to configure the daemon. 
The PSD has its configuration under "sync policy-server," and has a policy 
configuration named "a-pol" in which all Fast Ethernet interfaces on FPC 0 get 
assigned the filters filter1 and filter2 as the input and output filters 
respectively.

\verbatim
sync {
    policy-server {
        policy a-pol {
            interface-name fe-0*;
            address-family inet;
            filter {
                input filter1;
                output filter2;
            }
        }
        policy b-pol {
            interface-name sp*;
            address-family inet;
            filter {
                input filter3;
                output filter4;
            }
            route 1.1.1.0/24 {
                next-hop-type reject;
            }
            route 1.1.2.0/24 {
                next-hop-address 1.1.3.3;
                metrics 10;
            }
        }
        traceoptions {
            file psd.trace;
            syslog;
            level all;
            flag all;
        }
    }
}
\endverbatim

*/


#include <sync/common.h>
#include "psd_config.h"
#include "psd_server.h"

#include PS_OUT_H
#include PS_SEQUENCE_H

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_PSD    "psd"

/*** Data Structures ***/

/**
 * CMD Mode menu tree of commands:
 * (This is defined at the end of ped_ui.c)
 */
extern const parse_menu_t master_menu[];

/*** STATIC/INTERNAL Functions ***/

/**
 * Callback for the first initialization of the RE-SDK Application
 * 
 * @param[in] ctx
 *     Newly created event context 
 * 
 * @return SUCCESS
 */
static int
psd_init(evContext ctx)
{
    init_config(); // Initialize patricia tree.
    server_init(ctx); // Init the psd services server connection
    return SUCCESS;
}

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Intializes psd's environment
 * 
 * @param[in] argc
 *     Number of command line arguments
 * 
 * @param[in] argv
 *     String array of command line arguments
 * 
 * @return 0 upon successful exit of the application or non-zero upon failure
 */
int
main(int argc, char **argv)
{
    int ret = 0;
    junos_sdk_app_ctx_t ctx;
    const char * psd_config[] = {DDLNAME_SYNC, DDLNAME_SYNC_PS, NULL};

    /* Create an application context */
    ctx = junos_create_app_ctx(argc, argv, DNAME_PSD,
                               NULL, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    /* Set init call back */
    if ((ret = junos_set_app_cb_init(ctx, psd_init))) {
        goto cleanup;
    }

    /* Set config read call back */
    if ((ret = junos_set_app_cb_config_read(ctx, psd_config_read))) {
        goto cleanup;
    }
    
    /* set trace options DDL path */
    if ((ret = junos_set_app_cfg_trace_path(ctx, psd_config)) < 0) {
        goto cleanup;
    }

    /* Calling junos_app_init */
    ret = junos_app_init(ctx);

cleanup:
    /* Destroying context if daemon init/exit failed */
    junos_destroy_app_ctx(ctx);
    return ret;
}

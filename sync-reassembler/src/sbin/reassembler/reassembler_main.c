/*
 * $Id: reassembler_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file reassembler_main.c
 * @brief Contains main entry point
 *
 * Contains the main entry point and registers the application as a MSP daemon
 */

/* The Application and This Daemon's Documentation: */

/**

\mainpage

\section overview_sec Overview

This is a sample application that is part of the sync-reassembler package.

This document contains the functional specifications for the SDK Your Net 
Corporation (SYNC) IP Reassembler project. This project consists of the 
development of a basic IP fragment reassembler that can receive IP fragment 
packets and reassemble them into one whole IP packet.

The system is designed to demonstrate the ability to quickly manipulate packets
while operating on a Juniper Networks MultiServices PIC/DPC hardware module.
 This makes the system suitable for deployment on Juniper Networks MX-, M- and 
 T-Series routers. The system will be implemented using Juniper's Partner 
 Solution Development Platform (PSDP), also called the JUNOS SDK. This system 
 is targeted to operate with version 9.5 of JUNOS and beyond.

The system will be constructed in the phase-1 and phase-2 SDK models of 
respectively building a daemon and a plug-in for the MS PIC. Because in the 
phase-2 model of constructing a plug-in IP fragment reassembly happens 
automatically, the plug-in will simply pass traffic through it. Thus, it also 
demonstrates an SDK plug-in in its simplest form. It will only log any fragment 
packets that it sees which should be none. We, therefore, focus herein on the 
phase-1 SDK daemon that will have to do the reassembly manually.

\section func Functionality

In this section we detail the functionality of the system. As described in the 
\ref overview_sec "Overview" section , a basic design assumption and 
prerequisite is that the system will operate in the data path using the Juniper 
Networks MultiServices PIC or DPC hardware module. Typically with a full 
application there is a management component running in the router's control 
plane to control the behaviour of the application. This project is meant to be 
simple however; thus, we will not be constructing a management component.

While examining the functionality of the system, we start by examining the 
system's user interface, and progress to the operations of the data component.

\subsection ui_sec User Interface

The user interface of the system is usually a consequent of the JUNOS user 
interface, using a dual organization into a configuration user interface and 
a command user interface. In this application however, we have no management 
component to be able to implement the UI in that way, so we simply use 
command-line options with the daemon. Unfortunately these are defined at the 
time of packaging, not runtime.

\subsubsection cmd_line_sec Command-line Options Interface

\li The <tt>-N</tt> option will be used when starting the application as a daemon and 
there is no need for it to "daemonize" itself. This should be used when the 
process is being launched in the typical way from the init process on the 
MS PIC.

\li The <tt>-v</tt> option can be used alone to show version information for the 
process.

\li The <tt>-l</tt> option turns on logging to STDERR as well as to syslog.

\li The <tt>-d <#></tt> option set the debug level used for internal SDK APIs.

\li The <tt>-m <#></tt> option is optional and specifies a number between 576 and 9192. 
This number represents the largest size of a packet that the system will build 
out of IP fragments. This number should ideally match the MTU on the target 
egress network interface. If it is larger, it is possible that the system 
builds a packet too large to be sent out an egress interface. In that case, 
the PFE will re-fragment the IP packet that the system constructed in order to 
send it successfully. If the number is smaller than the target MTU, then 
packets will simply be smaller than necessary. The default MTU size used is 
9192. If this argument is specified it must be the first one.

\subsection data_sec The Data Component

In this section, we feature the operation of the data component. Generally it 
is responsible for the reassembly behaviour, and reading the configuration from 
the command-line options.

\subsubsection reass_sec Reassembling IP Fragments

Firstly, the most simple traffic case for this application is when packets are 
not fragmented at all. Obviously in this case the application simply passes 
these packets through it without further examination.

In order to accomplish defragmentation, however, it sets up some data 
structures in object-cache shared memory.

We will setup a shared hashtable based a on hash key of the source IP address, 
the destination IP address, the protocol encapsulated by the IP header, and 
the fragment group ID. This enables us to lookup a hash value that will be 
an ordered list of IP fragment packets seen thus far in the same group.

When we receive a fragment, we add it to this ordered list and check to see if 
the list is complete. If the list is not complete, we simply move on to receive 
and process another packet. If the list is complete, and we have all fragments 
in the fragment group, then we rebuild a new IP packet with the same header as 
the one at the head of the list without the fragment group set. We then check 
to see if the new packet is under the configured MTU (see section 
\ref cmd_line_sec "Command-line Options Interface"). As long as the packet is 
less than or equal to the MTU, we send out the newly constructed packet. If the 
packet exceeds the MTU, we refragment the packet at the MTU boundaries with 
the same fragment group ID. We then send out these fragments. When fragmenting 
we copy all IP options for simplicity (this is not strictly required for all 
kinds IP options).

The system will always use the maximum number of data CPUs available. It is, 
therefore, certainly possible that the list of packets associated with each 
hash key is accessed by multiple data CPUs. This will cause increased lock 
contention. To minimize this we recommend enabling the data-flow-affinity 
option in the configuration database for the MS PIC on which the application is 
run.

*/

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/mpsdk.h>
#include <sys/jnx/jbuf.h>
#include "reassembler_logging.h"
#include "reassembler_packet.h"


/*** Constants ***/

#define DNAME_REASSEMBLER  "reassembler"      ///< The daemon name

#define MIN_MTU            576                ///< Min allowed MTU of any IP net

#define MAX_MTU            JBUF_MAX_SEND_LEN  ///< Max allowed MTU


/*** Data Structures ***/

uint16_t reassembler_mtu; ///< the configured MTU

static bool invalid_mtu;  ///< if true during init issue a warning declaring MTU

static evContext mainctx; ///< the event context of this main thread


/*** STATIC/INTERNAL Functions ***/


/**
 * This function quits the application does an exit
 */
static void
reassembler_quit(int signo __unused)
{
    LOG(LOG_INFO, "%s: Reassmebler is exiting", __func__);
    
    stop_packet_loops(mainctx);
    msp_exit();
    exit(0);
}


/**
 * Callback for the first initialization of the Services-SDK Application
 *
 * @param[in] ctx
 *     Newly created event context
 *
 * @return SUCCESS (0) upon successful completion; EFAIL otherwise (exits)
 */
static int
reassembler_init(evContext ctx)
{
    msp_init();
    
    mainctx = ctx;
    

    // Handle some signals that we may receive
    
    signal(SIGTERM, reassembler_quit); // call quit fnc
    signal(SIGHUP, SIG_IGN); // ignore
    signal(SIGPIPE, SIG_IGN); // ignore
    
    if(invalid_mtu) { // tell them what MTU we're using
        LOG(LOG_WARNING, "Did not parse an MTU in the valid range [%d-%d]. "
              "Using %d as the MTU", MIN_MTU, MAX_MTU, reassembler_mtu);
    } else {
        LOG(LOG_INFO, "Using %d as the configured MTU", reassembler_mtu);
    }
    
    // Start processing

    if(init_packet_loops(ctx)) {
        // exit
        LOG(LOG_EMERG, "Reassembler could not initialize. See error log.");
        return EFAIL;
    }

    return SUCCESS;
}

static int
cmd_opt_hdlr (char c, char *args)
{
    reassembler_mtu = MAX_MTU;

    invalid_mtu = true;
    if (c == 'm') {
        // optarg contains our MTU
        reassembler_mtu = (int)strtol(args, NULL, 10);

        if(reassembler_mtu > MAX_MTU || reassembler_mtu < MIN_MTU) {
            reassembler_mtu = MAX_MTU;
        } else {
            invalid_mtu = false;
        }
    }
    return 0;
}

/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Intialize application's environment
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
main(int32_t argc , char ** argv)
{
    mp_sdk_app_ctx_t app_ctx;
    int rc;
    struct option opts[] = {
        {"mtu", 1, NULL, 'm' },
        { NULL, 0, NULL, 0}
    };

    app_ctx = msp_create_app_ctx(argc, argv, DNAME_REASSEMBLER);
    msp_set_app_cb_init(app_ctx, reassembler_init);
    msp_set_app_cb_cmd_line_opts(app_ctx, cmd_opt_hdlr);
    msp_reg_cmd_line_opts(app_ctx, opts);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    LOG(LOG_ERR, "Reassembler exiting unexpectedly");
    return rc;
}


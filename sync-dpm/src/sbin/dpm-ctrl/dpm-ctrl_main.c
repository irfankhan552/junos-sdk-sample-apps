/*
 * $Id: dpm-ctrl_main.c 365138 2010-02-27 10:16:06Z builder $
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
 * @file dpm-ctrl_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as a MSP daemon 
 */ 

/* The Application and This Daemon's Documentation: */

/**
 
\mainpage

\section overview_sec Overview

This document contains the functional specifications for the SDK Your Net 
Corporation (SYNC) Dynamic Policy Manager project. This project consists of the 
development of a basic policy manager that can dynamically apply and remove 
filters and policers on logical interfaces in order to control subscribers' 
policies. The policies applied will primarily be driven by subscribers 
authenticating through a web portal; however, static default policies can also 
be configured through the system.

The system is designed to demonstrate the ability to quickly manipulate the 
dynamic policies while operating on a Juniper Networks MultiServices PIC 
hardware module. This makes the system suitable for deployment on Juniper 
Networks M- and T-Series routers. The system will be implemented using 
Juniper's Partner Solution Development Platform (PSDP), also called the JUNOS 
SDK. This system is targeted to operate with version 9.3 of JUNOS and beyond.

\section func_sec Functionality

In this section we detail the functionality of the system. As described in 
the \ref overview_sec "Overview" section, a basic design assumption and 
prerequisite is that the system will 
operate partly on the Juniper Networks MultiServices PIC hardware module. 
Although this normally implies that the system's software will be running in 
the router's data plane, the component running on the MS PIC will not interact 
with the data path. We will use the MS PIC to create a scalable server 
component; however, that does not consume resources on the routing engine. We 
will also create a management counterpart in the router's control plane to 
control the behaviour of the system. Accordingly, we introduce the concept of 
dividing the system into the control (MS PIC) component and the management 
component. Although the control component is not running on the routing engine 
which is commonly referred to as the control plane, we can think of it as an 
extension of the control plane in this case.

While examining the functionality of the system, we discuss the operation of 
both the management and control components. We start by examining the system's 
user interface, and progress to the operations of the management component 
which will interact with the user interface. We then turn to the management 
component's communications with the control component in order to appropriately 
direct the control component as well as receive status updates from it. 
Finally, we dive into the details of the control component itself.

\subsection ui_sec User Interface

The user interface of the system is of course a consequent of the JUNOS user 
interface. Thus, it uses a dual organization into a configuration user 
interface and a command user interface. In this section we examine both in turn.

\subsubsection cnf_ui_sec Configuration User Interface

In this section we examine the total of all possible configuration options as 
well as an example use case.

The new configuration possibilities are organized into a new Dynamic Policy 
Manager (dpm) hierarchy under the sync object as follows in Figure 1. We 
begin by looking at each of the new additions individually for further 
explanation.

\verbatim

 1 dpm {
 2     policer pname {
 3         if-exceeding {
 4             bandwidth-limit x;
 5             bandwidth-percent x;
 6             burst-size-limit x;
 7         }
 8         then {
 9             discard;
10             forward-class x;
11             loss-priority x;
12         }
13     }
14     ...
15     interface-defaults {
16         ifl-pattern {
17             input-policer pname;
18             output-policer pname;
19         }
20         ...
21     }
22     subscriber-database {
23         class cname {
24             policer pname;
25             subscriber sname {
26                 password pw;
27             }
28             ...
29         }
30         ...
31     }
32     use-classic-filters;  ## hidden
33 }

\endverbatim
<b>Figure 1 – The system's extensions to the JUNOS configuration hierarchy.</b>

The entire new hierarchy as shown on line 1 is enveloped in the dpm stanza. 
Within this we define the configuration of the system.

The first part of the configuration we show is on line 4. The \c policer object 
is similar to what one would be able to define under the JUNOS firewall's 
policer object. The configuration allowed here in the system is a subset of 
what the JUNOS policer allows. All the objects and attributes defined in this 
object define similar characteristics as the JUNOS policer; thus, we do not 
repeat their explanation here. On line 14 we indicate using three periods that 
there may be multiple objects like this. In essence, it is possible to define 
a list of policers so long as they have different pname identifying attributes 
as on line 4.

On line 15 we define the \c interface-defaults object. This object contains the 
configuration for the default policies applied to interfaces.

Line 16 defines the \c ifl-pattern object. Using the pattern given in the 
object's name, we match against all IFL interface names, and for the matching 
IFL interfaces, we apply input and output filters with a corresponding policer 
action of either the \c input-policier or \c output-policer attributes from 
lines 17 or 18. Again on line 20 we have three periods indicating that there 
may be multiple unique ifl-pattern objects.

On line 22 we define the \c subscriber-database object. This object contains the
configuration for the dynamic policies applied to or removed from interfaces 
for subscribers as they log in or out using the system's web-interface portal.

Inside this object one defines multiple classes to group and categorize 
subscribers. Each class as on line 23 is defined by a \c class object, and has a
unique name. It also has a \c policer attribute as shown on line 24. This 
policer gets applied to or removed from the subscriber's traffic dynamically as 
the subscriber logs in or out. This is done through the creation of another term 
matching the subscriber's traffic with this policer as the term's action. 
Section 2.3.1 has more details on this process.

In each class we define multiple subscribers by defining their username and 
\c password in a list as shown on lines 25 and 26. A \c subscriber with a unique
identifier should only be defined in one class.

Lastly on line 32 we have a \c use-classic-filters attribute. If this normally 
hidden toggle attribute is present in the configuration, then to manage the 
policies, the system will use classic implicit filters instead of the default 
fast-update implicit filters. This does not change the functionality of the 
system in anyway, but it changes how the behaviour is achieved. It is added 
because we wish to demonstrate the use of both kinds of filters as well as to 
compare the performance of the two kinds. In our case, the fast-update filters 
should perform much faster. Moreover, they are the natural (and hence the 
default) choice since the terms are evaluated based on priority instead of 
sequentially, thus facilitating implementation, since we have two levels of 
policy priorities (for subscribers and for interface defaults). Currently the
system will always use classic filters regardless of this knob since fast-update
filters are only supported for JUNOS-internal use. This may change in the 
future.

A sample use case is shown in Figure 2. In this sample scenario, we classify 
subscribers in to two categories. Once they sign in they will get the bandwidth 
for the class that they belong to. The silver-class subscribers will have the 
medium-speed policer applied to their traffic, and the gold-class subscribers 
will have the high-speed policer applied to their traffic. Without logging in 
all subscribers connect presumably through an interface matching one of the 
patterns in the interface-defaults section; thus, they will have the default 
policer (the dead-slow policer) applied to their traffic.

In the sample we also show the configuration for enabling the system's tracing, 
which is normally only done for debugging purposes; nonetheless, it is a new 
addition to the configuration, and thus we show it here.

\verbatim

chassis {
    fpc 1 {
        pic 2 {
            adaptive-services {
                service-package {
                    extension-provider {
                        control-cores 4;
                        object-cache-size 128;
                        forwarding-database-size 127;
                        package sync-dpm-ctrl;
                        wired-process-mem-size 512;
                    }
                }
            }
        }
    }
}
interfaces {
    ms-1/2/0 {
        unit 0 {
            family inet {
                address 30.30.20.20/32;
            }
        }
    }
}
sync {
    dpm {
        policer dead-slow {
            if-exceeding {
                bandwidth-limit 64000;
                burst-size-limit 2000;
            }
            then discard;
        }
        policer medium-speed {
            if-exceeding {
                bandwidth-limit 1000000;
                burst-size-limit 50000;
            }
            then loss-priority high;
        }
        policer high-speed {
            if-exceeding {
                bandwidth-limit 6000000;
                burst-size-limit 100000;
            }
            then loss-priority high;
        }
        interface-defaults {
            fe-1/2* {
                input-policer dead-slow;
                output-policer dead-slow;
            }
            fe-0/1/0* {
                input-policer dead-slow;
                output-policer dead-slow;
            }
            fe-0/1/1.0 {
                input-policer dead-slow;
                output-policer dead-slow;
            }
        }
        subscriber-database {
            class silver {
                policer medium-speed;
                subscriber barb {
                    password abc123;
                }
                subscriber steve {
                    password mnlop;
                }
            }
            class gold {
                policer high-speed;
                subscriber sara {
                    password 543210;
                }
                subscriber mike {
                    password xyzabc;
                }
            }

        }
    }
    traceoptions {                 ## only used for debugging purposes
            file dpm.trace;
            flag all;
            syslog;
        }
    }
}

\endverbatim
<b>Figure 2 – A sample configuration</b>

\subsubsection cmd_ui_sec Command User Interface

In this section we examine the new commands added to JUNOS as well as some 
preliminary sample output.

There is only one new command that will be added. It is the "show sync dpm 
subscribers" command. 

This show command shows the subscribers that are currently logged in and 
categorizes them by class. The subscribers shown are a result of the latest 
status received from the control component on the PIC which can come as the 
state of any subscriber changes; however, if the configuration changes the 
status is cleared. Sample output is shown in Figure 3.

\verbatim

router> show sync dpm subscribers

Class silver

Subscribers currently logged in:

steve

Class gold

Subscribers currently logged in:

sara
mike

\endverbatim
<b>Figure 3 – The system's extensions to the JUNOS command interface.</b>

\subsection ctrl_sec The Control Component

In this section, we feature the operation of the control component. Generally 
it is responsible for the primary behaviour of applying the policies. Over and 
above that, it will receive configuration updates and send status updates to 
the management component.

\subsubsection pol_app_sec Policy Application

In this section, our focus will involve the processing of the policies 
configuration in the control component and when it happens. This is twofold, 
since there are static default policies applied to interfaces, and then 
policies that get dynamically added and removed as subscribers log in and out. 
The system must be able to quickly handle the dynamic changes as they may be 
frequent.

As configuration update messages are received, the system commences its 
application of the default interface policies for all the interfaces under 
management. As the list of managed interfaces is received, we apply an input 
and output filter matching all traffic to each of these interfaces. The 
corresponding action will be a policer given to match the input or output 
direction.

At this point the system is said to be statically setup, and is ready to 
interact dynamically with subscriber login requests. This is done through a 
web portal. The portal server is expected to run concurrently in the control 
component.

The web portal presents a subscriber with a web form to authenticate. Upon 
the server servicing this portal receiving the subscriber credentials, it will 
validate them. If they are incorrect—for example, a wrong password—then nothing 
happens other than the user being presented with a web page to retry. When the 
credentials match a subscriber's credentials in one of the configured classes, 
the system needs to apply a new policy to that subscriber's traffic.

To do this the system will first find the interface that this subscriber's 
traffic gets routed through. This is done with a query to the forwarding 
database. If the interface is not one of the managed interfaces, the processing 
stops here, and the portal server returns an error message. Otherwise, the 
interface's input and output filters are retrieved, and on the input filter a 
new term matching a source address to the subscriber's address is created and 
added. On the output filter a new term matching a destination address to the 
subscriber's address is created and added. The action in both of these new 
terms is the policer for the class that the subscriber belongs to. The system 
uses fast-update filters by default, so these new terms are created with a 
higher priority than the term responsible for the default policy (policer) 
on the interface. If the system is set to use classic filters, then the new 
terms will be prepended to the list of existing terms. At this point the 
portal's server returns a web page indicating a successful login.

In the case that a logged-in subscriber navigates to the portal, the server 
detects this, and presents the subscriber with a web form to log out. In this 
case, no credentials are needed, and to log out, the user simply clicks a 
button.

Upon this action being received by the system, the system takes measures to 
undo the filter terms added as described above, and consequently the 
subscriber's policy. These steps are much like those described above for the 
process of a subscriber's login.

\subsubsection rcv_conf_sec Receiving the Configuration

This section is the counterpart to the Loading the Configuration section in 
the management component's documentation, and will focus on receiving the data 
that the management component sends.

Upon the start and end of a configuration update, the management component 
sends start and end messages, so that the control component is aware and can 
prepare for the coming changes. During the configuration change, subscriber 
interaction through the web portal is suspended.

Essentially the configuration information comes in the form of messages. When 
the start message is initially received, all previous policies applied through 
the system get purged. Again this is due to the simple model that we have 
chosen as detailed in Section 2.2.1. Consequently, subscriber logins are not 
persistent across configuration changes, but these changes are expected to be 
very infrequent. This purge also takes place upon any loss of communication 
with the management component, such as if the management component is shutdown.

\subsubsection status_sec Sending Status Updates

This section is the counterpart to the Receiving Status Updates section in 
the management component's documentation, and will focus on sending the status 
updates to the management component.

As mentioned in the the \ref pol_app_sec "Policy Application" section, policies 
get applied to interfaces dynamically as subscribers log in and out. Upon one 
of these actions occurring, the control component sends a message to the 
management component indicating the subscriber, the subscriber's class, and 
the action.

*/

#include "dpm-ctrl_main.h"
#include "dpm-ctrl_config.h"
#include "dpm-ctrl_conn.h"
#include "dpm-ctrl_http.h"
#include "dpm-ctrl_dfw.h"
#include <jnx/msvcspmon_lib.h>

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_DPM_CTRL    "dpm-ctrl"

/**
 * Retry interval (every 5 seconds) for FDB attachment until it succeeds
 */
#define RETRY_FDB_ATTACH_INTERVAL 5

/**
 * Wait for DFW interval. Shouldn't be long, so 1 second is fine
 */
#define WAIT_FOR_DFW_INTERVAL 1


/**
 * Normal interval for heartbeat monitoring signals
 */
#define DPM_CTRL_HB_INTERVAL 3


/**
 * If we miss this many heartbeat, we're "sick", so let the health monitoring
 * infrastructure take care of us (will probably restart the process and log it)
 */
#define DPM_CTRL_SICK_INTERVALS 3


/**
 * Heartbeat monitoring identifier. This SHOULD be prefixed with the assigned
 * provider prefix.
 */
#define DPM_CTRL_HM_IDENTIFIER "sync-dpm-ctrl"


/*** Data Structures ***/

msp_fdb_handle_t fdb_handle;    ///< handle for FDB (forwarding DB)
int http_cpu_num;               ///< CPU setup for use of HTTP server

static evContext ev_ctx;        ///< event context saved for when unavailable
static evTimerID retry_timer;   ///< timer set to do fdb attach retry
static evTimerID wait_timer;    ///< timer set to wait for DFW connection
static evTimerID hb_timer;      ///< timer set to perform heartbeats
static struct msvcspmon_client_info * mci; ///< handle for msvcspmon API

/*** STATIC/INTERNAL Functions ***/


/**
 * This function quits the application does an exit
 */
static void 
dpm_quit(int signo __unused)
{
    LOG(LOG_INFO, "Shutting down");
    
    if(fdb_handle) {
        shutdown_http_server();
        msp_fdb_detach(fdb_handle);
    }
    
    shutdown_dfw();
    
    close_connections();
    
    clear_configuration();
    
    if(mci != NULL) {
        msvcspmon_unregister(mci);
    }

    msp_exit();
    
    LOG(LOG_INFO, "Shutting down finished");
    
    exit(0);
}


/**
 * Callback to periodically retry attaching to FDB. It stops being called 
 * once successfully attached.
 * 
 * @param[in] ctx
 *     The event context for this application
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] due
 *     The absolute time when the event is due (now)
 * 
 * @param[in] inter
 *     The period; when this will next be called 
 */
static void
retry_attach_fdb(evContext ctx,
                 void * uap __unused,
                 struct timespec due __unused,
                 struct timespec inter __unused)
{
    int rc;
    
    rc = msp_fdb_attach(NULL, &fdb_handle);
    
    if(rc == MSP_EAGAIN) {
        return; // will retry again later
    } else if(rc != MSP_OK) {
        LOG(LOG_EMERG, "%s: Failed to attach to the forwarding database. Check "
                "that it is configured (Error code: %d)", __func__, rc);
    } else { // it succeeded
        evClearTimer(ctx, retry_timer);
        evInitID(&retry_timer);
        LOG(LOG_INFO, "%s: Attached to FDB", __func__);
        
        if(!evTestID(wait_timer)) {
            // We need FDB attachment and DFW before we can process logins
            init_http_server();
        }
    }
}


/**
 * Callback to periodically wait until DFW module is ready.
 * It stops being called once it is ready.
 * 
 * @param[in] ctx
 *     The event context for this application
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] due
 *     The absolute time when the event is due (now)
 * 
 * @param[in] inter
 *     The period; when this will next be called 
 */
static void
wait_for_dfw(evContext ctx,
             void * uap __unused,
             struct timespec due __unused,
             struct timespec inter __unused)
{
    if(dfw_ready()) {
        evClearTimer(ctx, wait_timer);
        evInitID(&wait_timer);
        LOG(LOG_INFO, "%s: DFW module is ready", __func__);
        
        // connect to RE now...
        if(init_connections(ctx) != SUCCESS) {
            LOG(LOG_ALERT, "%s: Failed to intialize. "
                    "See error log for details.", __func__);
            dpm_quit(0);
        }
        
        if(!evTestID(retry_timer)) {
            // We need FDB attachment also before we can process logins
            init_http_server();
        }
    } else {
        return;
    }
}


/**
 * Callback to periodically send heartbeats
 * 
 * @param[in] ctx
 *     The event context for this application
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] due
 *     The absolute time when the event is due (now)
 * 
 * @param[in] inter
 *     The period; when this will next be called 
 */
static void
send_hb(evContext ctx __unused,
        void * uap __unused,
        struct timespec due __unused,
        struct timespec inter __unused)
{
    msvcspmon_heartbeat(mci);
}


/**
 * Heartbeat setup status callback
 */
static void
heartbeat_setup_status(msvcspmon_hb_setup_status_t status)
{
    if(status != MSVCSPMON_HB_SETUP_SUCCESS) {
        LOG(LOG_ERR, "%s: msvcs-pmon infrastructure could not "
                "setup a heartbeat monitor", __func__);
        return;
    }
    
    LOG(LOG_INFO, "%s: msvcs-pmon infrastructure setup "
            "a heartbeat monitor for this daemon", __func__);
    
    // turn it on
    msvcspmon_heartbeat(mci);
    msvcspmon_heartbeat_state(mci, MSVCSPMON_HEARTBEAT_ON);
    
    evInitID(&hb_timer);
    if(evSetTimer(ev_ctx, send_hb, NULL, evAddTime(evNowTime(), 
            evConsTime(DPM_CTRL_HB_INTERVAL, 0)),
            evConsTime(DPM_CTRL_HB_INTERVAL, 0), &hb_timer)) {

        LOG(LOG_EMERG, "%s: Failed to initialize a timer to send heartbeats. "
                "(Error: %m)", __func__);
    }
}


/**
 * Health monitor connection status callback
 * 
 * @param[in] status
 *      The status of the connection to pmon on the RE
 */
static void
hm_connection_status(msvcspmon_connection_status_t status)
{
    struct msvcspmon_hb_setup_info mhsi;
    
    switch(status)
    {
    case MSVCSPMON_REGISTER_SUCCESS:
        LOG(LOG_INFO, "%s: Connection status reported as up", __func__);
        
        
        bzero(&mhsi, sizeof(struct msvcspmon_hb_setup_info));
        // mhsi.pid will be setup by API
        mhsi.keepalive_interval = DPM_CTRL_HB_INTERVAL * 1000;
        mhsi.no_of_missed_keepalive = DPM_CTRL_SICK_INTERVALS;
        mhsi.action = MSVCSPMON_ACTION_DEFAULT;
        strncpy(mhsi.name, DPM_CTRL_HM_IDENTIFIER, MSVCSPMON_PROC_NAME_LEN);
        msvcspmon_setup_heartbeat(mci, &mhsi, heartbeat_setup_status);
        
        break;
        
    case MSVCSPMON_CLOSED:
        LOG(LOG_INFO, "%s: Connection status reported as closed", __func__);
        break;
        
    case MSVCSPMON_FAILED: 
        LOG(LOG_ERR, "%s: Connection status reported as failed", __func__);
        break;
    
    default:
        LOG(LOG_ERR, "%s: Connection status reported as unknown", __func__);
        break;
    }
}


/**
 * Callback for the first initialization of the MP-SDK Application
 * 
 * @param[in] ctx
 *     Newly created event context 
 * 
 * @return SUCCESS (0) upon successful completion; EFAIL otherwise (exits)
 */
static int
dpm_init(evContext ctx)
{
    int rc, cpu;

    msp_control_init();
    
    // Handle some signals that we may receive:
    signal(SIGTERM, dpm_quit); // call quit fnc
    signal(SIGHUP, SIG_IGN); // ignore
    signal(SIGPIPE, SIG_IGN); // ignore
    
    ev_ctx = ctx;
    
    // setup some simple health/heartbeat monitoring
    mci = msvcspmon_register(ctx, hm_connection_status);
    
    if(mci == NULL) {
        LOG(LOG_ERR, "%s: Registering for health monitoring failed",
                __func__);
        goto failed;
    }
    
    // call init function for modules:
    
    rc = init_configuration();
    if(rc != SUCCESS) {
        goto failed;
    }
    
    rc = init_dfw(ctx);
    if(rc != SUCCESS) {
        goto failed;
    }
    
    if(dfw_ready()) {
        rc = init_connections(ctx);
        if(rc != SUCCESS) {
            goto failed;
        }
    } else {
        // need to try again later because FDB isn't initialized yet
        evInitID(&wait_timer);
        if(evSetTimer(ctx, wait_for_dfw, NULL, evAddTime(evNowTime(), 
                evConsTime(WAIT_FOR_DFW_INTERVAL, 0)),
                evConsTime(WAIT_FOR_DFW_INTERVAL, 0), &wait_timer)) {

            LOG(LOG_EMERG, "%s: Failed to initialize a timer to wait for the "
                    "DFW module to become ready (Error: %m)", __func__);
            return EFAIL;
        }
    }
    
    // find a user CPU number to use for this main thread
    cpu = msp_env_get_next_user_cpu(MSP_NEXT_NONE);

    // if at least one valid user cpu available...
    if(cpu != MSP_NEXT_END) {

        LOG(LOG_INFO, "%s: Binding main process to user cpu %d",
                __func__, cpu);

        if(msp_process_bind(cpu) != SUCCESS) {
            LOG(LOG_ALERT, "%s: Failed to initialize. Cannot bind to user cpu. "
                    "See error log for details ", __func__);
            dpm_quit(0);
            return EFAIL;
        }

    } else {
        LOG(LOG_EMERG, "%s: Main process will not be bound to a user cpu as "
            "expected. System cannot run without a user core.", __func__);
    }
    
    // find another user CPU number to use for the HTTP server
    http_cpu_num = msp_env_get_next_user_cpu(cpu);

    if(http_cpu_num == MSP_NEXT_END) {
        http_cpu_num = cpu;
    }
    
    LOG(LOG_INFO, "%s: Binding HTTP server thread to user cpu %d",
            __func__, http_cpu_num);
    
    // We are using FDB which is why we need user CPUs
    
    rc = msp_fdb_attach(NULL, &fdb_handle);
    
    if(rc == MSP_EAGAIN) {
        // need to try again later because FDB isn't initialized yet
        evInitID(&retry_timer);
        if(evSetTimer(ctx, retry_attach_fdb, NULL, evAddTime(evNowTime(), 
                evConsTime(RETRY_FDB_ATTACH_INTERVAL, 0)),
                evConsTime(RETRY_FDB_ATTACH_INTERVAL, 0), &retry_timer)) {

            LOG(LOG_EMERG, "%s: Failed to initialize a timer to retry "
                "attaching to FDB (Error: %m)", __func__);
            return EFAIL;
        }
    } else if(rc != MSP_OK) {
        LOG(LOG_EMERG, "%s: Failed to attach to the forwarding database. Check "
                "that it is configured (Error code: %d)", __func__, rc);
        return EFAIL;
    } else {
        LOG(LOG_INFO, "%s: Attached to FDB", __func__);
        
        // We need FDB attachment before we can process logins
        init_http_server();
    }    
    
    return SUCCESS;
    
failed:

    LOG(LOG_ALERT, "%s: Failed to initialize. See error log for details.",
            __func__);
    dpm_quit(0);
    return EFAIL;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Intialize dpm's environment
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
    int rc;
    mp_sdk_app_ctx_t app_ctx;

    app_ctx = msp_create_app_ctx(argc, argv, DNAME_DPM_CTRL);
    msp_set_app_cb_init(app_ctx, dpm_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


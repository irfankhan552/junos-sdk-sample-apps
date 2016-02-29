/*
 * $Id: dpm-mgmt_main.c 365138 2010-02-27 10:16:06Z builder $
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
 * @file dpm-mgmt_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as a JUNOS daemon 
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
component. Although the control component is not running on the routine engine 
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

\subsection mgmt_sec The Management Component

In this section, we feature the operation of the management component. 
Generally, it is responsible for providing the user interface and sending the 
control component the system configuration. It also must get information from 
the control component in order to output information as shown in the 
\ref cmd_ui_sec "Command User Interface" section.

\subsubsection loading_sec Loading the Configuration

Messages are traced as the configuration is loaded as to observe and understand 
the progress, and that the effects of the configuration may not appear 
immediately. Configuring traceoptions will enable this supplementary debug and 
informational logging to the configured trace file. Logging is done through 
syslog at the external facility and at the informational level. The DPM-MGMT 
log tag is used so that messages from this system may easily be filtered for 
viewing.

The management component needs to parse, validate and load the configuration 
hierarchy introduced in the \ref cnf_ui_sec "Configuration User Interface" 
section. This is triggered by a configured 
change, and the control component may or may not be available and connected at 
the time. To cope with this challenge, the management component stores a 
record of the configuration to achieve its end goal of sending accurate 
configuration to the control component.

When the system's configuration is found and examined for validity, the entire 
new configuration will simply replace the previously stored configuration (if 
there is any). While it is possible to extract the configuration changes, we 
elect not to take that approach here for simplicity.

When the configuration is successfully stored into the management component's 
memory, and when or if the control component is connected, the management 
component pushes the entire new configuration to the control component. Because 
we have elect to push the entire configuration in this 'reset' fashion, the 
control component will work this way as well. See the control component's 
documentation for more details.

\subsubsection rcv_sec Receiving Status Updates

The second major purpose of the management component is to receive status 
updates as they come, and report them through the JUNOS CLI as requested via 
the show command presented in the \ref cmd_ui_sec "Command User Interface" 
section. The management component will not 
request status updates from the control component; it will just accept them as 
the data component chooses to send them. Therefore, the management component 
must cache the status. The status updates come in two forms. When any status 
update is received a message is logged via tracing.

The two types of status messages received are to tell the management component 
when a subscriber logs in or logs out. The message carries the subscriber 
name and class that they belong to. For more information on when these updates 
are sent see the control component's documentation.

When a configuration change is made from the CLI, all status results stored 
in the management component are deleted.

*/

#include <sync/common.h>
#include <jnx/pmon.h>
#include "dpm-mgmt_main.h"
#include "dpm-mgmt_config.h"
#include "dpm-mgmt_conn.h"
#include "dpm-mgmt_kcom.h"
#include "dpm-mgmt_logging.h"

#include DPM_SEQUENCE_H

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_DPM_MGMT    "dpm-mgmt"


/**
 * The heartbeat interval to use when setting up process health monitoring
 */
#define PMON_HB_INTERVAL   30


/*** Data Structures ***/

/**
 * CMD Mode menu tree of commands:
 * (This is defined at the end of dpm-mgmt_ui.c)
 */
extern const parse_menu_t master_menu[];

/**
 * Path in configuration DB to DPM configuration 
 */
extern const char * dpm_config[];

static junos_pmon_context_t pmon; ///< health monitoring handle

/*** STATIC/INTERNAL Functions ***/


/**
 * Shutdown app upon a SIGTERM.
 * 
 * @param[in] signal_ign
 *     Always SIGTERM (ignored)
 */
static void
dpm_sigterm(int signal_ign __unused)
{
    dpm_shutdown(TRUE);
}

/**
 * Callback for the first initialization of the RE-SDK Application
 * 
 * @param[in] ctx
 *     Newly created event context 
 * 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message
 */
static int
dpm_init (evContext ctx)
{
    struct timespec interval;
    
    pmon = NULL;
    
    junos_sig_register(SIGTERM, dpm_sigterm);

    init_config();
    
    if(kcom_init(ctx) != KCOM_OK) {
        goto init_fail;
    }
    
    if((pmon = junos_pmon_create_context()) == PMON_CONTEXT_INVALID) {
        LOG(LOG_ERR, "%s: health monitoring: initialization failed", __func__);
        goto init_fail;
    }

    if(junos_pmon_set_event_context(pmon, ctx)) {
        LOG(LOG_ERR, "%s: health monitoring: setup failed", __func__);
        goto init_fail;
    }
    
    if(junos_pmon_select_backend(pmon, PMON_BACKEND_LOCAL, NULL)) {
        LOG(LOG_ERR, "%s: health monitoring: select backend failed", __func__);
        goto init_fail;
    }
    
    interval.tv_sec = PMON_HB_INTERVAL;
    interval.tv_nsec = 0;
    
    if(junos_pmon_heartbeat(pmon, &interval, 0)) {
        LOG(LOG_ERR, "health monitoring: setup heartbeat failed");
        goto init_fail;
    }
    
    if(init_conn_server(ctx)) {
        goto init_fail;
    }
    
    return SUCCESS;
    
init_fail:

    LOG(LOG_CRIT, "initialization failed");
    kcom_shutdown();
    clear_config();
    shutdown_conn_server();
    if(pmon) {
        junos_pmon_destroy_context(&pmon);
    }
    
    return EFAIL;
}


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Shutdown app and opitonally exit with status 0
 * 
 * @param[in] do_exit
 *      if true, log a shutdown message and call exit(0)
 */
void
dpm_shutdown(boolean do_exit)
{
    kcom_shutdown();
    clear_config();
    shutdown_conn_server();
    junos_pmon_destroy_context(&pmon);
    
    if(do_exit) {
        LOG(TRACE_LOG_INFO, "dpm-mgmt shutting down");
        exit(0);
    }
}


/**
 * Intializes dpm-mgmt's environment
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
main (int argc, char **argv)
{
    int ret = 0;
    junos_sdk_app_ctx_t ctx;
    
    // create an application context
    ctx = junos_create_app_ctx(argc, argv, DNAME_DPM_MGMT,
                 master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    // set config read call back
    ret = junos_set_app_cb_config_read(ctx, dpm_config_read);
    if (ret < 0) {
        junos_destroy_app_ctx(ctx); 
        return ret;
    }

    // set init call back
    ret = junos_set_app_cb_init(ctx, dpm_init);
    if (ret < 0) {
        junos_destroy_app_ctx(ctx); 
        return ret;
    }
    
    // set trace options DDL path
    ret = junos_set_app_cfg_trace_path(ctx, dpm_config);
    if (ret < 0) {
        junos_destroy_app_ctx(ctx);
        return ret;
    }

    // now just call the great junos_app_init
    ret = junos_app_init(ctx);

    // should not come here unless daemon exiting or init failed,
    // destroy context
    junos_destroy_app_ctx(ctx);
    return ret;
}

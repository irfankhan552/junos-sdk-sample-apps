/*
 * $Id: equilibrium-mgmt_main.c 431556 2011-03-20 10:23:34Z sunilbasker $
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
 * @file equilibrium-mgmt_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as a JUNOS daemon 
 */

/* The Application and This Daemon's Documentation: */

/**
 
\mainpage

\section overview_sec Overview

This is a sample application that is part of the sync-equilibrium package.

The Equilibrium application overall consists of the development of a 
basic HTTP load-balancing system designed to operate quickly 
in the network data path on the Juniper Networks MultiServices PIC hardware 
module. The system function will be comparable to that of a reverse-proxy 
web-application load balancer designed to be deployed adjacent to the web
server cluster for added performance and high availability.

\section functionality_sec Functionality

Although this implies that the system's software will be chiefly running
in the router's data plane, we will also create a management counterpart 
in the router's control plane to control the behaviour of the load balancing.
Accordingly, we introduce the concept of dividing the system into 
the data (data-plane) component and the management component.

While examining the functionality of the system herein, we discuss the 
operation of both the management and data components. We start by examining 
the system's user interface, and progress to the operations of the 
management component which will interact with the user interface. We then 
turn to the management component's communications with the data component 
in order to appropriately direct the data component as well as receive 
status updates from it. We dive into the details of the data component 
itself in the documentation for the equilibrium-data daemon.

\subsection ui_sec User Interface

The user interface of the system is of course a consequent of the JUNOS 
user interface. Thus, it uses a dual organization into a configuration 
user interface and a command user interface. In this section we examine 
both in turn.

\subsubsection cnf_ui_sec Configuration User Interface

In this section we examine the total of all possible configuration options 
as well as some example use cases.

The new configuration possibilities are organized into the JUNOS 
hierarchy of services as follows in Figure 1. We begin by looking at 
each of the new additions individually for further explanation.

\verbatim

 1 services {                                              ##existing:
 2     service-set service-set-name { 
 3         interface-service {
 4             service-interface ms-x/y/0.z;
 5         }
 6         extension-service load-balance-instance-name {  ##new extensions:
 7             application application-name {
 8                 application-address w.x.y.z;            ##mandatory
 9                 application-port port-number;
10                 servers {                               ##mandatory
11                     a.b.c.d;
12                     e.f.g.h;
13                 }
14                 server-monitor {
15                     server-connection-interval time-in-seconds;
16                     server-connection-timeout time-in-seconds;
17                     server-timeouts-allowed number;
18                     server-down-retry-interval time-in-seconds;
19                 }
20                 session-timeout time-in-seconds;
21             }
22         }
23     }
24 }

\endverbatim
<b>Figure 1 – The system's extensions to the JUNOS configuration hierarchy.</b>

The first part of new configuration that is important is on line 6.
The service-name identifier of the extension-service object must be 
prefixed with "<i>equilibrium-</i>. This is important because the 
management component will be parsing this configuration tree and searching 
for this prefix.

Although multiple extension-service services are allowed to be configured,
only one should start with the "equilibrium-" prefix. Failing to do this 
will result in a failure upon committing the configuration. Notwithstanding 
this restriction, it is possible to configure multiple service-set objects 
each with its own load balancing service. This is not new to JUNOS, and it 
is supported by the system.

Now we move on to line 7. The service within a service set may contain the
configuration to load balance for multiple applications. This is done by 
configuring multiple applications. The \c application object has the 
application-name as its identifier. This useful to make each application 
object unique; however, it is also suggested that a meaningful name be used.

Line 8 shows the addition of the \c application-address attribute. Its value
is an IP address used as the façade for the cluster of web servers and also 
particularly for the application at hand. For more information about this 
address, see the Requirements document. The application-address attribute 
is mandatory.

Line 9 shows the addition of the \c application-port attribute. Its value is 
a port number in the valid range which is 0-65535. This attribute is 
optional, and defaults to 80, the normal HTTP port number.

There is one additional limitation to what may be configured. The 
application-address and application-port in combination must be unique. 
That is, two application objects may not have identical values for both 
of these attributes.

Line 10 introduces the \c servers object. This is a container object for the
server-address attributes of which there may be up to 99, but at 
least one. Thus, the servers object is also mandatory.

Line 14 sets up the \c server-monitor object. When this optional object 
is present the servers in the list (inside the servers object) are each 
monitored using periodic connections to them. If this object is not 
present, then no monitor will be setup, and all servers will be 
assumed up, running, and ready to receive load-balanced traffic. Using 
the monitor, however, can detect servers that may not be in a ready 
state, and hence the system would not direct traffic to them.

Line 15 allows the configuration of \c server-connection-interval, a custom 
probing interval that the monitor will use. This is measured in seconds. 
Precisely every time this interval fires after system initialization, a 
connection to each server in the list will be attempted to verify its state. If
a connection is made, an HTTP request is sent, and a response is awaited. This 
attribute is optional, and when unspecified the default value used is 60. The 
valid range for this attribute is 1-3600.

Line 16 allows \c server-connection-timeout, a customized timeout value, to be 
configured for the total time to perform these attempts; namely, creating the 
connection and getting an HTTP response. This is measured in seconds. The nature
of the system is such that if a timeout is hit, the system records a missed 
probe for that server. This attribute is optional, and when unspecified, the 
default value used is 30. The valid range for this attribute is 1-3600.

Line 17 configures \c server-timeouts-allowed, a customized allowed number of 
missed probes. When the system sees that this number is exceeded, it marks the 
server as down. At this point no more load balanced traffic will be directed to 
it. This attribute is optional, and when unspecified, the default value used is 
1. The valid range for this attribute is 0-99.

Line 18 configures \c server-down-retry-interval, a customized wait period 
before the system retries probing servers that are marked down. This is measured
in seconds. This attribute is optional, and when unspecified, the default value 
used is 300 (which is equivalent to 5 minutes). The valid range for this 
attribute is 1-3600.

Lastly, line 20 configures a \c session-timeout attribute for each client of the
current application that has sent traffic through the system. This is measured 
in seconds. This attribute is optional, and when unspecified, the default value 
used is 900 (which is equivalent to 15 minutes). The valid range for this 
attribute is 15-3600. The session timeout value is recommended to match the 
servers' session timeout. The system shall flush out sessions aged by the amount
of the timeout, where a session's age is effectively incremented every second 
and reset to new upon observing traffic from the flow.

A sample use case is shown in Figure 2 In this sample scenario, 3 servers are
used for load balancing the normal traffic, and 4 more servers are load 
balancing the special search traffic. What the servers are used for (normal and 
search traffic) is, of course, not dependant on the configuration, and in this 
case is just used as an example.

\verbatim

interfaces {
    fe-0/0/0 {
        unit 0 {
            family inet {
                service {
                    input {
                        service-set web-balance;
                    }
                    output {
                        service-set web-balance;
                    }
                }
                address {
                    64.252.155.1/24;
                }
            }
        }
    }
}
services {
    service-set web-balance { 
        interface-service {
            service-interface ms-1/2/0.0;
        }
        extension-service load-balance-my-http {
            application www {
                application-address 64.252.155.10;
                servers {
                    64.252.155.3;
                    64.252.155.4;
                    64.252.155.5;
                }
                server-monitor;
            }
            application www-search {
                application-address 64.252.155.11;
                servers {
                    64.252.155.6;
                    64.252.155.7;
                    64.252.155.8;
                    64.252.155.9;
                }
                server-monitor {
                    server-connection-interval 300;
                    server-timeouts-allowed 0;
                    server-down-retry 60;
                }
                session-timeout 600;
            }
        }
    }
}

\endverbatim
<b>Figure 2 – A sample configuration</b>

We see that in the www application object 3 servers are listed, and monitoring 
is enabled with all the defaults. Also the default application port of 80 is 
used, and the default session timeout of 15 minutes is used. In the traffic 
that is supposed to be web-search related, we see the application object called 
www-search which would be a good meaningful name. In this application there is a
different address used, as well as 4 different servers. Thus, requests using the
search servers in some way would use this different address or a name that 
resolves to it. The port number used is still 80, but here we see a custom 
session timeout of 600 (10 minutes). Moreover, there is some customization of 
the monitor in 3 of the 4 optional attributes.

Finally, note that there is some necessary configuration that is not shown. 
Specifically, the configuration of the logical interface that is used as the 
service-interface in the service set (i.e. ms-1/2/0.0 here) needs to have a 
routable address. It is used when probing servers. Additionally, the chassis 
configuration of the data and control cores should leave at least one user core 
and one data core. However, it is recommended to have 6 data cores (as many as 
possible while leaving one user core). If no user cores are available, the extra
non-data threads remain bound to control cores. Also it is necessary to 
configure the object cache so that this is available to the data component for 
fast memory access and allocation. Lastly, the configuration of the standard 
JUNOS trace options will be separate, under "sync equilibrium."

\subsubsection cmd_ui_sec Command User Interface

In this section we examine the new command added to JUNOS as well as some sample
output.

There is only one new command that will be added. That is the "show sync 
equilibrium status" command. It also takes two parameters; namely, the 
service-set name and the application name. If an application name is specified 
it must be done so with the service-set name as well. The order of these two 
parameters does not matter. Sample output is shown in Figure 3.

\verbatim

router>show sync equilibrium status service-set ss-name application app-name

For application app-name within service set ss-name

  Application servers' status:
    64.252.155.6 is up
    64.252.155.7 is up
    64.252.155.8 is down
    64.252.155.9 is up

  Current number of cached sessions is 10000.

\endverbatim
<b>Figure 3 – The system's extensions to the JUNOS command interface.</b>

Omitting the application name will simply output the status of all of the 
applications in the service set, and omitting the service-set name as well will 
simply output the status for all applications in all service sets.

\subsection mgmt_comp_sec The Management Component

In this section, we feature the operation of the management component. 
Generally, it is responsible for providing the user interface and sending the 
data component the system configuration. It also must get information from the 
data component in order to output information as shown in Section 
\ref cmd_ui_sec "Command User Interface".

\subsubsection loading_sec Loading the Configuration

The details of this section mix implementation and functionality; hence, some 
internal details are present. This is attributable to the fact that simply 
committing a configuration for the system, does not mean that it is configured. 
The information needs to pass through a few phases which depend on other JUNOS 
processes and the data component. These parts behave independent of the 
management component, and thus coordination is needed which may affect the 
externally visible functionality of the management component.

Messages are logged as the configuration is loaded in these phases as to observe
and understand the progress, and that the effects of the configuration may not 
appear immediately. Logging is done through syslog at the external facility and 
at the informational level. The EQUILIBRIUM_MGMT log tag is used so that
messages from this system may easily be filtered for viewing.

The management component needs to parse the configuration hierarchy introduced 
in Section \ref cnf_ui_sec "Configuration User Interface". The configuration 
under a service belongs to only one service set, and is tied to a service-set 
ID which will eventually be received with every packet passing through the 
system in the data component. The retrieval of these service-set IDs may or may 
not be available at the time of the configuration load. Furthermore, the data 
component may or may not be available and connected. To cope with these 
challenges, the management component passes the configuration through three 
steps to achieve its end goal of sending accurate configuration to the data 
component.

Naturally, the first step the configuration data passes through is loading. With
every service configuration examined, all of the applications' configurations 
and deleted applications' configurations are stored with an action of added, 
changed, or deleted. The configuration data sits in this list, and waits until 
the service-set resolution blob (SSRB) is available.

The SSRB is published by another JUNOS process that is responsible for services.
The management component does not need to communicate with the other process 
because the SSRB is passed through the JUNOS kernel, and the management 
component will have registered with the kernel for notifications. When it is 
available, the list passes through the next phase. That is, the service-set 
names are mapped to service-set IDs using the information present in the SSRB.

Lastly, the third phase of processing happens when the management component 
receives notification from the data component that it is up and ready for 
configuration. This may already be the case if the data component is not in the 
process of starting for the first time. Nonetheless, at this point the list of 
each application's configuration is processed, and with the pertinent action 
this information is sent to the data component in a message.
 
\subsubsection rcv_sec Receiving Status Updates

The second major purpose of the management component is to receive status 
updates as they come and report them through the JUNOS CLI as requested via the 
command presented in Section \ref cmd_ui_sec "Command User Interface". The 
management component will not request status updates from the data component; 
it will just accept them as the data component chooses to send them. Therefore, 
the management component must cache a status, and in doing so it may deviate 
slightly from the true status in the data component. The status updates come in 
two forms. When any status update is received a message is logged via syslog.

The status of a server is reported when it changes. By default, the status of 
a given server is initiated as down until the data component's monitoring 
service can reach them to assure that they are up. The exception is that 
servers that are not monitored have no status updates since they are assumed 
up without verification. For more information on when these updates are sent 
see Sections "Receiving the Configuration" and "Aging the Existing Sessions" in 
the equilibrium-data documentation.

The other kind of update received is an update about the number of active flows 
or sessions that have not timed out for each application. These updates are sent
periodically as the aging service processes all sessions. For more information 
on when these updates are sent see Sections "Monitoring the Servers" and "Aging 
the Existing Sessions" in the equilibrium-data documentation.


\section samp_setup Sample setup

In this section we show a sample configuration, and explain how to setup the 
system to get it running. 

These are the parts of the configuration that should be setup to activate and 
run the system properly. The comments included are numbered, and will 
subsequently be explained in more detail below.

\verbatim

system {
    syslog {

        file messages {
            ##
            ## 1. Show informational messages from the data component
            ##
            any info;
            authorization info;
        }
    }
}
chassis {
    fpc 1 {
        pic 2 {
            adaptive-services {
                service-package {
                    extension-provider {
                        ##
                        ## 2. Recommended settings for a Type-I MS PIC
                        ##
                        control-cores 1;
                        data-cores 5;
                        object-cache-size 512;
                        package sync-equilibrium-data;
                    }
                }
            }
        }
    }
}
interfaces {
    ##
    ## 3. Clients come from this subnet
    ##
    fe-1/1/0 {
        unit 0 {
            family inet {
                address 192.168.2.1/24;
            }
        }
    }
    ##
    ## 4. Servers are in this subnet
    ##
    fe-1/1/3 {
        unit 0 {
            family inet {
                service {
                    input {
                        service-set sync;
                    }
                    output {
                        service-set sync;
                    }
                }
                address 192.168.0.1/24;
            }
        }
    }
    ms-1/2/0 {
        unit 0 {
            family inet {
                ##
                ## 5. This address will be used for probing
                ##
                address 192.168.1.2/32;
            }
        }
    }
    ##
    ## 6. Optionally setup debugging
    ##
    pc-1/2/0 {
        multiservice-options {
            core-dump;
            debugger-on-panic;
        }
    }
}
routing-options {
    static {
        ##
        ## 7. This is to force traffic destined to the
        ## facade out the interface towards the servers.
        ##
        route 192.168.0.10/32 {
            next-hop 192.168.0.2;
            retain;
        }
    }
}
services {
    service-set sync {
        interface-service {
            service-interface ms-1/2/0.0;
        }
        extension-service equilibrium-load-bal {
            application www {
                application-address 192.168.0.10;
                servers {
                    192.168.0.2;
                    192.168.0.3;
                    192.168.0.4;
                    192.168.0.7;
                }
                server-monitor {
                    server-connection-interval 90;
                    server-connection-timeout 10;
                    server-timeouts-allowed 3;
                    server-down-retry-interval 300;
                }
                session-timeout 60; ## 8. for testing, so we can observe changes
            }
            ##
            ## 9. Create another one that won't work, just for show
            ##
            application abc {
                application-address 5.6.7.8;
                servers {
                    1.2.3.4;
                }
                server-monitor;
            }
        }
    }
}
sync {
    equilibrium {
        traceoptions {
            file eq.trace;
            flag all;
        }
    }
}

\endverbatim
<b>Figure 4 – Sample excerpts of a system configuration.</b>


Here we present more detail on the configuration above by explaining the 
commented sections in detail. The number of the item below corresponds to the 
number in the comment from figure 4.

-# We have setup the syslog file called messages to receive messages from all 
facilities if they are at or above the informational level. This is important 
since the management component uses the info level when tracing is not available
(prior to the initial configuration load). Furthermore, the data component uses
the info level in all of its regular status logging. If one desires to observe 
the normal behavior of the system, setting up the syslog file in this way is 
necessary.
-# In this section of the configuration, we direct the data component’s package 
to be installed on the PIC. We define an object-cache size to use. It is 
recommended to use the maximum since it is the only application using the object
cache. For the Type-1 MS PIC that we are using, 512 MB is the maximum. Here 
we set the number of control cores to the minimum number of 1, and data cores to
5. Of the 8 total cores in the MS PIC’s CPU, this leaves 2 cores as user cores. 
When 2 user cores are available the server monitor will run as a separate thread
on a single virtual CPU (hardware thread) in one user core, and in the other 
user core on a single virtual CPU (hardware thread), the main thread of the 
process runs. This main thread’s responsibilities are the aging, receiving 
general configuration, and all aspects of communication with the management 
component. This configuration could equally happen with any configuration that 
leaves two or more user cores (for example 4 data cores and 2 control cores, or 
1 data core and 1 control core). Had we set the number of data cores to 6 
instead of 5, there would only be one user core available. In this scenario, the
server monitor’s thread and main thread run on this same user core, but in 
separate virtual CPUs (hardware threads). This configuration could equally 
happen with any configuration that leaves only one user core (for example 4 data
cores and 3 control cores). Lastly, if we configure the MS PIC here, leaving no 
user cores available, then the server monitor run on the main (software) thread 
which will run on all available control cores. In summary, the system’s main 
thread always starts on the control cores, but immediately detects the number of
user cores available, and configures itself to take advantage of as many user 
cores as it can, up to a maximum of two user cores. Note that the system will 
never run a control core and a user core, and generally running on a control 
core will mean lower performance for the data component overall since all the 
threads depend on each others’ performance due to locking that occurs to protect
data access in memory shared between the threads.
-# We give the IP address 192.168.2.1 to the fe-1/1/0.0 interface. Attached to 
this interface is the subnet 192.168.2.0/24. The traffic of the clients of web 
servers in our sample setup come into the router through this interface; 
however, the system would work fine and equally if the traffic came in through 
any other interface except fe-1/1/3.0, since that is where the servers are for 
this traffic.
-# We give the IP address 192.168.0.1 to the fe-1/1/3.0 interface. Attached to 
this interface is the subnet 192.168.0.0/24. We also attach the sync service 
set to this interface in both directions, which, if configured correctly, should
imply that all of the servers for all of the applications found in this service 
set can be reached out of this interface. See the details of comment 7 below for
more on this.
-# This comment shows the address 192.168.1.2 being used as the address on the 
ms-1/2/0.0 interface. Any service sets that use this IFL interface as their 
service interface will have all servers of all applications probed using this 
address as the probe source. These probes are of course the probes from the 
server monitor.
-# In this section we configure an optional part of the system’s configuration, 
but it may be useful for debugging should any problems arise. This configuration
is not meant for production environments. Here we use the pc prefixed form of 
the MS PIC’s interface name. Under this we may set some debugging configurations
such as debugger-on-panic.
-# This section is not always necessary, and it is related to the configuration 
comment number 4 as well. First of all note that in our setup scenario here, we 
have the façade address and the servers of application www in the same subnet 
which is directly attached to the fe-1/1/3.0 interface. Because it is directly 
attached and the façade address falls into that network, when the router 
receives traffic destined to the façade, it will try to resolve the façade 
address to a MAC address in that subnet. Of course, the façade’s address is 
phony, and there is no host with that address so no MAC address can be found 
for it. If this is the case, the router will drop the packet since it does not 
know where to send it. Of course this is not what we want; thus, we need to 
force the router to send the packet out the interface so that the sync service 
set can be applied to the traffic. When that happens the data component will 
replace the façade address with a real address of a server that is reachable 
and known to be up. To achieve this, we setup the static route in this section. 
It forces the router to try to route traffic destined to the façade to a 
machine that we know is reachable (this can be a server, or any machine). In so 
doing, the traffic is passed through the sync service set in the egress 
direction, and the load balancing can take place as described. As mentioned, 
creating this configuration is optional. It is only necessary when the façade 
address falls into a subnet that is directly attached (as it does here), and is 
not the address of a machine present in that subnet. If, for example, there was 
at least one other network between the servers’ subnet and the given router, 
then (assuming that the façade’s address falls into the same subnet as the 
servers) the router would just route the traffic without any problem through 
the outgoing interface where the service set would be applied.
-# Here we have overridden the default value of 900 for the session timeout. We 
reduce it to only 60 seconds for the sample setup only so that it is more easily 
observable that the sessions do timeout whereby the information for the flow is 
cleaned out, and they are no longer counted as part of a server’s load. This is 
observable through the “show sync equilibrium status” command.
-# Here we just wish to indicate, again, that it is possible to have multiple 
applications per service set, and that we can create phony servers as well. 
Watching the messages syslog file, one would see that the probes to the servers 
of application abc timeout quickly, as in our case they are not reachable.

*/

#include <sync/common.h>
#include <jnx/pmon.h>
#include "equilibrium-mgmt_main.h"
#include "equilibrium-mgmt_config.h"
#include "equilibrium-mgmt_conn.h"
#include "equilibrium-mgmt_kcom.h"
#include "equilibrium-mgmt_logging.h"

#include EQUILIBRIUM_OUT_H
#include EQUILIBRIUM_SEQUENCE_H

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_EQUILIBRIUM_MGMT    "equilibrium-mgmt"


/**
 * The heartbeat interval to use when setting up process health monitoring
 */
#define PMON_HB_INTERVAL   30


/*** Data Structures ***/

/**
 * CMD Mode menu tree of commands:
 * (This is defined at the end of equilibrium-mgmt_ui.c)
 */
extern const parse_menu_t master_menu[];

static junos_pmon_context_t pmon; ///< health monitoring handle

/*** STATIC/INTERNAL Functions ***/


/**
 * Shutdown app upon a SIGTERM.
 * 
 * @param[in] signal_ign
 *     Always SIGTERM (ignored)
 */
static void
equilibrium_sigterm(int signal_ign __unused)
{
    equilibrium_shutdown(TRUE);
}

/**
 * Start server upon resync completion
 *
 * @param[in] ctx
 *     App event ctx
 */
static void
equilibrium_start_server(void* ctx)
{
    evContext ev = *(evContext*)ctx;

    if (init_server(ev)) {
        LOG(LOG_ERR, "%s: init_server: failed",__func__);
        equilibrium_shutdown(TRUE);
    }
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
equilibrium_init (evContext ctx)
{
    pmon = NULL;
    struct timespec interval;
    
    junos_sig_register(SIGTERM, equilibrium_sigterm);

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
    
    if(junos_kcom_resync(equilibrium_start_server, &ctx)) {
        goto init_fail;
    }
    
    return SUCCESS;
    
init_fail:

    LOG(LOG_CRIT, "initialization failed");

    kcom_shutdown();
    clear_config();
    close_connections();
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
equilibrium_shutdown(boolean do_exit)
{
    kcom_shutdown();
    clear_config();
    clear_ssrb_config();
    close_connections();
    junos_pmon_destroy_context(&pmon);
    
    if(do_exit) {
        LOG(TRACE_LOG_INFO, "equilibrium-mgmt shutting down");
        exit(0);
    }
}


/**
 * Intializes equilibrium-mgmt's environment
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
    const char * eq_trace_config[] = {
            DDLNAME_SYNC, DDLNAME_SYNC_EQUILIBRIUM, NULL};
    int ret = 0;
    junos_sdk_app_ctx_t ctx;
    
    // create an application context
    ctx = junos_create_app_ctx(argc, argv, DNAME_EQUILIBRIUM_MGMT,
                 master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    // set config read call back
    ret = junos_set_app_cb_config_read(ctx, equilibrium_config_read);
    if (ret < 0) {
        junos_destroy_app_ctx(ctx); 
        return ret;
    }

    // set init call back
    ret = junos_set_app_cb_init(ctx, equilibrium_init);
    if (ret < 0) {
        junos_destroy_app_ctx(ctx); 
        return ret;
    }
    
    // set trace options DDL path
    ret = junos_set_app_cfg_trace_path(ctx, eq_trace_config);
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

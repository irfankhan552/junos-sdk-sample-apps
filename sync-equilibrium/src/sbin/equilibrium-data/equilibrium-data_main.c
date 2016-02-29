/*
 * $Id: equilibrium-data_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-data_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as a MSP daemon 
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

\subsection data_comp_sec The Data Component

In this section, we feature the operation of the data component. Generally it 
is responsible for the main load balancing behaviour, receiving configuration 
from the management component, monitoring the servers, aging the sessions, 
and sending status updates to the management component.

\subsubsection sess_sec Maintaining the Session Information

Upon initialization, the data component will create and initialize a session 
(flow) lookup table in object cache. For this reason, it is important that an 
object-cache size is configured as large as possible. This lookup table is 
essentially a hashtable, and used for looking up forwarding information for 
flows that we have already seen. This table is very centric to performing the 
duties of load balancing and the session aging, which essentially is flow aging 
since the last-observed traffic.

The size of the table supported is also important for performance and depends 
on future testing results, but as a preliminary estimate we will setup the size 
of the lookup table to store half a million flow hashes. A flow hash is simply 
the output of some hash function with the input being the source and destination
addresses in this case. The hash function output can be thought of in terms of 
number of bits, commonly measured in the width. The wider the output of the hash
function is, the larger the table must be, since this table will map (index) a 
hash output to a table entry. In our table, an entry may contain information for
multiple existing flows which we go on to call session entries.

Here there is a tradeoff between the table size and the speed of a lookup. This 
is caused by a larger table (and hash output) having fewer collisions, since our
hash function will have a wider input than output. When a collision happens, 
more than one flow's information ends up in a list attached to each table entry.
Having to search this list for an exact match takes extra time, and the lists 
can be expected to be shorter when the size of the lookup table is larger.

\subsubsection lb_sec Load Balancing Behaviour in the Data Path

In this section, our focus will involve the processing of a packet to 
effectively achieve the load balancing behaviour. The easiest way to describe 
and trace the behaviour is in a set of processing steps which is somewhat more 
abstract than pseudocode. The set of processing steps is outlined below.

-# Receive the packet
-# If it is not TCP, then forward it back out immediately; otherwise continue
-# (FAST PATH) Lookup the session matching the addresses in the lookup table, 
and upon a match with the addresses, we also examine the ports. If the traffic 
is ingress traffic the source port must match. If it is egress traffic, the 
destination port must match. If there is no match we re-lookup as in step 3 
until we have a match. (This only involves searching the list at a given table 
entry).
    -# If we find a match and the flow is for an application, then interchange 
    the necessary address using the session information stored in the matching 
    entry. For egress traffic this means we load balance to the server stored 
    in the session information (to preserve the session) and change the 
    destination address to that of the server's. For ingress traffic, we change 
    the source address to the façade's address, since the client is expecting 
    the reply from that address. If we found a match but the entry indicates 
    that it is not application traffic, there is nothing to do. Once this is 
    done, forward the packet back out immediately; otherwise we continue.
    -# In this case, we did not find a match. If traffic is ingress traffic 
    (it is coming from the direction of the cluster), then we forward the 
    packet back out immediately; otherwise continue to what we call the slow 
    processing path.
-# (SLOW PATH) At this point we know the traffic is egress traffic (it is going 
in the direction of the cluster), and we know we do not have a session for this 
traffic. Therefore, we must do more processing on the packet to see if the 
traffic matches some application's parameters, for all the applications in the 
applicable service set (recall we receive the service set id with each packet).
-# Now we check to see if the packet's destination address and destination port 
match that of an application within the applications in the matching service 
set.
    -# If we find a match, then we must find the server with the least load 
    (the list will be kept sorted to expedite this) and use it as the server 
    for this new session. Session entries in the table will be created with the 
    needed information for the flow in both directions.
    -# Otherwise there is no match, so this traffic does not belong to an 
    application, but we still create a flow entry in the table for it (for 
    faster lookups in the future); however, the age timeout of this flow is set 
    to 5 minutes. This timeout value is not configurable. Lastly the packet is 
    forwarded out immediately.
-# Finally, for all egress packets in the above steps 3a) and 5a), we apply URL 
filtering. This means we search for the façade's address in any HTTP request's 
"Server" field. We if find it, we replace it with the real address of the 
server serving the session. Note that normally a domain name is used in the 
HTTP request, in which case no change is made; however, since this is not 
guaranteed, we must search for IP addresses as well. (This feature is
currently not present due to the complexity of making changes in the size of a 
TCP segment--it impacts subsequent segments; it is planned to be added in the 
future).
-# Lastly, since all egress packets are being searched for an HTTP request 
header, we also log each request with the server that it is directed to. This 
is done again via syslog, but the messages are pushed up to the control plane 
syslog file.

An important assumption here is that when dealing with an IP fragment, we 
always observe the first fragment first, otherwise the packet is forwarded out 
normally (until we see the first fragment). This is necessary, since no port 
information is present in trailing fragments, so we cannot determine if the 
traffic matches that of an application. Also, when we do see the first 
fragment, we must store the fragment ID, and match on this in trailing 
fragments as an alternative to the ports in step 3.

Another assumption we make is that since HTTP requests may get fragmented (at 
various levels), we enforce that we must observe the start of the HTTP request 
header and the Server field in one packet.

\subsubsection rcv_cnf_sec Receiving the Configuration

This section is the counterpart to Section "Loading the Configuration" in the 
equilibrium-mgmt daemon's documentation, and will focus on receiving the data 
that the management component sends.

Essentially the information comes in forms of data with an action of added, 
changed, or deleted. When configuration is added, it will affect only the 
stored configuration state applied to new sessions. However, changes and 
deletions can cause the session entry information in the lookup table to become 
stale; therefore, the configuration manager will remove session entries 
containing the deleted or changed configuration. That means a configuration 
change with renames and deletes can affect existing flows and ongoing HTTP 
sessions.

\subsubsection mon_srv_sec Monitoring the Servers

In this section we concentrate on the monitoring behaviour. As mentioned in 
Section \ref cnf_ui_sec "Configuration User Interface", the monitoring for 
each application can be customized. The monitoring manager, scheduled on its 
own thread, will perform this task. If possible, it will run attached to a 
user-type hardware thread (in a user core).

When the data component receives information about a server within an 
application, it will also be told how to monitor it if at all. When a server is 
to be monitored, it is assumed to be down until the manager validates that it 
is up. When it first transitions to the up state, it is inserted into a list of 
servers for the given application. Correspondingly, when a server transitions 
to the down state, it is removed from the list of servers for the given 
application. When such a state transition occurs, a message is sent to the 
management component indicating the server's status.

Furthermore, the monitoring manager has an extra duty when a server goes down. 
In order to expedite the client being able to reach a new server, the 
monitoring manager will iterate over the session entries in the flow table 
searching for flows that are using the server. If any entries are found, they 
are removed immediately.

When the monitoring manager probes a server, it requests the root (/) resource 
from it. As long as a valid HTTP response is returned, the server is assumed up 
regardless of the response status code.

\subsubsection aging_sec Aging the Existing Sessions

In this section we concentrate on the aging behaviour. As mentioned in 
Section \ref cnf_ui_sec "Configuration User Interface", the session aging for 
each application can be customized using the session-timeout parameter. The 
aging manager, scheduled on its own thread, will perform this task. If possible 
it will run attached to a user-type hardware thread. This thread has the shared 
responsibility of receiving the configuration updates, and it is also the main 
thread initially bound to a control-type hardware thread.

The aging manager is only scheduled to run every 15 seconds. Its duty is to 
iterate over the lookup table entries and remove session entries in the table 
when they have expired. To do this it compares the current time to the session 
expiration time which was last updated when a packet passed through the system 
matching this session.

As it does this iteration, it also counts the number of active sessions for 
each application in each service set. When the iteration has finished it 
reports these counters to the management component.

\subsubsection snd_sec Sending Status Updates

This section is the counterpart to Section "Receiving Status Updates" in the 
equilibrium-mgmt daemon's documentation, and will focus on sending the status 
updates to the management component. There are two kinds of information that 
the data component sends to the management component.

In the first case, the monitoring manager will be sending status updates when a 
server's state changes. The maximum frequency of these updates depends on the 
monitor's configuration of course. Secondly, the aging manager sends status 
updates about the number of sessions that are assumed to still be active. The 
frequency of these updates is approximately every 15 seconds, but the content 
of the message, being the number of active flows, again depends on the 
session-timeout configuration. Generally we can expect larger numbers when a 
longer session-timeout value is configured.

Finally, although the status updates come from managers running on separate 
threads, the update messages are funneled into a common channel, so that only 
one connection to the management component is needed for this.


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

#include "equilibrium-data_main.h"
#include "equilibrium-data_config.h"
#include "equilibrium-data_conn.h"
#include "equilibrium-data_monitor.h"
#include "equilibrium-data_packet.h"

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_EQUILIBRIUM_DATA    "equilibrium-data"

/*** Data Structures ***/

static evContext mainctx; ///< the event context of this main thread

/*** STATIC/INTERNAL Functions ***/


/**
 * This function quits the application does an exit
 */
static void 
equilibrium_quit(int signo __unused)
{
    LOG(LOG_INFO, "Shutting down");
    
    close_connections();
    
    LOG(LOG_INFO, "Shutting down data loops");
    destroy_packet_loops(mainctx);
    LOG(LOG_INFO, "Shutting down data loops...done");
    
    clear_config();
    
    LOG(LOG_INFO, "Stopping server montior");
    shutdown_monitor();
    
    msp_exit();
    
    LOG(LOG_INFO, "Shutting down finished");
    
    exit(0);
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
equilibrium_init(evContext ctx)
{
    int rc;
    int other_user_cpu, tmp_cpu;
    int monitor_user_cpu;
    int mon_core_num, tmp_core_num;

    msp_init();
    
    // Handle some signals that we may receive:
    signal(SIGTERM, equilibrium_quit); // call quit fnc
    signal(SIGHUP, SIG_IGN); // ignore
    signal(SIGPIPE, SIG_IGN); // ignore
    
    logging_set_level(LOG_INFO);
    logging_set_mode(LOGGING_SYSLOG);
    
    // call init function for modules:
    
    mainctx = ctx;
    
    rc = init_config(ctx);
    if(rc != SUCCESS) {
        goto failed;
    }
    
    rc = init_connections(ctx);
    if(rc != SUCCESS) {
        goto failed;
    }
    
    // give the monitor a user cpu number to use
    
    monitor_user_cpu = msp_env_get_next_user_cpu(MSP_NEXT_NONE);
    
    rc = init_monitor(ctx, monitor_user_cpu);
    if(rc != SUCCESS) {
        goto failed;
    }
    
    // find another user cpu number to use for this main thread
    tmp_cpu = other_user_cpu = msp_env_get_next_user_cpu(monitor_user_cpu);
    
    // if at least one valid user cpu available...
    if(monitor_user_cpu != MSP_NEXT_END && other_user_cpu != MSP_NEXT_END) {

        mon_core_num = msp_env_cpu_to_core(monitor_user_cpu);
        tmp_core_num = msp_env_cpu_to_core(tmp_cpu);
        
        // try to find a user cpu on a different core from the monitoring
        // thread if one exists
        while(mon_core_num == tmp_core_num) {
            if((tmp_cpu = msp_env_get_next_user_cpu(tmp_cpu)) == MSP_NEXT_END) {
                break;
            }
            tmp_core_num = msp_env_cpu_to_core(tmp_cpu);
        }
        
        if(tmp_cpu != MSP_NEXT_END) { // it should be on another user core
            other_user_cpu = tmp_cpu;
        }
        // else it will be on the same user core as the monitoring thread
        
        LOG(LOG_INFO, "%s: Binding main process to user cpu %d",
                __func__, other_user_cpu);
        
        if(msp_process_bind(other_user_cpu) != SUCCESS) {
            goto failed;
        }
    }
    
    rc = init_packet_loops(ctx);
    if(rc != SUCCESS) {
        goto failed;
    }

    return SUCCESS;
    
failed:

    LOG(LOG_ALERT, "%s: Failed to intialize. "
            "See error log for details ", __func__);
    equilibrium_quit(0);
    return EFAIL;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Intialize equilibrium's environment
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

    app_ctx = msp_create_app_ctx(argc, argv, DNAME_EQUILIBRIUM_DATA);
    msp_set_app_cb_init(app_ctx, equilibrium_init);

    rc = msp_app_init(app_ctx);
    
    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


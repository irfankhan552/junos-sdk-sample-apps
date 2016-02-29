/*
 * $Id: monitube-data_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-data_main.c
 * @brief Contains main entry point
 *
 * Contains the main entry point and registers the application as a MSP daemon
 */

/* The Application and This Daemon's Documentation: */

/**

\mainpage

\section overview_sec Overview


This is a sample application that is part of the sync-monitube package.

This documentation contains the functional specifications for the SDK Your Net 
Corporation (SYNC) Monitube Project. This project consist of the development of 
a basic IPTV monitoring system (herein the system) designed to operate quickly 
in the network data path on the Juniper Networks MultiServices PIC hardware 
module. This makes the system suitable for deployment on Juniper Networks M-, 
MX- and T-Series routers. The system will be implemented using Juniper's Partner 
Solution Development Platform (PSDP), also called the JUNOS SDK. This system 
is targeted to operate with version 9.2 of JUNOS and beyond. In 9.6 it was 
updated to support running the data component on multiple MS PICs/DPCs. 

The system functionality will include monitoring selected Real-time Transfer 
Protocol (RTP) streams for calculation of their media delivery index (MDI) 
(RFC 4445). The system is designed to be deployed on any nodes that pass IPTV 
broadcast service or video on demand (VoD) streams which are respectively 
delivered over IP multicast and unicast. This document assumes the reader has 
basic knowledge of IPTV and MDI.

In version 2.0 (9.3) of this project we add an optional feature for high 
availability, including data replication across two MultiServices PICs, and 
manual switchover between them. In version 3.0 (9.6) this was disabled because
with the support of multiple sampling targets and MoniTube running on many PICs,
we have not added the ability to specify which is master and which is slave. 
This may be re-added in the future when/if rms interfaces are supported with
sampling. The code to do the replication has been left in the monitube-data 
daemon (data component) for demonstration purposes.

For configuration and behavior of past versions, please see the documentation
for that version. 

\section functionality_sec Functionality

In this section we detail the functionality of the system. As described in the 
\ref cnf_ui_sec "Overview" section, a basic design assumption and prerequisite 
is that the system will operate in the data path using the Juniper Networks 
MultiServices PIC hardware module. Although this implies that the system's 
software will be chiefly running in the router's data plane, we will also 
create a management counterpart in the router's control plane to control the 
behaviour of the monitoring. Accordingly, we introduce the concept of dividing 
the system into the data (data-plane) component and the management component.

While examining the functionality of the system, we discuss the operation of 
both the management and data components. We start by examining the system's 
user interface, and progress to the operations of the management component 
which will interact with the user interface. We then turn to the management 
component's communications with the data component in order to appropriately 
direct the data component as well as receive status updates from it. Finally, 
we dive into the details of the data component itself.

\subsection ui_sec User Interface

The user interface of the system is of course a consequent of the JUNOS user 
interface. Thus, it uses a dual organization into a configuration user 
interface and a command user interface. In this section we examine both in turn.

\subsubsection conf_ui_sec Configuration User Interface

In this section we examine the total of all possible configuration options as 
well as some example use cases.

The new configuration possibilities are organization into the JUNOS hierarchy 
of forwarding-options as follows in Figure 1. We begin by looking at each of 
the new additions individually for further explanation.

\verbatim

 1 sync {
 2     monitube {
 3         monitor monitor-name {
 4             monitored-networks {
 5                 a.b.c.d/i;
 6                 e.f.g.h/j;
 7             }
 8             rate X;                              ## mandatory
 9         }
10         mirror q.r.s.t {
11             destination u.v.w.x;
12         }
13     }
14 }

\endverbatim
<b>Figure 1 – The system's extensions to the JUNOS configuration hierarchy.</b>

Line 3 shows the \c monitor object which configures a 
monitoring group. It groups a set of networks or hosts. Multiple groups can be 
configured through the use of these objects with varying identifiers 
(\c monitor-name). Line 4 shows the \c monitored-networks object which is a 
container for the prefixes for the monitor. Lines 5 and 6 show how a network or 
host (/32) address may be added. Up to 100 prefixes (addresses) may be added to 
a group. A network mask of less than 32 implies that all host addresses falling 
into the network will be monitored as observed. A given prefix should not fall 
into more than one monitor; however, the same network address with different 
prefix lengths is suitable to configure within one or more monitors if desired.

The monitor also has a \c rate attribute as shown on line 11. This is a 
mandatory attribute within each monitor object. It is used to specify the media 
bit-per-second rate found in the monitored streams. Its configurable range 
is 10,000 to 1,000,000,000 (10 Kbps to 1 Gbps). This should be sufficient as 
most IPTV video streams are in the range of 3.75 to 20 Mbps.

Lastly, another function is to \c mirror streams. The mirror object on line 10 
takes the destination IP address of the flow to monitor as its identifier. 
On line 11 its \c destination attribute specifies the IP address that the 
monitored traffic will be mirrored to. Beware of mirroring too many flows to 
one destination if the bandwidth to handle it is not available.

When the system receives a packet, if it is destined to an address captured in 
one of the groups (the system does a best-match lookup with the address within 
all the prefixes), it is assumed that if it is RTP traffic, it is carrying a 
media stream payload with a drain rate equal to the rate configured in the 
group. This information is essential for calculating an accurate MDI for the 
given flow.

A sample use case is shown in Figure 2. In this sample scenario, we setup 
monitoring for a number of different IPTV channels (sent through multicast) 
at various rates. We also monitor VoD to all subscribers assuming that the 
streams' media bit rates are all the same. Mirroring of two selected IPTV 
channels is also configured. In the sample we also show the configuration for 
enabling the system's tracing, which is normally only done for debugging 
purposes; nonetheless, it is a new addition to the configuration, and thus we 
show it here.

In the sample we also attach the filter to the egress interface. Doing so 
with the ingress interface may be more desirable in some cases, because you 
should not attach the sampling filter to more than one egress interface (or 
the egress and ingress interfaces), otherwise the PIC will receive two copies 
of the same packet. This will result in incorrect mirroring and monitoring 
statistics, and it is up to the administrator to configure the filters 
appropriately.

We also configure the data component package to be installed on two MS PICs. 

\verbatim

chassis {
    fpc 0 {
        sampling-instance inst1;
    }
    fpc 1 {
        ## will use global sampling instance
        
        pic 1 {
            adaptive-services {
                service-package {
                    extension-provider {
                        control-cores 1;
                        data-cores 6;
                        object-cache-size 512;
                        forwarding-database-size 20;
                        package sync-monitube-data;
                    }
                }
            }
        }
        pic 2 {
            adaptive-services {
                service-package {
                    extension-provider {
                        control-cores 1;
                        data-cores 6;
                        object-cache-size 512;
                        forwarding-database-size 20;
                        package sync-monitube-data;
                    }
                }
            }
        }
    }
}
interfaces {
    ms-1/1/0 {
        unit 0 {
            family inet;
        }
    }
    ms-1/2/0 {
        unit 0 {
            family inet;
        }
    }
    fe-1/0/0 {                      ## example of attaching to ingress
        unit 0 {                    ## interface; egress not possible with mcast
            family inet {
                filter {
                    input monitor;        ## apply firewall filter here
                } 
                address 30.31.32.1/18;    ## server facing interface
            }
        }
    }
}
forwarding-options {
    sampling {
        input {
            rate 1;
        }
        family inet {  ## global sampling instance
            output {
                interface ms-1/1/0;             ## ms prefix required
                extension-service monitube-iptv;
            }
        }
        instance inst1 {
            input {
                rate 1;
            }
            family inet {
                output {
                    interface ms-1/2/0;             ## ms prefix required
                    extension-service monitube-iptv2;
                }
            }
        }
    }
}
firewall {
    family inet { 
        filter monitor {  ## create a firewall filter for sampling all UDP
            term sample-all {
                from {
                    protocol udp;

                    ## In another user case, one may also want:
                    ## destination-address 224.0.0.0/4;
                }
                then {
                    sample;
                    accept;
                }
            }
            term override-discard {
                then {
                    accept;
                }
            }
        }
    }
}
sync {
    monitube {
        monitor sd-bcast-fox {
            monitored-networks {
                224.0.3.8/29; ## monitor 8 SD channels (.8 - .15)
            }
            rate 3750000;
        }
        monitor hd-bcast-espn {
            monitored-networks {
                224.1.3.16/30;  ## monitor 4 HD channels (.16 - .19)
                224.1.4.88/32;  ## monitor additional HD channel
                224.1.4.176/32; ## monitor additional HD channel
            }
            rate 19500000;
        }
        monitor ppv-vod-dvdmovie {
            monitored-networks {
                30.31.32.0/24; ## monitor 480p PPV channels

            }
            rate 9000000;
        }
        mirror 224.1.4.54 {
            destination 10.10.1.47;
        }
        mirror 224.1.4.55 {
            destination 10.10.1.48;
        }
        traceoptions {                 ## only used for debugging purposes
            file monitube.trace;
            flag all;
            syslog;
        }
    }
}

\endverbatim
<b>Figure 2 – A sample configuration.</b>

\subsubsection cmd_ui_sec Command User Interface

In this section we examine the new commands added to JUNOS as well as some 
preliminary sample output.

There are only two new commands that will be added. They are the "show 
sync monitube statistics" command, and the "clear sync monitube statistics" 
command. Either of these can take one optional parameter; namely, the group 
name from a monitor group defined in the configuration. Passing in 
this parameter will result in only the statistics for flows falling into that 
group being shown or cleared. Omitting it will simply output the results for 
(or effect) all configured groups.

The show command shows the MDI values for all monitored flows. By default the 
values shown are the latest received from the PIC which can come as often as 
every second; however, if the results are more than 5 minutes old they will be 
cleared automatically. The clear command can be used to clear statistics 
earlier on demand. Sample output is shown in Figure 3. The italicized letters 
in Figure 3 represent numbers.

Note: If a flow is analyzed multiple times (on different PICs), it may show up
in the list multiple times.

\verbatim

router> show sync monitube statistics name

Statistics for group name

Flow to          Last MDI     Max MDI      Min MDI      Average MDI   Total Loss
a.b.c.d:pppp     A:B          C:D          E:F          G:H           #
e.f.g.h:pppp     A:B          C:D          E:F          G:H           #
i.j.k.l:pppp     A:B          C:D          E:F          G:H           #
...
w.x.y.z:pppp     A:B          C:D          E:F          G:H           #

router> clear sync monitube statistics name

Cleared statistics for group name

\endverbatim
<b>Figure 3 – The system's extensions to the JUNOS command interface.</b>

\subsection data_comp_sec The Data Component

In this section, we feature the operation of the data component. Generally it
is responsible for the main monitoring and mirroring behaviour, receiving
configuration from the management component, state replication to another
similar data component on another PIC, and sending status updates to the
management component.

\subsubsection data1_sec Maintaining the Flow Information

Upon initialization, the data component will create and initialize a flow
lookup table in object cache. For this reason, it is important that an
object-cache size is configured as large as possible. This lookup table is
essentially a hashtable, and used for looking up forwarding information for
flows that we have already seen. This table is very centric to performing the
duties of monitoring and flow aging.

The size of the table supported is also important for performance and depends
on future testing results, but as a preliminary estimate we will setup the
size of the lookup table to store half a million flow hashes. A flow hash is
simply the output of some hash function with the input being a destination
address and destination port in this case. The hash function output can be
thought of in terms of number of bits, commonly this measure is referred to as
the width. The wider the output of the hash function is, the larger the table
must be, since this table will map (index) a hash output to a table entry. In
our table, an entry may contain information for multiple existing flows which
we go on to call flow entries.

Here there is a tradeoff between the table size and the speed of a lookup.
This is caused by a larger table (and hash output) having fewer collisions,
since our hash function will have a wider input than output. When a collision
happens, more than one flow’s information ends up in a list attached to each
table entry. Having to search this list for an exact match takes extra time,
and the lists can be expected to be shorter when the size of the lookup table
is larger (assuming the hash function provides an output with uniform
distribution over the output range).

\subsubsection data2_sec Monitoring and Mirroring Behaviour in the Data Path

In this section, our focus will involve the processing of a packet to
effectively achieve the monitoring and mirroring behaviour. The easiest way to
describe and trace the behaviour is in a set of processing steps which is
somewhat more abstract than pseudocode. The set of processing steps is outlined
below. Keep in mind that we are receiving sampled packets; therefore, they are
copies.

-# Receive the packet.
-# If it is not UDP, then send it out immediately; otherwise continue.
-# Lookup the flow matching the destination address and destination port* in
the lookup table; upon a match we proceed to step 4) (FAST PATH), otherwise we
proceed to step 3a) (SLOW PATH).
  -# In this case we are dealing with a new flow, and we do not know if the
  flow ought to be monitored or mirrored. We lookup the destination address
  in the configuration stored in the data component. If it falls into one of
  the monitoring groups, we store the rate in the flow’s state, so that we may
  calculate the MDI for the stream the flow is carrying. Similarly if the flow
  falls into one of the mirroring groups, we store the redirect destination in
  the flow’s state**. It may be possible that a flow is both monitored and
  mirrored. We now add the flow to the lookup table and proceed.
-# Given a known flow (match), if the flow is not marked as monitored or
mirrored, then we can simply discard the packet. First if the flow is marked
as monitored, we update the MDI state values. If it is the first packet in the
second time frame, and there was a packet in the previous time frame (1 second
is used for the MDI delay factor as suggested in RFC 4445; 10 seconds is used
for the MDI media loss rate), then we enqueue an update to be sent to the
management component with the previous MDI value for this flow. If the flow
is marked as mirrored, then we replace the destination address of the packet
with the configured destination address, update the necessary checksums, unflag
the packet as sampled, and send it out. If the flow is not marked as mirrored,
then the packet is discarded.

* An important assumption here is that when dealing with an IP fragment, we
* always observe the first fragment first; otherwise, the packets are discarded
* until we see the first fragment. This is necessary, since no port information
* is present in trailing fragments, so we cannot determine if the traffic
* matches that of a known flow. Also, when we do see the first fragment, we
* must store the fragment ID, and match on this in trailing fragments as an
* alternative to the destination port in step 3.

** The mirror destination is used as a lookup address in a search through the
* routing tables of all VRFs (routing instances). While searching for a VRF
* with a route record, we use the first match, and select that VRF as VRF to
* mirror out of. Therefore, all VRFs with routes for mirror destinations
* should have an IFL interface from the sampling MS PIC in the VRF.

\subsubsection data3_sec Receiving the Configuration

This section is the counterpart to the management component's Loading the
Configuration section, and will focus on receiving the data that the management
component sends.

Essentially the information comes in forms of data with an action of changed or
deleted. When configuration is added, it will affect only the stored
configuration state applied to new sessions. However, changes and deletions
can cause the known flows’ (in the lookup table) state information to become
stale. The configuration manager will remove flow entries from destinations
that are no longer monitored or mirrored. Changes in the configuration can
simply be applied to the flows’ state, but MDI statistics state will be reset.

\subsubsection data4_sec Aging the Existing Flows

In this section we concentrate on the aging behaviour. The aging manager,
scheduled on its own thread, will perform this task. If possible it will run
attached to a user-type hardware thread so that it has real-time priority. This
thread has the shared responsibility of receiving the configuration updates,
and it is also the main thread initially bound to a control-type hardware
thread.

The aging manager is only scheduled to run every 15 seconds. Its duty is to
iterate over the lookup table entries (known flows) and remove entries in the
table when they are older than 60 seconds. To do this it compares the current
time to the flow expiration time which was last updated when a packet passed
through the system matching the flow.

\subsubsection data5_sec Sending Status Updates

This section is the counterpart to the management component's Receiving
Status Updates section, and will focus on sending the status updates to the
management component. There is only the flows’ MDI information that the data
component sends to the management component.

As mentioned in the \ref data2_sec "Monitoring and Mirroring Behaviour in the
Data Path" section, the MDI delay factor and media loss rate
components are updated continually, but measured with time factors of 1 and
10 seconds respectively. Because the end of the time frame is not discovered
until it has passed, the MDI value for the time frame where the flow’s previous
packet was seen is sent when the first packet arrives in the current (new) time
frame. With streaming IPTV traffic, one can expect that there are packets seen
in every time frame; however, the MDI calculation can account for time frames
without packets seen.

Finally, the update messages are funneled into a common queue, so that only
one connection to the management component is needed for this. The data threads
enqueue updates, while the main thread dequeues them and sends them to the
management component.

\subsubsection data6_sec Replicating the Flow Information

As mentioned above, the data component can be configured as master or slave
by the management component. As of version 3.0 (9.6) this is no longer done, so 
all data components act as a master. 

If it is a slave, it does not process traffic,
so in receiving replication updates from the master it builds what it can of
the flow hashtable. In doing so, if it does become the master, it will in
effect pick up where the old master left off, up to the point where the old
master stopped notifying its slave (the new master).

As a master, the data component may replicate flow state information per flow
as fast as every second. This is triggered by two things. First, there must be
a change in the flow state, which happens whenever a packet of the flow is
received. Second, the configurable replication interval time must have gone by
since the last replication update to the slave. Furthermore, if a flow is
removed from the table, a delete update is sent for the flow.

The replication-interval configuration is hidden as of version 3.0 (9.6), but 
defaults to 3.

\section samp_setup Sample setup

In this section we show a sample configuration, and explain how to setup the 
system to get it running. 

These are the parts of the configuration that should be setup to activate and 
run the system properly. The comments included are numbered, and will 
subsequently be explained in more detail below.

\verbatim

system {
    syslog {
        ##
        ## 1. Show informational messages
        ##
        user * {
            any emergency;
        }
        file messages {
            any info;
            authorization info;
        }
    }
    extensions {
        providers {
            sync; ## Use the providerID in the certificate used for compiling
        }
    }
}
chassis {
    fpc 0 {
        pic 1 {
            adaptive-services {
                service-package {
                    extension-provider {
                        control-cores 1;
                        data-cores 6;
                        object-cache-size 512;
                        forwarding-db-size 20;
                        package sync-monitube-data;
                        syslog {
                            external info;
                        }
                    }
                }
            }
        }
    }
    fpc 1 {
        pic 2 {
            adaptive-services {
                service-package {
                    extension-provider {
                        ##
                        ## 2. Recommended settings for a Type-I MS PIC
                        ##    could be higher for others
                        ##
                        control-cores 1;
                        data-cores 6;
                        object-cache-size 512;
                        forwarding-db-size 20;
                        package sync-monitube-data;
                        syslog {
                            external info;
                        }
                    }
                }
            }
        }
    }
    fpc 2 {
        sampling-instance numero-uno;
    }
    fpc 3 {
        sampling-instance numero-uno;
    }
}
interfaces {
    ##
    ## 3. Server is on this network; note the firewall filter application
    ##
    fe-1/1/0 {
        unit 0 {
            family inet {
                filter {
                    input my-samp-filter;
                }
                address 192.168.2.1/24;
            }
        }
    }
    ##
    ## 4. Mirror destination is on this network
    ##
    fe-1/1/1 {
        unit 0 {
            family inet {
                address 10.227.7.230/24;
            }
        }
    }
    ##
    ## 5. Client is on this network
    ##
    fe-1/1/3 {
        unit 0 {
            family inet {
                address 192.168.0.1/24;
            }
        }
    }
    ms-0/1/0 {
        unit 0 {
            family inet;
        }
    }
    ms-1/2/0 {
        unit 0 {
            family inet;
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
    lo0 {
        unit 0 {
            family inet {
                address 192.168.1.1/32;
            }
        }
    }
}
forwarding-options {
    sampling {
        ##
        ## 7. Configure sampling to the MS PIC
        ##
        sample-once; ## optional
        input {
            rate 1;
        }
        family inet {
            output {
                interface ms-0/1/0;
                extension-service monitube-test;
            }
        }
        instance {
            numero-uno {
                input {
                    rate 1;
                }
                family inet {
                    output {
                        interface ms-1/2/0;
                        extension-service monitube-test2;
                    }
                }
            }
        }
    }
    family inet {
        filter {
            input sampling-filter2; ## table filter (applies to all interfaces)
        }
    }
}
##
## 8. Setup the router to enable IGMP and route Multicast using PIM
##
protocols {
    pim {
        interface fe-1/1/0.0 {
            mode dense;
        }
        interface fe-1/1/3.0 {
            mode dense;
        }
    }
}
##
## 9. Setup the firewall filter to perform the sampling
##
firewall {
    family inet {
        filter my-samp-filter {
            term rule1 {
                from {
                    destination-address {
                        224.0.0.0/4;
                    }
                    protocol udp;
                }
                then {
                    sample;
                    accept;
                }
            }
            term rule2 {
                then {
                    accept;
                }
            }
        }
        filter my-samp-filter2 {
            term rule1 {
                from {
                    destination-address {
                        224.0.0.0/4;
                    }
                    protocol udp;
                }
                then {
                    sample;
                    accept;
                }
            }
            term rule2 {
                then {
                    accept;
                }
            }
        }
    }
}
sync {
    monitube {
        ##
        ## 10. Configure MoniTube monitoring and mirroring
        ##
        monitor mon1 {
            rate 1245184; ## 1 Mbps video + 192 Kbps audio
            monitored-networks {
                226.0.1.4/30;
            }
        }
        mirror 226.0.1.5 {
            destination 10.227.1.45;
        }
        mirror 129.168.10.5 {
            destination 10.227.1.46;
        }
        traceoptions {
            file mon.tr;
            syslog;
            flag all;
        }
    }
}

\endverbatim
<b>Figure 4 – Sample excerpts of a system configuration.</b>


Here we present more detail on the configuration above by explaining the 
commented sections in detail. The number of the item below corresponds to the 
number in the comment from the Figure 4.

-# We have setup the syslog file called messages to receive messages from all 
facilities if they are at or above the informational level. This is important 
since the management component uses the info level when tracing is not 
available (prior to the initial configuration load). Furthermore, the data 
component uses the info level in all of its regular status logging. If one 
desires to observe the normal behavior of the system, setting up the syslog 
file in this way is necessary. Note the new syslog configuration under the 
chassis hierarchy as well.
-# In this section of the configuration, we direct the data component's package 
to be installed on the MS PIC. There is also the configuration for another 
backup or slave component on another PIC.We define an object-cache size to use. 
It is generally recommended to use the maximum. For the Type-1 MS PIC that we 
are using, 512 MB is the maximum. We also set the forwarding database 
size to 50 MB is has shown to be fine for approximately half a 
million routes. Here we set the number of control cores to the 
minimum number of 1, and data cores to 6. Of the 8 total cores in the MS PIC's 
CPU, this leaves 1 core as a user core. The system's main thread tries to bind 
itself to this user core upon startup, but if no user cores are available, it 
will run on the control core(s). This main thread's responsibilities are the 
aging, receiving general configuration, and all aspects of communication with 
the management component. Note that the system will never run on a control 
core and a user core, and generally running on control cores will mean lower 
performance for the data component overall, since all the threads depend on 
each others' performance due to locking that occurs to protect data access in 
memory shared between the threads.
-# We give the IP address 192.168.2.1 to the fe-1/1/0.0 interface. Attached 
to this interface is the network 192.168.2.0/24. In our sample setup we have 
the IPTV video server in this network configured with the address 192.168.2.2.
We also attach the my-samp-filter firewall filter to this interface in the input 
(ingress) direction. See the details of comment 9 below for more on this filter.
-# We give the IP address 10.227.7.230 to the fe-1/1/1.0 interface. Attached to 
this interface is the network 10.227.7.0/24. In our sample setup we have the 
mirror destination configured at an address that is reachable out this 
interface. In our case there is another router in between the end host we will 
see is 10.227.1.45; however, the mirror destination address could equally be 
in the 10.227.7.0/24 network (making it directly connected with respect to 
layer 3), or any other network that is routable out of any interface.
-# We give the IP address 192.168.0.1 to the fe-1/1/3.0 interface. Attached to 
this interface is the network 192.168.0.0/24. In our sample setup we have the 
IPTV video client in this network configured with the address 192.168.0.2.
-# In this section we configure an optional part of the system's configuration, 
but it may be useful for debugging should any problems arise. This 
configuration is normally not meant for production environments. Here we use 
the pc prefixed form of the MS PIC's interface name. Under this we may set 
some debugging configurations such as debugger-on-panic.
-# Under the 'forwarding-options sampling' hierarchy, configure where sampled 
packets will be sent. We have a global instance configuration that applies to 
all sampling in general. We also have another instance numero-uno. When sampling
happens on FPC 2 or 3 (any interface there or by the table filter there), then
instead of getting the packet sampled to ms-0/1/0 it will go to ms-1/2/0. This 
is governed by the sampling-instance configuration under chassis. Out data 
component is installed on both sampling targets.
-# Since our router is in between the IPTV video server and client, it needs 
to be configured to enable IGMP and Multicast routing. Here we setup the PIM 
routing protocol on the interfaces where we expect the multicast transmitters 
and receivers. Since we know our transmitter (server) will be 192.168.2.2 and 
our receiver will be 192.168.0.2, we enable PIM on the fe-1/1/0 and fe-1/1/3 
interfaces. For this setup we recommend using PIM in dense mode as shown.
-# Here we show the setup of the firewall filter that is eventually applied 
to the fe-1/1/0.0 interface. Using the filter's term from parameters we setup 
this filter to match all UDP multicast traffic. Using the filter's term then 
parameters we setup this filter to take all matched packets and sample them. 
The accept action is implied with sample, but it is a good idea to configure 
it, nonetheless, for those who are not aware of this. Looking at this firewall 
filter and the sampling configuration, it is easy to understand how the traffic 
matched by the firewall filter will be directed to the MS PIC, and hence our 
data component. We also added a similiar filter applied to the inet routing
tables.
-# Under this object is where most of the system's configuration 
lies. For our sample setup we will stream a multicast channel using the 
226.0.1.5 address. In our only monitor, mon1, we choose to set it up to 
capture and monitor traffic in the 226.0.1.4/30 prefix (226.0.1.4, 226.0.1.5, 
226.0.1.6, 226.0.1.7). This will thus capture our stream on 226.0.1.5. We will 
stream the IPTV channel using a video at a rate of 1024 Kbps and audio at a 
rate of 192 Kbps, thus we configure our media bit rate to 1,245,184 bps 
(1,216 Kbps) for monitor mon1. We also setup a mirror, to mirror any traffic 
going to 226.0.1.5 to 10.227.1.45. It would be expected that the host at 
10.227.1.45 contains a view station capable of viewing the IPTV traffic. 

Once the above configuration has been performed, setup the VideoLAN VLC 
software on the server and client hosts. This is freely available at 
http://www.videolan.org. Use VideoLAN VLC streaming how-to guide to easily 
setup a video stream. In this setup we use a lengthy video file as input on 
the server, and set the stream output setting to use:

- Protocol: RTP (Note this is RTP over UDP)
- Address:
- Port: 226.0.1.5:1234
- Encapsulation method: MPEG TS
- Transcoding Video Codec: mp4v (MPEG-4) or h264 (MPEG-4/AVC)
- Transcoding Video Bitrate (kb/s): 1024 (this is equivalent to 1,048,576 b/s)
- Transcoding Audio Codec: mp4a (AAC)
- Transcoding Video Bitrate (kb/s): 192 (this is equivalent to 196,608 b/s)
- Time-to-Live (TTL): 4


Using this setting one can start the playback (multicast) as desired from the 
server. When it is started, it may be viewed using another instance of VLC 
with the Media Resource Locator udp://\@226.0.1.5. This can also easily be 
setup by selecting Open Network Stream, and choosing UDP/RTP Multicast as the 
method with the 226.0.1.5 address. When this is done from the client host it 
generates an IGMP Join message to join the 226.0.1.5 multicast group, and 
subsequently the router begins sending the multicast out of the fe-1/1/3.0 
interface.

As the router is configured with PIM, as the server begins multicasting, and 
as the client joins or leaves the multicast group, it is educational and 
interesting to observe the changes in the output of the following commands:

- show igmp interface
- show igmp group
- show pim interface
- show pim join detail
- show pim source detail
- show multicast route extensive (note stats Kbps)
  (note forwarding state = Pruned/Forwarding)
- show multicast rpf 192.168.0.2
- show multicast rpf 192.168.2.2
- show multicast usage
- show route table inet.1

In addition, you can observe the MDI statistics changing using the new Monitube 
commands presented in the \ref conf_ui_sec "Configuration User Interface" 
section.

*/

#include "monitube-data_main.h"
#include "monitube-data_config.h"
#include "monitube-data_conn.h"
#include "monitube-data_ha.h"
#include "monitube-data_packet.h"
#include <jnx/radix.h>
#include <unistd.h>

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_MONITUBE_DATA    "monitube-data"

/*** Data Structures ***/

static evContext mainctx; ///< the event context of this main thread
static uint8_t init_conf; ///< has config been initialized 

/*** STATIC/INTERNAL Functions ***/


/**
 * This function quits the application does an exit
 */
static void
monitube_quit(int signo __unused)
{
    if(init_conf) {
        close_connections();
    
        stop_replication();
    
        stop_packet_loops(mainctx);
    
        clear_config();
    
        destroy_packet_loops_oc();
    }
    
    msp_exit();

    LOG(LOG_INFO, "Shutting down finished");

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
monitube_init(evContext ctx)
{
    int cpu;

    msp_init();

    // Handle some signals that we may receive:
    signal(SIGTERM, monitube_quit); // call quit fnc
    signal(SIGHUP, SIG_IGN); // ignore
    signal(SIGPIPE, SIG_IGN); // ignore

    logging_set_level(LOG_INFO);
    logging_set_mode(LOGGING_SYSLOG);

    // call init function for modules:

    mainctx = ctx;
    init_conf = 0;

    // find another user cpu number to use for this main thread
    cpu = msp_env_get_next_user_cpu(MSP_NEXT_NONE);

    // if at least one valid user cpu available...
    if(cpu != MSP_NEXT_END) {

        // else it will be on the same user core as the monitoring thread

        LOG(LOG_INFO, "%s: Binding main process to user cpu %d",
                __func__, cpu);

        if(msp_process_bind(cpu) != SUCCESS) {
            LOG(LOG_ALERT, "%s: Failed to initialize. Cannot bind to user cpu. "
                    "See error log for details ", __func__);
            monitube_quit(0);
            return EFAIL;
        }

    } else {
        LOG(LOG_EMERG, "%s: Main process will not be bound to a user cpu as "
            "expected. System cannot run without a user core.", __func__);
    }

    init_forwarding_database(ctx);

    return SUCCESS;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * There are several things that shouldn't happen until FDB is attached.
 * This is called only once it is attached.
 *
 * If any initialization fails the application exits
 */
void
init_application(void)
{
    int rc;

    rc = init_config(mainctx);
    if(rc != SUCCESS) {
        goto failed;
    }

    rc = init_connections(mainctx);
    if(rc != SUCCESS) {
        goto failed;
    }

    rc = init_packet_loops(mainctx);
    if(rc != SUCCESS) {
        goto failed;
    }
    
    init_conf = 1;

    return;

failed:

    LOG(LOG_ALERT, "%s: Failed to initialize. "
            "See error log for details ", __func__);
    monitube_quit(0);
}



/**
 * Intialize monitube's environment
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

    app_ctx = msp_create_app_ctx(argc, argv, DNAME_MONITUBE_DATA);
    msp_set_app_cb_init(app_ctx, monitube_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


/*
 * $Id: monitube-mgmt_main.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-mgmt_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as a JUNOS daemon 
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

\subsection mgmt_comp_sec The Management Component

In this section, we feature the operation of the management component. 
Generally, it is responsible for providing the user interface and sending the 
data component the system configuration. It also must get information from the 
data component in order to output information as shown in the \ref cmd_ui_sec 
"Command User Interface" section.

\subsubsection loading_sec Loading the Configuration

The details of this section mix implementation and functionality; hence, some 
internal details are present. This is attributable to the fact that simply 
committing a configuration for the system, does not mean that it is configured. 
The information needs to pass through a few phases which depend on other JUNOS 
processes and the data component. These parts behave independent of the 
management component, and thus coordination is needed which may affect the 
externally visible functionality of the management component.

Messages are logged as the configuration is loaded in these phases as to 
observe and understand the progress, and that the effects of the configuration 
may not appear immediately. Logging is done through syslog at the external 
facility and at the informational level. The MONITUBE log tag is used so that 
messages from this system may easily be filtered for viewing. Furthermore, 
configuring traceoptions will enable supplementary debug and informational 
logging to the configured trace file.

The management component needs to parse, validate and load the configuration 
hierarchy introduced in the \ref conf_ui_sec "Configuration User Interface" 
section. This is triggered by a configured change, and the data component may 
or may not be available and connected at the time. To cope with this challenge, 
the management component stores a record of the configuration actions to 
achieve its end goal of sending accurate configuration to the data component.

When the monitube extension-service configuration is found and examined for 
validity, all of the monitors' configurations and deleted 
monitors' configurations are stored with an action of changed, or 
deleted. The same thing is done with the configured mirrors. These records of 
configuration changes sit in a list until the data component is available to 
receive this information. This may already be the case (likely) if the data 
component is not in the process of starting for the first time. Nonetheless, 
at this point the list of each configuration change or delete record is 
processed, and with the pertinent action this information is sent to the data 
component in a message.

All configuration is passed to all connected data components.

\subsubsection receiving_sec Receiving Status Updates

The second major purpose of the management component is to receive status 
updates as they come and report them through the JUNOS CLI as requested via 
the show command presented in the \ref cmd_ui_sec 
"Command User Interface" section. The management component will 
not request status updates from the data component; it will just accept them 
as the data component chooses to send them. Therefore, the management component 
must cache a status, and in doing so it may deviate slightly from the true 
status in the data component. The status updates come in two forms. When any 
status update is received a message is logged via tracing.

The MDI of a flow is reported whenever there is traffic observed in the last 
second. By default, the management component does not know about any flows, 
but as it learns about them it keeps the result for a maximum of 5 minutes. 
If results are received for a flow that it already knows about, then the 
result replaces the previously stored result (and the 5 minute timer is 
reset). For more information on when these updates are sent see Sections 
in the data component documentation.

When a clear command is issued from the CLI and received, all stored results 
are deleted.

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

#include <sync/common.h>
#include <jnx/pmon.h>
#include "monitube-mgmt_main.h"
#include "monitube-mgmt_config.h"
#include "monitube-mgmt_conn.h"
#include "monitube-mgmt_kcom.h"
#include "monitube-mgmt_logging.h"

#include MONITUBE_OUT_H
#include MONITUBE_SEQUENCE_H

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_MONITUBE_MGMT    "monitube-mgmt"


/**
 * The heartbeat interval to use when setting up process health monitoring
 */
#define PMON_HB_INTERVAL   30


/*** Data Structures ***/

/**
 * CMD Mode menu tree of commands:
 * (This is defined at the end of monitube-mgmt_ui.c)
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
monitube_sigterm(int signal_ign __unused)
{
    monitube_shutdown(TRUE);
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
monitube_init (evContext ctx)
{
    struct timespec interval;
    
    pmon = NULL;
    
    junos_sig_register(SIGTERM, monitube_sigterm);

    init_config(ctx);
    
    if(kcom_init(ctx) != KCOM_OK) {
        goto init_fail;
    }
    
    if((pmon = junos_pmon_create_context()) == PMON_CONTEXT_INVALID) {
        LOG(LOG_ERR, "%s: health monitoring: initialization failed", __func__);
        goto init_fail;
    }

    if(junos_pmon_set_event_context(pmon, ctx)) {
        LOG(LOG_ERR, "%s: health monitoring: setup failed", __func__);
        // upon failure forget about this hb monitoring... run daemon anyway
    } else  {
        if(junos_pmon_select_backend(pmon, PMON_BACKEND_LOCAL, NULL)) {
            LOG(LOG_ERR, "%s: health monitoring: select backend failed",
                    __func__);
        } else {
            interval.tv_sec = PMON_HB_INTERVAL;
            interval.tv_nsec = 0;
            
            if(junos_pmon_heartbeat(pmon, &interval, 0)) {
                LOG(LOG_ERR, "%s: health monitoring: setup heartbeat failed",
                        __func__);
            }
        }
    }
    
    if(init_server(ctx)) {
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
monitube_shutdown(boolean do_exit)
{
    kcom_shutdown();
    clear_config();
    close_connections();
    
    if(pmon)
        junos_pmon_destroy_context(&pmon);
    
    if(do_exit) {
        LOG(TRACE_LOG_INFO, "monitube-mgmt shutting down");
        exit(0);
    }
}


/**
 * Intializes monitube-mgmt's environment
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
    const char * monitube_trace_config[] = 
        {DDLNAME_SYNC, DDLNAME_SYNC_MONITUBE, NULL};
    
    int ret = 0;
    junos_sdk_app_ctx_t ctx;
    
    // create an application context
    ctx = junos_create_app_ctx(argc, argv, DNAME_MONITUBE_MGMT,
                 master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    // set config read call back
    ret = junos_set_app_cb_config_read(ctx, monitube_config_read);
    if (ret < 0) {
        junos_destroy_app_ctx(ctx); 
        return ret;
    }
    
    ret = junos_set_app_cfg_trace_path(ctx, monitube_trace_config);
    if (ret < 0) {
        junos_destroy_app_ctx(ctx); 
        return ret;
    }
    
    // set init call back
    ret = junos_set_app_cb_init(ctx, monitube_init);
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

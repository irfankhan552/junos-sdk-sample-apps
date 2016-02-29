/*
 * $Id: monitube2-mgmt_main.c 407596 2010-10-29 10:52:37Z sunilbasker $
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
 * @file monitube2-mgmt_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as a JUNOS daemon 
 */

/* The Application and This Daemon's Documentation: */

/**
 
\mainpage

\section overview_sec Overview

This is a sample application that is part of the sync-monitube2-mgmt package.

This documentation contains the functional specifications for the SDK Your Net 
Corporation (SYNC) Monitube2 Project. This project consist of the development of 
a basic IPTV monitoring system (herein the system) designed to operate quickly 
in the network data path on the Juniper Networks MultiServices PIC hardware 
module. This makes the system suitable for deployment on Juniper Networks M-, 
MX- and T-Series routers. The system will be implemented using Juniper's Partner 
Solution Development Platform (PSDP), also called the JUNOS SDK. This system 
is targeted to operate with version 10.0 of JUNOS and beyond. The system's 
behavior is similar to that of the original SYNC MoniTube project, but with a 
different implementation so as to demonstrate the usage of a different model and
different APIs. The key difference in MoniTube2 compared to the MoniTube are:

- It uses the Services SDK's plug-in model
- It uses sampling service sets
- It uses the FPGA-generated timestamps in the jbuf meta-data
- It uses libsvcs-mgmt
- It uses the policy database
- It uses the recommended configuration organization of its policy for a service
using service sets 

The system functionality will include monitoring selected Real-time Transfer 
Protocol (RTP) streams for calculation of their media delivery index (MDI) 
(RFC 4445). The system is designed to be deployed on any nodes that pass IPTV 
broadcast service or video on demand (VoD) streams which are respectively 
delivered over IP multicast and unicast. This document assumes the reader has 
basic knowledge of IPTV and MDI.

For configuration and behavior of past versions or the original MoniTube sample
application project, please see the documentation for that version.


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
                        ##
                        ## 2. Recommended settings for a Type-I MS PIC
                        ##    could be higher for others
                        ##
                        control-cores 1;
                        data-cores 7;
                        object-cache-size 512;
                        forwarding-db-size 20;
                        policy-db-size 40;
                        package sync-monitube2-data;
                        syslog {
                            external info;
                            daemon any;
                            pfe any;
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
                        control-cores 1;
                        data-cores 7;
                        object-cache-size 512;
                        forwarding-db-size 20;
                        policy-db-size 40;
                        package sync-monitube2-data;
                        syslog {
                            external info;
                            daemon any;
                            pfe any;
                        }
                    }
                }
            }
        }
    }
    ##
    ## 3. Send traffic sampled in FPCs 2 and 3 to a special sampling instance
    ##
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
                interface ms-1/2/0;
                extension-service sample-to-ss2;
            }
        }
        instance {
            numero-uno {
                input {
                    rate 1;
                }
                family inet {
                    output {
                        interface ms-0/1/0;
                        extension-service sample-to-ss1;
                    }
                }
            }
        }
    }
    family inet {
        filter {
            input my-samp-filter2; ## table filter (applies to all interfaces)
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
                        226.0.0.0/8;
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
services {
    ##
    ## 10. Configure service sets and apply MoniTube2 rules
    ##
    service-set ss1 {
        sampling-service {
            service-interface ms-0/1/0;
        }
        extension-service monitube2-plugin { ## must have this name
            monitube2-rules r4;
        }
    }
    service-set ss2 {
        sampling-service {
            service-interface ms-1/2/0;
        }
        extension-service monitube2-plugin { ## must have this name
            monitube2-rules [ r1 r2 ]; ## can be a list
        }
    }
    ##
    ## 11. Configure MoniTube monitoring and mirroring rules
    ##
    sync {
        monitube2 {
            rule r1 {
                from {
                    destination-addresses {
                        226.0.1.4/24;
                        228.0.3.0/24;
                    }
                }
                then {
                    monitoring-rate 1245184; ## 1 Mbps video + 192 Kbps audio
                }
            }
            rule r2 {
                from {
                    destination-addresses {
                        226.0.1.4/30;
                    }
                }
                then {
                    monitoring-rate 1245184; ## 1 Mbps video + 192 Kbps audio
                    mirror-to 10.227.1.45;
                }
            }
            rule r3 {
                from {
                    destination-addresses {
                        0.0.0.0/0;
                    }
                }
                then {
                    mirror-to 10.227.7.234;
                }
            }
            rule r4 {
                from {
                    destination-addresses {
                        226.10.1.5/32;
                    }
                }
                then {
                    mirror-to 10.227.7.233;
                }
            }
            traceoptions {
                file m2.tr;
                syslog;
                flag all;
            }
        }
    }
}

\endverbatim
<b>Figure 1 – Sample excerpts of a system configuration.</b>

Here we present more detail on the configuration above by explaining the 
commented sections in detail. The number of the item below corresponds to the 
number in the comment from the Figure 1.

-# We have setup the syslog file called messages to receive messages from all 
facilities if they are at or above the informational level. This is important 
since the management component uses the info level when trace messages merge 
into syslog. Furthermore, the data component uses the info level in all of its 
regular status logging. If one desires to observe the normal behavior of the 
system, setting up the syslog file in this way is necessary. Note the new 
syslog configuration under the chassis hierarchy as well.
-# In this section of the configuration, we direct the data component's package 
to be installed on the MS PIC. We define an object-cache size to use. 
It is generally recommended to use the maximum. For the Type-1 MS PIC 
512 MB is the maximum. We also set the forwarding database 
size to 20 MB which should easily be be fine for ~200K routes.
We also set the policy database size to 40 MB which is more than adequate to 
hold most policy configurations for our application. 
Here we set the number of control cores to the 
minimum number of 1, and data cores to 7. Of the 8 total cores in the MS PIC's 
CPU, this leaves 0 user cores. The system's main control thread runs an internal
user CPU carved from core 0 or one of the data cores without a poller.
This main thread's responsibilities are processing general configuration,
and and all aspects of communication with the management component.
-# We give the IP address 192.168.2.1 to the fe-1/1/0.0 interface. Attached 
to this interface is the network 192.168.2.0/24. In our sample setup we have 
the IPTV video server in this network configured with the address 192.168.2.2.
We also attach the my-samp-filter firewall filter to this interface in the input 
(ingress) direction. See the details of comments below for more on this filter.
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
some debugging configurations such as debugger-on-panic. This prevents the PIC
from rebooting upon a crash that would normally cause a reboot.
-# Under the 'forwarding-options sampling' hierarchy, we configure where sampled 
packets will be sent. We have a global instance configuration that applies to 
all sampling in general. We also have another instance numero-uno. When sampling
happens on FPC 2 or 3 (any interface there or by the table filter there), then
instead of getting the packet sampled to ms-1/2/0 it will go to ms-0/1/0. This 
is governed by the sampling-instance configuration under chassis. Our data 
component is installed on both sampling targets, but we don't expect anything 
going this sampling instance in practice. It is merely for example purposes.
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
service. We also added a similar filter, my-samp-filter2, applied to the inet
routing tables under forwarding-options. This filter is for example purposes.
-# Here we configure the service sets. ss2 is the one we will use in practice,
the other is for example purposes along with the other sampling instance. In ss2
we have extended the configuration to support a one-line list of MoniTube2 
rules. The rule names must match rule names configured below. Only these rules
will be applied to the traffic of this service set. 
-# Under this object is where most of the new system's configuration 
lies. MoniTube2 policy configuration is grouped into rules consisting of 
mandatory from and then clauses. In the from clause, a list destination prefixes
to match against are specified. Any traffic falling into these prefixes will
match the rule. Once a rule match occurs, the rule's actions are taken. Actions
are specified in the then clause. One or two action can be specified. The 
monitoring-rate action specifies that the service should collect MDI statistics
on the traffic matching the rule. The mirror-to traffic specifies that the 
matching traffic should be mirrored to the given IP address. If of the multiple
applied rules prefixes overlap, the best (longest prefix) match takes precedence
. In the case of equal length prefix, the first applied rule takes precedence. 
For example, 226.0.1.5 will match rule 2.


For our sample setup we will stream a multicast channel using the 
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

In addition, you can observe the MDI statistics changing using the new Monitube2 
commands 'show sync monitube2 statistics'. You can clear the statistics with 
'clear sync monitube2 statistics'. 

*/

#include <sync/common.h>
#include <jnx/pmon.h>
#include <jnx/patricia.h>
#include "monitube2-mgmt_main.h"
#include "monitube2-mgmt_config.h"
#include "monitube2-mgmt_conn.h"
#include "monitube2-mgmt_kcom.h"
#include "monitube2-mgmt_logging.h"

#include MONITUBE2_OUT_H
#include MONITUBE2_SEQUENCE_H

/*** Constants ***/

/**
 * Constant string for the daemon name
 */
#define DNAME_MONITUBE_MGMT    "monitube2-mgmt"


/**
 * The heartbeat interval to use when setting up process health monitoring
 */
#define PMON_HB_INTERVAL   30


/*** Data Structures ***/

/**
 * CMD Mode menu tree of commands:
 * (This is defined at the end of monitube2-mgmt_ui.c)
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
 * Callback for KCOM resync completion event
 *
 * @param[in] ctx
 *     Event context
 */
static void
monitube_resync_done (void* ctx)
{
    evContext ev = *(evContext*)ctx;
    if (init_server(ev) != SUCCESS) {
        LOG(LOG_ERR, "%s: init_server: failed", __func__);
        monitube_shutdown(TRUE);
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
monitube_init (evContext ctx)
{
    struct timespec interval;
    
    pmon = NULL;
    
    junos_sig_register(SIGTERM, monitube_sigterm);

    if(kcom_init(ctx) != KCOM_OK) {
        goto init_fail;
    }
    
    init_config(ctx);

    if((pmon = junos_pmon_create_context()) == PMON_CONTEXT_INVALID) {
        LOG(LOG_ERR, "%s: health monitoring: initialization failed", __func__);
        goto init_fail;
    }

    if(junos_pmon_set_event_context(pmon, ctx)) {
        LOG(LOG_ERR, "%s: health monitoring: setup failed", __func__);
        goto init_fail;
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

    if (junos_kcom_resync(monitube_resync_done, &ctx) != KCOM_OK) {
        LOG(LOG_ERR, "%s: KCOM resync : failed", __func__);
        goto init_fail;
    }
    
    return SUCCESS;

init_fail:

    LOG(LOG_CRIT, "initialization failed");
    kcom_shutdown();
    clear_config();
    clear_stats();
//    close_connections();
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
    //kcom_shutdown();
    clear_config();
    clear_stats();
    //close_connections();
    
    if(pmon)
        junos_pmon_destroy_context(&pmon);
    
    if(do_exit) {
        LOG(TRACE_LOG_INFO, "monitube2-mgmt shutting down");
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
        {DDLNAME_SERVICES, DDLNAME_SYNC, DDLNAME_MONITUBE, NULL};
    
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

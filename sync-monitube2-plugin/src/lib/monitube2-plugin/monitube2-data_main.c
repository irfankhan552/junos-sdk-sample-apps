/*
 * $Id: monitube2-data_main.c 422753 2011-02-01 02:26:15Z thejesh $
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
 * @file monitube2-data_main.c
 * @brief Contains plug-in entry point and main handlers
 *
 */

/* The Application and This Plug-in's Documentation: */


/**
 
\mainpage

\section overview_sec Overview

This is a sample application that is part of the sync-monitube2-data package.

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


#include "monitube2-data_main.h"
#include <netinet/ip.h>
#include <netinet/udp.h>

#include <sys/jnx/jbuf.h>

#include <jnx/multi-svcs/msvcs_plugin.h>
#include <jnx/multi-svcs/msvcs_state.h>
#include <jnx/multi-svcs/msvcs_events.h>
#include <jnx/multi-svcs/msvcs_session.h>

#include <jnx/junos_kcom.h>
#include <jnx/junos_kcom_mpsdk_cfg.h>

#include <jnx/msp_pkt_utils.h>
#include <jnx/msp_fdb_api.h> 
#include <jnx/msp_hw_ts.h>
#include <jnx/ipc_msp_pub.h>

#include "monitube2-data_config.h"
#include "monitube2-data_conn.h"
#include "monitube2-data_rtp.h"
#include "monitube2-data_packet.h"

/*** Constants ***/

// PIC redundancy-state strings for KCOM_IFDEV_REDUNDANCY_STATE_* values
const char * waiting_state = "waiting for primary"; ///< WAITING_FOR_PRIMARY
const char * primary_state = "primary active"; ///< PRIMARY_ACTIVE
const char * secondary_state = "secondary active"; ///< SECONDARY_ACTIVE
const char * none_state = "none active"; ///< NONE_ACTIVE

// PIC redundancy operations for KCOM_IFDEV_REDUNDANCY_CMD_* values
const char * no_op = "none"; ///< NONE
const char * switch_op = "switch"; ///< SWITCH
const char * revert_op = "revert"; ///< REVERT


/*** Data Structures ***/

/**
 * The id assigned by mspmand unique over all plugins
 */
int plugin_id;

/**
 * The main evContext on the ctrl plane side
 */
evContext * ctx;

/**
 * The policy database handle
 */
msp_policy_db_handle_t pdb_handle;

/**
 * The shared mem handle for the PDB
 */
void * pdb_shm_handle;

static msp_oc_handle_t  entry_handle; ///< handle for OC flow entry allocator

static msp_fdb_handle_t fdb_handle;    ///< handle for FDB (forwarding DB)

static evTimerID        retry_timer;   ///< timer set to do fdb attach retry

/*** STATIC/INTERNAL Functions ***/


/**
 * Iterator callback as we search for a VRF
 *
 * @param[in] route_info
 *      The routing record info retrived from FDB
 *
 * @param[in] ctxt
 *      The user data passed into the callback msp_fdb_get_all_route_records
 *      (this was &flow->m_vrf)
 *
 * @return the iterator result to stop iterating
 */
static msp_fdb_iter_res_t
set_vrf(msp_fdb_rt_info_t * route_info, void * ctxt)
{
    if(route_info != NULL) {
        *((uint32_t *)ctxt) = route_info->rt_idx;
    }

    // once we found one, we'll assume it is good enough
    return msp_fdb_iter_stop;
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
retry_attach_fdb(evContext ectx UNUSED,
                 void * uap UNUSED,
                 struct timespec due UNUSED,
                 struct timespec inter UNUSED)
{
    int rc = msp_fdb_attach(NULL, &fdb_handle);

    if(rc == MSP_EAGAIN) {
        return; // will retry again later
    } else if(rc != MSP_OK) {
        CLOG(LOG_ALERT, "%s: Failed to attach to the forwarding database. Check"
                " that it is configured (Error code: %d)", __func__, rc);
        // we will keep trying, but something is probably wrong
    } else { // it succeeded
        evClearTimer(*ctx, retry_timer);
        evInitID(&retry_timer);
        CLOG(LOG_INFO, "%s: Attached to FDB", __func__);

        // Once FDB is attached, init the rest:

        init_config();
        init_connections();
    }
}


/**
 * Data event handler
 *
 * @param[in] ctx
 *      Pointer to data context
 *
 * @param[in] ev
 *      Data event
 *
 * @return Data handler status code
 */
static int monitube_data_hdlr(msvcs_data_context_t * md_ctx,
                              msvcs_data_event_t ev)
{
    msvcs_pub_data_req_t * req;
    msp_objcache_params_t ocp;
    jbuf_svc_set_info_t ss_info;
    struct jbuf * jb;
    struct ip * ip_hdr;
    int rc, cpu;
    uint16_t ip_options_bytes;
    uint32_t rate;
    in_addr_t mirror_addr;
    flow_entry_t * flow;

    switch (ev) {
    case MSVCS_DATA_EV_SM_INIT:

        // Should only get this once, and first

        DLOG(LOG_INFO, "%s: Initialization of shared memory event", __func__);

        bzero(&ocp, sizeof(ocp));
        ocp.oc_shm = md_ctx->sc_shm; // use the global SHM arena handle
        strlcpy(ocp.oc_name, PLUGIN_NAME "-flow entry OC", sizeof(ocp.oc_name));
        ocp.oc_size = sizeof(flow_entry_t);

        rc = msp_objcache_create(&ocp);

        if (rc) {
            DLOG(LOG_ERR, "%s: Failed to initialize an object cache", __func__);
            return rc;
        }

        // Save the OC handle
        entry_handle = ocp.oc;

        break;

    case MSVCS_DATA_EV_INIT:

        break;

    case MSVCS_DATA_EV_FIRST_PKT_PROC:

        // First event received only once for every session

        jb = (struct jbuf *) md_ctx->sc_pkt;
        jbuf_get_svc_set_info(jb, &ss_info);

        if (ss_info.mon_svc == 0) { // we'll ignore flows of non-sampled packets
            DLOG(LOG_NOTICE, "%s: Encountered a non-sampled packet", __func__);

            msvcs_session_ignore(md_ctx->sc_session, md_ctx->sc_pid,
                    (MSVCS_SESSION_OP_FLAG_FORWARD_DIR
                            | MSVCS_SESSION_OP_FLAG_REVERSE_DIR));

            return MSVCS_ST_PKT_FORWARD;
        }

        if (pullup_bytes(&jb, sizeof(struct ip))) {
            DLOG(LOG_ERR, "%s: Failed to pullup IP header bytes", __func__);

            msvcs_session_ignore(md_ctx->sc_session, md_ctx->sc_pid,
                    md_ctx->sc_flags);

            return MSVCS_ST_PKT_FORWARD;
        }

        // Get IP header
        ip_hdr = jbuf_to_d(jb, struct ip *);

        if (!ip_hdr || ip_hdr->ip_p != IPPROTO_UDP) { // only care about UDP/RTP

            msvcs_session_ignore(md_ctx->sc_session, md_ctx->sc_pid,
                    md_ctx->sc_flags);

            return MSVCS_ST_PKT_FORWARD;
        }

        ip_options_bytes = (ip_hdr->ip_hl * 4) - sizeof(struct ip);

        if (pullup_bytes(&jb, sizeof(struct ip) + sizeof(struct udphdr)
                + ip_options_bytes)) {
            DLOG(LOG_ERR, "%s: Failed to pullup UDP header bytes", __func__);

            msvcs_session_ignore(md_ctx->sc_session, md_ctx->sc_pid,
                    md_ctx->sc_flags);

            return MSVCS_ST_PKT_FORWARD;
        }

        mirror_addr = rate = 0;

        if (get_monitored_rule_info(md_ctx->sc_sset_id, ss_info.svc_id,
                ip_hdr->ip_dst.s_addr, &rate, &mirror_addr) || (rate == 0
                && mirror_addr == 0)) {

            // no policy match for this destination

            msvcs_session_ignore(md_ctx->sc_session, md_ctx->sc_pid,
                    md_ctx->sc_flags);

            return MSVCS_ST_PKT_FORWARD;
        }

        cpu = msvcs_state_get_cpuid();

        flow = msp_objcache_alloc(entry_handle, cpu, md_ctx->sc_sset_id);

        if (!flow) {
            DLOG(LOG_ERR, "%s: Failed to allocate flow state", __func__);

            msvcs_session_ignore(md_ctx->sc_session, md_ctx->sc_pid,
                    md_ctx->sc_flags);

            return MSVCS_ST_PKT_FORWARD;
        }

        flow->daddr = ip_hdr->ip_dst.s_addr;
        flow->dport = ((struct udphdr *)
                        ((uint32_t *) ip_hdr + ip_hdr->ip_hl))->uh_dport;

        flow->rate = rate;

        if (flow->rate != 0) {
            // init monitoring params for this flow
            DLOG(LOG_INFO, "%s: Setup monitoring for flow to %s", __func__,
                    inet_ntoa(ip_hdr->ip_dst));

            flow->ssrc = 0;
            bzero(&flow->source, sizeof(source_t));
            flow->pl_sum = 0;
            flow->vb_max = 0.0;
            flow->vb_min = 0.0;
            flow->vb_pre = 0.0;
            flow->vb_post = 0.0;
            flow->mdi_df = 0.0;
            flow->mdi_mlr = 0;

            // start window right before we received the first one
            flow->base_ts = jbuf_get_hw_timestamp(jb) - 1;
        }

        flow->maddr = mirror_addr;

        if (flow->maddr != 0) {
            
            flow->m_vrf = 0;

            // look up VRF in FDB
            if (msp_fdb_get_all_route_records(fdb_handle, PROTO_IPV4,
                    set_vrf, &flow->m_vrf) != MSP_OK) {
            
                struct in_addr tmp;
                tmp.s_addr = flow->maddr;
                DLOG(LOG_ERR, "%s: Did not successfully lookup a VRF "
                    "for mirrored site %s", __func__, inet_ntoa(tmp));
            }
            
            DLOG(LOG_INFO, "%s: Setup mirroring for flow to %s to VRF %d",
                 __func__, inet_ntoa(ip_hdr->ip_dst), flow->m_vrf);
        }

        // save the flow entry in the session context
        // we should always only see a flow one direction

        if (md_ctx->sc_flags & MSVCS_CTX_FLAGS_DIR_REVERSE) {
            msvcs_session_set_ext_handle(
                    (msvcs_session_t *) md_ctx->sc_session,
                    (uint8_t) plugin_id, NULL, flow);
        } else {
            msvcs_session_set_ext_handle(
                    (msvcs_session_t *) md_ctx->sc_session,
                    (uint8_t) plugin_id, flow, NULL);
        }

        process_packet(jb, flow, md_ctx->sc_sset_id);

        if (jb != md_ctx->sc_pkt) {
            // if a new jbuf was returned during any jbuf manipulations, then
            // we can't return forward. We hold the old jbuf (which is eaten by 
            // jbuf APIs anyway) and we inject the new jbuf
            // Note this cannot be done on "first" packets

            // Having only done a pullup this is usually not supposed to happen,
            // but just in case...

            msp_reinject_packet((msvcs_session_t *) md_ctx->sc_session, jb);

            return MSVCS_ST_PKT_HOLD;
        }

        // Drop by default since we are a monitoring application and
        // packets should be copies
        return MSVCS_ST_PKT_FORWARD;

    case MSVCS_DATA_EV_PKT_PROC:

        // received for all packets in a session following the first one

        jb = (struct jbuf *) md_ctx->sc_pkt;

        if (pullup_bytes(&jb, sizeof(struct ip))) {
            DLOG(LOG_ERR, "%s: Failed to pullup IP header bytes", __func__);
            return MSVCS_ST_PKT_FORWARD;
        }

        // safe typecast to an IP header
        ip_hdr = jbuf_to_d(jb, struct ip *);

        // retrieve the flow entry in the session context

        if (md_ctx->sc_flags & MSVCS_CTX_FLAGS_DIR_REVERSE) {
            msvcs_session_get_ext_handle(
                    (msvcs_session_t *) md_ctx->sc_session,
                    (uint8_t) plugin_id, NULL, (void **) &flow);
        } else {
            msvcs_session_get_ext_handle(
                    (msvcs_session_t *) md_ctx->sc_session,
                    (uint8_t) plugin_id, (void **) &flow, NULL);
        }

        if (!flow) {
            DLOG(LOG_ERR, "%s: Could not retrieve session context", __func__);
            
            msvcs_session_ignore(md_ctx->sc_session, md_ctx->sc_pid,
                    md_ctx->sc_flags);
            
            return MSVCS_ST_PKT_FORWARD;
        }

        process_packet(jb, flow, md_ctx->sc_sset_id);

        if (jb != md_ctx->sc_pkt) {
            // if a new jbuf was returned during any jbuf manipulations, then
            // we can't return forward. We hold the old jbuf (which is eaten by 
            // jbuf APIs anyway) and we inject the new jbuf
            // Note this cannot be done on "first" packets

            // Having only done a pullup this is usually not supposed to happen,
            // but just in case...

            msp_reinject_packet((msvcs_session_t *) md_ctx->sc_session, jb);

            return MSVCS_ST_PKT_HOLD;
        }

        return MSVCS_ST_PKT_FORWARD;

    case MSVCS_DATA_EV_SESSION_OPEN:

        // received after all plug-ins have decided to forward the first packet
        // of a session

        break;

    case MSVCS_DATA_EV_SESSION_CLOSE:

        // received when an existing session (OPEN rcv'd previously) times out

        break;

    case MSVCS_DATA_EV_SESSION_DESTROY:

        // All plug-ins have been notified about the CLOSE or some plug-in 
        // dropped the first packet of a session, so no open/close was received

        DLOG(LOG_INFO, "%s: Session destroyed", __func__);

        cpu = msvcs_state_get_cpuid();

        // Get and free attached session context containing the flow entry
        msvcs_session_get_ext_handle((msvcs_session_t *) md_ctx->sc_session,
                (uint8_t) plugin_id, (void **) &flow, NULL);

        if (flow) {
            msp_objcache_free(entry_handle, flow, cpu, md_ctx->sc_sset_id);
        }

        // Get and free attached session context containing the flow entry
        msvcs_session_get_ext_handle((msvcs_session_t *) md_ctx->sc_session,
                (uint8_t) plugin_id, NULL, (void **) &flow);

        if (flow) {
            msp_objcache_free(entry_handle, flow, cpu, md_ctx->sc_sset_id);
        }

        break;

    case MSVCS_DATA_EV_REQ_PUB_DATA:

        req = md_ctx->plugin_data;

        DLOG(LOG_INFO, "%s: Requesting public data %d", __func__, req->data_id);

        // we do not expect this event; no one would be requesting data from us
        req->err = -1; // we do not have any public data to give

        break;

    default:

        DLOG(LOG_ERR, "%s: Received an unhandled data event %X", __func__, ev);
    }

    return MSVCS_ST_OK;
}

/**
 * Control events handler
 *
 * @param[in] ctx
 *      Pointer to control context
 *
 * @param[in] ev
 *      Control event
 *
 * @return Control handler status code
 */
static int monitube_ctrl_hdlr(msvcs_control_context_t * mc_ctx,
                              msvcs_control_event_t ev)
{
    junos_kcom_gencfg_t * jkg;
    msvcs_ha_info_hdr_t * ha_info;
    kcom_ifdev_redundancy_info_t * ri;
    const char * op, *state;
    char tmp1[32], tmp2[32];

    CLOG(LOG_INFO, "%s: Handling control event for plug-in %d from CPU %d.",
            __func__, plugin_id, msvcs_state_get_cpuid());

    switch (ev) {
    case MSVCS_CONTROL_EV_INIT:

        CLOG(LOG_INFO, "%s: Initialization Event...", __func__);

        // do initialization here

        ctx = mc_ctx->scc_ev_ctxt;
        pdb_handle = mc_ctx->policy_db_handle;
        pdb_shm_handle = mc_ctx->policy_shm_handle;

        if (!msp_fdb_is_configured()) {
            CLOG(LOG_EMERG, "%s: FDB is not configured, but required.",
                    __func__);
        }

        // init the hardware timestamp infrastructure
        if (msp_hw_ts32_init() != MSP_OK) {
            CLOG(LOG_EMERG, "%s: Failed to initialize HW timestamp "
                "infrastructure.", __func__);
        }
        
        // Attach to FDB
        evInitID(&retry_timer);
        if(evSetTimer(*ctx, retry_attach_fdb, NULL, evConsTime(0, 0),
                evConsTime(5, 0), &retry_timer)) {

            CLOG(LOG_EMERG, "%s: Failed to initialize a timer to retry "
                "attaching to FDB", __func__);
        }
        
        // Init the rest when attached to FDB...

        break;

    case MSVCS_CONTROL_EV_CFG_BLOB:

        CLOG(LOG_INFO, "%s: Configuration Event", __func__);

        // we do not expect this event as their is no one sending us blobs
        jkg = (junos_kcom_gencfg_t *) mc_ctx->plugin_data;

        if (!jkg) {
            CLOG(LOG_ERR, "%s: Malformed control event", __func__);
            break;
        }

        CLOG(LOG_INFO, "%s: received unexpected GENCFG blob %d with key of "
            "size %d and blob of size %d. It was sent to %d MS PIC peers.",
                __func__, jkg->opcode, jkg->key.size, jkg->blob.size,
                jkg->peer_count);

        JUNOS_KCOM_MPSDK_CFG_FREE(jkg);

        break;

    case MSVCS_CONTROL_EV_HA_INFO_BLOB:

        CLOG(LOG_INFO, "%s: HA Info Event", __func__);

        ha_info = (msvcs_ha_info_hdr_t *) mc_ctx->plugin_data;

        if (!ha_info) {
            CLOG(LOG_ERR, "%s: Malformed control event", __func__);
            break;
        }

        if (ha_info->subtype == MSVCS_HA_INFO_REDUNDANCY_INFO) {
            CLOG(LOG_INFO, "%s: Received redundancy information of length %d",
                    __func__, ha_info->length);

            ri = (kcom_ifdev_redundancy_info_t *) MSVCS_HA_INFO_HDR_DATA(
                    ha_info);

            switch (ri->state) {
            case KCOM_IFDEV_REDUNDANCY_STATE_WAITING_FOR_PRIMARY:
                state = waiting_state;
                break;
            case KCOM_IFDEV_REDUNDANCY_STATE_PRIMARY_ACTIVE:
                state = primary_state;
                break;
            case KCOM_IFDEV_REDUNDANCY_STATE_SECONDARY_ACTIVE:
                state = secondary_state;
                break;
            case KCOM_IFDEV_REDUNDANCY_STATE_NONE_ACTIVE:
                state = none_state;
                break;
            default:
                state = tmp1;
                snprintf(tmp1, sizeof(tmp1), "%d", (int) ri->state);
                break;
            }

            switch (ri->cmd) {
            case KCOM_IFDEV_REDUNDANCY_CMD_NONE:
                op = no_op;
                break;
            case KCOM_IFDEV_REDUNDANCY_CMD_SWITCH:
                op = switch_op;
                break;
            case KCOM_IFDEV_REDUNDANCY_CMD_REVERT:
                op = revert_op;
                break;
            default:
                op = tmp2;
                snprintf(tmp2, sizeof(tmp2), "%d", (int) ri->cmd);
                break;
            }

            CLOG(LOG_INFO, "State: %s, Cmd: %s, "
                "Time since last change Sec: %ld, uSec: %ld, "
                "Primary: %s, Secondary: %s, Active: %s", state, op,
                    ri->lastupdate.tv_sec, ri->lastupdate.tv_usec,
                    ri->primary_ifdev_name, ri->secondary_ifdev_name,
                    ri->active_ifdev_name);

        } else {
            CLOG(LOG_ERR, "%s: Received unrecognizable HA info", __func__);
        }

        free(ha_info);

        break;

    default:
        CLOG(LOG_ERR, "%s: Received an unhandled ctrl event %X", __func__, ev);
    }

    return MSP_OK;
}


/*** GLOBAL/EXTERNAL Functions ***/

int monitube2_entry(void);


/**
 * The very first entry point into plug-in code called when the plug-in
 * (shared object) is loaded by mspmand. The function name must be defined in
 * in the entry tag of this plugin in the package's XML configuration file.
 *
 * @return  Valid plugin ID on success, -1 on failure
 */
int monitube2_entry(void)
{
    msvcs_plugin_params_t params;

    CLOG(LOG_INFO, "%s: Registering plug-in in entry function.", __func__);

    // Register plug-in itself

    bzero(&params, sizeof(msvcs_plugin_params_t));

    strlcpy(params.spp_name, PLUGIN_NAME, sizeof(params.spp_name));
    params.spp_plugin_app_id = PLUGIN_ID;
    params.spp_class = MSVCS_PLUGIN_CLASS_EXTERNAL;
    params.spp_data_evh = monitube_data_hdlr;
    params.spp_control_evh = monitube_ctrl_hdlr;

    plugin_id = msvcs_plugin_register(&params);

    if (plugin_id < 0) {
        CLOG(LOG_ALERT, "%s: %s cannot be registered as a valid plug-in",
                __func__, PLUGIN_NAME);
    } else {
        CLOG(LOG_INFO, "%s: %s was successfully registered and assigned id"
            "%d.", __func__, PLUGIN_NAME, plugin_id);
    }

    return plugin_id;
}


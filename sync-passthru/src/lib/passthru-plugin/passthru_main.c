/*
 * $Id: passthru_main.c 422753 2011-02-01 02:26:15Z thejesh $
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
 * @file passthru_main.c
 * @brief Contains plug-in entry point and main handlers
 *
 */

/* The Application and This Plug-in's Documentation: */

/**

\mainpage

\section overview_sec Overview

This is a sample application that is part of the sync-passthru-plugin package.

This document contains the functional specifications for the SDK Your Net 
Corporation (SYNC) Passthru project. This project consists of the 
development of a very basic plug-in that can be compared with the SYNC 
IP Reassembler. The IP reassembler does nothing with the packets other than 
reassemble fragments. The plug-in automatically gets IP reassembly done for free
by the plug-in framework; therefore, we simple pass the packet through the 
system and demonstrate some minimal plug-in responsibilities herein. Thus this
also serves as a good starting point for those learning the plug-in model and 
events.

When a packet is received a log message is written with the following format:

\verbatim
"<source IP address> -> <destination IP address>" 
\endverbatim

These messages are built when observing the first packet of each session, and 
the strings are stored in the session context so they may be used for subsequent
packets in the same session.

\section samp_setup Sample setup

In this section we show a sample configuration, and explain how to setup the 
system to get it running. 

\verbatim

chassis {
    fpc 1 {
        pic 2 {
            adaptive-services {
                service-package {
                    extension-provider {
                        control-cores 1;
                        data-cores 7;
                        object-cache-size 512;
                        policy-db-size 64;
                        package sync-passthru-plugin;
                        syslog {
                            external any;
                        }
                    }
                }
            }
        }
    }
}
interfaces {
    ##
    ## Traffic through this interface will be processed
    ##
    fe-1/1/3 {
        unit 0 {
            family inet {
                address 192.168.0.1/24;
                service {
                    input {
                        service-set ss0;
                    }
                    output {
                        service-set ss0;
                    }
                }
            }
        }
    }
    ms-1/2/0 {
        unit 0 {
            family inet;
        }
    }
}
services {
    service-set ss0 {
        interface-service {
            service-interface ms-1/2/0;
        }
        #
        # extension-service name must match plug-in's name
        #
        extension-service passthru-plugin;
    }
}


\endverbatim
<b>Figure 1 – Sample excerpts of a system configuration.</b>

*/

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <jnx/aux_types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <jnx/logging.h>
#include <jnx/msp_trace.h>
#include <jnx/junos_kcom.h>
#include <jnx/mpsdk.h>
#include <sys/jnx/jbuf.h>
#include <jnx/msp_objcache.h>
#include <jnx/msp_policy_db.h>
#include <jnx/multi-svcs/msvcs_plugin.h>
#include <jnx/multi-svcs/msvcs_state.h>
#include <jnx/multi-svcs/msvcs_events.h>
#include <jnx/multi-svcs/msvcs_session.h>
#include <jnx/junos_kcom_mpsdk_cfg.h>
#include <jnx/msp_pkt_utils.h>

/*** Constants ***/

// PROCESSING MODES:

/**
 * Full session affinity and session context is allowed. Also we get separate 
 * data events for the first packet and other packets. We also get session open,
 * close, and destroy notifications
 */
#define SESSION_BASED_MODE     1

/**
 * No sessions are created and no session context can be stored.
 * Only makes sense when CONFIG_MODE is WITH_POLICY_DATABASE
 */
#define PACKET_BASED_MODE      2

/**
 * The currently active mode to compile with of the 2 above options
 */
#define PROCESSING_MODE SESSION_BASED_MODE

/**
 * Uses the policy database
 */
#define WITH_POLICY_DATABASE   1

/**
 * Does not use the policy database
 */
#define NO_POLICY_DATABASE     2

/**
 * The currently active mode to compile with of the 2 above options
 */
#define CONFIG_MODE NO_POLICY_DATABASE

/**
 * The plug-in's name
 */
#define PASSTHRU_PLUGIN_NAME "passthru-plugin"

/**
 * The per-provider plug-in id
 */
#define PASSTHRU_PLUGIN_ID 0

/**
 * Length of string for our "<src> -> <dst>" incl. NULL-byte suffix
 */
#define SESSION_DESC_LEN (INET_ADDRSTRLEN + 4 + INET_ADDRSTRLEN + 1)

/**
 * Ctrl-side logging
 */
#define CLOG(_level, _fmt...)   \
    logging((_level), "PASSTHRU PLUG-IN: " _fmt)

/**
 * (More efficient) Data-side logging
 */
#define DLOG(_level, _fmt...)   \
    msp_log((_level), "PASSTHRU PLUG-IN: " _fmt)

// protocol strings
const char * tcp_str = "TCP";     ///< TCP protocol string
const char * udp_str = "UDP";     ///< UDP protocol string
const char * icmp_str = "ICMP";   ///< ICMP protocol string

// PIC redundancy-state strings for KCOM_IFDEV_REDUNDANCY_STATE_* values
const char * waiting_state = "waiting for primary"; ///< WAITING_FOR_PRIMARY
const char * primary_state = "primary active";      ///< PRIMARY_ACTIVE
const char * secondary_state = "secondary active";  ///< SECONDARY_ACTIVE
const char * none_state = "none active";            ///< NONE_ACTIVE

// PIC redundancy operations for KCOM_IFDEV_REDUNDANCY_CMD_* values
const char * no_op = "none";        ///< NONE
const char * switch_op = "switch";  ///< SWITCH
const char * revert_op = "revert";  ///< REVERT


/*** Data Structures ***/


/**
 * The id assigned by mspmand unique over all plugins
 */
static int plugin_id;

/**
 * The main evContext on the ctrl side
 */
static evContext * ctx;

#if CONFIG_MODE == WITH_POLICY_DATABASE
/**
 * The policy database handle
 */
static msp_policy_db_handle_t pdb_handle;

#endif

/**
 * Handle to alloc/free the session description OC 
 */
static msp_oc_handle_t soc_handle;


/*** STATIC/INTERNAL Functions ***/


/**
 * Ensure or pullup enough data into the first jbuf of the chain in order to
 * analyze it better where the bytes are contiguous
 *
 * @param[in] pkt_buf
 *      The packet in jbuf format (chain of jbufs)
 *
 * @param[in] num_bytes
 *      The number of contiguous bytes of data required in the first jbuf
 *
 * @return
 *      Returns the result of the jbuf_pullup on the pkt_buf upon SUCCESS;
 *      otherwise pkt_buf remains unchanged and EFAIL is returned
 */
static status_t
pullup_bytes(struct jbuf ** pkt_buf, uint16_t num_bytes)
{
    struct jbuf * tmp_buf;

    if(jbuf_particle_get_data_length(*pkt_buf) < num_bytes) {
        tmp_buf = jbuf_pullup((*pkt_buf), num_bytes);

        if(!tmp_buf) { // check in case it failed
            DLOG(LOG_ERR, "%s: jbuf_pullup() of %d failed on jbuf of length %d",
                    __func__, num_bytes, jbuf_total_len(*pkt_buf));
            return EFAIL;
        }

        *pkt_buf = tmp_buf;
    }
    return SUCCESS;
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
static int
passthru_data_hdlr(msvcs_data_context_t * md_ctx,
                   msvcs_data_event_t ev)
{
    msvcs_pub_data_req_t * req;
    msp_objcache_params_t ocp;
    struct jbuf * jb;
    struct ip * ip_hdr;
    const char * protocol;
    char tmp1[INET_ADDRSTRLEN];
    char tmp2[INET_ADDRSTRLEN];
    char * forward, * reverse, * desc;
    int rc, cpu = msvcs_state_get_cpuid();

    DLOG(LOG_INFO, "%s: Handling data event for plug-in %d from CPU %d.",
            __func__, plugin_id, cpu);

    switch (ev) {
    case MSVCS_DATA_EV_SM_INIT:
        
        // Should only get this once, and first

        DLOG(LOG_INFO, "%s: Initialization of shared memory event", __func__);

        bzero(&ocp, sizeof(ocp));
        ocp.oc_shm = md_ctx->sc_shm; // use the global SHM arena handle
        strlcpy(ocp.oc_name, PASSTHRU_PLUGIN_NAME "OC", sizeof(ocp.oc_name));
        ocp.oc_size = SESSION_DESC_LEN;
        
        rc = msp_objcache_create(&ocp);

        if(rc) {
            DLOG(LOG_ERR, "%s: Failed to initialize an object cache", __func__);
            return rc;
        }

        // Save the OC handle
        soc_handle = ocp.oc;

        break;

    case MSVCS_DATA_EV_INIT:
        
        // Should only get this once, and second

        DLOG(LOG_INFO, "%s: General initialization event", __func__);

        break;

    case MSVCS_DATA_EV_FIRST_PKT_PROC:
        
#if PROCESSING_MODE == SESSION_BASED_MODE
        // First event received only once for every session
        if (md_ctx->sc_session) {
            DLOG(LOG_INFO, "%s: Received the first packet of a session",
                    __func__);
        } else {
            DLOG(LOG_EMERG, "%s: Received a packet without a session in "
                    "session-based mode", __func__);
            return MSVCS_ST_PKT_DISCARD;
        }
#elif PROCESSING_MODE == PACKET_BASED_MODE
        if (md_ctx->sc_session) {
            DLOG(LOG_EMERG, "%s: Received the first packet of a session in "
                    "packet-based mode", __func__);
            return MSVCS_ST_PKT_DISCARD;
        } else {
            // plug-in was setup to be packet based, not session based
            DLOG(LOG_INFO, "%s: Received a packet in packet-based mode",
                    __func__);
            return MSVCS_ST_PKT_FORWARD; // don't do string description stuff
        }
#endif

        jb = (struct jbuf *)md_ctx->sc_pkt;

        if(pullup_bytes(&jb, sizeof(struct ip))) {
            DLOG(LOG_ERR, "%s: Failed to pullup IP header bytes, dropping "
                    "packet", __func__);
            return MSVCS_ST_PKT_DISCARD;
        }
        
        // safe typecast to an IP header
        ip_hdr = jbuf_to_d(jb, struct ip *);
        
        // store description strings as the session metadata 

        inet_ntop(AF_INET, &ip_hdr->ip_src, tmp1, sizeof(tmp1));
        inet_ntop(AF_INET, &ip_hdr->ip_dst, tmp2, sizeof(tmp2));
        
        forward = msp_objcache_alloc(soc_handle, cpu, md_ctx->sc_sset_id);
        reverse = msp_objcache_alloc(soc_handle, cpu, md_ctx->sc_sset_id);

        if(md_ctx->sc_flags & MSVCS_CTX_FLAGS_DIR_REVERSE) {
            sprintf(reverse, "%s -> %s", tmp1, tmp2); // for the current packet
            sprintf(forward, "%s -> %s", tmp2, tmp1);
        } else {
            sprintf(forward, "%s -> %s", tmp1, tmp2); // for the current packet
            sprintf(reverse, "%s -> %s", tmp2, tmp1);
        }

        // save a simple textual description of the flow
        msvcs_session_set_ext_handle((msvcs_session_t *)md_ctx->sc_session,
                (uint8_t)plugin_id, forward, reverse);

        // fall through (no break or return)

    case MSVCS_DATA_EV_PKT_PROC:
        
#if PROCESSING_MODE == PACKET_BASED_MODE
        
        // All packets should arrive as MSVCS_DATA_EV_FIRST_PKT_PROC
        
        DLOG(LOG_NOTICE, "%s: Received packet in packet-based mode, but event "
                "type is unrecognized for this mode (MSVCS_DATA_EV_PKT_PROC)",
                __func__);            

        return MSVCS_ST_PKT_FORWARD; // don't do string description stuff
#endif
        
        if (!md_ctx->sc_session) {
            DLOG(LOG_EMERG, "%s: Received packet without a session in "
                    "session-based mode", __func__);
            return MSVCS_ST_PKT_DISCARD;
        }
        
        // received for all packets in a session following the first one

        jb = (struct jbuf *)md_ctx->sc_pkt;

        if(pullup_bytes(&jb, sizeof(struct ip))) {
            DLOG(LOG_ERR, "%s: Failed to pullup IP header bytes, dropping "
                    "packet", __func__);
            return MSVCS_ST_PKT_DISCARD;
        }

        // safe typecast to an IP header
        ip_hdr = jbuf_to_d(jb, struct ip *);

        // find a string matching the protocol
        switch(ip_hdr->ip_p) {
        case IPPROTO_TCP:
            protocol = tcp_str;
            break;
        case IPPROTO_UDP:
            protocol = udp_str;
            break;
        case IPPROTO_ICMP:
            protocol = icmp_str;
            break;
        default:
            snprintf(tmp1, sizeof(tmp1), "%d", (int)ip_hdr->ip_p);
            protocol = tmp1;
            break;
        }

        // find the string stored in the session context describing what we want
        if(md_ctx->sc_flags & MSVCS_CTX_FLAGS_DIR_FORWARD) {
            msvcs_session_get_ext_handle((msvcs_session_t *)md_ctx->sc_session,
                    (uint8_t)plugin_id, (void **)&desc, NULL);
        } else if(md_ctx->sc_flags & MSVCS_CTX_FLAGS_DIR_REVERSE) {
            msvcs_session_get_ext_handle((msvcs_session_t *)md_ctx->sc_session,
                    (uint8_t)plugin_id, NULL, (void **)&desc);
        } else {
            // should never happen
            DLOG(LOG_ALERT, "%s: Direction of packet is not defined.");
        }

        if(!desc) {
            DLOG(LOG_ERR, "%s: Could not retrieve session context", __func__);
        } else {
            DLOG(LOG_INFO, "%s: %s packet of SS: %d, Direction: %s, %s",
                __func__, protocol, md_ctx->sc_sset_id,
                ((md_ctx->sc_flags & MSVCS_CTX_FLAGS_DIR_FORWARD) ?
                        "forward" : "reverse"),
                desc);
        }
        
        if(jb != md_ctx->sc_pkt) {
            // if a new jbuf was returned during any jbuf manipulations, then
            // we can't return forward. We hold the old jbuf (which is eaten by 
            // jbuf APIs anyway) and we inject the new jbuf
            // Note this cannot be done on "first" packets
            
            // Having only done a pullup this is usually not supposed to happen,
            // but just in case...
            
            msp_reinject_packet((msvcs_session_t *)md_ctx->sc_session, jb);
            
            return MSVCS_ST_PKT_HOLD;
        }

        return MSVCS_ST_PKT_FORWARD;

    case MSVCS_DATA_EV_SESSION_OPEN:

        // received after all plug-ins have decided to forward the first packet
        // of a session
        
        // print out forward-based flow info only

        msvcs_session_get_ext_handle((msvcs_session_t *)md_ctx->sc_session,
                (uint8_t)plugin_id, (void **)&desc, NULL);

        if(!desc) {
            DLOG(LOG_ERR, "%s: Could not retrieve session context", __func__);
        } else {
            DLOG(LOG_INFO, "%s: Session opened for %s", __func__, desc);
        }

        break;

    case MSVCS_DATA_EV_SESSION_CLOSE:
        
        // received when an existing session (OPEN rcv'd previously) times out

        DLOG(LOG_INFO, "%s: Session closed", __func__);

        // print out forward-based flow info only

        msvcs_session_get_ext_handle((msvcs_session_t *)md_ctx->sc_session,
                (uint8_t)plugin_id, (void **)&desc, NULL);

        if(!desc) {
            DLOG(LOG_ERR, "%s: Could not retrieve session context", __func__);
        } else {
            DLOG(LOG_INFO, "%s: Session closed for %s", __func__, desc);
        }

        break;

    case MSVCS_DATA_EV_SESSION_DESTROY:
        
        // All plug-ins have been notified about the CLOSE or some plug-in 
        // dropped the first packet of a session, so no open/close was received

        DLOG(LOG_INFO, "%s: Session destroyed", __func__);

        // Get and free attached session context containing the textual desc
        msvcs_session_get_ext_handle((msvcs_session_t *)md_ctx->sc_session,
                (uint8_t)plugin_id, (void **)&forward, (void **)&reverse);

        if(forward) {
            msp_objcache_free(soc_handle, forward, cpu, md_ctx->sc_sset_id);
        } else {
            DLOG(LOG_ERR, "%s: Could not retrieve forward session context",
                    __func__);
        }

        if(reverse) {
            msp_objcache_free(soc_handle, reverse, cpu, md_ctx->sc_sset_id);
        } else {
            DLOG(LOG_ERR, "%s: Could not retrieve reverse session context",
                    __func__);
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
static int
passthru_ctrl_hdlr(msvcs_control_context_t * mc_ctx,
                   msvcs_control_event_t ev)
{
#if CONFIG_MODE == WITH_POLICY_DATABASE
    msp_policy_db_params_t policy_db_params;
#endif
    
    junos_kcom_gencfg_t * jkg;
    msvcs_ha_info_hdr_t * ha_info;
    kcom_ifdev_redundancy_info_t * ri;
    const char * op, * state;
    char tmp1[32], tmp2[32];

    CLOG(LOG_INFO, "%s: Handling control event for plug-in %d from CPU %d.",
            __func__, plugin_id, msvcs_state_get_cpuid());

    switch (ev) {
    case MSVCS_CONTROL_EV_INIT:

        CLOG(LOG_INFO, "%s: Initialization Event...", __func__);

        // do initialization here

        ctx = mc_ctx->scc_ev_ctxt;
        
#if CONFIG_MODE == WITH_POLICY_DATABASE
        
        pdb_handle = mc_ctx->policy_db_handle;

        // Put some dummy data into the policy database
        bzero(&policy_db_params, sizeof(msp_policy_db_params_t));
        policy_db_params.handle = pdb_handle;
        policy_db_params.svc_set_id = 1;
        policy_db_params.svc_id = MSP_SVC_ID_SERVICES;
        policy_db_params.plugin_id = plugin_id;
        policy_db_params.policy_op = MSP_POLICY_DB_POLICY_ADD;
        strlcpy(policy_db_params.plugin_name, PASSTHRU_PLUGIN_NAME,
                sizeof(policy_db_params.plugin_name));

        // Add params
        policy_db_params.op.add_params.gen_num = 0;

#if PROCESSING_MODE == PACKET_BASED_MODE
        // Enable this to make the plugin a packet-based processing plugin
        // where no session is created for packets of this service set
        policy_db_params.op.add_params.policy_flags = 
            MSP_POLICY_FLAGS_SSET_PACKET;
#endif

        policy_db_params.op.add_params.policy =
            msp_shm_alloc(mc_ctx->policy_shm_handle,
                strlen(PASSTHRU_PLUGIN_NAME) + 1);
        // put the plug-in's name into PDB
        if(!policy_db_params.op.add_params.policy) {
            CLOG(LOG_ERR, "%s: Cannot allocate into policy SHM...", __func__);
            break;
        }
        strcpy(policy_db_params.op.add_params.policy, PASSTHRU_PLUGIN_NAME);

        if (msp_policy_db_op(&policy_db_params) != MSP_OK) {
            CLOG(LOG_ERR, "%s: Policy DB Add failed!", __func__);
        }
        
#endif

        break;

    case MSVCS_CONTROL_EV_CFG_BLOB:

        CLOG(LOG_INFO, "%s: Configuration Event", __func__);

        // we do not expect this event as their is no RE-SDK (mgmt) component
        // normally we populate the policy database with application data
        // inserted into the blob from the mgmt component
        jkg = (junos_kcom_gencfg_t *)mc_ctx->plugin_data;

        if(!jkg) {
            CLOG(LOG_ERR, "%s: Malformed control event", __func__);
            break;
        }

        CLOG(LOG_INFO, "%s: received the GENCFG operation %d with key of "
                "size %d and blob of size %d. It was sent to %d MS PIC peers.",
                __func__, jkg->opcode, jkg->key.size, jkg->blob.size,
                jkg->peer_count);

        JUNOS_KCOM_MPSDK_CFG_FREE(jkg);

        break;

    case MSVCS_CONTROL_EV_HA_INFO_BLOB:

        CLOG(LOG_INFO, "%s: HA Info Event", __func__);

        ha_info = (msvcs_ha_info_hdr_t *)mc_ctx->plugin_data;

        if(!ha_info) {
            CLOG(LOG_ERR, "%s: Malformed control event", __func__);
            break;
        }

        if(ha_info->subtype == MSVCS_HA_INFO_REDUNDANCY_INFO) {
            CLOG(LOG_INFO, "%s: Received redundancy information of length %d",
                    __func__, ha_info->length);

            ri= (kcom_ifdev_redundancy_info_t *)MSVCS_HA_INFO_HDR_DATA(ha_info);

            switch(ri->state) {
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
                snprintf(tmp1, sizeof(tmp1), "%d", (int)ri->state);
                break;
            }

            switch(ri->cmd) {
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
                snprintf(tmp2, sizeof(tmp2), "%d", (int)ri->cmd);
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

int passthru_entry(void);

/**
 * The very first entry point into plug-in code called when the plug-in
 * (shared object) is loaded by mspmand. The function name must be defined in
 * in the entry tag of this plugin in the package's XML configuration file.
 *
 * @return  Valid plugin ID on success, -1 on failure
 */
int
passthru_entry(void)
{
    msvcs_plugin_params_t params;

    CLOG(LOG_INFO, "%s: Registering plug-in in entry function from CPU %d.",
            __func__, msvcs_state_get_cpuid());
    
    // Register plug-in itself

    bzero(&params, sizeof(msvcs_plugin_params_t));

    strlcpy(params.spp_name, PASSTHRU_PLUGIN_NAME, sizeof(params.spp_name));
    params.spp_plugin_app_id = PASSTHRU_PLUGIN_ID;
    params.spp_class = MSVCS_PLUGIN_CLASS_EXTERNAL;
    params.spp_data_evh = passthru_data_hdlr;
    params.spp_control_evh = passthru_ctrl_hdlr;

#if CONFIG_MODE == NO_POLICY_DATABASE
    // Enable this to make the plugin a packet-based processing plugin
    // where no session is created only for this service set

    params.spp_plugin_flags |= MSVCS_PLUGIN_FLAGS_IGNORE_POLICY;
#endif

    plugin_id = msvcs_plugin_register(&params);

    if (plugin_id < 0) {
        CLOG(LOG_ALERT, "%s: %s cannot be registered as a valid plug-in",
                __func__, PASSTHRU_PLUGIN_NAME);
    } else {
        CLOG(LOG_INFO, "%s: %s was successfully registered and assigned id"
                "%d.", __func__, PASSTHRU_PLUGIN_NAME, plugin_id);
    }

    return plugin_id;
}


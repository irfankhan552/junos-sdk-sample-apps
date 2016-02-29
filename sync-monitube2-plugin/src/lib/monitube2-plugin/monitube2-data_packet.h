/*
 * $Id: monitube2-data_packet.h 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file monitube2-data_packet.h
 * @brief Relating to processing packets in the fast path
 *
 * These functions and types will manage the packet processing in the data path
 */

#ifndef __MONITUBE_DATA_PACKET_H__
#define __MONITUBE_DATA_PACKET_H__

/*** Constants ***/


/*** Data structures ***/

/**
 * flow entry information structure
 */
typedef struct flow_entry_s {
    // flow information
    time_t                       age_ts;     ///< flow age timestamp
    in_addr_t                    daddr;      ///< dest IP address (HT KEY field)
    uint16_t                     dport;      ///< dest port (HT KEY field)

    // mirroring related:
    in_addr_t                    maddr;      ///< mirror IP address
    uint32_t                     m_vrf;      ///< mirror with outgoing VRF

    // monitoring related:
       // MDI media loss rate related:
    uint32_t                     ssrc;       ///< current RTP source
    source_t                     source;     ///< RTP state of last known packet
    int32_t                      mdi_mlr;    ///< last observed MDI MLR
       // MDI delay factor related:
    uint32_t                     rate;       ///< drain rate of RTP payload
    msp_hw_ts32_t                base_ts;    ///< timestamp (start of timeframe)
    uint32_t                     pl_sum;     ///< payload bits seen in timeframe
    double                       vb_pre;     ///< VB(pre) of last seen packet
    double                       vb_post;    ///< VB(post) of last seen packet
    double                       vb_min;     ///< min VB seen this timeframe
    double                       vb_max;     ///< max VB seen this timeframe
    double                       mdi_df;     ///< last observed MDI DF

    // for list at this hash bucket:
    TAILQ_ENTRY(flow_entry_s)    entries;    ///< next and prev list entries
} flow_entry_t;

/*** GLOBAL/EXTERNAL Functions ***/


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
status_t
pullup_bytes(struct jbuf ** pkt_buf, uint16_t num_bytes);


/**
 * Process an IP packet that is UDP, and the UDP header must be available
 * in the bytes following the IP address.
 *
 * @param[in] jb
 *      The received jbuf for this packet
 *
 * @param[in] flow
 *      The flow state
 *
 * @param[in] ssid
 *      The service set id
 */
void
process_packet(struct jbuf * jb, flow_entry_t * flow, uint16_t ssid);

#endif

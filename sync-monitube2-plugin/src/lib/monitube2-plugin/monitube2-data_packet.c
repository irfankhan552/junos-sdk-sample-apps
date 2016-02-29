/*
 * $Id: monitube2-data_packet.c 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file monitube2-data_packet.c
 * @brief Relating to processing packets in the fast path
 *
 * These functions and types will manage the packet processing in the data path
 */

#include "monitube2-data_main.h"
#include <jnx/msp_hw_ts.h>
#include <sys/jnx/jbuf.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include "monitube2-data_config.h"
#include "monitube2-data_conn.h"
#include "monitube2-data_rtp.h"
#include "monitube2-data_packet.h"

/*** Constants ***/

/**
 * This many (188) bytes total in each MPEG TS packet (188 is standard)
 * DVb-ASI uses 204 bytes. ATSC uses 208 bytes. Adjust accordingly.
 */
#define MPEG_TS_PACKET_BYTES (188)

/**
 * This many bytes of each MPEG TS packet are purely header
 */
#define MPEG_TS_HEADER_BYTES (4)

/*** Data Structures ***/

/*** STATIC/INTERNAL Functions ***/

/**
 * This function will adjust a checksum.
 * It is taken directly from the NAT RFC 3022.
 * The algorithm below is applicable only for even offsets
 * (i.e. optr below must be at an even offset from start of header)
 * and even lengths (i.e. olen and nlen below must be even).
 *
 * @param[in,out] chksum
 *      Checksum
 *
 * @param[in] optr
 *      Pointer to old data to scan
 *
 * @param[in] olen
 *      Length of old data to scan
 *
 * @param[in] nptr
 *      Pointer to old data to scan
 *
 * @param[in] nlen
 *      Length of new data to scan
 */
static void
checksum_adjust(unsigned char * chksum,
                unsigned char * optr, 
                int olen,
                unsigned char * nptr,
                int nlen)
{
    long x, old, new_;
    x = chksum[0] * 256 + chksum[1];
    x = ~x & 0xFFFF;
    while (olen) {
        old = optr[0] * 256 + optr[1];
        optr += 2;
        x -= old & 0xffff;
        if (x <= 0) {
            x--;
            x &= 0xffff;
        }
        olen -= 2;
    }
    while (nlen) {
        new_ = nptr[0] * 256 + nptr[1];
        nptr += 2;
        x += new_ & 0xffff;
        if (x & 0x10000) {
            x++;
            x &= 0xffff;
        }
        nlen -= 2;
    }
    x = ~x & 0xFFFF;
    chksum[0] = x / 256;
    chksum[1] = x & 0xff;
}

/**
 * Calculate MDI stats for this flow
 *
 * @param[in] data
 *      The payload data
 *
 * @param[in] flow
 *      The locked flow entry
 *
 * @param[in] length
 *      The length of payload data (under UDP/L4)
 *      
 * @param[in] ssid
 *      service set id
 *      
 * @param[in] fpga_ts
 *      Timestamp of packet delivery to HW
 */
static void
update_stats_for_flow(rtp_hdr_t * rh,
                      flow_entry_t * flow,
                      uint16_t length,
                      uint16_t ssid,
                      uint64_t * fpga_ts)
{
    int pl_len;
    double rel_time, tmp;
    uint64_t ts = *fpga_ts;

    // find rcv'd time relative to base_ts but in seconds w/ 0.0001 precision
    if (flow->base_ts < ts) { // hasn't wrapped around yet

        rel_time = (double) (ts - flow->base_ts) / msp_hw_ts64_frequency();

    } else if (flow->base_ts > ts) { // wrapped around

        rel_time = (double) (ts - flow->base_ts + 1 + UQUAD_MAX)
                / msp_hw_ts64_frequency();
    } else {
        rel_time = 0.000001; // shouldn't happen (base is always before ts 
        //                   unless wrapped)
    }

    // Check if we are into the next timeframe (base + 1 sec)
    if (rel_time > 1.0) {

        // reset the timeframe start to the next second
        flow->base_ts += msp_hw_ts64_frequency();
        rel_time -= 1.0; // in next interval

        // Check if we are updating the MLR yet

        // these values are discussed in Section A.3 of RFC 1889

        uint32_t extended_max = flow->source.cycles + flow->source.max_seq;
        uint32_t expected = extended_max - flow->source.base_seq + 1;
        // lost (total) = expected - flow->source.received

        // WRT this interval, since last report
        uint32_t expected_interval = expected - flow->source.expected_prior;
        uint32_t received_interval = flow->source.received
                - flow->source.received_prior;

        flow->source.expected_prior = expected;
        flow->source.received_prior = flow->source.received;

        // lost this interval:
        flow->mdi_mlr = expected_interval - received_interval;

        // Calculate the DF, store and save
        flow->mdi_df = (flow->vb_max - flow->vb_min) / (double) flow->rate;

        /*
         * Really we don't need to save the mdi_df, but we do here anyway
         * in case we want to reference it in the future.
         */

        // Report the (previous timeframe's) MDI stats (DF and MLR)
        // no report if 0, which could happen after slave takes over as master
        if (flow->mdi_df != 0.0) {
            notify_stat_update(flow->daddr, flow->dport, flow->mdi_df,
                    flow->mdi_mlr, ssid);
        }

        flow->pl_sum = 0;
        flow->vb_max = 0.0;
        flow->vb_min = 0.0;
        flow->vb_pre = 0.0;
        flow->vb_post = 0.0;
    }

    // Update information related to the MDI DF

    if (rh->version != RTP_VERSION) {
        DLOG(LOG_WARNING, "%s: Found a UDP datagram without a valid "
            "RTP header", __func__);
        return;
    }

    // Update information related to the MDI DF (length)
    pl_len = length - (sizeof(rtp_hdr_t) + (rh->cc * 4));

    if (pl_len < 0) {
        DLOG(LOG_WARNING, "%s: Found an RTP header without any payload (or "
            "an invalid packet for monitoring)", __func__);
        return;
    }

    // Update information related to the MDI MLR
    if (flow->ssrc == rh->ssrc) {
        update_seq(&flow->source, rh->seq);
    } else {
        // don't even know if it's a valid RTP stream, so don't bother
        // with the MLR
        flow->mdi_mlr = 0;

        // init these to compare to the next packet
        flow->ssrc = rh->ssrc;
        flow->source.probation = MIN_SEQUENTIAL;
        flow->source.max_seq = rh->seq;
    }

    pl_len -= (pl_len / MPEG_TS_PACKET_BYTES) * MPEG_TS_HEADER_BYTES;

    if (pl_len < 0) {
        DLOG(LOG_WARNING, "%s: Found a UDP/RTP datagram without at least one"
            " MPEG TS packet in it (or an invalid packet for monitoring)",
                __func__);
        return;
    }

    // ... Continue updating information related to the MDI DF

    tmp = (double) flow->rate * rel_time;

    if ((double) flow->pl_sum > tmp) { // want a positive/abs value
        flow->vb_pre = (double) flow->pl_sum - tmp;
    } else {
        flow->vb_pre = tmp - (double) flow->pl_sum;
    }

    flow->vb_post = flow->vb_pre + pl_len;
    flow->pl_sum += (pl_len << 3); // need bits not bytes *8 = <<3

    if (flow->vb_max == 0 && flow->vb_min == 0) {
        // first observed packet in timeframe
        flow->vb_max = flow->vb_post;
        flow->vb_min = flow->vb_pre;
    } else {
        // update max and min
        if (flow->vb_post > flow->vb_max) {
            flow->vb_max = flow->vb_post;
        }
        if (flow->vb_pre < flow->vb_min) {
            flow->vb_min = flow->vb_pre;
        }
    }
}

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
pullup_bytes(struct jbuf ** pkt_buf, uint16_t num_bytes)
{
    struct jbuf * tmp_buf;

    if (jbuf_particle_get_data_length(*pkt_buf) < num_bytes) {
        tmp_buf = jbuf_pullup((*pkt_buf), num_bytes);

        if (!tmp_buf) { // check in case it failed
            DLOG(LOG_ERR,
                    "%s: jbuf_pullup() of %d failed on jbuf of length %d",
                    __func__, num_bytes, jbuf_total_len(*pkt_buf));
            return EFAIL;
        }

        *pkt_buf = tmp_buf;
    }
    return SUCCESS;
}

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
process_packet(struct jbuf * jb,
               flow_entry_t * flow,
               uint16_t ssid)
{
    struct jbuf * jb2;
    struct ip * ip_pkt = jbuf_to_d(jb, struct ip *);
    struct udphdr * udp_hdr = (struct udphdr *) ((uint32_t *) ip_pkt
            + ip_pkt->ip_hl);
    uint64_t fpga_ts = jbuf_get_hw_timestamp(jb);

    flow->age_ts = get_current_time();

    if (flow->rate != 0) { // is it monitored

        if (!pullup_bytes(&jb, (ip_pkt->ip_hl * 4) + sizeof(struct udphdr)
                + sizeof(rtp_hdr_t))) {

            // pulled up RTP header

            // refresh pointer into jbuf data for ip and udp header
            ip_pkt = jbuf_to_d(jb, struct ip *);
            udp_hdr = (struct udphdr *) ((uint32_t *) ip_pkt + ip_pkt->ip_hl);

            update_stats_for_flow((rtp_hdr_t *) ((uint8_t *) udp_hdr
                    + sizeof(struct udphdr)), flow, jbuf_total_len(jb)
                    - ((ip_pkt->ip_hl * 4) + sizeof(struct udphdr)), ssid,
                    &fpga_ts);
        } else {
            DLOG(LOG_NOTICE, "%s: Couldn't monitor UDP datagram because there "
                "were not enough bytes to form an RTP header", __func__);
        }
    }

    if (flow->maddr != 0) { // is it mirrored anywhere

        jb2 = jbuf_dup(jb);
        if (!jb2) {
            DLOG(LOG_ERR, "%s: Failed to dup a packet for mirroring", __func__);
            return;
        }

        ip_pkt = jbuf_to_d(jb2, struct ip *);
        udp_hdr = (struct udphdr *) ((uint32_t *) ip_pkt + ip_pkt->ip_hl);

        // adjust checksums taking new dest IP addresses into account

        checksum_adjust((unsigned char *) &ip_pkt->ip_sum,
                (unsigned char *) &ip_pkt->ip_dst, sizeof(in_addr_t),
                (unsigned char *) &flow->maddr, sizeof(in_addr_t));

        checksum_adjust((unsigned char *) &udp_hdr->uh_sum,
                (unsigned char *) &ip_pkt->ip_dst, sizeof(in_addr_t),
                (unsigned char *) &flow->maddr, sizeof(in_addr_t));

        // change destination address
        ip_pkt->ip_dst.s_addr = flow->maddr;

        jbuf_setvrf(jb2, flow->m_vrf);

        if (msp_send_packet(jb2) != MSP_OK) { // send bypasses plug-in chain
            DLOG(LOG_ERR,
                    "%s: Failed to send a duplicated packet for mirroring",
                    __func__);
            jbuf_free(jb2);
        }
    }
}

/*
 * $Id: monitube-data_packet.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-data_packet.c
 * @brief Relating to processing packets in the fast path
 *
 * These functions and types will manage the packet processing in the data path
 */

#include "monitube-data_main.h"
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/jnx/jbuf.h>
#include <jnx/ipc_types.h> // need this one indirectly for next one
#include <jnx/ipc_msp_pub.h>
#include <jnx/msp_fdb_api.h>
#include <jnx/msp_hw_ts.h>
#include "monitube-data_config.h"
#include "monitube-data_conn.h"
#include "monitube-data_ha.h"
#include "monitube-data_packet.h"
#include "monitube-data_rtp.h"


/*** Constants ***/

#define SHARED_MEM_NAME "monitube-data arena"    ///< shared mem name

#define FLOW_TABLE_NAME "monitube flow table" ///< table o.c. name

#define FLOW_ENTRY_NAME "monitube flow entry" ///< entry o.c. name

#define FLOW_AGE_CHECK_INTERVAL 15 ///< run aging routine interval in seconds

#define RETRY_FDB_ATTACH_INTERVAL 3 ///< retry every 3 seconds for fdb attach

#define MAX_MSP_SEND_RETRIES 100 ///< Max msp_data_send retries before panic

/**
 * Must have this many bytes of each packet in the jbuf to analyze it
 */
#define IP_NEEDED_BYTES (sizeof(struct ip))

/**
 * Must have this many bytes of each packet in the jbuf to analyze it for UDP
 */
#define UDP_NEEDED_BYTES (sizeof(struct ip) + sizeof(struct udphdr))

/**
 * This many (188) bytes total in each MPEG TS packet (188 is standard)
 * DVb-ASI uses 204 bytes. ATSC uses 208 bytes. Adjust accordingly.
 */
#define MPEG_TS_PACKET_BYTES (188)

/**
 * This many bytes of each MPEG TS packet are purely header
 */
#define MPEG_TS_HEADER_BYTES (4)

/**
 * The number of buckets, must be a power of 2, currently 2^20
 */
#define FLOW_BUCKET_COUNT (1024 * 1024)

/**
 * Our mask defining the width of our hash function
 */
const uint32_t HASH_MASK = FLOW_BUCKET_COUNT - 1;


/*** Data Structures ***/

/**
 * flow entry information structure
 */
typedef struct flow_entry_s {
    // flow information
    msp_spinlock_t               lock;       ///< flow lock
    time_t                       age_ts;     ///< flow age timestamp
    in_addr_t                    daddr;      ///< dest IP address (HT KEY field)
    uint16_t                     dport;      ///< dest port (HT KEY field)
    uint16_t                     frag_group; ///< frag group (ID) (HT KEY field)
    char *                       mon;        ///< monitor name

    // replication related:
    time_t                       r_trigger;  ///< replication trigger time

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


/**
 * A list/set of servers type as a tailq
 */
typedef TAILQ_HEAD(ht_bucket_list_s, flow_entry_s) ht_bucket_list_t;


/**
 * A bucket in a hashtable_t
 */
typedef struct hash_bucket_s {
    msp_spinlock_t        bucket_lock;    ///< lock for this bucket
    ht_bucket_list_t      bucket_entries; ///< list of entries in bucket
} hash_bucket_t;


/**
 * A hashtable for the flows_table
 *
 * In hashtable the destination addr is hashed for the KEY to lookup a bucket
 * within the bucket list an input search KEY must match exactly with an
 * existing entry's KEY which is its dest addr + (port or frag_group)
 * We use the destination port for whole IP packets and the frag_group for IP
 * fragments.
 *
 * The hashtable value is the whole flow entry (in which the key info is also
 * stored).
 */
typedef struct hashtable_s {
    hash_bucket_t hash_bucket[FLOW_BUCKET_COUNT]; ///<maps hashes to buckets
} hashtable_t;


static evTimerID        aging_timer;   ///< timer set to do aging and cleanup
static evTimerID        retry_timer;   ///< timer set to do fdb attach retry
static msp_shm_handle_t shm_handle;    ///< handle for shared memory allocator
static msp_oc_handle_t  table_handle;  ///< handle for OC table allocator
static msp_oc_handle_t  entry_handle;  ///< handle for OC table entry allocator
static hashtable_t *    flows_table;   ///< pointer to the hashtable of flows
static atomic_uint_t    loops_running; ///< # of data loops running
static volatile uint8_t fdb_connected; ///< T/F depending if attached to FDB
static volatile uint8_t do_shutdown;   ///< do the data loops need to shutdown
static uint32_t         obj_cache_id;  ///< ID for OC memory tracking
static msp_fdb_handle_t fdb_handle;    ///< handle for FDB (forwarding DB)

extern volatile boolean is_master; ///< mastership state of this data component


/*** STATIC/INTERNAL Functions ***/


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
retry_attach_fdb(evContext ctx,
                 void * uap __unused,
                 struct timespec due __unused,
                 struct timespec inter __unused)
{
    int rc = msp_fdb_attach(NULL, &fdb_handle);

    if(rc == MSP_EAGAIN) {
        return; // will retry again later
    } else if(rc != MSP_OK) {
        LOG(LOG_ALERT, "%s: Failed to attach to the forwarding database. Check "
                "that it is configured (Error code: %d)", __func__, rc);
        // we will keep trying, but something is probably wrong
    } else { // it succeeded
        evClearTimer(ctx, retry_timer);
        evInitID(&retry_timer);
        LOG(LOG_INFO, "%s: Attached to FDB", __func__);
        fdb_connected = 1;

        // Once FDB is initialized it is safe to init SHM & OC
        init_application();
    }
}


/**
 * Callback to periodically age out entries in our flows_table and
 * cleanup shared memory
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
aging_cleanup(evContext ctx __unused,
              void * uap __unused,
              struct timespec due __unused,
              struct timespec inter __unused)
{
    const time_t flow_duration = 180;

    uint32_t i, cpu;
    hash_bucket_t * bucket;
    flow_entry_t * flow, * next;
    time_t current_time, flow_timeout;
    delete_replication_data_t data;

    cpu = msp_get_current_cpu();
    current_time = get_current_time();
    flow_timeout = current_time - flow_duration;

    for(i = 0; i < FLOW_BUCKET_COUNT; ++i) {

        bucket = &flows_table->hash_bucket[i];

        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

        flow = TAILQ_FIRST(&bucket->bucket_entries);

        while(flow != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(flow, entries);

            // Get the flow lock
            INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

            if(flow->age_ts < flow_timeout) { // check for timeout/expiry
                // notify slave
                data.bucket = i;
                data.daddr = flow->daddr;
                data.dport = flow->dport;
                delete_replication_entry(&data);

                TAILQ_REMOVE(&bucket->bucket_entries, flow, entries);
                msp_objcache_free(entry_handle, flow, cpu, obj_cache_id);
            } else {
                // Release the flow lock
                INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);
            }
            flow = next;
        }

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    msp_objcache_reclaim(shm_handle);
}


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
checksum_adjust(
    unsigned char * chksum,
    unsigned char * optr,
    int olen,
    unsigned char * nptr,
    int nlen)
{
    long x, old, new_;
    x=chksum[0]*256+chksum[1];
    x=~x & 0xFFFF;
    while (olen)
    {
        old=optr[0]*256+optr[1]; optr+=2;
        x-=old & 0xffff;
        if (x<=0) { x--; x&=0xffff; }
        olen-=2;
    }
    while (nlen)
    {
        new_=nptr[0]*256+nptr[1]; nptr+=2;
        x+=new_ & 0xffff;
        if (x & 0x10000) { x++; x&=0xffff; }
        nlen-=2;
    }
    x=~x & 0xFFFF;
    chksum[0]=x/256; chksum[1]=x & 0xff;
}


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

        if(!tmp_buf) { // check it didn't fail
            return EFAIL;
        }

        *pkt_buf = tmp_buf;
    }
    return SUCCESS;
}


/**
 * Calculate MDI stats for this flow
 *
 * @param[in] data
 *      The payload data
 *      (if fragment==FALSE, this is where the RTP header starts)
 *      (if fragment==TRUE, no RTP header is assumed to be present)
 *
 * @param[in] fragment
 *      Is the data from an IP fragmented packet (in a secondary fragment)
 *
 * @param[in] flow
 *      The locked flow entry
 *
 * @param[in] length
 *      The length of payload data (under UDP/L4)
 */
static void
update_stats_for_flow(void * data,
                      uint8_t fragment,
                      flow_entry_t * flow,
                      uint16_t length)
{
    rtp_hdr_t * rh = NULL;
    int pl_len;
    double rel_time, tmp;
    msp_hw_ts32_t now = msp_hw_ts32_read();

    // find rcv'd time relative to base_ts but in seconds w/ 0.0001 precision
    if(flow->base_ts < now) { // hasn't wrapped around yet

        rel_time = (double)msp_hw_ts32_diff(now, flow->base_ts)
                        / msp_hw_ts32_frequency();

    } else if(flow->base_ts > now) { // wrapped around

        rel_time = (double)(msp_hw_ts32_diff(now, flow->base_ts) + 1 + UINT_MAX)
                        / msp_hw_ts32_frequency();
    } else {
        rel_time = 0.000001; // shouldn't happen
    }

    // Check if we are into the next timeframe (base + 1 sec)
    if(rel_time > 1.0) {

        // reset the timeframe start to the next second
        flow->base_ts += msp_hw_ts32_frequency();
        rel_time -= 1.0; // in next interval

        // Check if we are updating the MLR yet

        // these values are discussed in Section A.3 of RFC 1889

        uint32_t extended_max = flow->source.cycles + flow->source.max_seq;
        uint32_t expected = extended_max - flow->source.base_seq + 1;
        // lost (total) = expected - flow->source.received

        // WRT this interval, since last report
        uint32_t expected_interval = expected - flow->source.expected_prior;
        uint32_t received_interval = flow->source.received -
                                                flow->source.received_prior;

        flow->source.expected_prior = expected;
        flow->source.received_prior = flow->source.received;

        // lost this interval:
        flow->mdi_mlr = expected_interval - received_interval;

        // Calculate the DF, store and save
        flow->mdi_df = (flow->vb_max - flow->vb_min) / (double)flow->rate;

        /*
         * Really we don't need to save the mdi_df, but we do here anyway
         * in case we want to reference it in the future.
         */

        // Report the (previous timeframe's) MDI stats (DF and MLR)
        // no report if 0, which could happen after slave takes over as master
        if(flow->mdi_df != 0.0) {
            notify_stat_update(flow->daddr, flow->dport,
                    flow->mdi_df, flow->mdi_mlr, flow->mon);
        }

        flow->pl_sum = 0;
        flow->vb_max = 0.0;
        flow->vb_min = 0.0;
        flow->vb_pre = 0.0;
        flow->vb_post = 0.0;
    }

    // Update information related to the MDI DF

    if(!fragment) {
        // its not part of a secondary fragment, so it should have an RTP header
        rh = (rtp_hdr_t *)data;

        if(rh->version != RTP_VERSION) {
            LOG(LOG_WARNING, "%s: Found a UDP datagram without a valid "
                    "RTP header", __func__);
            return;
        }

        // Update information related to the MDI DF (length)
        pl_len = length - (sizeof(rtp_hdr_t) + (rh->cc * 4));

        if(pl_len < 0) {
            LOG(LOG_WARNING, "%s: Found an RTP header without any payload (or "
                    "an invalid packet for monitoring)", __func__);
            return;
        }

        // Update information related to the MDI MLR
        if(flow->ssrc == rh->ssrc) {
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

    } else {
        pl_len = length;

        if(pl_len < 0) {
            LOG(LOG_ERR, "%s: Found an IP fragment without any payload "
                    "report (or an invalid packet for monitoring)", __func__);
            return;
        }
    }

    pl_len -= (pl_len / MPEG_TS_PACKET_BYTES) * MPEG_TS_HEADER_BYTES;

    if(pl_len < 0) {
        LOG(LOG_WARNING, "%s: Found a UDP/RTP datagram without at least one"
            " MPEG TS packet in it (or an invalid packet for monitoring)",
            __func__);
        return;
    }

    // ... Continue updating information related to the MDI DF

    tmp = (double)flow->rate * rel_time;

    if((double)flow->pl_sum > tmp) { // want a positive/abs value
        flow->vb_pre = (double)flow->pl_sum - tmp;
    } else {
        flow->vb_pre = tmp - (double)flow->pl_sum;
    }

    flow->vb_post = flow->vb_pre + pl_len;
    flow->pl_sum += (pl_len << 3); // need bits not bytes *8 = <<3

    if(flow->vb_max == 0 && flow->vb_min == 0) {
        // first observed packet in timeframe
        flow->vb_max = flow->vb_post;
        flow->vb_min = flow->vb_pre;
    } else {
        // update max and min
        if(flow->vb_post > flow->vb_max) {
            flow->vb_max = flow->vb_post;
        }
        if(flow->vb_pre < flow->vb_min) {
            flow->vb_min = flow->vb_pre;
        }
    }
}


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
 * Process an IP packet that is UDP, and the UDP header must be available
 * in the bytes following the IP address.
 *
 * @param[in] pkt_buf
 *      The received jbuf for this packet
 *
 * @param[in] length
 *      The IP packet length
 *
 * @param[in] cpu
 *      The current cpu of the caller (used for object cache alloc calls)
 *
 * @param[out] mirror
 *      Should the packet be sent out (set to 1) or just monitored (unchanged)
 *
 * @param[out] mirror_vrf
 *      If mirror is set, then this param will contain the VRF
 *
 * @return SUCCESS if the packet can be sent out; EFAIL if it can be dropped
 */
static status_t
process_packet(struct jbuf * pkt_buf,
               int cpu,
               uint8_t * mirror,
               uint32_t * mirror_vrf)
{
    register uint32_t hash;
    uint16_t * key;
    hash_bucket_t * bucket;
    flow_entry_t * flow;
    struct in_addr tmp;
    uint16_t jb_length = jbuf_total_len(pkt_buf);
    struct ip * ip_pkt = jbuf_to_d(pkt_buf, struct ip *);
    struct udphdr * udp_hdr =
        (struct udphdr *)((uint32_t *)ip_pkt + ip_pkt->ip_hl);
    replication_data_t data;

    if(jb_length != ip_pkt->ip_len) {
        LOG(LOG_EMERG, "%s: Jbuf does not contain entire packet",
                __func__);
        return EFAIL;
    }

    // get hash of the just the destination address
    // hash input key is 32 bits; hash output is FLOW_BUCKET_COUNT bits
    // Could also hash the dest port but it makes dealing with fragments harder

    key = (uint16_t *)&ip_pkt->ip_dst;
    hash = 0x5F5F;
    hash = ((hash << 5) + hash) ^ key[0];
    hash = ((hash << 5) + hash) ^ key[1];

    hash &= HASH_MASK; // trim to output width

    // use hash to lookup a hash bucket and find the matching entry

    bucket = &flows_table->hash_bucket[hash]; // get bucket

    // Get the bucket lock
    INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

    flow = TAILQ_FIRST(&bucket->bucket_entries);

    while(flow != NULL) {
        if(flow->daddr == ip_pkt->ip_dst.s_addr
           && flow->dport == udp_hdr->uh_dport) {

            break; // match
        }
        flow = TAILQ_NEXT(flow, entries);
    }

    // if there's no matching flow, then create one... the slow path
    if(flow == NULL) {

        // SLOW PATH:

        flow = msp_objcache_alloc(entry_handle, cpu, obj_cache_id);
        if(flow == NULL) {
            // Release the bucket lock
            INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);

            LOG(LOG_ERR, "%s: Failed to allocate object cache for a "
                    "flow entry", __func__);
            return EFAIL;
        }

        // init and grab lock
        msp_spinlock_init(&flow->lock);
        INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

        TAILQ_INSERT_HEAD(&bucket->bucket_entries, flow, entries);

        flow->daddr = ip_pkt->ip_dst.s_addr;
        flow->dport = udp_hdr->uh_dport;

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);

        // find out if this flow will match a monitor or mirror

        flow->rate = get_monitored_rate(flow->daddr, &flow->mon);

        if(flow->rate != 0) {
            // init monitoring params for this flow
            LOG(LOG_INFO, "%s: Monitoring new flow to %s",
                    __func__, inet_ntoa(ip_pkt->ip_dst));

            flow->ssrc = 0;
            flow->base_ts = msp_hw_ts32_read();
            bzero(&flow->source, sizeof(source_t));
            flow->pl_sum = 0;
            flow->vb_max = 0.0;
            flow->vb_min = 0.0;
            flow->vb_pre = 0.0;
            flow->vb_post = 0.0;
        } else {
            LOG(LOG_INFO, "%s: NOT Monitoring new flow to %s",
                    __func__, inet_ntoa(ip_pkt->ip_dst));
        }

        flow->maddr = get_mirror(flow->daddr);

        if(flow->maddr != 0) {
            flow->m_vrf = 0;
            // look up VRF in FDB
            if(msp_fdb_get_all_route_records(fdb_handle, PROTO_IPV4,
                    set_vrf, &flow->m_vrf) != MSP_OK) {

                tmp.s_addr = flow->maddr;
                LOG(LOG_ERR, "%s: Did not successfully lookup a VRF "
                        "for mirrored site %s", __func__, inet_ntoa(tmp));
            }
        }

        flow->r_trigger = get_current_time(); // flag to replicate asap

    } else {
        // Get the flow lock
        INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    // there's a matching flow, so use it to forward the traffic
    // FAST PATH:

    flow->age_ts = get_current_time();

    // If it is the first fragment note the frag ID
    if((ntohs(ip_pkt->ip_off) & IP_MF)) {
        flow->frag_group = ip_pkt->ip_id;
    } else {
        flow->frag_group = 0;
    }

    if(flow->rate != 0) { // is it monitored

        if(!pullup_bytes(&pkt_buf, (ip_pkt->ip_hl * 4)
                + sizeof(struct udphdr) + sizeof(rtp_hdr_t))) {

            // refresh pointer into jbuf data
            ip_pkt = jbuf_to_d(pkt_buf, struct ip *);
            udp_hdr = (struct udphdr *)((uint32_t *)ip_pkt + ip_pkt->ip_hl);

            update_stats_for_flow((uint8_t *)udp_hdr + sizeof(struct udphdr),
                    FALSE, flow, jb_length -
                    ((ip_pkt->ip_hl * 4) + sizeof(struct udphdr)));
        } else {
            LOG(LOG_NOTICE, "%s: Couldn't monitor UDP datagram because there "
                    "were not enough bytes to form an RTP header", __func__);
        }
    }

    if(flow->maddr != 0) { // is it mirrored anywhere

        // adjust IP checksum taking new dest IP addresses into account
        checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
            (unsigned char *)&ip_pkt->ip_dst, sizeof(in_addr_t),
            (unsigned char *)&flow->maddr, sizeof(in_addr_t));

        // adjust UDP checksum taking new dest IP addresses into account
        checksum_adjust((unsigned char *)&udp_hdr->uh_sum,
                (unsigned char *)&ip_pkt->ip_dst, sizeof(in_addr_t),
                (unsigned char *)&flow->maddr, sizeof(in_addr_t));

        // change destination address
        ip_pkt->ip_dst.s_addr = flow->maddr;

        *mirror = 1;
        *mirror_vrf = flow->m_vrf;
    }

    if(flow->r_trigger <= get_current_time()) { // is it time to replicate ?

        // notify slave
        data.bucket = hash;
        data.daddr = flow->daddr;
        data.dport = flow->dport;
        data.age_ts = flow->age_ts;
        data.rate = flow->rate;
        data.maddr = flow->maddr;
        data.m_vrf = flow->m_vrf;
        if(flow->rate) {
            data.ssrc = flow->ssrc;
            memcpy(&data.source, &flow->source, sizeof(source_t));
            strncpy(data.mon, flow->mon, sizeof(MAX_MON_NAME_LEN));
        } else {
            data.mon[0] = '\0';
        }

        update_replication_entry(&data);
        flow->r_trigger += get_replication_interval();
    }

    // Release the flow lock
    INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);

    return SUCCESS;
}


/**
 * Process an IP packet that is UDP, but it is an IP fragment,
 * and not the first. We assume, no UDP header info is available, and a
 * flow must exist for it.
 *
 * @param[in] ip_pkt
 *      The IP packet
 *
 * @param[in] jb_length
 *      The length of the received packet
 *
 * @param[out] mirror
 *      Should the packet be sent out (set to 1) or just monitored (unchanged)
 *
 * @param[out] mirror_vrf
 *      If mirror is set, then this param will contain the VRF
 *
 * @return SUCCESS if the packet can be sent out; EFAIL if it can be dropped
 */
static status_t
process_fragment(struct ip * ip_pkt,
                 uint16_t jb_length,
                 uint8_t * mirror,
                 uint32_t * mirror_vrf)
{
    register uint32_t hash;
    uint16_t * key;
    hash_bucket_t * bucket;
    flow_entry_t * flow;

    if(jb_length != ip_pkt->ip_len) {
        LOG(LOG_WARNING, "%s: Jbuf does not contain entire packet",
                __func__);
    }

    // get hash of the just the destination address
    // hash input key is 32 bits; hash output is FLOW_BUCKET_COUNT bits
    // Could also hash the dest port but it makes dealing with fragments harder

    key = (uint16_t *)&ip_pkt->ip_dst;
    hash = 0x5F5F;
    hash = ((hash << 5) + hash) ^ key[0];
    hash = ((hash << 5) + hash) ^ key[1];

    hash &= HASH_MASK; // trim to output width

    // use hash to lookup a hash bucket and find the matching entry

    bucket = &flows_table->hash_bucket[hash]; // get bucket

    // Get the bucket lock
    INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

    flow = TAILQ_FIRST(&bucket->bucket_entries);

    while(flow != NULL) {
        if(flow->daddr == ip_pkt->ip_dst.s_addr
           && flow->frag_group == ip_pkt->ip_id) {

            break; // match
        }
        flow = TAILQ_NEXT(flow, entries);
    }

    // if there's no matching flow, we haven't seen the first fragment yet
    if(flow == NULL) {
        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);

        LOG(LOG_WARNING, "%s: Received a packet from %s. It is an IP "
                "fragment, but we have not yet received the first "
                "fragment, so we cannot tell if it belongs to an "
                "monitube application",
            __func__, inet_ntoa(ip_pkt->ip_src));

        return SUCCESS; // don't know anything about this flow
    } else {
        // Get the flow lock
        INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    // else there's a matching flow, so use it to forward the traffic
    // FAST PATH:
    flow->age_ts = get_current_time();

    if(flow->rate != 0) { // is it monitored
        update_stats_for_flow((uint32_t *)ip_pkt + ip_pkt->ip_hl, TRUE,
                flow, jb_length - ((ip_pkt->ip_hl * 4)));
    }

    if(flow->maddr != 0) { // is it mirrored anywhere
        // adjust IP checksum taking new dest IP addresses into account
        checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
            (unsigned char *)&ip_pkt->ip_dst, sizeof(in_addr_t),
            (unsigned char *)&flow->maddr, sizeof(in_addr_t));

        // change destination address
        ip_pkt->ip_dst.s_addr = flow->maddr;

        *mirror = 1;
        *mirror_vrf = flow->m_vrf;
    }

    // Release the flow lock
    INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);

    return SUCCESS;
}


/**
 * Send the packet or other message
 *
 * @param[in] pkt_buf
 *      The packet in jbuf format as we received it
 *
 * @param[in] handle
 *      The handle for the data loop
 *
 * @return
 *      Returns the result of the msp_data_send on the packet
 *      (MSP_OK upon SUCCESS)
 */
static int
send_packet(struct jbuf * pkt_buf,
            const msp_data_handle_t const * handle)
{
    // enqueue it back into the FIFO to go out

    int rc = MSP_DATA_SEND_RETRY;
    int retries = 0;

    while(rc == MSP_DATA_SEND_RETRY && ++retries <= MAX_MSP_SEND_RETRIES) {
        rc = msp_data_send(*handle, pkt_buf, MSP_MSG_TYPE_PACKET);
    }

    if(rc == MSP_DATA_SEND_FAIL) {

        LOG(LOG_ERR, "%s: Failed to forward packet using msp_data_send().",
            __func__);

    } else if(rc == MSP_DATA_SEND_RETRY) { // Panic / calls exit(1)

        LOG(LOG_EMERG, "%s: PANIC: Failed to send a jbuf after %d retries "
            "with msp_data_send().", __func__, MAX_MSP_SEND_RETRIES);

    } else if(rc != MSP_OK) {

        LOG(LOG_ERR, "%s: Failed to forward packet and got unknown return "
            "code from msp_data_send().", __func__);
    }
    return rc;
}


/**
 * Entry point for packet processing threads
 * (function passed to msp_data_create_loop_on_cpu)
 *
 * @param[in] params
 *     dataloop parameters with user data, loop identifier, and loop number
 */
static void *
monitube_process_packet(msp_dataloop_args_t * params)
{
    struct jbuf * pkt_buf;
    jbuf_svc_set_info_t ss_info;
    struct ip * ip_pkt;
    int type, cpu;
    uint16_t ip_frag_offset;
    uint8_t ip_options_bytes = 0, mirror = 0;
    uint32_t mirror_vrf = 0;
    sigset_t sig_mask;

    // Block SIGTERM to this thread/main thread will handle otherwise we inherit
    // this behaviour in our threads sigmask and the signal might come here
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sig_mask, NULL);

    atomic_add_uint(1, &loops_running);

    cpu = msp_data_get_cpu_num(params->dhandle);
    INSIST_ERR(cpu != MSP_NEXT_NONE);
    
    LOG(LOG_INFO, "%s: STARTED dLoop on CPU %d", __func__, cpu);

    // Start the packet loop...
    while(!do_shutdown) {

        // Dequeue a packet from the rx-fifo
        pkt_buf = msp_data_recv(params->dhandle, &type);

        if(pkt_buf == NULL) { // Didn't get anything
            continue;
        }

        if(!is_master) {
            jbuf_free(pkt_buf);
            continue;
        }

        if(type != MSP_MSG_TYPE_PACKET) { // Didn't get network traffic
            LOG(LOG_WARNING, "%s: Message wasn't a packet...dropping",
                __func__);
            jbuf_free(pkt_buf);
            continue;
        }

        jbuf_get_svc_set_info(pkt_buf, &ss_info);
        if(ss_info.mon_svc == 0) { // we'll drop non-sampled packets
            LOG(LOG_NOTICE,"%s: Monitube-data encountered a non-sampled packet",
                __func__);
            jbuf_free(pkt_buf);
            continue;
        }

        if(pullup_bytes(&pkt_buf, IP_NEEDED_BYTES)) {

            LOG(LOG_ERR, "%s: Dropped a packet because there's not enough "
                "bytes to form an IP header.", __func__);

            jbuf_free(pkt_buf);
            continue;
        }

        // Get IP header
        ip_pkt = jbuf_to_d(pkt_buf, struct ip *);

        if(!ip_pkt || ip_pkt->ip_p != IPPROTO_UDP) { // only care about UDP/RTP
            jbuf_free(pkt_buf);
            continue;
        }

        ip_frag_offset = ntohs(ip_pkt->ip_off);
        ip_options_bytes = (ip_pkt->ip_hl * 4) - sizeof(struct ip);
        mirror = 0;

        if((ip_frag_offset & IP_OFFMASK)) {

            // It's a fragment, but not the first fragment
            process_fragment(ip_pkt, pkt_buf->jb_total_len,
                    &mirror, &mirror_vrf);

        } else if(!pullup_bytes(&pkt_buf, UDP_NEEDED_BYTES+ ip_options_bytes)) {

            // It is UDP, and could be the first fragment or normal
            process_packet(pkt_buf, cpu, &mirror, &mirror_vrf);

        } else {
            LOG(LOG_NOTICE, "%s: Did not process a packet to %s. There's not "
                "enough bytes to form the UDP header (its not an IP fragment).",
                __func__, inet_ntoa(ip_pkt->ip_dst));
        }

        if(mirror) {
            jbuf_setvrf(pkt_buf, mirror_vrf);
            if(send_packet(pkt_buf, &params->dhandle) != MSP_OK) {
                jbuf_free(pkt_buf);
            }
        } else {
            // Drop by default since we are a monitoring application and
            // packets should be copies
            jbuf_free(pkt_buf);
        }
    }

    atomic_sub_uint(1, &loops_running);

    // thread is done if it reaches this point
    pthread_exit(NULL);
    return NULL;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Start packet loops, one per available data core
 *
 * @param[in] ctx
 *     event context from master thread used for cleanup timer
 */
status_t
init_packet_loops(evContext ctx)
{
    int i, rc, core;
    msp_dataloop_params_t params;
    msp_dataloop_result_t result;
    msp_shm_params_t shmp;
    msp_objcache_params_t ocp;

    shm_handle = table_handle = entry_handle = flows_table = NULL;
    evInitID(&aging_timer);
    obj_cache_id = 0; // for now this can always be zero

    LOG(LOG_INFO, "%s: Initializing object cache for data loops", __func__);

    bzero(&shmp, sizeof(shmp));
    bzero(&ocp, sizeof(ocp));

    // allocate & initialize the shared memory

    strncpy(shmp.shm_name, SHARED_MEM_NAME, SHM_NAME_LEN);

    if(msp_shm_allocator_init(&shmp) != MSP_OK) {
        LOG(LOG_ERR, "%s: Shared memory allocator initialization failed",
                __func__);
        return EFAIL;
    }

    shm_handle = shmp.shm; // get handle

    // create object cache allocator for the flow look up table
    ocp.oc_shm = shm_handle;
    ocp.oc_size  = sizeof(hashtable_t);
    strncpy(ocp.oc_name, FLOW_TABLE_NAME, OC_NAME_LEN);

    if(msp_objcache_create(&ocp) != MSP_OK) {
        LOG(LOG_ERR, "%s: Object-cache allocator initialization failed (table)",
                __func__);
        return EFAIL;
    }

    table_handle = ocp.oc; // get handle

    // create object cache allocator for the flow look up table entries
    ocp.oc_shm = shmp.shm;
    ocp.oc_size  = sizeof(flow_entry_t);
    strncpy(ocp.oc_name, FLOW_ENTRY_NAME, OC_NAME_LEN);

    if (msp_objcache_create(&ocp) != MSP_OK) {
        LOG(LOG_ERR, "%s: Object-cache allocator initialization failed (entry)",
                __func__);
        return EFAIL;
    }

    entry_handle = ocp.oc; // get handle

    // allocate flows_table in OC:

    flows_table = msp_objcache_alloc(table_handle,
            msp_get_current_cpu(), obj_cache_id);
    if(flows_table == NULL) {
        LOG(LOG_ERR, "%s: Failed to allocate object cache for flows table ",
                __func__);
        return EFAIL;
    }

    for(i = 0; i < FLOW_BUCKET_COUNT; ++i) {
        msp_spinlock_init(&flows_table->hash_bucket[i].bucket_lock);
        TAILQ_INIT(&flows_table->hash_bucket[i].bucket_entries);
    }

    // init the hardware timestamp infrastructure
    rc = msp_hw_ts32_init();
    if(rc != MSP_OK) {
        LOG(LOG_EMERG, "%s: Failed to initialize HW timestamp infrastructure."
                "(Error code: %d)", __func__, rc);
        return EFAIL;
    }

    // start ager

    if(evSetTimer(ctx, aging_cleanup, NULL,
            evAddTime(evNowTime(), evConsTime(FLOW_AGE_CHECK_INTERVAL, 0)),
            evConsTime(FLOW_AGE_CHECK_INTERVAL, 0),
            &aging_timer)) {

        LOG(LOG_EMERG, "%s: Failed to initialize a timer to periodically "
            "check age of flow entries (Error: %m)", __func__);
        return EFAIL;
    }

    LOG(LOG_INFO, "%s: Starting packet loops", __func__);

    bzero(&params, sizeof(msp_dataloop_params_t));
    bzero(&result, sizeof(msp_dataloop_result_t));

    loops_running = 0;
    do_shutdown = 0;

    // go through the available data cores
    core = msp_env_get_next_data_core(MSP_NEXT_NONE);
    while(core != MSP_NEXT_END) {

        // for each data core, create only one data cpu
        rc = msp_env_get_next_data_cpu_in_core(core, MSP_NEXT_NONE);

        if(rc != MSP_NEXT_END) {
            LOG(LOG_INFO, "%s: Creating a data loop on CPU %d (in core %d)",
                    __func__, rc, core);

            rc = msp_data_create_loop_on_cpu(rc, monitube_process_packet,
                    &params, &result);

            if(rc != MSP_OK) {
                LOG(LOG_ERR, "%s: Failed to create a data loop (Error: %d)",
                        __func__, rc);
                return EFAIL;
            }

        } else {
            LOG(LOG_ERR, "%s: Found no available data CPUs in data core %d",
                __func__, rc);
            return EFAIL;
        }

        core = msp_env_get_next_data_core(core);
    }
    return SUCCESS;
}


/**
 * Initialize the forwarding database (start trying to attach to it)
 *
 * @param[in] ctx
 *     event context from master thread used for FDB attach retry timer
 */
void
init_forwarding_database(evContext ctx)
{
    evInitID(&retry_timer);

    LOG(LOG_INFO, "%s: Attaching to the forwarding database", __func__);

    // need to try again later because FDB isn't initialized yet
    if(evSetTimer(ctx, retry_attach_fdb, NULL, evConsTime(0, 0),
            evConsTime(RETRY_FDB_ATTACH_INTERVAL, 0), &retry_timer)) {

        LOG(LOG_EMERG, "%s: Failed to initialize a timer to retry "
            "attaching to FDB (Error: %m)", __func__);
    }
}


/**
 * Cleanup data loops for shutdown
 *
 * @param[in] ctx
 *     event context from master thread used for cleanup timer
 */
void
stop_packet_loops(evContext ctx)
{
    do_shutdown = 1;

    if(evTestID(aging_timer)) {
        evClearTimer(ctx, aging_timer);
        evInitID(&aging_timer);
    }

    if(evTestID(retry_timer)) {
        evClearTimer(ctx, retry_timer);
        evInitID(&retry_timer);
    }

    while(loops_running > 0) ; // note the spinning while waiting

    if(fdb_connected) {
        msp_fdb_detach(fdb_handle);
    }

    fdb_connected = 0;
}


/**
 * Cleanup shared memory and object caches
 */
void
destroy_packet_loops_oc(void)
{
    // loops must be all shutdown

    if(flows_table) {
        msp_objcache_free(
                table_handle, flows_table, msp_get_current_cpu(), obj_cache_id);
        flows_table = NULL;
    }

    if(table_handle) {
        msp_objcache_destroy(table_handle);
        table_handle = NULL;
    }

    if(entry_handle) {
        msp_objcache_destroy(entry_handle);
        entry_handle = NULL;
    }
}


/**
 * Find all flow entries using the a monitor, and remove them
 */
void
clean_flows_with_any_monitor(void)
{
    uint32_t i, cpu;
    hash_bucket_t * bucket;
    flow_entry_t * flow, * next;
    delete_replication_data_t data;

    cpu = msp_get_current_cpu();

    for(i = 0; i < FLOW_BUCKET_COUNT; ++i) {

        bucket = &flows_table->hash_bucket[i];

        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

        flow = TAILQ_FIRST(&bucket->bucket_entries);

        while(flow != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(flow, entries);

            // Get the flow lock
            INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

            if(flow->mon != NULL) {
                // notify slave
                data.bucket = i;
                data.daddr = flow->daddr;
                data.dport = flow->dport;
                delete_replication_entry(&data);

                TAILQ_REMOVE(&bucket->bucket_entries, flow, entries);
                msp_objcache_free(entry_handle, flow, cpu, obj_cache_id);
                flow = next;
            } else {
                // Release the flow lock
                INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);
                flow = next;
            }
        }

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    msp_objcache_reclaim(shm_handle);
}


/**
 * Find all flow entries using the a mirror, and remove them
 */
void
clean_flows_with_any_mirror(void)
{
    uint32_t i, cpu;
    hash_bucket_t * bucket;
    flow_entry_t * flow, * next;
    delete_replication_data_t data;

    cpu = msp_get_current_cpu();

    for(i = 0; i < FLOW_BUCKET_COUNT; ++i) {

        bucket = &flows_table->hash_bucket[i];

        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

        flow = TAILQ_FIRST(&bucket->bucket_entries);

        while(flow != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(flow, entries);

            // Get the flow lock
            INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

            if(flow->maddr != 0) {
                // notify slave
                data.bucket = i;
                data.daddr = flow->daddr;
                data.dport = flow->dport;
                delete_replication_entry(&data);

                TAILQ_REMOVE(&bucket->bucket_entries, flow, entries);
                msp_objcache_free(entry_handle, flow, cpu, obj_cache_id);
                flow = next;
            } else {
                // Release the flow lock
                INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);
                flow = next;
            }
        }

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    msp_objcache_reclaim(shm_handle);
}


/**
 * Find all flow entries using the given mirror, and remove them
 *
 * @param[in] addr
 *      The mirror from address
 */
void
clean_flows_with_mirror(in_addr_t addr)
{
    uint32_t i, cpu;
    hash_bucket_t * bucket;
    flow_entry_t * flow, * next;
    delete_replication_data_t data;

    cpu = msp_get_current_cpu();

    for(i = 0; i < FLOW_BUCKET_COUNT; ++i) {

        bucket = &flows_table->hash_bucket[i];

        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

        flow = TAILQ_FIRST(&bucket->bucket_entries);

        while(flow != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(flow, entries);

            // Get the flow lock
            INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

            if(flow->daddr == addr && flow->maddr != 0) {
                // notify slave
                data.bucket = i;
                data.daddr = flow->daddr;
                data.dport = flow->dport;
                delete_replication_entry(&data);

                TAILQ_REMOVE(&bucket->bucket_entries, flow, entries);
                msp_objcache_free(entry_handle, flow, cpu, obj_cache_id);
                flow = next;
            } else {
                // Release the flow lock
                INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);
                flow = next;
            }
        }

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    msp_objcache_reclaim(shm_handle);
}


/**
 * Find all flow entries using the given mirror, and update them
 *
 * @param[in] addr
 *      The mirror from address
 *
 * @param[in] to
 *      The new mirror to address
 */
void
redirect_flows_with_mirror(in_addr_t addr, in_addr_t to)
{
    uint32_t i, cpu;
    hash_bucket_t * bucket;
    flow_entry_t * flow, * next;
    struct in_addr tmp;
    replication_data_t data;

    cpu = msp_get_current_cpu();

    for(i = 0; i < FLOW_BUCKET_COUNT; ++i) {

        bucket = &flows_table->hash_bucket[i];

        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

        flow = TAILQ_FIRST(&bucket->bucket_entries);

        while(flow != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(flow, entries);

            // Get the flow lock
            INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

            if(flow->daddr == addr && flow->maddr != 0) {

                flow->maddr = to; // update
                flow->m_vrf = 0;
                if(flow->maddr != 0) {
                    flow->m_vrf = 0;
                    // look up VRF in FDB
                    if(msp_fdb_get_all_route_records(fdb_handle, PROTO_IPV4,
                            set_vrf, &flow->m_vrf) != MSP_OK) {

                        tmp.s_addr = flow->maddr;
                        LOG(LOG_ERR, "Did not successfully lookup a VRF "
                                "for mirrored site %s", inet_ntoa(tmp));
                    }
                }

                // notify slave
                data.bucket = i;
                data.daddr = flow->daddr;
                data.dport = flow->dport;
                data.maddr = flow->maddr; // new
                data.m_vrf = flow->m_vrf; // new
                data.age_ts = flow->age_ts;
                data.ssrc = flow->ssrc;
                memcpy(&data.source, &flow->source, sizeof(source_t));
                data.rate = flow->rate;
                strncpy(data.mon, flow->mon, sizeof(MAX_MON_NAME_LEN));

                update_replication_entry(&data);
            }
            flow = next;
            INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);
        }

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    msp_objcache_reclaim(shm_handle);
}


/**
 * Find all flow entries using the given monitor, and remove them
 *
 * @param[in] name
 *      The monitor name
 */
void
clean_flows_with_monitor(char * name)
{
    uint32_t i, cpu;
    hash_bucket_t * bucket;
    flow_entry_t * flow, * next;
    delete_replication_data_t data;

    cpu = msp_get_current_cpu();

    for(i = 0; i < FLOW_BUCKET_COUNT; ++i) {

        bucket = &flows_table->hash_bucket[i];

        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

        flow = TAILQ_FIRST(&bucket->bucket_entries);

        while(flow != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(flow, entries);

            // Get the flow lock
            INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

            if(flow->mon && strcmp(flow->mon, name) == 0) {
                // notify slave
                data.bucket = i;
                data.daddr = flow->daddr;
                data.dport = flow->dport;
                delete_replication_entry(&data);

                TAILQ_REMOVE(&bucket->bucket_entries, flow, entries);
                msp_objcache_free(entry_handle, flow, cpu, obj_cache_id);

            } else {
                // Release the flow lock
                INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);
            }

            flow = next;
        }

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    msp_objcache_reclaim(shm_handle);
}


/**
 * Find all flow entries using the given monitor and prefix, and remove them
 *
 * @param[in] name
 *      The monitor name
 *
 * @param[in] prefix
 *      The prefix address portion
 *
 * @param[in] mask
 *      The prefix mask portion
 */
void
clean_flows_in_monitored_prefix(char * name, in_addr_t prefix, in_addr_t mask)
{
    uint32_t i, cpu;
    hash_bucket_t * bucket;
    flow_entry_t * flow, * next;
    delete_replication_data_t data;

    cpu = msp_get_current_cpu();

    for(i = 0; i < FLOW_BUCKET_COUNT; ++i) {

        bucket = &flows_table->hash_bucket[i];

        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

        flow = TAILQ_FIRST(&bucket->bucket_entries);

        while(flow != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(flow, entries);

            // Get the flow lock
            INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

            if(((name == NULL) || (flow->mon && strcmp(flow->mon, name) == 0))
               && (prefix & mask) == (flow->daddr & mask)) {

                // notify slave
                data.bucket = i;
                data.daddr = flow->daddr;
                data.dport = flow->dport;
                delete_replication_entry(&data);

                TAILQ_REMOVE(&bucket->bucket_entries, flow, entries);
                msp_objcache_free(entry_handle, flow, cpu, obj_cache_id);
            } else {
                // Release the flow lock
                INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);
            }
            flow = next;
        }

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    msp_objcache_reclaim(shm_handle);
}


/**
 * Get flow state from the "database" i.e. the flow hashtable. This is be sent
 * to the backup/slave data component. This is only called on the master.
 *
 * @param[in] last
 *      The last data returned, or NULL if looking for the first entry
 *
 * @param[out] next
 *      The data to populate
 *
 * @return
 *      SUCCESS if there was more flow state information found after last,
 *      which was the previous data returned in next. EFAIL, if no more.
 */
status_t
get_next_flow_state(replication_data_t * last, replication_data_t * next)
{
    uint32_t i;
    hash_bucket_t * bucket;
    flow_entry_t * flow;
    boolean return_next = (last == NULL);

    for(i = last->bucket; i < FLOW_BUCKET_COUNT; ++i) {

        bucket = &flows_table->hash_bucket[last->bucket];

        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

        flow = TAILQ_FIRST(&bucket->bucket_entries);

        while(flow != NULL) {

            // Get the flow lock
            INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

            if(return_next) {
                // return this flow's information
                next->bucket = i;
                next->daddr = flow->daddr;
                next->dport = flow->dport;
                next->age_ts = flow->age_ts;
                next->ssrc = flow->ssrc;
                memcpy(&next->source, &flow->source, sizeof(source_t));
                next->rate = flow->rate;
                next->maddr = flow->maddr;
                next->m_vrf = flow->m_vrf;
                strncpy(next->mon, flow->mon, sizeof(MAX_MON_NAME_LEN));
                return SUCCESS;
            }

            // match the last returned, then set return_next
            if(last->daddr == flow->daddr && last->dport == flow->dport) {
                // find the next flow and return it
                return_next = TRUE;
            }
            // Release the flow lock
            INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);
            flow = TAILQ_NEXT(flow, entries);
        }

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    return EFAIL;
}


/**
 * Add flow state to the backup "database" i.e. the flow hashtable. It only
 * gets added if the flow doesn't exist, otherwise it gets merged. This is
 * only called while this data component is a slave and not processing
 * traffic.
 *
 * @param[in] new_data
 *       The new data to add to the (backup) flow table in this slave component
 *
 * @return
 *      SUCCESS if the flow state information was added; EFAIL, if the
 *      OC memory allocation failed.
 */
status_t
add_flow_state(replication_data_t * new_data)
{
    hash_bucket_t * bucket;
    flow_entry_t * flow;
    uint32_t cpu, rc;

    cpu = msp_get_current_cpu();

    bucket = &flows_table->hash_bucket[new_data->bucket];

    // Get the bucket lock
    INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

    flow = TAILQ_FIRST(&bucket->bucket_entries);

    while(flow != NULL) {
        if(flow->daddr == new_data->daddr && flow->dport == new_data->dport) {
            break; // match
        }
        flow = TAILQ_NEXT(flow, entries);
    }

    if(flow == NULL) { // adding a new one
        flow = msp_objcache_alloc(entry_handle, cpu, obj_cache_id);
        if(flow == NULL) {
            // Release the bucket lock
            INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
            LOG(LOG_ERR, "%s: Failed to allocate object cache for a "
                    "flow entry", __func__);
            return EFAIL;
        }

        // init flow lock
        msp_spinlock_init(&flow->lock);
        flow->daddr = new_data->daddr;
        flow->dport = new_data->dport;
        // insert into bucket list of flows
        TAILQ_INSERT_HEAD(&bucket->bucket_entries, flow, entries);

    }
    // Get the flow lock
    INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

    // Release the bucket lock
    INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);

    // copy things into flow from backup data received

    flow->age_ts = new_data->age_ts;
    flow->ssrc = new_data->ssrc;
    memcpy(&flow->source, &new_data->source, sizeof(source_t));
    flow->rate = new_data->rate;
    flow->maddr = new_data->maddr;
    flow->m_vrf = new_data->m_vrf;

    // init flow info not present in new_data
    msp_spinlock_init(&flow->lock);
    flow->frag_group = 0;

    if(flow->rate != 0) {
        // init monitoring params for this flow

        rc = get_monitored_rate(flow->daddr, &flow->mon); // populate flow->mon
        if(rc && strcmp(new_data->mon, flow->mon) != 0) {
            LOG(LOG_ERR, "%s: Could not find the same monitor group in the "
                    "slave's configuration", __func__);
        }

        flow->base_ts = msp_hw_ts32_read();
        flow->mdi_mlr = 0;
        flow->pl_sum = 0;
        flow->vb_max = 0.0;
        flow->vb_min = 0.0;
        flow->vb_pre = 0.0;
        flow->vb_post = 0.0;
        flow->mdi_df = 0.0;
    } else {
        flow->mon = NULL;
    }

    INSIST_ERR(msp_spinlock_unlock(&flow->lock) == MSP_OK);

    return SUCCESS;
}


/**
 * Remove flow state from the backup "database" i.e. the flow hashtable. This
 * is only called while this data component is a slave and not processing
 * traffic.
 *
 * @param[in] data
 *      The keys to find the flow state to remove
 */
void
remove_flow_state(delete_replication_data_t * data)
{
    hash_bucket_t * bucket;
    flow_entry_t * flow;
    uint32_t cpu;

    cpu = msp_get_current_cpu();

    bucket = &flows_table->hash_bucket[data->bucket];

    // Get the bucket lock
    INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

    flow = TAILQ_FIRST(&bucket->bucket_entries);

    while(flow != NULL) {
        if(flow->daddr == data->daddr && flow->dport == data->dport) {

            INSIST_ERR(msp_spinlock_lock(&flow->lock) == MSP_OK);

            TAILQ_REMOVE(&bucket->bucket_entries, flow, entries);
            msp_objcache_free(entry_handle, flow, cpu, obj_cache_id);

            INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
            return;
        }
        flow = TAILQ_NEXT(flow, entries);
    }

    INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);

    LOG(LOG_ERR, "%s: Did not find a flow entry to remove", __func__);
}

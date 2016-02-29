/*
 * $Id: equilibrium-data_packet.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-data_packet.c
 * @brief Relating to processing packets in the fast path
 * 
 * These functions and types will manage the packet processing in the data path
 */

#include "equilibrium-data_main.h"
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/jnx/jbuf.h>
#include "equilibrium-data_config.h"
#include "equilibrium-data_monitor.h"
#include "equilibrium-data_packet.h"


/*** Constants ***/

#define EQ_SHARED_MEM_NAME    "equilibrium-data arena"    ///< shared mem name

#define EQ_SESSION_TABLE_NAME "equilibrium session table" ///< table o.c. name

#define EQ_SESSION_ENTRY_NAME "equilibrium session entry" ///< entry o.c. name

#define SESSION_AGE_CHECK_INTERVAL 15 ///< run aging routine interval in seconds

#define MAX_MSP_SEND_RETRIES 100 ///< Max msp_data_send retires before panic

/**
 * Must have this many bytes of each packet in the jbuf to analyze it
 */
#define IP_NEEDED_BYTES (sizeof(struct ip))

/**
 * Must have this many bytes of each packet in the jbuf to analyze it for nat
 */
#define TCP_NEEDED_BYTES (sizeof(struct ip) + sizeof(struct tcphdr))

/**
 * The number of buckets, must be a power of 2, current 2^19
 */
#define SESSIONS_BUCKET_COUNT (512 * 1024)

/**
 * Our mask defining the width of our hash function
 */
const uint32_t HASH_MASK = SESSIONS_BUCKET_COUNT - 1;


/*** Data Structures ***/

/**
 * flow entry information structure 
 */
typedef struct session_entry_s {
    struct session_entry_s     * reverse; ///< reverse flow contains other addr/port
    in_addr_t                    saddr;   ///< src IP address
    in_addr_t                    daddr;   ///< dest IP address
    uint16_t                     sport;   ///< src port
    uint16_t                     dport;   ///< dest port
    uint8_t                      cpu;     ///< the cpu on which this entry was allocated
    uint8_t                      dir;     ///< direction
    in_addr_t                    faddr;   ///< IP address of facade if INGRESS entry, or real server if EGRESS entry
    time_t                       age_ts;  ///< session/flow timestamp
    uint16_t                     ss_id;   ///< svc set id
    uint16_t                     frag_group; ///< fragment group (ID)
    msp_spinlock_t               lock;    ///< flow lock
    TAILQ_ENTRY(session_entry_s) entries; ///< next and prev list entries
} session_entry_t;


/**
 * A list/set of servers type as a tailq
 */
typedef TAILQ_HEAD(ht_bucket_list_s, session_entry_s) ht_bucket_list_t;


/**
 * A bucket in a hashtable_t
 */
typedef struct hash_bucket_s {
    msp_spinlock_t        bucket_lock;    ///< lock for this bucket
    ht_bucket_list_t      bucket_entries; ///< list of entries in bucket
} hash_bucket_t;


/**
 * A hashtable for the sessions_table
 */
typedef struct hashtable_s {
    hash_bucket_t hash_bucket[SESSIONS_BUCKET_COUNT]; ///<maps hashes to buckets
} hashtable_t;


static evTimerID aging_timer;        ///< timer set to do aging and cleanup
static msp_shm_handle_t shm_handle;  ///< handle for shared memory allocator
static msp_oc_handle_t table_handle; ///< handle for OC table allocator
static msp_oc_handle_t entry_handle; ///< handle for OC table entry allocator
static hashtable_t * sessions_table; ///< pointer to the hashtable of sessions
static atomic_uint_t loops_running;  ///< # of data loops running
static volatile uint8_t do_shutdown; ///< do the data loops need to shutdown
static uint32_t obj_cache_id;        ///< ID for OC memory tracking

/*** STATIC/INTERNAL Functions ***/


/**
 * Callback to periodically age out entries in our sessions_table and
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
    const time_t down_server_flow_duration = 60;
    const time_t non_app_flow_duration = 300;
    
    uint32_t i, cpu;
    hash_bucket_t * bucket;
    session_entry_t * session, * next;
    time_t current_time, down_server_flow_timeout, non_app_flow_timeout;
    
    cpu = msp_get_current_cpu();
    
    current_time = get_current_time();
    down_server_flow_timeout = current_time - down_server_flow_duration;
    non_app_flow_timeout = current_time - non_app_flow_duration;
    
    for(i = 0; i < SESSIONS_BUCKET_COUNT; ++i) {
        
        bucket = &sessions_table->hash_bucket[i];
        
        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);
      
        session = TAILQ_FIRST(&bucket->bucket_entries);
        
        while(session != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(session, entries);
            
            // Get the session lock
            INSIST_ERR(msp_spinlock_lock(&session->lock) == MSP_OK);
            
            // check for timeout/expiry
            
            if(session->faddr == 0) {
                // an entry for a non-application
                if(session->age_ts < non_app_flow_timeout) {
                    
                    TAILQ_REMOVE(&bucket->bucket_entries, session, entries);
                    msp_objcache_free(entry_handle, session, cpu, obj_cache_id);
                    session = next;
                    continue;
                }
            } else if(session->faddr == (in_addr_t)-1) {
                // entry for a flow matching an application w/ no servers up
                if(session->age_ts < down_server_flow_timeout) {
                    
                    TAILQ_REMOVE(&bucket->bucket_entries, session, entries);
                    msp_objcache_free(entry_handle, session, cpu, obj_cache_id);
                    session = next;
                    continue;
                }
            } else {
                // it is a session entry for an application
                
                if(session->dir == JBUF_PACKET_DIR_INGRESS) {
                    if(session->age_ts +
                        get_app_session_timeout(session->ss_id,
                            session->faddr, session->sport) < current_time) {

                        TAILQ_REMOVE(&bucket->bucket_entries, session, entries);
                        msp_objcache_free(entry_handle, session, cpu, obj_cache_id);
                        session = next;
                        continue;
                    }
                } else {
                    if(session->age_ts +
                       get_app_session_timeout(session->ss_id,
                            session->daddr, session->dport) < current_time) {
                        
                        // since it is egress
                        monitor_remove_session_for_server(session->ss_id,
                                session->daddr, session->dport, session->faddr);
                        
                        TAILQ_REMOVE(&bucket->bucket_entries, session, entries);
                        msp_objcache_free(entry_handle, session, cpu, obj_cache_id);
                        session = next;
                        continue;
                    }
                }
            }
            
            // Release the session lock
            INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
            session = next;
        }
        
        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }
    
    monitor_send_stats(); // (update to mgmt component)
    
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
 * Process an IP packet that is TCP, and the TCP header must be available 
 * in the bytes following the IP address.
 * 
 * @param[in] pkt_buf
 *      The received jbuf for this packet, that will 
 *      go to http_request_filter if needed
 * 
 * @param[in] ip_pkt
 *      The IP packet
 * 
 * @param[in] ss_info
 *      The service set info from the jbuf
 * 
 * @param[in] cpu
 *      The current cpu of the caller (used for object cache alloc calls)
 * 
 * @return SUCCESS if the packet can be sent out; EFAIL if it can be dropped
 */
static status_t
process_packet(struct ip * ip_pkt,
               jbuf_svc_set_info_t * ss_info,
               int cpu)
{
    uint32_t hash;
    uint16_t * key;
    int len;
    in_addr_t facade, real;
    hash_bucket_t * bucket;
    session_entry_t * session;
    struct tcphdr * tcp_hdr = 
        (struct tcphdr *)((uint32_t *)ip_pkt + ip_pkt->ip_hl);
    
    len = ip_pkt->ip_len - (ip_pkt->ip_hl * 4); // TCP hdr + TCP segment length
    
    // subtract length of TCP header to get payload length
    len -= tcp_hdr->th_off * 4;
    
    if(tcp_hdr->th_off < 5 || len < 0) { // check for malformed TCP header
        LOG(LOG_ERR, "%s: Found a malformed TCP header in a packet.",
                __func__);
        return EFAIL;
    }
    
    // get hash of the two IP addresses
    
    key = (uint16_t *)&ip_pkt->ip_src;
    hash = 0x5F5F;
    hash = ((hash << 5) + hash) ^ key[0];
    hash = ((hash << 5) + hash) ^ key[1];
    hash = ((hash << 5) + hash) ^ key[2];
    hash = ((hash << 5) + hash) ^ key[3];
    
    hash &= HASH_MASK;
    
    // use hash to lookup a hash bucket and find the matching entry
    
    bucket = &sessions_table->hash_bucket[hash]; // get bucket
    
    // Get the bucket lock
    INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);
  
    session = TAILQ_FIRST(&bucket->bucket_entries);
    
    while(session != NULL) {
        if(session->saddr == ip_pkt->ip_src.s_addr
                && session->daddr == ip_pkt->ip_dst.s_addr
                && session->ss_id == ss_info->info.intf_type.svc_set_id
                && session->dport == tcp_hdr->th_dport
                && session->sport == tcp_hdr->th_sport) {

            break; // match
        }
        session = TAILQ_NEXT(session, entries);
    }
    
    // if there's no matching session, then create one... the slow path
    if(session == NULL) {
        if(ss_info->pkt_dir == JBUF_PACKET_DIR_INGRESS) {
            // not initiated by a client,
            // so definietly can't belong to an application
            INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
            return SUCCESS;
        }
        // SLOW PATH FOR EGRESS TRAFFIC:
        
        session = msp_objcache_alloc(entry_handle, cpu, obj_cache_id);
        if(session == NULL) {
            // Release the bucket lock
            INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
            
            LOG(LOG_ERR, "%s: Failed to allocate object cache for a "
                    "session entry", __func__);
            return EFAIL;
        }
        session->reverse = msp_objcache_alloc(entry_handle, cpu, obj_cache_id);
        if(session->reverse == NULL) {
            
            msp_objcache_free(entry_handle, session, cpu, obj_cache_id);
            
            // Release the bucket lock
            INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
            
            LOG(LOG_ERR, "%s: Failed to allocate object cache for a "
                    "session reverse entry", __func__);
            return EFAIL;
        }
        
        // init and grab lock
        msp_spinlock_init(&session->lock);
        INSIST_ERR(msp_spinlock_lock(&session->lock) == MSP_OK);
        
        TAILQ_INSERT_HEAD(&bucket->bucket_entries, session, entries);
        
        session->age_ts = get_current_time();
        session->saddr = ip_pkt->ip_src.s_addr;
        session->daddr = ip_pkt->ip_dst.s_addr;
        session->sport = tcp_hdr->th_sport;
        session->dport = tcp_hdr->th_dport;
        session->ss_id = ss_info->info.intf_type.svc_set_id;
        session->dir = JBUF_PACKET_DIR_EGRESS;
        session->cpu = cpu;
        

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
        
        // init reverse
        
        // init and grab lock
        msp_spinlock_init(&session->reverse->lock);
        INSIST_ERR(msp_spinlock_lock(&session->reverse->lock) == MSP_OK);
        
        session->reverse->age_ts = session->age_ts;
        session->reverse->saddr = ip_pkt->ip_dst.s_addr;
        session->reverse->daddr = ip_pkt->ip_src.s_addr;
        session->reverse->sport = tcp_hdr->th_dport;
        session->reverse->dport = tcp_hdr->th_sport;
        session->reverse->ss_id = ss_info->info.intf_type.svc_set_id;
        session->reverse->reverse = session;
        session->reverse->dir = JBUF_PACKET_DIR_INGRESS;
        session->reverse->cpu = cpu;
        
        // find out if this flow will match an application
        // ip_dst must match that of an application in this service set
        
        session->faddr = monitor_get_server_for(
                session->ss_id, session->daddr, session->dport);
        
        if(session->faddr == (in_addr_t)-1) {
            // indicate it is for an app, but no servers are up
            session->reverse->faddr = (in_addr_t)-1;
            session->reverse->saddr = ip_pkt->ip_dst.s_addr;
        } else if(session->faddr == 0) {
            // indicate not for an app
            session->reverse->faddr = 0;
            session->reverse->saddr = ip_pkt->ip_dst.s_addr;
        } else {
            // session->faddr is actually the real server address now
            
            // for reverse set faddr to the facade
            session->reverse->faddr = ip_pkt->ip_dst.s_addr;
            
            // ingress src will be real server
            session->reverse->saddr = session->faddr;
        }
        
        session->reverse->frag_group = 0;
        
        // If it is the first fragment note the frag ID
        if((ntohs(ip_pkt->ip_off) & IP_MF)) {
            session->frag_group = ip_pkt->ip_id;
        } else {
            session->frag_group = 0;
        }
        
        // get hash of the reverse flow to find bucket for reverse entry

        hash = 0x5F5F;
        key = (uint16_t *)&session->reverse->saddr;
        hash = ((hash << 5) + hash) ^ key[0];
        hash = ((hash << 5) + hash) ^ key[1];
        hash = ((hash << 5) + hash) ^ key[2];
        hash = ((hash << 5) + hash) ^ key[3];        
        hash &= HASH_MASK;
        
        // use hash to lookup a hash bucket and find the matching entry
        bucket = &sessions_table->hash_bucket[hash]; // get bucket
        
        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);
        
        TAILQ_INSERT_HEAD(&bucket->bucket_entries, session->reverse, entries);
        
        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);        
        
        if(session->faddr == 0) {
            // Release the session lock
            INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
            INSIST_ERR(msp_spinlock_unlock(&session->reverse->lock) == MSP_OK);
            return SUCCESS; // not an application, so nothing to change
        }
        if(session->faddr == (in_addr_t)-1) {
            // Release the session lock
            INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
            INSIST_ERR(msp_spinlock_unlock(&session->reverse->lock) == MSP_OK);
            
            LOG(LOG_ERR, "%s: Dropping packet found to an application with no "
                    "servers up.", __func__);
            return EFAIL; // indicate to drop, when no servers are up
        }
        
        // going to a real server for an application, so
        // replace the facade's address with the real server's address
        
        // adjust IP checksum taking IP addresses into account
        checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
            (unsigned char *)&ip_pkt->ip_dst, sizeof(in_addr_t),
            (unsigned char *)&session->faddr, sizeof(in_addr_t));
        
        // adjust TCP checksum taking IP addresses into account
        checksum_adjust((unsigned char *)&tcp_hdr->th_sum,
            (unsigned char *)&ip_pkt->ip_dst, sizeof(in_addr_t),
            (unsigned char *)&session->faddr, sizeof(in_addr_t));
        
        // change address
        facade = ip_pkt->ip_dst.s_addr;
        real = ip_pkt->ip_dst.s_addr = session->faddr;
        
        // Release the session lock (don't need for filter)
        INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
        INSIST_ERR(msp_spinlock_unlock(&session->reverse->lock) == MSP_OK);
        
        return SUCCESS;
    }
    
    // else there's a matching session, so use it to forward the traffic
    // FAST PATH:

    // Get the session lock
    INSIST_ERR(msp_spinlock_lock(&session->lock) == MSP_OK);
    
    // Release the bucket lock
    INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    
    // If it is the first fragment note the frag ID
    if((ntohs(ip_pkt->ip_off) & IP_MF)) {
        session->frag_group = ip_pkt->ip_id;
    } else {
        session->frag_group = 0;
    }
    
    if(session->faddr == (in_addr_t)-1) {
        // all servers down for the application for this flow
        
        // Release the session lock
        INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
        
        LOG(LOG_ERR, "%s: Dropping packet found to an application with no "
                "servers up.", __func__);
        
        // don't refresh timestamp, and don't send out traffic (drop)
        return EFAIL;
    }
    
    session->age_ts = get_current_time();
    
    if(session->faddr != 0) { // it is for an application
        if(ss_info->pkt_dir == JBUF_PACKET_DIR_INGRESS) {
            // coming from a server, so replace the real server's address (src) 
            // with the facade's address  (stored in faddr for INGRESS)
            
            // adjust IP checksum taking IP addresses into account
            checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
                (unsigned char *)&ip_pkt->ip_src, sizeof(in_addr_t),
                (unsigned char *)&session->faddr, sizeof(in_addr_t));
            
            // adjust TCP checksum taking IP addresses into account
            checksum_adjust((unsigned char *)&tcp_hdr->th_sum,
                (unsigned char *)&ip_pkt->ip_src, sizeof(in_addr_t),
                (unsigned char *)&session->faddr, sizeof(in_addr_t));
            
            // change address
            ip_pkt->ip_src.s_addr = session->faddr;
        } else {
            // going to a server, so replace the facade's address (dst)
            // with the real server's address (stored in faddr for EGRESS)
            
            // adjust IP checksum taking IP addresses into account
            checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
                (unsigned char *)&ip_pkt->ip_dst, sizeof(in_addr_t),
                (unsigned char *)&session->faddr, sizeof(in_addr_t));
            
            // adjust TCP checksum taking IP addresses into account
            checksum_adjust((unsigned char *)&tcp_hdr->th_sum,
                (unsigned char *)&ip_pkt->ip_dst, sizeof(in_addr_t),
                (unsigned char *)&session->faddr, sizeof(in_addr_t));
            
            // change address
            facade = ip_pkt->ip_dst.s_addr;
            real = ip_pkt->ip_dst.s_addr = session->faddr;
            
            // Release the session lock (faster...don't need for filter)
            INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
            
            return SUCCESS;
        }
    }
    
    // Release the session lock
    INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
    
    return SUCCESS;
}


/**
 * Process an IP packet that is TCP, but it is an IP fragment, 
 * and not the first. We assume, no TCP header info is available, and a 
 * session must exist for it.
 * 
 * @param[in] ip_pkt
 *      The IP packet
 * 
 * @param[in] ss_info
 *      The service set info from the jbuf
 * 
 * @return SUCCESS if the packet can be sent out; EFAIL if it can be dropped
 */
static status_t
process_fragment(struct ip * ip_pkt, jbuf_svc_set_info_t * ss_info)
{
    uint32_t hash;
    uint16_t * key;
    hash_bucket_t * bucket;
    session_entry_t * session;
    
    // get hash of the two IP addresses
    
    key = (uint16_t *)&ip_pkt->ip_src;
    hash = 0x5F5F;
    hash = ((hash << 5) + hash) ^ key[0];
    hash = ((hash << 5) + hash) ^ key[1];
    hash = ((hash << 5) + hash) ^ key[2];
    hash = ((hash << 5) + hash) ^ key[3];
    
    hash &= HASH_MASK;    
    
    // use hash to lookup a hash bucket and find the matching entry
    
    bucket = &sessions_table->hash_bucket[hash]; // get bucket
    
    // Get the bucket lock
    INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);
  
    session = TAILQ_FIRST(&bucket->bucket_entries);
    
    while(session != NULL) {
        if(session->saddr == ip_pkt->ip_src.s_addr 
                && session->daddr == ip_pkt->ip_dst.s_addr
                && session->ss_id == ss_info->info.intf_type.svc_set_id
                && session->frag_group == ip_pkt->ip_id) {

            break; // match
        }
        session = TAILQ_NEXT(session, entries);
    }
    
    // if there's no matching session, so we haven't seen the first fragment yet
    if(session == NULL) {
        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
        
        LOG(LOG_WARNING, "%s: Received a packet from %s. It is an IP "
                "fragment, but we have not yet received the first "
                "fragment, so we cannot tell if it belongs to an "
                "equilibrium application",
            __func__, inet_ntoa(ip_pkt->ip_src));
        
        return SUCCESS; // don't know anything about this flow
    }
    
    // else there's a matching session, so use it to forward the traffic
    // FAST PATH:

    // Get the session lock
    INSIST_ERR(msp_spinlock_lock(&session->lock) == MSP_OK);
    
    // Release the bucket lock
    INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    
    if(session->faddr == (in_addr_t)-1) {
        // all servers down for the application for this flow
        
        // Release the session lock
        INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
        
        LOG(LOG_ERR, "%s: Dropping packet found to an application with no "
                "servers up.", __func__);
        
        // don't refresh timestamp, and don't send out traffic (drop)
        return EFAIL;
    }
    
    session->age_ts = get_current_time();
    
    if(session->faddr != 0) { // it is for an application
        if(ss_info->pkt_dir == JBUF_PACKET_DIR_INGRESS) {
            // coming from a server, so replace the real server's address (src) 
            // with the facade's address  (stored in faddr for INGRESS)
            
            // adjust IP checksum taking IP addresses into account
            checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
                (unsigned char *)&ip_pkt->ip_src, sizeof(in_addr_t),
                (unsigned char *)&session->faddr, sizeof(in_addr_t));
            
            // change address
            ip_pkt->ip_src.s_addr = session->faddr;
        } else {
            // going to a server, so replace the facade's address (dst)
            // with the real server's address (stored in faddr for EGRESS)
            
            // adjust IP checksum taking IP addresses into account
            checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
                (unsigned char *)&ip_pkt->ip_dst, sizeof(in_addr_t),
                (unsigned char *)&session->faddr, sizeof(in_addr_t));
            
            // change address
            ip_pkt->ip_dst.s_addr = session->faddr;
        }
    }
    
    // Release the session lock
    INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
    
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
        jbuf_free(pkt_buf);
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
equilibrium_process_packet(msp_dataloop_args_t * params)
{
    struct jbuf * pkt_buf;
    jbuf_svc_set_info_t ss_info;
    struct ip * ip_pkt;
    int type;
    uint16_t ip_frag_offset;
    uint8_t ip_options_bytes = 0;
    int cpu;
    
    atomic_add_uint(1, &loops_running);
    
    cpu = msp_data_get_cpu_num(params->dhandle);
    INSIST_ERR(cpu != MSP_NEXT_NONE);
    
    // Start the packet loop...
    while(!do_shutdown) {
        
        // Dequeue a packet from the rx-fifo
        pkt_buf = msp_data_recv(params->dhandle, &type);
        
        if(pkt_buf == NULL) { // Didn't get anything
            continue;
        }

        if(type != MSP_MSG_TYPE_PACKET) { // Didn't get network traffic
            LOG(LOG_WARNING, "%s: Message wasn't a packet...dropping",
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
        
        if(!ip_pkt || ip_pkt->ip_p != IPPROTO_TCP) {
            send_packet(pkt_buf, &params->dhandle);
            continue;
        }
        
        jbuf_get_svc_set_info(pkt_buf, &ss_info);
        INSIST_ERR(ss_info.svc_type == JBUF_SVC_TYPE_INTERFACE);
        
        ip_frag_offset = ntohs(ip_pkt->ip_off);
        ip_options_bytes = (ip_pkt->ip_hl * 4) - sizeof(struct ip);
        
        if((ip_frag_offset & IP_OFFMASK)) {
            
            // It's a fragment, but not the first fragment
            if(process_fragment(ip_pkt, &ss_info)) {

                LOG(LOG_NOTICE, "%s: Dropping a packet who's processing failed",
                    __func__);
                
                jbuf_free(pkt_buf);
                continue;
            }
        } else if(!pullup_bytes(&pkt_buf, TCP_NEEDED_BYTES+ ip_options_bytes)) {
            
            // It is TCP, and could be the first fragment or normal
            if(process_packet(ip_pkt, &ss_info, cpu)) {
                
                LOG(LOG_NOTICE, "%s: Dropping a packet who's processing failed",
                    __func__);
                
                jbuf_free(pkt_buf);
                
                continue;
            }
        } else {
            LOG(LOG_NOTICE, "%s: Did not process a packet from %s. There's not "
                "enough bytes to form the TCP header.",
                __func__, inet_ntoa(ip_pkt->ip_src));
        }
        
        send_packet(pkt_buf, &params->dhandle);
    }
    
    atomic_sub_uint(1, &loops_running);
    
    return NULL; // thread is done if it reaches this point
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Start packet loops, one per available data core
 * 
 * @param[in] ctx
 *     event context from master thread used for cleanup timer 
 * 
 * @return SUCCESS we created one data cpu on each available data core;
 *       otherwise EFAIL (error is logged) 
 */
status_t
init_packet_loops(evContext ctx)
{
    int i, rc, core;
    msp_dataloop_params_t params;
    msp_dataloop_result_t result;
    msp_shm_params_t shmp;
    msp_objcache_params_t ocp;
    
    shm_handle = table_handle = entry_handle = sessions_table = NULL;
    evInitID(&aging_timer);
    
    LOG(LOG_INFO, "%s: Initializing object cache for data loops", __func__);
    
    bzero(&shmp, sizeof(shmp));
    bzero(&ocp, sizeof(ocp));

    // allocate & initialize the shared memory
    
    strncpy(shmp.shm_name, EQ_SHARED_MEM_NAME, SHM_NAME_LEN);
    
    if(msp_shm_allocator_init(&shmp) != MSP_OK) {
        LOG(LOG_ERR, "%s: Shared memory allocator initialization failed",
                __func__);
        return EFAIL;
    }

    shm_handle = shmp.shm; // get handle
    
    // create object cache allocator for the session/flow look up table
    ocp.oc_shm = shm_handle;
    ocp.oc_size  = sizeof(hashtable_t);
    strncpy(ocp.oc_name, EQ_SESSION_TABLE_NAME, OC_NAME_LEN);

    if(msp_objcache_create(&ocp) != MSP_OK) {
        LOG(LOG_ERR, "%s: Object-cache allocator initialization failed (table)",
                __func__);
        return EFAIL;
    }

    table_handle = ocp.oc; // get handle

    // create object cache allocator for the session/flow look up table entries
    ocp.oc_shm = shmp.shm;
    ocp.oc_size  = sizeof(session_entry_t);
    strncpy(ocp.oc_name, EQ_SESSION_TABLE_NAME, OC_NAME_LEN);

    if (msp_objcache_create(&ocp) != MSP_OK) {
        LOG(LOG_ERR, "%s: Object-cache allocator initialization failed (entry)",
                __func__);
        return EFAIL;
    }

    entry_handle = ocp.oc; // get handle
    
    // allocate sessions_table in OC:
    
    sessions_table = msp_objcache_alloc(table_handle,
            msp_get_current_cpu(), obj_cache_id);
    if(sessions_table == NULL) {
        LOG(LOG_ERR, "%s: Failed to allocate object cache for sessions table ",
                __func__);
        return EFAIL;
    }
    
    for(i = 0; i < SESSIONS_BUCKET_COUNT; ++i) {
        msp_spinlock_init(&sessions_table->hash_bucket[i].bucket_lock);
        TAILQ_INIT(&sessions_table->hash_bucket[i].bucket_entries);
    }
    
    // start ager
    
    if(evSetTimer(ctx, aging_cleanup, NULL,
            evAddTime(evNowTime(), evConsTime(SESSION_AGE_CHECK_INTERVAL, 0)),
            evConsTime(SESSION_AGE_CHECK_INTERVAL, 0),
            &aging_timer)) {

        LOG(LOG_EMERG, "%s: Failed to initialize a timer to periodically "
            "check age of session entries (Error: %m)", __func__);
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
            
            rc = msp_data_create_loop_on_cpu(rc, equilibrium_process_packet,
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
 * Cleanup shared memory and data loops for shutdown
 * 
 * @param[in] ctx
 *     event context from master thread used for cleanup timer 
 */
void
destroy_packet_loops(evContext ctx)
{
    if(evTestID(aging_timer)) {
        evClearTimer(ctx, aging_timer);
        evInitID(&aging_timer);
    }
    
    do_shutdown = 1;
    
    while(loops_running > 0) ; // note the spinning while waiting
    
    // now they are all shutdown
    
    if(sessions_table) {
        msp_objcache_free(
                table_handle, sessions_table, msp_get_current_cpu(), obj_cache_id);
        sessions_table = NULL;
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
 * Find all session entries using the given server, and remove them
 * 
 * @param[in] ss_id
 *      The service set id
 * 
 * @param[in] app_addr
 *      The application address
 * 
 * @param[in] app_port
 *      The application port
 * 
 * @param[in] server_addr
 *      The server address
 */
void
clean_sessions_using_server(uint16_t ss_id,
                            in_addr_t app_addr,
                            uint16_t app_port,
                            in_addr_t server_addr)
{
    uint32_t i, cpu;
    hash_bucket_t * bucket;
    session_entry_t * session, * next;
    
    cpu = msp_get_current_cpu();
    
    for(i = 0; i < SESSIONS_BUCKET_COUNT; ++i) {
        
        bucket = &sessions_table->hash_bucket[i];
        
        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);
      
        session = TAILQ_FIRST(&bucket->bucket_entries);
        
        while(session != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(session, entries);
            
            // Get the session lock
            INSIST_ERR(msp_spinlock_lock(&session->lock) == MSP_OK);
            
            if(session->ss_id == ss_id &&
                ((session->dir == JBUF_PACKET_DIR_INGRESS &&
                   session->faddr == app_addr && session->sport == app_port &&
                        session->saddr == server_addr) ||
                  (session->dir == JBUF_PACKET_DIR_EGRESS &&
                   session->faddr == server_addr && session->dport == app_port &&
                        session->daddr == app_addr))) {
                
                // we need to delete this session
                
                TAILQ_REMOVE(&bucket->bucket_entries, session, entries);
                msp_objcache_free(entry_handle, session, cpu, obj_cache_id); 
            } else {
                // Release the session lock
                INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
            }
            session = next;
        }
        
        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }
    
    msp_objcache_reclaim(shm_handle);
}


/**
 * Find all session entries using the given server, and remove them
 * 
 * @param[in] ss_id
 *      The service set id
 * 
 * @param[in] app_addr
 *      The application address
 * 
 * @param[in] app_port
 *      The application port
 */
void
clean_sessions_with_app(uint16_t ss_id,
                        in_addr_t app_addr,
                        uint16_t app_port)
{
    uint32_t i, cpu;
    hash_bucket_t * bucket;
    session_entry_t * session, * next;
    
    cpu = msp_get_current_cpu();
    
    for(i = 0; i < SESSIONS_BUCKET_COUNT; ++i) {
        
        bucket = &sessions_table->hash_bucket[i];
        
        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);
      
        session = TAILQ_FIRST(&bucket->bucket_entries);
        
        while(session != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(session, entries);
            
            // Get the session lock
            INSIST_ERR(msp_spinlock_lock(&session->lock) == MSP_OK);
            
            if(session->ss_id == ss_id &&
                ((session->dir == JBUF_PACKET_DIR_INGRESS &&
                  session->faddr == app_addr && session->sport == app_port) ||
                 (session->dir == JBUF_PACKET_DIR_EGRESS &&
                  session->daddr == app_addr && session->dport == app_port))) {
                
                // we need to delete this session
                
                TAILQ_REMOVE(&bucket->bucket_entries, session, entries);
                msp_objcache_free(entry_handle, session, cpu, obj_cache_id); 
            } else {
                // Release the session lock
                INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
            }
            session = next;
        }
        
        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }
    
    msp_objcache_reclaim(shm_handle);
}


/**
 * Find all session entries using the given server, and remove them
 * 
 * @param[in] ss_id
 *      The service set id
 */
void
clean_sessions_with_service_set(uint16_t ss_id)
{
    uint32_t i, cpu;
    hash_bucket_t * bucket;
    session_entry_t * session, * next;

    cpu = msp_get_current_cpu();
    
    for(i = 0; i < SESSIONS_BUCKET_COUNT; ++i) {
        
        bucket = &sessions_table->hash_bucket[i];
        
        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);
      
        session = TAILQ_FIRST(&bucket->bucket_entries);

        while(session != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(session, entries);
            
            // Get the session lock
            INSIST_ERR(msp_spinlock_lock(&session->lock) == MSP_OK);
            
            if(session->ss_id == ss_id) {
                
                // we need to delete this session
                
                TAILQ_REMOVE(&bucket->bucket_entries, session, entries);
                msp_objcache_free(entry_handle, session, cpu, obj_cache_id); 
            } else {
                // Release the session lock
                INSIST_ERR(msp_spinlock_unlock(&session->lock) == MSP_OK);
            }
            session = next;
        }
        
        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }
    
    msp_objcache_reclaim(shm_handle);
}

/*
 * $Id: reassembler_packet.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file reassembler_packet.c
 * @brief Relating to processing packets in the fast path
 *
 * These functions and types will manage the packet processing in the data path
 */

#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <sys/limits.h>
#include <isc/eventlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/mpsdk.h>
#include <jnx/msp_objcache.h>
#include <jnx/atomic.h>
#include <jnx/msp_locks.h>
#include <sys/jnx/jbuf.h>
#include "reassembler_logging.h"
#include "reassembler_packet.h"


/*** Constants ***/

#define SHARED_MEM_NAME "reassembler arena"    ///< shared mem name

#define HASHTABLE_NAME "reassembler hash table" ///< table o.c. name

#define TABLE_ENTRY_NAME "reassembler table entry" ///< entry o.c. name

#define FRAGMENT_ENTRY_NAME "reassembler fragment entry" ///< frag o.c. name

#define ENTRY_AGE_CHECK_INTERVAL 5 ///< run aging routine interval in seconds

#define MAX_MSP_SEND_RETRIES 100 ///< Max msp_data_send retries before panic

#define MIN_FIFO_DEPTH 1023 ///< Minimum FIFO depth (used with max # of dCPUs)

#define BYTES_PER_FRAG_BLOCK 8 ///< Bytes in a fragment block (for frag offset)

#define WORDS_PER_FRAG_BLOCK 2 ///< Words in a fragment block (for frag offset)

/**
 * The number of buckets; must be a power of 2, currently 2^16
 */
#define FLOW_BUCKET_COUNT (1024 * 64)

/**
 * Our mask defining the width of our hash function
 */
const uint32_t HASH_MASK = FLOW_BUCKET_COUNT - 1;

/*** Data Structures ***/

/**
 * fragment hole information structure
 * 
 * @note fragment blocks are 8 bytes
 */
typedef struct fragment_hole_s {
    uint16_t start;   ///< start of hole offset (counted in fragment blocks)
    uint16_t end;     ///< end of hole offset (counted in fragment blocks)
} fragment_hole_t;


/**
 * fragment entry list item information structure
 */
typedef struct fragment_entry_s {
    fragment_hole_t   leading_hole;  ///< the hole    
    struct jbuf *     jb;            ///< the jbuf
    fragment_hole_t   trailing_hole; ///< the hole
    
    bool is_leading_hole;  ///< T = leading hole present, F = n/a
    bool is_trailing_hole; ///< T = trailing hole present, F = n/a
    
    // for list at this hash bucket:
    TAILQ_ENTRY(fragment_entry_s) entries; ///< next and prev list entries
} fragment_entry_t;


/**
 * A list/set of servers type as a tailq
 */
typedef TAILQ_HEAD(fragment_list_s, fragment_entry_s) fragment_list_t;


/**
 * table entry information structure
 */
typedef struct table_entry_s {
    msp_spinlock_t               lock;       ///< entry lock
    time_t                       age_ts;     ///< entry age timestamp
    
    in_addr_t                    saddr;      ///< src IP address (HT KEY field)
    in_addr_t                    daddr;      ///< dest IP address (HT KEY field)
    uint16_t                     frag_group; ///< frag group (ID) (HT KEY field)
    uint8_t                      protocol;   ///< protocol (ID) (HT KEY field)
    
    uint8_t                      free;       ///< flag to free entry
    uint16_t                     total_len;  ///< total length of bytes in flist
    fragment_list_t              flist;      ///< list of fragments and holes

    // for list at this hash bucket:
    TAILQ_ENTRY(table_entry_s)    entries;    ///< next and prev list entries
} table_entry_t;


/**
 * A list/set of servers type as a tailq
 */
typedef TAILQ_HEAD(ht_bucket_list_s, table_entry_s) ht_bucket_list_t;


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
 * In the hashtable the KEY fields are hashed to lookup a bucket, then
 * within the bucket list an input search KEY must match exactly with an
 * existing entry's KEY (i.e. all KEY fields).
 *
 * The hashtable value is the whole table entry (in which the key info is also
 * stored).
 */
typedef struct hashtable_s {
    hash_bucket_t hash_bucket[FLOW_BUCKET_COUNT]; ///<maps hashes to buckets
} hashtable_t;


extern uint16_t         reassembler_mtu;  ///< the configured MTU

static evTimerID        aging_timer;   ///< timer set to do aging and cleanup
static msp_shm_handle_t shm_handle;    ///< handle for shared memory allocator
static msp_oc_handle_t  table_handle;  ///< handle for OC table allocator
static msp_oc_handle_t  entry_handle;  ///< handle for OC table entry allocator
static msp_oc_handle_t  frag_handle;   ///< handle for OC frag entry allocator
static hashtable_t *    flows_table;   ///< pointer to the hashtable of flows
static atomic_uint_t    loops_running; ///< # of data loops running
static volatile uint8_t do_shutdown;   ///< do the data loops need to shutdown
static uint32_t         obj_cache_id;  ///< ID for OC memory tracking


/*** STATIC/INTERNAL Functions ***/


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
aging_cleanup(evContext ctx UNUSED,
              void * uap UNUSED,
              struct timespec due UNUSED,
              struct timespec inter UNUSED)
{
    const time_t flow_duration = 30;

    uint32_t i, cpu;
    hash_bucket_t * bucket;
    table_entry_t * entry, * next;
    time_t current_time, entry_timeout;
    struct timeval curtime;

    cpu = msp_get_current_cpu();
    
    if(!lw_getsystimes(&curtime, NULL)) {
        current_time = curtime.tv_sec;
    } else {
        LOG(LOG_ERR, "%s: Cannot get a timestamp", __func__);
        return;
    }
    
    entry_timeout = current_time - flow_duration;

    for(i = 0; i < FLOW_BUCKET_COUNT; ++i) {

        bucket = &flows_table->hash_bucket[i];

        // Get the bucket lock
        INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

        entry = TAILQ_FIRST(&bucket->bucket_entries);

        while(entry != NULL) {

            // keep next to safely remove from list
            next = TAILQ_NEXT(entry, entries);

            // Get the entry lock
            INSIST_ERR(msp_spinlock_lock(&entry->lock) == MSP_OK);

            // check for timeout/expiry or free flag
            if(entry->free || entry->age_ts < entry_timeout) {
                TAILQ_REMOVE(&bucket->bucket_entries, entry, entries);
                msp_objcache_free(entry_handle, entry, cpu, obj_cache_id);
            } else {
                // Release the entry lock
                INSIST_ERR(msp_spinlock_unlock(&entry->lock) == MSP_OK);
            }
            entry = next;
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
 * Send the packet or other message; frees the packet upon failure
 *
 * @param[in] pkt_buf
 *      The packet in jbuf format as we received it
 *
 * @param[in] handle
 *      The handle for the data loop
 */
static void
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

        DLOG(LOG_ERR, "%s: Failed to forward packet using msp_data_send().",
            __func__);
        jbuf_free(pkt_buf);
        
    } else if(rc == MSP_DATA_SEND_RETRY) {

        DLOG(LOG_ERR, "%s: Failed to send a jbuf after %d retries "
            "with msp_data_send().", __func__, MAX_MSP_SEND_RETRIES);
        jbuf_free(pkt_buf);
        
    } else if(rc != MSP_OK) {

        DLOG(LOG_ERR, "%s: Failed to forward packet and got unknown return "
            "code from msp_data_send().", __func__);
        jbuf_free(pkt_buf);
    }
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

    if((*pkt_buf)->jb_len < num_bytes) {
        tmp_buf = jbuf_pullup((*pkt_buf), num_bytes);

        if(!tmp_buf) { // check it didn't fail
            return EFAIL;
        }

        *pkt_buf = tmp_buf;
    }
    return SUCCESS;
}


/**
 * Process a fragment list in the given table entry and send out the reassembled
 * packet held in the list. If the total length exceeds the configured MTU
 * (reassembler_mtu), then re-fragment at nearest 8-byte boundary <= 
 * reassembler_mtu. The fragment list is emptied and freed.
 * Any errors are logged, but list is freed regardless. 
 *
 * @param[in] entry
 *      The table entry with the fragment list to use
 *
 * @param[in] handle
 *      The data handle for the current dCPU/FIFO; used to send packet
 */
static void
send_fragment_list(table_entry_t * entry,
                   const int cpu,
                   const msp_data_handle_t const * handle)
{
    struct jbuf * pkt_buf, * ip_hdr, * tmp;
    fragment_entry_t * fe;
    struct ip * ip_pkt;
    uint16_t len = 0;
    uint16_t orig_ip_offset = htons(IP_MF);
    uint16_t new_len;
    uint16_t cur_frag_off = 0;
    uint16_t offset_increment;
    uint16_t mtu; // mtu incl. IP hdr + IP PL
    
    pkt_buf = jbuf_get();
    
    fe = TAILQ_FIRST(&entry->flist);
    if(!fe) {
        DLOG(LOG_ERR, "%s: found nothing in list", __func__);
        return;
    }
    TAILQ_REMOVE(&entry->flist, fe, entries);
    
    ip_hdr = fe->jb;
    if(pullup_bytes(&ip_hdr, sizeof(struct ip))) {
        DLOG(LOG_ERR, "%s: Not enough bytes to form an IP header because"
                "because a pullup failed (0).", __func__);
        goto failure;
    }
    ip_pkt = jbuf_to_d(pkt_buf, struct ip *);
    
    // fix up the length to always be reassembler_mtu
    // max payload len in a fragment has to be divisible by BYTES_PER_FRAG_BLOCK
    new_len = reassembler_mtu - (ip_pkt->ip_hl * sizeof(uint32_t));
    offset_increment = new_len / BYTES_PER_FRAG_BLOCK; // max PL frag blocks
    new_len = offset_increment * BYTES_PER_FRAG_BLOCK; // max PL len in bytes
    mtu = new_len + (ip_pkt->ip_hl * sizeof(uint32_t));
    
    mtu = htons(mtu);
    checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
        (unsigned char *)&ip_pkt->ip_len, sizeof(uint16_t),
        (unsigned char *)&mtu, sizeof(uint16_t));
    
    ip_pkt->ip_len = mtu;
    mtu = ntohs(mtu);
    
    while(1) {
        
        len = jbuf_total_len(pkt_buf) + jbuf_total_len(fe->jb);
        if(len < mtu) {
            // safe to glue the 2 together; don't send it yet though
            jbuf_cat(pkt_buf, fe->jb);

        } else if(len == mtu) {
            
            // safe to glue the 2 together; send it now
            jbuf_cat(pkt_buf, fe->jb);

            if(pullup_bytes(&pkt_buf, sizeof(struct ip))) {
                DLOG(LOG_ERR, "%s: Not enough bytes to form an IP header "
                        "because a pullup failed (1).", __func__);
                goto failure;
            }
            ip_pkt = jbuf_to_d(pkt_buf, struct ip *);
            
            if(!TAILQ_EMPTY(&entry->flist)) { // if there's more to send...
                // save IP header in another jbuf for next fragment to send

                ip_hdr = jbuf_copychain(pkt_buf, 0, ip_pkt->ip_hl * 
                        sizeof(uint32_t));
                if(ip_hdr) {
                    DLOG(LOG_ERR, "%s: Failed to copy IP header from packet"
                            " into a new buffer (1).", __func__);
                    goto failure;
                }
                
                ip_pkt->ip_off = htons(cur_frag_off);
                // ip_pkt->ip_off |= IP_MF should already be set
                
                cur_frag_off += offset_increment; // keep track of where we are
                
                checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
                    (unsigned char *)&orig_ip_offset, sizeof(uint16_t),
                    (unsigned char *)&ip_pkt->ip_off, sizeof(uint16_t));
                
                // send it
                send_packet(pkt_buf, handle);
                pkt_buf = ip_hdr;
                
            } else { // there's nothing left to send after this
                // send it
                
                ip_pkt->ip_off = htons(cur_frag_off);
                ip_pkt->ip_off &= ~IP_MF; // unset MF
                
                checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
                    (unsigned char *)&orig_ip_offset, sizeof(uint16_t),
                    (unsigned char *)&ip_pkt->ip_off, sizeof(uint16_t));
                
                send_packet(pkt_buf, handle);
            }
            
        } else { // len > mtu
            // just add some of fe->jb to pkt_buf and send it
            
            len = mtu - jbuf_total_len(pkt_buf); // room left
            
            tmp = jbuf_split(fe->jb, len);
            if(tmp == NULL) {
                DLOG(LOG_ERR, "%s: Error splitting jbuf", __func__);
                jbuf_free(fe->jb);
                goto failure;
            }
            
            jbuf_cat(pkt_buf, fe->jb);
            
            // deal with remainder of the fragment that we split
            
            // save IP header in another jbuf for next fragment to send
            if(pullup_bytes(&pkt_buf, sizeof(struct ip))) {
                DLOG(LOG_ERR, "%s: Not enough bytes to form an IP header "
                        "because a pullup failed (2).", __func__);
                goto failure;
            }
            ip_pkt = jbuf_to_d(pkt_buf, struct ip *);
            ip_hdr = jbuf_copychain(pkt_buf, 0, ip_pkt->ip_hl * 
                    sizeof(uint32_t));
            if(ip_hdr) {
                DLOG(LOG_ERR, "%s: Failed to copy IP header from packet into a "
                        "new buffer (2).", __func__);
                goto failure;
            }
            
            ip_pkt->ip_off = htons(cur_frag_off);
            // ip_pkt->ip_off |= IP_MF should already be set
            
            cur_frag_off += offset_increment; // keep track of where we are
            
            checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
                (unsigned char *)&orig_ip_offset, sizeof(uint16_t),
                (unsigned char *)&ip_pkt->ip_off, sizeof(uint16_t));
            
            // send it
            send_packet(pkt_buf, handle);
            pkt_buf = ip_hdr;
            
            // fix fe->jb to point to the remainder after the split
            fe->jb = tmp;
            
            continue; // continue with fe because there's some more left in it
        }
        
        // we can free fe and get the next one
        
        msp_objcache_free(frag_handle, fe, cpu, obj_cache_id);
        
        fe = TAILQ_FIRST(&entry->flist); // go to next fragment in list
        if(!fe && pkt_buf) { // list is empty & still something in the buffer
            // send what we have since it is all that is left
            
            // fix up the length to always be what ever was left
            new_len = jbuf_total_len(pkt_buf);
            
            DLOG(LOG_INFO, "%s: Sending reassembled packet of length %d",
                    __func__, new_len);
            
            new_len = htons(new_len);
            
            checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
                (unsigned char *)&ip_pkt->ip_len, sizeof(uint16_t),
                (unsigned char *)&new_len, sizeof(uint16_t));
            
            ip_pkt->ip_len = new_len;
            
            // fix up the offset
            ip_pkt->ip_off = htons(cur_frag_off);
            ip_pkt->ip_off &= ~IP_MF; // unset MF
            
            checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
                (unsigned char *)&orig_ip_offset, sizeof(uint16_t),
                (unsigned char *)&ip_pkt->ip_off, sizeof(uint16_t));
            
            send_packet(pkt_buf, handle);
            
            break;
        } else {
            TAILQ_REMOVE(&entry->flist, fe, entries);
        }
    }

    return;
    
failure:

    if(pkt_buf) {
        jbuf_free(pkt_buf);
    }
    msp_objcache_free(frag_handle, fe, cpu, obj_cache_id);
    
    while((fe = TAILQ_FIRST(&entry->flist)) != NULL) {
        TAILQ_REMOVE(&entry->flist, fe, entries);
        msp_objcache_free(frag_handle, fe, cpu, obj_cache_id);
    }
}


/**
 * Process an IP fragment and do reassembly as necessary. The input packet must
 * be a fragment. The more fragment bit or the fragment offset must be set.
 * If the packet overlaps with something already seen, we keep the older data. 
 * Frees the input jbuf. Sends the reassembled packet according to the 
 * send_fragment_list function on reassembly completion.
 *
 * @param[in] pkt_buf
 *      The current jbuf that we received; will be sent or dropped in here
 *
 * @param[in] cpu
 *      The current CPU; used in objcache operations
 *      
 * @param[in] handle
 *      The data handle for the current dCPU/FIFO; used to send packet
 *
 * @return SUCCESS reassembly succeeded; EFAIL failure occured with a log msg
 */
static status_t
process_fragment(struct jbuf * pkt_buf,
                 const int cpu,
                 const msp_data_handle_t const * handle)
{
    struct jbuf * tmp;
    struct ip * ip_pkt = jbuf_to_d(pkt_buf, struct ip *);
    uint32_t hash;
    hash_bucket_t * bucket;
    table_entry_t * entry;
    struct timeval curtime;
    fragment_entry_t * fe, * fe_tmp, * next;
    uint8_t hdr_len = ip_pkt->ip_hl * sizeof(uint32_t);
    uint16_t payload_len = (ntohs(ip_pkt->ip_len) - hdr_len);
    boolean more_fragments = (ip_pkt->ip_off & IP_MF) ? true : false;
    uint16_t ip_id = ip_pkt->ip_id;
    uint16_t offset = ntohs(ip_pkt->ip_off & IP_OFFMASK);
    uint16_t payload_end = 0, old_end = 0;
    uint16_t overlap_bytes = 0;
    boolean more_holes = false, fragment_inserted = false;
    
    // get hash of the just the dest & src addresses + IP protocol + IP ID:
    //   xor l3 hash with id and trim to hash output width
    hash = (ip_id ^ pkt_buf->jb_l3_hash) & HASH_MASK;

    // use hash to lookup a hash bucket and find the matching entry
    bucket = &flows_table->hash_bucket[hash];

    // Get the bucket lock
    INSIST_ERR(msp_spinlock_lock(&bucket->bucket_lock) == MSP_OK);

    entry = TAILQ_FIRST(&bucket->bucket_entries);

    while(entry != NULL) { // not likely many entries per bucket
        if(entry->daddr == ip_pkt->ip_dst.s_addr &&
           entry->saddr == ip_pkt->ip_src.s_addr &&
           entry->protocol == ip_pkt->ip_p &&
           entry->frag_group == ip_pkt->ip_id) {

            break; // match
        }
        entry = TAILQ_NEXT(entry, entries);
    }

    if(entry == NULL) {
        // if there's no matching entry, create one
        // we haven't seen a fragment yet (in flow)

        entry = msp_objcache_alloc(entry_handle, cpu, obj_cache_id);
        if(entry == NULL) {
            // Release the bucket lock
            INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
            jbuf_free(pkt_buf);
            DLOG(LOG_ERR, "%s: Failed to allocate object cache for an entry.",
                    __func__);
            return EFAIL;
        }

        // init and grab lock
        msp_spinlock_init(&entry->lock);
        INSIST_ERR(msp_spinlock_lock(&entry->lock) == MSP_OK);

        TAILQ_INSERT_HEAD(&bucket->bucket_entries, entry, entries);

        // build key 
        entry->daddr = ip_pkt->ip_dst.s_addr;
        entry->saddr = ip_pkt->ip_src.s_addr;
        entry->protocol = ip_pkt->ip_p;
        entry->frag_group = ip_id;
        
        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
        
        // init the rest of entry
        
        entry->total_len = 0;
        entry->free = 0;
        TAILQ_INIT(&entry->flist);
        
        if(!lw_getsystimes(&curtime, NULL)) {
            entry->age_ts = curtime.tv_sec; 
        } else {
            DLOG(LOG_EMERG, "%s: Cannot get a timestamp", __func__);
        }

        DLOG(LOG_INFO, "%s: Created entry for id %d", __func__, ip_id);
    } else {
        // else there's a matching entry, so use it
        
        // Get the flow lock
        INSIST_ERR(msp_spinlock_lock(&entry->lock) == MSP_OK);

        // Release the bucket lock
        INSIST_ERR(msp_spinlock_unlock(&bucket->bucket_lock) == MSP_OK);
    }

    // put this jbuf in the fragment list
    fe = msp_objcache_alloc(frag_handle, cpu, obj_cache_id);
    if(fe == NULL) {
        // Release the entry lock
        INSIST_ERR(msp_spinlock_unlock(&entry->lock) == MSP_OK);
        jbuf_free(pkt_buf);
        DLOG(LOG_ERR, "%s: Failed to allocate object cache for a fragment"
                " entry.", __func__);
        return EFAIL;
    }
    
    fe->jb = jbuf_dup(pkt_buf);
    jbuf_free(pkt_buf);
    
    if(fe->jb == NULL) {
        INSIST_ERR(msp_spinlock_unlock(&entry->lock) == MSP_OK);
        msp_objcache_free(frag_handle, fe, cpu, obj_cache_id);
        DLOG(LOG_ERR, "%s: Error dupping jbuf (1)", __func__);
        return EFAIL;
    }
    
    // Chop off IP header if it is not the first fragment
    if(offset != 0) {
        tmp = jbuf_split(fe->jb, hdr_len);
        if(tmp != NULL) {
            jbuf_free(fe->jb);
            fe->jb = tmp;
        } else {
            INSIST_ERR(msp_spinlock_unlock(&entry->lock) == MSP_OK);
            jbuf_free(fe->jb);
            msp_objcache_free(frag_handle, fe, cpu, obj_cache_id);
            DLOG(LOG_ERR, "%s: Error splitting jbuf (1)", __func__);
            return EFAIL;
        }
        
        payload_end = offset + (int)(ceil( // round up for last fragment case
                (float)payload_len / BYTES_PER_FRAG_BLOCK));
        
    } else {
        payload_end = offset + (payload_len / BYTES_PER_FRAG_BLOCK);

        // this is the first frag..so save the IP header
        // add the IP hdr len back to payload_len because we're keeping it
        payload_len += hdr_len;
    }
    
    // find where to insert this fragment in the fragment list
    
    fe_tmp = TAILQ_FIRST(&entry->flist);
    
    /*
     * Debug
     *
    DLOG(LOG_INFO, "%s: id: %d, hl:%d, pl:%d, of:%d%s, pe:%d", __func__, ip_id,
            hdr_len, payload_len, offset, more_fragments?"+":"", payload_end);
     */
    
    if(!fe_tmp) { // list is empty ... easy case
        
        // now put the holes in
        if(offset == 0) { // It's the first fragment
            fe->is_leading_hole = false;
        } else {
            fe->is_leading_hole = true;
            fe->leading_hole.start = 0;
            fe->leading_hole.end = offset;
        }
            
        if(!more_fragments) { // It's the last fragment
            fe->is_trailing_hole = false;
        } else {
            fe->is_trailing_hole = true;
            if(offset == 0)
                fe->trailing_hole.start = offset +  
                    ((payload_len - hdr_len) / BYTES_PER_FRAG_BLOCK);
            else 
                fe->trailing_hole.start = offset +  
                    (payload_len / BYTES_PER_FRAG_BLOCK);
            fe->trailing_hole.end = USHRT_MAX;
        }
        
        TAILQ_INSERT_HEAD(&entry->flist, fe, entries);
        more_holes = true;
    
    } else if(fe_tmp->is_leading_hole) { // hole at front of list
        
        // handle overlap first (hopefully rare)
        if(offset < fe_tmp->leading_hole.end &&
                fe_tmp->leading_hole.end < payload_end) {
            
            // some of it is before fe_tmp, but not all of it
            // trim the part we already have from the end of the fragment
            
            overlap_bytes = (payload_end - fe_tmp->leading_hole.end) *
                    BYTES_PER_FRAG_BLOCK;
            
            // recalculate desired payload
            payload_len -= overlap_bytes;
            payload_end = fe_tmp->leading_hole.end;
            
            tmp = jbuf_split(fe->jb, payload_len); // trim
            
            DLOG(LOG_INFO, "%s: Trimmed %d trailing overlap bytes (1)",
                    __func__, overlap_bytes);
            
            if(tmp != NULL) {
                jbuf_free(tmp); // free overlap
            } else {
                INSIST_ERR(msp_spinlock_unlock(&entry->lock) == MSP_OK);
                jbuf_free(fe->jb);
                msp_objcache_free(frag_handle, fe, cpu, obj_cache_id);
                DLOG(LOG_ERR, "%s: Error splitting jbuf (2)", __func__);
                return EFAIL;
            }
            
            // now payload_end == fe_tmp->leading_hole.end
            // we will process fe in the next if statement
        }
            
        if(fe_tmp->leading_hole.end >= payload_end) { // fe is before fe_tmp
            
            TAILQ_INSERT_HEAD(&entry->flist, fe, entries);
            fragment_inserted = true;
            
            if(offset == 0) {
                fe->is_leading_hole = false;
            } else {
                fe->is_leading_hole = true;
                fe->leading_hole.start = 0;
                fe->leading_hole.end = offset;
                more_holes = true;
            }
            
            if(payload_end == fe_tmp->leading_hole.end) {
                // fe comes right before fe_tmp
                fe_tmp->is_leading_hole = false;
                fe->is_trailing_hole = false;
            } else {
                // fe comes before fe_tmp but with a hole in between
                fe->is_trailing_hole = true;
                fe->trailing_hole.start = payload_end;
                fe_tmp->leading_hole.start = payload_end;
                fe->trailing_hole.end = fe_tmp->leading_hole.end;
                more_holes = true;
            }
            fe_tmp = fe;
        }
    }
    
    if(!more_holes) {
        
        while(!fragment_inserted && fe_tmp != NULL) {

            if(!fe_tmp->is_trailing_hole) {
                // fe is entirely past fe_tmp and not next to it
                fe_tmp = TAILQ_NEXT(fe_tmp, entries);
                continue;
            }
            
            // else there is a trailing hole
            
            if(offset >= fe_tmp->trailing_hole.end) {
                // it doesn't fit anywhere in this hole
                fe_tmp = TAILQ_NEXT(fe_tmp, entries);
                more_holes = true; // indicate we saw a hole
                continue;
            }
            
            if(payload_end <= fe_tmp->trailing_hole.start) {
                // fe fragment is useless to us, we already have the content
                more_holes = true; // indicate we saw a hole
                break;
            }
            
            // else: it fits somewhere in this hole...find where
            // and then break out of the loop
            
            // handle leading overlap first (hopefully rare)
            // beginning of fe overlaps with some of fe_tmp
            if(offset < fe_tmp->trailing_hole.start) {
                
                // trim the part we already have from the start

                overlap_bytes = (fe_tmp->trailing_hole.start - offset) *
                        BYTES_PER_FRAG_BLOCK;
                
                // recalculate desired payload and offset
                offset = fe_tmp->trailing_hole.start;
                payload_len -= overlap_bytes;
                
                tmp = jbuf_split(fe->jb, overlap_bytes); // trim
                
                DLOG(LOG_INFO, "%s: Trimmed %d leading overlap bytes",
                        __func__, overlap_bytes);
                
                if(tmp != NULL) {
                    jbuf_free(fe->jb); // free overlap
                    fe->jb = tmp;
                } else {
                    INSIST_ERR(msp_spinlock_unlock(&entry->lock) == MSP_OK);
                    jbuf_free(fe->jb);
                    msp_objcache_free(frag_handle, fe, cpu, obj_cache_id);
                    DLOG(LOG_ERR, "%s: Error splitting jbuf (3)", __func__);
                    return EFAIL;
                }
                
                // now offset == fe_tmp->leading_hole.start
            }
            
            // fe comes somewhere after fe_tmp
            TAILQ_INSERT_AFTER(&entry->flist, fe_tmp, fe, entries);
            fragment_inserted = true;
            
            old_end = fe_tmp->trailing_hole.end;
            
            if(fe_tmp->trailing_hole.start == offset) {
                // fe comes right after fe_tmp
                fe_tmp->is_trailing_hole = false;
                fe->is_leading_hole = false;
            } else {
                fe->is_leading_hole = true;
                fe->leading_hole.start= fe_tmp->trailing_hole.start;
                fe->leading_hole.end = offset;
                fe_tmp->trailing_hole.end = offset;
                more_holes = true;
            }
            
            if(!more_fragments) { // it is the last fragment
                fe->is_trailing_hole = false;
                
                // in case there is anything after fe, we can get rid of it
                fe_tmp = TAILQ_NEXT(fe, entries);
                while(fe_tmp != NULL) {
                    next = TAILQ_NEXT(fe_tmp, entries);
                    jbuf_free(fe_tmp->jb);
                    TAILQ_REMOVE(&entry->flist, fe_tmp, entries);
                    msp_objcache_free(frag_handle, fe_tmp, cpu, obj_cache_id);
                    fe_tmp = next;
                    DLOG(LOG_INFO, "%s: Discarded a useless fragment from end "
                            "of chain", __func__);
                }
            } else {
                
                // handle trailing overlap with next frag (hopefully rare)
                if(payload_end > old_end) {
                    
                    // trim the part we already have from the end
                    
                    overlap_bytes = (payload_end - old_end) *
                            BYTES_PER_FRAG_BLOCK;
                    
                    // recalculate desired payload
                    payload_len -= overlap_bytes;
                    payload_end = old_end;
                    
                    tmp = jbuf_split(fe->jb, payload_len); // trim
                    
                    DLOG(LOG_INFO, "%s: Trimmed %d trailing overlap bytes (2)",
                            __func__, overlap_bytes);
                    
                    if(tmp != NULL) {
                        jbuf_free(tmp); // free overlap
                    } else {
                        TAILQ_REMOVE(&entry->flist, fe, entries);
                        
                        // restore fe_tmp back to how it was
                        next = TAILQ_NEXT(fe_tmp, entries);
                        fe_tmp->is_trailing_hole = true;
                        if(next)
                            fe_tmp->trailing_hole.end = next->leading_hole.end;
                        else
                            fe_tmp->trailing_hole.end = USHRT_MAX;
                        
                        INSIST_ERR(msp_spinlock_unlock(&entry->lock) == MSP_OK);
                        jbuf_free(fe->jb);
                        msp_objcache_free(frag_handle, fe, cpu, obj_cache_id);

                        DLOG(LOG_ERR, "%s: Error splitting jbuf (4)", __func__);
                        return EFAIL;
                    }
                    
                    // now payload_end == fe_tmp->trailing_hole.end
                    // we will process fe in the next if statement
                }
                
                // calculate the trailing hole for fe and then
                // the leading hole for fe->next 
                fe->trailing_hole.start = payload_end;
                fe->trailing_hole.end = old_end;
                
                fe_tmp = TAILQ_NEXT(fe, entries);
                
                if(fe->trailing_hole.start == fe->trailing_hole.end) {
                    // filled the entire hole
                    fe->is_trailing_hole = false;
                    // insist on the following b/c trailing_hole.end
                    // would be infinity otherwise
                    INSIST_ERR(fe_tmp != NULL);
                    fe_tmp->is_leading_hole = false;
                    // will break below and scan the rest faster for holes
                } else {
                    // filled some of the hole
                    fe->is_trailing_hole = true;
                    if(fe_tmp)
                        fe_tmp->leading_hole.start = fe->trailing_hole.start;
                    more_holes = true;
                    fe_tmp = NULL; // no point of scanning more
                }
            }
            break;
        }
        
        // scan anything else quicker
        while(!more_holes && fe_tmp != NULL) {
            if(fe_tmp->is_trailing_hole) {
                more_holes = true;
                break;
            }
            fe_tmp = TAILQ_NEXT(fe_tmp, entries);
        }
        
        if(!more_holes) { // Done reassembly
            
            entry->total_len += payload_len;
            
            // Send out what we have reassembled
            
            DLOG(LOG_INFO, "%s: Sending a reassembled fragment", __func__);
            
            send_fragment_list(entry, cpu, handle);
            
            entry->free = 1; // flag telling the ager to clean this up
            
            // Release the entry lock
            INSIST_ERR(msp_spinlock_unlock(&entry->lock) == MSP_OK);
            
            return SUCCESS;
        }
        
        if(!fragment_inserted) {
            // this packet was a complete overlap of what we already had 
            jbuf_free(fe->jb);
            msp_objcache_free(frag_handle, fe, cpu, obj_cache_id);
            DLOG(LOG_INFO, "%s: Discarded a useless fragment "
                    "(complete overlap)", __func__);
        }
    }
    
    /*
     * Print list (Debug):
     * 
    
    fe_tmp = TAILQ_FIRST(&entry->flist);
    while(fe_tmp != NULL) {
        DLOG(LOG_INFO, "len: %d", jbuf_total_len(fe_tmp->jb));
        if(fe_tmp->is_leading_hole) {
            DLOG(LOG_INFO, "leading: %d - %d", fe_tmp->leading_hole.start, fe_tmp->leading_hole.end);
        }
        if(fe_tmp->is_trailing_hole) {
            DLOG(LOG_INFO, "trailing: %d - %d", fe_tmp->trailing_hole.start, fe_tmp->trailing_hole.end);
        }
        fe_tmp = TAILQ_NEXT(fe_tmp, entries);
    }
    DLOG(LOG_INFO, "Done printing fragment list");
    
    */

    if(!lw_getsystimes(&curtime, NULL)) {
        entry->age_ts = curtime.tv_sec; 
    } else {
        DLOG(LOG_EMERG, "%s: Cannot get a timestamp", __func__);
    }

    entry->total_len += payload_len;
    
    // Release the entry lock
    INSIST_ERR(msp_spinlock_unlock(&entry->lock) == MSP_OK);

    return SUCCESS;
}


/**
 * Entry point for packet processing threads
 * (function passed to msp_data_create_loop_on_cpu)
 *
 * @param[in] params
 *     dataloop parameters with user data, loop identifier, and loop number
 */
static void *
reassembler_process_packet(msp_dataloop_args_t * params)
{
    struct jbuf * pkt_buf;
    struct ip * ip_pkt;
    int type, cpu;
    sigset_t sig_mask;

    // Block SIGTERM to this thread/main thread will handle otherwise we inherit
    // this behaviour in our threads sigmask and the signal might come here
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sig_mask, NULL);

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
            DLOG(LOG_WARNING, "%s: Message wasn't a packet...dropping",
                __func__);
            jbuf_free(pkt_buf);
            continue;
        }

        if(pullup_bytes(&pkt_buf, sizeof(struct ip))) {

            DLOG(LOG_ERR, "%s: Dropped a packet because there's not enough "
                "bytes to form an IP header and a pullup failed.", __func__);

            jbuf_free(pkt_buf);
            continue;
        }

        // Get IP header
        ip_pkt = jbuf_to_d(pkt_buf, struct ip *);

        if(ip_pkt->ip_off & htons(IP_MF | IP_OFFMASK)) { // It's a fragment

            process_fragment(pkt_buf, cpu, &params->dhandle);

        } else {

            send_packet(pkt_buf, &params->dhandle);
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
    int i, cpu, rc;
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
    
    /*
    if(msp_trace_init(shm_handle)) { // need this for msp_log/DLOG
        LOG(LOG_ERR, "%s: Could not initialize shared memory for dCPU logging",
                __func__);
        return EFAIL;
    }
    */

    // create object cache allocator for the flow look up table
    ocp.oc_shm = shm_handle;
    ocp.oc_size  = sizeof(hashtable_t);
    strncpy(ocp.oc_name, HASHTABLE_NAME, OC_NAME_LEN);

    if(msp_objcache_create(&ocp) != MSP_OK) {
        LOG(LOG_ERR, "%s: Object-cache allocator initialization failed (table)",
                __func__);
        return EFAIL;
    }

    table_handle = ocp.oc; // get handle

    // create object cache allocator for the flow look up table entries
    ocp.oc_shm = shmp.shm;
    ocp.oc_size  = sizeof(table_entry_t);
    strncpy(ocp.oc_name, TABLE_ENTRY_NAME, OC_NAME_LEN);

    if (msp_objcache_create(&ocp) != MSP_OK) {
        LOG(LOG_ERR, "%s: Object-cache allocator initialization failed (entry)",
                __func__);
        return EFAIL;
    }

    entry_handle = ocp.oc; // get handle
    
    // create object cache allocator for the fragment entries
    ocp.oc_shm = shmp.shm;
    ocp.oc_size  = sizeof(fragment_entry_t);
    strncpy(ocp.oc_name, FRAGMENT_ENTRY_NAME, OC_NAME_LEN);

    if (msp_objcache_create(&ocp) != MSP_OK) {
        LOG(LOG_ERR, "%s: Object-cache allocator initialization failed (entry)",
                __func__);
        return EFAIL;
    }

    frag_handle = ocp.oc; // get handle

    // allocate flows_table in OC:

    flows_table = msp_objcache_alloc(table_handle, msp_get_current_cpu(),
            obj_cache_id);
    
    if(flows_table == NULL) {
        LOG(LOG_ERR, "%s: Failed to allocate object cache for flows table ",
                __func__);
        return EFAIL;
    }

    for(i = 0; i < FLOW_BUCKET_COUNT; ++i) {
        INSIST_ERR(msp_spinlock_init(&flows_table->hash_bucket[i].bucket_lock)
                == MSP_OK);
        TAILQ_INIT(&flows_table->hash_bucket[i].bucket_entries);
    }

    LOG(LOG_INFO, "%s: Starting packet loops...", __func__);

    bzero(&params, sizeof(msp_dataloop_params_t));
    bzero(&result, sizeof(msp_dataloop_result_t));

    loops_running = 0;
    do_shutdown = 0;
    
    // go through the available data CPUs and count them
    cpu = MSP_NEXT_NONE;
    i = 0; // count data CPUs
    while((cpu = msp_env_get_next_data_cpu(cpu)) != MSP_NEXT_END) {
        ++i;
    }
    
    if(i == 0) {
        LOG(LOG_ERR, "%s: No available data CPUs", __func__);
        return EFAIL;
    }
    
    cpu = MSP_NEXT_NONE;
    if((cpu = msp_env_get_next_data_cpu(cpu)) != MSP_NEXT_END) {
        
        // We actually bind this main thread of the process to a data CPU
        // This means we use up a dCPU without a FIFO and packet loop running 
        // on it, but on this dCPU, we guarantee that our thread runs in
        // real-time, that is, without preemption
        
        if(msp_process_bind(cpu)) {
            // This is bad because we need to acquire spinlocks
            LOG(LOG_ERR, "%s: Failed to bind the main thread of the process "
                    "to dCPU %d.", __func__, cpu);
            return EFAIL;
        } else {
             LOG(LOG_INFO, "%s: Bound the main thread of the process to dCPU %d",
                     __func__, cpu);
        }
    }
    
    
    // MIN_FIFO_DEPTH (1023) is the internal default depth FIFO depth
    // Here we scale the depth depending on the number of data loop...
    
    // If we have 21 dCPUs (max), then we would leave it at 1023, otherwise add
    // more space in the FIFOs because we will have less of them
    
    --i; // we used the first dCPU without a packet loop (above)
    
    params.dl_fifo_depth = (int)(MIN_FIFO_DEPTH * (21.0f / i));
    
    // create data loops on the remaining dCPUs
    while((cpu = msp_env_get_next_data_cpu(cpu)) != MSP_NEXT_END) {

        rc = msp_data_create_loop_on_cpu(cpu, reassembler_process_packet,
                &params, &result);
        
        if (rc != MSP_OK) {
            LOG(LOG_ERR, "%s: Could not start data loop on dCPU %d (err: %d).",
                    __func__, cpu, rc);
        }
    }
    
    LOG(LOG_INFO, "%s: Started %d packet loops with FIFO depth of %d",
            __func__, i, params.dl_fifo_depth);
    
    // start ager on this ctrl thread... will run in real-time

    if(evSetTimer(ctx, aging_cleanup, NULL,
            evAddTime(evNowTime(), evConsTime(ENTRY_AGE_CHECK_INTERVAL, 0)),
            evConsTime(ENTRY_AGE_CHECK_INTERVAL, 0), &aging_timer)) {

        LOG(LOG_EMERG, "%s: Failed to initialize a timer to periodically "
            "check age of flow entries (Error: %m)", __func__);
        return EFAIL;
    }

    return SUCCESS;
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

    while(loops_running > 0) ; // note the spinning while waiting

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

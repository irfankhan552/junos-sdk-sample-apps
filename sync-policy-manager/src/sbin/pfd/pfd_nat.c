/*
 * $Id: pfd_nat.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/** 
 * @file pfd_nat.c
 * @brief Contains the implementation of NAT for the PFD
 * 
 * Contains the implementation of NAT for the PFD, including, NAT table, 
 * table monitoring thread, and NAT/reverse-NAT header-rewriting functions for 
 * packets in transit.
 * 
 */

#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>
#include <jnx/vrf_util_pub.h>
#include <jnx/rt_shared_pub.h>
#include <jnx/jnx_types.h>
#include <jnx/mpsdk.h>
#include <sys/jnx/jbuf.h>
#include <sys/socket.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <hashtable.h>
#include "pfd_logging.h"
#include "pfd_config.h"
#include "pfd_nat.h"

/*** Constants ***/

/**
 * A NAT entry expires after this many seconds without use
 */
#define NAT_ENTRY_LIFETIME 60

/**
 * The number of NAT entries in the table (the size of the table is fixed) and 
 * the max number of local port use for NAT 
 */
#define NAT_MAX_ENTRIES 1000

/**
 * Start of local port range. We use these NAT_MAX_ENTRIES ports starting at 
 * this value to the re-write the source IP to the PFD's address and the source
 * port to one of the available ports in the range. 
 */
#define NAT_LPORT_RANGE_MIN 50000 

/**
 * End of local port range. See NAT_LPORT_RANGE_MIN. (do not edit formula)
 */
#define NAT_LPORT_RANGE_MAX (NAT_LPORT_RANGE_MIN + NAT_MAX_ENTRIES - 1)

/**
 * The port that the CPD's public HTTP server runs on 
 */
#define CPD_HTTP_PORT 80

/*** Data Structures ***/


/**
 * An entry in our NAT table. Addresses & Ports stay in network byte order
 */
typedef struct {
    in_addr_t       ipsrc;    ///< original source address
    in_addr_t       ipdst;    ///< original destination address
    uint16_t        srcport;  ///< original source port
    uint16_t        dstport;  ///< original destination port 
    time_t          exp_time; ///< time of expiry for this entry
    pthread_mutex_t lock;     ///< lock indicating that this entry is in use
} nat_table_t;

/**
 * NAT table lookup/indexed by local port number used. Lookups are performed
 * faster here when doing reverse NAT. 
 */
static nat_table_t nat_table[NAT_MAX_ENTRIES];

/**
 * The actual forward NAT lookup table which maps src & dst IP addr and src port
 * number to a local port number
 */
static struct hashtable * lookup_table = NULL;
static uint16_t next_lport_num; ///< next available local src port number
static pthread_mutex_t lookup_table_lock; ///<lookup_table & next_lport_num lock

/*
 * Note: next_lport_num is one entry past the last free index found in nat_table
 * It is only ever accessed when holding the lookup_table_lock so we don't need
 * yet another lock for it too.
 */

static uint16_t cpd_port; ///< Port of the CPD interface

/**
 * The key for the hashtable. All values stay in network byte order
 */
typedef struct key_s {
    in_addr_t       ipsrc;    ///< original source address
    in_addr_t       ipdst;    ///< original destination address
    uint16_t        srcport;  ///< original source port
} hash_key_t;


/*** STATIC/INTERNAL Functions ***/


// Define functions ht_insert, ht_get, and ht_remove

/**
 * We use insert_entry to insert a (key,value) safely into the hashtable
 */
static DEFINE_HASHTABLE_INSERT(insert_entry, hash_key_t, uint16_t);

/**
 * We use get_entry to get a (key,value) safely from the hashtable
 */
static DEFINE_HASHTABLE_SEARCH(get_entry, hash_key_t, uint16_t);

/**
 * We use remove_entry to remove a (key,value) safely from the hashtable
 */
static DEFINE_HASHTABLE_REMOVE(remove_entry, hash_key_t, uint16_t);


/**
 * This function will adjust a checksum.
 * It is taken directly from the NAT RFC 3022.
 * 
 * @param[in,out] chksum
 *      Checksum
 * 
 * @param[out] optr
 *      Pointer to old data to scan
 * 
 * @param[out] olen
 *      Length of old data to scan
 * 
 * @param[out] nptr
 *      Pointer to old data to scan
 * 
 * @param[out] nlen
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
 * Returns the hash value of a key:
 * 
 * @param[in] key
 *    The key to be typecasted to (hash_key_t *)
 * 
 * @return a hash of the key (key's contents)
 */
static unsigned int
hashFromKey(void * key)
{
    static unsigned int key_length = (2 * sizeof(in_addr_t)) + sizeof(uint16_t);
    unsigned int hash, i;
    uint8_t * k = (uint8_t *)key;

    for (i = 0, hash = sizeof(hash_key_t); i < key_length; ++i) {
        hash = (hash<<4) ^ (hash>>28) ^ (k[i]);
    }
    
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
   
    return hash;
}


/**
 * Compare two keys:
 * 
 * @param[in] k1
 *    First key
 * 
 * @param[in] k2
 *    Second key
 * 
 * @return 1 is keys are equal, 0 otherwise
 */
static int
equalKeys(void * k1, void * k2)
{
  return (memcmp(k1, k2, sizeof(hash_key_t)) == 0);
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the NAT table, variables, and all the mutexes used in this module
 */
void 
init_nat(void)
{
    int i;
    
    bzero(&nat_table, sizeof(nat_table));
    
    for(i = 0; i < NAT_MAX_ENTRIES; ++i) {
        pthread_mutex_init(&nat_table[i].lock, NULL);
    }
    
    next_lport_num = 0;
    
    lookup_table = create_hashtable(2*NAT_MAX_ENTRIES, hashFromKey, equalKeys);
    INSIST_ERR(lookup_table != NULL);
    
    pthread_mutex_init(&lookup_table_lock, NULL);
    
    cpd_port = htons(CPD_HTTP_PORT);
}


/**
 * Destroy the NAT table and all the mutexes used in this module
 */
void 
terminate_nat(void)
{
    int i;
    
    pthread_mutex_destroy(&lookup_table_lock);
    
    for(i = 0; i < NAT_MAX_ENTRIES; ++i) {
        pthread_mutex_destroy(&nat_table[i].lock);
    }
    
    if(lookup_table) {
        hashtable_destroy(lookup_table, TRUE); // TRUE to free values
        lookup_table = NULL;
    }
}


/**
 * NAT the packet from the original source to the CPD making the PFD the new 
 * sender.
 * 
 * @param[in] ip_pkt
 *      the packet to nat
 * 
 * @param[in] addresses
 *      PFD and CPD addresses
 * 
 * @return
 *      TRUE if successfully NAT'd
 *      FALSE if an entry doesn't exist and we couldn't create one (full table)
 */
boolean
nat_packet(struct ip * ip_pkt, address_bundle_t * addresses)
{
    uint16_t * entry_index, start, port;
    hash_key_t * key, tmp_key;
    struct tcphdr * tcp_hdr = 
        (struct tcphdr *)((uint32_t *)ip_pkt + ip_pkt->ip_hl);
    int rc;
    time_t current_time;
    boolean found_free_port = FALSE;
    
    struct ports_s {
        uint16_t src_port;
        uint16_t dst_port;
    } port_bundle; // bundle ports to speed up checksum calc

    // Lookup into hashtable (lookup_table) with src IP, dst IP, and src port
    // Keep network byte order to speed this up (no need to switch) 
    
    bzero(&tmp_key, sizeof(tmp_key));
    tmp_key.ipdst = ip_pkt->ip_dst.s_addr;
    tmp_key.ipsrc = ip_pkt->ip_src.s_addr;
    tmp_key.srcport = tcp_hdr->th_sport;
    
    /*
     * We need to lock access to the lookup table until we are guaranteed to 
     * have a lock on the NAT table entry that it is associated with. Otherwise
     * we could lookup an entry and get the local port number, but then another
     * thread could take over the processing and find the same port number free 
     * (expired) and overwrite the entry.
     */
    LOCK_MUTEX(&lookup_table_lock);
    
    entry_index = get_entry(lookup_table, &tmp_key);
    
    if(entry_index == NULL) { // nothing found in lookup table
        current_time = get_current_time();
        start = port = next_lport_num;
        
        /*
         * We start searching for a free local port and spot in the nat_table,
         * then create a new lookup entry. We start looking at next_lport_num
         * which was set by the last thread that found a free spot/port.
         */
        
        do {
            if(nat_table[port].exp_time < current_time) {
                // the entry in this spot/for this local port # is expired

                rc = pthread_mutex_trylock(&nat_table[port].lock);
                
                if(rc == 0) { // we got the lock for this free spot/port
                    
                    // test an unlikely case (that it is no longer expired)
                    if(nat_table[port].exp_time > current_time) {
                        /*
                         * Somebody overwrote the expiry before we got the lock
                         * and after we checked that it was expired, so this
                         * entry is still in use. Keep searching ...
                         */
                        UNLOCK_MUTEX(&nat_table[port].lock);

                    } else { // We're safe to overwrite the entry in this spot
                        
                        /*
                         * Setup next_lport_num better for the next thread that
                         * needs to search for a free spot/port. Hopefully it
                         * will have "better" luck starting its search from here
                         * because (we assume) it is more likely that it is free
                         */
                        next_lport_num = port + 1;

                        // remove the entry in the hashtable for the entry that
                        // was previously in this spot, and insert a new one
    
                        key = (hash_key_t *)malloc(sizeof(hash_key_t));
                        INSIST_ERR(key != NULL);
                        bzero(key, sizeof(hash_key_t));
    
                        key->ipdst = tmp_key.ipdst; // note: tmp_key has values
                        key->ipsrc = tmp_key.ipsrc; // from the current packet
                        key->srcport = tmp_key.srcport;
                        
                        entry_index = (uint16_t *)malloc(sizeof(uint16_t));
                        INSIST_ERR(entry_index != NULL);
                        
                        *entry_index = port;
                        
                        insert_entry(lookup_table, key, entry_index);
    
                        tmp_key.ipdst = nat_table[port].ipdst;
                        tmp_key.ipsrc = nat_table[port].ipsrc;
                        tmp_key.srcport = nat_table[port].srcport;
                        
                        // if the entry was never used freshly init'd these will
                        // all be zero and this remove will fail, but it's ok
                        entry_index = remove_entry(lookup_table, &tmp_key);
                        
                        if(entry_index) {
                            free(entry_index);
                        }
                        
                        /*
                         * Because this is round-robin packet delivery we can 
                         * only release this lock here. If it was flow-based, 
                         * then there would be no potential race between two 
                         * thread doing a get_entry on the lookup_table with the
                         * same key.
                         * 
                         * If this was flow-based we could release the lock at 
                         * the top of this block i.e. when we know we have 
                         * safely locked nat_table[port].lock 
                         */
                        UNLOCK_MUTEX(&lookup_table_lock);
                        
    
                        // Setup the new NAT table entry
                        nat_table[port].ipdst = ip_pkt->ip_dst.s_addr;
                        nat_table[port].ipsrc = ip_pkt->ip_src.s_addr;
                        // Destination port should be HTTP/80
                        nat_table[port].dstport = tcp_hdr->th_dport;
                        nat_table[port].srcport = tcp_hdr->th_sport;
                        nat_table[port].exp_time = 
                            get_current_time() + NAT_ENTRY_LIFETIME;
                        
                        UNLOCK_MUTEX(&nat_table[port].lock);
    
                        found_free_port = TRUE;
                        
                        break;
                    }
                    
                } else if(rc != EBUSY) {
                    // this should never happen, so abort.
                    LOG(LOG_EMERG, "%s:%s: pthread_mutex_trylock failed with "
                        "EINVAL (Mutex is invalid for local port %d)",
                        __FILE__, __func__, port + NAT_LPORT_RANGE_MIN);
                }
                // if rc == EBUSY, then another thread beat us to it
            }

            // increment port being careful to wrap around range of local ports
            if(++port == NAT_MAX_ENTRIES) {
                port = 0;
            }

        } while(port != start);
        
        if(!found_free_port) {
            UNLOCK_MUTEX(&lookup_table_lock);
            return FALSE; // the NAT table is full
        }
    } else { // Entry exists, so use it
        port = *entry_index;
        
        LOCK_MUTEX(&nat_table[port].lock);
        UNLOCK_MUTEX(&lookup_table_lock);
            
        // refresh expiry time:
        nat_table[port].exp_time = get_current_time() + NAT_ENTRY_LIFETIME;
        
        UNLOCK_MUTEX(&nat_table[port].lock);
    }
    port += NAT_LPORT_RANGE_MIN; // shift the value into the local port range
    
    port = htons(port);
    
    port_bundle.src_port = port;
    port_bundle.dst_port = cpd_port;
    
    // adjust IP checksum taking IP addresses into account
    checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
        (unsigned char *)&ip_pkt->ip_src, sizeof(address_bundle_t),
        (unsigned char *)addresses, sizeof(address_bundle_t));
    
    // adjust TCP checksum taking IP addresses into account
    checksum_adjust((unsigned char *)&tcp_hdr->th_sum,
        (unsigned char *)&ip_pkt->ip_src, sizeof(address_bundle_t),
        (unsigned char *)addresses, sizeof(address_bundle_t));
    
    // adjust TCP checksum taking TCP ports into account
    checksum_adjust((unsigned char *)&tcp_hdr->th_sum,
        (unsigned char *)&tcp_hdr->th_sport, sizeof(struct ports_s),
        (unsigned char *)&port_bundle.src_port, sizeof(struct ports_s));
    
    ip_pkt->ip_dst.s_addr = addresses->cpd_addr;
    ip_pkt->ip_src.s_addr = addresses->pfd_addr;
    tcp_hdr->th_dport = cpd_port;
    tcp_hdr->th_sport = port;
    
    return TRUE;
}


/**
 * Reverse NAT the packet from the CPD to the original sender. (The PFD was the 
 * intermediate sender).
 * 
 * @param[in] ip_pkt
 *       the packet to reverse nat
 * 
 * @return
 *       TRUE if successfully NAT'd, FALSE if cannot find an entry for dst port
 */
boolean
reverse_nat_packet(struct ip * ip_pkt)
{
    uint16_t port; // local port
    struct tcphdr * tcp_hdr = 
        (struct tcphdr *)((uint32_t *)ip_pkt + ip_pkt->ip_hl);
    time_t current_time;
    
    port = ntohs(tcp_hdr->th_dport);
    
    if(port < NAT_LPORT_RANGE_MIN || port > NAT_LPORT_RANGE_MAX) {
        return FALSE;
    }
    
    port -= NAT_LPORT_RANGE_MIN; // shift into the range for nat_table array
    
    current_time = get_current_time();
    
    LOCK_MUTEX(&nat_table[port].lock);
    
    if(nat_table[port].exp_time < current_time) { // if entry is expired
        UNLOCK_MUTEX(&nat_table[port].lock);
        return FALSE;
    }
    
    // refresh expiry time
    nat_table[port].exp_time = current_time + NAT_ENTRY_LIFETIME;

    UNLOCK_MUTEX(&nat_table[port].lock);
    
    /*
     * It's safe to unlock before reading these values because the entry won't 
     * be modified for at least NAT_ENTRY_LIFETIME seconds (other than possibly
     * the expiry time
     */

    // adjust IP checksum taking IP addresses into account
    checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
        (unsigned char *)&ip_pkt->ip_src, sizeof(address_bundle_t),
        (unsigned char *)&nat_table[port].ipsrc, sizeof(address_bundle_t));
    
    // adjust TCP checksum taking IP addresses into account
    checksum_adjust((unsigned char *)&tcp_hdr->th_sum,
        (unsigned char *)&ip_pkt->ip_src, sizeof(address_bundle_t),
        (unsigned char *)&nat_table[port].ipsrc, sizeof(address_bundle_t));
    
    // adjust TCP checksum taking TCP ports into account
    checksum_adjust((unsigned char *)&tcp_hdr->th_sum,
        (unsigned char *)&tcp_hdr->th_sport, sizeof(u_short) * 2,
        (unsigned char *)&nat_table[port].srcport, sizeof(u_short) * 2);
    
    // rewrite IP addresses and ports
    
    ip_pkt->ip_dst.s_addr = nat_table[port].ipsrc;
    ip_pkt->ip_src.s_addr = nat_table[port].ipdst;
    tcp_hdr->th_dport = nat_table[port].srcport;
    tcp_hdr->th_sport = nat_table[port].dstport; // Should be HTTP / 80
    
    return TRUE;
}


/**
 * NAT the IP-fragmented packet from the original source to the CPD making the 
 * PFD the new sender. This will not assume nor change TCP headers.
 * This performs no lookups. It only changes the IP addresses and checksum.
 * 
 * @param[in] ip_pkt
 *      the packet to nat
 * 
 * @param[in] addresses
 *      PFD and CPD addresses
 */
void
nat_fragment(struct ip * ip_pkt, address_bundle_t * addresses)
{
    // adjust IP checksum taking IP addresses into account
    checksum_adjust((unsigned char *)&ip_pkt->ip_sum,
        (unsigned char *)&ip_pkt->ip_src, sizeof(address_bundle_t),
        (unsigned char *)addresses, sizeof(address_bundle_t));
    
    ip_pkt->ip_src.s_addr = addresses->pfd_addr;
    ip_pkt->ip_dst.s_addr = addresses->cpd_addr;
}


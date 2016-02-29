/*
 * $Id: pfd_packet.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file pfd_packet.c
 * @brief Relating to processing packets in the fast path
 * 
 * These functions and types will manage the packet processing in the fast path
 */

#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>
#include <jnx/vrf_util_pub.h>
#include <jnx/rt_shared_pub.h>
#include <jnx/jnx_types.h>
#include <jnx/mpsdk.h>
#include <jnx/atomic.h>
#include <sys/jnx/jbuf.h>
#include "pfd_packet.h"
#include "pfd_logging.h"
#include "pfd_config.h"
#include "pfd_conn.h"
#include "pfd_main.h"
#include "pfd_nat.h"

/*** Constants ***/

#ifndef VRFINDEX_ERROR
#define VRFINDEX_ERROR -1 ///< Error code when not able to get VRF index
#endif

#define MAX_MSP_SEND_RETRIES 100 ///< Max msp_data_send retires before panic

/**
 * Must have this many bytes of each packet in the jbuf to analyze it
 */
#define IP_NEEDED_BYTES (sizeof(struct ip))

/**
 * Must have this many bytes of each packet in the jbuf to analyze it for nat
 */
#define TCP_NEEDED_BYTES (sizeof(struct ip) + sizeof(struct tcphdr))

/*** Data Structures ***/

static uint8_t next_id; ///< next thread ID to take
static pthread_mutex_t next_id_lock; ///< lock to control access
static int vrf_default; ///< default RI's VRF index
static int vrf_pfd_forwarding;  ///< PFD RI's VRF index
static atomic_uint_t    loops_running; ///< # of data loops running
static volatile uint8_t do_shutdown;   ///< do the data loops need to shutdown


/* GLOBAL:
 * Since they're accessed in the conn module when we receive new addresses
 * 
 * All threads grab an ID on start up and then use it as an index into 
 * update_messages to check if there's an "update" message for them
 */
/**
 *  update messages for threads' config. Defined in the packet module
 */
thread_message_t update_messages[MAX_CPUS];


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
        
        if(!tmp_buf) { // check it didn't fail 
            return EFAIL;
        }
        
        *pkt_buf = tmp_buf;
    }
    return SUCCESS;
}


/**
 * Entry point for packet processing threads (function passed to pthread_create)
 * 
 * @param[in] params
 *     dataloop parameters with user data, loop identifier, and loop number
 */
static void *
pfd_process_packet(msp_dataloop_args_t * params)
{
    struct jbuf * pkt_buf;
    struct ip * ip_pkt;
    int rc, type, retries;
    uint8_t thread_id;
    uint16_t ip_frag_offset;
    address_bundle_t addresses;
    uint8_t ip_options_bytes = 0;
    sigset_t sig_mask;
    
    // Block SIGTERM to this thread/main thread will handle otherwise we inherit
    // this behaviour in our threads sigmask and the signal might come here
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sig_mask, NULL);

    // get thread ID used for messaging 
    LOCK_MUTEX(&next_id_lock);
    thread_id = next_id++;
    UNLOCK_MUTEX(&next_id_lock);

    atomic_add_uint(1, &loops_running);
    
    LOG(LOG_INFO, "%s: Started packet loop on cpu %d.",
            __func__, msp_data_get_cpu_num(params->dhandle));
    
    // Start the packet loop until shutdown
    while(!do_shutdown) {

        // check flag for a message to update config
        if(update_messages[thread_id].update) {
            LOCK_MUTEX(&update_messages[thread_id].lock);
            
            // turn off the update flag now that we have the lock and
            // are reading the config
            update_messages[thread_id].update = FALSE;

            // cache the addresses per CPU
            // leave in network byte order for faster comparison
            addresses.cpd_addr = update_messages[thread_id].addresses.cpd_addr;
            addresses.pfd_addr = update_messages[thread_id].addresses.pfd_addr;

            UNLOCK_MUTEX(&update_messages[thread_id].lock);
        }

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
        
        if(ip_pkt == NULL) {
            LOG(LOG_ERR, "%s: Accessing jbuf data failed.", __func__);
            jbuf_free(pkt_buf);
            continue;
        }
        
        if(jbuf_getvrf(pkt_buf) == (uint32_t)vrf_pfd_forwarding) {
            
            // it could be any src/dst IP addresses ...
            // and was pushed into the RI by a filter on a managed interface
            
            if(ip_pkt->ip_dst.s_addr != addresses.cpd_addr &&
               !is_auth_user(ip_pkt->ip_src.s_addr)) {

                // We can't let it through because it isn't authorized and not 
                // going to the CPD, so we need to NAT and send it to the CPD
    
                ip_frag_offset = ntohs(ip_pkt->ip_off);
                ip_options_bytes = (ip_pkt->ip_hl * 4) - sizeof(struct ip);
                
                // if it's a fragment (but not the first)
                if((ip_frag_offset & IP_OFFMASK)) {
                    
                    nat_fragment(ip_pkt, &addresses);
                    
                } else if(ip_pkt->ip_p == IPPROTO_TCP && !pullup_bytes(
                    &pkt_buf, TCP_NEEDED_BYTES + ip_options_bytes)) {
                    
                    
                    // It is TCP and could be the first fragment or normal
                    if(!nat_packet(ip_pkt, &addresses)) {
                        
                        LOG(LOG_ERR, "%s: Not enough room in the NAT table to "
                          "create a new entry. Dropping packet.", __func__);

                        jbuf_free(pkt_buf);
                        continue;
                    }
                } else {
                    LOG(LOG_NOTICE, "%s: Dropped a packet (%d) from %s. It's "
                        "not TCP or there's not enough bytes to form the TCP "
                        "header.", __func__, ip_pkt->ip_p,
                        inet_ntoa(ip_pkt->ip_src));
                    
                    jbuf_free(pkt_buf);
                    continue;
                }
            }
            // else it will just be reinjected
            
            // reinject into the default RI
            jbuf_setvrf(pkt_buf, (uint32_t)vrf_default);
        
        } else { // it should be coming back from the CPD in the default RI
            
            // if for some reason it is not from the currently known CPD
            if(ip_pkt->ip_src.s_addr != addresses.cpd_addr) {

                LOG(LOG_ERR, "%s: Received and dropped a packet from %s when "
                    " expecting only traffic from the CPD.", __func__,
                    inet_ntoa(ip_pkt->ip_src));

                jbuf_free(pkt_buf);
                continue;
            }
            
            // if for some reason it is not to the currently known PFD address
            if(ip_pkt->ip_dst.s_addr != addresses.pfd_addr) {
                
                LOG(LOG_ERR, "%s: Received and dropped a packet to %s when "
                    " expecting only traffic to the PFD.", __func__,
                    inet_ntoa(ip_pkt->ip_dst));

                jbuf_free(pkt_buf);
                continue;
            }
            
            // If it is a fragment drop it
            ip_frag_offset = ntohs(ip_pkt->ip_off);
            
            if((ip_frag_offset & (IP_MF|IP_OFFMASK))) {
                LOG(LOG_ERR, "%s: Packet coming from the CPD is fragmented. "
                    "Cannot reverse nat this packet. Dropping!", __func__);
                
                jbuf_free(pkt_buf);
                continue;                
            }
            
            ip_options_bytes = (ip_pkt->ip_hl * 4) - sizeof(struct ip);
            
            if(ip_pkt->ip_p != IPPROTO_TCP ||
               pullup_bytes(&pkt_buf, TCP_NEEDED_BYTES + ip_options_bytes)) {
                
                LOG(LOG_ERR, "%s: Dropped a packet from the CPD. It is not TCP "
                    "or there's not enough bytes to form the TCP header.",
                    __func__);
                
                jbuf_free(pkt_buf);
                continue;
            }
            
            // This is TCP from the CPD to PFD, so...
            // We need to reverse the NAT'ing and fwd to the original host
            
            if(!reverse_nat_packet(ip_pkt)) {
                
                LOG(LOG_ERR, "%s: Reverse NAT lookup failed. Dropping packet.",
                    __func__);
                
                jbuf_free(pkt_buf);
                continue;
            }
        }
        
        // enqueue it back into the FIFO to go out
        
        rc = MSP_DATA_SEND_RETRY;
        retries = 0;
        
        while(rc == MSP_DATA_SEND_RETRY && ++retries <= MAX_MSP_SEND_RETRIES) {
            rc = msp_data_send(params->dhandle, pkt_buf, type);
        }
        
        if(rc == MSP_DATA_SEND_FAIL) {
            
            LOG(LOG_ERR, "%s: Failed to forward packet using msp_data_send().",
                __func__);
            jbuf_free(pkt_buf);
            
        } else if(rc == MSP_DATA_SEND_RETRY) { // Panic / calls exit(1)
            
            LOG(LOG_ERR, "%s: PANIC: Failed to send a jbuf after %d retries "
                "with msp_data_send().", __func__, MAX_MSP_SEND_RETRIES);
            jbuf_free(pkt_buf);
            
        } else if(rc != MSP_OK) {
            
            LOG(LOG_ERR, "%s: Failed to forward packet and got unknown return "
                "code from msp_data_send().", __func__);
            jbuf_free(pkt_buf);
        }
    }
    
    //shutting down
    
    atomic_sub_uint(1, &loops_running);
    
    LOG(LOG_INFO, "%s: Stopped packet loop on cpu %d.",
            __func__, msp_data_get_cpu_num(params->dhandle));

    // thread is done if it reaches this point
    pthread_exit(NULL);
    return NULL;
}


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Init config for packet loop threads
 */
void
init_packet_loops_config(void)
{
    int i;
    
    next_id = 0;
    pthread_mutex_init(&next_id_lock, NULL);

    for(i = 0; i < MAX_CPUS; ++i) {
        pthread_mutex_init(&update_messages[i].lock, NULL);
    }
}


/**
 * Destroy config for packet loop threads
 */
void
destroy_packet_loops_config(void)
{
    int i;
    
    pthread_mutex_destroy(&next_id_lock);

    for(i = 0; i < MAX_CPUS; ++i) {
        pthread_mutex_destroy(&update_messages[i].lock);
    }
}


/**
 * Start packet loops
 */
void
init_packet_loops(void)
{
    static boolean started_loops = FALSE;
    int rc;
    msp_dataloop_params_t params;
    
    // only start once:
    if(started_loops) return;
    started_loops = TRUE;
    
    loops_running = 0;
    do_shutdown = 0;
    
    // get the vrf values upon failure abort because tables are probably not 
    // created which is a prerequisite of this happening

    vrf_default = vrf_getindexbyvrfname(
                    JUNOS_SDK_TABLE_DEFAULT_NAME, NULL, AF_INET);
    
    if(vrf_default == VRFINDEX_ERROR) {
        LOG(LOG_EMERG, "%s: Failed to retrieve VRF index for %s due to %m",
            __func__, JUNOS_SDK_TABLE_DEFAULT_NAME);
    } else {
        LOG(LOG_INFO, "%s: Retrieved VRF index for %s; index is %d",
            __func__, JUNOS_SDK_TABLE_DEFAULT_NAME, vrf_default);
    }
    
    vrf_pfd_forwarding = vrf_getindexbyvrfname(
                             RI_PFD_FORWARDING, NULL, AF_INET);
    
    if(vrf_pfd_forwarding == VRFINDEX_ERROR) {
        LOG(LOG_EMERG, "%s: Failed to retrieve VRF index for %s due to %m",
            __func__, RI_PFD_FORWARDING);
    } else {
        LOG(LOG_INFO, "%s: Retrieve VRF index for %s; index is %d",
            __func__, RI_PFD_FORWARDING, vrf_pfd_forwarding);
    }
    
    LOG(LOG_INFO, "%s: Starting packet loops", __func__);
    
    bzero(&params, sizeof(msp_dataloop_params_t));
    
    // start data threads/loops on all available data CPUs
    rc = msp_data_create_loops(pfd_process_packet, &params);
    
    if (rc != MSP_OK) {
        LOG(LOG_ALERT, "%s: Failed to starting packet loops in PFD... Exiting.",
            __func__);
        pfd_shutdown();
        return;
    }
    
    LOG(LOG_INFO, "%s: Finished starting packet loops", __func__);
}


/**
 * Stop packet loops
 */
void
stop_packet_loops(void)
{
    do_shutdown = 1;

    while(loops_running > 0) ; // note the spinning while waiting
}

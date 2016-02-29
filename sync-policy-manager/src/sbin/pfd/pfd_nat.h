/*
 * $Id: pfd_nat.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file pfd_nat.h
 * @brief 
 * 
 * Contains the declaration of functions for NAT/reverse-NAT header-rewriting 
 * functions for packets in transit, and for initializing the NAT table and 
 * monitoring thread. 
 */
 
#ifndef __PFD_NAT_H__
#define __PFD_NAT_H__

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <pthread.h>
#include <errno.h>

/*** Constants ***/

/**
 * Macro to lock mutex and catch errors and abort if any
 */
#define LOCK_MUTEX(lock) \
{   \
    int _rc = pthread_mutex_lock((lock)); \
    if(_rc == EINVAL) { \
        LOG(LOG_EMERG, "%s:%s:%d: pthread_mutex_lock failed with EINVAL " \
         "(Mutex %s is invalid)", __FILE__, __func__, __LINE__, #lock); \
    } else if(_rc == EDEADLK) { \
        LOG(LOG_EMERG, "%s:%s:%d: pthread_mutex_lock failed with EDEADLK " \
         "(Deadlocked on mutex %s)", __FILE__, __func__, __LINE__, #lock); \
    } else if(_rc) { \
        LOG(LOG_EMERG, "%s:%s:%d: pthread_mutex_lock failed with return code " \
         "%d (Mutex %s)", __FILE__, __func__, __LINE__, _rc, #lock); \
    } \
}


/**
 * Macro to unlock mutex and catch errors and abort if any
 */
#define UNLOCK_MUTEX(lock) \
{   \
    int _rc = pthread_mutex_unlock((lock)); \
    if(_rc) { \
        LOG(LOG_EMERG, "%s:%s:%d: pthread_mutex_unlock failed. Return code " \
            "%d (Mutex %s)", __FILE__, __func__, __LINE__, _rc, #lock); \
    } \
}

/*** Data structures ***/

/**
 * Bundle to hold both addresses
 */
typedef struct address_bundle_s {
    in_addr_t pfd_addr; ///< Address of the PFD interface (we're sending from)
    in_addr_t cpd_addr; ///< Address of the CPD interface
} address_bundle_t;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the NAT table, variables, and all the mutexes used in this module
 */
void init_nat(void);


/**
 * Destroy the NAT table and all the mutexes used in this module
 */
void terminate_nat(void);


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
boolean nat_packet(struct ip * ip_pkt, address_bundle_t * addresses);

/**
 * Reverse NAT the packet from the CPD to the original sender. (The PFD was the 
 * intermediate sender).
 * 
 * @param[in] ip_pkt
 *       the packet to reverse nat
 * 
 * @param[in] addresses
 *      PFD and CPD addresses
 *  
 * @return
 *       TRUE if successfully NAT'd, FALSE if cannot find an entry for dst port
 */
boolean reverse_nat_packet(struct ip * ip_pkt);


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
void nat_fragment(struct ip * ip_pkt, address_bundle_t * addresses);


#endif


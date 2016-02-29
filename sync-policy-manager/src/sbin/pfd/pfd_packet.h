/*
 * $Id: pfd_packet.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file pfd_packet.h
 * @brief Relating to processing packets in the fast path
 * 
 * These functions and types will manage the packet processing in the fast path
 */
 
#ifndef __PFD_PACKET_H__
#define __PFD_PACKET_H__

#include "pfd_nat.h"

/*** Constants ***/

/**
 * The routing instance name where all traffic is pushed into the PFD
 */
#define RI_PFD_FORWARDING "pfd_forwarding"

#define MAX_CPUS  32 ///<  The theoric maximum number of virtual CPUs/threads

/*** Data structures ***/


/**
 * Message for a thread set
 */
typedef struct thread_message_s {
    boolean update;             ///< There is a new message for reading set by another component and unset by the thread
    pthread_mutex_t lock;       ///< lock for accessing the message contents
    address_bundle_t addresses; ///< message contains addresses
} thread_message_t;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init config for packet loop threads
 */
void init_packet_loops_config(void);


/**
 * Destroy config for packet loop threads
 */
void destroy_packet_loops_config(void);


/**
 * Start packet loops
 */
void init_packet_loops(void);


/**
 * Stop packet loops
 */
void stop_packet_loops(void);

#endif


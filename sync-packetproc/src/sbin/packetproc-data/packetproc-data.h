/*
 * $Id: packetproc-data.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file packetproc-data.h
 * @brief Related to the packetproc data application.
 * 
 */
 
#ifndef __PACKETPROC_DATA_H__
#define __PACKETPROC_DATA_H__

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <isc/eventlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <jnx/bits.h>
#include <jnx/jnx_types.h>
#include <jnx/aux_types.h>
#include <jnx/mpsdk.h>
#include <jnx/trace.h>
#include <sys/jnx/jbuf.h>
#include <jnx/vrf_util_pub.h>

/** Packet thread data structure. */
typedef struct packet_thread_s {
    msp_data_handle_t thrd_data_hdl;  /**< thread data handle */
    msp_fifo_handle_t thrd_fifo_hdl;  /**< thread fifo handle */
    int               thrd_cpu;       /**< thread CPU number */
    int               thrd_user_data; /**< thread user data */
    pthread_t         thrd_tid;       /**< thread ID */
    uint32_t          thrd_counter;   /**< thread running counter */
} packet_thread_t;

/**
 * Initialize data loops
 * 
 */
int init_packet_loop(void);

#endif


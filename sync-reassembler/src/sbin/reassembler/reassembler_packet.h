/*
 * $Id: reassembler_packet.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file reassembler_packet.h
 * @brief Relating to processing packets in the fast path
 *
 * These functions and types will manage the packet processing in the data path
 */

#ifndef __REASSEMBLER_PACKET_H__
#define __REASSEMBLER_PACKET_H__

#include <isc/eventlib.h>
#include <jnx/aux_types.h>

/*** Constants ***/


/*** Data structures ***/


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
init_packet_loops(evContext ctx);


/**
 * Cleanup data loops for shutdown
 *
 * @param[in] ctx
 *     event context from master thread used for cleanup timer
 */
void
stop_packet_loops(evContext ctx);


#endif

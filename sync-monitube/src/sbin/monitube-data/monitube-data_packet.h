/*
 * $Id: monitube-data_packet.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-data_packet.h
 * @brief Relating to processing packets in the fast path
 *
 * These functions and types will manage the packet processing in the data path
 */

#ifndef __MONITUBE_DATA_PACKET_H__
#define __MONITUBE_DATA_PACKET_H__

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
 * Initialize the forwarding database (start trying to attach to it)
 *
 * @param[in] ctx
 *     event context from master thread used for FDB attach retry timer
 */
void
init_forwarding_database(evContext ctx);


/**
 * Cleanup data loops for shutdown
 *
 * @param[in] ctx
 *     event context from master thread used for cleanup timer
 */
void
stop_packet_loops(evContext ctx);


/**
 * Cleanup shared memory and object caches
 */
void
destroy_packet_loops_oc(void);


/**
 * Find all flow entries using the a monitor, and remove them
 */
void
clean_flows_with_any_monitor(void);


/**
 * Find all flow entries using the a mirror, and remove them
 */
void
clean_flows_with_any_mirror(void);


/**
 * Find all flow entries using the given mirror, and remove them
 *
 * @param[in] addr
 *      The mirror from address
 */
void
clean_flows_with_mirror(in_addr_t addr);


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
redirect_flows_with_mirror(in_addr_t addr, in_addr_t to);


/**
 * Find all flow entries using the given monitor, and remove them
 *
 * @param[in] name
 *      The monitor name
 */
void
clean_flows_with_monitor(char * name);


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
clean_flows_in_monitored_prefix(char * name, in_addr_t prefix, in_addr_t mask);


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
get_next_flow_state(replication_data_t * last, replication_data_t * next);


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
add_flow_state(replication_data_t * new_data);


/**
 * Remove flow state from the backup "database" i.e. the flow hashtable. This
 * is only called while this data component is a slave and not processing
 * traffic.
 *
 * @param[in] data
 *      The keys to find the flow state to remove
 */
void
remove_flow_state(delete_replication_data_t * data);


#endif

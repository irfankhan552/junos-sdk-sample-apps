/*
 * $Id: equilibrium-data_packet.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-data_packet.h
 * @brief Relating to processing packets in the fast path
 * 
 * These functions and types will manage the packet processing in the data path
 */
 
#ifndef __EQUILIBRIUM_DATA_PACKET_H__
#define __EQUILIBRIUM_DATA_PACKET_H__

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
 * Cleanup shared memory and data loops for shutdown
 * 
 * @param[in] ctx
 *     event context from master thread used for cleanup timer 
 */
void
destroy_packet_loops(evContext ctx);


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
                            in_addr_t server_addr);


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
                        uint16_t app_port);


/**
 * Find all session entries using the given server, and remove them
 * 
 * @param[in] ss_id
 *      The service set id
 */
void
clean_sessions_with_service_set(uint16_t ss_id);


#endif


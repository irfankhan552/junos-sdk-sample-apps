/*
 * $Id: dpm-mgmt_conn.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-mgmt_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __DPM_MGMT_CONN_H__
#define __DPM_MGMT_CONN_H__

/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the server socket connection
 * 
 * @param[in] ctx
 *     Newly created event context 
 
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_conn_server(evContext ctx);


/**
 * Close existing connections and shutdown server
 */
void
shutdown_conn_server(void);


/**
 * Notification about an MS-PIC interface going down
 * 
 * @param[in] name
 *      name of interface that has gone down
 */
void
mspic_offline(const char * name);


/**
 * Send a message to the ctrl component about a configuration reset
 * 
 * @param[in] use_classic
 *      Use classic filters
 */
void
notify_configuration_reset(boolean use_classic);


/**
 * Send a message to the ctrl component that configuration is complete 
 */
void
notify_configuration_complete(void);


/**
 * Send a message to the ctrl component about a policer configuration
 * 
 * @param[in] pol
 *      The policer configuration to send
 */
void
notify_policy_add(policer_info_t * pol);


/**
 * Send a message to the ctrl component about a default interface configuration
 * 
 * @param[in] int_name
 *      The interface name
 * 
 * @param[in] ifl_subunit
 *      The interface IFL subunit
 * 
 * @param[in] interface
 *      The interface default policy info
 * 
 * @param[in] ifl_index
 *      The interface IFL index
 */
void
notify_interface_add(char * int_name,
                     if_subunit_t ifl_subunit,
                     int_def_t * interface,
                     ifl_idx_t ifl_index);


/**
 * Send a message to the ctrl component about a subscriber configuration
 * 
 * @param[in] sub
 *      The subscriber configuration to send
 */
void
notify_subscriber_add(sub_info_t * sub);


#endif

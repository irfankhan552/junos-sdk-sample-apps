/*
 * $Id: monitube2-mgmt_conn.h 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file monitube2-mgmt_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __MONITUBE2_MGMT_CONN_H__
#define __MONITUBE2_MGMT_CONN_H__

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
init_server(evContext ctx);


/**
 * Close existing connections and shutdown server
 */
void
close_connections(void);


/**
 * Notification about an MS-PIC interface going down
 *
 * @param[in] name
 *      name of interface that has gone down
 */
void
mspic_offline(const char * name);


/**
 * Enqueue a message to go to the data component to delete all policy
 * 
 * Message relayed to all PICs
 */
void
notify_delete_all_policy(void);


/**
 * Enqueue a message to go to the data component to delete policy for a 
 * service set given the service set id
 *
 * @param[in] ss_id
 *      service set id
 *      
 * @param[in] gen_num
 *      Generation number of service set
 *      
 * @param[in] svc_id
 *      Service id
 *      
 * @param[in] fpc_slot
 *      FPC slot # of the MS PIC to send this to
 *      
 * @param[in] pic_slot
 *      PIC slot # of the MS PIC to send this to
 */
void
notify_delete_serviceset(uint16_t ss_id,
                         uint32_t gen_num,
                         uint32_t svc_id,
                         uint16_t fpc_slot,
                         uint16_t pic_slot);


/**
 * Enqueue a message to go to the data component about a rule application
 *
 * @param[in] rule_name
 *      The rule name
 *      
 * @param[in] ss_id
 *      Service set id
 *      
 * @param[in] gen_num
 *      Generation number of service set
 *      
 * @param[in] svc_id
 *      Service id
 *      
 * @param[in] fpc_slot
 *      FPC slot # of the MS PIC to send this to
 *      
 * @param[in] pic_slot
 *      PIC slot # of the MS PIC to send this to
 */
void
notify_apply_rule(char * rule_name,
                  uint16_t ss_id,
                  uint32_t gen_num,
                  uint32_t svc_id,
                  uint16_t fpc_slot,
                  uint16_t pic_slot);


/**
 * Enqueue a message to go to the data component about a rule removal
 *
 * @param[in] rule_name
 *      The rule name
 *      
 * @param[in] ss_id
 *      Service set id
 *      
 * @param[in] gen_num
 *      Generation number of service set
 *      
 * @param[in] svc_id
 *      Service id
 *      
 * @param[in] fpc_slot
 *      FPC slot # of the MS PIC to send this to
 *      
 * @param[in] pic_slot
 *      PIC slot # of the MS PIC to send this to
 */
void
notify_remove_rule(char * rule_name,
                   uint16_t ss_id,
                   uint32_t gen_num,
                   uint32_t svc_id,
                   uint16_t fpc_slot,
                   uint16_t pic_slot);


/**
 * Enqueue a message to go to the data component about a rule configuration
 *
 * @param[in] rule_name
 *      The rule name
 *      
 * @param[in] rate
 *      Monitoring rate
 *      
 * @param[in] redirect
 *      Mirror/redirect IP address
 *      
 * @param[in] fpc_slot
 *      FPC slot # of the MS PIC to send this to
 *      
 * @param[in] pic_slot
 *      PIC slot # of the MS PIC to send this to
 */
void
notify_config_rule(char * rule_name,
                   uint32_t rate,
                   in_addr_t redirect,
                   uint16_t fpc_slot,
                   uint16_t pic_slot);


/**
 * Enqueue a message to go to the data component about a rule delete
 *
 * @param[in] rule_name
 *      The rule name
 *      
 * @param[in] fpc_slot
 *      FPC slot # of the MS PIC to send this to
 *      
 * @param[in] pic_slot
 *      PIC slot # of the MS PIC to send this to
 */
void
notify_delete_rule(char * rule_name,
                   uint16_t fpc_slot,
                   uint16_t pic_slot);


/**
 * Enqueue a message to go to the data component about a rule's prefix
 * configuration
 *
 * @param[in] rule_name
 *      The rule name
 *      
 * @param[in] addr
 *      IP prefix
 *      
 * @param[in] mask
 *      Mask for prefix length
 *      
 * @param[in] delete
 *      Are we deleting the prefix or adding it
 *      
 * @param[in] fpc_slot
 *      FPC slot # of the MS PIC to send this to
 *      
 * @param[in] pic_slot
 *      PIC slot # of the MS PIC to send this to
 */
void
notify_config_rule_prefix(char * rule_name,
                          in_addr_t addr,
                          in_addr_t mask,
                          bool delete,
                          uint16_t fpc_slot,
                          uint16_t pic_slot);


/**
 * Go through the buffered messages and send them to the data component
 * if it's connected.
 */
void
process_notifications(void);

#endif

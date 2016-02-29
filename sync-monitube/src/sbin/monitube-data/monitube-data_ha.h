/*
 * $Id: monitube-data_ha.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-data_ha.h
 * @brief High Availability module 
 *        
 * 
 * These functions will manage the flow state replication for a master/slave  
 * data component.
 */
 
#ifndef __MONITUBE_DATA_HA_H__
#define __MONITUBE_DATA_HA_H__

#include "monitube-data_rtp.h"

/*** Constants ***/

#define MAX_MON_NAME_LEN  256     ///< Application name length


/*** Data structures ***/

/**
 * Replication data of flow state to send to backup/slave for its flow table 
 */
typedef struct replication_data_s {
    
    // flow table and identifier info:
    uint32_t  bucket;    ///< hashtable bucket number (HT BUCKET field)
    in_addr_t daddr;     ///< flow destination address (HT Key/Identifier field)
    uint16_t  dport;     ///< flow destination port (HT Key/Identifier field)
    
    // general:
    time_t    age_ts;    ///< flow age timestamp
    
    // MDI media loss rate related:
    uint32_t  ssrc;      ///< current RTP source
    source_t  source;    ///< RTP state of last known packet
    
    // MDI delay factor related:
    uint32_t  rate;      ///< drain rate of RTP payload    
    
    // monitor and mirror configuration applied to flow:
    in_addr_t maddr;     ///< mirror IP address
    uint32_t  m_vrf;     ///< mirror with outgoing VRF
    char      mon[MAX_MON_NAME_LEN];   ///< monitor name
} replication_data_t;


/**
 * Replication data of flow state to delete in backup/slave 
 */
typedef struct delete_replication_data_s {
    
    // flow table and identifier info:
    uint16_t  dport;     ///< flow destination port (HT Key/Identifier field)
    in_addr_t daddr;     ///< flow destination address (HT Key/Identifier field)
    uint32_t  bucket;    ///< hashtable bucket number (HT BUCKET field)

} delete_replication_data_t;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 *
 * @param[in] master_address
 *      Address of master, or 0 if this is the master
 *      
 * @param[in] ev_ctx
 *      main event context
 *
 * @return
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_replication(in_addr_t master_address, evContext ev_ctx);


/**
 * Pass replication data to add or update to the slave
 * 
 * @param[in] data
 *      The data to replicate to the slave
 */
void
update_replication_entry(replication_data_t * data);


/**
 * Pass replication data to delete to the slave
 * 
 * @param[in] data
 *      The data to replicate to the slave
 */
void
delete_replication_entry(delete_replication_data_t * data);


/**
 * Stop and shutdown all the replication subsystem
 */
void
stop_replication(void);


#endif

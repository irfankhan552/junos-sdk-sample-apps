/*
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
 * @file hellopics-mgmt_ha.c
 * @brief High Availability module 
 *        
 * 
 * These functions will manage the packet statistics replication for a master/slave  
 * management component.
 */
#include <stdlib.h>
#include <string.h>
#include <jnx/aux_types.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "hellopics-mgmt_logging.h"
#include "hellopics-mgmt_conn.h"
#include "hellopics-mgmt_ha.h"
#include <jnx/junos_sync.h>

#include HELLOPICS_OUT_H
/*** Constants ***/

#define HELLOPICS_HA_REPLICATION_PORT 39089  ///< port # to use for messaging

#define HELLOPICS_HA_REPLICATION_KA 30       ///< keepalive message interval

/*** Data structures ***/

static junos_sync_state_t     rep_state;     ///< replication state
static junos_sync_callbacks_t rep_callbacks; ///< replication callbacks
static junos_sync_context_t * rep_ctx;       ///< replication API context
volatile boolean is_master;

extern hellopics_stats_t  hellopics_stats;
extern evContext present_ctx;

/*** STATIC/INTERNAL Functions ***/
void update_hellopics_statistics(hellopics_stats_t *data);


/**
 * Perform necessary host to network byte-order swaps
 * 
 * @param[in,out] data
 *      replication data
 */
static void
replication_data_hton(hellopics_stats_t * data)
{
    data->msgs_sent = htonl(data->msgs_sent);
    data->msgs_received = htonl(data->msgs_received);
    data->msgs_missed = htonl(data->msgs_missed);
    data->msgs_badorder = htonl(data->msgs_badorder);
}


/**
 * Perform necessary network to host byte-order swaps
 * 
 * @param[in,out] data
 *      replication data
 */
static void
replication_data_ntoh(hellopics_stats_t * data)
{
    data->msgs_sent = ntohl(data->msgs_sent);
    data->msgs_received = ntohl(data->msgs_received);
    data->msgs_missed = ntohl(data->msgs_missed);
    data->msgs_badorder = ntohl(data->msgs_badorder);
}



/**
 * Callback when an acknolewedgement is received
 * 
 * @param[in] data
 *      The TLV data ack'd
 * 
 * @param[in] len
 *      The TLV data's length
 */
static void
ack_notify(junos_sync_tlv_t * data UNUSED, size_t len UNUSED)
{
    LOG(LOG_INFO, "%s: len %d", __func__, len);
}


/**
 * Callback when a data record is received and need to be decoded and 
 * added to the backup database
 * 
 * @param[in] data
 *      data to be decoded
 * 
 * @param[out] len
 *      length of data 
 */
static void
decode(void * data, size_t len)
{
   INSIST_ERR(!rep_state.is_master);
   LOG(LOG_INFO, "%s: update message of length %d", __func__, len);
   replication_data_ntoh((hellopics_stats_t *)data);
   update_hellopics_statistics((hellopics_stats_t *)data);
}


/**
 * Iterator callback to get the next data in application's database.
 * Called during the initial (re-)sync.
 * 
 * @param[in] current
 *      Null for first item, otherwise the item before the next one returned
 *      This could be the last item returned (works iterator-style).
 * 
 * @param[out] gndata
 *      The struct containing the TLV. We popluate data with an 
 *      allocated copy of the replication data to send 
 */
static void *
get_next(void * current __unused , junos_sync_getnext_data_t * gndata __unused)
{
  return NULL;
}


/**
 * Callback to notify that the connection's status has changed
 * 
 * @param[in] conn_status
 *      Number of entries / messages passed in initial replication
 */
static void
conn_status_change (junos_sync_conn_status_t conn_status)
{
    LOG(LOG_INFO, "%s: replication connection status is %s", __func__,
        (conn_status == JUNOS_SYNC_CONN_DOWN) ? "Down" : "Up");
}


/**
 * Callback to notify that the initial synchronization is done
 * 
 * @param[in] num_of_entries_synced
 *      Number of entries / messages passed in initial replication
 */
static void
init_sync_done (size_t num_of_entries_synced)
{
    LOG(LOG_INFO, "%s: initial replication is done, %d entries are synced",
            __func__, num_of_entries_synced);
}


/**
 * Callback to notify application that the master finished binding
 * 
 * @param[in] port
 *      Free port on which it bound itself
 */
static void
bind_done(in_port_t port)
{
    if(port != HELLOPICS_HA_REPLICATION_PORT) {
        LOG(LOG_EMERG, "%s: master's replication server could not bind "
                "to port %d", __func__, HELLOPICS_HA_REPLICATION_PORT);
        return;
    }
    
    LOG(LOG_INFO, "%s: master's replication server is bound to port %d",
            __func__, port);
}


/**
 * Callback to log a message
 * 
 * @param[in] format
 *      message to log
 */
static void
log_msg(const char * format, ...)
{
    va_list ap;
    va_start(ap, format);
    vlogging(LOG_INFO, format, ap);
    va_end(ap);
}


/**
 * Callback to clean up the backup database
 */
static void
db_clear(void)
{
    LOG(LOG_INFO, "%s", __func__);
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 * 
 * @param[in] master_address
 *      Address of master, or 0 if this is the master
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_replication(in_addr_t master_address)
{
    int result;
    rep_ctx = NULL;
    bzero(&rep_state, sizeof(junos_sync_state_t));
    bzero(&rep_callbacks, sizeof(junos_sync_callbacks_t));
    
    rep_state.is_master = (master_address == 0);
    rep_state.no_resync = false;
    rep_state.server_port = HELLOPICS_HA_REPLICATION_PORT;
    rep_state.keepalive_time = HELLOPICS_HA_REPLICATION_KA;
    
    if(!rep_state.is_master) {
        rep_state.server_addr = master_address;
    }
    
    rep_callbacks.sync_ack_notify_func = ack_notify;
    rep_callbacks.sync_data_decode_func = decode;
    rep_callbacks.sync_data_get_next_func = get_next;
    rep_callbacks.sync_conn_status_change_func = conn_status_change;
    rep_callbacks.sync_init_sync_done_func = init_sync_done;
    rep_callbacks.sync_bind_done_func = bind_done;
    rep_callbacks.sync_log_msg_func = log_msg;
    rep_callbacks.sync_malloc_func = malloc;
    rep_callbacks.sync_free_func = free;
    rep_callbacks.sync_db_clear_func = db_clear;

    rep_ctx = junos_sync_init(&rep_state, &rep_callbacks);
    
    if (!rep_ctx) {
        LOG(LOG_ERR, "%s: Failed to initialize a junos_sync context.",
            __func__);
        return EFAIL;
    }
    
    if ((result = junos_sync_set_event_context(rep_ctx, present_ctx)) != JUNOS_SYNC_OK) {
        LOG(LOG_ERR, "%s: Failed to set an event context. Error is %d",
                    __func__,result);
        junos_sync_exit(rep_ctx);
    }
    if ((result = junos_sync_start(rep_ctx)) != JUNOS_SYNC_OK) {
        LOG(LOG_ERR, "%s: Failed to start a junos_sync subsystem. Error is %d",
                    __func__,result);
        junos_sync_exit(rep_ctx);
        return EFAIL;
    }
      LOG(LOG_INFO, 
            "%s: Intialized and started sync", __func__);
    return SUCCESS;
}


/**
 * Pass replication data to add or update to the slave
 * 
 * @param[in] data
 *      The data to replicate to the slave
 */
void
update_replication_entry(hellopics_stats_t *data)
{
    hellopics_stats_t * current;
    junos_sync_tlv_t * tlv_data;
    
    if(rep_ctx == NULL)
        return;
    
    LOG(LOG_INFO, "%s", __func__);
    
    // create a new tlv with the trailing data
    tlv_data = calloc(1, sizeof(junos_sync_tlv_t) + sizeof(hellopics_stats_t));
    INSIST_ERR(tlv_data != NULL);
    
    tlv_data->tlv_len = sizeof(hellopics_stats_t);
    current = (hellopics_stats_t *)&tlv_data->tlv_value;

    // populate data to enqueue
    memcpy(current, data, sizeof(hellopics_stats_t));
    
    replication_data_hton(current);
    
      LOG(LOG_INFO, 
            "%s: Master:Queueing the data for replication", __func__);
    junos_sync_queue(rep_ctx, tlv_data, FALSE);
}


/**
 * Stop and shutdown all the replication subsystem
 */
void
stop_replication(void)
{
    junos_sync_exit(rep_ctx);
    rep_ctx = NULL;
}

/**
 * Function called in backup daemon to update the state received 
 * from the master
 * @param[in] data
 *      Data received from the master
 */
void update_hellopics_statistics(hellopics_stats_t *data)
{
    LOG(LOG_INFO, "%s: Backup: Received replication data",
            __func__);
    hellopics_stats.msgs_sent      = data->msgs_sent;
    hellopics_stats.msgs_received  = data->msgs_received;
    hellopics_stats.msgs_missed    = data->msgs_missed;
    hellopics_stats.msgs_badorder  = data->msgs_badorder;
    LOG(LOG_INFO, "Backup: Messages sent-%d, received-%d, missed-%d, badorder-%d",
                    hellopics_stats.msgs_sent,hellopics_stats.msgs_received, 
                    hellopics_stats.msgs_missed,hellopics_stats.msgs_badorder);
}
/**
 * Configure mastership of this data component
 *
 * @param[in] state
 *      Mastership state (T=Master, F=Slave)
 *
 * @param[in] master_addr
 *      Master address (only present if state=F/slave)
 */
void
set_mastership(boolean state, in_addr_t master_addr)
{
     if(init_replication(master_addr) == SUCCESS) {

        is_master = state;
    }
}

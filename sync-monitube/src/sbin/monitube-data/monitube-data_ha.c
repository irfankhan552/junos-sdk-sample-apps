/*
 * $Id: monitube-data_ha.c 346460 2009-11-14 05:06:47Z ssiano $
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

#include "monitube-data_main.h"
#include "monitube-data_ha.h"
#include "monitube-data_packet.h"
#include <jnx/junos_sync.h>

/*** Constants ***/

#define MONITUBE_DC_REPLICATION_PORT 39079  ///< port # to use for messaging

#define MONITUBE_DC_REPLICATION_KA 30       ///< keepalive message interval

/*** Data structures ***/

static junos_sync_state_t     rep_state;     ///< replication state
static junos_sync_callbacks_t rep_callbacks; ///< replication callbacks
static junos_sync_context_t * rep_ctx;       ///< replication API context


/*** STATIC/INTERNAL Functions ***/


/**
 * Perform necessary host to network byte-order swaps
 *
 * @param[in,out] data
 *      replication data
 */
static void
replication_data_hton(replication_data_t * data)
{
    data->bucket = htonl(data->bucket);
    data->dport = htons(data->dport);
    data->age_ts = htonl(data->age_ts);
    data->ssrc = htonl(data->ssrc);
    data->rate = htonl(data->rate);
    data->m_vrf = htonl(data->m_vrf);
    data->source.max_seq = htons(data->source.max_seq);
    data->source.cycles = htonl(data->source.cycles);
    data->source.base_seq = htonl(data->source.base_seq);
    data->source.bad_seq = htonl(data->source.bad_seq);
    data->source.probation = htonl(data->source.probation);
    data->source.received = htonl(data->source.received);
    data->source.expected_prior = htonl(data->source.expected_prior);
    data->source.received_prior = htonl(data->source.received_prior);
}


/**
 * Perform necessary network to host byte-order swaps
 *
 * @param[in,out] data
 *      replication data
 */
static void
replication_data_ntoh(replication_data_t * data)
{
    data->bucket = ntohl(data->bucket);
    data->dport = ntohs(data->dport);
    data->age_ts = ntohl(data->age_ts);
    data->ssrc = ntohl(data->ssrc);
    data->rate = ntohl(data->rate);
    data->m_vrf = ntohl(data->m_vrf);
    data->source.max_seq = ntohs(data->source.max_seq);
    data->source.cycles = ntohl(data->source.cycles);
    data->source.base_seq = ntohl(data->source.base_seq);
    data->source.bad_seq = ntohl(data->source.bad_seq);
    data->source.probation = ntohl(data->source.probation);
    data->source.received = ntohl(data->source.received);
    data->source.expected_prior = ntohl(data->source.expected_prior);
    data->source.received_prior = ntohl(data->source.received_prior);
}


/**
 * Perform necessary host to network byte-order swaps
 *
 * @param[in,out] data
 *      replication data
 */
static void
delete_replication_data_hton(delete_replication_data_t * data)
{
    data->bucket = htonl(data->bucket);
    data->dport = htons(data->dport);
}


/**
 * Perform necessary network to host byte-order swaps
 *
 * @param[in,out] data
 *      replication data
 */
static void
delete_replication_data_ntoh(delete_replication_data_t * data)
{
    data->bucket = ntohl(data->bucket);
    data->dport = ntohs(data->dport);
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

    if(len == sizeof(replication_data_t)) {

        LOG(LOG_INFO, "%s: update message of length %d", __func__, len);

        replication_data_ntoh((replication_data_t *)data);
        add_flow_state((replication_data_t *)data);

    } else if(len == sizeof(delete_replication_data_t)) {

        LOG(LOG_INFO, "%s: delete message of length %d", __func__, len);

        delete_replication_data_ntoh((delete_replication_data_t *)data);
        remove_flow_state((delete_replication_data_t *)data);

    } else {

        LOG(LOG_ERR, "%s: unknown message of length %d", __func__, len);
    }
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
get_next(void * current, junos_sync_getnext_data_t * gndata)
{
    replication_data_t * next, * last = (replication_data_t *)current;
    junos_sync_tlv_t * tlv_data = calloc(1,
            sizeof(junos_sync_tlv_t) + sizeof(replication_data_t));

    INSIST_ERR(tlv_data != NULL);

    LOG(LOG_INFO, "%s", __func__);

    tlv_data->tlv_len = sizeof(replication_data_t);
    next = (replication_data_t *)&tlv_data->tlv_value;
    gndata->data = tlv_data;

    if(get_next_flow_state(last, next) == EFAIL) {
        free(tlv_data);
        gndata->data = NULL;
        return NULL;
    }

    replication_data_hton(next);

    return next;
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
    if(port != MONITUBE_DC_REPLICATION_PORT) {
        LOG(LOG_EMERG, "%s: master's replication server could not bind "
                "to port %d", __func__, MONITUBE_DC_REPLICATION_PORT);
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
 * @param[in] ev_ctx
 *      main event context
 *
 * @return
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_replication(in_addr_t master_address, evContext ev_ctx)
{
    static bool first_time = true;
    int rc;

    if(!first_time) {

        if(rep_state.is_master && master_address != 0) {
            // shutdown master server and become slave, but start from scratch
            junos_sync_exit(rep_ctx);
        } else if(!rep_state.is_master && master_address == 0) {

            rep_state.is_master = true;
            rep_state.server_addr = 0;

            // slave can take over as master
            junos_sync_switchover(rep_ctx, &rep_state, &rep_callbacks);
            return SUCCESS;
        }
    }

    first_time = false;

    rep_ctx = NULL;
    bzero(&rep_state, sizeof(junos_sync_state_t));
    bzero(&rep_callbacks, sizeof(junos_sync_callbacks_t));

    rep_state.is_master = (master_address == 0) ? true : false;
    rep_state.no_resync = false;
    rep_state.server_port = MONITUBE_DC_REPLICATION_PORT;
    rep_state.keepalive_time = MONITUBE_DC_REPLICATION_KA;

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
        first_time = true;
        return EFAIL;
    }
    
    rc = junos_sync_set_event_context(rep_ctx, ev_ctx);
    
    if (rc != JUNOS_SYNC_OK) {
        LOG(LOG_ERR, "%s: Failed to use main event context (err: %d).",
            __func__, rc);
        junos_sync_exit(rep_ctx);
        rep_ctx = NULL;
        first_time = true;
        return EFAIL;
    }
    
    rc = junos_sync_start(rep_ctx);

    if (rc != JUNOS_SYNC_OK) {
        LOG(LOG_ERR, "%s: Failed to start a junos_sync subsystem (err: %d).",
            __func__, rc);
        junos_sync_exit(rep_ctx);
        rep_ctx = NULL;
        first_time = true;
        return EFAIL;
    }
    return SUCCESS;
}


/**
 * Pass replication data to add or update to the slave
 *
 * @param[in] data
 *      The data to replicate to the slave
 */
void
update_replication_entry(replication_data_t * data)
{
    replication_data_t * current;
    junos_sync_tlv_t * tlv_data;

    if(rep_ctx == NULL)
        return;

    LOG(LOG_INFO, "%s", __func__);

    // create a new tlv with the trailing data
    tlv_data = calloc(1, sizeof(junos_sync_tlv_t) + sizeof(replication_data_t));
    INSIST_ERR(tlv_data != NULL);

    tlv_data->tlv_len = sizeof(replication_data_t);
    current = (replication_data_t *)&tlv_data->tlv_value;

    // populate data to enqueue
    memcpy(current, data, sizeof(replication_data_t));

    replication_data_hton(current);

    junos_sync_queue(rep_ctx, tlv_data, false);
}


/**
 * Pass replication data to delete to the slave
 *
 * @param[in] data
 *      The data to replicate to the slave
 */
void
delete_replication_entry(delete_replication_data_t * data)
{
    delete_replication_data_t * current;
    junos_sync_tlv_t * tlv_data;

    if(rep_ctx == NULL)
        return;

    LOG(LOG_INFO, "%s", __func__);

    // create a new tlv with the trailing data
    tlv_data = calloc(1, sizeof(junos_sync_tlv_t) +
                            sizeof(delete_replication_data_t));
    INSIST_ERR(tlv_data != NULL);

    tlv_data->tlv_len = sizeof(delete_replication_data_t);
    current = (delete_replication_data_t *)&tlv_data->tlv_value;

    // populate data to enqueue
    memcpy(current, data, sizeof(delete_replication_data_t));

    delete_replication_data_hton(current);

    junos_sync_queue(rep_ctx, tlv_data, false);
}


/**
 * Stop and shutdown all the replication subsystem
 */
void
stop_replication(void)
{
    if(rep_ctx) {
        junos_sync_exit(rep_ctx);
    }
    rep_ctx = NULL;
}

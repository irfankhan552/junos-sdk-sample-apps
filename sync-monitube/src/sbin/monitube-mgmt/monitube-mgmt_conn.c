/*
 * $Id: monitube-mgmt_conn.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-mgmt_conn.c
 * @brief Relating to managing the connections
 *
 * These functions and types will manage the connections.
 */

#include <sync/common.h>
#include <jnx/pconn.h>
#include <sync/monitube_ipc.h>
#include "monitube-mgmt_config.h"
#include "monitube-mgmt_conn.h"
#include "monitube-mgmt_logging.h"

#include MONITUBE_OUT_H

/*** Constants ***/

#define MONITUBE_MGMT_SERVER_MAX_CONN 20  ///< max # cnxs (to service component)

#define RETRY_INTERVAL 5 ///< retry processing notification messages

/*** Data Structures ***/

static evContext       m_ctx;         ///< Event context for monitube
static pconn_server_t  * mgmt_server; ///< the server connection info

/**
 * session item in our session list
 */
typedef struct session_s {
    pconn_session_t           * session;  ///< this session
    TAILQ_ENTRY(session_s)    entries;    ///< list pointers (for TAILQ)
} session_t;

/**
 * A type of list/set for sessions (as a TAILQ)
 */
typedef TAILQ_HEAD(session_list_s, session_s) session_list_t;

/**
 * The session list
 */
static session_list_t session_list = TAILQ_HEAD_INITIALIZER(session_list);

/**
 * A notification message to store in the queue of messages to go out to the
 * service component(s)
 */
typedef struct notification_msg_s {
    msg_type_e   action;        ///< action (see: shared/h/sync/monitube_ipc.h)
    uint16_t     message_len;   ///< length of message to be sent
    void *       message;       ///< data to send (depends on action)
    TAILQ_ENTRY(notification_msg_s) entries;    ///< ptrs to next/prev
} notification_msg_t;


/**
 * List of notification messages to buffer while no one is connected
 */
static TAILQ_HEAD(notification_buffer_s, notification_msg_s) notification_msgs =
            TAILQ_HEAD_INITIALIZER(notification_msgs);


/*** STATIC/INTERNAL Functions ***/

/**
 * Delete all messages in the notifications queue
 */
static void
empty_notifications_queue(void)
{
    notification_msg_t * msg;

    while((msg = TAILQ_FIRST(&notification_msgs)) != NULL) {
        TAILQ_REMOVE(&notification_msgs, msg, entries);
        if(msg->message_len != 0) {
            free(msg->message);
        }
        free(msg);
    }
}

/**
 * Connection handler for new, dying, or failed connections
 *
 * @param[in] session
 *      The session information for the source peer
 *
 * @param[in] event
 *      The event (established, or shutdown are the ones we care about)
 *
 * @param[in] cookie
 *      The cookie we passed in. This is the eventlib context here.
 */
static void
receive_connection(pconn_session_t * session,
                   pconn_event_t event,
                   void * cookie __unused)
{
    int rc;
    monitor_t * mon;
    mirror_t * mir;
    address_t * address;
    struct in_addr peer_addr;
    pconn_peer_info_t info;
    notification_msg_t * msg;
    session_t * tmp_session;

    rc = pconn_session_get_peer_info(session, &info);

    if(rc != PCONN_OK) {
        LOG(TRACE_LOG_ERR, "%s: Cannot retrieve peer info "
            "for session. Error: %d", __func__, rc);

        pconn_session_close(session);
        return;
    }
    
    INSIST_ERR(info.ppi_peer_type == PCONN_PEER_TYPE_PIC);
    
    rc = pconn_get_peer_address(&info, &peer_addr.s_addr);

    if(rc != PCONN_OK) {
        LOG(TRACE_LOG_ERR, "%s: Cannot retrieve peer address "
            "for session. Error: %d", __func__, rc);

        pconn_session_close(session);
        return;
    }
    
    switch (event) {

    case PCONN_EVENT_ESTABLISHED:

        junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
            "%s: Accepted connection from ms-%d/%d/0 (%s)",
            __func__, info.ppi_fpc_slot, info.ppi_pic_slot,
            inet_ntoa(peer_addr));

        tmp_session = malloc(sizeof(session_t));
        INSIST(tmp_session != NULL);
        
        tmp_session->session = session;
        TAILQ_INSERT_TAIL(&session_list, tmp_session, entries);

        // Send all configuration to the service (PIC) component
        // go through all monitors and mirrors
        // Assume it has nothing
        
        empty_notifications_queue();

        mon = next_monitor(NULL);
        while(mon != NULL) {

            notify_monitor_update(mon->name, mon->rate);

            // addresses of this monitor
            address = next_address(mon, NULL);
            while(address != NULL) {

                notify_address_update(
                        mon->name, address->address, address->mask);

                address = next_address(mon, address);
            }

            mon = next_monitor(mon);
        }

        mir = next_mirror(NULL);
        while(mir != NULL) {

            notify_mirror_update(mir->original, mir->redirect);

            mir = next_mirror(mir);
        }
        
        notify_replication_interval(get_replication_interval());
        

        // send it all out to the component that just connected
        while((msg = TAILQ_FIRST(&notification_msgs)) != NULL) {

            if(pconn_server_send(session, msg->action, msg->message,
                    msg->message_len) != PCONN_OK) {

                LOG(TRACE_LOG_ERR, "%s: Failed to send configuration message"
                    " (%d) to the data component.", __func__, msg->action);

                empty_notifications_queue();
                return;
            }

            TAILQ_REMOVE(&notification_msgs, msg, entries);
            if(msg->message_len != 0) {
                free(msg->message);
            }
            free(msg);

            junos_trace(MONITUBE_TRACEFLAG_CONNECTION, "%s: Dequeued message "
                    "sent to the data component", __func__);
        }

        return;

    case PCONN_EVENT_SHUTDOWN:

        junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
            "%s: Disconnected from ms-%d/%d/0 (%s)",
            __func__, info.ppi_fpc_slot, info.ppi_pic_slot,
            inet_ntoa(peer_addr));

        break;

    case PCONN_EVENT_FAILED:

        junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
            "%s: Connection failure from ms-%d/%d/0 (%s)",
            __func__, info.ppi_fpc_slot, info.ppi_pic_slot,
            inet_ntoa(peer_addr));


        break;

    default:

        LOG(TRACE_LOG_ERR, "%s: Received an unknown event", __func__);

        return;
    }
    
    tmp_session = TAILQ_FIRST(&session_list);
    
    while(tmp_session != NULL) {
        
        if(tmp_session->session == session) {
            TAILQ_REMOVE(&session_list, tmp_session, entries);
            free(tmp_session);
            return;
        }
        
        tmp_session = TAILQ_NEXT(tmp_session, entries);
    }
    
    LOG(TRACE_LOG_ERR, "%s: Cannot find an existing "
            "peer session to shutdown", __func__);
}


/**
 * Message handler for open connections to the data component.
 * Called when we receive a message.
 *
 * @param[in] session
 *      The session information for the source peer
 *
 * @param[in] msg
 *      The inbound message
 *
 * @param[in] cookie
 *      The cookie we passed in. This is the eventlib context here.
 *
 * @return
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
static status_t
receive_message(pconn_session_t * session,
                ipc_msg_t * msg,
                void * cookie __unused)
{
    flow_stat_t * data = NULL;
    double df;
    void * tmp;
    pconn_peer_info_t  info;

    pconn_session_get_peer_info(session, &info);
    
    switch(msg->subtype) {

    case MSG_FLOW_STAT_UPDATE:

        data = (flow_stat_t *)msg->data;

        // check message length
        INSIST_ERR(msg->length ==
            sizeof(flow_stat_t) + ntohs(data->mon_name_len));

        // check name length
        INSIST_ERR(strlen(data->mon_name) + 1 == ntohs(data->mon_name_len));

        // carefully convert to the double it really is
        data->mdi_df = ntohq(data->mdi_df);
        tmp = &data->mdi_df;
        df = *(double *)tmp;

        set_flow_stat(info.ppi_fpc_slot, info.ppi_pic_slot, data->mon_name,
            data->flow_addr, ntohs(data->flow_port), df, ntohl(data->mdi_mlr));

        break;

    default:

        LOG(TRACE_LOG_ERR,
            "%s: Received an unknown message type (%d) from the "
            "data component", __func__, msg->subtype);
        return EFAIL;
    }

    return SUCCESS;
}


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
init_server(evContext ctx)
{
    pconn_server_params_t params;

    m_ctx = ctx;

    bzero(&params, sizeof(pconn_server_params_t));

    // setup the server args
    params.pconn_port            = MONITUBE_PORT_NUM;
    params.pconn_max_connections = MONITUBE_MGMT_SERVER_MAX_CONN;
    params.pconn_event_handler   = receive_connection;

    // bind
    mgmt_server = pconn_server_create(&params, m_ctx, receive_message, NULL);

    if(mgmt_server == NULL) {
        LOG(TRACE_LOG_ERR, "%s: Failed to initialize the pconn server"
            " on port %d.", __func__, MONITUBE_PORT_NUM);
        return EFAIL;
    }

    LOG(TRACE_LOG_INFO, "%s: Successfully initialized "
            "the pconn server on port %d.", __func__, MONITUBE_PORT_NUM);

    return SUCCESS;
}


/**
 * Close existing connections and shutdown server
 */
void
close_connections(void)
{
    session_t * session;

    while((session = TAILQ_FIRST(&session_list)) != NULL) {
        TAILQ_REMOVE(&session_list, session, entries);
        pconn_session_close(session->session);
        free(session);
    }

    if(mgmt_server != NULL) {
        junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
            "%s: Shutting down the server on port %d.",
            __func__, MONITUBE_PORT_NUM);
        
        pconn_server_shutdown(mgmt_server);
        mgmt_server = NULL;
    }

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Deleting the queue of notification messages.", __func__);

    empty_notifications_queue();
}


/**
 * Notification about an MS-PIC interface going down
 *
 * @param[in] name
 *      name of interface that has gone down
 */
void
mspic_offline(const char * name)
{
    char current_int_prefix[64];
    session_t * session;
    pconn_peer_info_t info;

    // see if its one we are connected to

    session = TAILQ_FIRST(&session_list);
    
    while(session != NULL) {

        pconn_session_get_peer_info(session->session, &info);
        
        snprintf(current_int_prefix, sizeof(current_int_prefix),
            "ms-%d/%d/0", info.ppi_fpc_slot, info.ppi_pic_slot);

        if(strstr(name, current_int_prefix) != NULL) {

            junos_trace(MONITUBE_TRACEFLAG_CONNECTION, "%s: Connection from %s "
                "was shutdown (forced).", __func__, current_int_prefix);

            pconn_session_close(session->session);
            TAILQ_REMOVE(&session_list, session, entries);
            free(session);
            return;
        }
        session = TAILQ_NEXT(session, entries);
    }
}


/**
 * Enqueue a message to go to the data component about a monitor update
 *
 * @param[in] mon_name
 *      Monitor's name
 *
 * @param[in] rate
 *      Configuration group bps rate
 */
void
notify_monitor_update(const char * mon_name, uint32_t rate)
{
    notification_msg_t * msg;
    update_mon_info_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);

    msg->action = MSG_CONF_MON;

    msg->message_len = sizeof(update_mon_info_t) + strlen(mon_name) + 1;

    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->rate = htonl(rate);
    data->mon_name_len = htons(strlen(mon_name) + 1);
    strcpy(data->mon_name, mon_name);

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


/**
 * Enqueue a message to go to the data component about an address update
 *
 * @param[in] mon_name
 *      Monitor's name
 *
 * @param[in] address
 *      Address in the monitor
 *
 * @param[in] mask
 *      Address mask
 */
void
notify_address_update(const char * mon_name, in_addr_t address, in_addr_t mask)
{
    notification_msg_t * msg;
    maddr_info_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);

    msg->action = MSG_CONF_MON_ADDR;

    msg->message_len = sizeof(maddr_info_t) + strlen(mon_name) + 1;

    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->addr = address;
    data->mask = mask;
    data->mon_name_len = htons(strlen(mon_name) + 1);
    strcpy(data->mon_name, mon_name);

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


/**
 * Enqueue a message to go to the data component about a mirror update
 *
 * @param[in] mirror_from
 *      Mirror from address
 *
 * @param[in] mirror_to
 *      Mirror to address
 */
void
notify_mirror_update(in_addr_t mirror_from, in_addr_t mirror_to)
{
    notification_msg_t * msg;
    update_mir_info_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);

    msg->action = MSG_CONF_MIR;

    msg->message_len = sizeof(update_mir_info_t);

    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->mirror_from = mirror_from;
    data->mirror_to = mirror_to;

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


/**
 * Enqueue a message to go to the data component about a monitor delete
 *
 * @param[in] mon_name
 *      Monitor's name
 */
void
notify_monitor_delete(const char * mon_name)
{
    notification_msg_t * msg;
    del_mon_info_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);

    msg->action = MSG_DELETE_MON;

    msg->message_len = sizeof(del_mon_info_t) + strlen(mon_name) + 1;

    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->mon_name_len = htons(strlen(mon_name) + 1);
    strcpy(data->mon_name, mon_name);

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


/**
 * Enqueue a message to go to the data component about an address delete
 *
 * @param[in] mon_name
 *      Monitor's name
 *
 * @param[in] address
 *      Address in the monitor
 *
 * @param[in] mask
 *      Address mask
 */
void
notify_address_delete(const char * mon_name, in_addr_t address, in_addr_t mask)
{
    notification_msg_t * msg;
    maddr_info_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);

    msg->action = MSG_DELETE_MON_ADDR;

    msg->message_len = sizeof(maddr_info_t) + strlen(mon_name) + 1;

    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->addr = address;
    data->mask = mask;
    data->mon_name_len = htons(strlen(mon_name) + 1);
    strcpy(data->mon_name, mon_name);

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


/**
 * Enqueue a message to go to the data component about an mirror delete
 *
 * @param[in] mirror_from
 *      Mirror from address
 */
void
notify_mirror_delete(in_addr_t mirror_from)
{
    notification_msg_t * msg;
    del_mir_info_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);

    msg->action = MSG_DELETE_MIR;

    msg->message_len = sizeof(del_mir_info_t);

    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->mirror_from = mirror_from;

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


/**
 * Enqueue a message to go to the data component to delete all monitors
 */
void
notify_delete_all_monitors(void)
{
    notification_msg_t * msg;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);

    msg->action = MSG_DELETE_ALL_MON;

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


/**
 * Enqueue a message to go to the data component to delete all mirrors
 */
void
notify_delete_all_mirrors(void)
{
    notification_msg_t * msg;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);

    msg->action = MSG_DELETE_ALL_MIR;

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


/**
 * Enqueue a message to go to the data component to set the replication interval
 *
 * @param[in] r_int
 *      New replication interval
 */
void
notify_replication_interval(uint8_t r_int)
{
    notification_msg_t * msg;
    replication_info_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST(msg != NULL);

    msg->action = MSG_REP_INFO;

    msg->message_len = sizeof(replication_info_t);

    msg->message = data = calloc(1, msg->message_len);
    INSIST(data != NULL);
    data->interval = r_int;

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


/**
 * Go through the buffered messages and send them to the data component
 * if it's connected.
 */
void
process_notifications(void)
{
    notification_msg_t * msg;
    session_t * session;
    pconn_peer_info_t info;

    if(TAILQ_FIRST(&session_list) == NULL) { // no connections
        // don't bother doing anything yet
        empty_notifications_queue();
        return;
    }

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION, "%s", __func__);

    // for each msg
    while((msg = TAILQ_FIRST(&notification_msgs)) != NULL) {

        // send to each session (PIC)
        session = TAILQ_FIRST(&session_list);
        while(session != NULL) {
            
            if(pconn_server_send(session->session,
                    msg->action, msg->message, msg->message_len) != PCONN_OK) {

                pconn_session_get_peer_info(session->session, &info);
                
                LOG(LOG_ERR, "%s: Failed to send configuration message (%d) to "
                    "ms-%d/%d/0.", __func__, msg->action, info.ppi_fpc_slot,
                    info.ppi_pic_slot);
            }
            
            session = TAILQ_NEXT(session, entries);          
        }

        TAILQ_REMOVE(&notification_msgs, msg, entries);
        if(msg->message_len != 0) {
            free(msg->message);
        }
        free(msg);

        junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
            "%s: Dequeued message sent to the data component(s)", __func__);
    }
}


/*
 * $Id: monitube2-mgmt_conn.c 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file monitube2-mgmt_conn.c
 * @brief Relating to managing the connections
 *
 * These functions and types will manage the connections.
 */

#include <sync/common.h>
#include <limits.h>
#include <jnx/pconn.h>
#include <sync/monitube2_ipc.h>
#include "monitube2-mgmt_config.h"
#include "monitube2-mgmt_conn.h"
#include "monitube2-mgmt_logging.h"

#include MONITUBE2_OUT_H

/*** Constants ***/

#define MONITUBE_MGMT_SERVER_MAX_CONN 30  ///< max # cnxs (to service component)

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
    uint16_t     fpc_slot;      ///< PIC info: where msg is going
    uint16_t     pic_slot;      ///< PIC info: where msg is going
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
                   void * cookie UNUSED)
{
    int rc;
    struct in_addr peer_addr;
    pconn_peer_info_t info;
    session_t * tmp_session;
    ss_info_t * ssi;
    list_item_t * item;
    rule_t * rule;
    address_t * a;

    rc = pconn_session_get_peer_info(session, &info);

    if(rc != PCONN_OK) {
        LOG(LOG_ERR, "%s: Cannot retrieve peer info "
            "for session. Error: %d", __func__, rc);

        pconn_session_close(session);
        return;
    }
    
    INSIST_ERR(info.ppi_peer_type == PCONN_PEER_TYPE_PIC);
    
    rc = pconn_get_peer_address(&info, &peer_addr.s_addr);

    if(rc != PCONN_OK) {
        LOG(LOG_ERR, "%s: Cannot retrieve peer address "
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

        tmp_session = calloc(1, sizeof(session_t));
        INSIST_ERR(tmp_session != NULL);
        
        tmp_session->session = session;
        TAILQ_INSERT_TAIL(&session_list, tmp_session, entries);

        // Reset and send all configuration to the service (PIC) component
        
        empty_notifications_queue(); // start empty

        // Go over conf and add it to queue of messages
        // only service sets using this ms pic: FPC#+PIC#
        
        ssi = next_serviceset(NULL);
        while(ssi != NULL) {
            if (ssi->fpc_slot != info.ppi_fpc_slot || 
                    ssi->pic_slot != info.ppi_pic_slot) {
                ssi = next_serviceset(ssi);
                continue;
            }
            // else service set is applied on this same PIC
            
            item = TAILQ_FIRST(&ssi->rules);
            while(item != NULL) {
                rule = (rule_t *)item->item;
                
                notify_config_rule(rule->name, rule->rate, rule->redirect,
                                        ssi->fpc_slot, ssi->pic_slot);
                
                a = next_address(rule, NULL);
                while(a != NULL) {
                    notify_config_rule_prefix(rule->name, a->address, a->mask,
                                           false, ssi->fpc_slot, ssi->pic_slot);
                    a = next_address(rule, a);
                }
                
                if (ssi->id) { // if still 0 we haven't got ssrb info
                    notify_apply_rule(rule->name, ssi->id, ssi->gen_num, 
                            ssi->svc_id, ssi->fpc_slot, ssi->pic_slot);
                }
                item = TAILQ_NEXT(item, entries);
            }
            
            ssi = next_serviceset(ssi);
        }

        process_notifications(); // send it

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

        LOG(LOG_ERR, "%s: Received an unknown event", __func__);

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
    
    LOG(LOG_ERR, "%s: Cannot find an existing peer session to shutdown",
            __func__);
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
                void * cookie UNUSED)
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
        INSIST_ERR(msg->length == sizeof(flow_stat_t));

        // carefully convert to the double it really is
        data->mdi_df = ntohq(data->mdi_df);
        tmp = &data->mdi_df;
        df = *(double *)tmp;

        set_flow_stat(info.ppi_fpc_slot, info.ppi_pic_slot, ntohs(data->ss_id),
            data->flow_addr, ntohs(data->flow_port), df, ntohl(data->mdi_mlr));

        break;

    default:

        LOG(LOG_ERR,
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
        LOG(LOG_ERR, "%s: Failed to initialize the pconn server on port %d.",
            __func__, MONITUBE_PORT_NUM);
        return EFAIL;
    }

    LOG(LOG_INFO, "%s: Successfully initialized the pconn server on port %d.",
            __func__, MONITUBE_PORT_NUM);

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
 * Enqueue a message to go to the data component to delete all policy
 * 
 * Message relayed to all PICs
 */
void
notify_delete_all_policy(void)
{
    notification_msg_t * msg;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST_ERR(msg != NULL);

    msg->action = MSG_DELETE_ALL;
    msg->fpc_slot = USHRT_MAX; // broadcast to all

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
                         uint16_t pic_slot)
{
    notification_msg_t * msg;
    del_ss_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST_ERR(msg != NULL);

    msg->action = MSG_DELETE_SS;
    msg->fpc_slot = fpc_slot;
    msg->pic_slot = pic_slot;

    msg->message_len = sizeof(del_ss_t);

    msg->message = data = calloc(1, msg->message_len);
    INSIST_ERR(data != NULL);
    data->ss_id = htons(ss_id);
    data->gen_num = htonl(gen_num);
    data->svc_id = htonl(svc_id);

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
                  uint16_t pic_slot)
{
    notification_msg_t * msg;
    apply_rule_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST_ERR(msg != NULL);

    msg->action = MSG_APPLY_RULE;
    msg->fpc_slot = fpc_slot;
    msg->pic_slot = pic_slot;

    msg->message_len = sizeof(apply_rule_t) + strlen(rule_name) + 1;

    msg->message = data = calloc(1, msg->message_len);
    INSIST_ERR(data != NULL);
    data->ss_id = htons(ss_id);
    data->gen_num = htonl(gen_num);
    data->svc_id = htonl(svc_id);    
    data->rule_name_len = htons(strlen(rule_name) + 1);
    strcpy(data->rule_name, rule_name);

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
                   uint16_t pic_slot)
{
    notification_msg_t * msg;
    apply_rule_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST_ERR(msg != NULL);

    msg->action = MSG_REMOVE_RULE;
    msg->fpc_slot = fpc_slot;
    msg->pic_slot = pic_slot;

    msg->message_len = sizeof(apply_rule_t) + strlen(rule_name) + 1;

    msg->message = data = calloc(1, msg->message_len);
    INSIST_ERR(data != NULL);
    data->ss_id = htons(ss_id);
    data->gen_num = htonl(gen_num);
    data->svc_id = htonl(svc_id);
    data->rule_name_len = htons(strlen(rule_name) + 1);
    strcpy(data->rule_name, rule_name);

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
                   uint16_t pic_slot)
{
    notification_msg_t * msg;
    update_rule_action_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST_ERR(msg != NULL);

    msg->action = MSG_CONF_RULE_ACTION;
    msg->fpc_slot = fpc_slot;
    msg->pic_slot = pic_slot;

    msg->message_len = sizeof(update_rule_action_t) + strlen(rule_name) + 1;

    msg->message = data = calloc(1, msg->message_len);
    INSIST_ERR(data != NULL);
    data->rate = htonl(rate);
    data->redirect = redirect; // already network byte order   
    data->rule_name_len = htons(strlen(rule_name) + 1);
    strcpy(data->rule_name, rule_name);

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
                   uint16_t pic_slot)
{
    notification_msg_t * msg;
    delete_rule_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST_ERR(msg != NULL);

    msg->action = MSG_DELETE_RULE;
    msg->fpc_slot = fpc_slot;
    msg->pic_slot = pic_slot;

    msg->message_len = sizeof(delete_rule_t) + strlen(rule_name) + 1;

    msg->message = data = calloc(1, msg->message_len);
    INSIST_ERR(data != NULL);   
    data->rule_name_len = htons(strlen(rule_name) + 1);
    strcpy(data->rule_name, rule_name);

    TAILQ_INSERT_TAIL(&notification_msgs, msg, entries);

    junos_trace(MONITUBE_TRACEFLAG_CONNECTION,
        "%s: Enqueueing message for the data component", __func__);
}


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
                          uint16_t pic_slot)
{
    notification_msg_t * msg;
    rule_addr_t * data;

    if(TAILQ_FIRST(&session_list) == NULL)
        return;

    msg = calloc(1, sizeof(notification_msg_t));
    INSIST_ERR(msg != NULL);

    if (delete) {
        msg->action = MSG_DELETE_RULE_MATCH_ADDR;
    } else {
        msg->action = MSG_CONF_RULE_MATCH_ADDR;
    }
    msg->fpc_slot = fpc_slot;
    msg->pic_slot = pic_slot;

    msg->message_len = sizeof(rule_addr_t) + strlen(rule_name) + 1;

    msg->message = data = calloc(1, msg->message_len);
    INSIST_ERR(data != NULL);
    data->addr = addr; // already network byte order
    data->mask = mask; // already network byte order   
    data->rule_name_len = htons(strlen(rule_name) + 1);
    strcpy(data->rule_name, rule_name);

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

        // send to sessions with matching fpc & pic info
        
        session = TAILQ_FIRST(&session_list);
        while(session != NULL) {
            pconn_session_get_peer_info(session->session, &info);
            
            if(msg->fpc_slot == USHRT_MAX ||
                (msg->fpc_slot == info.ppi_fpc_slot &&
                    msg->pic_slot == info.ppi_pic_slot)) {
            
                if(pconn_server_send(session->session, msg->action,
                        msg->message, msg->message_len) != PCONN_OK) {
    
                    LOG(LOG_ERR, "%s: Failed to send configuration message (%d)"
                            "to ms-%d/%d/0.", __func__, msg->action,
                            info.ppi_fpc_slot, info.ppi_pic_slot);
                }
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


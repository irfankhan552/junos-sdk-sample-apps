/*
 * $Id: equilibrium2-mgmt_server.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium2-mgmt_server.c
 * @brief The manager server to keep track of all services and query status.
 *
 * These functions will create the server, handle client messages and query
 * service status.
 */

#include <sync/equilibrium2.h>
#include "equilibrium2-mgmt.h"

#include <stdlib.h>
#include <string.h>
#include <jnx/pconn.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>

#include EQUILIBRIUM2_OUT_H

static pconn_server_t *server_hdl;
static client_head_t client_head;

/**
 * @brief
 * Get a client.
 *
 * @param[in] session
 *      Client session
 *
 * @return
 *      Pointer to the client data on success, NULL on failure
 */
static client_t *
client_get (pconn_session_t *session)
{
    client_t *client;

    LIST_FOREACH(client, &client_head, entry) {
        if (client->session == session) {
            return client;
        }
    }
    return NULL;
}

/**
 * @brief
 * Add a client.
 *
 * @param[in] session
 *      Client session
 */
static int
client_add (pconn_session_t *session)
{
    client_t *client;

    if (client_get(session)) {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Client exists!", __func__);
        return -1;
    }

    client = calloc(1, sizeof(client_t));
    INSIST_ERR(client != NULL);
    client->session = session;
    pconn_session_get_peer_info(session, &client->info);
    LIST_INSERT_HEAD(&client_head, client, entry);
    return 0;
}

/**
 * @brief
 * Delete a clinet.
 *
 * @param[in] session
 *      Client session
 */
static void
client_del (pconn_session_t *session)
{
    client_t *client;

    client = client_get(session);
    if (client) {
        LIST_REMOVE(client, entry);
        if (client->msg) {
            free(client->msg);
        }
        free(client);
    } else {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Client doesn't exist!", __func__);
    }
}

/**
 * @brief
 * Server event handler.
 */
static void
server_event_hdlr (pconn_session_t *session, pconn_event_t event,
        void *cookie __unused)
{
    switch (event) {
    case PCONN_EVENT_ESTABLISHED:
        /* A client is connected. */
        EQ2_TRACE(EQ2_TRACEFLAG_NORMAL, "%s: Client 0x%x is connected.",
                __func__, session);
        client_add(session);
        break;
    case PCONN_EVENT_SHUTDOWN:
        /* A client is dwon. */
        EQ2_TRACE(EQ2_TRACEFLAG_NORMAL, "%s: Client 0x%x is gone.",
                __func__, session);
        client_del(session);
        break;
    default:
        EQ2_LOG(TRACE_LOG_ERR, "%s: Invalid event %d!", __func__, event);
    }
}

/**
 * @brief
 * Server message handler.
 */
static status_t
server_msg_hdlr (pconn_session_t *session, ipc_msg_t *msg,
        void *cookie __unused)
{
    client_t *client;

    if (msg->subtype != EQ2_BALANCE_MSG_SVR_GROUP) {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Unrecoganized message type!", __func__);
        return -1;
    }
    client = client_get(session);
    if (client == NULL) {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Client doesn't exist!", __func__);
        return -1;
    }

    /* Free old message. */
    if (client->msg) {
        free(client->msg);
    }

    EQ2_TRACE(EQ2_TRACEFLAG_NORMAL, "%s: Received message len %d.",
            __func__, msg->length);
    client->msg = malloc(msg->length);
    INSIST_ERR(client->msg != NULL);
    bcopy(msg->data, client->msg, msg->length);
    return 0;
}

/**
 * @brief
 * Get the next client.
 *
 * @param[in] client
 *      Client
 *
 * @return
 *      Pointer to the next client on success, NULL on failure
 */
client_t *
client_get_next (client_t *client)
{
    if (client == NULL) {
        return LIST_FIRST(&client_head);
    } else {
        return LIST_NEXT(client, entry);
    }
}

/**
 * @brief
 * Open the management server.
 */
int
server_open (evContext ctx)
{
    pconn_server_params_t param;

    LIST_INIT(&client_head);

    bzero(&param, sizeof(param));
    param.pconn_port = EQ2_MGMT_SERVER_PORT;
    param.pconn_event_handler = server_event_hdlr;

    server_hdl = pconn_server_create(&param, ctx, server_msg_hdlr, NULL);
    if (server_hdl == NULL) {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Create server ERROR!", __func__);
        return -1;
    } else {
        EQ2_TRACE(EQ2_TRACEFLAG_NORMAL, "%s: Create server OK.", __func__);
        return 0;
    }
}

/**
 * @brief
 * Close the management server.
 */
void
server_close (void)
{
    pconn_server_shutdown(server_hdl);
}


/*
 * $Id: ipprobe-manager.c 366969 2010-03-09 15:30:13Z taoliu $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 *
 */

#include "ipprobe-manager.h"

/**
 * @file ipprobe-manager.c
 * @brief IP Probe manager functions
 *
 * This file contains the functions to manage the following components:
 * - probe manager,
 * - probe responder,
 * - probe authentication.
 *
 * The probe manager is a TCP server running on the probe target.
 * It starts a TCP session when received and accepted the connection
 * request from the probe initiator.
 *
 * After received request from initiator to start probe, the probe manager
 * first sends probe user information to the authentication server. The
 * request will be refused if authentication failed. Otherwise, the probe
 * manager will create a responder for the probe.
 *
 * The responder receives probe packet, puts timestamp into the packet and
 * send it back to the initiator.
 *
 * When probe is done, the initiator will close the connection to the probe
 * manager which will close the responder and the session as well.
 *
 */

/*** Data Structures ***/
/** The global event context for the probe manager */
extern evContext manager_ev_ctx;

/** The data structure for the probe manager. */
static manager_t manager;

/** The list header of responder(s). */
static LIST_HEAD(, responder_s)  responder_list;

/** The responder buffer to receive probe packet. */
static u_char responder_buf[PROBE_PACKET_SIZE_MAX];

/** The session handle to authd. */
static junos_aaa_session_hdl_t auth_session = NULL;

/*** STATIC/INTERNAL Functions ***/

/**
 * Close the responder.
 *
 * @param[in] responder
 *      Pointer to the responder data structure
 */
static void
responder_close (responder_t *responder)
{
    if (responder == NULL) {
        return;
    }

    /* Decrement responder use count. */
    if (--responder->rspd_use > 0) {
        /* It's still used by some other session(s). */
        return;
    }

    if (evTestID(responder->rspd_read_fd)) {
        evDeselectFD(responder->rspd_ev_ctx, responder->rspd_read_fd);
        evInitID(&responder->rspd_read_fd);
    }

    /* Close responder socket. */
    if (responder->rspd_socket >= 0) {
        close(responder->rspd_socket);
    }

    /* Remove it from the responder list. */
    if (responder->rspd_entry.le_prev != NULL) {
        LIST_REMOVE(responder, rspd_entry);
    }
    free(responder);

    junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
            "%s: Responder closed.", __func__);
    return;
}

/**
 * Close the session and the associated responder.
 *
 * @param[in] session
 *      Pointer to the session data structure.
 */
static void
session_close (session_t *session)
{
    if (session == NULL) {
        return;
    }
    if (evTestID(session->ss_read_fd)) {
        evDeselectFD(session->ss_ev_ctx, session->ss_read_fd);
        evInitID(&session->ss_read_fd);
    }
    if (session->ss_pipe != NULL) {
        ipc_pipe_detach_socket(session->ss_pipe, session->ss_socket);
        ipc_pipe_destroy(session->ss_pipe);
    }
    if (session->ss_socket >= 0) {
        close(session->ss_socket);
    }
    if (session->ss_entry.le_prev != NULL) {
        LIST_REMOVE(session, ss_entry);
    }
    responder_close(session->ss_responder);
    free(session);

    junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
            "%s: Session closed.", __func__);
    return;
}

/**
 * Responder probe packet handler.
 *
 * @param[in] ev_ctx
 *      Event context
 *
 * @param[in] uap
 *      Pointer to user data
 *
 * @param[in] fd
 *      File descriptor
 *
 * @param[in] eventmask
 *      Event mask
 */
static void
responder_packet_handler (evContext ev_ctx UNUSED, void *uap,
        int fd UNUSED, int eventmask UNUSED)
{
    responder_t     *responder = ((responder_t *)uap);
    probe_packet_t  *packet = (probe_packet_t *)responder_buf;
    struct sockaddr_in  src_addr;
    probe_packet_data_t *data;
    struct timeval rx_time;
    int recv_len = 0;
    int send_len = 0;
    int addr_len;

    while (1) {
        /* Something came in, recode receive time. */
        gettimeofday(&rx_time, NULL);
        addr_len = sizeof(src_addr);
        recv_len = recvfrom(responder->rspd_socket, responder_buf,
                PROBE_PACKET_SIZE_MAX, 0, (struct sockaddr *)&src_addr,
                &addr_len);

        if (recv_len == 0) {
            break;
        }
        if (recv_len < 0) {
            if(errno != EAGAIN) {
                ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                        "%s ERROR %d: Receive probe packet!",
                        __func__, errno);
            }
            break;
        }

        if (recv_len < PROBE_PACKET_SIZE_MIN) {
            ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                    "%s ERROR: Receive probe packet size! size: %d",
                    __func__, recv_len);
            break;
        }

        if (responder->rspd_protocol == IPPROTO_UDP) {
            data = (probe_packet_data_t *)responder_buf;
        }  else {
            data = &packet->data;
            packet->header.ip_src.s_addr = 0;
            packet->header.ip_dst.s_addr = src_addr.sin_addr.s_addr;
        }

        if (data->type != PROBE_PACKET_REQ) {
            ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                    "%s ERROR: Not request packet! type: %d",
                    __func__, data->type);
            break;
        }
        /* Packet has be validated, copy receive time into packet */
        bcopy(&rx_time, &data->target_rx_time, sizeof(struct timeval));

        junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
                "%s: Packet len: %d, src_addr: %s, src_port: %d.",
                __func__, recv_len, inet_ntoa(src_addr.sin_addr),
                ntohs(src_addr.sin_port));

        data->type = PROBE_PACKET_REPLY;
        gettimeofday(&data->target_tx_time, NULL);

        send_len = sendto(responder->rspd_socket, responder_buf,
                recv_len, 0, (struct sockaddr *)&src_addr, addr_len);
        if (send_len < 0) {
            ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                    "%s ERROR %d: Send probe packet!", __func__, errno);
            break;
        }
    }
    return;
}

/**
 * Create a responder as requested.
 *
 * @param[in] protocol
 *      IP protocol
 *
 * @param[in] session
 *      Pointer to the connected session
 *
 * @return 0 on success, -1 on failure
 */
static int
responder_create (void *data, session_t *session)
{
    responder_t *responder = LIST_FIRST(&responder_list);
    msg_start_t *msg_data = (msg_start_t *)data;
    struct sockaddr_in addr;
    const int on = 1;

    /* Look for responder with the same protocol and port. */
    while (responder) {
        if ((responder->rspd_protocol == msg_data->protocol)
                && (responder->rspd_port == msg_data->port)) {
            responder->rspd_use++;
            return 0;
        }
        responder = LIST_NEXT(responder, rspd_entry);
    }

    /* Create the new responder. */
    responder = calloc(1, sizeof(responder_t));
    INSIST_ERR(responder != NULL);

    responder->rspd_protocol = msg_data->protocol;
    responder->rspd_port = msg_data->port;
    responder->rspd_ev_ctx = manager_ev_ctx;
    LIST_INSERT_HEAD(&responder_list, responder, rspd_entry);
    session->ss_responder = responder;

    if (responder->rspd_protocol == IPPROTO_UDP) {
        responder->rspd_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (responder->rspd_socket < 0) {
            ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                    "%s ERROR %d: Create UDP responder!",
                    __func__, errno);
            goto error;
        }

        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(responder->rspd_port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(responder->rspd_socket, (struct sockaddr *)&addr,
                sizeof(addr)) < 0) {
            ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                    "%s ERROR %d: Bind socket!", __func__, errno);
            goto error;
        }
    } else {
        responder->rspd_socket = socket(PF_INET, SOCK_RAW,
                responder->rspd_protocol);
        if (responder->rspd_socket < 0) {
            ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                    "%s ERROR %d: Create responder!", __func__, errno);
            goto error;
        }

        if (setsockopt(responder->rspd_socket, IPPROTO_IP, IP_HDRINCL,
                &on, sizeof(on)) < 0) {
            ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                    "%s ERROR %d: Set socket option!", __func__, errno);
            goto error;
        }
    }

    if (evSelectFD(responder->rspd_ev_ctx, responder->rspd_socket, EV_READ,
            responder_packet_handler, responder, &responder->rspd_read_fd)
            < 0) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: evSelectFD!", __func__);
        goto error;
    }

    junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
            "%s: Responder on protocol %d port %d opened.",
            __func__, responder->rspd_protocol, responder->rspd_port);
    return 0;

error:
    responder_close(responder);
    return -1;
}

/**
 * Send message.
 *
 * @param[in] msg_pipe
 *      Pointer to the message pipe
 *
 * @param[in] msg_type
 *      Message sub-type
 *
 * @param[in] data
 *      Pointer to the message data
 *
 * @param[in] len
 *      Message data length
 */
static void
manager_send_msg (ipc_pipe_t *msg_pipe, msg_type_e msg_type,
        void *data, int len)
{
    ipc_msg_t reply;

    reply.type = PROBE_MANAGER_MSG_TYPE;
    reply.subtype = msg_type;
    reply.opcode = 0;
    reply.error = 0;
    reply.length = len;

    if (ipc_msg_write(msg_pipe, &reply, data)) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Write message!", __func__);
    } else if(ipc_pipe_write(msg_pipe, NULL)) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Write pipe!", __func__);
    } else {
        junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
                "%s: Message sent out, type: %d.", __func__, msg_type);
    }
    ipc_pipe_write_clean(msg_pipe);

    return;
}

/**
 * The manager session message handler.
 *
 * @param[in] ev_ctx
 *      Event context
 *
 * @param[in] uap
 *      Pointer to the user data
 *
 * @param[in] fd
 *      File descriptor
 *
 * @param[in] eventmask
 *      Event mask
 */
static void
manager_session_msg_handler (evContext ev_ctx UNUSED, void *uap,
        int fd UNUSED, int eventmask)
{
    session_t *session = (session_t *)uap;
    ipc_msg_t *msg;
    u_char *data;

    if (eventmask & EV_EXCEPT) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Received execption!", __func__);
        goto error;
    }

    if (ipc_pipe_read(session->ss_pipe) < 0) {
        if (errno == EAGAIN) {
            return;
        }
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR %d: Read from socket!", __func__, errno);
        goto error;
    }

    while ((msg = ipc_msg_read(session->ss_pipe)) != NULL) {
        if (msg->type != PROBE_MANAGER_MSG_TYPE) {
            ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                    "%s ERROR: Message type %d!", __func__, msg->type);
            continue;
        }
        data = (u_char *)msg->data;
        switch (msg->subtype) {
        case PROBE_MANAGER_MSG_START:
            if (!responder_create(data, session)) {
                manager_send_msg(session->ss_pipe, PROBE_MANAGER_MSG_ACK,
                        NULL, 0);
            }
            break;
        }
    }

    ipc_pipe_read_clean(session->ss_pipe);
    return;

error:
    session_close(session);
    return;
}

/**
 * Create a session for the new connection.
 *
 * @param[in] ev_ctx
 *      Event context
 *
 * @param[in] uap
 *      Pointer to user data
 *
 * @param[in] socket
 *      The socket for the new connection
 *
 * @param[in] la
 *      Local address
 *
 * @param[in] lalen
 *      The length of local address
 *
 * @param[in] ra
 *      Remote address
 *
 * @param[in] ralen
 *      The length of remote address
 */
static void
manager_session_accept (evContext ev_ctx, void *uap UNUSED,
        int session_socket, const void *la UNUSED, int lalen UNUSED,
        const void *ra UNUSED, int ralen UNUSED)
{
    session_t *session = NULL;

    /* Create a session for the new connection. */
    session = calloc(1, sizeof(session_t));
    INSIST_ERR(session != NULL);

    session->ss_ev_ctx = ev_ctx;
    session->ss_socket = session_socket;

    /* Create pipe for the session. */
    session->ss_pipe = ipc_pipe_create(PROBE_MANAGER_PIPE_SIZE);
    if (session->ss_pipe == NULL) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Create session pipe!", __func__);
        goto error;
    }

    LIST_INSERT_HEAD(&manager.mngr_ss_list, session, ss_entry);

    if (ipc_pipe_attach_socket(session->ss_pipe, session->ss_socket)) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Attach session pipe!", __func__);
        goto error;
    }

    if (evSelectFD(ev_ctx, session->ss_socket, EV_READ | EV_EXCEPT,
            manager_session_msg_handler, session, &session->ss_read_fd) < 0) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: evSelectFD!", __func__);
        goto error;
    }

    junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
            "%s: New session is created.", __func__);
    return;

error:
    session_close(session);
    return;
}

/**
 * aaa session event handler
 *
 * @param[in] hdl
 *      aaa client session handle
 *
 * @param[in] msg
 *      aaa message
 *
 */
static void
auth_msg_hdlr (junos_aaa_session_hdl_t hdl UNUSED, junos_aaa_msg_t *msg)
{
    /* Under construction */
    junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
            "%s: msg_type: %d", __func__, msg->msg_type);
    return;
}


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Close the connection to authd.
 */
void
manager_auth_close (void)
{
    if (auth_session != NULL) {
        junos_aaa_session_close(auth_session);
        auth_session = NULL;
    }
    return;
}

/**
 * Open connection to authd.
 *
 * @return 0 on success, -1 on failure
 */
int
manager_auth_open (void)
{
    
    if (junos_aaa_session_open(manager_ev_ctx, &auth_session,
                               auth_msg_hdlr) < 0) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Allocate authd session handle!", __func__);
        return -1;
    }

    junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
            "%s: Connected to authd.", __func__);
    return 0;
}

/**
 * Close the manager and all sessions.
 */
void
manager_close (void)
{
    session_t *cur, *next;

    if (manager.mngr_socket >= 0) {
        if (evTestID(manager.mngr_conn_id)) {
            evCancelConn(manager_ev_ctx, manager.mngr_conn_id);
        }
        close(manager.mngr_socket);
        manager.mngr_socket = -1;
    }

    cur = LIST_FIRST(&manager.mngr_ss_list);
    while (cur) {
        next = LIST_NEXT(cur, ss_entry);
        session_close(cur);
        cur = next;
    }

    junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
            "%s: Manager closed.", __func__);
    return;
}

/**
 * Intializes the manager
 */
void
manager_init (void)
{
    manager.mngr_socket = -1;
    manager.mngr_port = 0;
    LIST_INIT(&manager.mngr_ss_list);
    LIST_INIT(&responder_list);
    return;
}

/**
 * Open the manager
 *
 * @param[in] port
 *      Manager TCP port
 *
 * @return 0 on success, -1 on failure
 */
int
manager_open (u_short port)
{
    struct sockaddr_in addr;

    if (manager.mngr_port == port) {
        return 0;
    } else if (manager.mngr_socket >= 0) {
      manager_close();
    }

    manager.mngr_port = port;
    manager.mngr_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (manager.mngr_socket < 0) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: Open manager socket!", __func__);
        goto error;
    }

    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(manager.mngr_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(manager.mngr_socket, (struct sockaddr *)&addr, sizeof(addr))
            < 0) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR %d: Bind socket!", __func__, errno);
        goto error;
    }

    evInitID(&manager.mngr_conn_id);
    if (evListen(manager_ev_ctx, manager.mngr_socket, MAX_MANAGER_CONNS,
            manager_session_accept, &manager, &manager.mngr_conn_id)) {
        ERRMSG(PROBE_MANAGER, TRACE_LOG_ERR,
                "%s ERROR: evListen!", __func__);
        goto error;
    }

    junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
            "%s: The manager is opened, port: %d.",
            __func__, manager.mngr_port);
    return 0;

error:
    if (manager.mngr_socket >= 0) {
        close(manager.mngr_socket);
    }
    return -1;
}

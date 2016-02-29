/*
 * $Id: ipsnooper_server.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ipsnooper_server.c
 * @brief IP Snooper server functions.
 *
 * These functions manage client connection and communication.
 * The main server process is scheduled to run any available control CPU.
 * After a client connection setup, server process will find out which
 * control CPU was assigned to handle the flow for this client.
 *
 * If there is already a session thread running on that control CPU,
 * the server process will send the new session info to that thread by the
 * pipe.
 *
 * Otherwise, the server process creates a session thread and binds it to
 * that control CPU. A pipe between the server process and the session
 * thread will be created too.
 *
 * The session thread has its own event context and listens to the pipe
 * and all session sockets. Once it receives a request from a client, it
 * will process and reply it.
 *
 * When the session is closed by client, the session thread will remove
 * it from its listen list and notify server process to clean it up.
 * The session thread will stay once it was created, even all sessions are
 * closed.
 *
 */

#include "ipsnooper.h"

extern ssn_thrd_t ssn_thrd[MSP_MAX_CPUS];

static server_t server; /**< the server data */

/*** STATIC/INTERNAL Functions ***/

/**
 * Close a client session in session thread.
 * This is done in session thread context.
 *
 * @param[in] ssn
 *      Pointer to the session data
 */
static void
session_thread_session_close (ssn_t *ssn)
{
    if (ssn == NULL) {
        return;
    }

    /* Remove session read event. */
    if (evTestID(ssn->ssn_read_fid)) {
        evDeselectFD(ssn->ssn_thrd->ssn_thrd_ev_ctx, ssn->ssn_read_fid);
    }
    close(ssn->ssn_socket);

    /* Remove session from the list. */
    LIST_REMOVE(ssn, ssn_entry);
    free(ssn);

    logging(LOG_INFO, "%s: Client session closed.", __func__);
    return;
}

/**
 * Close a session thread prepare to exit.
 * This is done in session thread context or thread signal context.
 *
 * @param[in] session
 *      Pointer to the session data
 */
static void
session_thread_close (ssn_thrd_t *thrd)
{
    ssn_t *ssn;

    if (thrd == NULL) {
        return;
    }

    /* Cut off thread data first, so packet threads won't bother it. */
    atomic_and_int(0, &thrd->ssn_thrd_ready);

    /* Close all sessions. */
    while ((ssn = LIST_FIRST(&thrd->ssn_thrd_ssn_list))) {
        session_thread_session_close(ssn);
    }

    /* Remove registered pipe event. */
    evDeselectFD(thrd->ssn_thrd_ev_ctx, thrd->ssn_thrd_srv_pipe_read_fid);
    evDeselectFD(thrd->ssn_thrd_ev_ctx, thrd->ssn_thrd_pkt_pipe_read_fid);

    evDestroy(thrd->ssn_thrd_ev_ctx);

    /* Close server pipe and packet pipe. */
    close(thrd->ssn_thrd_srv_pipe[PIPE_READ]);
    close(thrd->ssn_thrd_srv_pipe[PIPE_WRITE]);
    close(thrd->ssn_thrd_pkt_pipe[PIPE_READ]);
    close(thrd->ssn_thrd_pkt_pipe[PIPE_WRITE]);

    return;
}

/**
 * Close a session thread with given control CPU number.
 *
 * This is done in server process context. It sends SIGQUIT signal to
 * the session thread.
 *
 * All session thread resource, like pipe, socket, etc., will be
 * cleared by session thread.
 *
 * @param[in] thrd
 *      Pointer to session thread data
 */
static void
server_session_thread_close (ssn_thrd_t *thrd)
{

    if (thrd->ssn_thrd_tid == NULL) {
        return;
    }

    logging(LOG_INFO, "%s: Kill session thread %d...",
                    __func__, thrd->ssn_thrd_cpu);
    pthread_kill(thrd->ssn_thrd_tid, SIGQUIT);

    /* Wait till the thread was killed. */
    if (pthread_join(thrd->ssn_thrd_tid, NULL) == 0) {
        logging(LOG_INFO, "%s: Session thread %d was killed.",
                __func__, thrd->ssn_thrd_cpu);
    } else {
        logging(LOG_ERR, "%s: Session thread %d was NOT killed! %d",
                __func__, thrd->ssn_thrd_cpu, errno);
    }
    thrd->ssn_thrd_tid = NULL;

    return;
}

/**
 * Process client message.
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
session_thread_client_msg_hdlr (evContext ev_ctx UNUSED, void *uap,
        int fd, int eventmask)
{
    ssn_t *session = (ssn_t *)uap;
    int len;
    char buf[MAX_LINE_BUF_SIZE];

    if (eventmask & EV_EXCEPT) {
        logging(LOG_ERR, "%s: Read from client exception!", __func__);
        goto error;
    }

    len = recv(fd, buf, MAX_LINE_BUF_SIZE - 1, 0);

    if (len < 0) {
        logging(LOG_ERR, "%s: Read from client ERROR! err: %d",
                __func__, errno);
        goto error;
    } else if (len == 0) {
        logging(LOG_INFO, "%s: Client closed.", __func__);
        goto error;
    }

    /* Don't care the request for now. */

    return;

error:
    session_thread_session_close(session);
    return;
}

/**
 * Process pipe message from packet thread.
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
session_thread_pkt_msg_hdlr (evContext ev_ctx UNUSED, void *uap,
        int fd, int eventmask UNUSED)
{
    ssn_thrd_t *thrd = (ssn_thrd_t *)uap;
    ssn_t *ssn = NULL;
    char buf[MAX_LINE_BUF_SIZE + 1];
    char src_str[IPV4_ADDR_STR_LEN];
    char dst_str[IPV4_ADDR_STR_LEN];
    int len = 0;

    /* Read from the pipe and write to pkt_msg_offset in pkt_msg. */
    len = read(fd, ((char *)&thrd->ssn_thrd_pkt_msg) +
            thrd->ssn_thrd_pkt_msg_offset,
            sizeof(pkt_msg_t) - thrd->ssn_thrd_pkt_msg_offset);

    if (len <= 0) {

        /* Error or EOF, reset offset. */
        thrd->ssn_thrd_pkt_msg_offset = 0;
        logging(LOG_ERR, "%s: Read server pipe ERROR!", __func__);
    } else if (len < (int)(sizeof(pkt_msg_t) -
            thrd->ssn_thrd_pkt_msg_offset)) {

        /* Didn't get the whole message yet. */
        thrd->ssn_thrd_pkt_msg_offset += len;
    } else {

        logging(LOG_INFO, "%s: Got message from packet thread.",
                __func__);

        /* Send packet info to all connected clients. */
        strncpy(src_str, inet_ntoa(thrd->ssn_thrd_pkt_msg.pkt_msg_src_addr),
                IPV4_ADDR_STR_LEN);
        strncpy(dst_str, inet_ntoa(thrd->ssn_thrd_pkt_msg.pkt_msg_dst_addr),
                IPV4_ADDR_STR_LEN);
        ssn = LIST_FIRST(&thrd->ssn_thrd_ssn_list);
        while (ssn) {
            snprintf(buf, MAX_LINE_BUF_SIZE, "proto: %d, %s -> %s\n",
                    thrd->ssn_thrd_pkt_msg.pkt_msg_proto,
                    src_str, dst_str);
            write(ssn->ssn_socket, buf, strlen(buf));
            ssn = LIST_NEXT(ssn, ssn_entry);
        }

        /* Reset offset. */
        thrd->ssn_thrd_pkt_msg_offset = 0;
    }
    return;
}

/**
 * Process pipe message from server process.
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
session_thread_srv_msg_hdlr (evContext ev_ctx UNUSED, void *uap,
        int fd, int eventmask)
{
    ssn_thrd_t *thrd = (ssn_thrd_t *)uap;
    ssn_t *ssn;
    int len = 0;

    if (eventmask & EV_EXCEPT) {
        logging(LOG_ERR, "%s: Read server pipe exception!", __func__);
        return;
    }

    /* Read from the pipe and write to srv_msg_offset in srv_msg. */
    len = read(fd, ((char *)&thrd->ssn_thrd_srv_msg) +
            thrd->ssn_thrd_srv_msg_offset,
            sizeof(srv_msg_t) - thrd->ssn_thrd_srv_msg_offset);

    if (len <= 0) {

        /* Error or EOF, reset offset. */
        thrd->ssn_thrd_srv_msg_offset = 0;
        logging(LOG_ERR, "%s: Read server pipe ERROR!", __func__);
    } else if (len < (int)(sizeof(srv_msg_t) -
            thrd->ssn_thrd_srv_msg_offset)) {

        /* Didn't get the whole message yet. */
        thrd->ssn_thrd_srv_msg_offset += len;
    } else {

        logging(LOG_INFO, "%s: Received message from server.", __func__);

        /* Process received message. */
        if (thrd->ssn_thrd_srv_msg.srv_msg_op == MSG_OP_ADD) {

            /* Create a session. */
            ssn = (ssn_t *)calloc(1, sizeof(ssn_t));
            INSIST_ERR(ssn != NULL);

            ssn->ssn_thrd = thrd;
            ssn->ssn_socket = thrd->ssn_thrd_srv_msg.srv_msg_ssn_socket;
            evInitID(&ssn->ssn_read_fid);

            /* Add session to the list. */
            LIST_INSERT_HEAD(&thrd->ssn_thrd_ssn_list, ssn, ssn_entry);

            /* Add session socket to event list. */
            if (evSelectFD(thrd->ssn_thrd_ev_ctx, ssn->ssn_socket,
                    EV_READ | EV_EXCEPT, session_thread_client_msg_hdlr,
                    ssn, &ssn->ssn_read_fid) < 0) {
                logging(LOG_ERR, "%s: Add session socket to event ERROR!",
                        __func__);
            }
            logging(LOG_INFO, "%s: New client session was added.",
                    __func__);
        }

        /* Reset offset. */
        thrd->ssn_thrd_srv_msg_offset = 0;
    }
    return;
}

/**
 * The session thread function entry
 *
 * @param[in] thrd
 *      Pointer to session thread data structure
 *
 */
static void
session_thread_entry (ssn_thrd_t *thrd)
{
    uint8_t cpu = thrd->ssn_thrd_cpu;
    evEvent event;
    sigset_t sig_mask;

    /* Unblock SIGQUIT to session thread. */
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGQUIT);
    pthread_sigmask(SIG_UNBLOCK, &sig_mask, NULL);

    /* Create event context for this session thread. */
    if (evCreate(&thrd->ssn_thrd_ev_ctx) < 0) {
        logging(LOG_ERR, "%s: %d Create event context ERROR!",
                __func__, cpu);
        goto exit;
    }
    
    /* Add server pipe to event list. */
    if (evSelectFD(thrd->ssn_thrd_ev_ctx, thrd->ssn_thrd_srv_pipe[PIPE_READ],
            EV_READ | EV_EXCEPT, session_thread_srv_msg_hdlr, thrd,
            &thrd->ssn_thrd_srv_pipe_read_fid) < 0) {
        logging(LOG_ERR, "%s: %d Add server pipe to event ERROR!",
                __func__, cpu);
        goto exit;
    }

    /* Add packet pipe to event list. */
    if (evSelectFD(thrd->ssn_thrd_ev_ctx, thrd->ssn_thrd_pkt_pipe[PIPE_READ],
            EV_READ | EV_EXCEPT, session_thread_pkt_msg_hdlr, thrd,
            &thrd->ssn_thrd_srv_pipe_read_fid) < 0) {
        logging(LOG_ERR, "%s: %d Add packet pipe to event ERROR!",
                __func__, cpu);
        goto exit;
    }

    /* The session thread is ready to receive info from data threads. */
    atomic_or_int(1, &thrd->ssn_thrd_ready);
    logging(LOG_INFO, "%s: The session thread is ready on CPU %d.",
            __func__, cpu);

    /* Waiting for SIGQUIT to exit. */
    while (1) {
        if (evGetNext(thrd->ssn_thrd_ev_ctx, &event, EV_WAIT) < 0) {
            break;
        }
        if (evDispatch(thrd->ssn_thrd_ev_ctx, event) < 0) {
            break;
        }
    }

exit:
    session_thread_close(thrd);
    return;
}

/**
 * Create a session thread and bind it to the control CPU
 *
 * @param[in] cpu
 *      Control CPU number
 *
 * @return 0 on success, -1 on failure
 */
static int
session_thread_create (uint8_t cpu)
{
    pthread_attr_t attr;
    ssn_thrd_t *thrd = &ssn_thrd[cpu];

    /* Initialize the session thread data structure. */
    LIST_INIT(&thrd->ssn_thrd_ssn_list);
    thrd->ssn_thrd_cpu = cpu;
    thrd->ssn_thrd_tid = NULL;
    thrd->ssn_thrd_ready = 0;
    thrd->ssn_thrd_srv_pipe[PIPE_READ] = -1;
    thrd->ssn_thrd_srv_pipe[PIPE_WRITE] = -1;
    thrd->ssn_thrd_pkt_pipe[PIPE_READ] = -1;
    thrd->ssn_thrd_pkt_pipe[PIPE_WRITE] = -1;
    thrd->ssn_thrd_srv_msg_offset = 0;
    thrd->ssn_thrd_pkt_msg_offset = 0;
    evInitID(&thrd->ssn_thrd_srv_pipe_read_fid);
    evInitID(&thrd->ssn_thrd_pkt_pipe_read_fid);
    msp_spinlock_init(&thrd->ssn_thrd_pipe_lock);

    /* Create server pipe for the session thread. */
    if (pipe(thrd->ssn_thrd_srv_pipe) < 0) {
        logging(LOG_ERR, "%s: Create server pipe ERROR!", __func__);
        goto error;
    }

    /* Create packet pipe for the session thread. */
    if (pipe(thrd->ssn_thrd_pkt_pipe) < 0) {
        logging(LOG_ERR, "%s: Create thread pipe ERROR!", __func__);
        goto error;
    }

    /* Create session thread and bind it to control CPU. */
    pthread_attr_init(&attr);
    if (pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)) {
        logging(LOG_ERR,
                "%s: pthread_attr_setscope() on CPU %d ERROR!",
                __func__, cpu);
        goto error;
    }
    if (pthread_attr_setcpuaffinity_np(&attr, cpu)) {
        logging(LOG_ERR,
                "%s: pthread_attr_setcpuaffinity_np() on CPU %d ERROR!",
                __func__, cpu);
        goto error;
    }
    if (pthread_create(&thrd->ssn_thrd_tid, &attr,
            (void *)&session_thread_entry, thrd)) {
        logging(LOG_ERR,
                "%s: pthread_create() on CPU %d ERROR!",
                __func__, cpu);
        goto error;
    }

    return 0;

error:
    server_session_thread_close(thrd);
    return -1;
}

/**
 * Create a session for the new connection.
 *
 * @param[in] ctx
 *      Event context
 *
 * @param[in] uap
 *      Pointer to user data
 *
 * @param[in] sock
 *      The socket for the new connection
 *
 * @param[in] la
 *      Local address
 *
 * @param[in] lalen
 *      The length of local address
 *
 * @param[in] ra
 *      Peer address
 *
 * @param[in] ralen
 *      The length of peer address
 */
static void
server_accept (evContext ctx UNUSED, void *uap UNUSED, int sock,
        const void *la, int lalen, const void *ra, int ralen)
{
    struct sockaddr_in local_addr, peer_addr;
    msp_ip_hash_selector_t sel;
    srv_msg_t msg;
    uint8_t cpu = 0;
    char src_str[IPV4_ADDR_STR_LEN];
    char dst_str[IPV4_ADDR_STR_LEN];


    /* Support IPv4 only. */
    if ((lalen != sizeof(struct sockaddr_in))
            || (ralen != sizeof(struct sockaddr_in))) {
        logging(LOG_ERR, "%s: Address ERROR!", __func__);
        return;
    }

    bcopy(la, &local_addr, sizeof(struct sockaddr_in));
    bcopy(ra, &peer_addr, sizeof(struct sockaddr_in));
    strncpy(src_str, inet_ntoa(peer_addr.sin_addr), IPV4_ADDR_STR_LEN);
    strncpy(dst_str, inet_ntoa(local_addr.sin_addr), IPV4_ADDR_STR_LEN);

    /* Initialize the selector. */
    bzero(&sel, sizeof(sel));
    sel.sa_family = MSP_AF_INET;
    sel.proto = IPPROTO_TCP;
    sel.l3_hdr.v4.src_addr = peer_addr.sin_addr.s_addr;
    sel.l3_hdr.v4.dst_addr = local_addr.sin_addr.s_addr;
    sel.l3_hdr.v4.l4_info.tcp_udp.src_port = peer_addr.sin_port;
    sel.l3_hdr.v4.l4_info.tcp_udp.dst_port = local_addr.sin_port;

    /* Get control CPU number. */
    if (msp_cpu_affinity(&sel, &cpu) != MSP_OK) {
        logging(LOG_ERR, "%s: Get CPU affinity ERROR!", __func__);
        goto error;
    }
    logging(LOG_INFO, "%s: New session %s:%u -> %s:%u on CPU %d",
            __func__, src_str, ntohs(peer_addr.sin_port),
            dst_str, ntohs(local_addr.sin_port), cpu);

    /* Create the session thread on control CPU if it doesn't exist. */
    if (ssn_thrd[cpu].ssn_thrd_tid == NULL) {
        if (session_thread_create(cpu) < 0) {
            goto error;
        }
    }

    /* Notify session thread to add new session. */
    msg.srv_msg_op = MSG_OP_ADD;
    msg.srv_msg_ssn_socket = sock;
    write(ssn_thrd[cpu].ssn_thrd_srv_pipe[PIPE_WRITE], &msg,
            sizeof(srv_msg_t));

    return;

error:
    close(sock);
    return;
}

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Session thread exit.
 *
 * @param[in] thrd
 *      Pointer to the session thread data
 */
void
session_thread_exit (ssn_thrd_t *thrd)
{
    session_thread_close(thrd);

    logging(LOG_INFO, "%s: Session thread %d exit.",
            __func__, thrd->ssn_thrd_cpu);

    pthread_exit(NULL);
}

/**
 * Close the server only.
 */
void
server_close (void)
{
    int cpu;

    /* Remove server connect event. */
    if (evTestID(server.srv_conn_id)) {
        evCancelConn(server.srv_ev_ctx, server.srv_conn_id);
    }

    /* Close server socket. */
    if (server.srv_socket >= 0) {
        close(server.srv_socket);
        server.srv_socket = -1;
    }

    /* Close all running threads. */
    for (cpu = 0; cpu < MSP_MAX_CPUS; cpu++) {
        server_session_thread_close(&ssn_thrd[cpu]);
    }

    return;
}

/**
 * Initialize the server
 *
 * @param[in] ctx
 *      Event context
 *
 * @return 0 on success, -1 on failure
 */
int
server_init (evContext ctx)
{
    struct sockaddr_in addr;
    int opt = 1;

    /* Initialize server data structure. */
    bzero(&server, sizeof(server));
    server.srv_ev_ctx = ctx;
    server.srv_socket = -1;

    bzero(ssn_thrd, sizeof(ssn_thrd));

    /* Create server socket. */
    server.srv_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (server.srv_socket < 0) {
        logging(LOG_ERR, "%s: Create socket ERROR! errno: %d",
                __func__, errno);
        goto error;
    }

    if (setsockopt(server.srv_socket, SOL_SOCKET, SO_REUSEADDR, &opt,
            sizeof(opt))) {
        logging(LOG_ERR, "%s: Set REUSEADDR socket option ERROR! errno: %d",
                __func__, errno);
        goto error;
    }

    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(IPSNOOPER_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(server.srv_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        logging(LOG_ERR, "%s: Bind socket ERROR! errno: %d",
               __func__, errno);
        goto error;
    }

    if (evListen(server.srv_ev_ctx, server.srv_socket, IPSNOOPER_MAX_CONNS,
            server_accept, NULL, &server.srv_conn_id)) {
        logging(LOG_ERR, "%s: Listen to socket ERROR!", __func__);
        goto error;
    }

    logging(LOG_INFO, "%s: Server is ready on %d.",
            __func__, IPSNOOPER_PORT);
    return 0;

error:
    server_close();
    return -1;
}


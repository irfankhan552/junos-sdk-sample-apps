/*
 * $Id: psd_server.c 366969 2010-03-09 15:30:13Z taoliu $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */


/**
 * @file psd_server.c
 * @brief Interface and implementation for the policy server
 * 
 * Functions to initialize the server functionality and lookup a policy
 */
#include <sync/common.h>
#include <sync/psd_ipc.h>
#include <fnmatch.h>
#include "psd_config.h"
#include "psd_server.h"
#include "psd_logging.h"

#include PS_OUT_H

/*** Constants ***/


#define MAX_IPC_CONN        64        ///< maximum number of connections
#define MAX_IPC_WAIT_CONN   3         ///< maximum number of connections waiting

/**
 * @brief Send buffer size in bytes for any pipes / sockets
 */
#define BUFFER_SIZE         1024


/*** Define'd helpers ***/

/**
 * @brief initialize the evFileID
 */
#ifndef evInitID
#define evInitID(id) ((id)->opaque = NULL)
#endif

/**
 * @brief Test if the evFileID has been initialized
 */
#ifndef evTestID
#define evTestID(id) ((id).opaque != NULL)
#endif


/*** Data Structures ***/

/**
 * Connection information bundle
 */
typedef struct psd_ipc_conn_s {
    ipc_pipe_t * pipe;       ///< ipc pipe to pic
    int          socket;     ///< socket to the pic
    evFileID     id;         ///< event file id associated with the connection
} psd_ipc_conn_t;


static psd_ipc_conn_t * connect_table[MAX_IPC_CONN]; ///< client connections

/**
 * Connection ID for the server socket accepting new connections
 */
static evConnID server_conn_id;

/**
 * Event context for psd
 */
static evContext psd_ctx;


/*** STATIC/INTERNAL Functions ***/


/**
 * Initialize connect table.
 */
static void
connect_table_init(void)
{
    int i;

    for(i = 0; i < MAX_IPC_CONN; i++) {
        connect_table[i] = NULL;
    }
}


/**
 * Add connection to the table.
 *
 * @param[in] conn
 *     Pointer to connection data.
 *
 * @return
 *      TRUE if successful, otherwise FALSE if table is full.
 */
static boolean
connect_table_add(psd_ipc_conn_t *conn)
{
    int i;

    for(i = 0; i < MAX_IPC_CONN; ++i) {
        if(connect_table[i] == NULL) {
            connect_table[i] = conn;
            return TRUE;
        }
    }
    return FALSE;
}


/**
 * Remove connection from the table. Caller should free connection.
 *
 * @param[in] conn
 *     Pointer to connection data.
 *
 * @return
 *      TRUE if successful, otherwise FALSE if didn't find the connection.
 *
 */
static boolean
connect_table_del(psd_ipc_conn_t *conn)
{
    int i;

    for(i = 0; i < MAX_IPC_CONN; ++i) {
        if(connect_table[i] == conn) {
            connect_table[i] = NULL;
            return TRUE;
        }
    }
    return FALSE;
}


/**
 * Shutdown / close pipe associated with this connection struct (socket)
 * 
 * @param[in] conn
 *     The connection to close
 */
static void
psd_ipc_shutdown_conn(psd_ipc_conn_t *conn)
{
    junos_trace(PSD_TRACEFLAG_NORMAL, "%s", __func__);

    if(evTestID(conn->id)) { // if select failed then don't deselect
        if(evDeselectFD(psd_ctx, conn->id) < 0) {  // Detach event.
            ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: evDeselect FAILED!", __func__);
        }
        evInitID(&conn->id);
    }
    
    ipc_pipe_detach_socket(conn->pipe, conn->socket);  // Detach pipe.
    
    if (conn->pipe != NULL)
        ipc_pipe_destroy(conn->pipe);  // Close pipe.
    
    close(conn->socket);  // Close socket to this client
    
    connect_table_del(conn);  // Remove connection from the table.
    
    free(conn);
}


/**
 * Find the first known policy that matches from the set of 
 * configured expressions given interface name and address family.
 * 
 * @param[in] req
 *      Policy request message from client.
 *
 * @return
 *      The policy matching request, otherwise NULL.
 */
static psd_policy_t *
lookup_policy(policy_req_msg_t *req)
{
    psd_policy_t *policy;

    junos_trace(PSD_TRACEFLAG_NORMAL, "%s", __func__);

    policy = first_policy();
    while(policy != NULL) {
        if((fnmatch(policy->ifname, req->ifname, 0) == 0) &&
                (policy->af == req->af)) {
            break;
        }
        policy = next_policy(policy);
    }
    return policy;
}


/**
 * Send message to client.
 *
 * @param[in] ipc_pipe
 *     Pointer to the pipe.
 * 
 * @param[in] msg_type
 *     Message type.
 * 
 * @param[in] data
 *     Pointer to the message data.
 * 
 * @param[in] len
 *     Message length.
 */
static void 
send_msg(ipc_pipe_t *ipc_pipe, msg_type_e msg_type, void *data, int len)
{
    ipc_msg_t reply;

    reply.type = IPC_MSG_TYPE_PSD;
    reply.subtype = msg_type;
    reply.opcode = 0;
    reply.error = 0;
    reply.length = len;

    if(ipc_msg_write(ipc_pipe, &reply, data)) {
        ERRMSG(PSD, TRACE_LOG_ERR,
                  "%s: Write message FAILED!", __func__);
        
    } else if(ipc_pipe_write(ipc_pipe, NULL)) {
        ERRMSG(PSD, TRACE_LOG_ERR,
                  "%s: Write pipe FAILED!", __func__);
    }
    
    ipc_pipe_write_clean(ipc_pipe);
}


/**
 * A callback for evSelectFD registered in psd_ipc_connect, this function
 * accepts an incoming message (it reads) from the existing connection.
 * 
 * @param[in] ctx
 *     The eventlib context
 * 
 * @param[in] uap
 *     The user data registered to be passed to this callback function. In our
 *     case this is the connection structure built in psd_ipc_connect that
 *     contains the IPC pipe.
 * 
 * @param[in] sockfd
 *     The socket's file descriptor
 * 
 * @param[in] eventmask
 *     A bit mask of events that generated the select to unblock and call this 
 *     function. In our case we only registered to listen to reads and
 *     exceptions so we know this is a read or exception.
 */
static void
psd_ipc_dispatch (evContext ctx __unused,
                      void * uap,
                      int sockfd __unused,
                      int eventmask)
{
    psd_ipc_conn_t *conn = (psd_ipc_conn_t *)uap;
    ipc_msg_t *msg;
    psd_policy_t *policy;
    psd_policy_route_t *route;
    policy_filter_msg_t fmsg;
    policy_route_msg_t rmsg;

    INSIST(conn != NULL);

    if(eventmask & EV_EXCEPT) { // check if it's an exception
        // We received out-of-band data
        ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: closing connection due to client exception", __func__);
        psd_ipc_shutdown_conn(conn);
        return;
    }

    // Otherwise it's a read
    if(ipc_pipe_read(conn->pipe) < 0) {
        /*
         * When errno is EAGAIN don't shutdown the connection.
         * EAGAIN is received when only partial msg is read.
         * So bail out and try later.
         */
        if(errno == EAGAIN || errno == EINTR) //ignore
            return;

        if(errno != 0) { // problem
            ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: Null ipc read causing connection shutdown due to: %m",
                __func__);

            // if cannot read anything, shutdown the connection
            psd_ipc_shutdown_conn(conn);
            
        } else {
            junos_trace(PSD_TRACEFLAG_NORMAL,
                "%s: Remote side of the pipe closed", __func__);

            // if cannot read anything, shutdown the connection
            psd_ipc_shutdown_conn(conn);
        }
        return;
    }

    while((msg = ipc_msg_read(conn->pipe)) != NULL) {

        /* if we got a null message, complain and break out */
        if (msg && msg->type == 0 && msg->subtype == 0 &&
            msg->length == 0 && msg->opcode == 0 && msg->error == 0) {

            ERRMSG(PSD, TRACE_LOG_ERR,
                      "%s: Received null msg", __func__);
            break;
        }

        //sanity checks; we just complain but still proceed.
        if(msg->type != IPC_MSG_TYPE_PSD) {
            ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: Unexpected message type %d", __func__, msg->type);
            continue;
        }

        switch(msg->subtype) {
            case MSG_POLICY_REQ:
                junos_trace(PSD_TRACEFLAG_NORMAL,
                    "%s: Got policy request.", __func__);
                policy = lookup_policy((policy_req_msg_t *)msg->data);
                if(policy) {
                    if(policy->filter) {
                        junos_trace(PSD_TRACEFLAG_NORMAL,
                            "%s: Send filter.", __func__);
                        
                        // copy the template msg from the policy 
                        // into the local msg here (except ifname)
                        memcpy(((char *)&fmsg) + sizeof(fmsg.ifname),
                            ((char *)&policy->filter->filter_data) +
                                 sizeof(fmsg.ifname),
                            sizeof(policy_filter_msg_t) - sizeof(fmsg.ifname));

                        // copy the interface name we received in the reqest
                        // into the reply message                            
                        bzero(fmsg.ifname, sizeof(fmsg.ifname));
                        strcpy(fmsg.ifname,
                            ((policy_req_msg_t *)msg->data)->ifname);
                        
                        //send
                        send_msg(conn->pipe, MSG_FILTER, &fmsg,
                            sizeof(policy_filter_msg_t));
                    }
                    if(policy->route) {
                        route = policy->route;
                        while(route) {
                            junos_trace(PSD_TRACEFLAG_NORMAL,
                                "%s: Send route.", __func__);
                        
                            // copy the template msg from the policy 
                            // into the local msg here (except ifname)        
                            memcpy(((char *)&rmsg) + sizeof(rmsg.ifname),
                                ((char *)&route->route_data) +
                                    sizeof(rmsg.ifname),
                                sizeof(policy_route_msg_t) -
                                    sizeof(rmsg.ifname));

                            // copy the interface name we received in the reqest
                            // into the reply message
                            bzero(rmsg.ifname, sizeof(rmsg.ifname));
                            strcpy(rmsg.ifname,
                                ((policy_req_msg_t *)msg->data)->ifname);
                            
                            //send
                            send_msg(conn->pipe, MSG_ROUTE, &rmsg,
                                    sizeof(policy_route_msg_t));
                            route = route->next;
                        }
                    }
                } else {
                    junos_trace(PSD_TRACEFLAG_NORMAL,
                            "%s: Send no policy.", __func__);
                    // No policy, echo policy request message.
                    send_msg(conn->pipe, MSG_POLICY_NA, msg->data,
                            sizeof(policy_req_msg_t));
                }
                break;
            case MSG_HB:
                junos_trace(PSD_TRACEFLAG_HB,
                        "%s: Got heartbeat and reply.", __func__);
                // Echo heart-beat message, no message data.
                send_msg(conn->pipe, MSG_HB, NULL, 0);
                break;
            case MSG_UPDATE_DONE:
                junos_trace(PSD_TRACEFLAG_NORMAL,
                        "%s: Got update done and reply.", __func__);
                // Echo update-done message, no message data.
                send_msg(conn->pipe, MSG_UPDATE_DONE, NULL, 0);
                break;
            default:
                ERRMSG(PSD, TRACE_LOG_ERR,
                        "%s: Unexpected message subtype %d",
                         __func__, msg->subtype);
        }
    }
    ipc_pipe_read_clean(conn->pipe);
}

/**
 * A callback for evListen registered in server_init, this function
 * accepts an incoming connection, builds the connection structure that
 * holds the pipe, socket and evFileID, and lastly registers the callback
 * function psd_ipc_dispatch for subsequent reads (of requests).
 * 
 * @param[in] ctx
 *     The eventlib context
 * 
 * @param[in] uap
 *     The user data registered to be passed to this callback function
 * 
 * @param[in] sockfd
 *     The socket's file descriptor
 * 
 * @param[in] la
 *     The local address (needs typecasting to) (const struct sockaddr_in *)
 * 
 * @param[in] lalen
 *     The local address structure's length in bytes
 * 
 * @param[in] ra
 *     The remote address (needs typecasting to) (const struct sockaddr_in *)
 * 
 * @param[in] ralen
 *     The remote address structure's length in bytes
 * 
 */
static void
psd_ipc_connect (evContext ctx __unused, void * uap __unused, int sockfd,
                     const void * la, int lalen, // local addr from getsockname
                     const void * ra, int ralen) // remote addr from getpeername
{
    psd_ipc_conn_t * conn;
    const struct sockaddr_in * laddr, * raddr;

    junos_trace(PSD_TRACEFLAG_NORMAL, "%s", __func__);

    laddr = (const struct sockaddr_in *)la;
    raddr = (const struct sockaddr_in *)ra;

    // check they are inet addresses of course
    INSIST(laddr != NULL && sizeof(struct sockaddr_in) == lalen);
    INSIST(raddr != NULL && sizeof(struct sockaddr_in) == ralen);

    junos_trace(PSD_TRACEFLAG_NORMAL,
            "%s: Received connection from %s:%d", 
             __func__, inet_ntoa(raddr->sin_addr), ntohs(raddr->sin_port));

    // create the connection structure
    conn = malloc(sizeof(*conn));
    INSIST(conn != NULL);
    bzero(conn, sizeof(*conn));

    // fill out the connection args
    conn->socket = sockfd;
    conn->pipe = ipc_pipe_create(BUFFER_SIZE);
    evInitID(&conn->id);

    // attach socket with pipe
    ipc_pipe_attach_socket(conn->pipe, conn->socket);
    connect_table_add(conn);
    
    if(evSelectFD(psd_ctx, sockfd, EV_READ | EV_EXCEPT,
                    psd_ipc_dispatch, conn, &conn->id) < 0) {
        ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: evSelectFD: %m", __func__);

        psd_ipc_shutdown_conn(conn);
    }
    // How do we shutdown in the normal disconnect case: We get a EV_READ
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the server socket and start listening for new connections.
 * It binds to inet0 and listens on port PSD_PORT_NUM.
 * It also calls evListen which registers psd_ipc_connect as the callback
 * function to accept new incoming connections.
 * 
 * @param[in] ctx
 *      Eventlib context for this application.
 * 
 * @return
 *      SUCCESS if successful, or EFAIL if failed.
 */
int
server_init (evContext ctx)
{
    struct sockaddr_in addr;
    int sock = -1;
    const int sndbuf = BUFFER_SIZE;
    const int on = 1; // turn option on
    const int port = PSD_PORT_NUM;

    junos_trace(PSD_TRACEFLAG_NORMAL, "%s", __func__);

    psd_ctx = ctx; // save context
    connect_table_init();

    // Create socket and set options
    if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: Create socket FAILED!", __func__);
        return EFAIL;
    }

    // Set some socket options
    if(setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof (on))) {
        close(sock);
        ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: Set socket option SO_REUSEADDR FAILED!", __func__);
        return EFAIL;
    }
    if(setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf))) {
        close(sock);
        ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: Set socket option SO_SNDBUF FAILED!", __func__);
        return EFAIL;
    }

    if(vrf_setsocketbyvrfname(sock, "__juniper_private2__", NULL)) {
        close(sock);
        ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: Set socket to VRF FAILED!", __func__);
        return EFAIL;
    }

    // bind socket to address:
    bzero(&addr, sizeof(addr));
    addr.sin_len = sizeof (addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if(bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(sock);
        ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: Bind socket FAILED!", __func__);
        return EFAIL;
    }

    // listen for connection events.
    junos_trace(PSD_TRACEFLAG_NORMAL,
            "%s: policy server bound and listening on 0.0.0.0:%d",
             __func__, port);
             
    evInitID(&server_conn_id);
    if(evListen(psd_ctx, sock, MAX_IPC_WAIT_CONN, psd_ipc_connect,
                NULL, &server_conn_id) < 0) { // no user data
        // NOTE: should unlink socket
        close(sock);
        ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: evListen FAILED!", __func__);
        return EFAIL;
    }
    
    return SUCCESS;   
}


/**
 * Notify all currently connected clients to update policies.
 *
 */
void
notify_all_clients(void)
{
    int i;

    for(i = 0; i < MAX_IPC_CONN; i++) {
        if(connect_table[i]) {
            junos_trace(PSD_TRACEFLAG_NORMAL, "%s: %d", __func__, i);
            send_msg(connect_table[i]->pipe, MSG_POLICY_UPDATE, NULL, 0);
        }
    }
}


/**
 * Shutdown the server socket that accecpts new connections
 * (only used when stopping/restarting the daemon)
 * 
 * @return
 *      SUCCESS if the server connection id wasn't set because 
 *      the initial call to listen failed, or if it was, then if 
 *      we successfully stop listening for connections; or
 *      EFAIL if we were listening, but the attempt to stop 
 *      listening for connections failed.
 */
int
server_shutdown(void)
{
    int i;
    
    junos_trace(PSD_TRACEFLAG_NORMAL, "%s", __func__);
    
    // Stop listening for connections

    // if listen failed then don't cancel
    if(evTestID(server_conn_id)) {
        
        // This should stop accepting and close the server socket
        
        if(evCancelConn(psd_ctx, server_conn_id) < 0) {
            ERRMSG(PSD, TRACE_LOG_ERR,
                "%s: Unable to stop listening for connections: %s",
                    __func__, strerror(errno));
            return EFAIL;
        }

        // need to call this after closing the connection
        evInitID(&server_conn_id);
    }

    // Get rid of any remaining connections to clients
    
    for(i = 0; i < MAX_IPC_CONN; ++i) {
        if(connect_table[i] != NULL) {
            psd_ipc_shutdown_conn(connect_table[i]);
        }
    }

    return SUCCESS;   
}

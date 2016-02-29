/*
 * $Id: ipsnooper.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ipsnooper.h
 * @brief Related to the IP Snooper application.
 *
 */

#ifndef __IPSNOOPER_H__
#define __IPSNOOPER_H__

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <isc/eventlib.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <jnx/jnx_types.h>
#include <jnx/aux_types.h>
#include <jnx/mpsdk.h>
#include <jnx/msp_hash.h>
#include <jnx/atomic.h>
#include <jnx/msp_locks.h>
#include <jnx/trace.h>
#include <sys/jnx/jbuf.h>

/** The TCP port for IP Snooper server. */
#define IPSNOOPER_PORT          48008

/** The maximum concurrent incomming connections to IP Snooper server. */
#define IPSNOOPER_MAX_CONNS     16

/** The maximum buffer size for each line of client request. */
#define MAX_LINE_BUF_SIZE       128

#define IPV4_ADDR_STR_LEN       16  /**< max length of IPv4 address string */
#define IPV4_ADDR_LEN           4   /**< length of IPv4 address in byte */
#define PACKET_LOOP_SEND_RETRY  100 /**< retry times to send packet */

#define PIPE_READ               0   /**< pipe read FD index */
#define PIPE_WRITE              1   /**< pipe write FD index */

/** Define an INSIST/assert with logging. */
#ifdef INSIST_ERR
#undef INSIST_ERR
#endif

#define INSIST_ERR(c) if (!(c)) \
    logging(LOG_EMERG, "%s:%d: insist '%s' ERROR: %m", \
        __FILE__, __LINE__, #c); else (void)NULL

/** Operation for server pipe message. */
typedef enum {
    MSG_OP_ADD = 0, /**< add a new session */
    MSG_OP_DEL      /**< delete en existing session */
} srv_msg_op_e;

/** Server pipe message. */
typedef struct srv_msg_s {
    srv_msg_op_e    srv_msg_op;         /**< operation */
    int             srv_msg_ssn_socket; /**< session socket */
} srv_msg_t;

/** Packet pipe message. */
typedef struct pkt_msg_s {
    uint8_t        pkt_msg_proto;      /**< IP protocol */
    struct in_addr  pkt_msg_src_addr;   /**< Source IP address */
    struct in_addr  pkt_msg_dst_addr;   /**< Destination IP address */
} pkt_msg_t;

/** Packet thread data structure. */
typedef struct pkt_thrd_s {
    int                 pkt_thrd_cpu;       /**< packet CPU number */
    pthread_t           pkt_thrd_tid;       /**< packet thread ID */
    msp_data_handle_t   pkt_thrd_data_hdl;  /**< data handle */
} pkt_thrd_t;


/**
 * Session thread data structure, one for each thread running
 * a control CPU.
 * Each thread could handle multiple client sessions whoes flows
 * are bound to the same control CPU.
 */
typedef struct ssn_thrd_s {
    LIST_HEAD(, ssn_s)  ssn_thrd_ssn_list;  /**< list of thread session */
    evContext           ssn_thrd_ev_ctx;    /**< event context */
    pthread_t           ssn_thrd_tid;       /**< thread ID pointer */
    atomic_int_t        ssn_thrd_ready;     /**< thread ready flag */
    uint8_t             ssn_thrd_cpu;
                        /**< CPU number on which thread runs */
    int                 ssn_thrd_srv_pipe[2];
                        /**< pipe for server process to write */
    int                 ssn_thrd_pkt_pipe[2];
                        /**< pipe for packet thread to write */
    evFileID            ssn_thrd_srv_pipe_read_fid;
                        /**< event file ID to read server pipe */
    evFileID            ssn_thrd_pkt_pipe_read_fid;
                        /**< event file ID to read packet pipe */
    msp_spinlock_t      ssn_thrd_pipe_lock;
                        /**< lock for accessing thread pipes */
    srv_msg_t           ssn_thrd_srv_msg;   /**< Server message buffer */
    int                 ssn_thrd_srv_msg_offset;
                        /**< offset of server message buffer */
    pkt_msg_t           ssn_thrd_pkt_msg;   /**< Packet message buffer */
    int                 ssn_thrd_pkt_msg_offset;
                        /**< offset of packet message buffer */
} ssn_thrd_t;

/** Session data structure. */
typedef struct ssn_s {
    LIST_ENTRY(ssn_s)   ssn_entry;      /**< entry of session list */
    ssn_thrd_t          *ssn_thrd;      /**< pointer to the thread */
    int                 ssn_socket;     /**< session socket */
    evFileID            ssn_read_fid;   /**< event file ID */
} ssn_t;

/** IP Snooper server data structure. */
typedef struct server_s {
    evContext           srv_ev_ctx;  /**< event context */
    evConnID            srv_conn_id;  /**< event connect ID */
    int                 srv_socket;  /** server socket */
} server_t;

/**
 * Initialize packet loops
 *
 * @return MSP_OK on success, MSP error code on failure
 */
int packet_loop_init (void);

/**
 * Packet thread exit.
 *
 * @param[in] thrd
 *      Pointer to the packet thread data
 */
void packet_thread_exit (pkt_thrd_t *thrd);

/**
 * Session thread exit.
 *
 * @param[in] thrd
 *      Pointer to the session thread data
 */
void session_thread_exit (ssn_thrd_t *thrd);

/**
 * Initialize the server
 *
 * @param[in] ctx
 *      Event context
 *
 * @return 0 on success, -1 on failure
 */
int server_init (evContext ctx);

/**
 * Close the server.
 */
void server_close (void);

#endif


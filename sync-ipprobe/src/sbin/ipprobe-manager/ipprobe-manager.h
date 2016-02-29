/*
 * $Id: ipprobe-manager.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ipprobe-manager.h
 * @brief The header file for the IP Probe manager
 *
 * This header file defines all macros and data structures for the
 * probe manager.
 *
 */

#ifndef __IPPROBE_MANAGER_H__
#define __IPPROBE_MANAGER_H__

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <netinet/udp.h>
#include <arpa/inet.h>
#include <time.h>

#include <sys/queue.h>
#include <isc/eventlib.h>
#include <ddl/ddl.h>
#include <jnx/aux_types.h>
#include <jnx/ipc.h>
#include <jnx/bits.h>
#include <jnx/trace.h>
#include <jnx/junos_init.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>
#include <jnx/pmon.h>
#include <jnx/junos_stat.h>
#include <jnx/junos_aaa.h>

#include IPPROBE_OUT_H

/** The length of IP header, in byte. */
#define IP_HEADER_LEN           20

/** The minimum and maximum size of probe packet. */
#define PROBE_PACKET_SIZE_MIN   80
#define PROBE_PACKET_SIZE_MAX   1024

/** The default TCP port for probe manager. */
#define DEFAULT_MANAGER_PORT    6060

/** The maximum concurrent connections to probe manager. */
#define MAX_MANAGER_CONNS       16

/** The probe manager IPC pipe size. */
#define PROBE_MANAGER_PIPE_SIZE 1024

/** The probe manager primary message type. */
#define PROBE_MANAGER_MSG_TYPE  2000

/** The maximum string length. */
#define MAX_STR_LEN             64

/**
 * The sub IPC message type.
 */
typedef enum {
    PROBE_MANAGER_MSG_ACK = 1,  /**< Acknowledge to the request */
    PROBE_MANAGER_MSG_START     /**< Request to start a probe */
} msg_type_e;

/**
 * The probe packet type.
 */
typedef enum {
    PROBE_PACKET_REQ = 1,   /**< Request (probe) packet to the target */
    PROBE_PACKET_REPLY      /**< Reply packet back to the initiator */
} packet_type_e;

/**
 * The request message to start a probe.
 */
typedef struct {
    u_char  protocol;    /**< The IP protocol */
    u_short port;       /**< The protocol port */
} msg_start_t;

/**
 * The responder data structure.
 */
typedef struct responder_s {
    LIST_ENTRY(responder_s) rspd_entry;      /**< Entry of the list */
    evContext               rspd_ev_ctx;     /**< Event context */
    evFileID                rspd_read_fd;    /**< Event read file ID */
    int                     rspd_socket;     /**< The socket */
    u_char                  rspd_protocol;   /**< IP protocol */
    u_short                 rspd_port;       /**< Protocol port */
    int                     rspd_use;        /**< Usage counter */
} responder_t;

/**
 * The probe initiator session data structure.
 */
typedef struct session_s {
    LIST_ENTRY(session_s)   ss_entry;       /**< Entry of the list */
    evContext               ss_ev_ctx;      /**< Event context */
    evFileID                ss_read_fd;     /**< Event read file ID */
    int                     ss_socket;      /**< The socket */
    ipc_pipe_t              *ss_pipe;       /**< Pointer to the message pipe */
    struct responder_s      *ss_responder;  /**< Pointer to the responder */
} session_t;

/**
 * The probe manager data structure.
 */
typedef struct manager_s {
    LIST_HEAD(, session_s)  mngr_ss_list;   /**< Head of the session list */
    evConnID                mngr_conn_id;   /**< Event connect ID */
    int                     mngr_socket;    /**< Manager socket */
    u_short                 mngr_port;      /**< Manager TCP port */
} manager_t;

/**
 * The data section of probe packet.
 */
typedef struct {
    u_short id;                     /**< Probe ID */
    u_short type;                   /**< Packet type */
    u_short seq;                    /**< Packet sequence number */
    struct timeval tx_time;         /**< Initiator transmit time */
    struct timeval target_rx_time;  /**< Target receive time */
    struct timeval target_tx_time;  /**< Target transmit time */
    struct timeval rx_time;         /**< Initiator receive time */
    char pad[0];                    /**< Pad section */
} probe_packet_data_t;

/**
 * The probe packet data structure.
 */
typedef struct {
    struct ip header;           /**< IP header */
    union {
        struct icmphdr icmp;    /**< ICMP header */
        struct udphdr udp;      /**< UDP header */
    };
    probe_packet_data_t data;   /**< Packet data section */
} probe_packet_t;


/**
 * Read daemon configuration from the database.
 *
 * @param[in] check
 *      1 if this function being invoked because of a commit check
 *
 * @return 0 on success, -1 on failure
 *
 * @note Do not use ERRMSG during config check.
 */
int manager_config_read(int check);

/**
 * Intializes the manager
 */
void manager_init(void);

/**
 * Open the manager
 *
 * @param[in] port
 *      Manager TCP port
 *
 * @return 0 on success, -1 on failure
 */
int manager_open(u_short port);

/**
 * Close the manager and all sessions.
 */
void manager_close(void);

/**
 * Open connection to authd.
 *
 * @return 0 on success, -1 on failure
 */
int manager_auth_open(void);

/**
 * Close the connection to authd.
 */
void manager_auth_close(void);

#endif

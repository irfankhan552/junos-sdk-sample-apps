/*
 * $Id: ipprobe-mt.h 366969 2010-03-09 15:30:13Z taoliu $
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
 * @file ipprobe-mt.h
 * @brief The header file for the IP Probe MT
 *
 * This header file defines all macros and data structures.
 *
 */

#ifndef __IPPROBE_MT_H__
#define __IPPROBE_MT_H__

#define IP_HEADER_LEN             20    /**< The IP header length, in byte */

#define PROBE_NAME_LEN            64    /**< The length of probe name */
#define PROBE_MNGR_PORT_DEFAULT   6068  /**< TCP port for probe manager */
#define RSPD_MNGR_PORT_DEFAULT    6069  /**< UDP port for responder manager */
#define CONN_RETRY_DEFAULT        32    /**< Default connect retry */

#define PROBE_MGMT_MSG_REG        1     /**< Probe client registration */
#define PROBE_MGMT_MSG_ADD_DST    2     /**< Add destination */

#define RSPD_MGMT_MSG_REQ         1     /**< Request responder */
#define RSPD_MGMT_MSG_ACK         2     /**< Request ACK */

#define PROBE_PKT_REQ             1     /**< Probe request packet */
#define PROBE_PKT_REPLY           2     /**< Probe reply packet */
#define PROBE_PKT_REQ_LAST        3     /**< The last probe request packet */

#define PROBE_PKT_SIZE_MIN        64    /**< The min. size of probe packet */
#define PROBE_PKT_SIZE_MAX        1024  /**< The max. size of probe packet */
#define PROBE_PKT_COUNT_MAX       200   /**< The max. count of probe packets */

#define PROBE_PKT_TTL_DEFAULT     128   /**< The default packet TTL */

/** The macro to log tracing message. */
#define PROBE_TRACE(_msg_type, _fmt, ...) \
    junos_trace((_msg_type), _fmt, ##__VA_ARGS__)

/** The macro to log syslog message. */
#define PROBE_LOG(_level, _fmt, ...)   \
        ERRMSG(IPPROBE-MT, (_level), _fmt, ##__VA_ARGS__)

/** The structure of probe parameters. */
typedef struct probe_params_s {
    LIST_ENTRY(probe_params_s)  entry;                /**< List entry */
    char                        name[PROBE_NAME_LEN]; /**< Probe name */
    uint8_t                     proto;                /**< IP protocol */
    uint8_t                     tos;                  /**< IP TOS bits */
    uint16_t                    src_port;             /**< Source port */
    uint16_t                    dst_port;             /**< Destination port */
    uint16_t                    pkt_size;             /**< Packet size */
    uint16_t                    pkt_count;            /**< Packet count */
    uint16_t                    pkt_interval;         /**< Packet interval */
} probe_params_t;

/** The structure of probe packet data. */
typedef struct {
    u_short               id;              /**< Probe ID */
    u_short               type;            /**< Packet type */
    u_short               seq;             /**< Packet sequence number */
    struct timeval        tx_time;         /**< Initiator transmit time */
    struct timeval        target_rx_time;  /**< Target receive time */
    struct timeval        target_tx_time;  /**< Target transmit time */
    struct timeval        rx_time;         /**< Initiator receive time */
    char                  pad[0];          /**< Pad section */
} probe_pkt_data_t;

/** The structure of probe packet. */
typedef struct {
    struct ip             header;          /**< IP header */
    union {
        struct icmphdr    icmp;            /**< ICMP header */
        struct udphdr     udp;             /**< UDP header */
    };
    probe_pkt_data_t      data;            /**< Packet data section */
} probe_pkt_t;

/** The structure of probe. */
typedef struct probe_s {
    LIST_ENTRY(probe_s)   entry;           /**< List entry */
    pthread_t             tid;             /**< Thread ID */
    evContext             ev_ctx;          /**< Event context for each probe
                                                thread */
    pconn_client_t        *client_hdl;     /**< Client handle used by probe
                                                thread */
    pconn_session_t       *ssn;            /**< Client session used by probe
                                                thread manager */
    probe_params_t        params;          /**< Probe parameters */
    int                   rspd_socket;     /**< Socket to responder manager */
    evFileID              rspd_read_fid;   /**< Read file ID for
                                                @c rspd_socket */
    in_addr_t             dst_first;       /**< The first destination */
    patroot               dst_pat;         /**< Root of destination patricia
                                                tree */
    int                   dst_count;       /**< Destination count */
    int                   tx_socket;       /**< The socket to send probe
                                                packets */
    evTimerID             tx_tid;          /**< Timer ID for sending probe
                                                packets */
    int                   tx_count;        /**< Tx packet count */
    uint8_t               *tx_buf;         /**< Tx packet buffer */
    int                   rx_socket;       /**< The socket to receive replied
                                                probe packets */
    evFileID              rx_fid;          /**< Read file ID for @c rx_socket */
} probe_t;

/** The structure of packet statistics. */
typedef struct {
    float             delay_sd;        /**< Source to destination delay */
    float             delay_ds;        /**< Destination to source delay */
    float             rrt;             /**< Round-trip time */
    float             jitter_sd;       /**< Source to destination jitter */
    float             jitter_ds;       /**< Destination to source jitter */
    float             jitter_rr;       /**< Round-trip jitter */
} pkt_stats_t;

/** The structure of probe result. */
typedef struct {
    float             delay_sd_average;   /**< Average src to dst delay */
    float             delay_ds_average;   /**< Average dst to src delay */
    float             delay_rr_average;   /**< Average round-trip delay */
    float             delay_sd_max;       /**< Maximum src to dst delay */
    float             delay_ds_max;       /**< Maximum dst to src delay */
    float             delay_rr_max;       /**< Maximum round-trip delay */
    float             jitter_sd_average;  /**< Average src to dst jitter */
    float             jitter_ds_average;  /**< Average dst to src jitter */
    float             jitter_rr_average;  /**< Average round-trip jitter */
    float             jitter_sd_max;      /**< Maximum src to dst jitter */
    float             jitter_ds_max;      /**< Maximum dst to src jitter */
    float             jitter_rr_max;      /**< Maximum round-trip jitter */
} probe_result_t;

/** The structure of Rx probe packet buffer. */
typedef struct probe_rx_buf_s {
    LIST_ENTRY(probe_rx_buf_s)  entry;       /**< List entry */
    uint8_t                     pkt[0];      /**< Packet data */
} probe_rx_buf_t;

/** The definitions of the state of probe. */
typedef enum {
    PROBE_DST_STATE_INIT,                  /**< Probe is initialized */
    PROBE_DST_STATE_RUN,                   /**< Probe is running */
    PROBE_DST_STATE_DONE                   /**< Probe is done */
} probe_dst_state_t;

/** The structure of probe destination. */
typedef struct {
    patnode               node;            /**< Node entry */
    in_addr_t             dst_addr;        /**< Destination address */
    in_addr_t             local_addr;      /**< Local address */
    probe_dst_state_t     state;           /**< State for this destination */
    probe_result_t        result;          /**< Probe result */
    int                   rx_count;        /**< Rx packet count */
    LIST_HEAD(, probe_rx_buf_s)  rx_buf_list;  /**< Rx buffer */
    probe_rx_buf_t        *rx_buf_last;    /**< Rx buffer */
} probe_dst_t;

/** The structure of responder management packet. */
typedef struct {
    uint16_t              type;            /**< Packet type */
    uint16_t              timeout;         /**< Probe timeout */
    uint16_t              port;            /**< Probe port */
    uint8_t               proto;           /**< Probe protocol */
} rspd_mgmt_pkt_t;

/** The structure of probe manager. */
typedef struct {
    evContext             ev_ctx;          /**< Event context */
    pconn_server_t        *hdl;            /**< Manager handle */
    uint16_t              port;            /**< Manager TCP port */
} probe_mngr_t;

/** The structure of responder manager. */
typedef struct {
    evContext             ev_ctx;          /**< Event context */
    int                   socket;          /**< Manager socket */
    evFileID              read_fid;        /**< Read file ID for @c socket */
    uint16_t              port;            /**< Manager UDP port */
    rspd_mgmt_pkt_t       rx_pkt;          /**< Rx management packet buffer */
} rspd_mngr_t;

/** The structure of responder. */
typedef struct rspd_s {
    LIST_ENTRY(rspd_s)    entry;           /**< List entry */
    evContext             ev_ctx;          /**< Event context */
    int                   socket;          /**< Responder socket */
    evFileID              read_fid;        /**< Read file ID for @ socket */
    uint8_t               proto;           /**< Responder protocol */
    uint16_t              port;            /**< Responder port */
    int                   usage;           /**< Usage count */
    int                   timeout;         /**< Timeout */
    uint8_t               rx_buf[PROBE_PKT_SIZE_MAX];  /**< Rx buffer */
} rspd_t;

int probe_mngr_init(evContext lev_ctx);
int probe_mngr_open(uint16_t port);
int probe_mngr_probe_start(char *name, in_addr_t dst_addr);
int probe_mngr_probe_stop(char *name);
int probe_mngr_probe_clear(char *name);
patroot *probe_mngr_probe_result_get(char *name);
void probe_mngr_close(void);

int rspd_mngr_init(evContext lev_ctx);
int rspd_mngr_open(uint16_t port);
void rspd_mngr_close(void);

int config_read(int check);
probe_params_t *probe_params_get(char *name);

void *probe_thrd_entry(probe_t *probe);

#endif


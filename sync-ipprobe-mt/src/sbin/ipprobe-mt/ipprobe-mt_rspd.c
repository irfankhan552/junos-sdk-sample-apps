/*
 * $Id: ipprobe-mt_rspd.c 347265 2009-11-19 13:55:39Z kdickman $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2009, Juniper Networks, Inc.
 * All rights reserved.
 *
 */

/**
 * @file ipprobe-mt_responder.c
 * @brief Responder manager and responders
 *
 * These functions manage responder manager and responders.
 *
 */
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <isc/eventlib.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/pconn.h>
#include <jnx/junos_trace.h>
#include "ipprobe-mt.h"
#include IPPROBE_MT_OUT_H

rspd_mngr_t rspd_mngr;
static LIST_HEAD(, rspd_s)  rspd_list;

/**
 * Close the responder.
 *
 * @param[in] rspd
 *      Pointer to the responder data structure
 */
static void
rspd_close (rspd_t *rspd)
{
    if (rspd == NULL) {
        return;
    }

    /* Decrement responder use count. */
    if (--rspd->usage > 0) {
        /* It's still used by some other client(s). */
        return;
    }

    if (evTestID(rspd->read_fid)) {
        evDeselectFD(rspd->ev_ctx, rspd->read_fid);
    }

    /* Close responder socket. */
    if (rspd->socket >= 0) {
        close(rspd->socket);
    }

    /* Remove it from the responder list. */
    LIST_REMOVE(rspd, entry);
    free(rspd);

    PROBE_TRACE(PROBE_TF_RSPD, "%s: Responder is closed.", __func__);
}

/**
 * Responder probe packet handler.
 *
 * @param[in] ev_ctx
 *      Event context
 * @param[in] uap
 *      Pointer to user data
 * @param[in] fd
 *      File descriptor
 * @param[in] eventmask
 *      Event mask
 */
static void
rspd_pkt_hdlr (evContext lev_ctx UNUSED, void *uap, int fd UNUSED,
        int eventmask UNUSED)
{
    rspd_t *rspd = (rspd_t *)uap;
    probe_pkt_t *pkt = (probe_pkt_t *)rspd->rx_buf;
    struct sockaddr_in  src_addr;
    probe_pkt_data_t *data;
    struct timeval rx_time;
    int recv_len = 0;
    int send_len = 0;
    int addr_len;
    bool last_req = false;

    while (!last_req) {
        /* Something came in, recode receive time. */
        gettimeofday(&rx_time, NULL);
        addr_len = sizeof(src_addr);
        recv_len = recvfrom(rspd->socket, rspd->rx_buf, PROBE_PKT_SIZE_MAX,
                0, (struct sockaddr *)&src_addr, &addr_len);

        if (recv_len == 0) {
            break;
        } else if (recv_len < 0) {
            if(errno != EAGAIN) {
                PROBE_LOG(LOG_ERR, "%s: Receive probe packet ERROR(%d)!",
                        __func__, errno);
            }
            break;
        }

        if (rspd->proto == IPPROTO_UDP) {
            data = (probe_pkt_data_t *)rspd->rx_buf;
        }  else {
            data = &pkt->data;
            pkt->header.ip_src.s_addr = 0;
            pkt->header.ip_dst.s_addr = src_addr.sin_addr.s_addr;
        }

        if (data->type == PROBE_PKT_REQ_LAST) {
            last_req = true;
        } else if (data->type != PROBE_PKT_REQ) {
            PROBE_LOG(LOG_ERR, "%s: Not request packet! type: %d",
                    __func__, data->type);
            break;
        }

        /* Packet has be validated, copy receive time into packet */
        bcopy(&rx_time, &data->target_rx_time, sizeof(struct timeval));

        PROBE_TRACE(PROBE_TF_RSPD,
                "%s: Packet type %d, len %d, src_addr %s, src_port %d.",
                __func__, data->type, recv_len, inet_ntoa(src_addr.sin_addr),
                ntohs(src_addr.sin_port));

        data->type = PROBE_PKT_REPLY;
        gettimeofday(&data->target_tx_time, NULL);

        send_len = sendto(rspd->socket, rspd->rx_buf, recv_len, 0,
                (struct sockaddr *)&src_addr, addr_len);
        if (send_len < 0) {
            PROBE_LOG(LOG_ERR, "%s: Send probe packet ERROR(%d)!",
                    __func__, errno);
            break;
        }
    }
    if (last_req) {
        rspd_close(rspd);
    }
}

/**
 * Create a responder as requested.
 *
 * @return
 *      0 on success, -1 on failure
 */
static int
rspd_open (void)
{
    rspd_t *rspd = LIST_FIRST(&rspd_list);
    struct sockaddr_in addr;
    const int on = 1;

    /* Look for responder with the same protocol and port to reuse. */
    while (rspd) {
        if ((rspd->proto == rspd_mngr.rx_pkt.proto)
                && (rspd->port == rspd_mngr.rx_pkt.port)) {
            rspd->usage++;
            if (rspd->timeout < rspd_mngr.rx_pkt.timeout) {
                rspd->timeout = rspd_mngr.rx_pkt.timeout;
            }
            PROBE_TRACE(PROBE_TF_RSPD, "%s: Reuse responder on protocol %d, "
                    "port %d", __func__, rspd->proto, rspd->port);
            return 0;
        }
        rspd = LIST_NEXT(rspd, entry);
    }

    /* Create the new responder. */
    rspd = calloc(1, sizeof(rspd_t));
    INSIST_ERR(rspd);

    rspd->proto = rspd_mngr.rx_pkt.proto;
    rspd->port = rspd_mngr.rx_pkt.port;
    rspd->timeout = rspd_mngr.rx_pkt.timeout;
    rspd->ev_ctx = rspd_mngr.ev_ctx;
    rspd->usage++;
    LIST_INSERT_HEAD(&rspd_list, rspd, entry);

    if (rspd->proto == IPPROTO_UDP) {
        rspd->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (rspd->socket < 0) {
            PROBE_LOG(LOG_ERR, "%s: Create responder UDP socket ERROR(%d)!",
                    __func__, errno);
            goto ret_err;
        }

        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(rspd->port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(rspd->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            PROBE_LOG(LOG_ERR, "%s: Bind responder UDP socket ERROR(%d)!",
                    __func__, errno);
            goto ret_err;
        }
        PROBE_TRACE(PROBE_TF_RSPD, "%s: Responder on UDP port %d is opened.",
                __func__, rspd->port);
    } else {
        rspd->socket = socket(PF_INET, SOCK_RAW, rspd->proto);
        if (rspd->socket < 0) {
            PROBE_LOG(LOG_ERR, "%s: Create responder RAW socket ERROR(%d)!",
                    __func__, errno);
            goto ret_err;
        }

        if (setsockopt(rspd->socket, IPPROTO_IP, IP_HDRINCL, &on,
                sizeof(on)) < 0) {
            PROBE_LOG(LOG_ERR, "%s: Set responder RAW socket option ERROR(%d)!",                    __func__, errno);
            goto ret_err;
        }
        PROBE_TRACE(PROBE_TF_RSPD, "%s: Responder on protocol %d is opened.",
                __func__, rspd->proto);
    }

    if (evSelectFD(rspd->ev_ctx, rspd->socket, EV_READ, rspd_pkt_hdlr,
            rspd, &rspd->read_fid) < 0) {
        PROBE_LOG(LOG_ERR, "%s: evSelectFD responder socket ERROR(%d)!",
                __func__, errno);
        goto ret_err;
    }

    return 0;

ret_err:
    rspd_close(rspd);
    return -1;
}

/**
 * The packet handler for responder manager.
 */
static void
rspd_mngr_pkt_hdlr (evContext lev_ctx UNUSED, void *uap UNUSED, int fd,
        int eventmask UNUSED)
{
    struct sockaddr_in src_addr;
    int recv_len = 0;
    int addr_len;
    rspd_mgmt_pkt_t tx_pkt;

    while (1) {
        addr_len = sizeof(src_addr);
        recv_len = recvfrom(fd, &rspd_mngr.rx_pkt, sizeof(rspd_mngr.rx_pkt), 0,
                (struct sockaddr *)&src_addr, &addr_len);

        if (recv_len > 0) {
            if (recv_len == sizeof(rspd_mgmt_pkt_t)) {
                PROBE_TRACE(PROBE_TF_RSPD,
                        "%s: Got probe request, protocol %d, port %d.",
                       __func__, rspd_mngr.rx_pkt.proto,
                       rspd_mngr.rx_pkt.port);
                rspd_open();
                tx_pkt.type = RSPD_MGMT_MSG_ACK;
                sendto(fd, &tx_pkt, sizeof(tx_pkt), 0,
                        (struct sockaddr *)&src_addr, addr_len);
            } else {
                PROBE_LOG(LOG_ERR, "%s: Received fragment!", __func__);
            }
        } else if (recv_len == 0) {
            break;
        } else if (recv_len < 0) {
            if (errno != EAGAIN) {
                PROBE_LOG(LOG_ERR, "%s, Read socket ERROR(%d)!",
                        __func__, errno);
            }
            break;
        }
    }
}

/**
 * Close the responder manager.
 */
void
rspd_mngr_close (void)
{
    if (evTestID(rspd_mngr.read_fid)) {
        evDeselectFD(rspd_mngr.ev_ctx, rspd_mngr.read_fid);
        evInitID(&rspd_mngr.read_fid);
    }

    if (rspd_mngr.socket >= 0) {
        close(rspd_mngr.socket);
        rspd_mngr.socket = -1;
    }

    PROBE_TRACE(PROBE_TF_RSPD, "%s: The responder manager is closed.",
            __func__);
}

/**
 * Open the responder manager.
 *
 * @param[in] port
 *      UDP port
 * @return
 *      0 on success, -1 on failure
 */
int
rspd_mngr_open (uint16_t port)
{
    struct sockaddr_in addr;

    if (rspd_mngr.socket >= 0) {
        if (rspd_mngr.port == port) {
            return 0;
        } else {
            rspd_mngr_close();
        }
    }
    rspd_mngr.port = port;

    evInitID(&rspd_mngr.read_fid);
    rspd_mngr.socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (rspd_mngr.socket < 0) {
        PROBE_LOG(LOG_ERR, "%s: Open responder manager socket ERROR(%d)!",
                __func__, errno);
        goto ret_err;
    }

    bzero(&addr, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(rspd_mngr.port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(rspd_mngr.socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        PROBE_LOG(LOG_ERR, "%s: Bind responder manager socket ERROR(%d)!",
                __func__, errno);
        goto ret_err;
    }

    if (evSelectFD(rspd_mngr.ev_ctx, rspd_mngr.socket, EV_READ,
            rspd_mngr_pkt_hdlr, NULL, &rspd_mngr.read_fid)) {
        PROBE_LOG(LOG_ERR, "%s: evSelectFD responder manager socket ERROR(%d)!",
                __func__, errno);
        goto ret_err;
    }

    PROBE_TRACE(PROBE_TF_RSPD, "%s: The responder manager is open on port %d.",
            __func__, rspd_mngr.port);
    return 0;

ret_err:
    rspd_mngr_close();
    return -1;
}

/**
 * Initialize the responder manager.
 *
 * @param[in] lev_ctx
 *      Event context
 * @return
 *      0 on success, -1 on failure
 */
int
rspd_mngr_init (evContext lev_ctx)
{
    PROBE_TRACE(PROBE_TF_RSPD, "%s: Initialize the responder manager.",
            __func__);
    bzero(&rspd_mngr, sizeof(rspd_mngr));
    rspd_mngr.socket = -1;
    rspd_mngr.port = RSPD_MNGR_PORT_DEFAULT;
    rspd_mngr.ev_ctx = lev_ctx;

    return 0;
}


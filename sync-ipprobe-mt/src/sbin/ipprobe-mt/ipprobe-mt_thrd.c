/*
 * $Id: ipprobe-mt_thrd.c 347265 2009-11-19 13:55:39Z kdickman $
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

#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
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

extern probe_mngr_t probe_mngr;
extern rspd_mngr_t rspd_mngr;

/**
 * @brief
 * Calculate ICMP checksum.
 *
 * @param[in] buf
 *      Pointer to ICMP packet
 * @param[in] len
 *      Length of ICMP packet in byte
 * @return
 *      The checksum with network order
 */
static u_short
icmp_cksum (void *buf, int len)
{
    u_char *p = (u_char *)buf;
    uint sum = 0;

    while (len > 1) {
        sum += (*p << 8) + *(p + 1);
        len -= 2;
        p += 2;
    }
    if (len == 1) {
        sum += (*p << 8);
    }

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    sum = ~sum;

    return(htons((u_short)sum));
}

/**
 * @brief
 * Calculate UDP checksum.
 *
 * @param[in] len
 *      Length of UDP packet in byte
 * @param[in] src_addr
 *      Pointer to the source IP address
 * @param[in] dst_addr
 *      Pointer to the destination IP address
 * @param[in] buf
 *      Pointer to the UDP packet
 * @return
 *      The checksum with network order
 */
static u_short
udp_cksum (u_short len, void *src_addr, void *dst_addr, void *buf)
{
    uint sum = 0;
    u_char *p = NULL;

    p = (u_char *)src_addr;
    sum += (*p << 8) + *(p + 1);
    sum += (*(p + 2) << 8) + *(p + 3);

    p = (u_char *)dst_addr;
    sum += (*p << 8) + *(p + 1);
    sum += (*(p + 2) << 8) + *(p + 3);

    sum += IPPROTO_UDP + len;

    p = (u_char *)buf;
    while (len > 1) {
        sum += (*p << 8) + *(p + 1);
        len -= 2;
        p += 2;
    }
    if (len == 1) {
        sum += (*p << 8);
    }

    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);

    sum = ~sum;

    return(htons((u_short)sum));
}

/**
 * @brief
 * Calculate the time difference.
 *
 * @param[in] time1
 *      The earlier time
 * @param[in] time2
 *      The later time
 * @return
 *      The time difference
 */
static float
time_diff (struct timeval *time1, struct timeval *time2)
{
    float t1, t2;

    t1 = time1->tv_sec * 1000 + (float)time1->tv_usec / 1000;
    t2 = time2->tv_sec * 1000 + (float)time2->tv_usec / 1000;
    return (t2 - t1);
}

/**
 * @brief
 * Process received probe packets and calculate statistics.
 *
 * @param[in] probe
 *      Pointer to the probe
 * @param[in] dst
 *      Pointer to the probe destination
 */
static void
process_probe_pkts (probe_t *probe, probe_dst_t *dst)
{
    pkt_stats_t *pkt_stats;
    probe_rx_buf_t *rx_buf;
    probe_pkt_t *pkt;
    probe_pkt_data_t *data;
    probe_pkt_data_t *data_prev = NULL;
    int i;
    float max_delay_sd = 0;
    float max_delay_ds = 0;
    float max_delay_rr = 0;
    float sum_delay_sd = 0;
    float sum_delay_ds = 0;
    float sum_delay_rr = 0;
    float max_jitter_sd = 0;
    float max_jitter_ds = 0;
    float max_jitter_rr = 0;
    float sum_jitter_sd = 0;
    float sum_jitter_ds = 0;
    float sum_jitter_rr = 0;

    PROBE_TRACE(PROBE_TF_THRD, "%s: Process %d replied packets from 0x%08x",
            __func__, dst->rx_count, dst->dst_addr);
    pkt_stats = calloc(dst->rx_count, sizeof(*pkt_stats));
    INSIST_ERR(pkt_stats != NULL);

    i = 0;
    LIST_FOREACH(rx_buf, &dst->rx_buf_list, entry) {
        if (probe->params.proto == IPPROTO_UDP) {
            data = (probe_pkt_data_t *)rx_buf->pkt;
        } else {
            pkt = (probe_pkt_t *)rx_buf->pkt;
            data = &pkt->data;
        }
        if (probe->params.proto != IPPROTO_ICMP) {
            pkt_stats[i].delay_sd = time_diff(&data->tx_time,
                    &data->target_rx_time);

            pkt_stats[i].delay_ds = time_diff(&data->target_tx_time,
                    &data->rx_time);
        }
        pkt_stats[i].rrt = time_diff(&data->tx_time, &data->rx_time);

        if (rx_buf == LIST_FIRST(&dst->rx_buf_list)) {
            goto continue_loop;
        }
        if (probe->params.proto != IPPROTO_ICMP) {
            pkt_stats[i].jitter_sd = time_diff(&data_prev->target_rx_time,
                    &data->target_rx_time) -
                    time_diff(&data_prev->tx_time, &data->tx_time);

            pkt_stats[i].jitter_ds = time_diff(&data_prev->rx_time,
                    &data->rx_time) -
                    time_diff(&data_prev->target_tx_time,
                    &data->target_tx_time);
        }
        pkt_stats[i].jitter_rr = time_diff(&data_prev->rx_time,
                &data->rx_time) -
                time_diff(&data_prev->tx_time, &data->tx_time);
continue_loop:
        data_prev = data;
        i++;
    }

    for (i = 0; i < dst->rx_count; i++) {
        if (pkt_stats[i].delay_sd > max_delay_sd) {
            max_delay_sd = pkt_stats[i].delay_sd;
        }
        if (pkt_stats[i].delay_ds > max_delay_ds) {
            max_delay_ds = pkt_stats[i].delay_ds;
        }
        if (pkt_stats[i].rrt > max_delay_rr) {
            max_delay_rr = pkt_stats[i].rrt;
        }
        if (abs(pkt_stats[i].jitter_sd) > abs(max_jitter_sd)) {
            max_jitter_sd = pkt_stats[i].jitter_sd;
        }
        if (abs(pkt_stats[i].jitter_ds) > abs(max_jitter_ds)) {
            max_jitter_ds = pkt_stats[i].jitter_ds;
        }
        if (abs(pkt_stats[i].jitter_rr) > abs(max_jitter_rr)) {
            max_jitter_rr = pkt_stats[i].jitter_rr;
        }
        sum_delay_sd += pkt_stats[i].delay_sd;
        sum_delay_ds += pkt_stats[i].delay_ds;
        sum_delay_rr += pkt_stats[i].rrt;
        sum_jitter_sd += pkt_stats[i].jitter_sd;
        sum_jitter_ds += pkt_stats[i].jitter_ds;
        sum_jitter_rr += pkt_stats[i].jitter_rr;
    }

    dst->result.delay_sd_average = sum_delay_sd / dst->rx_count;
    dst->result.delay_ds_average = sum_delay_ds / dst->rx_count;
    dst->result.delay_rr_average = sum_delay_rr / dst->rx_count;
    dst->result.delay_sd_max = max_delay_sd;
    dst->result.delay_ds_max = max_delay_ds;
    dst->result.delay_rr_max = max_delay_rr;
    dst->result.jitter_sd_average = sum_jitter_sd / (dst->rx_count - 1);
    dst->result.jitter_ds_average = sum_jitter_ds / (dst->rx_count - 1);
    dst->result.jitter_rr_average = sum_jitter_rr / (dst->rx_count - 1);
    dst->result.jitter_sd_max = max_jitter_sd;
    dst->result.jitter_ds_max = max_jitter_ds;
    dst->result.jitter_rr_max = max_jitter_rr;

    free(pkt_stats);
}

/**
 * @brief
 * Clear Rx buffer for the destination.
 *
 * @param[in] dst
 *      Pointer to probe destination
 */
static void
probe_dst_rx_buf_clear (probe_dst_t *dst)
{
    probe_rx_buf_t *rx_buf;

    LIST_FOREACH_SAFE(rx_buf, &dst->rx_buf_list, entry, dst->rx_buf_last) {
        LIST_REMOVE(rx_buf, entry);
        free(rx_buf);
    }
}

/**
 * @brief
 * The event handler for receiving probe packets.
 */
static void
probe_pkt_hdlr (evContext ctx UNUSED, void *uap, int fd UNUSED,
        int evmask UNUSED)
{
    probe_t *probe = uap;
    probe_rx_buf_t *rx_buf;
    int recv_len;
    struct sockaddr_in addr;
    int addr_len;
    probe_pkt_t *pkt;
    probe_pkt_data_t *data;
    probe_dst_t *dst;
    struct timeval rx_time;

    gettimeofday(&rx_time, NULL);
    rx_buf = calloc(1, sizeof(*rx_buf) + probe->params.pkt_size);
    INSIST_ERR(rx_buf);

    recv_len = recvfrom(probe->rx_socket, &rx_buf->pkt, probe->params.pkt_size,
            0, (struct sockaddr *)&addr, &addr_len);

    if (recv_len == 0) {
        goto ret_err;
    } else if(recv_len < 0) {
        if (errno != EAGAIN) {
            PROBE_LOG(LOG_ERR, "%s: Receive probe packets ERROR!", __func__);
        }
        goto ret_err;
    }

    if (probe->params.proto == IPPROTO_UDP) {
        data = (probe_pkt_data_t *)rx_buf->pkt;
    } else {
        pkt = (probe_pkt_t *)rx_buf->pkt;
        data = &pkt->data;
        if ((probe->params.proto == IPPROTO_ICMP)
                && (pkt->icmp.icmp_type != ICMP_ECHOREPLY)) {
            goto ret_err;
        }
    }
    bcopy(&rx_time, &data->rx_time, sizeof(struct timeval));

    PROBE_TRACE(PROBE_TF_THRD, "%s: protocol %d, type %d, seq %d, src 0x%08x.",
            __func__, probe->params.proto, data->type, data->seq,
            addr.sin_addr.s_addr);

    dst = (probe_dst_t *)patricia_get(&probe->dst_pat, sizeof(in_addr_t),
            &addr.sin_addr.s_addr);
    if (!dst) {
        PROBE_TRACE(PROBE_TF_THRD, "%s: Dst 0x%08x does not exits!",
                __func__, addr.sin_addr.s_addr);
        goto ret_err;
    }
    if (dst->rx_buf_last) {
        LIST_INSERT_AFTER(dst->rx_buf_last, rx_buf, entry);
    } else {
        LIST_INSERT_HEAD(&dst->rx_buf_list, rx_buf, entry);
    }
    dst->rx_buf_last = rx_buf;

    if (++dst->rx_count == probe->params.pkt_count) {
        process_probe_pkts(probe, dst);
        dst->state = PROBE_DST_STATE_DONE;
        probe_dst_rx_buf_clear(dst);
        PROBE_TRACE(PROBE_TF_THRD, "%s: Destination count %d.",
                __func__, probe->dst_count - 1);

        /* Decrement the counter for running destinations and
         * exit if there is no destination running.
         */
        if (--probe->dst_count == 0) {
            pthread_exit(NULL);
        }
    }
    return;

ret_err:
    free(rx_buf);
    return;
}

/**
 * @brief
 * The event handler for sending probe packets.
 */
static void
send_packet (evContext ctx UNUSED, void *uap, struct timespec due UNUSED,
        struct timespec inter UNUSED)
{
    probe_t *probe = uap;
    probe_pkt_t *tx_pkt = (probe_pkt_t *)probe->tx_buf;
    struct sockaddr_in dst_addr;
    probe_dst_t *dst;
    int send_len;
    patnode *node;

    do {
        bzero(&dst_addr, sizeof(dst_addr));
        dst_addr.sin_family = AF_INET;
        tx_pkt->data.seq = probe->tx_count;
        if (probe->tx_count == probe->params.pkt_count - 1) {
            tx_pkt->data.type = PROBE_PKT_REQ_LAST;
        }

        node = NULL;
        while ((node = patricia_find_next(&probe->dst_pat, node))) {
            gettimeofday(&tx_pkt->data.tx_time, NULL);
            dst = (probe_dst_t *)node;
            if (dst->state == PROBE_DST_STATE_INIT) {
                PROBE_TRACE(PROBE_TF_THRD, "%s: dst 0x%08x is in init mode.",
                        __func__, dst);
                continue;
            }
            tx_pkt->header.ip_dst.s_addr = dst->dst_addr;
            tx_pkt->header.ip_src.s_addr = dst->local_addr;
            if (probe->params.proto == IPPROTO_ICMP) {
                tx_pkt->icmp.icmp_cksum = 0;
                tx_pkt->icmp.icmp_cksum = icmp_cksum(&tx_pkt->icmp,
                        probe->params.pkt_size - sizeof(struct ip));
            } else if (probe->params.proto == IPPROTO_UDP) {
                tx_pkt->udp.uh_sum = 0;
                tx_pkt->udp.uh_sum = udp_cksum(ntohs(tx_pkt->udp.uh_ulen),
                        &tx_pkt->header.ip_src.s_addr,
                        &tx_pkt->header.ip_dst.s_addr,
                        &tx_pkt->udp);
            }
            dst_addr.sin_addr.s_addr = dst->dst_addr;
            send_len = sendto(probe->tx_socket, tx_pkt, probe->params.pkt_size,
                    0, (struct sockaddr *)&dst_addr, sizeof(dst_addr));
            if (send_len < 0) {
                PROBE_LOG(LOG_ERR, "Send probe packet ERROR!");
            }
            PROBE_TRACE(PROBE_TF_THRD, "%s: Sent packet to %s, dst(0x%08x)",
                    __func__, inet_ntoa(dst_addr.sin_addr), dst);
        }

        if (++probe->tx_count == probe->params.pkt_count) {

            /* Disable the timer. */
            evClearTimer(probe->ev_ctx, probe->tx_tid);
            evInitID(&probe->tx_tid);
            PROBE_TRACE(PROBE_TF_THRD, "%s: Sent %d probe packets to %s.",
                    __func__, probe->tx_count, inet_ntoa(dst_addr.sin_addr));
            break;
        }
    } while (probe->params.pkt_interval == 0);
}

/**
 * @brief
 * Close probe Tx socket and free Tx buffer.
 *
 * @param[in] probe
 *      Pointer to the probe
 */
static void
probe_tx_socket_close (probe_t *probe)
{
    if (evTestID(probe->tx_tid)) {
        evClearTimer(probe->ev_ctx, probe->tx_tid);
        evInitID(&probe->tx_tid);
    }
    if (probe->tx_socket >= 0) {
        close(probe->tx_socket);
        probe->tx_socket = -1;
    }
    if (probe->tx_buf) {
        free(probe->tx_buf);
        probe->tx_buf = NULL;
    }
}

/**
 * @brief
 * Open probe Tx socket and setup Tx buffer.
 *
 * @param[in] probe
 *      Pointer to the probe
 * @return
 *      0 on success, -1 on failure
 */
static int
probe_tx_socket_open (probe_t *probe)
{
    const int on = 1;

    /* Create TX socket for sending probe packets. */
    probe->tx_socket = socket(AF_INET, SOCK_RAW, probe->params.proto);
    if (probe->tx_socket < 0) {
        PROBE_LOG(LOG_ERR, "Create probe TX socket ERROR!");
        goto ret_err;
    }

    if (setsockopt(probe->tx_socket, IPPROTO_IP, IP_HDRINCL, &on,
            sizeof(on)) < 0) {
        PROBE_LOG(LOG_ERR, "Set probe transmit socket IP_HDRINCL ERROR!");
        goto ret_err;
    }
    probe->tx_buf = calloc(1, probe->params.pkt_size);
    INSIST_ERR(probe->tx_buf);

    PROBE_TRACE(PROBE_TF_THRD, "%s: Tx socket is created on protocol %d.",
            __func__, probe->params.proto);
    return 0;

ret_err:
    probe_tx_socket_close(probe);
    return -1;
}

/**
 * @brief
 * Close probe Rx socket and free Rx buffer.
 *
 * @param[in] probe
 *      Pointer to the probe
 */
static void
probe_rx_socket_close (probe_t *probe)
{
    patnode *node;
    probe_dst_t *dst;

    if (evTestID(probe->rx_fid)) {
        evDeselectFD(probe->ev_ctx, probe->rx_fid);
        evInitID(&probe->rx_fid);
    }
    if (probe->rx_socket >= 0) {
        close(probe->rx_socket);
        probe->rx_socket = -1;
    }
    node = NULL;
    while ((node = patricia_find_next(&probe->dst_pat, node))) {
        dst = (probe_dst_t *)node;
        probe_dst_rx_buf_clear(dst);
    }
}

/**
 * @brief
 * Open probe Rx socket and setup Rx buffer.
 *
 * @param[in] probe
 *      Pointer to the probe
 * @param[in] dst
 *      Pointer to probe destination
 * @return
 *      0 on success, -1 on failure
 */
static int
probe_rx_socket_open (probe_t *probe, probe_dst_t *dst)
{
    const int on = 1;
    struct sockaddr_in addr;

    /* Create RX socket. */
    if (probe->params.proto == IPPROTO_UDP) {
        probe->rx_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (probe->rx_socket < 0) {
            PROBE_LOG(LOG_ERR, "Create probe receive socket ERROR!");
            goto ret_err;
        }
        if (setsockopt(probe->rx_socket, SOL_SOCKET, SO_REUSEADDR, &on,
                sizeof(on)) < 0) {
            PROBE_LOG(LOG_ERR, "Set probe receive socket option ERROR!");
            goto ret_err;
        }
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(probe->params.src_port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(probe->rx_socket, (struct sockaddr *)&addr, sizeof(addr))
                < 0) {
            PROBE_LOG(LOG_ERR, "Bind probe receive socket ERROR!");
            goto ret_err;
        }
        PROBE_TRACE(PROBE_TF_THRD, "%s: Rx socket is created on UDP port %d.",
                __func__, probe->params.src_port);
    } else {
        probe->rx_socket = dup(probe->tx_socket);
        PROBE_TRACE(PROBE_TF_THRD, "%s: Rx socket is created on protocol %d.",
                __func__, probe->params.proto);
    }
    LIST_INIT(&dst->rx_buf_list);
    dst->rx_buf_last = NULL;
    dst->rx_count = 0;

    evInitID(&probe->rx_fid);
    if (evSelectFD(probe->ev_ctx, probe->rx_socket, EV_READ, probe_pkt_hdlr,
            probe, &probe->rx_fid) < 0) {
        PROBE_LOG(LOG_ERR, "Add probe receive socket!\n");
        goto ret_err;
    }
    return 0;

ret_err:
    probe_rx_socket_close(probe);
    return -1;
}

/**
 * @brief
 * Start probe.
 *
 * @param[in] probe
 *      Pointer to the probe
 * @return
 *      0 on success, -1 on failure
 */
static int
probe_start (probe_t *probe)
{
    probe_pkt_t *tx_pkt = (probe_pkt_t *)probe->tx_buf;

    tx_pkt->header.ip_v = IPVERSION;
    tx_pkt->header.ip_hl = IP_HEADER_LEN >> 2;
    tx_pkt->header.ip_tos = probe->params.tos;
    tx_pkt->header.ip_len = htons(probe->params.pkt_size);
    tx_pkt->header.ip_ttl = PROBE_PKT_TTL_DEFAULT;
    tx_pkt->header.ip_p = probe->params.proto;

    if (probe->params.proto == IPPROTO_ICMP) {
        tx_pkt->icmp.icmp_type = ICMP_ECHO;
        tx_pkt->icmp.icmp_code = 0;
        tx_pkt->data.type = PROBE_PKT_REPLY;
    } else if (probe->params.proto == IPPROTO_UDP) {
        tx_pkt->udp.uh_sport = htons(probe->params.src_port);
        tx_pkt->udp.uh_dport = htons(probe->params.dst_port);
        tx_pkt->udp.uh_ulen = htons(probe->params.pkt_size - sizeof(struct ip));
        tx_pkt->data.type = PROBE_PKT_REQ;
    } else {
        tx_pkt->data.type = PROBE_PKT_REQ;
    }
    probe->tx_count = 0;

    if (evSetTimer(probe->ev_ctx, send_packet, probe, evConsTime(0, 0),
            evConsTime(0, probe->params.pkt_interval * 1000000),
            &probe->tx_tid) < 0) {
        PROBE_LOG(LOG_ERR, "Set timer to schedule sending packets ERROR!");
        return -1;
    }
    return 0;
}

/**
 * @brief
 * Close the responder manager connection.
 *
 * @param[in] probe
 *      Pointer to the probe
 */
static void
rspd_mgmt_close (probe_t *probe)
{
    if (evTestID(probe->rspd_read_fid)) {
        evDeselectFD(probe->ev_ctx, probe->rspd_read_fid);
        evInitID(&probe->rspd_read_fid);
    }
    if (probe->rspd_socket >= 0) {
        close(probe->rspd_socket);
        probe->rspd_socket = -1;
    }
}

/**
 * @brief
 * Handling the packets from responder manager.
 */
static void
rspd_mgmt_pkt_hdlr (evContext lev_ctx UNUSED, void *uap, int fd,
        int eventmask UNUSED)
{
    probe_t *probe = uap;
    struct sockaddr_in dst_addr;
    int recv_len = 0;
    socklen_t addr_len = sizeof(dst_addr);
    rspd_mgmt_pkt_t rx_pkt;
    probe_dst_t *dst;

    recv_len = read(fd, &rx_pkt, sizeof(rx_pkt));

    if (recv_len > 0) {
        if (recv_len == sizeof(rspd_mgmt_pkt_t)) {
            if (rx_pkt.type == RSPD_MGMT_MSG_ACK) {
                getpeername(fd, (struct sockaddr *)&dst_addr, &addr_len);

                PROBE_TRACE(PROBE_TF_THRD, "%s: Received ACK from %s, "
                        "start probe.",
                        __func__, inet_ntoa(dst_addr.sin_addr));
                rspd_mgmt_close(probe);
                dst = (probe_dst_t *)patricia_get(&probe->dst_pat,
                        sizeof(in_addr_t), &dst_addr.sin_addr.s_addr);
                if (dst) {
                    dst->state = PROBE_DST_STATE_RUN;
                    probe_tx_socket_open(probe);
                    probe_rx_socket_open(probe, dst);
                    probe_start(probe);
                } else {
                    PROBE_LOG(LOG_ERR, "%s: Destination does not exist!",
                            __func__);
                }
            }
        } else {
            PROBE_LOG(LOG_ERR, "%s: Received fragment!", __func__);
        }
    } else if (recv_len < 0) {
        if (errno != EAGAIN) {
            PROBE_LOG(LOG_ERR, "%s, Read socket ERROR(%d)!",
                    __func__, errno);
        }
    }
}

/**
 * @brief
 * Open socket to send request to responder manager.
 *
 * @param[in] probe
 *      Pointer to the probe
 * @param[in] dst
 *      Pointer to destination data
 * @return
 *      0 on success, -1 on failure
 */
static int
rspd_mgmt_open (probe_t *probe, probe_dst_t *dst)
{
    rspd_mgmt_pkt_t pkt;
    struct sockaddr_in rspd_addr;
    struct sockaddr_in local_addr;
    socklen_t len;

    PROBE_TRACE(PROBE_TF_THRD, "%s: Open socket to responder manager.",
            __func__);
    PROBE_TRACE(PROBE_TF_THRD, "%s: protocol %d, dst_port %d, src_port %d, "
            "size %d, count %d, interval %d.", __func__,
            probe->params.proto, probe->params.dst_port,
            probe->params.src_port, probe->params.pkt_size,
            probe->params.pkt_count, probe->params.pkt_interval);

    evInitID(&probe->rspd_read_fid);
    probe->rspd_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (probe->rspd_socket < 0) {
        PROBE_LOG(LOG_ERR, "%s: Open socket to responder manager ERROR(%d)!",
                __func__, errno);
        goto ret_err;
    }

    bzero(&rspd_addr, sizeof(rspd_addr));
    rspd_addr.sin_family = AF_INET;
    rspd_addr.sin_port = htons(rspd_mngr.port);
    rspd_addr.sin_addr.s_addr = dst->dst_addr;

    /* Find out the local address to this destination. */
    connect(probe->rspd_socket, (struct sockaddr *)&rspd_addr,
            sizeof(rspd_addr));
    len = sizeof(local_addr);
    getsockname(probe->rspd_socket, (struct sockaddr *)&local_addr, &len);
    dst->local_addr = local_addr.sin_addr.s_addr;
    PROBE_TRACE(PROBE_TF_THRD, "%s: Local address %s.",
            __func__, inet_ntoa(local_addr.sin_addr));

    if (evSelectFD(probe->ev_ctx, probe->rspd_socket, EV_READ,
            rspd_mgmt_pkt_hdlr, probe, &probe->rspd_read_fid)) {
        PROBE_LOG(LOG_ERR,
                "%s: evSelectFD socket to responder manager ERROR(%d)!",
                __func__, errno);
        goto ret_err;
    }

    pkt.type = RSPD_MGMT_MSG_REQ;
    pkt.port = probe->params.dst_port;
    pkt.proto = probe->params.proto;
    write(probe->rspd_socket, &pkt, sizeof(pkt));

    return 0;

ret_err:
    rspd_mgmt_close(probe);
    return -1;
}

/**
 * @brief
 * Handling the message from probe manager.
 *
 * @param[in] client
 *      Pointer to the pconn client
 * @param[in] msg
 *      Pointer to the IPC message
 * @param[in] cookie
 *      Pointer to the user data
 * @return
 *      0 always
 */
static status_t
client_msg_hdlr (pconn_client_t *client UNUSED, ipc_msg_t *msg, void *cookie)
{
    probe_t *probe = cookie;
    in_addr_t addr;
    probe_dst_t *dst;

    if (msg->subtype == PROBE_MGMT_MSG_ADD_DST) {
        addr = *((in_addr_t *)msg->data);
        PROBE_TRACE(PROBE_TF_THRD, "%s: Got request to add dst 0x%08x.",
               __func__, addr);
        PROBE_TRACE(PROBE_TF_THRD, "%s: probe 0x%08x, protocol %d, port %d, "
                "size %d, count %d, interval %d.", __func__,
                probe, probe->params.proto, probe->params.dst_port,
                probe->params.pkt_size, probe->params.pkt_count,
                probe->params.pkt_interval);
        dst = (probe_dst_t *)patricia_get(&probe->dst_pat, sizeof(in_addr_t),
                &addr);
        if (dst) {
            if (dst->state == PROBE_DST_STATE_RUN ||
                    dst->state == PROBE_DST_STATE_INIT) {
                PROBE_TRACE(PROBE_TF_THRD,
                        "%s: The probe to 0x%08x is running or initilaizing.",
                        __func__, addr);
                return 0;
            }
        } else {
            dst = calloc(1, sizeof(*dst));
            INSIST_ERR(dst);
            dst->dst_addr = addr;
            dst->state = PROBE_DST_STATE_INIT;
            patricia_node_init_length(&dst->node, sizeof(in_addr_t));
            patricia_add(&probe->dst_pat, &dst->node);
        }
        probe->dst_count++;

        if (probe->params.proto == IPPROTO_ICMP) {

            /* ICMP probing doesn't need responder. */
            dst->state = PROBE_DST_STATE_RUN;
            probe_tx_socket_open(probe);
            probe_rx_socket_open(probe, dst);
            probe_start(probe);
        } else {
            rspd_mgmt_open(probe, dst);
        }
    }
    return 0;
}

/**
 * @brief
 * Probe thread pconn client event handler.
 *
 * @param[in] client
 *      Pointer to the pconn client
 * @param[in] event
 *      pconn event
 * @param[in] cookie
 *      Pointer to the user data
 */
static void
client_event_hdlr (pconn_client_t *client, pconn_event_t event, void *cookie)
{
    probe_t *probe = cookie;

    switch (event) {
    case PCONN_EVENT_ESTABLISHED:
        PROBE_TRACE(PROBE_TF_THRD, "%s: Connected to the probe manager.",
                __func__);
        pconn_client_send(client, PROBE_MGMT_MSG_REG, probe->params.name,
                sizeof(probe->params.name));
        break;
    case PCONN_EVENT_SHUTDOWN:
        PROBE_TRACE(PROBE_TF_THRD, "%s: Connection is down.", __func__);
        break;
    case PCONN_EVENT_FAILED:
        PROBE_TRACE(PROBE_TF_THRD, "%s: Connect ERROR.", __func__);
        pthread_exit(NULL);
        break;
    default:
        PROBE_LOG(LOG_ERR, "%s: Unknown event %d.", __func__, event);
    }
}

/**
 * @brief
 * The cleanup handler for probe thread exit.
 *
 * @param[in] arg
 *      Pointer to the probe thread
 */
static void
probe_thrd_cleanup (void *arg)
{
    probe_t *probe = arg;

    PROBE_TRACE(PROBE_TF_THRD, "%s: Thread cleanup and exit.", __func__);
    rspd_mgmt_close(probe);
    if (probe->client_hdl) {
        pconn_client_close(probe->client_hdl);
        probe->client_hdl = NULL;
    }
    probe_tx_socket_close(probe);
    probe_rx_socket_close(probe);
    evDestroy(probe->ev_ctx);
    probe->tid = NULL;
}

/**
 * @brief
 * Entry point for probe thread.
 * 
 * @param[in] probe
 *      Pointer to the probe
 */
void *
probe_thrd_entry (probe_t *probe)
{
    pconn_client_params_t params;
    int old_val;

    PROBE_TRACE(PROBE_TF_THRD, "%s: Probe thread %s started.",
            __func__, probe->params.name);
    PROBE_TRACE(PROBE_TF_THRD, "%s: probe 0x%08x, protocol %d, port %d, "
            "size %d, count %d, interval %d.", __func__,
            probe, probe->params.proto, probe->params.dst_port,
            probe->params.pkt_size, probe->params.pkt_count,
            probe->params.pkt_interval);
    if (pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, &old_val) < 0) {
        PROBE_LOG(LOG_ERR, "%s: Set cancel state ERROR(%d)!", __func__, errno);
    }
    if (pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &old_val) < 0) {
        PROBE_LOG(LOG_ERR, "%s: Set cancel type ERROR(%d)!", __func__, errno);
    }
    pthread_cleanup_push(probe_thrd_cleanup, probe);

    patricia_root_init(&probe->dst_pat, FALSE, sizeof(in_addr_t), 0);
    if (evCreate(&probe->ev_ctx) < 0) {
        PROBE_LOG(LOG_ERR, "%s: evCreate ERROR(%d)!", __func__, errno);
        goto ret_err;
    }

    bzero(&params, sizeof(params));
    params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_RE;
    params.pconn_port = probe_mngr.port;
    params.pconn_num_retries = CONN_RETRY_DEFAULT;
    params.pconn_event_handler = client_event_hdlr;
    probe->client_hdl = pconn_client_connect_async(&params, probe->ev_ctx,
            client_msg_hdlr, probe);
    if (!probe->client_hdl) {
        PROBE_LOG(LOG_ERR, "%s: Connect to probe manager ERROR!", __func__);
        goto ret_err;
    }

    evMainLoopSyncSighdl(probe->ev_ctx);

ret_err:
    PROBE_LOG(LOG_ERR, "%s: Probe thread exits with ERROR!", __func__);
    return NULL;
}


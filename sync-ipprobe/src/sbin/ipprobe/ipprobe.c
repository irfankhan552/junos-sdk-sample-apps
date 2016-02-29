/*
 * $Id: ipprobe.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ipprobe.c
 * @brief The IP Probe initiator (client) functions
 *
 * \mainpage
 *
 * \section intro_sec Introduction
 *
 * This program
 *   - reads probe configuration,
 *   - connects to the probe manager on the target and sends probe information,
 *   - initializes probe,
 *   - starts timer to schedule sending probe packets to the target,
 *   - receives probe reply packets from the target,
 *   - processes timestamps in received packets and generates statistics for
 *     network performance.
 */

#include "../ipprobe-manager/ipprobe-manager.h"
#include "ipprobe.h"
#include <getopt.h>

/*** Data Structures ***/

extern u_short manager_port;
extern probe_params_t probe_params;
extern char ifd_name[];
extern char username[];
extern char password[];

int probe_start (void);

/** Event context and IDs. */
evContext           ev_ctx;
evFileID            ev_rx_fid;
evTimerID           ev_tx_timer_id;
evTimerID           ev_rx_timer_id;

/** Probe address. */
struct sockaddr_in  manager_addr;
struct sockaddr_in  target_addr;
struct sockaddr_in  local_addr;

int                 manager_socket = -1;
int                 tx_socket = -1;
int                 rx_socket = -1;
u_char              rx_buf[PROBE_PACKET_SIZE_MAX];
u_char              tx_buf[PROBE_PACKET_SIZE_MAX];
int                 rx_count;
int                 rx_err;
int                 tx_count;
ipc_pipe_t          *manager_pipe = NULL;
probe_packet_data_t *data_buf = NULL;

/** The interface traffic statistics. */
stat_if_traff_t     ifd_abs_traff1;
stat_if_traff_t     ifd_abs_traff2;
stat_if_traff_t     ifd_traff1;
stat_if_traff_t     ifd_traff2;
stat_if_traff_t     ifl_local_abs_traff1;
stat_if_traff_t     ifl_local_abs_traff2;
stat_if_traff_t     ifl_local_traff1;
stat_if_traff_t     ifl_local_traff2;

/** The output flag. */
u_char              packet_detail = 0;
u_char              stats_detail = 0;
u_char              interface_detail = 0;

/*** STATIC/INTERNAL Functions ***/

/**
 * Calculate ICMP checksum.
 *
 * @param[in] buf
 *      Pointer to ICMP packet
 *
 * @param[in] len
 *      Length of ICMP packet in byte
 *
 * @return The checksum with network order
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
 * Calculate UDP checksum.
 *
 * @param[in] len
 *      Length of UDP packet in byte
 *
 * @param[in] src_addr
 *      Pointer to the source IP address
 *
 * @param[in] dst_addr
 *      Pointer to the destination IP address
 *
 * @param[in] buf
 *      Pointer to the UDP packet
 *
 * @return The checksum with network order
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
 * Timer signal handler.
 */
static void
timer_sig_handler (int sig UNUSED)
{
    return;
}

/**
 * Close the session to the probe manager on the target.
 */
static void
close_session (void)
{
    if (manager_pipe != NULL) {
        ipc_pipe_detach_socket(manager_pipe, manager_socket);
        ipc_pipe_destroy(manager_pipe);
    }
    if (manager_socket >= 0) {
        close(manager_socket);
    }
    printf("\nSession closed!\n");
    return;
}

/**
 * Close probe sockets.
 */
static void
close_probe (void)
{
    if (evTestID(ev_rx_fid)) {
        evDeselectFD(ev_ctx, ev_rx_fid);
        evInitID(&ev_rx_fid);
    }
    close(tx_socket);
    close(rx_socket);
    return;
}

/**
 * Clear everything up and exit.
 */
static void
clear_exit (void)
{
    close_session();
    close_probe();
    free(data_buf);
    junos_stat_session_shutdown();
    evDestroy(ev_ctx);
    exit(0);
}

/**
 * Calculate the time difference.
 *
 * @param[in] time1
 *      The earlier time.
 *
 * @param[in] time2
 *      The later time.
 *
 * @return The time difference.
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
 * Display the interface traffic statistics.
 */
static void
show_interface_stats (void)
{
    junos_stat_ifd_get_traff_by_name(ifd_name, &ifd_traff2);
    junos_stat_ifd_get_abs_traff_by_name(ifd_name, &ifd_abs_traff2);
    junos_stat_ifl_get_local_traff_by_name(ifd_name, 0,
            &ifl_local_traff2);
    junos_stat_ifl_get_abs_local_traff_by_name(ifd_name, 0,
            &ifl_local_abs_traff2);

    printf("\nifd                        abs_delta           clear_delta\n");
    printf("Input bytes:    %20qu  %20qu\n",
            ifd_abs_traff2.ibytes - ifd_abs_traff1.ibytes,
            ifd_traff2.ibytes - ifd_traff1.ibytes);
    printf("Input packets:  %20qu  %20qu\n",
            ifd_abs_traff2.ipackets - ifd_abs_traff1.ipackets,
            ifd_traff2.ipackets - ifd_traff1.ipackets);
    printf("Output bytes:   %20qu  %20qu\n",
            ifd_abs_traff2.obytes - ifd_abs_traff1.obytes,
            ifd_traff2.obytes - ifd_traff1.obytes);
    printf("Output packets: %20qu  %20qu\n",
            ifd_abs_traff2.opackets - ifd_abs_traff1.opackets,
            ifd_traff2.opackets - ifd_traff1.opackets);
    printf("\nifl local                  abs_delta           clear_delta\n");
    printf("Input bytes:    %20qu  %20qu\n",
            ifl_local_abs_traff2.ibytes - ifl_local_abs_traff1.ibytes,
            ifl_local_traff2.ibytes - ifl_local_traff1.ibytes);
    printf("Input packets:  %20qu  %20qu\n",
            ifl_local_abs_traff2.ipackets - ifl_local_abs_traff1.ipackets,
            ifl_local_traff2.ipackets - ifl_local_traff1.ipackets);
    printf("Output bytes:   %20qu  %20qu\n",
            ifl_local_abs_traff2.obytes - ifl_local_abs_traff1.obytes,
            ifl_local_traff2.obytes - ifl_local_traff1.obytes);
    printf("Output packets: %20qu  %20qu\n",
            ifl_local_abs_traff2.opackets - ifl_local_abs_traff1.opackets,
            ifl_local_traff2.opackets - ifl_local_traff1.opackets);
    return;
}

/**
 * Process received probe packets and calculate statistics.
 */
static void
process_probe_packets (void)
{
    packet_stats_t *packet_stats;
    int i;
    float max_sd_delay = 0;
    float max_ds_delay = 0;
    float max_rr_delay = 0;
    float sum_sd_delay = 0;
    float sum_ds_delay = 0;
    float sum_rr_delay = 0;
    float max_sd_jitter = 0;
    float max_ds_jitter = 0;
    float max_rr_jitter = 0;
    float sum_sd_jitter = 0;
    float sum_ds_jitter = 0;
    float sum_rr_jitter = 0;

    printf("\n\nReceived %d packets.\n", rx_count);
    if (rx_count == 0) {
        return;
    }

    packet_stats = (packet_stats_t *)calloc(rx_count, sizeof(packet_stats_t));
    INSIST_ERR(packet_stats != NULL);

    for (i = 0; i < rx_count; i++) {
        if (probe_params.protocol != IPPROTO_ICMP) {
            packet_stats[i].sd_delay = time_diff(&data_buf[i].tx_time,
                    &data_buf[i].target_rx_time);

            packet_stats[i].ds_delay = time_diff(&data_buf[i].target_tx_time,
                    &data_buf[i].rx_time);
        }
        packet_stats[i].rrt = time_diff(&data_buf[i].tx_time,
                &data_buf[i].rx_time);

        if (i == (probe_params.packet_count - 1)) {
            continue;
        }
        if (probe_params.protocol != IPPROTO_ICMP) {
            packet_stats[i].sd_jitter = time_diff(&data_buf[i].target_rx_time,
                    &data_buf[i + 1].target_rx_time) -
                    time_diff(&data_buf[i].tx_time,
                    &data_buf[i + 1].tx_time);

            packet_stats[i].ds_jitter = time_diff(&data_buf[i].rx_time,
                    &data_buf[i + 1].rx_time) -
                    time_diff(&data_buf[i].target_tx_time,
                    &data_buf[i + 1].target_tx_time);
        }
        packet_stats[i].rr_jitter = time_diff(&data_buf[i].rx_time,
                &data_buf[i + 1].rx_time) -
                time_diff(&data_buf[i].tx_time,
                &data_buf[i + 1].tx_time);
    }

    if (stats_detail) {
        printf("\n\nPacket   sd-delay   ds-delay  total-delay  sd-jitter "
                "ds-jitter rr-jitter\n");
        for (i = 0; i < rx_count; i++) {
            printf("  %3d   %8.3f    %8.3f %8.3f     %8.3f  %8.3f  %8.3f\n",
                    i,
                    packet_stats[i].sd_delay,
                    packet_stats[i].ds_delay,
                    packet_stats[i].rrt,
                    packet_stats[i].sd_jitter,
                    packet_stats[i].ds_jitter,
                    packet_stats[i].rr_jitter);
        }
    }

    for (i = 0; i < rx_count; i++) {
        if (packet_stats[i].sd_delay > max_sd_delay) {
            max_sd_delay = packet_stats[i].sd_delay;
        }
        if (packet_stats[i].ds_delay > max_ds_delay) {
            max_ds_delay = packet_stats[i].ds_delay;
        }
        if (packet_stats[i].rrt > max_rr_delay) {
            max_rr_delay = packet_stats[i].rrt;
        }
        if (abs(packet_stats[i].sd_jitter) > abs(max_sd_jitter)) {
            max_sd_jitter = packet_stats[i].sd_jitter;
        }
        if (abs(packet_stats[i].ds_jitter) > abs(max_ds_jitter)) {
            max_ds_jitter = packet_stats[i].ds_jitter;
        }
        if (abs(packet_stats[i].rr_jitter) > abs(max_rr_jitter)) {
            max_rr_jitter = packet_stats[i].rr_jitter;
        }
        sum_sd_delay += packet_stats[i].sd_delay;
        sum_ds_delay += packet_stats[i].ds_delay;
        sum_rr_delay += packet_stats[i].rrt;
        sum_sd_jitter += packet_stats[i].sd_jitter;
        sum_ds_jitter += packet_stats[i].ds_jitter;
        sum_rr_jitter += packet_stats[i].rr_jitter;
    }

    printf("\n\n           average_delay  max_delay  average_jitter  "
            "max_jitter\n");
    printf("src->dst   %8.3f ms  %8.3f ms  %8.3f ms   %8.3f ms\n",
            (sum_sd_delay / rx_count), max_sd_delay,
            (sum_sd_jitter / (rx_count - 1)), max_sd_jitter);
    printf("dst->src   %8.3f ms  %8.3f ms  %8.3f ms   %8.3f ms\n",
            (sum_ds_delay / rx_count), max_ds_delay,
            (sum_ds_jitter / (rx_count - 1)), max_ds_jitter);
    printf("round-trip %8.3f ms  %8.3f ms  %8.3f ms   %8.3f ms\n",
            (sum_rr_delay / rx_count), max_rr_delay,
            (sum_rr_jitter / (rx_count - 1)), max_rr_jitter);

    show_interface_stats();

    free(packet_stats);
    return;
}

/**
 * The event handler for receiving timeout.
 */
static void
receive_timeout (evContext ctx UNUSED, void *uap UNUSED,
        struct timespec due UNUSED, struct timespec inter UNUSED)
{
    printf("Receiver timeout!\n");
    process_probe_packets();
    clear_exit();
    return;
}

/**
 * The event handler for sending probe packets.
 */
static void
send_packet (evContext ctx UNUSED, void *uap UNUSED,
        struct timespec due UNUSED, struct timespec inter UNUSED)
{
    probe_packet_t *packet = (probe_packet_t *)tx_buf;
    int send_len;

    do {
        packet->data.seq = tx_count;
        gettimeofday(&packet->data.tx_time, NULL);

        if (probe_params.protocol == IPPROTO_ICMP) {
            packet->icmp.icmp_cksum = 0;
            packet->icmp.icmp_cksum = icmp_cksum(&packet->icmp,
                    probe_params.packet_size - sizeof(struct ip));
        } else if (probe_params.protocol == IPPROTO_UDP) {
            packet->udp.uh_sum = 0;
            packet->udp.uh_sum = udp_cksum(ntohs(packet->udp.uh_ulen),
                    &packet->header.ip_src.s_addr,
                    &packet->header.ip_dst.s_addr,
                    &packet->udp);
        }

        send_len = sendto(tx_socket, packet, probe_params.packet_size, 0,
                (struct sockaddr *)&target_addr, sizeof(target_addr));
        if (send_len < 0) {
            PRINT_ERRORNO("Send probe packet!\n");
        }

        if (++tx_count == probe_params.packet_count) {

            /* Disable the timer. */
            evClearTimer(ev_ctx, ev_tx_timer_id);
            printf("Sent %d probe packets to %s.\n", tx_count,
                    inet_ntoa(target_addr.sin_addr));
            break;
        }
    } while (probe_params.packet_interval == 0);
    return;
}

/**
 * The event handler for receiving probe packets.
 */
static void
receive_probe_packets (evContext ctx UNUSED, void *uap UNUSED,
        int fd UNUSED, int evmask UNUSED)
{
    probe_packet_t *packet = (probe_packet_t *)rx_buf;
    probe_packet_data_t *data = NULL;
    struct sockaddr_in addr;
    struct timeval rx_time;
    int recv_len = 0;
    int addr_len;

    gettimeofday(&rx_time, NULL);
    recv_len = recvfrom(rx_socket, rx_buf, PROBE_PACKET_SIZE_MAX, 0,
            (struct sockaddr *)&addr, &addr_len);

    if (recv_len == 0) {
        return;
    } else if(recv_len < 0) {
        if (errno != EAGAIN) {
            PRINT_ERRORNO("Receive probe packets!\n");
        }
        return;
    }

    if (probe_params.protocol == IPPROTO_UDP) {
        data = (probe_packet_data_t *)rx_buf;
    } else {
        data = &packet->data;
    }
    bcopy(&rx_time, &data->rx_time, sizeof(struct timeval));

    if ((probe_params.protocol == IPPROTO_ICMP)
            && (packet->icmp.icmp_type != ICMP_ECHOREPLY)) {
        rx_err++;
        return;
    }

    if (data->type != PROBE_PACKET_REPLY) {
        rx_err++;
        return;
    }

    memcpy(&data_buf[rx_count], (u_char *)data, sizeof(probe_packet_data_t));
    rx_count++;
    if (packet_detail) {
        printf("Packet %3d: %3d.%03d -> %3d.%03d, "
                "%3d.%03d -> %3d.%03d\n", rx_count,
                data->tx_time.tv_sec % 1000,
                data->tx_time.tv_usec / 1000,
                data->target_rx_time.tv_sec % 1000,
                data->target_rx_time.tv_usec / 1000,
                data->target_tx_time.tv_sec % 1000,
                data->target_tx_time.tv_usec / 1000,
                data->rx_time.tv_sec % 1000,
                data->rx_time.tv_usec / 1000);
    }

    if (rx_count == probe_params.packet_count) {
        process_probe_packets();
        clear_exit();
    }

    /* Reset timer for receive timeout. */
    evResetTimer(ev_ctx, ev_rx_timer_id, receive_timeout, NULL,
            evAddTime(evNowTime(), evConsTime(5, 0)), evConsTime(0, 0));

    return;
}

/**
 * Initialize probe.
 *
 * @return 0 on success, -1 on failure
 */
static int
init_probe (void)
{
    const int on = 1;
    struct sockaddr_in addr;

    /* Create raw socket for sending probe packets. */
    tx_socket = socket(AF_INET, SOCK_RAW, probe_params.protocol);
    if (tx_socket < 0) {
        PRINT_ERRORNO("Create probe socket!\n");
        return -1;
    }

    if (setsockopt(tx_socket, IPPROTO_IP, IP_HDRINCL,
            &on, sizeof(on)) < 0) {
        PRINT_ERRORNO("Set probe socket IP_HDRINCL!\n");
        goto error;
    }

    if (probe_params.protocol == IPPROTO_UDP) {
        rx_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (rx_socket < 0) {
            PRINT_ERRORNO("Create receiver socket!\n");
            return -1;
        }
        if (setsockopt(rx_socket, SOL_SOCKET, SO_REUSEADDR,
                &on, sizeof(on)) < 0) {
            PRINT_ERRORNO("Set receiver socket option!\n");
            goto error;
        }
        bzero(&addr, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(probe_params.src_port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(rx_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            PRINT_ERRORNO("Bind receiver socket!\n");
            return -1;
        }
    } else {
        rx_socket = dup(tx_socket);
    }

    data_buf = (probe_packet_data_t *)calloc(probe_params.packet_count,
            sizeof(probe_packet_data_t));
    INSIST_ERR(data_buf != NULL);

    rx_count = 0;
    rx_err = 0;
    return 0;

error:
    close_probe();
    return -1;
}

/**
 * Prepare and start timer to send probe packets.
 *
 * @return 0 on success, -1 on failure
 */
static int
send_probe_packets (void)
{
    probe_packet_t *packet = (probe_packet_t *)tx_buf;

    bzero(tx_buf, sizeof(tx_buf));

    packet->header.ip_v = IPVERSION;
    packet->header.ip_hl = IP_HEADER_LEN >> 2;
    packet->header.ip_tos = probe_params.tos;
    packet->header.ip_len = htons(probe_params.packet_size);
    packet->header.ip_ttl = DEFAULT_PACKET_TTL;
    packet->header.ip_p = probe_params.protocol;
    packet->header.ip_dst.s_addr = target_addr.sin_addr.s_addr;

    if (probe_params.protocol == IPPROTO_ICMP) {
        packet->icmp.icmp_type = ICMP_ECHO;
        packet->icmp.icmp_code = 0;
        packet->data.type = PROBE_PACKET_REPLY;
    } else if (probe_params.protocol == IPPROTO_UDP) {
        packet->header.ip_src.s_addr = local_addr.sin_addr.s_addr;
        packet->udp.uh_sport = htons(probe_params.src_port);
        packet->udp.uh_dport = htons(probe_params.dst_port);
        packet->udp.uh_ulen = htons(probe_params.packet_size -
                sizeof(struct ip));
        packet->data.type = PROBE_PACKET_REQ;
    } else {
        packet->data.type = PROBE_PACKET_REQ;
    }
    tx_count = 0;

    if (evSetTimer(ev_ctx, send_packet, NULL, evConsTime(0, 0),
            evConsTime(0, probe_params.packet_interval * 1000000),
            &ev_tx_timer_id) < 0) {
        PRINT_ERROR("Set timer to schedule sending packets!");
        return -1;
    }
    return 0;
}

/**
 * Send message to the manager in the target to enable the probe responder.
 *
 * @return 0 on success, -1 on failure
 */
static int
enable_responder (void)
{
    ipc_msg_t req;
    ipc_msg_t *msg;
    msg_start_t msg_data;
    int status = -1;

    bzero(&req, sizeof(req));
    req.type = PROBE_MANAGER_MSG_TYPE;
    req.subtype = PROBE_MANAGER_MSG_START;
    req.length = sizeof(msg_data);

    msg_data.protocol = probe_params.protocol;
    msg_data.port = probe_params.dst_port;
    if (ipc_msg_write(manager_pipe, &req, &msg_data)) {
        PRINT_ERRORNO("Write message!");
        goto exit;
    } else if(ipc_pipe_write(manager_pipe, NULL)) {
        PRINT_ERRORNO("Write pipe!");
        goto exit;
    }

    do {
        status = ipc_pipe_read(manager_pipe);
    } while ((status < 0) && (errno == EAGAIN));

    if (status < 0) {
        PRINT_ERRORNO("Read from pipe!");
        goto exit;
    }

    status = -1;
    msg = ipc_msg_read(manager_pipe);
    if ((msg->type != PROBE_MANAGER_MSG_TYPE)
            || (msg->subtype != PROBE_MANAGER_MSG_ACK)) {
        PRINT_ERROR("Read ACK message! type: %d", msg->subtype);
        goto exit;
    }
    status = 0;

exit:
    ipc_pipe_write_clean(manager_pipe);
    ipc_pipe_read_clean(manager_pipe);
    return status;
}


/**
 * Connect to the manager on the target.
 *
 * @return 0 on success, -1 on failure
 */
static int
connect_manager (void)
{
    struct itimerval timer_val;
    struct timeval timeout;
    sig_t old_sig_handler;

    /* Create socket to connect to the manager on the target. */
    manager_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (manager_socket < 0) {
        PRINT_ERRORNO("Create manager socket!\n");
        return -1;
    }

    /* Set signal handler, save the old one. */
    old_sig_handler = signal(SIGALRM, timer_sig_handler);

    /* Set socket timeout for send and receive. */
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(manager_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout,
            sizeof(timeout));
    setsockopt(manager_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
            sizeof(timeout));

    /* Set timer for connection timeout. */
    timer_val.it_value.tv_sec = 5;
    timer_val.it_value.tv_usec = 0;
    timer_val.it_interval.tv_sec = 0;
    timer_val.it_interval.tv_usec = 0;
    if (setitimer(ITIMER_REAL, &timer_val, NULL) != 0) {
        PRINT_ERRORNO("Enable connect timeout timer!\n");
        goto error;
    }

    manager_addr.sin_port = htons(manager_port);
    if (connect(manager_socket, (struct sockaddr *)&manager_addr,
            sizeof(manager_addr)) < 0) {
        if (errno == EINTR) {
            PRINT_ERROR("Connect to the manager TIMEOUT!\n");
        } else {
            PRINT_ERRORNO("Connect to the manager\n");
        }
        goto error;
    }

    /* Disable the timer.
     * Not return error if disable timer failed, the timer will
     * be disabled when it expires.
     */
    timer_val.it_value.tv_sec = 0;
    timer_val.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &timer_val, NULL);

    /* Create pipe for the session. */
    manager_pipe = ipc_pipe_create(PROBE_MANAGER_PIPE_SIZE);
    if (manager_pipe == NULL) {
        PRINT_ERROR("Create manager pipe!\n");
        goto error;
    }

    if (ipc_pipe_attach_socket(manager_pipe, manager_socket)) {
        PRINT_ERROR("Attach session pipe!");
        goto error;
    }

    /* Restore signal handler. */
    signal(SIGALRM, old_sig_handler);
    return 0;

error:
    /* Restore signal handler. */
    signal(SIGALRM, old_sig_handler);
    close_session();
    return -1;
}

static int
help (void)
{
    printf("  ipprobe [-h] -p probe -t target\n");
    return 0;
}

static int
cmd_opt_hdlr (char c, char *args)
{
    switch (c) {
    case 'h':
        help();
        exit(0);
    case 't':
        if (!inet_aton(args, &manager_addr.sin_addr) ||
                !inet_aton(args, &target_addr.sin_addr)) {
            PRINT_ERROR("Reading IP address! %s\n", args);
            exit(0);
        }
        break;
    case 'p':
        strlcpy(probe_params.name, args, sizeof(probe_params.name));
        break;
    case 'o':
        if (strcmp(args, "packet") == 0) {
            packet_detail = 1;
        } else if(strcmp(args, "statistics") == 0) {
            stats_detail = 1;
        } else if(strcmp(args, "interface") == 0) {
            interface_detail = 1;
        } else if(strcmp(args, "all") == 0) {
            packet_detail = 1;
            stats_detail = 1;
            interface_detail = 1;
        }
    }
    return 0;
}

/*** GLOBAL/EXTERNAL Functions ***/
int
probe_start (void)
{
    int addr_len;

    if (probe_params.protocol != IPPROTO_ICMP) {
        printf("Connect to the manager on port %d...", manager_port);
        if (connect_manager() < 0) {
            goto error_exit;
        }
        getsockname(manager_socket, (struct sockaddr *)&local_addr,
                &addr_len);
        if (local_addr.sin_addr.s_addr == 0) {
            local_addr.sin_addr.s_addr = htonl(0x7F000001);
        }
        printf("from %s OK.\n", inet_ntoa(local_addr.sin_addr));

        printf("Enable responder on protocol %d, port %d...",
                probe_params.protocol, probe_params.dst_port);
        if (enable_responder() < 0) {
            goto error_exit;
        }
        printf("OK.\n");
    }

    if (init_probe() < 0) {
        goto error_exit;
    }

    /* Start a timer for receive timeout. */
    if (evSetTimer(ev_ctx, receive_timeout, NULL,
            evAddTime(evNowTime(), evConsTime(5, 0)),
            evConsTime(0, 0), &ev_rx_timer_id) < 0) {
        PRINT_ERROR("Start timer for receive timeout!\n");
        goto error_exit;
    }

    /* Add receiver socket to select loop. */
    if (evSelectFD(ev_ctx, rx_socket, EV_READ, receive_probe_packets, NULL,
            &ev_rx_fid) < 0) {
        PRINT_ERROR("Add receiver socket to select!\n");
        goto error_exit;
    }

    junos_stat_session_init();
    junos_stat_ifd_get_traff_by_name(ifd_name, &ifd_traff1);
    junos_stat_ifd_get_abs_traff_by_name(ifd_name, &ifd_abs_traff1);
    junos_stat_ifl_get_local_traff_by_name(ifd_name, 0,
            &ifl_local_traff1);
    junos_stat_ifl_get_abs_local_traff_by_name(ifd_name, 0,
            &ifl_local_abs_traff1);

    if (send_probe_packets() < 0) {
        goto error_exit;
    }

    return 0;

error_exit:
    return -1;
}

/**
 * The main function of the probe.
 *
 *
 * @param[in] argc
 *      Number of command line arguments
 *
 * @param[in] argv
 *      String array of command line arguments
 *
 * @return 0 always
 */
int
main (int argc, char **argv)
{
    junos_sdk_app_ctx_t app_ctx;
    struct option cmd_opts[] = {
        {"help", 1, NULL, 'h' },
        {"target", 1, NULL, 't' },
        {"probe", 1, NULL, 'p' },
        {"output", 1, NULL, 'o' },
        { NULL, 0, NULL, 0}
    };

    bzero(&manager_addr, sizeof(manager_addr));
    bzero(&target_addr, sizeof(target_addr));
    manager_addr.sin_family = AF_INET;
    target_addr.sin_family = AF_INET;

    app_ctx = junos_app_create_non_daemon_ctx(argc, argv, "ipprobe",
            "ipprobe", 0);

    /* Create event libary context. */
    if (evCreate(&ev_ctx) < 0) {
        PRINT_ERROR("Create event context!\n");
        goto exit;
    }
    junos_set_app_event_ctx(app_ctx, ev_ctx);
    junos_app_event_loop_enable(app_ctx);
    junos_reg_cmd_line_opts(app_ctx, cmd_opts);
    junos_set_app_cb_cmd_line_opts(app_ctx, cmd_opt_hdlr);
    junos_set_app_cb_config_read(app_ctx, config_read);

    junos_app_init(app_ctx);

exit:
    clear_exit();
    exit(0);
}


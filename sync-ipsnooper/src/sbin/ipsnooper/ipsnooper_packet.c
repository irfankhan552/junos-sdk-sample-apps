/*
 * $Id: ipsnooper_packet.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ipsnooper_packet.c
 * @brief Related to processing packets in the fast path
 *
 * The packet thread is running on packet CPU. It
 *   - receives the packet,
 *   - retrieve packet information,
 *   - write packet inforamtion to packet pipes of session threads.
 */

#include "ipsnooper.h"

extern ssn_thrd_t ssn_thrd[MSP_MAX_CPUS];
extern pkt_thrd_t pkt_thrd[MSP_MAX_CPUS];

/*** STATIC/INTERNAL Functions ***/

/**
 * Packet process
 *
 * Retrieve required information and send packet back.
 *
 * @param[in] thrd
 *      Pointer to the packet thread data
 *
 * @param[in] jb
 *      Pointer to jbuf
 */
static void
packet_process (pkt_thrd_t *thrd, struct jbuf *jb)
{
    char src_str[IPV4_ADDR_STR_LEN];
    char dst_str[IPV4_ADDR_STR_LEN];
    struct ip *iph = NULL;
    struct jbuf *jb_tmp;
    pkt_msg_t msg;
    int ret = MSP_OK;
    int i, retry = 0;

    /* Pullup IP header. */
    jb_tmp = jbuf_pullup(jb, sizeof(struct ip));
    if (jb_tmp != NULL) {
        jb = jb_tmp;
    } else {
        jbuf_free(jb);
        return;
    }

    /* Extract source and destination address. */
    iph = jbuf_to_d(jb, struct ip *);
    strncpy(src_str, inet_ntoa(iph->ip_src), IPV4_ADDR_STR_LEN);
    strncpy(dst_str, inet_ntoa(iph->ip_dst), IPV4_ADDR_STR_LEN);

    logging(LOG_INFO, "%s: CPU(%d) %s -> %s",
            __func__, thrd->pkt_thrd_cpu, src_str, dst_str);

    /* Construct packet information message before sending it out. */
    msg.pkt_msg_proto = iph->ip_p;
    msg.pkt_msg_src_addr = iph->ip_src;
    msg.pkt_msg_dst_addr = iph->ip_dst;

    switch (jbuf_get_rcv_subunit(jb)) {
    case 0:
        /* Send it back to unit 0. */
        jbuf_set_xmit_subunit(jb, 0);
        break;
    case 1:
        /* Forward it to unit 2 without any change. */
        jbuf_set_xmit_subunit(jb, 2);
        break;
    case 2:
        /* Forward it to unit 1 without any change. */
        jbuf_set_xmit_subunit(jb, 1);
        break;
    default:
        logging(LOG_ERR, "%s: Don't support traffic from unit %d.",
                __func__, jbuf_get_rcv_subunit(jb));
        /* Drop the packet. */
        jbuf_free(jb);
        return;
    }

    /* Send packets out. */
    retry = PACKET_LOOP_SEND_RETRY;
    do {
        ret = msp_data_send(thrd->pkt_thrd_data_hdl, (void *)jb,
                MSP_MSG_TYPE_PACKET);
    } while ((ret == MSP_DATA_SEND_RETRY) && (retry-- > 0));

    if (ret != MSP_OK) {
        logging(LOG_ERR, "%s: CPU %d sent data ERROR! error: %d",
                __func__, thrd->pkt_thrd_cpu, ret);
    }

    /* Write traffic info to all session thread packet pipes. */
    for (i = 0; i < MSP_MAX_CPUS; i++) {
        if (ssn_thrd[i].ssn_thrd_tid == NULL) {
            continue;
        }
        INSIST_ERR(msp_spinlock_lock(&ssn_thrd[i].ssn_thrd_pipe_lock) ==
                MSP_OK);
        write(ssn_thrd[i].ssn_thrd_pkt_pipe[PIPE_WRITE], &msg,
                sizeof(pkt_msg_t));
        INSIST_ERR(msp_spinlock_unlock(&ssn_thrd[i].ssn_thrd_pipe_lock) ==
                MSP_OK);
    }

}

/**
 * Entry point for packet processing threads
 *
 * @param[in] arg
 *      Packet loop parameters, including user data, loop handle
 *      and CPU number
 */
static void *
packet_loop (msp_dataloop_args_t *arg)
{
    sigset_t sig_mask;
    int type;
    void *ptr;

    logging(LOG_INFO, "%s: Packet loop started on CPU: %d",
            __func__, arg->dloop_number);

    /* Unblock SIGQUIT signal to this thread. */
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGQUIT);
    pthread_sigmask(SIG_UNBLOCK, &sig_mask, NULL);

    /* Fill up thread data structure. */
    pkt_thrd[arg->dloop_number].pkt_thrd_cpu = arg->dloop_number;
    pkt_thrd[arg->dloop_number].pkt_thrd_tid = pthread_self();
    pkt_thrd[arg->dloop_number].pkt_thrd_data_hdl = arg->dhandle;

    while(1) {
        ptr = msp_data_recv(arg->dhandle, &type);
        if (ptr) {
            if (type == MSP_MSG_TYPE_PACKET) {
                packet_process(&pkt_thrd[arg->dloop_number],
                        (struct jbuf *)ptr);
            } else {
                jbuf_free(ptr);
            }
        }
    }
    return NULL;
}

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Packet thread exit.
 *
 * @param[in] thrd
 *      Pointer to the packet thread data
 */
void
packet_thread_exit (pkt_thrd_t *thrd)
{
    /* No cleanup. */
    logging(LOG_INFO, "%s: Packet thread %d exit.",
            __func__, thrd->pkt_thrd_cpu);

    pthread_exit(NULL);
}

/**
 * Initialize packet loops
 *
 * @return MSP_OK on success, MSP error code on failure
 */
int
packet_loop_init (void)
{
    int ret;

    logging(LOG_INFO, "%s: Initializing packet loops.", __func__);

    bzero(pkt_thrd, sizeof(pkt_thrd));

    /* Create data loops on all data CPUs with no user data.
     * It returns error if any data CPU is not available
     * or creating loop failed.
     */
    ret = msp_data_create_loops(packet_loop, NULL);
    if (ret != MSP_OK) {
        logging(LOG_ERR, "%s: Create packet loops ERROR! error: %d",
                __func__, ret);
    }

    return ret;
}


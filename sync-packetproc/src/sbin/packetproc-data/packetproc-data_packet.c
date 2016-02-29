/*
 * $Id: packetproc-data_packet.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file packetproc-data_packet.c
 * @brief Related to processing packets in the fast path
 * 
 * These functions and types manage packet processing in the fast path
 */

#include "packetproc-data.h"

/**
 * Options of creating data loops on all data CPUs:
 * 1: Create data loops with msp_data_create_loops().
 * 2: Create data loops with msp_data_create_loop_on_cpu().
 * 3: Create data loops with pthread APIs.
 */
#define OPT_CREATE_LOOPS        1
#define OPT_CREATE_LOOP_ON_CPU  2
#define OPT_CREATE_THREAD       3

#define CREATE_LOOP_OPT     OPT_CREATE_LOOPS

/** Packet thread data. */
packet_thread_t packet_thrd[MSP_MAX_CPUS];

/**
 * SIGUSR1 signal handler.
 *
 * @param[in] signo
 *      Signal number
 */
static void
data_thread_quit (int signo UNUSED)
{
    logging(LOG_INFO, "%s: Thread exiting...", __func__);

    /* No thread specific data needs to be cleared. */
    pthread_exit(NULL);
}

/**
 * Packet process
 *
 * Process received IP packet.
 *
 * @param[in] jb
 *     Pointer to jbuf
 * 
 * @param[in] thrd
 *     Pointer to the packet thread data structure
 */
static void
packet_process (struct jbuf *jb, packet_thread_t *thrd)
{
    struct ip *iph = NULL;
    struct jbuf *jb_tmp = NULL;
    char src_str[INET_ADDRSTRLEN];
    char dst_str[INET_ADDRSTRLEN];
    struct in_addr tmp_addr;
    int ret = MSP_OK;
    char in_vrf_name[64];
    int count = 0;

    /* Pull out IP header. */
    jb_tmp = jbuf_pullup(jb, sizeof(struct ip));
    if (jb_tmp != NULL) {
        jb = jb_tmp;
    }
    
    iph = jbuf_to_d(jb, struct ip *); /* must come after jbuf pullup */

    if (vrf_getvrfnamebyindex(jbuf_getvrf(jb), AF_INET, in_vrf_name, 64) < 0) {
        logging(LOG_ERR, "%s: Get input VRF name ERROR! idx: %d",
                __func__, jbuf_getvrf(jb));
    }
    inet_ntop(AF_INET, &iph->ip_src, src_str, INET_ADDRSTRLEN);
    inet_ntop(AF_INET, &iph->ip_dst, dst_str, INET_ADDRSTRLEN);

    logging(LOG_INFO, "%s: %s.%d %s -> %s",
            __func__, in_vrf_name, jbuf_get_rcv_subunit(jb), src_str, dst_str);

    switch (jbuf_get_rcv_subunit(jb)) {
    case 0:
        /* Send it back to unit 0, flip src and dst. */
        jbuf_set_xmit_subunit(jb, 0);
        tmp_addr = iph->ip_src;
        iph->ip_src = iph->ip_dst;
        iph->ip_dst = tmp_addr;
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

    count = 100;
    if (thrd->thrd_data_hdl) {
        do {
            ret = msp_data_send(thrd->thrd_data_hdl, jb,
                    MSP_MSG_TYPE_PACKET);
        } while((ret == MSP_DATA_SEND_RETRY) && (count-- > 0));
    } else if (thrd->thrd_fifo_hdl) {
        do {
            ret = msp_fifo_send(thrd->thrd_fifo_hdl, jb,
                    MSP_MSG_TYPE_PACKET);
        } while((ret == MSP_DATA_SEND_RETRY) && (count-- > 0));
    } else {
        logging(LOG_ERR, "%s: Handler ERROR!", __func__);
    }

    if(ret != MSP_OK) {
        logging(LOG_ERR, "%s: Send data ERROR! err: %d",
                __func__, ret);
        jbuf_free(jb);
    }
    return;
}

#if ((CREATE_LOOP_OPT == OPT_CREATE_LOOPS) ||  \
     (CREATE_LOOP_OPT == OPT_CREATE_LOOP_ON_CPU))
/**
 * Entry point for packet processing threads
 * 
 * @param[in] arg
 *     Data loop parameters, including user data, loop handle, and CPU number
 * 
 */
static void *
packet_loop (msp_dataloop_args_t *arg)
{
    msp_data_handle_t data_hdl = arg->dhandle;
    int cpu_num = arg->dloop_number;
    int type;
    void *ptr;
    sigset_t sig_mask;
    
    /* Block SIGTERM from this thread so it is only delivered to the 
     * main thread */
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sig_mask, NULL);

    logging(LOG_INFO, "%s: data handle: 0x%x, CPU num: %d",
            __func__, data_hdl, cpu_num);

    packet_thrd[cpu_num].thrd_cpu = cpu_num;
    packet_thrd[cpu_num].thrd_data_hdl = data_hdl;
    packet_thrd[cpu_num].thrd_fifo_hdl = 0;
    packet_thrd[cpu_num].thrd_tid = pthread_self();
    while (1) {
        if ((ptr = msp_data_recv(data_hdl, &type)) != NULL) {
            if (type == MSP_MSG_TYPE_PACKET) {
                packet_process((struct jbuf *)ptr, &packet_thrd[cpu_num]);
            }
        }
    }

    return NULL;
}
#endif


#if (CREATE_LOOP_OPT == OPT_CREATE_LOOP_ON_CPU)
/**
 * Create data loops on all data CPUs
 * 
 * @param[in] void
 * 
 * @return MSP_OK (0)
 *      MSP_OK is always returned. Any failure of loop creation is logged.
 */
static int
create_loops (void)
{
    int cpu_num;
    msp_dataloop_params_t loop_option;
    msp_dataloop_result_t loop_result;
    int ret;

    bzero(&loop_option, sizeof(loop_option));
    bzero(&loop_result, sizeof(loop_result));

    cpu_num = MSP_NEXT_NONE;
    while (1) {
        cpu_num = msp_env_get_next_data_cpu(cpu_num);
        if (cpu_num == MSP_NEXT_END) {
            break;
        }

        /* Demo how to use user data. */
        packet_thrd[cpu_num].thrd_user_data = cpu_num;
        loop_option.app_data = &packet_thrd[cpu_num].thrd_user_data;

        /* Create loop with user data. */
        ret = msp_data_create_loop_on_cpu(cpu_num, packet_loop,
                &loop_option, &loop_result);
        if (ret != MSP_OK) {
            logging(LOG_ERR,
                    "%s: Create loop on CPU %d ERROR! err: %d",
                    __func__, cpu_num, ret);
        }
    }
    return MSP_OK;
}
#endif

#if (CREATE_LOOP_OPT == OPT_CREATE_THREAD)
/**
 * Entry point for packet processing threads
 * 
 * @param[in] thrd
 *     Pointer to the packet thread data structure
 */
static void *
packet_loop_thread (packet_thread_t *thrd)
{
    int type;
    void *ptr;
    sigset_t sig_mask;
        
    /* Block SIGTERM from this thread so it is only delivered to the 
     * main thread */
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sig_mask, NULL);

    logging(LOG_INFO, "%s: FIFO handle: 0x%x, cpu: %d",
            __func__, thrd->thrd_fifo_hdl, thrd->thrd_cpu);

    while (1) {
        if((ptr = msp_fifo_recv(thrd->thrd_fifo_hdl, &type)) != NULL) {
            if (type == MSP_MSG_TYPE_PACKET) {
                packet_process((struct jbuf *)ptr, thrd);
            }
        }
    }

    return NULL;
}


/**
 * Create data loops on all data CPUs
 * 
 * @return MSP_OK (0)
 *      MSP_OK is always returned. Any failure of thread creation is logged
 */
static int
create_threads (void)
{
    msp_fifo_create_t fifo_create;
    pthread_attr_t attr;
    int ret = MSP_OK;
    int cpu_num;

    cpu_num = MSP_NEXT_NONE;
    while (1) {
        /* Retrieve the next data CPU number. */
        cpu_num = msp_env_get_next_data_cpu(cpu_num);
        if (cpu_num == MSP_NEXT_END) {
            break;
        }

        /* Create FIFO for data CPU. */
        bzero(&fifo_create, sizeof(fifo_create));
        fifo_create.fifo_depth = MSP_FIFO_DEFAULT_DEPTH;
        ret = msp_fifo_create_fifo(cpu_num, &fifo_create);
        if(ret != MSP_OK) {
            logging(LOG_ERR,
                    "%s: Create FIFO for CPU %d ERROR! err: %d",
                    __func__, cpu_num, ret);
            continue;
        }

        packet_thrd[cpu_num].thrd_fifo_hdl = fifo_create.fhandle;
        packet_thrd[cpu_num].thrd_data_hdl = 0;
        packet_thrd[cpu_num].thrd_cpu = cpu_num;

        pthread_attr_init(&attr);
        if (pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)) {
            logging(LOG_ERR,
                    "%s: pthread_attr_setscope() on CPU %d ERROR!",
                    __func__, cpu_num);
        } else if (pthread_attr_setcpuaffinity_np(&attr, cpu_num)) {
            logging(LOG_ERR,
                    "%s: pthread_attr_setcpuaffinity_np() on CPU %d ERROR!",
                    __func__, cpu_num);
        } else if (pthread_create(&packet_thrd[cpu_num].thrd_tid, &attr,
                (void *)&packet_loop_thread, &packet_thrd[cpu_num])) {
            logging(LOG_ERR,
                    "%s: pthread_create() on CPU %d ERROR!",
                    __func__, cpu_num);
        } else if (msp_fifo_register_pthread(packet_thrd[cpu_num].thrd_fifo_hdl,
                packet_thrd[cpu_num].thrd_tid)) {
            logging(LOG_ERR,
                    "%s: msp_fifo_register_pthread() on CPU %d ERROR!",
                    __func__, cpu_num);
        }
    }
    return MSP_OK;
}
#endif


/**
 * Initialize data loops
 * 
 * @return MSP_OK upon successful completion,
 *      otherwise MSP error code.
 */
int
init_packet_loop(void)
{
    int ret;
    sigset_t sig_mask;

    logging(LOG_INFO, "%s: Start creating data loops.", __func__);

    bzero(packet_thrd, sizeof(packet_thrd));

    /* Register SIGUSR1 signal handler
     * All data threads will inherit this signal handler */
    signal(SIGUSR1, data_thread_quit);
    
#if (CREATE_LOOP_OPT == OPT_CREATE_LOOPS)
    /* Create data loops on all data CPUs with no user data.
     * It returns error if any data CPU is not available
     * or creating loop failed.
     */
    ret = msp_data_create_loops(packet_loop, NULL);
#elif (CREATE_LOOP_OPT == OPT_CREATE_LOOP_ON_CPU)
    ret = create_loops();
#elif (CREATE_LOOP_OPT == OPT_CREATE_THREAD)
    ret = create_threads();
#endif

    if(ret != MSP_OK) {
        logging(LOG_ERR, "%s: Create loops on all data CPUs ERROR! err: %d",
                __func__, ret);
    } else {
        // Block SIGUSR1 from this main thread (we shouldn't get it anyway)
        sigemptyset(&sig_mask);
        sigaddset(&sig_mask, SIGUSR1);
        pthread_sigmask(SIG_BLOCK, &sig_mask, NULL);
    }
    
    return ret;
}


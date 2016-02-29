/*
 * $Id: jnx-gateway-ctrl_main.c 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-ctrl_main.c - main function &
 *                           initialization routines
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file jnx-gateway-ctrl_main.c
 * @brief
 * This file contains the control agent 
 * main function, the control agent init,
 * clean up and periodic timer expiry
 * handler function
 * the init function initializes individual
 * submodules, simillarly the cleanup function
 * cleans up individual submodules
 */

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>

#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include <jnx/pconn.h>
#include <jnx/mpsdk.h>
#include <jnx/jnx_paths.h>

/* include the comman header files for
   the sample gateway application */
#include <jnx/jnx-gateway.h>
#include <jnx/jnx-gateway_msg.h>

/* include control agent header file */
#include "jnx-gateway-ctrl.h"


/* Global data structure definition */
/* Control Agent Module control block structure */
jnx_gw_ctrl_t jnx_gw_ctrl;
trace_file_t * jnx_gw_trace_file;

/* function table for ssd event handling */
static struct ssd_ipc_ft jnx_gw_ctrl_ssd_ftbl = {
    jnx_gw_ctrl_ssd_connect_handler,
    jnx_gw_ctrl_ssd_close_handler,
    jnx_gw_ctrl_ssd_msg_handler
};

/***********************************************************
 *                                                         *
 *        LOCAL FUNCTION PROTOTYPE DEFINITIONS             *
 *                                                         *
 ***********************************************************/
static int jnx_gw_ctrl_init(evContext ctxt);
static status_t jnx_gw_ctrl_periodic_conn(void);
static status_t jnx_gw_ctrl_mgmt_conn(void);
void jnx_gw_ctrl_cleanup(int);
void jnx_gw_ctrl_periodic_timer_handler(evContext ctxt, void * uap,
                                        struct timespec due,
                                        struct timespec inter);

/***********************************************************
 *                                                         *
 *        CONTROL AGENT INIT & CLEANUP ROUTINES            *
 *                                                         *
 ***********************************************************/

/**
 * This function opens a server socket  to listen for
 * the management module connections
 * used for operational command/configuration/interface
 * rtb events
 */
static status_t
jnx_gw_ctrl_mgmt_conn(void)
{
    pconn_server_params_t svr_params;

    /* operational commands from the management app */
    memset(&svr_params, 0, sizeof(pconn_server_params_t));

    svr_params.pconn_port            = JNX_GW_CTRL_PORT;
    svr_params.pconn_max_connections = JNX_GW_CTRL_MAX_CONN_IDX;
    svr_params.pconn_event_handler   = jnx_gw_ctrl_mgmt_event_handler;

    if (!(jnx_gw_ctrl.mgmt_conn = 
          pconn_server_create(&svr_params, jnx_gw_ctrl.ctxt,
                              jnx_gw_ctrl_mgmt_msg_handler, NULL))) {
        jnx_gw_log(LOG_INFO, "Management agent server create failed");
        return EFAIL;
    }
    jnx_gw_log(LOG_INFO, "Management agent server create done");
    return EOK;
}

/**
 * This function tries to connect to the management module
 * used for periodic status messages
 */
static status_t
jnx_gw_ctrl_periodic_conn(void)
{
    pconn_client_params_t    clnt_params;

    /* close if any existing connection */
    if (jnx_gw_ctrl.periodic_conn) {
        jnx_gw_log(LOG_INFO, "Management agent connection CLOSED");
        pconn_client_close(jnx_gw_ctrl.periodic_conn);
    }

    /* register for the periodic messages */
    memset(&clnt_params, 0, sizeof(pconn_client_params_t));

    clnt_params.pconn_port      = JNX_GW_MGMT_CTRL_PORT;
    clnt_params.pconn_peer_info.ppi_peer_type  = PCONN_PEER_TYPE_RE;

    /* open a new client connection */
    if (!(jnx_gw_ctrl.periodic_conn = pconn_client_connect(&clnt_params))) {
        jnx_gw_log(LOG_INFO, "Management agent connection FAIL");
        return EFAIL;
    }

    jnx_gw_log(LOG_INFO, "Management agent connection UP");
    return EOK;
}

/**
 * This function initializes the control agent
 * resources, opens the server listen connection, for
 * receiving management module operational command
 * messages, & open the client connection for
 * sending the periodic status messages to the management
 * module
 */

static int
jnx_gw_ctrl_init(evContext ctxt)
{
    uint32_t idx = 0;
    jnx_gw_ctrl_rx_thread_t * prthread = NULL;
    jnx_gw_ctrl_proc_thread_t * pthread = NULL;

    jnx_gw_log(LOG_INFO, "Control agent Initialize");

    /* set the status to init */
    jnx_gw_ctrl.ctxt = ctxt;
    jnx_gw_ctrl.ctrl_status = JNX_GW_CTRL_STATUS_INIT;
    jnx_gw_ctrl.min_prefix_len = 32;

    /* open the trace file */
    jnx_gw_trace_file  = trace_file_open(jnx_gw_trace_file,
                                         PATH_JNX_GW_TRACE,
                                         JNX_GW_TRACE_FILE_SIZE,
                                         JNX_GW_TRACE_FILE_COUNT);

    /* trace all */
    trace_flag_set(jnx_gw_trace_file, TRACE_ALL);

    logging_set_level(LOG_INFO);
    logging_set_mode(LOGGING_SYSLOG);

    /* on sigterm, cleanup self */
    signal(SIGTERM, jnx_gw_ctrl_cleanup);

    /* ignore sighup */
    signal(SIGHUP, SIG_IGN);

    /* ignore sigpipe */
    signal(SIGPIPE, SIG_IGN);

    /* get the control cpu count from mp-sdk API calls (TBD)*/
    jnx_gw_ctrl.ctrl_cpu_count = JNX_GW_CTRL_DFLT_CPU_COUNT;

    /* take one thread for the receive thread */
    jnx_gw_ctrl.recv_thread_count = 1;

    /* rest are the processing threads */
    jnx_gw_ctrl.proc_thread_count = 
        (jnx_gw_ctrl.ctrl_cpu_count - jnx_gw_ctrl.recv_thread_count);

    /* initialize the patricia root structures */

    /* vrf table */
    patricia_root_init(&jnx_gw_ctrl.vrf_db, FALSE,
                       fldsiz(jnx_gw_ctrl_vrf_t, vrf_id),
                       fldoff(jnx_gw_ctrl_vrf_t, vrf_id) -
                       fldoff(jnx_gw_ctrl_vrf_t, vrf_node) -
                       fldsiz(jnx_gw_ctrl_vrf_t, vrf_node));

    /* pic table */
    patricia_root_init(&jnx_gw_ctrl.pic_db, FALSE,
                       fldsiz(jnx_gw_ctrl_data_pic_t, pic_name),
                       fldoff(jnx_gw_ctrl_data_pic_t, pic_name) -
                       fldoff(jnx_gw_ctrl_data_pic_t, pic_node) -
                       fldsiz(jnx_gw_ctrl_data_pic_t, pic_node));

    /* user profile table */
    patricia_root_init(&jnx_gw_ctrl.user_db, FALSE, 
                       fldsiz(jnx_gw_ctrl_user_t, user_name),
                       fldoff(jnx_gw_ctrl_user_t, user_name) -
                       fldoff(jnx_gw_ctrl_user_t, user_node) -
                       fldsiz(jnx_gw_ctrl_user_t, user_node));

    /* data policy table */
    patricia_root_init(&jnx_gw_ctrl.data_db, FALSE, JNX_GW_STR_SIZE, 0);

    /* control policy table */
    patricia_root_init(&jnx_gw_ctrl.ctrl_db, FALSE,
                       fldsiz(jnx_gw_ctrl_policy_t, ctrl_key),
                       fldoff(jnx_gw_ctrl_policy_t, ctrl_key) -
                       fldoff(jnx_gw_ctrl_policy_t, ctrl_node) -
                       fldsiz(jnx_gw_ctrl_policy_t, ctrl_node));

    /* initialize the read write config lock */
    if (pthread_rwlock_init(&jnx_gw_ctrl.config_lock, 0) < 0) {
        jnx_gw_log(LOG_INFO, "Configuration lock init failed");
        goto gw_ctrl_init_cleanup;
    }

    /* initialize the ssd connection for route management */
    if ((jnx_gw_ctrl.ssd_fd =
         ssd_ipc_connect_rt_tbl(NULL, &jnx_gw_ctrl_ssd_ftbl, 0,
                                jnx_gw_ctrl.ctxt)) < 0) {
        jnx_gw_log(LOG_INFO, "SSD server client connect failed");
        goto gw_ctrl_init_cleanup;
    }

    /* initialize the receive buffer pool */
    if (jnx_gw_ctrl_init_buf() != EOK) {
        jnx_gw_log(LOG_INFO, "Packet Buffer poool for signaling failed");
        goto gw_ctrl_init_cleanup;
    }


    /*
     * set up the server connection setup
     * for listening to management connection
     */
    if (jnx_gw_ctrl_mgmt_conn() != EOK) {
        goto gw_ctrl_init_cleanup;
    }

    /*
     * now issue the client connect to the management 
     * agent
     */
    jnx_gw_ctrl_periodic_conn();

    /* now create the receive threads */
    idx = 0;
    while (idx < jnx_gw_ctrl.recv_thread_count) {

        if ((prthread = JNX_GW_MALLOC(JNX_GW_CTRL_ID,
                                      sizeof(*prthread))) == NULL) {
            jnx_gw_log(LOG_INFO, "Packet receive thread(%d) malloc failed",
                       idx);
            goto gw_ctrl_init_cleanup;
        }

        jnx_gw_ctrl_init_list(prthread);

        prthread->rx_thread_status = JNX_GW_CTRL_STATUS_INIT;

        if (pthread_create(&prthread->rx_thread_id, NULL,
                           jnx_gw_ctrl_rx_msg_thread, prthread)) {
            goto gw_ctrl_init_cleanup;
        }

        prthread->rx_thread_next   = jnx_gw_ctrl.recv_threads;
        jnx_gw_ctrl.recv_threads   = prthread;
        jnx_gw_log(LOG_INFO, "Packet receive thread(%d) created", idx);
        idx++;
    }

    /* now create the process threads */
    idx = 0;
    while (idx < jnx_gw_ctrl.proc_thread_count) {

        if ((pthread = JNX_GW_MALLOC(JNX_GW_CTRL_ID,
                                     sizeof(*pthread))) == NULL) {
            jnx_gw_log(LOG_INFO, "Packet processing thread(%d) malloc failed",
                   idx);
            goto gw_ctrl_init_cleanup;
        }

        /* initialize the processing thread mutexes */
        pthread_mutex_init(&pthread->proc_event_mutex, 0);
        pthread_cond_init(&pthread->proc_pkt_event, 0);
        pthread_mutex_init(&pthread->proc_thread_rx.queue_lock, 0);
        pthread->proc_thread_status = JNX_GW_CTRL_STATUS_INIT;

        if (pthread_create(&pthread->proc_thread_id, NULL,
                           jnx_gw_ctrl_proc_msg_thread, pthread)) {
            goto gw_ctrl_init_cleanup;
        }

        pthread->proc_thread_next   = jnx_gw_ctrl.proc_threads;
        jnx_gw_ctrl.proc_threads    = pthread;

        jnx_gw_log(LOG_INFO, "Packet process thread(%d) created", idx);
        idx++;
    }

    /* register the periodic timer function */
    if (evSetTimer(jnx_gw_ctrl.ctxt,
                   jnx_gw_ctrl_periodic_timer_handler,
                   NULL, evNowTime(),
                   evConsTime((JNX_GW_CTRL_PERIODIC_SEC), 0), 0)
        < 0) {

        jnx_gw_log(LOG_INFO, "Hello timer handler event create failed");

        goto gw_ctrl_init_cleanup;
    }


    jnx_gw_log(LOG_INFO, "Control agent initialization done");
    return EOK;

gw_ctrl_init_cleanup:
    /*
     * mark the status as delete, so that the processing
     * threads will close on timer expiry 
     */
    jnx_gw_ctrl.ctrl_status = JNX_GW_CTRL_STATUS_DELETE;

    /* close the ssd connection, for route management */
    if (jnx_gw_ctrl.ssd_fd >= 0) {
        ssd_ipc_close(jnx_gw_ctrl.ssd_fd);
        jnx_gw_ctrl.ssd_fd = -1;
    }

    jnx_gw_log(LOG_INFO, "Control agent initialization failed");
    return EFAIL;
}


/**
 * This function cleans up the resources at top level
 * closes the connections, etc.
 * calls individual submodule clean up routines
 * for cleaning up the resouces
 */

void
jnx_gw_ctrl_cleanup(int signo __unused)
{
    jnx_gw_ctrl_rx_thread_t * prthread = NULL;
    jnx_gw_ctrl_proc_thread_t * pthread = NULL;

    jnx_gw_ctrl.ctrl_status = JNX_GW_CTRL_STATUS_DELETE;

    /* clean up the vrf states */
    jnx_gw_ctrl_clear_vrfs();

    /* clean up the data pic states */
    jnx_gw_ctrl_clear_data_pics();

    prthread = jnx_gw_ctrl.recv_threads;
    while (prthread) {
        prthread->rx_thread_status = JNX_GW_CTRL_STATUS_DELETE;
        prthread = prthread->rx_thread_next;
    }

    pthread = jnx_gw_ctrl.proc_threads;
    while (pthread) {
        pthread->proc_thread_status = JNX_GW_CTRL_STATUS_DELETE;
        pthread = pthread->proc_thread_next;
    }

    if (pthread_rwlock_destroy(&jnx_gw_ctrl.config_lock) != 0) {
        jnx_gw_log(LOG_INFO, "Configuration lock destroy failed");
    }

    jnx_gw_ctrl_destroy_mgmt();

    jnx_gw_log(LOG_INFO, "Cleanup the control agent done");
    /* now exit */
    exit(0);
}

/**
 * This function sends periodic status messages to the 
 * management module & the gre gateway agents
 * @params  ctxt  event Lib context
 * @params  uap   user application pointer
 * @params  due   due time val
 * @params  inter inter time val
 */
void
jnx_gw_ctrl_periodic_timer_handler(evContext ctxt __unused,
                                   void * uap __unused,
                                   struct timespec due __unused,
                                   struct timespec inter __unused)
{
    int msg_len = 0;
    jnx_gw_msg_header_t     *msg_buffer = NULL;
    jnx_gw_msg_sub_header_t *sub_hdr = NULL;
    jnx_gw_periodic_stat_t  *stat_p = NULL;


    /*
     * XXX:TBD:GRE Gateway Agent periodic messages
     * this should not be tied with the connection
     * with the management agent
     */

    /*
     * check whether the connection to the management
     * agent is UP, if not try a reconnect 
     */
    if (jnx_gw_ctrl.ctrl_status != JNX_GW_CTRL_STATUS_UP) {

        if (jnx_gw_ctrl.ctrl_status == JNX_GW_CTRL_STATUS_DELETE) {

            /* clean up the management connections ! */
            jnx_gw_ctrl_destroy_mgmt();

            /* close the ssd connection, for route management */
            if (jnx_gw_ctrl.ssd_fd >= 0) {
                ssd_ipc_close(jnx_gw_ctrl.ssd_fd);
                jnx_gw_ctrl.ssd_fd = -1;
            }

            exit(0);
        }


        jnx_gw_log(LOG_INFO, "Reconnect to management agent");
        jnx_gw_ctrl_periodic_conn();
        return;
    }

    /* send periodic status message to the management module */

    msg_buffer = (typeof(msg_buffer))jnx_gw_ctrl.opcmd_buf;
    memset(msg_buffer, 0, sizeof(*msg_buffer));

    msg_buffer->msg_type = JNX_GW_STAT_PERIODIC_MSG;
    msg_len  = sizeof(*msg_buffer);

    sub_hdr = (typeof(sub_hdr))((char *)msg_buffer + msg_len);
    memset(sub_hdr, 0, sizeof(*sub_hdr));

    sub_hdr->sub_type = JNX_GW_STAT_PERIODIC_CTRL_AGENT;
    sub_hdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
    sub_hdr->length   = htons(sizeof(*sub_hdr) + sizeof(*stat_p));
    msg_len  += sizeof(*sub_hdr);

    stat_p = (typeof(stat_p))((char *)msg_buffer + msg_len);
    memset(stat_p, 0, sizeof(*stat_p));

    stat_p->active_sessions   = htonl(jnx_gw_ctrl.gre_active_sesn_count);
    stat_p->total_sessions    = htonl(jnx_gw_ctrl.gre_sesn_count);
    stat_p->ipip_tunnel_count = htonl(jnx_gw_ctrl.ipip_tunnel_count);

    msg_len  += sizeof(*stat_p);
    msg_buffer->msg_len = htons(msg_len);

    pconn_client_send(jnx_gw_ctrl.periodic_conn, JNX_GW_STAT_PERIODIC_MSG,
                      msg_buffer, msg_len);
}

/**
 * This function initializes the daemon &
 * spawns the control agent daemon 
 */
int
main(int32_t argc , char **argv)
{
    mp_sdk_app_ctx_t app_ctx;
    int rc;

    app_ctx = msp_create_app_ctx(argc, argv, "jnx-gateway-ctrl");
    msp_set_app_cb_init(app_ctx, jnx_gw_ctrl_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


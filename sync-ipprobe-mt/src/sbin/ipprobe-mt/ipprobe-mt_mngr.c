/*
 * $Id: ipprobe-mt_mngr.c 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file ipprobe-mt_mngr.c
 * @brief The probe manager to manage probe threads.
 *
 * These functions create the manager, manage probe threads, handle the
 * connection and messages from probe threads and handle user commands from
 * CLI.
 */

#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <isc/eventlib.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/ip_icmp.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/pconn.h>
#include <jnx/junos_trace.h>
#include "ipprobe-mt.h"
#include IPPROBE_MT_OUT_H

probe_mngr_t probe_mngr;

static LIST_HEAD(, probe_s)  probe_list;

/**
 * @brief
 * Get a probe by client session.
 *
 * @param[in] ssn
 *      Session
 * @return
 *      Pointer to the probe on success, NULL on failure
 */
static probe_t *
probe_get_by_ssn (pconn_session_t *ssn)
{
    probe_t *probe;

    LIST_FOREACH(probe, &probe_list, entry) {
        if (probe->ssn == ssn) {
            return probe;
        }
    }
    return NULL;
}

/**
 * @brief
 * Get a probe by probe name.
 *
 * @param[in] name
 *      Probe name
 * @return
 *      Pointer to the probe on success, NULL on failure
 */
static probe_t *
probe_get_by_name (char *name)
{
    probe_t *probe;

    LIST_FOREACH(probe, &probe_list, entry) {
        if (strncmp(name, probe->params.name, sizeof(probe->params.name))
                == 0) {
            return probe;
        }
    }
    return NULL;
}

/**
 * @brief
 * Probe manager message handler.
 */
static status_t
probe_mngr_msg_hdlr (pconn_session_t *ssn, ipc_msg_t *msg, void *cookie UNUSED)
{
    probe_t *probe;

    if (msg->subtype == PROBE_MGMT_MSG_REG) {
        PROBE_TRACE(PROBE_TF_NORMAL, "%s: Received probe register message.",
            __func__);
        probe = probe_get_by_name((char *)msg->data);
        if (probe) {
            probe->ssn = ssn;

            /* Pass the first destination to the thread. */
            pconn_server_send(ssn, PROBE_MGMT_MSG_ADD_DST, &probe->dst_first,
                    sizeof(probe->dst_first));
        } else {
            PROBE_TRACE(PROBE_TF_MNGR, "%s: Probe thrd %s does not exist.",
                __func__);
        }
    }
    return 0;
}

/**
 * @brief
 * Probe manager server event handler.
 */
static void
probe_mngr_event_hdlr (pconn_session_t *ssn, pconn_event_t event,
        void *cookie UNUSED)
{
    probe_t *probe;

    switch (event) {
    case PCONN_EVENT_ESTABLISHED:
        /* A client is connected. */
        PROBE_TRACE(PROBE_TF_NORMAL, "%s: Client 0x%x is connected.",
                __func__, ssn);
        break;
    case PCONN_EVENT_SHUTDOWN:
        /* A client is dwon. */
        PROBE_TRACE(PROBE_TF_NORMAL, "%s: Client 0x%x is gone.",
                __func__, ssn);
        probe = probe_get_by_ssn(ssn);
        if (probe) {
            probe->ssn = NULL;
        }
        break;
    default:
        PROBE_LOG(LOG_ERR, "%s: Invalid event %d!", __func__, event);
    }
}

/**
 * @brief
 * Close all probe threads.
 */
static void
probe_mngr_probe_close_all (void)
{
    probe_t *probe;

    /* Close all running probe threads. */
    LIST_FOREACH(probe, &probe_list, entry) {
        if (probe->tid) {
            if (pthread_cancel(probe->tid) < 0) {
                PROBE_LOG(LOG_ERR, "%s: Close thread 0x%x ERROR(%d)!",
                        __func__, probe->tid, errno);
            } else {
                PROBE_TRACE(PROBE_TF_MNGR, "%s: Thread 0x%08x is canceled.",
                        __func__, probe->tid);
            }
        }
    }

    /* Wait till all running probe threads are closed. */
    LIST_FOREACH(probe, &probe_list, entry) {
        if (probe->tid) {
            if (pthread_join(probe->tid, NULL) < 0) {
                PROBE_LOG(LOG_ERR, "%s: Wait thread 0x%x close ERROR(%d)!",
                        __func__, probe->tid, errno);
            } else {
                PROBE_TRACE(PROBE_TF_MNGR, "%s: Thread 0x%08x is closed.",
                        __func__, probe->tid);
            }
            probe->tid = 0;
        }
    }
}

/**
 * @brief
 * Start the probe.
 *
 * @param[in] name
 *      Probe name
 * @param[in] dst_addr
 *      Probe destination address
 * @return
 *      0 on success, -1 on failure
 */
int
probe_mngr_probe_start (char *name, in_addr_t dst_addr)
{
    probe_t *probe;
    probe_params_t *params;

    params = probe_params_get(name);
    if (!params) {
        PROBE_TRACE(PROBE_TF_MNGR, "%s: Probe %s is not configured.",
                __func__, name);
        return -1;
    }
    probe = probe_get_by_name(name);
    if (probe) {
        if (probe->tid) {
            PROBE_TRACE(PROBE_TF_MNGR, "%s: Probe thread %s is running, "
                    "add dst address 0x%08x.", __func__, name, dst_addr);
            pconn_server_send(probe->ssn, PROBE_MGMT_MSG_ADD_DST, &dst_addr,
                    sizeof(dst_addr));
        } else {
            PROBE_TRACE(PROBE_TF_MNGR, "%s: Probe thread %s is closed, "
                    "reopen it.", __func__, name);
            probe->dst_first = dst_addr;
            pthread_create(&probe->tid, NULL, (void *)probe_thrd_entry, probe);
        }
    } else {
        probe = calloc(1, sizeof(*probe));
        INSIST_ERR(probe);
        bcopy(params, &probe->params, sizeof(probe->params));
        probe->dst_first = dst_addr;
        LIST_INSERT_HEAD(&probe_list, probe, entry);
        PROBE_TRACE(PROBE_TF_MNGR, "%s: Probe thread %s does not exist, "
                    "create it.", __func__, name);
        PROBE_TRACE(PROBE_TF_MNGR, "%s: protocol %d, port %d, size %d, "
                "count %d, interval %d.", __func__,
                params->proto, params->dst_port, params->pkt_size,
                params->pkt_count, params->pkt_interval);
        pthread_create(&probe->tid, NULL, (void *)probe_thrd_entry, probe);
    }
    return 0;
}

/**
 * @brief
 * Get the result of the probe.
 *
 * @param[in] name
 *      Probe name
 * @return
 *      Pointer to the root of destination patricia tree, NULL on failure
 */
patroot *
probe_mngr_probe_result_get (char *name)
{
    probe_t *probe;

    probe = probe_get_by_name(name);
    if (!probe) {
        PROBE_LOG(LOG_ERR, "%s: Did not find the probe %s.", __func__, name);
        return NULL;
    }
    return &probe->dst_pat;
}

/**
 * @brief
 * Stop the probe.
 *
 * @param[in] name
 *      Probe name
 * @return
 *      0 on success, -1 on failure
 */
int
probe_mngr_probe_stop (char *name)
{
    probe_t *probe;

    probe = probe_get_by_name(name);
    if (!probe) {
        PROBE_LOG(LOG_ERR, "%s: Did not find the probe %s.", __func__, name);
        goto ret_err;
    }
    if (probe->tid) {
        if (pthread_cancel(probe->tid) < 0) {
            PROBE_LOG(LOG_ERR, "%s: Close thread 0x%x ERROR(%d)!",
                    __func__, probe->tid, errno);
            goto ret_err;
        } else {
            PROBE_TRACE(PROBE_TF_MNGR, "%s: Thread 0x%08x is canceled.",
                    __func__, probe->tid);
        }
        if (pthread_join(probe->tid, NULL) < 0) {
            PROBE_LOG(LOG_ERR, "%s: Wait thread 0x%x close ERROR(%d)!",
                    __func__, probe->tid, errno);
            goto ret_err;
        } else {
            PROBE_TRACE(PROBE_TF_MNGR, "%s: Thread 0x%08x is closed.",
                    __func__, probe->tid);
        }
        probe->tid = 0;
    }
    return 0;

ret_err:
    return -1;
}

/**
 * @brief
 * Clear the probe.
 *
 * @param[in] name
 *      Probe name
 * @return
 *      0 on success, -1 on failure
 */
int
probe_mngr_probe_clear (char *name)
{
    probe_t *probe;
    probe_dst_t *dst;

    probe = probe_get_by_name(name);
    if (!probe) {
        PROBE_LOG(LOG_ERR, "%s: Did not find the probe %s.", __func__, name);
        return -1;
    }
    if (probe->tid) {
        PROBE_LOG(LOG_ERR, "%s: Probe %s is running.", __func__, name);
        return -1;
    }
    while ((dst = (probe_dst_t *)patricia_find_next(&probe->dst_pat, NULL))) {
        patricia_delete(&probe->dst_pat, (patnode *)dst);
        free(dst);
    }
    LIST_REMOVE(probe, entry);
    free(probe);
    return 0;
}

/**
 * @brief
 * Close the probe manager.
 */
void
probe_mngr_close (void)
{
    probe_t *probe;

    /* Close all running probe threads. */
    probe_mngr_probe_close_all();

    /* Close the server and all sessions. */
    pconn_server_shutdown(probe_mngr.hdl);
    probe_mngr.hdl = NULL;

    /* Clear the session in local probe thread list. */
    LIST_FOREACH(probe, &probe_list, entry) {
        probe->ssn = NULL;
    }
}

/**
 * @brief
 * Open the probe manager.
 *
 * @param[in] port
 *      TCP port
 * @return
 *      0 on success, -1 on failure
 */
int
probe_mngr_open (uint16_t port)
{
    pconn_server_params_t params;

    if (probe_mngr.hdl) {
        if (probe_mngr.port == port) {
            return 0;
        } else {
            probe_mngr_close();
        }
    }
    probe_mngr.port = port;

    LIST_INIT(&probe_list);

    bzero(&params, sizeof(params));
    params.pconn_port = probe_mngr.port;
    params.pconn_event_handler = probe_mngr_event_hdlr;

    probe_mngr.hdl = pconn_server_create(&params, probe_mngr.ev_ctx,
            probe_mngr_msg_hdlr, NULL);
    if (!probe_mngr.hdl) {
        PROBE_LOG(LOG_ERR, "%s: Open probe manager ERROR!", __func__);
        return -1;
    }
    PROBE_TRACE(PROBE_TF_NORMAL, "%s: The probe manager is open on port %d.",
            __func__, probe_mngr.port);
    return 0;
}

/**
 * @brief
 * Initialize the probe manager.
 *
 * @param[in] lev_ctx
 *      Event context
 * @return
 *      0 on success, -1 on failure
 */
int
probe_mngr_init (evContext lev_ctx)
{
    bzero(&probe_mngr, sizeof(probe_mngr));
    probe_mngr.port = PROBE_MNGR_PORT_DEFAULT;
    probe_mngr.ev_ctx = lev_ctx;

    return 0;
}


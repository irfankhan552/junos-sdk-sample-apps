/*
 * $Id: ipprobe-mt_ui.c 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file ipprobe-mt_ui.c
 * @brief Relating to the user interface (commands)
 * 
 * Contains callback functions that get executed by commands on the router
 */

#include <string.h>
#include <sys/types.h>
#include <sys/queue.h>
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
#include <jnx/mgmt_sock_pub.h>
#include <jnx/ms_parse.h>
#include <jnx/parse_ip.h>
#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>
#include <junoscript/xmlstr.h>
#include "ipprobe-mt.h"
#include IPPROBE_MT_OUT_H
#include IPPROBE_MT_ODL_H

/**
 * @brief
 * Displays the probe report. 
 * 
 * @param[in] msp
 *     management socket pointer
 * @param[in] csb
 *     parsed info status (contains subcodes/arguments)
 * @param[in] unparsed
 *     unparsed command string
 * @return
 *      0 if no error; 1 to terminate connection
 */
static int32_t
show_probe (mgmt_sock_t *msp, parse_status_t* csb UNUSED, char *unparsed)
{
    probe_params_t *params;
    patroot *dst_pat;
    probe_dst_t *dst;
    struct in_addr addr;
    char buf[64];

    XML_OPEN(msp, ODCI_PROBE_REPORT);
    params = probe_params_get(unparsed);
    if (!params) {
        XML_ELT(msp, ODCI_PROBE_MSG, "Probe %s is not configured!", unparsed);
        XML_CLOSE(msp, ODCI_PROBE_REPORT);
        return 0;
    }
    XML_OPEN(msp, ODCI_PROBE_INFO);
    XML_ELT(msp, ODCI_PROBE_NAME, "%s", params->name);
    XML_ELT(msp, ODCI_PKT_SIZE, "%d", params->pkt_size);
    XML_ELT(msp, ODCI_PKT_COUNT, "%d", params->pkt_count);
    XML_ELT(msp, ODCI_PKT_INTERVAL, "%d", params->pkt_interval);
    XML_CLOSE(msp, ODCI_PROBE_INFO);

    dst_pat = probe_mngr_probe_result_get(unparsed);    
    if (!dst_pat) {
        XML_ELT(msp, ODCI_PROBE_MSG, "Probe %s thread is not setup!", unparsed);
        XML_CLOSE(msp, ODCI_PROBE_REPORT);
        return 0;
    }
    dst = (probe_dst_t *)patricia_find_next(dst_pat, NULL);
    while (dst) {
        XML_OPEN(msp, ODCI_PROBE_RESULT);

        XML_OPEN(msp, ODCI_RESULT_DST);
        addr.s_addr = dst->dst_addr;
        XML_ELT(msp, ODCI_PROBE_DST, "%s", inet_ntoa(addr));
        XML_CLOSE(msp, ODCI_RESULT_DST);

        XML_OPEN(msp, ODCI_RESULT_TABLE);
        XML_ELT(msp, ODCI_PROBE_PATH, "%s", "source->destination");
        sprintf(buf, "%8.3f", dst->result.delay_sd_average);
        XML_ELT(msp, ODCI_AVERAGE_DELAY, "%s", buf);
        sprintf(buf, "%8.3f", dst->result.delay_sd_max);
        XML_ELT(msp, ODCI_MAX_DELAY, "%s", buf);
        sprintf(buf, "%8.3f", dst->result.jitter_sd_average);
        XML_ELT(msp, ODCI_AVERAGE_JITTER, "%s", buf);
        sprintf(buf, "%8.3f", dst->result.jitter_sd_max);
        XML_ELT(msp, ODCI_MAX_JITTER, "%s", buf);

        XML_ELT(msp, ODCI_PROBE_PATH, "%s", "destination->source");
        sprintf(buf, "%8.3f", dst->result.delay_ds_average);
        XML_ELT(msp, ODCI_AVERAGE_DELAY, "%s", buf);
        sprintf(buf, "%8.3f", dst->result.delay_ds_max);
        XML_ELT(msp, ODCI_MAX_DELAY, "%s", buf);
        sprintf(buf, "%8.3f", dst->result.jitter_ds_average);
        XML_ELT(msp, ODCI_AVERAGE_JITTER, "%s", buf);
        sprintf(buf, "%8.3f", dst->result.jitter_ds_max);
        XML_ELT(msp, ODCI_MAX_JITTER, "%s", buf);

        XML_ELT(msp, ODCI_PROBE_PATH, "%s", "round->trip");
        sprintf(buf, "%8.3f", dst->result.delay_rr_average);
        XML_ELT(msp, ODCI_AVERAGE_DELAY, "%s", buf);
        sprintf(buf, "%8.3f", dst->result.delay_rr_max);
        XML_ELT(msp, ODCI_MAX_DELAY, "%s", buf);
        sprintf(buf, "%8.3f", dst->result.jitter_rr_average);
        XML_ELT(msp, ODCI_AVERAGE_JITTER, "%s", buf);
        sprintf(buf, "%8.3f", dst->result.jitter_rr_max);
        XML_ELT(msp, ODCI_MAX_JITTER, "%s", buf);
        XML_CLOSE(msp, ODCI_RESULT_TABLE);

        XML_CLOSE(msp, ODCI_PROBE_RESULT);
        dst = (probe_dst_t *)patricia_find_next(dst_pat, (patnode *)dst);
    }

    XML_CLOSE(msp, ODCI_PROBE_REPORT);
    return 0;
}

/**
 * @brief
 * Request to start a probe. 
 * 
 * @param[in] msp
 *     management socket pointer
 * @param[in] csb
 *     parsed info status (contains subcodes/arguments)
 * @param[in] unparsed
 *     unparsed command string
 * @return
 *      0 if no error; 1 to terminate connection
 */
static int32_t
start_probe (mgmt_sock_t *msp UNUSED, parse_status_t* csb UNUSED,
        char *unparsed)
{
    char sep[] = "\t ";
    char *probe_name;
    char *word;
    int af = AF_INET;
    struct in_addr addr;

    if (!unparsed) {
        /* This should never happen because of the 'mandatory' flag in DDL. */
        return 0;
    }
    XML_OPEN(msp, ODCI_PROBE_REPORT);
    word = strsep(&unparsed, sep);
    if (strncmp(word, "probe", 5) == 0) {
        probe_name = strsep(&unparsed, sep);
    } else {
        XML_ELT(msp, ODCI_PROBE_MSG, "%s", "Missing 'probe'!");
        goto ret_ok;
    }
    word = strsep(&unparsed, sep);
    if (strncmp(word, "destination", 11) == 0) {
        word = strsep(&unparsed, sep);
        if (parse_ipaddr(&af, word, 0, &addr, sizeof(addr), NULL, NULL, NULL,
                NULL, 0, NULL, 0) != PARSE_OK) {
            XML_ELT(msp, ODCI_PROBE_MSG, "%s", "Parse IP address ERROR!");
            goto ret_ok;
        }
    } else {
        XML_ELT(msp, ODCI_PROBE_MSG, "%s", "Missing 'destination'!");
        goto ret_ok;
    }
    if (probe_mngr_probe_start(probe_name, addr.s_addr) < 0) {
        XML_ELT(msp, ODCI_PROBE_MSG, "Probe %s is not configured!", probe_name);
    } else {
        XML_ELT(msp, ODCI_PROBE_MSG, "Probe %s started.", probe_name);
    }

ret_ok:
    XML_CLOSE(msp, ODCI_PROBE_REPORT);
    return 0;
}

/**
 * @brief
 * Request to stop a probe. 
 * 
 * @param[in] msp
 *     management socket pointer
 * @param[in] csb
 *     parsed info status (contains subcodes/arguments)
 * @param[in] unparsed
 *     unparsed command string
 * @return
 *      0 if no error; 1 to terminate connection
 */
static int32_t
stop_probe (mgmt_sock_t *msp, parse_status_t* csb UNUSED, char *unparsed)
{
    XML_OPEN(msp, ODCI_PROBE_REPORT);
    if (probe_mngr_probe_stop(unparsed) < 0) {
        XML_ELT(msp, ODCI_PROBE_MSG, "Stop probe %s ERROR!", unparsed);
    } else {
        XML_ELT(msp, ODCI_PROBE_MSG, "Probe %s is stopped.", unparsed);
    }
    XML_CLOSE(msp, ODCI_PROBE_REPORT);
    return 0;
}

/**
 * @brief
 * Clear the probe result. 
 * 
 * @param[in] msp
 *     management socket pointer
 * @param[in] csb
 *     parsed info status (contains subcodes/arguments)
 * @param[in] unparsed
 *     unparsed command string
 * @return
 *      0 if no error; 1 to terminate connection
 */
static int32_t
clear_probe (mgmt_sock_t *msp, parse_status_t* csb UNUSED, char *unparsed)
{
    XML_OPEN(msp, ODCI_PROBE_REPORT);
    if (probe_mngr_probe_clear(unparsed) < 0) {
        XML_ELT(msp, ODCI_PROBE_MSG, "Probe %s is not cleared!", unparsed);
    } else {
        XML_ELT(msp, ODCI_PROBE_MSG, "Probe %s is cleared.", unparsed);
    }
    XML_CLOSE(msp, ODCI_PROBE_REPORT);

    return 0;
}

/*** GLOBAL/EXTERNAL Functions ***/

/*
 * Menu Structure
 *
 * format (from libjuniper's ms_parse.h):
 *   command,
 *   help desc (or NULL),
 *   user data arg block (or 0),
 *   child (next) menu (or NULL),
 *   handler for command(or 0)
 */

/* request sync ipprobe-mt ... commands */
static const parse_menu_t request_sync_probe_menu[] = {
    { "start", NULL, 0, NULL, start_probe },
    { "stop", NULL, 0, NULL, stop_probe },
    { "clear", NULL, 0, NULL, clear_probe },
    { NULL, NULL, 0, NULL, NULL }
};

/* request sync ... commands */
static const parse_menu_t request_sync_menu[] = {
    { "ipprobe-mt", NULL, 0, request_sync_probe_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/* request ... commands */
static const parse_menu_t request_menu[] = {
    { "sync", NULL, 0, request_sync_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/* show sync ... commands */
static const parse_menu_t show_sync_menu[] = {
    { "ipprobe-mt", NULL, 0, NULL, show_probe },
    { NULL, NULL, 0, NULL, NULL }
};

/* show ... commands */
static const parse_menu_t show_menu[] = {
    { "sync", NULL, 0, show_sync_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/* clear sync ... commands */
static const parse_menu_t clear_sync_menu[] = {
    { "ipprobe-mt", NULL, 0, NULL, clear_probe },
    { NULL, NULL, 0, NULL, NULL }
};

/* clear ... commands */
static const parse_menu_t clear_menu[] = {
    { "sync", NULL, 0, clear_sync_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/* main menu of commands */
const parse_menu_t master_menu[] = {
    { "show", NULL, 0, show_menu, NULL }, 
    { "request", NULL, 0, request_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};


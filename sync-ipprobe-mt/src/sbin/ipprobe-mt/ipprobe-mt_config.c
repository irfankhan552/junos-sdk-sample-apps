/*
 * $Id: ipprobe-mt_config.c 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file ipprobe-mt_config.c
 * @brief Load and store the configuration data
 *
 * These functions will parse and load the configuration data.
 *
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
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/pconn.h>
#include <jnx/junos_trace.h>
#include <ddl/dax.h>
#include "ipprobe-mt.h"
#include IPPROBE_MT_OUT_H

/*** Data Structures ***/

static uint16_t probe_mngr_port;   /**< The TCP port for probe manager */
static uint16_t rspd_mngr_port;    /**< The UDP port for responder manager */

/** The list of probe parameters */
static LIST_HEAD(, probe_params_s)  probe_params_list;

/*** STATIC/INTERNAL Functions ***/

/**
 * Invoke functions which depend on configuration.
 */
static void
config_post_proc (void)
{
    probe_mngr_open(probe_mngr_port);
    rspd_mngr_open(rspd_mngr_port);
}

static void
probe_params_clear (void)
{
    probe_params_t *params;

    while ((params = LIST_FIRST(&probe_params_list))) {
        LIST_REMOVE(params, entry);
        free(params);
    }
}


/*** GLOBAL/EXTERNAL Functions ***/

probe_params_t *
probe_params_get (char *name)
{
    probe_params_t *params;

    LIST_FOREACH(params, &probe_params_list, entry) {
        if (strncmp(params->name, name, sizeof(params->name)) == 0) {
            return params;
        }
    }
    return NULL;
}

/**
 * Read daemon configuration from the database.
 *
 * @param[in] check
 *      1 if this function being invoked because of a commit check
 * @return
 *      0 on success, -1 on failure
 *
 * @note Do not use ERRMSG during config check.
 */
int
config_read (int check)
{
    const char *rspd_mngr_config[] = { "sync", "ipprobe-mt",
            "responder-manager", NULL };
    const char *probe_mngr_config[] = { "sync", "ipprobe-mt",
            "probe-manager", NULL };
    const char *probe_params_config[] = { "sync", "ipprobe-mt",
            "probe", NULL };
    ddl_handle_t *top = NULL;
    ddl_handle_t *cop = NULL;
    ddl_handle_t *dop = NULL;
    probe_params_t *params;

    PROBE_TRACE(PROBE_TF_CONFIG, "%s: Loading probe configuration.", __func__);

    probe_mngr_port = PROBE_MNGR_PORT_DEFAULT;
    rspd_mngr_port = RSPD_MNGR_PORT_DEFAULT;

    /* Read probe manager port. */
    if (dax_get_object_by_path(NULL, probe_mngr_config, &top, TRUE)) {
        dax_get_ushort_by_aid(top, PROBE_MNGR_PORT, &probe_mngr_port);
        PROBE_TRACE(PROBE_TF_CONFIG, "%s: Probe manager port %d.",
                __func__, probe_mngr_port);
        dax_release_object(&top);
    }

    /* Read responder manager port. */
    if (dax_get_object_by_path(NULL, rspd_mngr_config, &top, TRUE)) {
        dax_get_ushort_by_aid(top, RSPD_MNGR_PORT, &rspd_mngr_port);
        PROBE_TRACE(PROBE_TF_CONFIG, "%s: Responder manager port %d.",
                __func__, rspd_mngr_port);
        dax_release_object(&top);
    }

    if (dax_get_object_by_path(NULL, probe_params_config, &cop, FALSE)) {
        if (dax_is_changed(cop)) {
            probe_params_clear();
            while (dax_visit_container(cop, &dop)) {
                params = calloc(1, sizeof(*params));
                if (!params) {
                    dax_error(dop, "Allocate memory ERROR!");
                    dax_release_object(&dop);
                    break;
                }
                if (!dax_get_stringr_by_aid(dop, PROBE_NAME,
                        params->name, sizeof(params->name))) {

                    /* Probe name is mandatory. */
                    dax_error(dop, "Read probe name ERROR!");
                    dax_release_object(&dop);
                    break;
                }
                PROBE_TRACE(PROBE_TF_CONFIG, "%s: Probe name %s.",
                        __func__, params->name);
                dax_get_ubyte_by_aid(dop, PROBE_PROTOCOL, &params->proto);
                PROBE_TRACE(PROBE_TF_CONFIG, "%s: Probe protocol %d.",
                        __func__, params->proto);
                dax_get_ubyte_by_aid(dop, PROBE_TOS, &params->tos);
                PROBE_TRACE(PROBE_TF_CONFIG, "%s: Probe TOS %d.",
                        __func__, params->tos);
                dax_get_ushort_by_aid(dop, PROBE_SRC_PORT, &params->src_port);
                PROBE_TRACE(PROBE_TF_CONFIG, "%s: Probe source port %d.",
                        __func__, params->src_port);
                dax_get_ushort_by_aid(dop, PROBE_DST_PORT, &params->dst_port);
                PROBE_TRACE(PROBE_TF_CONFIG, "%s: Probe destination port %d.",
                        __func__, params->dst_port);
                dax_get_ushort_by_aid(dop, PACKET_SIZE, &params->pkt_size);
                PROBE_TRACE(PROBE_TF_CONFIG, "%s: Probe packet size %d.",
                        __func__, params->pkt_size);
                dax_get_ushort_by_aid(dop, PACKET_COUNT, &params->pkt_count);
                PROBE_TRACE(PROBE_TF_CONFIG, "%s: Probe packet count %d.",
                        __func__, params->pkt_count);
                dax_get_ushort_by_aid(dop, PACKET_INTERVAL,
                        &params->pkt_interval);
                PROBE_TRACE(PROBE_TF_CONFIG, "%s: Probe packet interval %d.",
                        __func__, params->pkt_interval);

                LIST_INSERT_HEAD(&probe_params_list, params, entry);
            }
        }
    } else {
        PROBE_TRACE(PROBE_TF_CONFIG, "%s: No probe parameters!", __func__);
        probe_params_clear();
    }

    if (check == 0) {
        config_post_proc();
    }

    return 0;
}


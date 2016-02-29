/*
 * $Id$
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file route-manager_kcom.c
 * @brief Relating to KCOM API
 * 
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <isc/eventlib.h>
#include <sys/queue.h>
#include <jnx/ssd_ipc_msg.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>
#include "route-manager.h"

#include OUT_H

/**
 * @brief
 * Get IFL index by IFL name
 *
 * @param[in] name
 *      IFL name
 *
 * @return
 *      IFL index on success, -1 on failure
 */
int
kcom_ifl_get_idx_by_name (char *name)
{
    int rc;
    char *unit_str;
    int unit;
    kcom_ifl_t ifl;

    unit_str = strdup(name);
    name = strsep(&unit_str, ".");
    if (unit_str) {
        unit = (int)strtol(unit_str, NULL, 10);
    } else {
        unit = 0;
    }
    if (junos_kcom_ifl_get_by_name(name, unit, &ifl) < 0) {
        rc = -1;
    } else {
        rc = ifl_idx_t_getval(ifl.ifl_index);
    }
    free(name);
    return rc;
}

/**
 * @brief
 * Save SSD client ID as GENCFG blob.
 *
 * @param[in] id
 *      SSD client ID
 *
 * @return
 *      0 on success, -1 on failure
 */
int
kcom_client_id_save (int id)
{
    junos_kcom_gencfg_t gencfg;

    JUNOS_KCOM_GENCFG_INIT(&gencfg);
    gencfg.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_ADD;
    gencfg.blob_id = RM_BLOB_ID_CLIENT_ID;
    gencfg.dest = JUNOS_KCOM_GENCFG_DEST_RE_LOCAL_ONLY;
    gencfg.key.size = sizeof(gencfg.blob_id);
    gencfg.key.data_p = &gencfg.blob_id;
    gencfg.blob.size = sizeof(id);
    gencfg.blob.data_p = &id;
    if (junos_kcom_gencfg(&gencfg) < 0) {
        RM_LOG(LOG_ERR, "%s: Save GENCFG blob ERROR!", __func__);
        return -1;
    } else {
        RM_TRACE(TF_KCOM, "%s: Saved ID %d", __func__, id);
        return 0;
    }
}

/**
 * @brief
 * Restore SSD client ID from GENCFG blob.
 *
 * @return
 *      Client ID on success, -1 on failure
 */
int
kcom_client_id_restore (void)
{
    junos_kcom_gencfg_t gencfg;

    JUNOS_KCOM_GENCFG_INIT(&gencfg);
    gencfg.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_GET;
    gencfg.blob_id = RM_BLOB_ID_CLIENT_ID;
    gencfg.key.size = sizeof(gencfg.blob_id);
    gencfg.key.data_p = &gencfg.blob_id;
    if (junos_kcom_gencfg(&gencfg) < 0) {
        RM_LOG(LOG_ERR, "%s: Get GENCFG blob ERROR!", __func__);
        return -1;
    } else {
        if (!gencfg.blob.size) {
            RM_TRACE(TF_KCOM, "%s: Didn't get the blob", __func__);
            return -1;
        } else {
            RM_TRACE(TF_KCOM, "%s: Got ID %d", __func__,
                    *((int *)gencfg.blob.data_p));
            return *((int *)gencfg.blob.data_p);
        }
    }
}

/**
 * @brief
 *  GENCFG notification handler (does nothing)
 *
 * @param[in] gencfg
 *      GENCFG message
 *
 * @return
 *      KCOM_OK (0) always
 */
static int
kcom_gencfg_async_hdlr (junos_kcom_gencfg_t *gencfg)
{

    gencfg->opcode = JUNOS_KCOM_GENCFG_OPCODE_GET_FREE;

    /* Set get_p to NULL. Otherwise junos_kcom_gencfg() will
     * free the data to which get_p points to. The data will be
     * freed later (in kcom_msg_handler())
     */
    gencfg->get_p = NULL;
    junos_kcom_gencfg(gencfg);
    junos_kcom_msg_free(gencfg);
    return 0;
}

/**
 * @brief
 * Close KCOM.
 *
 */
void
kcom_close (void)
{
    junos_kcom_shutdown();
}

/**
 * @brief
 * Initialize KCOM.
 *
 * @param[in] ev_ctx
 *      Event context
 *
 * @return
 *      0 on success, -1 on failure
 */
int
kcom_init (evContext ev_ctx)
{
    junos_kcom_gencfg_t gencfg_req;

    if (junos_kcom_init(255, ev_ctx) != KCOM_OK) {
        RM_LOG(LOG_ERR, "%s: KCOM init ERROR!", __func__);
        return -1;
    }

    JUNOS_KCOM_GENCFG_INIT(&gencfg_req);
    gencfg_req.opcode = JUNOS_KCOM_GENCFG_OPCODE_INIT;
    gencfg_req.user_handler = kcom_gencfg_async_hdlr;
    if (junos_kcom_gencfg(&gencfg_req) != KCOM_OK) {
        RM_LOG(LOG_ERR, "%s: KCOM GENCFG init ERROR!", __func__);
        return -1;
    }
    return 0;
}


/*
 * $Id: jnx-routeserviced_gencfg.c 346460 2009-11-14 05:06:47Z ssiano $
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


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>

#include JNX_ROUTESERVICED_OUT_H

#include "jnx-routeserviced_gencfg.h"

/**
 * @brief
 *     Fuction handler to handle async notifications
 *
 * @param[in] jk_gencfg
 *      Gencfg data structure which needs to be freed
 * 
 * @return
 *      KCOM_OK
 */
static int
jnx_routeserviced_gencfg_async (junos_kcom_gencfg_t * jk_gencfg)
{
    jk_gencfg->opcode = JUNOS_KCOM_GENCFG_OPCODE_GET_FREE;
    jk_gencfg->get_p = NULL;
    junos_kcom_gencfg(jk_gencfg);
    
    junos_kcom_msg_free(jk_gencfg);
    
    return KCOM_OK;
}

/**
 * @brief
 *     Initialize the gencfg subsystem
 *
 * This API performs the initialization functions required in order
 * to use the JUNOS gencfg subsystem
 */
int
jnx_routeserviced_gencfg_init (void)
{
    int error;
    junos_kcom_gencfg_t gencfg_obj;

    bzero(&gencfg_obj, sizeof(junos_kcom_gencfg_t));
    gencfg_obj.opcode = JUNOS_KCOM_GENCFG_OPCODE_INIT;
    gencfg_obj.user_info_p = NULL;
    gencfg_obj.user_handler = &jnx_routeserviced_gencfg_async; 

    error = junos_kcom_gencfg((junos_kcom_gencfg_t *) &gencfg_obj);

    if (error) {
        return error;
    }

    return 0;
}

/**
 * @brief
 *     Store client-id in the gencfg store
 * 
 * @param[in]
 *     client-id provided by SSD
 */
void
jnx_routeserviced_gencfg_store_client_id (int id)
{
    int error;

    junos_kcom_gencfg_t gencfg_obj;
    u_int32_t client_id_blob_key = JNX_ROUTESERVICED_CLIENT_ID_BLOB_KEY;

    bzero(&gencfg_obj, sizeof(junos_kcom_gencfg_t));

    gencfg_obj.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_ADD;
    gencfg_obj.blob_id  = JNX_ROUTESERVICED_CLIENT_ID_BLOB_ID;

    gencfg_obj.key.size = sizeof(u_int32_t);
    gencfg_obj.key.data_p = (void *) &client_id_blob_key;

    gencfg_obj.blob.size = sizeof(id);
    gencfg_obj.blob.data_p = (void *) &id;

    error = junos_kcom_gencfg((junos_kcom_gencfg_t *) &gencfg_obj);
        
    if (error) {
        junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                    "gencfg unable to store client-id with cause %d", 
                    __func__, errno);
    }
}

/**
 * @brief
 *     Fetch client-id from the gencfg store
 * 
 * @param[out] id
 *     Pointer to where client-id is going to be stored
 */
int
jnx_routeserviced_gencfg_get_client_id (int *id)
{
    int error;

    if (!id) {
        return -1;
    }

    junos_kcom_gencfg_t gencfg_obj;
    u_int32_t client_id_blob_key = JNX_ROUTESERVICED_CLIENT_ID_BLOB_KEY;

    bzero(&gencfg_obj, sizeof(junos_kcom_gencfg_t));

    gencfg_obj.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_GET;
    gencfg_obj.blob_id = JNX_ROUTESERVICED_CLIENT_ID_BLOB_ID;

    gencfg_obj.key.size = sizeof(u_int32_t);
    gencfg_obj.key.data_p = (void *) &client_id_blob_key;

    error = junos_kcom_gencfg((junos_kcom_gencfg_t *) &gencfg_obj);

    if (error) {
        junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                    "gencfg unable to get client-id with cause %d",
                    __func__, errno);
        *id = 0;
        return -1;
    }

    /* 
     * Get the client-id from the blob
     */
    *id = *((u_int32_t *) (gencfg_obj.blob.data_p));

    return 0;
}

/**
 * @brief
 *     Delete client-id from GENCFG store
 */
void 
jnx_routeserviced_gencfg_delete_client_id (int id __unused)
{
    int error;

    junos_kcom_gencfg_t gencfg_obj;
    u_int32_t client_id_blob_key = JNX_ROUTESERVICED_CLIENT_ID_BLOB_KEY;

    bzero(&gencfg_obj, sizeof(junos_kcom_gencfg_t));

    gencfg_obj.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_DEL;
    gencfg_obj.blob_id = JNX_ROUTESERVICED_CLIENT_ID_BLOB_ID;

    gencfg_obj.key.size = sizeof(u_int32_t);
    gencfg_obj.key.data_p = (void *) &client_id_blob_key;

    error = junos_kcom_gencfg((junos_kcom_gencfg_t *) &gencfg_obj);

    if (error) {
        junos_trace(JNX_ROUTESERVICED_TRACEFLAG_ROUTE_EVENTS, "%s: "
                    "gencfg unable to delete client-id with cause %d",
                    __func__, errno);
    }
}


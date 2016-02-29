/*
 * $Id: equilibrium2-mgmt_kcom.c 431556 2011-03-20 10:23:34Z sunilbasker $
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
 * @file equilibrium2-mgmt_kcom.c
 * @brief Init kcom and register handler
 * 
 * 
 * Initialize KCOM and register handlers
 */
#include <sync/equilibrium2.h>
#include "equilibrium2-mgmt.h"

#include <stdlib.h>
#include <string.h>
#include <jnx/aux_types.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>

#include EQUILIBRIUM2_OUT_H

/*** Constants ***/

#define MSP_IFD_NAME_PATTERN "ms-"  /**< prefix of MS PIC interface names */

/*** Data Structures ***/

/*** STATIC/INTERNAL Functions ***/

/**
 * @brief
 * Gets called for all ifds.
 * Watch for MS PIC which we are connected to going down 
 *
 * @param[in] msg
 *      The ifd we want to know about
 *
 * @param[in] user_info
 *      User info that was registered to be passed to this callback
 *
 * @return
 *      0 on success, -1 on failure
 */
static int
ifd_async_hdlr (kcom_ifdev_t *msg, void *user_info UNUSED)
{
    char *name = msg->ifdev_name;

    if ((msg->ifdev_op == KCOM_DELETE) && strstr(name, MSP_IFD_NAME_PATTERN)) {

        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: KCOM_DELETE %s.",
                __func__, name);
    }
    if (junos_kcom_ifd_down(msg) && strstr(name, MSP_IFD_NAME_PATTERN)) {

        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: ifd %s is down",
                __func__, name);

        /* An MS PIC is going down and may be one we're connected to */
    }

    junos_kcom_msg_free(msg);
    return 0;
}


/**
 * @brief
 * Notification handler for SSRB events
 *
 * @param[in] kpb_msg
 *      KCOM Public Blob Message, contains the SSRB
 * 
 * @param[in] arg
 *      Cookie passed in registration
 * 
 * @return
 *      KCOM_OK (0) on success, -1 on failure
 */
static int
ssrb_async_hdlr (junos_kcom_pub_blob_msg_t *kpb_msg, void *arg UNUSED)
{
    junos_kcom_pub_ssrb_t *ssrb;

    if (kpb_msg->key.blob_id != JNX_PUB_EXT_SVC_SSR_BLOB) {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Public blob ID %d, op %d ERROR!",
                __func__, kpb_msg->key.blob_id, kpb_msg->op);
        free(kpb_msg);
        return 0;
    }

    ssrb = &kpb_msg->pub_blob.blob.ssrb;
    switch (kpb_msg->op) {
    case JUNOS_KCOM_PUB_BLOB_MSG_ADD:
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: SSRB <%s, %d> was added.",
                __func__, ssrb->svc_set_name, ssrb->svc_set_id);
        config_ssrb_op(ssrb, CONFIG_SSRB_ADD);
        break;
    case JUNOS_KCOM_PUB_BLOB_MSG_DEL:
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: SSRB %d was deleted.",
                __func__, kpb_msg->key.blob_key.pub_ssrb_key.svc_set_id);
        config_ssrb_op(ssrb, CONFIG_SSRB_DEL);
        break;
    case JUNOS_KCOM_PUB_BLOB_MSG_CHANGE:
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: SSRB <%s, %d> was changed.",
                __func__, ssrb->svc_set_name, ssrb->svc_set_id);
        config_ssrb_op(ssrb, CONFIG_SSRB_CHANGE);
        break;
    default:
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: Unknown SSRB event <%s, %d>.",
                __func__, ssrb->svc_set_name, ssrb->svc_set_id);
    }

    free(kpb_msg);
    return 0;
}

/**
 * @brief
 * GENCFG notification handler (does nothing)
 *
 * @param[in] gencfg
 *      GENCFG message
 * 
 * @return
 *      KCOM_OK (0) always
 */
static int 
gencfg_async_hdlr (junos_kcom_gencfg_t *gencfg)
{
    EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s", __func__);

    gencfg->opcode = JUNOS_KCOM_GENCFG_OPCODE_GET_FREE;
    /*
     * Set get_p to NULL. Otherwise junos_kcom_gencfg() will
     * free the data to which get_p points to. The data will be
     * freed later (in kcom_msg_handler())
     */
    gencfg->get_p = NULL;
    junos_kcom_gencfg(gencfg);
    junos_kcom_msg_free(gencfg);
    return 0;
}


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * @brief
 * Add a configuration blob.
 * 
 * @param[in] key
 *      Pointer to the blob key
 * 
 * @param[in] blob
 *      Pointer to the blob
 * 
 * @return
 *      0 on success, -1 on failure
 */
int
kcom_add_config_blob (config_blob_key_t *key, void *blob)
{
    uint32_t blob_size;
    junos_kcom_gencfg_t gencfg;
    junos_kcom_gencfg_peer_info_t *peer_info;
    junos_kcom_gencfg_peer_info_t *peer;
    svc_if_t *svc_if;
    blob_svr_group_set_t *blob_gs;
    blob_svc_set_t *blob_ss;
    uint16_t cksum = 0;
    int err;

    if (svc_if_count == 0) {
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: No service interface configured!",
                __func__);
        return 0;
    }
    JUNOS_KCOM_GENCFG_INIT(&gencfg);
    gencfg.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_ADD;
    gencfg.blob_id = eq2_provider_id;
    gencfg.cfg_index = key->key_plugin_id;
    gencfg.key.size = sizeof(config_blob_key_t);
    gencfg.key.data_p = key;
    gencfg.blob.data_p = blob;
    gencfg.dest = JUNOS_KCOM_GENCFG_DEST_RE_AND_PFE;

    if (key->key_tag == CONFIG_BLOB_SVR_GROUP) {
        peer_info = calloc(1, svc_if_count *
                sizeof(junos_kcom_gencfg_peer_info_t));
        INSIST_ERR(peer_info != NULL);
        blob_gs = blob;
        cksum = blob_gs->gs_cksum;
        blob_size = ntohs(blob_gs->gs_size);
        peer = peer_info;
        LIST_FOREACH(svc_if, &svc_if_head, entry) {
            peer->peer_id.peer_name = svc_if->if_name;
            peer++;
        }
        gencfg.peer_info = peer_info;
        gencfg.peer_count = svc_if_count;
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: peer %d, %s",
                __func__, svc_if_count, peer_info->peer_id.peer_name);
    } else if (key->key_tag == CONFIG_BLOB_SVC_SET) {
        peer_info = calloc(1, sizeof(junos_kcom_gencfg_peer_info_t));
        INSIST_ERR(peer_info != NULL);
        blob_ss = blob;
        cksum = blob_ss->ss_cksum;
        blob_size = ntohs(blob_ss->ss_size);
        peer_info->peer_id.peer_name = blob_ss->ss_if_name;
        gencfg.peer_info = peer_info;
        gencfg.peer_count = 1;
    } else {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Unknown key tag!", __func__);
        return -1;
    }
    gencfg.blob.size = blob_size;

    EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: blob ID %d, size %d, key %s, "
            "tag %d, pid %d, peer %d, cksum %d", __func__, gencfg.blob_id,
            blob_size, key->key_name, key->key_tag, key->key_plugin_id,
            gencfg.peer_count, cksum);

    err = junos_kcom_gencfg(&gencfg);
    if (err != KCOM_OK) {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Add blob ERROR %d!", __func__, err);
        free(peer_info);
        return -1;
    }
    free(peer_info);
    return 0;
}

/**
 * @brief
 * Delete a configuration blob.
 * 
 * @param[in] key
 *      Pointer to the blob key
 * 
 * @return
 *      0 on success, -1 on failure
 */
int
kcom_del_config_blob (config_blob_key_t *key)
{
    junos_kcom_gencfg_t gencfg;
    int err;

    EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: blob %s, %d, %d.", __func__,
            key->key_name, key->key_tag, key->key_plugin_id);

    JUNOS_KCOM_GENCFG_INIT(&gencfg);
    gencfg.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_DEL;
    gencfg.dest = JUNOS_KCOM_GENCFG_DEST_RE_AND_PFE;
    gencfg.blob_id = eq2_provider_id;
    gencfg.key.size = sizeof(config_blob_key_t);
    gencfg.key.data_p = key;

    err = junos_kcom_gencfg(&gencfg);
    if (err != KCOM_OK) {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Delete blob ERROR %d!", __func__, err);
        return -1;
    }
    return 0;
}

/**
 * @brief
 * Get all configuration blobs.
 * 
 * @return
 *      0 on success, -1 on failure
 */
int
kcom_get_config_blob (void)
{
    junos_kcom_gencfg_t gencfg;
    int err = 0;
    config_blob_key_t key;

    EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s", __func__);

    JUNOS_KCOM_GENCFG_INIT(&gencfg);
    gencfg.blob_id = eq2_provider_id;
    gencfg.dest = JUNOS_KCOM_GENCFG_DEST_RE_AND_PFE;

    /* Clear key pointer and size to get the first blob. */
    gencfg.key.data_p = NULL;
    gencfg.key.size = 0;
    key.key_name[0] = '\0';
    key.key_tag = 0;
    key.key_plugin_id = 0;
    while (1) {
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: Req blob %s, tag %d, pid %d, %d.",
                __func__, key.key_name, key.key_tag, key.key_plugin_id,
                gencfg.key.size);

        gencfg.opcode = JUNOS_KCOM_GENCFG_OPCODE_BLOB_GETNEXT;
        err = junos_kcom_gencfg(&gencfg);

        /* ENOENT indicates no more blob when doing get-next. */
        if (err == ENOENT) {
            EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: No more blob.", __func__);
            break;
        } else if (err != KCOM_OK) {
            EQ2_LOG(TRACE_LOG_ERR, "%s: Get next blob ERROR (%d)!",
                    __func__, err);
            return -1;
        }

        /* Save the blob key for getting next blob. */
        memcpy(&key, gencfg.key.data_p, sizeof(key));
        EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: Got blob %s, tag %d, pid %d, "
                "key size %d.",
                __func__, key.key_name, key.key_tag, key.key_plugin_id,
                gencfg.key.size);

        switch (key.key_tag) {
        case CONFIG_BLOB_SVC_SET:
            config_svc_set_blob_proc(&key, gencfg.blob.data_p);
            break;
        case CONFIG_BLOB_SVR_GROUP:
            config_svr_group_blob_proc(&key, gencfg.blob.data_p);
            break;
        default:
            EQ2_LOG(TRACE_LOG_ERR, "%s: Unkown blob!", __func__);
        }

        /* Free blob memory assigned by API. */
        gencfg.opcode = JUNOS_KCOM_GENCFG_OPCODE_GET_FREE;
        err = junos_kcom_gencfg(&gencfg);
        if (err != KCOM_OK) {
            EQ2_LOG(TRACE_LOG_ERR, "%s: Free blob ERROR!", __func__);
            return -1;
        }

        /* Set parameters to get the next blob. */
        gencfg.key.size = sizeof(config_blob_key_t);
        gencfg.key.data_p = &key;
    }

    return 0;
}

/**
 * @brief
 * Init KCOM library and register handlers for asynchronous KCOM messages
 * 
 * @param[in] ctx
 *      Event context 
 * 
 * @return
 *      KCOM_OK (0) on success, -1 on failure
 */
int
kcom_init (evContext ctx)
{
    int status;
    junos_kcom_gencfg_t kcom_req;
    junos_kcom_pub_blob_req_t kpb_req;
    kcom_init_params_t  params = {KCOM_NEED_RESYNC};

    EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s: Initialize KCOM.", __func__);

    status = junos_kcom_init_extensive(eq2_origin_id, ctx, &params);
    INSIST(status == KCOM_OK);

    /* Register for IFD notification (listen for ms interfaces going down). */
    status = junos_kcom_register_ifd_handler(NULL, ifd_async_hdlr);
    if (status != KCOM_OK) {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Register ifd handler ERROR!",
                __func__);
    }

    /* Register gencfg handler. */
    JUNOS_KCOM_GENCFG_INIT(&kcom_req);
    kcom_req.opcode = JUNOS_KCOM_GENCFG_OPCODE_INIT;
    kcom_req.user_handler = gencfg_async_hdlr;

    status = junos_kcom_gencfg(&kcom_req);
    if (status != KCOM_OK) {
    	EQ2_LOG(TRACE_LOG_ERR, "%s: Register gencfg handler ERROR!",
                __func__);
    	return status;
    }

    /* Register SSRB handler. */
    bzero(&kpb_req, sizeof(junos_kcom_pub_blob_req_t));
    kpb_req.opcode = JUNOS_KCOM_PUB_BLOB_REG;
    kpb_req.blob_id = JNX_PUB_EXT_SVC_SSR_BLOB; 
    kpb_req.msg_user_info = NULL;
    kpb_req.msg_user_handler = ssrb_async_hdlr;

    status = junos_kcom_pub_blob_req(&kpb_req);
    if (status != KCOM_OK) {
        EQ2_LOG(TRACE_LOG_ERR, "%s: Register SSRB handler ERROR!",
                __func__);
        return status;
    }

    return status;
}

/**
 * @brief
 * Shutdown KCOM library and register handlers for asynchronous KCOM messages
 */
void
kcom_close (void)
{
    junos_kcom_pub_blob_req_t kpb_req;

    EQ2_TRACE(EQ2_TRACEFLAG_KCOM, "%s", __func__);

    bzero(&kpb_req, sizeof(junos_kcom_pub_blob_req_t));
    kpb_req.opcode = JUNOS_KCOM_PUB_BLOB_DEREG;
    kpb_req.blob_id = JNX_PUB_EXT_SVC_SSR_BLOB; 
    junos_kcom_pub_blob_req(&kpb_req);

    junos_kcom_shutdown();
}


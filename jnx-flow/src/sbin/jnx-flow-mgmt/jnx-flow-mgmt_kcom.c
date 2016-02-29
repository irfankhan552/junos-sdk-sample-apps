/*
 * $Id: jnx-flow-mgmt_kcom.c 431556 2011-03-20 10:23:34Z sunilbasker $
 *
 * jnx-flow-mgmt_kcom.c - kcom interface ifd event handler routines
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
 * @file : jnx-flow-mgmt_kcom.c
 * @brief
 * This file contains the kernel event handler routines
 *
 */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <ctype.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <ddl/dax.h>
#include <jnx/trace.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include <jnx/pconn.h>
#include <jnx/provider_info.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_kcom_pub_blob.h>
#include <jnx/parse_ip.h>
#include <jnx/junos_trace.h>
#include <jnx/ipc_gencfg_pub.h>
#include JNX_FLOW_MGMT_OUT_H

#include <jnx/jnx-flow.h>
#include <jnx/jnx-flow_msg.h>

#include "jnx-flow-mgmt.h"
#include "jnx-flow-mgmt_config.h"
#include "jnx-flow-mgmt_kcom.h"
#include "jnx-flow-mgmt_ssrb.h"

#define JNX_FLOW_MAX_SVC_SETID 0xFFFF

static int
jnx_flow_gencfg_async_handler(junos_kcom_gencfg_t* gencfg, void* arg __unused);

static jnx_flow_ssrb_node_t*   svc_set_id_node_map[JNX_FLOW_MAX_SVC_SETID];

static int jnx_flow_mgmt_pub_ssrb_async_handler(junos_kcom_pub_blob_msg_t* msg, void* arg);

static int 
jnx_flow_mgmt_ifd_event_handler(kcom_ifdev_t *ifdm, void * arg __unused);
static char msp_prefix[JNX_FLOW_STR_SIZE];

static uint32_t add_msg_count;
static uint32_t del_msg_count;
static uint32_t svc_set_count;
static uint32_t gencfg_msg_count;;

/**
 * Function : jnx_flow_mgmt_kcom_init
 * Puspose  : Init KCOM library and register for asynchronous event/messages
 * Return   : 0: success , -1: error
 */
int
jnx_flow_mgmt_kcom_init(evContext ctxt)
{
    int                         error;
    junos_kcom_gencfg_t         kcom_req;
    junos_kcom_pub_blob_req_t   kpb_req;
    provider_origin_id_t        origin_id;
    kcom_init_params_t          params = {KCOM_NEED_RESYNC};

    error = provider_info_get_origin_id(&origin_id);
    
    if (error) {
        jnx_flow_log(JNX_FLOW_KCOM, LOG_ERR, 
                "%s: Retrieving origin ID failed: %m", __func__);
        return error;
    }
    
    error = junos_kcom_init_extensive(origin_id, ctxt, &params);

    INSIST (error == KCOM_OK);

    /* register for gencfg events */
    memset(&kcom_req, 0, sizeof(junos_kcom_gencfg_t));
    memset(&kpb_req, 0, sizeof(junos_kcom_pub_blob_req_t));

    kcom_req.opcode       = JUNOS_KCOM_GENCFG_OPCODE_INIT;
    kcom_req.user_handler = jnx_flow_gencfg_async_handler;

    error = junos_kcom_gencfg(&kcom_req);

    INSIST (error == KCOM_OK);

    /* Register for the public blobs */
    kpb_req.opcode           = JUNOS_KCOM_PUB_BLOB_REG;
    kpb_req.blob_id          = JNX_PUB_EXT_SVC_SSR_BLOB; 
    kpb_req.msg_user_info    = NULL;
    kpb_req.msg_user_handler = jnx_flow_mgmt_pub_ssrb_async_handler;

    error = junos_kcom_pub_blob_req(&kpb_req);
        
    INSIST (error == KCOM_OK);

    strncpy(msp_prefix, MSP_PREFIX, sizeof(msp_prefix));

    if (junos_kcom_register_ifd_handler(NULL, jnx_flow_mgmt_ifd_event_handler)
        != KCOM_OK) { 
        jnx_flow_log(JNX_FLOW_KCOM, LOG_EMERG, "Kcom IFD Event Handler Setup failed");
        return EFAIL;
    }

    error = junos_kcom_resync(NULL, NULL);

    INSIST(error == KCOM_OK);

    return 0;
}

/**
 * Function : jnx_flow_mgmt_kcom_cleanup_
 * Puspose  : clean up kcom register events, states
 */

int
jnx_flow_mgmt_kcom_cleanup(evContext ctxt __unused)
{
    junos_kcom_pub_blob_req_t   kpb_req;

    memset(&kpb_req, 0, sizeof(junos_kcom_pub_blob_req_t));

    kpb_req.opcode  = JUNOS_KCOM_PUB_BLOB_DEREG;
    kpb_req.blob_id = JNX_PUB_EXT_SVC_SSR_BLOB; 

    junos_kcom_pub_blob_req(&kpb_req);

    junos_kcom_shutdown();
    return 0;
}

/**
 * Function : jnx_flow_mgmt_pub_ssrb_async_handler
 * Puspose  : receives the async message for ssrb blob events
 */

int
jnx_flow_mgmt_pub_ssrb_async_handler(junos_kcom_pub_blob_msg_t* kpb_msg, 
                                     void* info __unused)
{
    uint16_t               blob_id = kpb_msg->key.blob_id;
    uint16_t               svc_set_id = 0; 
    junos_kcom_pub_ssrb_t*  pub_ssrb;
    jnx_flow_ssrb_node_t*   ssrb = NULL;
    jnx_flow_svc_set_node_t * svc_set_node;

    if (blob_id != JNX_PUB_EXT_SVC_SSR_BLOB) {
        jnx_flow_log(JNX_FLOW_KCOM, LOG_INFO,"unknown ssrb event");

        free(kpb_msg);
        return -1;
    }

    pub_ssrb = &kpb_msg->pub_blob.blob.ssrb;

    switch (kpb_msg->op) {

        case JUNOS_KCOM_PUB_BLOB_MSG_ADD:

            add_msg_count++;

            jnx_flow_log(JNX_FLOW_KCOM, LOG_INFO,"public ssrb add event <%s, %d>",
                         pub_ssrb->svc_set_name, pub_ssrb->svc_set_id);
            /* Allocate an SSRB and add it to the tree */
            if((ssrb = jnx_flow_mgmt_alloc_ssrb_node()) == NULL) {

                jnx_flow_log(JNX_FLOW_KCOM, LOG_INFO,"alloc failed");
                break;
            }

            memcpy (ssrb->svc_set_name, pub_ssrb->svc_set_name, 
                    strlen(pub_ssrb->svc_set_name));

            ssrb->svc_set_id = pub_ssrb->svc_set_id;
            ssrb->in_nh_idx  = pub_ssrb->in_nh_idx;
            ssrb->out_nh_idx = pub_ssrb->out_nh_idx;

            patricia_node_init_length (&ssrb->node, 
                                       strlen(ssrb->svc_set_name) +1);

            patricia_add(&jnx_flow_mgmt.ssrb_db, &ssrb->node);

            svc_set_id_node_map[ssrb->svc_set_id] = ssrb;
            svc_set_count++;

            /*
             * Call the function to send the service-set to the data pic.
             */
            if ((svc_set_node = (typeof(svc_set_node))
                 patricia_get(&jnx_flow_mgmt.svc_set_db,
                              strlen(ssrb->svc_set_name) +1,
                              ssrb->svc_set_name)) == NULL) {
                break;
            }

            svc_set_node->svc_set_id = ssrb->svc_set_id;

            jnx_flow_mgmt_send_svc_set_config(NULL, svc_set_node,
                                              JNX_FLOW_MSG_CONFIG_ADD);
            jnx_flow_mgmt_send_svc_set_rule_config(NULL, svc_set_node,
                                                   JNX_FLOW_MSG_CONFIG_ADD);
            break;

        case JUNOS_KCOM_PUB_BLOB_MSG_DEL:

            del_msg_count++;
            svc_set_id = kpb_msg->key.blob_key.pub_ssrb_key.svc_set_id;

            jnx_flow_log(JNX_FLOW_KCOM, LOG_INFO,"public ssrb delete event <%d>",
                         svc_set_id);

            if (svc_set_id_node_map[svc_set_id] == NULL) {
                break;
            }

            ssrb = svc_set_id_node_map[svc_set_id];

            patricia_delete(&jnx_flow_mgmt.ssrb_db, &ssrb->node);


            svc_set_id_node_map[svc_set_id] = NULL;
            svc_set_count--;

            /* 
             * Call the function to remove the Service Set from the
             * jnx-flow-data
             */
            if ((svc_set_node = (typeof(svc_set_node))
                 patricia_get(&jnx_flow_mgmt.svc_set_db,
                              strlen(ssrb->svc_set_name) +1,
                              ssrb->svc_set_name)) == NULL) {
                jnx_flow_mgmt_free_ssrb_node(ssrb);
                break;
            }

            jnx_flow_mgmt_free_ssrb_node(ssrb);

            jnx_flow_mgmt_send_svc_set_rule_config(NULL, svc_set_node,
                                                   JNX_FLOW_MSG_CONFIG_DELETE);

            jnx_flow_mgmt_send_svc_set_config(NULL, svc_set_node,
                                              JNX_FLOW_MSG_CONFIG_DELETE);

            svc_set_node->svc_set_id = JNX_FLOW_SVC_SET_ID_UNASSIGNED;

            break;

        case JUNOS_KCOM_PUB_BLOB_MSG_CHANGE:
            jnx_flow_log(JNX_FLOW_KCOM, LOG_INFO,"public ssrb change event <%s, %d>",
                         pub_ssrb->svc_set_name, pub_ssrb->svc_set_id);

            /* TBD */

            break;
        default:
            jnx_flow_log(JNX_FLOW_KCOM, LOG_INFO,"public ssrb unknown event <%s, %d>",
                         pub_ssrb->svc_set_name, pub_ssrb->svc_set_id);
            break;
    }
    free(kpb_msg);
    return 0;
}

/**
 * Function : jnx_flow_mgmt_ifd_event_handler
 * Puspose  : handles the ifd state messages, deletes data sessions
 * on ifd delete events
 * 
 * Return   : 0: success , -1: error
 */

static int 
jnx_flow_mgmt_ifd_event_handler(kcom_ifdev_t *ifdm, void * arg __unused)
{
    uint8_t ifd_name[JNX_FLOW_STR_SIZE];
    jnx_flow_mgmt_data_session_t * pdata_pic;

    strncpy(ifd_name, ifdm->ifdev_name, sizeof(ifd_name));

    /* check only for the multi-services pic interfaces */
    if (strncmp(ifd_name, msp_prefix, strlen(msp_prefix))) { 
        junos_kcom_msg_free(ifdm);
        return (0);
    }

    jnx_flow_log(JNX_FLOW_KCOM, LOG_INFO, "IFD \"%s\" %s event", ifdm->ifdev_name,
                 (ifdm->ifdev_op == KCOM_DELETE) ? "delete" : 
                 (ifdm->ifdev_op == KCOM_ADD) ?  "add" :
                 (ifdm->ifdev_op == KCOM_CHANGE) ?  "change" : "unknown");

    switch (ifdm->ifdev_op) {
        case KCOM_DELETE:
            if ((pdata_pic = jnx_flow_mgmt_data_sesn_lookup(ifd_name))) {
                jnx_flow_mgmt_destroy_data_conn(pdata_pic);
            }
            break;

        case KCOM_ADD:
            /* the add will happen on receipt of the register messages
             * from the control and data pics
             */
            break;

        case KCOM_CHANGE:
            break;
        case KCOM_GET:
            break;
        default:
            break;
    }

    junos_kcom_msg_free(ifdm);
    return (EOK);
}

/**
 * Function : jnx_flow_mgmt_process_ssrb
 */
static int 
jnx_flow_gencfg_async_handler(junos_kcom_gencfg_t* gencfg, void* arg __unused)
{
    gencfg_msg_count++;
    gencfg->get_p = NULL;
    gencfg->opcode = JUNOS_KCOM_GENCFG_OPCODE_GET_FREE;
    junos_kcom_gencfg(gencfg);
    junos_kcom_msg_free(gencfg);

    return 0;
}

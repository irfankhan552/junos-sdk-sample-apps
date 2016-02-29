/*
 * $Id: equilibrium-mgmt_kcom.c 431556 2011-03-20 10:23:34Z sunilbasker $
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
 * @file equilibrium-mgmt_kcom.c
 * @brief Init kcom and register handler
 * 
 * 
 * Initialize KCOM and register a handlers
 */
#include <sync/common.h>
#include <jnx/junos_kcom_pub_blob.h>
#include <jnx/provider_info.h>
#include "equilibrium-mgmt_kcom.h"
#include "equilibrium-mgmt_config.h"
#include "equilibrium-mgmt_conn.h"
#include "equilibrium-mgmt_logging.h"

#include EQUILIBRIUM_OUT_H

/*** Constants ***/

#define MSP_IFD_NAME_PATTERN "ms-"  ///< common part of MS PIC interface names

/*** Data Structures ***/


/*** STATIC/INTERNAL Functions ***/

/**
 * Gets called for all ifds.
 * Watch for MS PIC which we are connected to going down 
 *
 * @param[in] msg
 *     The ifd we want to know about
 *
 * @param[in] user_info
 *     User info that was registered to be passed to this callback
 *
 * @return 0 upon successful completion, otherwise -1
 */
static int
ifd_down_async_handler(kcom_ifdev_t * msg, void * user_info __unused)
{
    char * name = msg->ifdev_name;

    if((msg->ifdev_op == KCOM_DELETE || junos_kcom_ifd_down(msg)) &&
       strstr(name, MSP_IFD_NAME_PATTERN)) {
        
        junos_trace(EQUILIBRIUM_TRACEFLAG_KCOM,
    		"%s: Handling ifd %s going down", __func__, name);
        
        // An MS PIC is going down and may be one we're connected to
        mspic_offline(name);
    }
    
    junos_kcom_msg_free(msg);

    return 0;
}


/**
 * Notification handler for SSRB events
 *
 * @param[in] kpb_msg
 * 		KCOM Public Blob Message, contains the SSRB
 * 
 * @param[in] arg
 * 		cookie passed in registration
 * 
 * @return
 *      KCOM_OK (0) on success, -1 on failure
 */
static int
ssrb_async_handler(junos_kcom_pub_blob_msg_t * kpb_msg, void * arg __unused)
{
    junos_kcom_pub_ssrb_t * ssrb;

    if (kpb_msg->key.blob_id != JNX_PUB_EXT_SVC_SSR_BLOB) {
        LOG(TRACE_LOG_ERR, "%s: Public blob, but not an SSRB <%d>",
            __func__, kpb_msg->pub_blob.blob_id);
        free(kpb_msg);
        return 0;
    }
    
    ssrb = &kpb_msg->pub_blob.blob.ssrb;
    
    switch (kpb_msg->op) {

        case JUNOS_KCOM_PUB_BLOB_MSG_ADD:
        	
            LOG(TRACE_LOG_INFO, "%s: public ssrb add <%s,%d>",
                    __func__, ssrb->svc_set_name, ssrb->svc_set_id);

            if(add_ssrb(ssrb) != SUCCESS) {
                LOG(TRACE_LOG_INFO, 
                    "%s: ssrb was not added to config (already exists) <%s,%d>",
                    __func__, ssrb->svc_set_name, ssrb->svc_set_id);
            }
            
            break;

        case JUNOS_KCOM_PUB_BLOB_MSG_DEL:

            LOG(TRACE_LOG_INFO, "%s: public ssrb delete <%d>", __func__,
                kpb_msg->key.blob_key.pub_ssrb_key.svc_set_id);

            delete_ssrb_by_id(kpb_msg->key.blob_key.pub_ssrb_key.svc_set_id);

            break;

        case JUNOS_KCOM_PUB_BLOB_MSG_CHANGE:
            
            LOG(TRACE_LOG_INFO, "%s: public ssrb change "
                "<%s,%d>", __func__, ssrb->svc_set_name, ssrb->svc_set_id);

              
             // We don't need to worry about receiving these, since we do not
             // use the next-hop-service style of service sets.
             // This event would occur upon a next-hop index changing. 
             

            break;
            
        default:
            LOG(TRACE_LOG_ERR, "%s: public ssrb unknown event <%s,%d>", __func__,
        		ssrb->svc_set_name, ssrb->svc_set_id);
            
            break;
    }
    
    free(kpb_msg);
    return 0;
}

/**
 * GENCFG notification handler (does nothing)
 *
 * @param[in] gencfg
 * 		GENCFG message
 * 
 * @param[in] arg
 * 		cookie passed in registration
 * 
 * @return
 *      KCOM_OK (0) on success
 */
static int 
gencfg_async_handler(junos_kcom_gencfg_t * gencfg, void * arg __unused)
{
    junos_kcom_msg_free(gencfg);
    return 0;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init KCOM library and register handlers for asynchronous KCOM messages
 * 
 * @param[in] ctx
 *     Newly created event context 
 * 
 * @return
 *      KCOM_OK (0) on success, or -1 on error.
 */
int
kcom_init(evContext ctx)
{
    int status;
    provider_origin_id_t origin_id;
    junos_kcom_gencfg_t         kcom_req;
    junos_kcom_pub_blob_req_t   kpb_req;
    kcom_init_params_t          params = {KCOM_NEED_RESYNC};
    
    status = provider_info_get_origin_id(&origin_id);
    
    if (status) {
        LOG(TRACE_LOG_ERR, "%s: Retrieving origin ID failed: %m", __func__);
        return status;
    }
    
    status = junos_kcom_init_extensive(origin_id, ctx, &params);
    INSIST(status == KCOM_OK);

    // Register for IFD notification (listen for ms interfaces going down)
    status = junos_kcom_register_ifd_handler(NULL, ifd_down_async_handler);
    if(status != KCOM_OK) {
        LOG(TRACE_LOG_ERR, "%s: Register ifd handler failed", __func__);
    }
    
    // register for General KCOM GENCFG events
    bzero(&kcom_req, sizeof(junos_kcom_gencfg_t));

    kcom_req.opcode       = JUNOS_KCOM_GENCFG_OPCODE_INIT;
    kcom_req.user_handler = gencfg_async_handler;

    status = junos_kcom_gencfg(&kcom_req);
    if(status != KCOM_OK) {
    	LOG(TRACE_LOG_ERR, "%s: Request to register for GENCFG notification "
    	        "failed", __func__);
    	return status;
    }
    
    bzero(&kpb_req, sizeof(junos_kcom_pub_blob_req_t));

    // register for SSRB KCOM GENCFG events
    kpb_req.opcode           = JUNOS_KCOM_PUB_BLOB_REG;
    kpb_req.blob_id          = JNX_PUB_EXT_SVC_SSR_BLOB; 
    kpb_req.msg_user_info    = NULL;
    kpb_req.msg_user_handler = ssrb_async_handler;

    status = junos_kcom_pub_blob_req(&kpb_req);
    if(status != KCOM_OK) {
        LOG(TRACE_LOG_ERR, "%s: Request to register for public SSR blob "
            "notification failed", __func__);
        return status;
    }
    
    return status;
}


/**
 * Shutdown KCOM library and register handlers for asynchronous KCOM messages
 */
void
kcom_shutdown(void)
{
    junos_kcom_pub_blob_req_t   kpb_req;
    
    junos_trace(EQUILIBRIUM_TRACEFLAG_KCOM, "%s", __func__);

    bzero(&kpb_req, sizeof(junos_kcom_pub_blob_req_t));

    kpb_req.opcode  = JUNOS_KCOM_PUB_BLOB_DEREG;
    kpb_req.blob_id = JNX_PUB_EXT_SVC_SSR_BLOB; 

    junos_kcom_pub_blob_req(&kpb_req);

    junos_kcom_shutdown();
}

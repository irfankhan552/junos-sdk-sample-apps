/*
 * $Id: dpm-mgmt_kcom.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-mgmt_kcom.c
 * @brief Init kcom and register handler
 * 
 * 
 * Initialize KCOM and register a handlers
 */

#include <sync/common.h>
#include <jnx/provider_info.h>
#include "dpm-mgmt_kcom.h"
#include "dpm-mgmt_config.h"
#include "dpm-mgmt_conn.h"
#include "dpm-mgmt_logging.h"

#include DPM_OUT_H

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
        
        junos_trace(DPM_TRACEFLAG_KCOM,
    		"%s: Handling ifd %s going down", __func__, name);
        
        // An MS PIC is going down and may be one we're connected to
        mspic_offline(name);
    }
    
    junos_kcom_msg_free(msg);

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
    provider_origin_id_t id;

    status = provider_info_get_origin_id(&id);
    
    if(status != 0) {
        LOG(TRACE_LOG_ERR, "%s: Retriving origin id failed", __func__);
        return -1;
    }
    
    status = junos_kcom_init(id, ctx);
    INSIST(status == KCOM_OK);

    // Register for IFD notification (listen for ms interfaces going down)
    status = junos_kcom_register_ifd_handler(NULL, ifd_down_async_handler);
    if(status != KCOM_OK) {
        LOG(TRACE_LOG_ERR, "%s: Register ifd handler failed", __func__);
    }
    
    return status;
}


/**
 * Shutdown KCOM library and register handlers for asynchronous KCOM messages
 */
void
kcom_shutdown(void)
{
    junos_trace(DPM_TRACEFLAG_KCOM, "%s", __func__);

    junos_kcom_shutdown();
}

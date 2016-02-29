/*
 * $Id: ped_kcom.c 366969 2010-03-09 15:30:13Z taoliu $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file ped_kcom.c
 * @brief Init kcom and register handler
 * 
 * 
 * Initialize KCOM and register a handler for iff changes
 */
#include <sync/common.h>
#include <jnx/provider_info.h>
#include "ped_services.h"
#include "ped_kcom.h"
#include "ped_config.h"
#include "ped_conn.h"
#include "ped_policy_table.h"
#include "ped_service_route.h"
#include "ped_logging.h"

#include PE_OUT_H

/*** Constants ***/

#define KCOM_ID_PED             77  ///< ID of this app with KCOM

#define MSP_IFD_NAME_PATTERN "ms-"  ///< common part of MS PIC interface names

#define INT_NAME_STR_SIZE       64  ///< string size for interface names


/*** Data Structures ***/

extern evContext ped_ctx;      ///< Event context for ped.

static int if_count;           ///< # of IFFs interfaces counted in callback
static int if_total = 0;       ///< Total # of IFFs after junos_kcom_iff_get_all

/*** STATIC/INTERNAL Functions ***/


/**
 * The callback registered to handle changes in the iffs.
 * 
 * @param[in] msg
 *     The iff we want to know about
 * 
 * @param[in] user_info
 *     User info that was registered to be passed to this callback
 * 
 * @return
 *      0 upon successful completion, otherwise -1
 */
static int
iff_async_handler(kcom_iff_t * msg, void * user_info __unused)
{
    INSIST(msg != NULL);

    switch(msg->iff_op) {
        
        case KCOM_ADD:
            
            junos_trace(PED_TRACEFLAG_KCOM, "%s: %s.%d, af: %d, (ADD)",
                __func__, msg->iff_name, msg->iff_subunit, msg->iff_af);
            
            update_interface(msg->iff_name, msg->iff_subunit,
                    msg->iff_af, INTERFACE_ADD);

            break;
            
        case KCOM_DELETE:
            
            junos_trace(PED_TRACEFLAG_KCOM, "%s: %s.%d, af: %d, (DELETE)",
                __func__, msg->iff_name, msg->iff_subunit, msg->iff_af);
            
            update_interface(msg->iff_name, msg->iff_subunit,
                    msg->iff_af, INTERFACE_DELETE);
            break;
            
        case KCOM_CHANGE:
        case KCOM_GET:
            // ignore
            break;
        
        default:
            ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Got an unknown operation type", __func__);
        
    }
    
    junos_kcom_msg_free(msg);
    
    return 0;
}

/**
 * Gets called for all iffs and calls update_interface with INTERFACE_REFRESH. 
 *
 * @param[in] msg
 *     The iff we want to know about
 *
 * @param[in] user_info
 *     User info that was registered to be passed to this callback
 *
 * @return 0 upon successful completion, otherwise -1
 */
static int
iff_manage_handler(kcom_iff_t * msg, void * user_info __unused)
{
    junos_trace(PED_TRACEFLAG_KCOM,
            "%s: Handling iff %s with family %d",
            __func__, msg->iff_name, msg->iff_af);
    
    ++if_count;
    
    update_interface(msg->iff_name, msg->iff_subunit,
            msg->iff_af, INTERFACE_REFRESH);

    junos_kcom_msg_free(msg);

    return 0;
}


/**
 * Gets called for all ifds and calls update_interface with INTERFACE_REFRESH. 
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
ifd_async_handler(kcom_ifdev_t * msg, void * user_info __unused)
{
    char * name = msg->ifdev_name;

    if((msg->ifdev_op == KCOM_DELETE || junos_kcom_ifd_down(msg)) &&
       strstr(name, MSP_IFD_NAME_PATTERN)) {
        
        junos_trace(PED_TRACEFLAG_KCOM, "%s: Handling ifd %s", __func__, name);
        
        // An MS PIC is going down and may be one we're connected to
        mspic_offline(name);
    }
    
    junos_kcom_msg_free(msg);

    return 0;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Get the number of all interfaces.
 *
 */
int
get_interface_total(void)
{
    return if_total;
}

/**
 * Update policy information and apply it for all interfaces which match the 
 * configured conditions by calling update_interface for all IFFs
 */
void
update_all_interfaces(void)
{
    ifl_idx_t idx;

    junos_trace(PED_TRACEFLAG_KCOM, "%s", __func__);

    // Set interface index to 0.
    ifl_idx_t_setval(idx, 0);

    if_count = 0;
    // Walk through all interfaces and families (note set idx to 0).
    junos_kcom_iff_get_all(iff_manage_handler, idx, NULL);
    if_total = if_count;
}


/**
 * Init KCOM library and register handlers 
 * for asynchronous KCOM messages.
 *
 * @return
 *      KCOM_OK (0) on success, or -1 on error.
 */
int
ped_kcom_init(void)
{
    int status;
    provider_origin_id_t origin_id;

    status = provider_info_get_origin_id(&origin_id);
    
    if (status) {
        ERRMSG(PED, TRACE_LOG_ERR,
            "%s: Retrieving origin ID failed: %m", __func__);
        return status;
    }

    junos_trace(PED_TRACEFLAG_KCOM, "%s", __func__);

    status = junos_kcom_init(origin_id, ped_ctx);
    INSIST(status == KCOM_OK);

    if((status = junos_kcom_register_iff_handler(NULL, iff_async_handler))) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Register iff handler FAILED!", __func__);
    }
    
    if((status = junos_kcom_register_ifd_handler(NULL, ifd_async_handler))) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Register ifd handler FAILED!", __func__);
    }
    
    return status;
}

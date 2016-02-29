/*
 * $Id: pfd_kcom.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file cpd_kcom.c
 * @brief Init kcom and register handler
 * 
 * 
 * Initialize KCOM and register a handler for ifa changes
 */

#include <string.h>
#include <jnx/aux_types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <isc/eventlib.h>
#include <jnx/junos_kcom.h>
#include "pfd_kcom.h"
#include "pfd_conn.h"
#include "pfd_config.h"
#include "pfd_packet.h"
#include "pfd_logging.h"
#include "pfd_main.h"

/*** Constants ***/

#define KCOM_ID_PFD   78  ///< ID of this app with KCOM

/*** Data Structures ***/

boolean vrf_ready = FALSE; ///< PFD VRF is ready on PIC

static evContext ev_ctx; ///< event context

/*** STATIC/INTERNAL Functions ***/

/**
 * Shutdowns kcom
 * 
 * @param[in] ctx
 *     The event context for this application
 *
 * @param[in] uap
 *     The user data for this callback
 *
 * @param[in] due
 *     The absolute time when the event is due (now)
 *
 * @param[in] inter
 *     The period; when this will next be called
 */
static void
stop_kcom(evContext ctx UNUSED, void * uap UNUSED,
            struct timespec due UNUSED, struct timespec inter UNUSED)
{
    junos_kcom_shutdown();
}

/**
 * Gets called for all rtbs or upon a change 
 *
 * @param[in] msg
 *     The rtb we want to know about
 *
 * @param[in] user_info
 *     User info that was registered to be passed to this callback
 *
 * @return 0 upon successful completion, otherwise -1
 */
static int
rtb_async_handler(kcom_rtb_t * msg, void * user_info UNUSED)
{    
    if(strcmp(msg->rtb_name, RI_PFD_FORWARDING) == 0) {
        
        if(msg->rtb_op == KCOM_ADD || msg->rtb_op == KCOM_GET) {
            
            // the one we're waiting for
            
            LOG(LOG_INFO, "%s: Found needed VRF %s", __func__,
                    RI_PFD_FORWARDING);
            
            vrf_ready = TRUE;
            
            // shutdown kcom (fires once)
            if(evSetTimer(ev_ctx, stop_kcom, NULL, evConsTime(0, 0), 
                    evConsTime(0, 0), NULL)) {
                LOG(LOG_EMERG, "Initialize the stop kcom timer error");
                return 1;
            }
            
            if(msg->rtb_op == KCOM_GET) {
                junos_kcom_msg_free(msg);
                
                if(cpd_ready && get_cpd_address() && get_pfd_address()) {
                    init_packet_loops();
                }
                return KCOM_ITER_END;
                
            } else {
                junos_kcom_msg_free(msg);
                
                if(cpd_ready && get_cpd_address() && get_pfd_address()) {
                    init_packet_loops();
                }
                return 0;
            }
        } else if(msg->rtb_op == KCOM_DELETE) {
            // It shouldn't be deleted while running, we need it.
            
            LOG(LOG_ALERT, "%s: Stopping service because of VRF delete (%s)",
                    __func__, RI_PFD_FORWARDING);
            
            junos_kcom_msg_free(msg);
            junos_kcom_shutdown(); // stop kcom
            pfd_shutdown(); // exits
            return 0;
        }
    }
            
    junos_kcom_msg_free(msg);

    return 0;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Check for the PFD VRF
 */
void
check_pfd_vrf_status(void)
{
    junos_kcom_rtb_get_all(rtb_async_handler, AF_INET, NULL);
}


/**
 * Init KCOM library and register handlers 
 * for asynchronous KCOM messages.
 *
 * @return
 *      KCOM_OK (0) on success, or -1 on error.
 */
int
pfd_kcom_init(evContext ctx)
{
    int status;
    
    ev_ctx = ctx;

    // We cannot get the origin ID on the PIC to feed to kcom init
    status = junos_kcom_init(KCOM_ID_PFD, ctx); // any number will do
    INSIST(status == KCOM_OK);

    if((status = junos_kcom_register_rtb_handler(NULL, rtb_async_handler))) {
        LOG(LOG_ERR, "%s: Register rtb handler FAILED!", __func__);
    }
    
    check_pfd_vrf_status();
    
    return status;
}

/*
 * $Id: cpd_kcom.c 346460 2009-11-14 05:06:47Z ssiano $
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

#include <jnx/aux_types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <isc/eventlib.h>
#include <jnx/junos_kcom.h>
#include "cpd_kcom.h"
#include "cpd_config.h"
#include "cpd_http.h"
#include "cpd_logging.h"

/*** Constants ***/

#define KCOM_ID_CPD             77  ///< ID of this app with KCOM

/*** Data Structures ***/


/*** STATIC/INTERNAL Functions ***/



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
ifa_async_handler(kcom_ifa_t * msg, void * user_info UNUSED)
{    
    static in_addr_t last_address = 0; 
    
    int i;
    in_addr_t address;
    u_int8_t * cp, * prefix;
    
    if(get_cpd_address() == 0) {
        return 0; // We haven't yet received the server address
    }

    // convert address to in_addr_t
    if(msg->ifa_lplen > IN_HOST_PLEN) {
        LOG(LOG_ERR, "%s: Incorrect inet prefix length %d", __func__,
                msg->ifa_lplen);
    }
    
    address = 0;
    cp = (u_int8_t *) &address;
    prefix = (u_int8_t *)msg->ifa_lprefix;
    for (i = 0;  i < (int)sizeof(in_addr_t); ++i)
        *cp++ = *prefix++;
    
    if(address == get_cpd_address()) {
        
        if((msg->ifa_op == KCOM_ADD || msg->ifa_op == KCOM_GET) &&
                address != last_address) { // check for an address change
            
            last_address = address;
            
            // it is a valid address
            LOG(LOG_INFO, "%s: Starting server because of address addition",
                    __func__);
            init_http_server(); // The CPD's HTTP server address has been added
            
            if(msg->ifa_op == KCOM_GET) {
                junos_kcom_msg_free(msg);
                return KCOM_ITER_END;
            } else {
                junos_kcom_msg_free(msg);
                return 0;
            }
            
        } else if(msg->ifa_op == KCOM_DELETE) {
            // it is now an invalid address
            LOG(LOG_INFO, "%s: Stopping server because of server address delete",
                    __func__);
            shutdown_http_server();
            junos_kcom_msg_free(msg);
            return 0;
        }
    } else {
        struct in_addr in;
        in.s_addr = address;
        LOG(LOG_DEBUG, "%s: Notified of PIC address %s with op %d",
                __func__, inet_ntoa(in), msg->ifa_op);
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
ifl_iterator(kcom_ifl_t * msg, void * user_info UNUSED)
{
    int status;

    status = junos_kcom_ifa_get_all(ifa_async_handler, msg->ifl_index,
                AF_INET, NULL);

    if(!status) {
        LOG(LOG_INFO, "%s: Could not find a matching address configured for"
               " IFL %s", __func__, msg->ifl_name); // may not yet be configured
    } else if (status && status != KCOM_ITER_END) {
        // Something went wrong
        LOG(LOG_ERR, "%s: Error iterating over the IFAs (%d) for IFL %s",
                __func__, status, msg->ifl_name);
    } // else status == KCOM_ITER_END so it was found
            
    junos_kcom_msg_free(msg);

    return 0;
}



/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Check for the server address, and if present start the server
 */
void
check_address_status(void)
{
    junos_kcom_ifl_get_all(ifl_iterator, NULL, NULL);
}


/**
 * Init KCOM library and register handlers 
 * for asynchronous KCOM messages.
 *
 * @return
 *      KCOM_OK (0) on success, or -1 on error.
 */
int
cpd_kcom_init(evContext ctx)
{
    int status;

    // We cannot get the origin ID on the PIC to feed to kcom init
    status = junos_kcom_init(KCOM_ID_CPD, ctx); // any number will do
    INSIST(status == KCOM_OK);

    if((status = junos_kcom_register_ifa_handler(NULL, ifa_async_handler))) {
        LOG(LOG_ERR, "%s: Register ifa handler FAILED!", __func__);
    }
    
    return status;
}

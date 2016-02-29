/*
 * $Id: jnx-exampled_kcom.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-exampled_kcom.c - functions using kcom interfaces.
 *
 * Copyright (c) 2006, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <sys/types.h>
#include <jnx/trace.h>
#include <ddl/dtypes.h>

#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>
#include <jnx/provider_info.h>
#include JNX_EXAMPLED_OUT_H

#include "jnx-exampled_kcom.h"

/*
 * JUNOS SDK applications determine their KCOM ID from the origin ID at runtime.
 * JUNOS internal applications are assigned specific IDs.
 */
#ifndef __JUNOS_SDK__
#define KCOM_ID_EXAMPLED 24
#endif



static int 
jnx_exampled_print_ifl (kcom_ifl_t *ifl, void *arg __unused)
{
    junos_trace(JNX_EXAMPLED_TRACEFLAG_RTSOCK,
                "Found ifl %s.%d during periodic scan", ifl->ifl_name,
                ifl->ifl_subunit);

    junos_kcom_msg_free(ifl);

    return (0);
}

/******************************************************************************
 *                        Local Functions
 *****************************************************************************/

static void
jnx_exampled_scan_ifl (evContext ctx __unused, void *uap  __unused,
                          struct timespec due __unused,
                          struct timespec inter __unused)
{
    junos_kcom_ifl_get_all(jnx_exampled_print_ifl, NULL, NULL);
}

/******************************************************************************
 * Function : jnx_exampled_iff_msg_handler
 * Purpose  : handler to print async msgs of iffs to trace file
 * Inputs   : async message, user arg
 *****************************************************************************/

static int
jnx_exampled_iff_msg_handler (kcom_iff_t *iffm, void *arg __unused)
{
    junos_trace(JNX_EXAMPLED_TRACEFLAG_RTSOCK, "Received async msg for iff %s",
                    iffm->iff_name);

    junos_kcom_msg_free(iffm);
    return (0);
}

/******************************************************************************
 * Function : jnx_exampled_ifl_msg_handler
 * Purpose  : handler to print async msgs of ifls to trace file
 * Inputs   : async message, user arg
 *****************************************************************************/
static int
jnx_exampled_ifl_msg_handler (kcom_ifl_t *iflm, void *arg __unused)
{
    junos_trace(JNX_EXAMPLED_TRACEFLAG_RTSOCK, "Received async msg for ifl %s",
                iflm->ifl_name);

    junos_kcom_msg_free(iflm);
    return (0);
}

/******************************************************************************
 * Function : jnx_exampled_ifd_msg_handler
 * Purpose  : handler to print async msgs of ifds to trace file
 * Inputs   : async message, user arg
 *****************************************************************************/
static int
jnx_exampled_ifd_msg_handler (kcom_ifdev_t *ifdm, void *arg __unused)
{
    junos_trace(JNX_EXAMPLED_TRACEFLAG_RTSOCK, "Received async msg for ifd %s",
                ifdm->ifdev_name);

    junos_kcom_msg_free(ifdm);
    return (0);
}

/******************************************************************************
 *                        Global Functions
 *****************************************************************************/

/******************************************************************************
 * Function : jnx_exampled_kcom_init
 * Purpose  : Init KCOM library and 
 *            Register for asynchronous message.
 * Return   : 0: success  -1: error
 *****************************************************************************/

int
jnx_exampled_kcom_init (evContext ctxt)
{
    int error;
    
    /*
     * JUNOS SDK applications determine their KCOM ID from the origin ID at 
     * runtime. JUNOS internal applications are assigned specific IDs.
     */
#ifdef __JUNOS_SDK__
    provider_origin_id_t origin_id;

    error = provider_info_get_origin_id(&origin_id);
    
    if (error) {
        junos_trace(JNX_EXAMPLED_TRACEFLAG_INIT,
            "Retrieving origin ID failed: %m");
        return -1;
    }

    error = junos_kcom_init(origin_id, ctxt);
#else
    error = junos_kcom_init(KCOM_ID_EXAMPLED, ctxt);
#endif
    
    INSIST(error == KCOM_OK);

    /*
     * Register handlers for asynchronous messages we care about.
     * (Here we register for updates on iff/ifd/ifls, just to
     * serve as an example).
     */
    error = junos_kcom_register_iff_handler(NULL, jnx_exampled_iff_msg_handler);
    if (error) {
        junos_trace(JNX_EXAMPLED_TRACEFLAG_RTSOCK,
                    "Failed to register iffm handler (error = %d)", error);
    }

    error = junos_kcom_register_ifl_handler(NULL, jnx_exampled_ifl_msg_handler);
    if (error) {
        junos_trace(JNX_EXAMPLED_TRACEFLAG_RTSOCK,
                    "Failed to register iflm handler (error = %d)", error);
    }

    error = junos_kcom_register_ifd_handler(NULL, jnx_exampled_ifd_msg_handler);
    if (error) {
        junos_trace(JNX_EXAMPLED_TRACEFLAG_RTSOCK,
                    "Failed to register ifdm handler (error = %d)", error);
    }

    junos_trace(JNX_EXAMPLED_TRACEFLAG_RTSOCK, "%s",
                "------ Finished with RTSOCK initialization ------");

    /*
     * start a periodic timer now, to give an example.
     * Whenever the timer expires, the status of an ifl is checked
     * using the sync rtsock.
     */
    if (evSetTimer(ctxt, jnx_exampled_scan_ifl, NULL,
                   evConsTime(0, 0),
                   evConsTime(JNX_EXAMPLED_KCOM_TIMER_PERIOD, 0), NULL)) {
        ERRMSG(JNX_EXAMPLED_EVLIB_FAILURE, TRACE_LOG_ERR, "evSetTimer: %m");
        return -1;
    }
    return 0;
}

/* end of file */

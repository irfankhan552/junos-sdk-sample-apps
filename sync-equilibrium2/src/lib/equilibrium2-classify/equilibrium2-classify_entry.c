/*
 * $Id: equilibrium2-classify_entry.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium2-classify_entry.c
 * @brief Contains plugin entry point
 *
 */

/* The Application and This Plug-in's Documentation: */

/**

@mainpage

@section overview 1 Overview

This is the classify service of Equilibrium II application. It classifies the
traffic by TCP port and modifies the destination address in IP header to the
proper service gate address.

See balance service documentation for the common service description.

*/

#include <sync/equilibrium2.h>
#include <sync/equilibrium2_svc.h>
#include "equilibrium2-classify.h"

#include <jnx/multi-svcs/msvcs_plugin.h>

/**
 * The entry of plugin service.
 *
 * @return
 *      Valid plugin ID on success, -1 on failure
 */
int
equilibrium2_classify_entry (void)
{
    msvcs_plugin_params_t params;

    strlcpy(params.spp_name, EQ2_CLASSIFY_SVC_NAME, sizeof(params.spp_name));
    params.spp_data_evh = equilibrium2_classify_data_hdlr;
    params.spp_control_evh = equilibrium2_classify_ctrl_hdlr;
    params.spp_plugin_app_id = CLASSIFY_PLUGIN;
    /* Plugin class will be determined by mspmand automatically according to
     * the provided ID.
     */
    params.spp_class = MSVCS_PLUGIN_CLASS_EXTERNAL;
    classify_pid = msvcs_plugin_register(&params);

    if (classify_pid < 0) {
        logging(LOG_ERR, "%s: Register service ERROR!", __func__);
    } else {
        logging(LOG_INFO, "%s: Service was registered.", __func__);
        if (msvcs_plugin_register_event_class(classify_pid, EV_CLASS_CLASSIFY,
                &classify_ev_class) < 0) {
            msp_log(LOG_ERR, "%s: Register event class ERROR!", __func__);
        }
    }
    return classify_pid;
}


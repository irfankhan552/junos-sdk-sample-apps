/*
 * $Id: equilibrium2-balance_entry.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium2-balance_entry.c
 * @brief Contains plugin entry point
 *
 */

/* The Application and This Plug-in's Documentation: */

/**

@mainpage

@section overview 1 Overview

This is the balance service of Equilibrium II application. It balances the
traffic load to a group of servers.

The service plugin exists as a dynamic object. It consists of three components,
entry, control event handler and data event handler.

The entry is called when the plugin is loaded as a dynamic object by management
daemon. It registers plugin information and gets a plugin ID.

The control event handler is called when there is a control event distributed
to the service.

The data event handler is called when there is a data event distributed to the
service.

@section functionality 2 Functionality

@subsection register 2.1 Register

The entry function is called when the service is loaded as a dynamic object
by the management daemon. It registers the control event handler, data event
handler and some other plugin information to the management daemon, then it
gets a plugin ID.

@subsection ctrl_event_hdlr 2.2 Control Event Handler

The control event handler is called when there is a control event distributed
to the service. For external service plguin, there are two control events need
to be processed, initialization event and configuration blob event.

The initializtion event is received at only once at the beginning, the control
event handler initializes the environment when it received this event.

The configuration blob event is received when there is a configuration blob
comming to this service. Two types of configuration blobs are for balance
service and two operations are for the blob as below.

Adding server group blob, the handler unpacks the blob and updates the server
group list.

Deleteing server group blob, the handler doesn't do anything.

Adding service-set blob, the handler unpacks the blob and adds all service-set
rules to policy database for data event handler to use. The data event handler
will receive packet belonging to certain service-set only after all registered
plugins added their rules to the policy database of that service-set.

Deleting service-set blob, the handler marks it inactive, but not remove it
from policy database. Because once the policy is removed from policy database
for certain service-set, the data event handler won't receive packet of that
service-set and this will break the session in the middle. During this transit
period, when the old policy is still being used and new policy came in, the
data event handler doesn't accept any new session (because the new policy is
not active yet), and once all the existing sessions are close, the old policy
is removed from policy database and the new policy is activated. 

@subsection data_event_hdlr 2.3 Data Event Handler

@c MSVCS_DATA_EV_SM_INIT event is for initializing shared memory for packet
process.

@c MSVCS_DATA_EV_INIT event is for initializing plugin data event handler
environment.

@c MSVCS_DATA_EV_FIRST_PKT_PROC event is for processing the first packet of
a session. When receiving the first packet, the handler goes through all rules
and looks for the proper action applying to this session. Then the handler
creates an action data structure and attaches it to the session, so the same
action will apply to all the rest packets in this session without going through
all rules again.

@c MSVCS_DATA_EV_PKT_PROC event is for processing the rest packets in the
session. The handler just retrieves the action from the session and apply it
to the packet.

@c MSVCS_DATA_EV_SESSION_OPEN is received when the session is open. This event
is received after @c MSVCS_DATA_EV_FIRST_PKE_PROC.

@c MSVCS_DATA_EV_SESSION_CLOSE is received when the session is closed.

*/

#include <sync/equilibrium2.h>
#include <sync/equilibrium2_svc.h>
#include "equilibrium2-balance.h"

#include <jnx/multi-svcs/msvcs_plugin.h>
 

/**
 * @brief
 * The entry of plugin service.
 *
 * @return
 *      Valid plugin ID on success, -1 on failure
 */
int
equilibrium2_balance_entry (void)
{
    msvcs_plugin_params_t params;

    strlcpy(params.spp_name, EQ2_BALANCE_SVC_NAME, sizeof(params.spp_name));
    params.spp_data_evh = equilibrium2_balance_data_hdlr;
    params.spp_control_evh = equilibrium2_balance_ctrl_hdlr;
    params.spp_plugin_app_id = BALANCE_PLUGIN;
    params.spp_class = MSVCS_PLUGIN_CLASS_EXTERNAL;
    balance_pid = msvcs_plugin_register(&params);

    if (balance_pid < 0) {
        msp_log(LOG_ERR, "%s: Register service ERROR!", __func__);
    } else {
        msp_log(LOG_INFO, "%s: Service was registered.", __func__);
    }
    return balance_pid;
}


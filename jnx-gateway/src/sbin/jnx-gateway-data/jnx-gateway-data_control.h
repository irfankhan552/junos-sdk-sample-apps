/*
 *$Id: jnx-gateway-data_control.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-gateway-data_control.h - Prototypes for the control thread functions
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

#ifndef JNX_GATEWAY_DATA_CONTROL_H_
#define JNX_GATEWAY_DATA_CONTROL_H_

/**
 * @file jnx-gateway-data-control.h
 * @brief This file lists the function prototypes for the various actvities
 * performed by th control thread.
 */

#include "jnx-gateway-data.h"
#include <jnx/jnx-gateway_msg.h>

/* Function to process the message from JNX-GW-MGMT (RE) or JNX-GW-CTRL (CTRL */
extern status_t jnx_gw_data_pconn_msg_handler(pconn_session_t*  session, 
                                          ipc_msg_t*  ipc_msg, void*  cookie);

/* Function to process the events from MGMT / CTRL */
extern void jnx_gw_data_pconn_event_handler(pconn_session_t *session, 
                                            pconn_event_t event, void* cookie);

/* Function to receive the messages from the jnx-gateway-mgmt(not used
 * currently) */
extern void jnx_gw_data_pconn_client_msg_handler(pconn_client_t*  client, 
                                        void*  ipc_msg, void*  cookie);

/* Function to process the events for the jnx-gateway-mgmt client */
extern void jnx_gw_data_pconn_client_event_handler(pconn_client_t *client, 
                                          pconn_event_t event);

/* Function to send the Periodic Statistics to the JNX-GW-MGMT (RE) */
extern void jnx_gw_data_send_periodic_msg(jnx_gw_data_cb_t* app_cb);

#endif

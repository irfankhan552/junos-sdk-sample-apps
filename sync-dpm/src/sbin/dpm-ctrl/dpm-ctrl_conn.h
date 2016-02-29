/*
 * $Id: dpm-ctrl_conn.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-ctrl_conn.h
 * @brief Relating to managing the connections
 * 
 * These functions and types will manage the connections.
 */
 
#ifndef __DPM_CTRL_CONN_H__
#define __DPM_CTRL_CONN_H__

#include <sync/dpm_ipc.h>

/*** Constants ***/


/*** Data structures ***/



/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the connection to the mgmt component
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_connections(evContext ctx);


/**
 * Terminate connection to the mgmt component
 */
void
close_connections(void);


/**
 * Notify the mgmt component about a subscriber status update
 * 
 * @param[in] action
 *      login/logout action
 * 
 * @param[in] mdi_mlr
 *      the subscriber name
 * 
 * @param[in] class_name
 *      the subscriber class name
 */
void
notify_status_update(status_msg_type_e action,
                     char * subscriber_name,
                     char * class_name);


#endif


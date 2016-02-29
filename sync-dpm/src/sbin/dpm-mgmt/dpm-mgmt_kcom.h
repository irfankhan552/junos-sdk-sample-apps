/*
 * $Id: dpm-mgmt_kcom.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-mgmt_kcom.h
 * 
 * @brief Routines related to KCOM
 * 
 * Function to initialize KCOM and register a handler for iff changes.
 */

#ifndef __DPM_KCOM_H__
#define __DPM_KCOM_H__

/*** Constants ***/

/*** Data structures ***/

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
kcom_init(evContext ctx);


/**
 * Shutdown KCOM library and register handlers for asynchronous KCOM messages
 */
void
kcom_shutdown(void);


#endif

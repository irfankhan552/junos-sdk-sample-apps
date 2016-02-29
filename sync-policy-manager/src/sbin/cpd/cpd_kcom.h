/*
 * $Id: cpd_kcom.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file cpd_kcom.h
 * 
 * @brief Routines related to KCOM
 * 
 * Function to initialize KCOM and and deal with server address changes
 */
#ifndef __CPD_KCOM_H__
#define __CPD_KCOM_H__

/*** Constants ***/

/*** Data structures ***/

/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Check for the server address, and if present start the server
 */
void
check_address_status(void);


/**
 * Init KCOM library and register handlers 
 * for asynchronous KCOM messages.
 *
 * @return
 *      KCOM_OK (0) on success, or -1 on error.
 */
int
cpd_kcom_init(evContext ctx);

#endif

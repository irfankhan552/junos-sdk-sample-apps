/*
 * $Id: pfd_kcom.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file pfd_kcom.h
 * 
 * @brief Routines related to KCOM
 * 
 * Function to initialize KCOM and and deal with route table (instance) changes
 */
#ifndef __PFD_KCOM_H__
#define __PFD_KCOM_H__

#include <stdbool.h> 

/*** Constants ***/

/*** Data structures ***/

extern boolean vrf_ready; ///< PFD VRF is ready on PIC

/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Check for PFD VRF
 */
void
check_pfd_vrf_status(void);


/**
 * Init KCOM library and register handlers 
 * for asynchronous KCOM messages.
 *
 * @return
 *      KCOM_OK (0) on success, or -1 on error.
 */
int
pfd_kcom_init(evContext ctx);

#endif

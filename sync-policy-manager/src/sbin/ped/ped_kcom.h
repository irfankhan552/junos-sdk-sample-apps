/*
 * $Id: ped_kcom.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */
 
/**
 * @file ped_kcom.h
 * 
 * @brief Routines related to KCOM
 * 
 * Function to initialize KCOM and register a handler for iff changes.
 */
#ifndef __PED_KCOM_H__
#define __PED_KCOM_H__

/*** Constants ***/

/*** Data structures ***/

/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init KCOM library and register handlers
 * for asynchronous KCOM messages.
 *
 * @return
 *      KCOM_OK (0) on success, or -1 on error.
 */
int ped_kcom_init(void);


/**
 * Update policy information and apply it for all interfaces which match the 
 * configured conditions by calling update_interface for all IFFs
 */
void update_all_interfaces(void);

/**
 * Get the number of all interfaces.
 *
 */
int get_interface_total(void);

#endif

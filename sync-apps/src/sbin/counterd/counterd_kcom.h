/*
 * $Id: counterd_kcom.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file counterd_kcom.h
 * 
 * @brief Routines related to KCOM
 * 
 * Function to initialize KCOM and to do GENCFG
 */
#ifndef __COUNTERD_KCOM_H__
#define __COUNTERD_KCOM_H__

#include <isc/eventlib.h>

/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init KCOM library so we can use it
 *
 * @param[in] ctx
 *      event context for app
 * 
 * @return
 *      KCOM_OK (0) on success, or -1 on error.
 */
int counterd_kcom_init(evContext ctx);


/**
 * Add some data to KCOM GENCFG
 *
 * @param[in] data
 *      data to add
 * 
 * @param[in] data_len
 *      length in bytes of data
 * 
 * @return
 *      KCOM_OK (0) on success, or a KCOM error on failure
 */
int counterd_add_data(void * data, uint32_t data_len);


/**
 * Get some data from KCOM GENCFG
 *
 * @param[in] data
 *      pointer of where to copy the data
 * 
 * @param[in] data_len
 *      max number of bytes to copy to into data
 * 
 * @return
 *      number of bytes of data read or -1 on error
 */
int
counterd_get_data(void * data, uint32_t data_len);

#endif

/* $Id: jnx-ifinfod_kcom.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 *jnx-ifinfod_kcom.h : jnx-ifinfod kernel communication functions
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2005-2008, Juniper Networks, Inc.
 * All rights reserved.
 */


#ifndef __JNX_IFINFOD_KCOM_H__
#define __JNX_IFINFOD_KCOM_H__

#define IFINFOD_KCOM_TIMER 60
#define MAX_BUF 80
#define LOW_PORT 161
#define HIGH_PORT 4453
#define SYSLOG(_tag, _prio, _fmt...) \
                ERRMSG(_tag, (_prio), ##_fmt)


/**
 * @file   jnx-ifinfod_kcom.h
 * @brief  jnx-ifinfo kernel communication functions
 */

/**
 * 
 *Function : ifinfod_kcom_init
 *Purpose  : This function initializes kcom subsystem and registers
 *           ifl/ifd/ifa and iff handlers
 * @param[in] ctxt
 *       Event context
 *
 *
 * @return 0 on success or 1 on failure
 */
int ifinfod_kcom_init (evContext ctxt);

/**
 *
 *Function : ifinfod_create_socket
 *Purpose  : This function creates a UDP socket and verifies port binding
 *           to the portnumber provided
 * @param[in] portnum
 *       port number to which the UDP socket needs to bind
 *
 *
 * @return N/A
 */
void ifinfod_create_socket (int);
#endif /* end of __JNX_IFINFOD_KCOM_H__ */

/*
 * $Id: dpm-ctrl_http.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-ctrl_http.h
 * @brief Relating to the DPM's HTTP server 
 *
 * 
 * The DPM runs an HTTP server in the dpm-ctrl_http module on a separate thread.
 * The public functions provide a way to start and stop the HTTP server.
 */

 
#ifndef __DPM_CTRL_HTTP_H__
#define __DPM_CTRL_HTTP_H__

/*** Constants ***/


/*** Data Structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize and start the HTTP server on a separate thread
 */
void
init_http_server(void);


/**
 * Stop and shutdown the HTTP server. It supports being restarted. Server 
 * cannot be suspended before calling this.
 */
void
shutdown_http_server(void);


/**
 * Suspend the HTTP server. May block.
 */
void
suspend_http_server(void);


/**
 * Resume the HTTP server. Can only be called if suspended with 
 * suspend_http_server.
 */
void
resume_http_server(void);


#endif

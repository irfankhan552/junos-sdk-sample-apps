/*
 * $Id: cpd_http.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file cpd_http.h
 * @brief Relating to the CPD's HTTP server 
 *
 * 
 * The CPD runs an HTTP server in the cpd_http module on a separate thread.
 * The functions provide a way to start and stop the HTTP server.
 */
 
#ifndef __CPD_HTTP_H__
#define __CPD_HTTP_H__

/*** Constants ***/


/*** Data Structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize and start the HTTP server on a separate thread
 */
void init_http_server(void);


/**
 * Stop and shutdown the HTTP server. It supports being restarted.
 */
void shutdown_http_server(void);


#endif

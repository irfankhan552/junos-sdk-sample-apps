/*
 * $Id: psd_server.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file psd_server.h
 * @brief Interface for the policy server
 * 
 * Functions to initialize the server functionality and lookup a policy
 */
 
#ifndef __PSD_SERVER_H__
#define __PSD_SERVER_H__

/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the server socket and start listening for new connections.
 * It binds to inet0 depending on and listens on port PSD_PORT_NUM.
 * It also calls evListen which registers psd_ipc_connect as the callback
 * function to accept new incoming connections.
 * 
 * @param[in] ctx
 *      Eventlib context for this application (will be saved).
 * 
 * @return
 *      SUCCESS if successful, or EFAIL if failed.
 */
int server_init(evContext ctx);


/**
 * Notify all currently connected clients to update policies.
 */
void notify_all_clients(void);


/**
 * Shutdown the server socket that accecpts new connections
 * (only used when stopping/restarting the daemon)
 * 
 * @return
 *      SUCCESS if the server connection id wasn't set because 
 *      the initial call to listen failed, or if it was, then if 
 *      we successfully stop listening for connections; or
 *      EFAIL if we were listening, but the attempt to stop 
 *      listening for connections failed.
 */
int server_shutdown(void);



#endif

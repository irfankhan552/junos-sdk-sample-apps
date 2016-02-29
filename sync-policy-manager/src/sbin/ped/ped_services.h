/*
 * $Id: ped_services.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_services.h
 * 
 * @brief Routines related to talking to psd 
 * 
 * Functions for connecting to psd and requesting policies from it. 
 */
#ifndef __PED_SERVICES_H__
#define __PED_SERVICES_H__


/*** Constants ***/


/*** Data structures ***/


/**
 * @brief applicable operations on interfaces
 */
typedef enum {
    INTERFACE_ADD = 1,        ///< Interface add operation
    INTERFACE_REFRESH,        ///< Interface refresh operation
    INTERFACE_DELETE          ///< Interface delete operation
} interface_op_e;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Check interface name against the configured conditions.
 * If it's match update this interface in the table of 
 * managed interfaces (associated policies) and if needed 
 * request a policy from the PSD.
 * 
 * @param[in] ifname
 *     interface name
 *
 * @param[in] unit
 *     interface (IFL) unit number
 *
 * @param[in] af
 *     address family
 *
 * @param[in] op
 *     operation to this interface
 *
 */
void update_interface(
        char * ifname, uint32_t unit, uint8_t af, interface_op_e op);


/**
 * Check heartbeat from PSD. This function is routinely called by ped_periodic()
 * If not received then disconnect and reconnect. This also establishes the 
 * connection when the application starts.
 * 
 * @return TRUE if a heartbeat was sent to the PSD, FALSE if the connection to 
 *      the PSD is down and cannot be re-established.
 */
boolean check_psd_hb(void);


/**
 * Update policies
 */
void update_policies(void);


/**
 * Disconnect the connection to the  Policy Server Daemon (psd).
 * Something's gone wrong...so we'll schedule a reconnect using a heart beat
 * failure flag (psd_hb).
 * 
 */
void disconnect_psd(void);


/**
 * Get the state of PSD connection.
 * 
 * @return
 *      state of psd connection T=up, F=down
 */
boolean psd_conn_state(void);


/**
 * Get the number of seconds since that the connection to the PSD has been up
 * 
 * @return
 *      the number of seconds since that the connection to the PSD has been up
 * 
 */
int get_psd_conn_time(void);

#endif

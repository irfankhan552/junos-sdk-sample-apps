/*
 * $Id: ped_snmp.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_snmp.h
 * 
 * @brief Routines related to SNMP
 * 
 * Functions to intialize snmp sub-agent and process snmp requests. 
 * 
 */
#ifndef __PED_SNMP_H__
#define __PED_SNMP_H__

/*** Constants ***/

 
#define PE_MIB_OID              SNMP_OID_ENTERPRISES, 2636, 5, 5, 21    ///< OID of PE MIB
 
#define PE_MIB_NOTIFY_OID       PE_MIB_OID, 0    ///< OID of PE notification
#define PE_MIB_DATA_OID         PE_MIB_OID, 1    ///< OID of PE data
 
#define PE_MIB_IF_TOTAL_OID     PE_MIB_DATA_OID, 1    ///< OID of the number of total interfaces
#define PE_MIB_IF_TABLE_OID     PE_MIB_DATA_OID, 2    ///< OID of the policy table
#define PE_MIB_IF_COUNT_OID     PE_MIB_DATA_OID, 3    ///< OID of the number of interfaces under management
#define PE_MIB_SNMP_VER_OID     PE_MIB_DATA_OID, 4    ///< OID of the net-snmp version
#define PE_MIB_PSD_STATE_OID    PE_MIB_DATA_OID, 5    ///< OID of the state of PSD connection
#define PE_MIB_PSD_TIME_OID     PE_MIB_DATA_OID, 6    ///< OID of the up time of PSD connection
 
#define PE_MIB_NOTIFY_PSD_STATE_OID     PE_MIB_NOTIFY_OID, 1 ///< OID of notification of the state of PSD connection


/**
 * Initialze snmp sub-agent.
 * 
 */
void netsnmp_subagent_init(void);

/**
 * Send notification of PSD connection status.
 * 
 * @param[in] state
 *      The state of PSD connection.
 * 
 */
void ped_notify_psd_state(boolean state);

#endif

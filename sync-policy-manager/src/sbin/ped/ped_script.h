/*
 * $Id: ped_script.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_script.h
 * @brief Configures the router through operation script
 * 
 * These functions provide high-level operations to manipulate the configuration
 * in the router related to attaching and removing filters to interfaces.
 */

#ifndef __PED_SCRIPT_H__
#define __PED_SCRIPT_H__


/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Execute operation script
 * 
 * @param[in] cmd
 *     CLI command to execute operation script
 * 
 * @return
 *      0 if operation script didn't return any error message, otherwise -1
 */
int exec_op_script(char *cmd);


#endif

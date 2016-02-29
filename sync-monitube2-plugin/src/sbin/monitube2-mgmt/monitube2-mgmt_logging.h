/*
 * $Id: monitube2-mgmt_logging.h 366969 2010-03-09 15:30:13Z taoliu $
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
 * @file monitube2-mgmt_logging.h
 * 
 * @brief Contains log tag
 * 
 * Contains log tag constant
 */


#ifndef __MONITUBE2_MGMT_LOGGING_H__
#define __MONITUBE2_MGMT_LOGGING_H__

/*** Constants ***/

/**
 * A logging helper for this daemon
 */
#define LOG(_level, _fmt...)   \
    ERRMSG(MONITUBE2_MGMT, (_level), _fmt)

/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/


#endif

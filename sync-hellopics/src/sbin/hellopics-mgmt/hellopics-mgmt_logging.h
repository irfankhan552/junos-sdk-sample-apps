/*
 * $Id: hellopics-mgmt_logging.h 366969 2010-03-09 15:30:13Z taoliu $
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
 * @file hellopics-mgmt_logging.h
 * 
 * @brief Contains log tag
 * 
 * Contains log tag constant
 */


#ifndef __HELLOPICS_MGMT_LOGGING_H__
#define __HELLOPICS_MGMT_LOGGING_H__

#include <jnx/logging.h>
/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/
/**
 * Define a logging macro which prefixes a log tag like on the RE
 */
#define LOG(_level, _fmt...)   \
    logging((_level), "HELLOPICS_MGMT: " _fmt)


#endif

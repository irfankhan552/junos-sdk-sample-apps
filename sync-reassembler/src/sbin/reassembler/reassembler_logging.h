/*
 * $Id: reassembler_logging.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file reassemabler_logging.h
 * @brief Relating to logging
 * 
 * These functions / macros are for logging in this component
 */
 
#ifndef __REASSEMBLER_LOGGING_H__
#define __REASSEMBLER_LOGGING_H__

#include <jnx/logging.h>

/*** Constants ***/

/**
 * Define a logging macro which prefixes a log tag like on the RE
 */
#define LOG(_level, _fmt...)   \
    logging((_level), "IP REASSEMBLER: " _fmt)

/**
 * Define a logging macro which prefixes a log tag like on the RE
 * 
 * This one was to be used by the data thread for better logging performance but
 * msp_log is only available for plug-ins currently, so we use logging for now
 */
#define DLOG(_level, _fmt...)   \
    logging((_level), "IP REASSEMBLER: " _fmt)

/**
 * Define an INSIST/assert with logging
 */
#define INSIST_ERR(c) if (!(c)) \
    logging(LOG_EMERG, "IP REASSEMBLER: %s:%d: insist '%s' failed!", \
        __FILE__, __LINE__, #c); else (void)NULL

/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/

#endif


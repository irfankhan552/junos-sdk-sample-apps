/*
 * $Id: monitube2-data_logging.h 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file monitube2-data_logging.h
 * @brief Relating to logging
 * 
 * These functions / macros are for logging in this component
 */
 
#ifndef __MONITUBE2_DATA_LOGGING_H__
#define __MONITUBE2_DATA_LOGGING_H__

#include <jnx/logging.h>

/*** Constants ***/

/**
 * Ctrl-side logging
 */
#define CLOG(_level, _fmt...)   \
    logging((_level), "MONITUBE PLUG-IN: " _fmt)

/**
 * (More efficient) Data-side logging
 */
#define DLOG(_level, _fmt...)   \
    msp_log((_level), "MONITUBE PLUG-IN: " _fmt)

/**
 * Define an INSIST/assert with logging
 */
#define INSIST_ERR(c) if (!(c)) \
    logging(LOG_EMERG, "MONITUBE PLUG-IN: %s:%d: insist '%s' failed!", \
        __FILE__, __LINE__, #c); else (void)NULL

/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/

#endif


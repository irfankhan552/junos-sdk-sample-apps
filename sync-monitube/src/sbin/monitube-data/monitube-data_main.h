/*
 * $Id: monitube-data_main.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-data_main.h
 * @brief Functions for shutting down the MONITUBE
 *
 * Functions for shutting down the MONITUBE
 */

#ifndef __MONITUBE_DATA_MAIN_H__
#define __MONITUBE_DATA_MAIN_H__

// some common includes

#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/mpsdk.h>
#include <jnx/atomic.h>
#include <jnx/msp_locks.h>
#include <jnx/msp_objcache.h>

#include "monitube-data_logging.h"

/*** Constants ***/

/*** Data structures ***/

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * There are several things that shouldn't happen until FDB is attached.
 * This is called only once it is attached.
 *
 * If any initialization fails the application exits
 */
void
init_application(void);

#endif


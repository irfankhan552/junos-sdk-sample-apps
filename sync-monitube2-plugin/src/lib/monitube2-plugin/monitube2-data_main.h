/*
 * $Id: monitube2-data_main.h 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file monitube2-data_main.h
 * @brief Common macros used globally
 */
 
#ifndef __MONITUBE2_DATA_MAIN_H__
#define __MONITUBE2_DATA_MAIN_H__


#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

/*
 * From libisc2
 */
#include <isc/eventlib.h>

/*
 * From libjuniper
 */
#include <jnx/bits.h>
#include <jnx/mgmt_sock_pub.h>
#include <jnx/ms_parse.h>
#include <jnx/aux_types.h>
#include <jnx/ipc.h>
#include <jnx/patricia.h>
#include <jnx/radix.h>

/*
 * Other:
 */
#include <jnx/mpsdk.h>
#include <jnx/msp_trace.h>
#include <jnx/atomic.h>
#include <jnx/msp_locks.h>
#include <jnx/msp_rwlocks.h>
#include <jnx/msp_objcache.h>
#include <jnx/msp_policy_db.h>

#include "monitube2-data_logging.h"

/*** Constants ***/

/**
 * The plug-in's name
 */
#define PLUGIN_NAME "monitube2-plugin"

/**
 * The per-provider plug-in id
 */
#define PLUGIN_ID 1

/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/

#endif


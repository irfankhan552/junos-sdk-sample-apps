/*
 * $Id: common.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file common.h
 * 
 * @brief Common includes
 * 
 * A few common includes 
 */


#ifndef SYNC_COMMON_H_
#define SYNC_COMMON_H_

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
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
#include <jnx/trace.h>

/*
 * From ddl
 */
#include <ddl/ddl.h>

/*
 * From libjunos-sdk
 */
#include <jnx/junos_init.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>

/*
 * From libvrfutil
 */
#include <jnx/vrf_util_pub.h>

/*
 * common/shared
 */
#include <jnx/rt_shared_pub.h>

#endif

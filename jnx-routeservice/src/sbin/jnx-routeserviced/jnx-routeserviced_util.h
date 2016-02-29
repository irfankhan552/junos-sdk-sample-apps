/*
 * $Id: jnx-routeserviced_util.h 346460 2009-11-14 05:06:47Z ssiano $
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

#ifndef __JNX_ROUTESERVICED_UTIL_H___
#define __JNX_ROUTESERVICED_UTIL_H___

const char* jnx_routeservice_util_get_prefix_by_ifl (ifl_idx_t);
int jnx_routeservice_util_get_idx_by_iflname (const char *iflname, 
                                              ifl_idx_t *idx); 

#endif /* !__JNX_ROUTESERVICED_UTIL_H__ */

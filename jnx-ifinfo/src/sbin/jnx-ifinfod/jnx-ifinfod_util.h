/* $Id: jnx-ifinfod_util.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-ifinfod_util.h : jnx-ifinfod common utilities
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2005-2008, Juniper Networks, Inc.
 * All rights reserved.
 */


#ifndef __JNX_IFINFOD_UTIL_H__
#define __JNX_IFINFOD_UTIL_H__

typedef struct kcom_ifds {
        kcom_ifdev_t     *devptr;
        struct kcom_ifds *nextifd;
} kcom_ifds_t;


/* function declarations */
const char * ifinfod_get_ifl_name (kcom_ifl_t *);
int ifinfod_write (int, char*, int);
int get_all_ifds(kcom_ifdev_t *,void *);

kcom_ifds_t * ifinfod_get_ifd_all(void);
kcom_ifds_t * ifinfod_get_ifd_first(void);
kcom_ifds_t * ifinfod_get_ifd_next(kcom_ifds_t *);

#endif

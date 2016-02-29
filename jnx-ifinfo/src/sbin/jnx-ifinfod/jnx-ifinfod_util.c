/*
 * $Id: jnx-ifinfod_util.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 *jnx-ifinfod_util.c
 * This file has common utility functions needed by jnx-ifinfod functions
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


#include <isc/eventlib.h>
#include <jnx/bits.h>
#include <unistd.h>
#include <junoscript/xmlrpc.h>

#include <string.h>
#include <jnx/patricia.h>

#include <jnx/stats.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>
#include "jnx-ifinfod_util.h"
#include "jnx-ifinfod_kcom.h"

kcom_ifds_t *head;

/**
 *
 *Function : ifinfod_get_ifl_name
 *Purpose  : Given a pointer to the ifl structure, this utitlity returns
 *           a string which is the ifl nam. It concatenates the subunit 
 *           information to the actual interface name and returns the string
 *
 * @param[in] *kcom_ifl_t
 *       Pointer to kcom_ifl_t structure defined in libjunos-sdk
 *
 * @return Logical interface name  on success 
 */
const char *
ifinfod_get_ifl_name (kcom_ifl_t *ifl) {
    static char ifl_name[KCOM_IFNAMELEN];

    memset(ifl_name, 0, sizeof(ifl_name));
    snprintf(ifl_name, sizeof(ifl_name), "%s.%d", 
	     ifl->ifl_name, ifl->ifl_subunit);
    return ifl_name;
}

/**
 *
 *Function : ifinfod_write
 *Purpose  : This function writes the requested number of bytes to the 
 *           to a file descriptor. 
 *
 * @param[in] fd
 *       Integer file descriptor
 *
 * @param[in] *buf
 *       String buffer that needs to be written
 *
 * @param[in] bytes
 *       Integer number of bytes to be written
 *
 * @return bytes on success and -1 on failure
 */
int
ifinfod_write (int fd, char *buf, int bytes)
{
    int left;
    int written;
    char *mover = buf;

    left = bytes;
    while (left > 0 ) {
        if ((written = write(fd, mover, left)) < 0)
            return -1; /***Error***/

        left  -= written;
        mover += written;

    }
    return bytes;
}

/* Handler for ifinfod_get_ifd_all function*/
int
get_all_ifds (kcom_ifdev_t *dev, void *ifds_ptr __unused) 
{
    kcom_ifds_t *newifdev, *current_ptr;

    newifdev = calloc(1, sizeof(kcom_ifds_t));
    newifdev->devptr = dev;
    newifdev->nextifd = NULL;

    current_ptr= head;
    while (current_ptr->nextifd != NULL)
                current_ptr = current_ptr->nextifd;
    current_ptr->nextifd = newifdev;
    return 0;
}

/* Utility to return all ifds on the system */
kcom_ifds_t *
ifinfod_get_ifd_all (void)
{
    head = calloc(1, sizeof(kcom_ifds_t));
    junos_kcom_ifd_get_all(get_all_ifds, NULL, NULL);
    return head;
}


/* Utility to return next ifd on the patricia tree */
kcom_ifds_t *
ifinfod_get_ifd_next (kcom_ifds_t *ifinfod_node)
{
    if (ifinfod_node->nextifd) {
         return ifinfod_node->nextifd;
    } else {
         return NULL;
    }
}

/* Utility to return first ifd on the patricia tree */
kcom_ifds_t *
ifinfod_get_ifd_first (void)
{
 
    kcom_ifds_t *ifd_ptr = ifinfod_get_ifd_all();

    if (ifd_ptr->nextifd) {
        return ifd_ptr->nextifd;
    } else {
        return NULL;
    }
}

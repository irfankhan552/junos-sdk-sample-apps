/*
 * $Id: jnx-routeserviced_util.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file 
 *     jnx-routeserviced_util.c
 * @brief 
 *     Useful utility functions
 *
 * Contains some utility functions.
 */

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <jnx/junos_kcom.h>
#include <jnx/jnx_types.h>

#include "jnx-routeserviced_util.h"

/**
 * @brief
 *     Get primary address associated with logical interface
 *
 * Looks up the logical interface name and returns the IPv4 
 * address in ASCII format, if found. 
 *
 * @param[in] idx 
 *     Logical interface index 
 * 
 * @return 
 *     Pointer to IPv4 address; NULL if not found
 */

const char *
jnx_routeservice_util_get_prefix_by_ifl (ifl_idx_t idx) 
{
    kcom_ifa_t ifa;
    static char ifa_lprefix[KCOM_MAX_PREFIX_LEN];
    struct in_addr in;
    int error;
    
    /*
     * Invalid interface 
     */
    if (ifl_idx_t_getval(idx) == 0) {
        return NULL;
    }
    
    memset(ifa_lprefix, 0, sizeof(ifa_lprefix));

    /*
     * Get the local address associated with the interface
     */
    error = junos_kcom_ifa_get_first(idx, AF_INET, &ifa);
    if (error) {
        return NULL;
    }

    /*
     * Check if the local address length is non-zero
     */
    if (ifa.ifa_lplen && ifa.ifa_lprefix != NULL) {
        /*
         * Store the address in network byte order 
         */
        in.s_addr = *(u_long *) ifa.ifa_lprefix;

        /*
         * Convert the internet address in the ASCII '.' notation
         */
        strlcpy(ifa_lprefix, inet_ntoa(in), sizeof(ifa_lprefix));
        return ifa_lprefix;
    }

    return NULL;
}

/**
 * @brief
 *     Get primary address associated with logical interface
 *
 * Searches for the logical interface name and returns the IPv4 
 * address in ASCII, if found. 
 *
 * @note
 *     If the interface name doesnt contain a suffix of the subunit,
 *     then this routine assumes a subunit of 0
 *
 * @param[in] iflname 
 *     Pointer to the name of the logical interface
 * @param[out] idx
 *     IFL index which needs to be set
 * 
 * @return Status
 *     @li 0      success
 *     @li EINVAL Invalid arguments 
 *     @li ENOENT kcom returned error 
 */
int
jnx_routeservice_util_get_idx_by_iflname (const char* iflname, 
                                          ifl_idx_t *idx) 
{
    kcom_ifl_t ifl;
    char *ifl_name_str;
    char *ifl_subunit_str;
    char name[KCOM_IFNAMELEN];
    if_subunit_t subunit;
    int error = 0;
    
    if (iflname == NULL) {
        return EINVAL;
    }
    
    /*
     * Copy it locally
     */
    if(strlcpy(name, iflname, sizeof(name)) > sizeof(name)) {
        return EINVAL;
    }

    /*
     * Split the string at the first '.' occurence, if any
     */
    ifl_name_str = strtok(name, ".");
    
    /*
     * If interface name has a subunit passed with it, extract it
     */
    ifl_subunit_str = strchr(name, '.');
    if (ifl_subunit_str) {
        subunit = (if_subunit_t) strtol(ifl_subunit_str + 1, NULL, 0);
    } else {
        subunit = (if_subunit_t) 0;
    }

    /*
     * KCOM api to get the IFl associated with the logical
     * interface.  Note: 'ifl_name_str' does not contain the subunit. 
     * Subunit is explicitly provided as another argument (here second).
     */
    error = junos_kcom_ifl_get_by_name(ifl_name_str, subunit, &ifl);
    if (error != KCOM_OK) {
        return ENOENT;
    }
    
    if (!idx) {
        return EINVAL;
    }
    /*
     * Set the ifl index number
     */
    idx->x = ifl.ifl_index.x;
    return 0;
}

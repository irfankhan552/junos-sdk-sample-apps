/*
 * $Id: jnx-ifinfod_kcom.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-ifinfod_kcom.c - kcom init and message handler functions
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


/**
 *
 * @file ifinfod_kcom.c 
 * @brief Handles kernel communication initialization and message handler
 *        functions
 *
 */

#include <sys/types.h>
#include <jnx/trace.h>
#include <ddl/dtypes.h>

#include <jnx/provider_info.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>

#include JNX_IFINFOD_OUT_H

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>

#include "jnx-ifinfod_kcom.h"

#define KCOM_ID_IFINFOD 25


/**
 *
 *Function : ifinfod_ifd_msg_handler
 *Purpose  : This function handles async messages relating to IFDs.
 *           It logs any ifd async messages to ifinfod trace file.
 *
 * @param[in] ifdp
 *       pointer to kcom_ifdev_t structure 
 * @param[in] void *
 *       Any User specific information.In this example, this field is unused.
 *
 *
 * @return 0 on success or 1 on failure  
 */
static int
ifinfod_ifd_msg_handler (kcom_ifdev_t *ifdp,void *arg __unused)
{
    junos_trace(JNX_IFINFOD_TRACEFLAG_RTSOCK,
		"ifd Async message IFD: %s SNMP ID:%d",
		ifdp->ifdev_name,ifdp->ifdev_snmp_id);
    junos_kcom_msg_free(ifdp);
    return 0;
}

/**
 *
 *Function : ifinfod_ifl_nsg_handler
 *Purpose  : This function handles async messages relating to IFLs.
 *            It logs any ifl async messages to ifinfod trace file.
 *
 * @param[in] iflp
 *       pointer to kcom_ifl_t structure
 * @param[in] void *
 *       Any User specific information.In this example, this field is unused.
 *
 *
 * @return 0 on success or 1 on failure
 */
static int
ifinfod_ifl_msg_handler (kcom_ifl_t *iflp, void *arg __unused)
{
    junos_trace(JNX_IFINFOD_TRACEFLAG_RTSOCK,
		"ifl Async message IFL: %s SNMP ID:%d", 
		iflp->ifl_name, iflp->ifl_snmp_id);
    junos_kcom_msg_free(iflp);
    return 0;
}


/**
 *
 *Function :ifinfod_ifa_msg_handler
 *Purpose  :This function handles async messages relating to 
 *          Interface Addresses (IFAs). It logs any ifa 
 *          async messages to ifinfod trace file.
 * @param[in] ifap
 *       pointer to kcom_ifa_t structure
 * @param[in] void *
 *       Any User specific information.In this example, 
 *       this field is unused.
 *
 *
 * @return 0 on success or 1 on failure
 */
static int
ifinfod_ifa_msg_handler (kcom_ifa_t *ifap, void *arg __unused)
{
    junos_trace(JNX_IFINFOD_TRACEFLAG_RTSOCK,
		"ifa Async message IFA: %s AF:%d",
		ifap->ifa_name, ifap->ifa_af);
    junos_kcom_msg_free(ifap);
    return 0;
}  

/**
 *
 *Function : ifinfod_iff_msg_handler
 *Purpose  : This function  handles iff async messages.
 *           It logs this information to ifinfo trace file
 * @param[in] iffp
 *       pointer to kcom_iff_t structure
 * @param[in] void *
 *       Any User specific information.In this example, this field is unused.
 *
 *
 * @return 0 on success or 1 on failure
 */
static int
ifinfod_iff_msg_handler (kcom_iff_t *iffp, void *arg __unused)
{
    junos_trace(JNX_IFINFOD_TRACEFLAG_RTSOCK, 
		"iff Async message IFF: %s MTU :%d", 
		iffp->iff_name, iffp->iff_mtu);
    junos_kcom_msg_free(iffp);
    return 0;
} 


/**
 *
 *Function : ifinfod_create_socket
 *Purpose  : This function creates a UDP socket and verifies port binding  
 *           to the portnumber provided
 * @param[in] portnum
 *       port number to which the UDP socket needs to bind
 *
 */
void 
ifinfod_create_socket (int portnum)
{
    struct sockaddr_in sock;
    int sd;

     /* Create a DGRAM socket and try to bind to a lower port */
    sd    = socket(AF_INET, SOCK_DGRAM, 0);

    sock.sin_port = htons(portnum);
    sock.sin_family = AF_INET;
    sock.sin_addr.s_addr = htonl(INADDR_ANY);
   
    if (bind(sd, (struct sockaddr *)&sock, sizeof(sock)) == -1) {

        SYSLOG(JNX_IFINFOD_PORT_TAG,LOG_INFO, 
	       "%d ifinfod socket binding failure due to %s",
	       portnum, strerror(errno));


        /* Binding failure might happen when a SDK application is trying to */
        /* use a lower port number which is normally not allowed  */

        if (portnum == LOW_PORT) {
            ERRMSG(JNX_IFINFOD_LOW_PORT,LOG_ERR,
		   "%d ifinfod socket binding failure to a lower port due to %s",
		   portnum, strerror(errno)); 
         }
        return;
    }
    SYSLOG(JNX_IFINFOD_PORT_TAG,LOG_INFO,"%d ifinfod socket binding %s",
	   portnum,"succeeded");

        if (portnum == LOW_PORT) {
            SYSLOG(JNX_IFINFOD_LOW_PORT,LOG_INFO,
		   "%d ifinfod socket binding %s",portnum, "succeeded");
        } else {
            SYSLOG(JNX_IFINFOD_HIGH_PORT,LOG_INFO,
		   "%d ifinfod socket binding %s",portnum, "succeeded");
        }
    /* No listen or accept calls on a UDP socket close it */
    close(sd);
}

/**
 *
 *Function : ifinfod_kcom_init
 *Purpose  : This function initializes kcom subsystem and registers
 *           ifl/ifd/ifa and iff handlers 
 * @param[in] ctxt
 *       Event context
 *
 *
 * @return 0 on success or 1 on failure
 */
int 
ifinfod_kcom_init (evContext ctxt)
{
    int error;
    provider_origin_id_t origin_id;

    error = provider_info_get_origin_id(&origin_id);
    
    if (error) {
        ERRMSG(JNX_IFINFOD_LOW_PORT, TRACE_LOG_ERR,
            "%s: Retrieving origin ID failed: %m", __func__);
        return error;
    }
    
    error = junos_kcom_init(origin_id, ctxt);
    INSIST(error == KCOM_OK);

    ERRMSG(JNX_IFINFOD_LOW_PORT,LOG_INFO,"%s ifinfod kcom init %s",
	   __func__,"start");

    /*
     * start registering handlers for asynchronous messages 
     * for any updates on iff/ifd/ifl/ifa 
     */
    error = junos_kcom_register_ifd_handler(NULL, ifinfod_ifd_msg_handler);

    if (error) {
        junos_trace(JNX_IFINFOD_TRACEFLAG_RTSOCK,
		    "Unable to register ifd handler , error %d\n", error);
    }
    error = junos_kcom_register_ifl_handler(NULL, ifinfod_ifl_msg_handler);
    
    if (error) {
        junos_trace(JNX_IFINFOD_TRACEFLAG_RTSOCK,
		    "Unable to register ifl handler , error %d\n", error);
    }
    error = junos_kcom_register_ifa_handler(NULL, ifinfod_ifa_msg_handler);

    if (error) {
        junos_trace(JNX_IFINFOD_TRACEFLAG_RTSOCK,
		    "Unable to register ifa handler , error %d\n", error);
    }
    error = junos_kcom_register_iff_handler(NULL, ifinfod_iff_msg_handler);

    if (error) {
        junos_trace(JNX_IFINFOD_TRACEFLAG_RTSOCK,
		    "Unable to register iff handler , error %d\n", error);
    }
    return 0;
}

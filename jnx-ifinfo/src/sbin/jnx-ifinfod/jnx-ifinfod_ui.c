/*
 * $Id: jnx-ifinfod_ui.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 *jnx-ifinfod_ui.c
 * This file handles all the cli commands indicated in 
 * lib/ddl/input/ifinfod.cmd.dd
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

#include <sys/bitstring.h>

#include <isc/eventlib.h>
#include <jnx/bits.h>
#include <errno.h>

#include <jnx/junos_init.h>
#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>

#include <string.h>
#include <jnx/patricia.h>

#include <jnx/stats.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>
#include <jnx/junos_stat.h>
#include <jnx/if_pub_pan.h>

#include JNX_IFINFOD_ODL_H
#include JNX_IFINFOD_OUT_H

#include "jnx-ifinfod_config.h"
#include "jnx-ifinfod_util.h"
#include "jnx-ifinfod_kcom.h"
#include "jnx-ifinfod_conn.h"

#define LEVEL_SUMMARY   0
#define LEVEL_BRIEF     1
#define LEVEL_DETAIL    2
#define LEVEL_EXTENSIVE 3 

#define STATE_DOWN 4
#define STATE_UP 5




/**
 * ifl_format_ structure holds the 
 * management socket pointer and a integer variable
 * which holds the granularity level in most cases
 * 
 */
typedef struct ifl_format_ {
    mgmt_sock_t *msp;
    int         level;
} ifl_format_t;

typedef struct ifl_format_ * ifl_fmt_ptr;

int show_ifinfod_interfaces (int, char *, ifl_fmt_ptr);

/**
 *
 *Function : show_ifinfod_aliases
 *Purpose  : This function is invoked when 'show ifinfo interface aliases'
 *           is issued on the cli. It returns all configured aliases and
 *           corresponding  interfaces. This function also tests the socket
 *           APIs, it tries to create a UDP socket and bind to a lower # port
 *           and a higer numbered port.
 *
 * @param[in] *msp
 *       Pointer to management socket that communicates to the cli
 *
 * @param[in] *parse_status_t
 *       Pointer to  parse status structure.This is unused in this case
 *
 * @param[in] *unparsed
 *       Pointer to unparsed string.This is ununsed in this function
 *
 * @return 0 on success and 1 on failure 
 */
static int
show_ifinfod_aliases (mgmt_sock_t *msp, parse_status_t *csb __unused,
		      char *unparsed __unused)
{
    ifinfod_intf_t *ifinfod_node;

    ifinfod_create_socket(LOW_PORT);
    ifinfod_create_socket(HIGH_PORT);

    XML_SUBTREE_OPEN(msp, ODCI_JNX_IFINFO_INTERFACES_ALIASES);

    for (ifinfod_node = ifinfod_node_first(); ifinfod_node ;
	 ifinfod_node = ifinfod_node_next(ifinfod_node)) {

        XML_OPEN(msp, ODCI_JNX_IFINFO_ALIASES);
        XML_ELT(msp, ODCI_JNX_IFINFO_INTERFACE_NAME,
		"%s", ifinfod_node->intf_name);

        switch (ifinfod_node->intf_status) {
            case 0 :
                XML_ELT(msp, ODCI_JNX_IFINFO_INTERFACE_STATUS, "%s", "DOWN");
                break;
            case 1 :
                XML_ELT(msp, ODCI_JNX_IFINFO_INTERFACE_STATUS, "%s", "UP");
                break;
            default :
                XML_ELT(msp, ODCI_JNX_IFINFO_INTERFACE_STATUS, "%s", "UNKOWN");
                break;
        }

        XML_ELT(msp, ODCI_JNX_IFINFO_INTERFACE_ALIAS,
		"%s", ifinfod_node->intf_alias_name);
        XML_CLOSE(msp, ODCI_JNX_IFINFO_ALIASES);
    }

        XML_CLOSE(msp, ODCI_JNX_IFINFO_INTERFACES_ALIASES);
   
  return 0;
}
   

/**
 *
 *Function : show_ifinfod_version
 *Purpose  : This function returns xmlized show version output to the cli
 *           based on the granularity level requested by the user(default
 *           brief)
 * @param[in] *msp
 *       Pointer to management socket that communicates to the cli
 *
 * @param[in] *parse_status_t
 *       Pointer to  parse status structure which contains the sub arguments
 *       like the granularity level in this case. 
 *
 * @param[in] *unparsed
 *       Pointer to unparsed string.This is ununsed in this function
 *
 * @return 0 on success and 1 on failure
 */
static int
show_ifinfod_version (mgmt_sock_t *msp, parse_status_t *csb,
		      char *unnparsed __unused)
{
    int granularity_level =  ms_parse_get_subcode(csb);

    if (granularity_level == LEVEL_BRIEF) {
        xml_show_version(msp, NULL, FALSE);
    } else if (granularity_level == LEVEL_DETAIL) {
        xml_show_version(msp, NULL, TRUE);
    }
  return 0;
}


/**
 *
 *Function : ifinfod_print_ifl_summary
 *Purpose  : This function returns ifl summary information to the cli.
 *           It Opens Interface Info subtree and displays ifl name,
 *           index, snmp id and the status to the cli. 
 *
 *
 * @param[in] *kcom_ifl_t
 *       Pointer to kcom_ifl_t structure defined in libjunos-sdk
 *
 * @param[in] *iflfmtptr
 *       Pointer to flfmtptr structure. This structure holds the msp
 *       and granularity level if any defined.
 *
 * @return 0 on success and 1 on failure
 */
static int
ifinfod_print_ifl_summary (kcom_ifl_t *ifl, void *iflfmtptr)
{
    ifl_fmt_ptr  iflptr = iflfmtptr;
    mgmt_sock_t *mmsp = iflptr->msp;
 
   XML_OPEN(mmsp, ODCI_JNX_IFINFO_INTERFACES);
   if (iflptr->level == STATE_DOWN) {
       if (junos_kcom_ifl_down(ifl)) {
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_NAME,
		   "%s", ifinfod_get_ifl_name(ifl));
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_INDEX,
		   "%d", ifl->ifl_index);
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_SNMP_INDEX,
		   "%d", ifl->ifl_snmp_id);
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS,
		   "%s", "Down");
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS,
		   "%s", "Down");
        }
    }
    else if (iflptr->level == STATE_UP) {

       if (!(junos_kcom_ifl_down(ifl))) {

           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_NAME,
		   "%s", ifinfod_get_ifl_name(ifl));
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_INDEX,
		   "%d", ifl->ifl_index);
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_SNMP_INDEX,
		   "%d", ifl->ifl_snmp_id);
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS,
		   "%s", "Up");
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS,
		   "%s",  "Up");
       }

    } else { /* dump everything for rest of the granularities */

           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_NAME,
		   "%s", ifinfod_get_ifl_name(ifl));
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_INDEX, 
		   "%d", ifl->ifl_index);
           XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_SNMP_INDEX,
		   "%d", ifl->ifl_snmp_id);
           if (junos_kcom_ifl_down(ifl)) {

               XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS,
		       "%s", "Down");
            } else {
               XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS,
		       "%s", "Up");
            }
            if (junos_kcom_ifl_down(ifl)) {
               XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS,
		       "%s", "Down");
            } else {
               XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS,
		       "%s", "Up");
            }
    }

    junos_kcom_msg_free(ifl);
    XML_CLOSE(mmsp, ODCI_JNX_IFINFO_INTERFACES);

    return 0;
}

/**
 *
 * Function : ifinfod_print_ifd
 * Purpose  : This function returns ifd information to the cli.
 *           It Opens Interface Info subtree and displays ifd name,
 *           index, snmp id and the status to the cli.
 *
 * @param[in] *kcom_ifdev_t
 *       Pointer to kcom_ifdev_t structure defined in libjunos-sdk
 *
 * @param[in] *iflfmtptr
 *       Pointer to iflfmtptr structure. This structure holds the msp
 *       and granularity level if any defined.
 *
 * @return 0 on success and 1 on failure
 */
static int
ifinfod_print_ifd_summary (kcom_ifdev_t *ifd, void *iflfmtptr)
{
    ifl_fmt_ptr  iflptr = iflfmtptr;
    mgmt_sock_t *mmsp = iflptr->msp;
          

    XML_OPEN(mmsp, ODCI_JNX_IFINFO_INTERFACES);

    if (iflptr->level == STATE_DOWN) {
        if  (junos_kcom_ifd_down(ifd)) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_NAME, "%s", ifd->ifdev_name);
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_INDEX,
		    "%d", ifd->ifdev_index);
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_SNMP_INDEX,
		    "%d", ifd->ifdev_snmp_id);
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS,
		    "%s", "Down"); 
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS, "%s", "Down");
        }

    }
    else if (iflptr->level == STATE_UP) { 
        if (!junos_kcom_ifd_down(ifd)) {
          XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_NAME, "%s", ifd->ifdev_name);
          XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_INDEX,
		  "%d", ifd->ifdev_index);
          XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_SNMP_INDEX,
		  "%d", ifd->ifdev_snmp_id);
          XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS, "%s", "Up");
          XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS, "%s", "Up");
        }
    } else { /* dump everything */

        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_NAME, "%s", ifd->ifdev_name);
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_INDEX, "%d", ifd->ifdev_index);
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_SNMP_INDEX,
		"%d", ifd->ifdev_snmp_id);
           if (junos_kcom_ifd_down(ifd)) {
               XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS,
		       "%s","Down");
               XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS,
		       "%s", "Down");
           } else {
               XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS,
		       "%s","Up");
               XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS,
		       "%s", "Up");
           }
    }
   junos_kcom_ifl_get_all(ifinfod_print_ifl_summary, ifd->ifdev_name, iflptr);
   junos_kcom_msg_free(ifd);
   XML_CLOSE(mmsp, ODCI_JNX_IFINFO_INTERFACES);

   return 0;
}

/**
 *
 *Function : ifinfod_print_ifa
 *Purpose  : This function returns ifa information to the cli.
 *           It Opens Interface Info subtree and displays ifa flags,
 *           destination prefix, local prefix and broadcast prefix
 *           to the cli.
 *
 *
 * @param[in] *kcom_ifa_t
 *       Pointer to kcom_ifa_t structure defined in libjunos-sdk
 *
 * @param[in] *iflfmtptr
 *       Pointer to iflfmtptr structure. This structure holds the msp
 *       and granularity level if any defined.
 *
 * @return 0 on success and 1 on failure
 */
static int
ifinfod_print_ifa (kcom_ifa_t *ifa, void *iflfmt)
{
    ifl_fmt_ptr  iflptr = iflfmt;

    XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFA_FLAGS,
	    "%d", ifa->ifa_flags);
    if (ifa->ifa_dplen) {
        XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFA_DEST,
		"%d", ifa->ifa_dprefix);
     }
    if (ifa->ifa_lplen) {
        XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFA_LOCAL,
		"%d", ifa->ifa_lprefix);
    }
    if (ifa->ifa_bplen) {
        XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFA_BCAST,
		"%d", ifa->ifa_bprefix);
    }
    junos_kcom_msg_free(ifa);
    return 0;
}


/**
 *
 *Function : ifinfod_print_iff
 *Purpose  : This function returns iff information to the cli.   
 *           It Opens Logical Interface Info subtree and displays 
 *           iff flags, Address family, MTU and table information to
 *           the cli.
 *
 *
 * @param[in] *kcom_iff_t
 *       Pointer to kcom_iff_t structure defined in libjunos-sdk
 *
 * @param[in] *iflfmtptr   
 *       Pointer to iflfmtptr structure. This structure holds the msp
 *       and granularity level if any defined.
 *
 * @return 0 on success and 1 on failure
 */
static int
ifinfod_print_iff (kcom_iff_t *iff, void *iflfmt)
{  
    ifl_fmt_ptr  iflptr = iflfmt;

    XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFF_AF, "%d", iff->iff_af);
    XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFF_MTU, "%d", iff->iff_mtu);
    XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFF_TABLE,
	    "%d", iff->iff_table);
    XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFF_FLAGS, 
	    "%d", iff->iff_flags);
    junos_kcom_ifa_get_all(ifinfod_print_ifa, iff->iff_index,
			   iff->iff_af, iflptr);
    junos_kcom_msg_free(iff);
    return 0;
}

/**
 *
 *Function : ifinfod_print_ifl
 *Purpose  : This function returns ifl information to the cli.
 *           It Opens Logical Interface Info subtree and displays
 *           ifl name,index,snmp ID, status, bandwidth information to
 *           the cli.
 *
 *
 * @param[in] *kcom_ifl_t
 *       Pointer to kcom_ifl_t structure defined in libjunos-sdk
 *
 * @param[in] *iflfmtptr
 *       Pointer to iflfmtptr structure. This structure holds the msp
 *       and granularity level if any defined.
 *
 * @return 0 on success and 1 on failure
 */
static int
ifinfod_print_ifl (kcom_ifl_t *ifl, void *iflfmt)
{
    ifl_fmt_ptr  iflptr = iflfmt;
    stat_if_traff_t ifl_tr;
   
    /*
     * Better to avoid any junk 
     */
    bzero(&ifl_tr, sizeof(stat_if_traff_t));

    XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFL_NAME, "%s.%d",
	    ifl->ifl_name, ifl->ifl_subunit);
    XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFL_INDEX,
	    "%d", ifl->ifl_index);
    XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFL_SNMP_INDEX,
	    "%d", ifl->ifl_snmp_id);
    if (junos_kcom_ifl_down(ifl)) {
            XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFL_STATUS,
		    "%s", "Down");
    } else {
            XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFL_STATUS,
		    "%s", "Up");
    }
    XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFL_BW,
	    "%d bps", ifl->ifl_bandwidth);
    if (iflptr->level == LEVEL_DETAIL) {
        XML_ELT(iflptr->msp, ODCI_JNX_IFINFO_INTERFACE_IFL_FLAGS,
		"%d", ifl->ifl_flags);
        junos_kcom_iff_get_all(ifinfod_print_iff, ifl->ifl_index, iflptr);
    }

    /*
     * Print traffic statistics if there is no error returned 
     */
    if (junos_stat_ifl_get_local_traff_by_index(ifl->ifl_index, &ifl_tr) == 0)
    {
        XML_ELT(iflptr->msp,  ODCI_JNX_IFINFO_INTERFACE_IFL_STAT_TRAFF_IBYTES,
                "%d", ifl_tr.ibytes);
        XML_ELT(iflptr->msp,  
                ODCI_JNX_IFINFO_INTERFACE_IFL_STAT_TRAFF_IBYTES_RATE,
                "%qu", ifl_tr.ibytes_1sec);
        XML_ELT(iflptr->msp,  ODCI_JNX_IFINFO_INTERFACE_IFL_STAT_TRAFF_IPKTS, 
                "%qu", ifl_tr.ipackets);
        XML_ELT(iflptr->msp,  
                ODCI_JNX_IFINFO_INTERFACE_IFL_STAT_TRAFF_IPKTS_RATE, 
                "%qu", ifl_tr.ipackets_1sec);
        XML_ELT(iflptr->msp,  ODCI_JNX_IFINFO_INTERFACE_IFL_STAT_TRAFF_OBYTES,
                "%qu", ifl_tr.obytes);
        XML_ELT(iflptr->msp,  
                ODCI_JNX_IFINFO_INTERFACE_IFL_STAT_TRAFF_OBYTES_RATE,
                "%qu", ifl_tr.obytes_1sec);
        XML_ELT(iflptr->msp,  ODCI_JNX_IFINFO_INTERFACE_IFL_STAT_TRAFF_OPKTS, 
                "%qu", ifl_tr.opackets);
        XML_ELT(iflptr->msp,  
                ODCI_JNX_IFINFO_INTERFACE_IFL_STAT_TRAFF_OPKTS_RATE, 
                "%qu", ifl_tr.opackets_1sec);
    }

    junos_kcom_msg_free(ifl);
    return 0;
}

/**
 *
 *Function : ifinfod_print_ifd_brief
 *Purpose  : This function returns ifd information to the cli in breif format.
 *           It Opens Interface Info subtree and displays ifd name,
 *           index,snmp ID and logical interface information to the cli.
 *   
 * @param[in] *kcom_ifdev_t
 *       Pointer to kcom_ifdev_t structure defined in libjunos-sdk
 *
 * @param[in] *iflfmtptr
 *       Pointer to iflfmtptr structure. This structure holds the msp
 *       and granularity level if any defined.
 *
 * @return 0 on success and 1 on failure
 */
static int
ifinfod_print_ifd_brief (kcom_ifdev_t *ifd, void *iflfmtptr)
{
    ifl_fmt_ptr  iflptr = iflfmtptr;
    mgmt_sock_t *mmsp = iflptr->msp;

    XML_OPEN(mmsp, ODCI_JNX_IFINFO_INTERFACES);
    XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_NAME, "%s", ifd->ifdev_name);

    if (junos_kcom_ifd_down(ifd)) {
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS, "%s", "Down");
    } else {
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS, "%s", "Enabled");
    }
    if (junos_kcom_ifd_down(ifd)) {
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS, "%s", "Down");
    } else {
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS, "%s", "Up");
    }
    XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_INDEX, "%d", ifd->ifdev_index);
    XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_SNMP_INDEX,
	    "%d", ifd->ifdev_snmp_id);

    junos_kcom_ifl_get_all(ifinfod_print_ifl, ifd->ifdev_name, iflptr);
    junos_kcom_msg_free(ifd);
    XML_CLOSE(mmsp, ODCI_JNX_IFINFO_INTERFACES);
    return 0;
}


/**
 *
 *Function : ifinfod_print_ifd_detail
 *Purpose  : This function returns ifd information to the cli in detail format.
 *           It Opens Interface Info subtree and displays ifd name,
 *           index,snmp ID,mtu,speed and logical interface information
 *           to the cli.
 *
 *  
 * @param[in] *kcom_ifdev_t
 *       Pointer to kcom_ifdev_t structure defined in libjunos-sdk
 *
 * @param[in] *iflfmtptr
 *       Pointer to iflfmtptr structure. This structure holds the msp
 *       and granularity level if any defined.
 *
 * @return 0 on success and 1 on failure
 */
static int
ifinfod_print_ifd_detail (kcom_ifdev_t *ifd, void *iflfmtptr)
{
    ifl_fmt_ptr  iflptr = iflfmtptr;
    mgmt_sock_t *mmsp = iflptr->msp;
    stat_if_traff_t ifd_tr;
    stat_ifd_error_t ifd_err;

    /*
     * Avoid junk
     */
    bzero(&ifd_tr, sizeof(stat_if_traff_t));
    bzero(&ifd_err, sizeof(stat_ifd_error_t));
    
    iflptr->level = LEVEL_DETAIL;
    XML_OPEN(mmsp, ODCI_JNX_IFINFO_INTERFACES);
    XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_NAME, "%s", ifd->ifdev_name);

    if (junos_kcom_ifd_down(ifd)) {
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS,
		"%s", "Administratively down");
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS,
		"%s", "Down");
    } else {
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_ADMIN_STATUS, "%s", "Enabled");
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK_STATUS, "%s", "Up");
    }

    XML_ELT(mmsp,  ODCI_JNX_IFINFO_INTERFACE_INDEX,  "%d", ifd->ifdev_index);
    XML_ELT(mmsp,  ODCI_JNX_IFINFO_INTERFACE_SNMP_INDEX,
	    "%d", ifd->ifdev_snmp_id);
    XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_TYPE, "%d", ifd->ifdev_media_type);
    XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_LINK, "%d", ifd->ifdev_media_link);
    XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_MTU, "%d", ifd->ifdev_media_mtu);
    XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_SPEED,
	    "%d", ifd->ifdev_media_speed);
    XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_DEV_FLAGS,
	    "%d", ifd->ifdev_devflags);

    /*
     * Print traffic statistics for physical interfaces
     */
    if (junos_stat_ifd_get_traff_by_name(ifd->ifdev_name, &ifd_tr) == 0) {
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_TRAFF_IBYTES, "%qu", 
                ifd_tr.ibytes);
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_TRAFF_IBYTES_RATE, "%qu",
                ifd_tr.ibytes_1sec);
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_TRAFF_IPKTS, "%qu", 
                ifd_tr.ipackets);
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_TRAFF_IPKTS_RATE, "%qu", 
                ifd_tr.ipackets_1sec);
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_TRAFF_OBYTES, "%qu", 
                ifd_tr.obytes);
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_TRAFF_OBYTES_RATE, "%qu",
                ifd_tr.obytes_1sec);
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_TRAFF_OPKTS, "%qu", 
                ifd_tr.opackets);
        XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_TRAFF_OPKTS_RATE, "%qu", 
                ifd_tr.opackets_1sec);
    }

    /*
     * Print the error statistics
     */
    if (junos_stat_ifd_get_error_by_name(ifd->ifdev_name, &ifd_err) == 0) {
        /*
         * Input error statistics
         */
        if (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), igeneric)) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_IERR_ERRORS,
                    "%qu", ifd_err.igeneric);
        }
        if (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), iqdrops)) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_IERR_DROPS,
                    "%qu", ifd_err.iqdrops);
        }
        if (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), iframe)) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_IERR_FRAMING_ERRORS,
                    "%qu", ifd_err.iframe);
        }
        if (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), irunts)) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_IERR_RUNTS,
                    "%qu", ifd_err.irunts);
        }
        if (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), igiants)) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_IERR_GIANTS,
                    "%qu", ifd_err.igiants);
        }
        if (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), idiscards)) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_IERR_POLICED_DISCARDS,
                    "%qu", ifd_err.idiscards);
        }
	if ((ifd->ifdev_media_type != IFMT_SONET) &&
            (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), iresource))) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_IERR_RESOURCE_ERRORS,
                    "%qu", ifd_err.iresource);
        }

        /*
         * Ouput error statistics
         */
        if (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), carriertrans)) {
            XML_ELT(mmsp, 
                    ODCI_JNX_IFINFO_INTERFACE_STAT_OERR_CARRIER_TRANSITIONS,
                    "%qu", ifd_err.carriertrans);
        }
        if (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), ogeneric)) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_OERR_ERRORS,
                    "%qu", ifd_err.ogeneric);
        }
        if (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), oqdrops)) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_OERR_DROPS,
                    "%qu", ifd_err.oqdrops);
        }
        if (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), mtu)) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_OERR_MTU_ERRORS,
                    "%qu", ifd_err.mtu);
        }
	if ((ifd->ifdev_media_type != IFMT_SONET) &&
            (STAT_IFD_ERR_FLD_IS_VALID((&ifd_err), oresource))) {
            XML_ELT(mmsp, ODCI_JNX_IFINFO_INTERFACE_STAT_OERR_RESOURCE_ERRORS,
                    "%qu", ifd_err.oresource);
        }
    }

    junos_kcom_ifl_get_all(ifinfod_print_ifl, ifd->ifdev_name, iflptr);
    junos_kcom_msg_free(ifd);
    XML_CLOSE(mmsp, ODCI_JNX_IFINFO_INTERFACES);

   return 0;
}


/**
 * 
 *Function : show_ifinfod_interfaces
 *Purpose  : This function returns ifd information to the cli. 
 *           Based on the granularity level, it calls the appropriate
 *           cli function to display information to users.
 *  
 *
 * @param[in] granularity
 *       Integer which indicates the level of detail needed by user
 *
 * @param[in] *prefix
 *       Pointer to unparsed string if a certain interface or alias
 *       information was requested in specific. Default is to display 
 *       information regarding all the interfaces on the system.
 *
 * @param[in] *iflfmtptr
 *       Pointer to iflfmtptr structure. This structure holds the msp
 *       and granularity level if any defined.
 *
 * @return 0 on success and -1 on failure
 */
int
show_ifinfod_interfaces (int granularity, char *prefix, ifl_fmt_ptr iflptr)
{
        if (granularity == LEVEL_SUMMARY) {
            junos_kcom_ifd_get_all(ifinfod_print_ifd_summary, prefix, iflptr);
        } else if (granularity == LEVEL_BRIEF) { /* level brief */
            junos_kcom_ifd_get_all(ifinfod_print_ifd_brief, prefix, iflptr);
        } else { /* level detail */
            /*
             * Initialize the statistics session only when user requests
             * to print a detailed interface information
             */
            if (junos_stat_session_init() != 0) {
            /*
             * If the statistics session fails to initialize, return 
             * because there is an unexpected error. 
             */
                return -1;
            }

            junos_kcom_ifd_get_all(ifinfod_print_ifd_detail, prefix, iflptr);

            /*
             * Close the session whence done with all interfaces
             */
            junos_stat_session_shutdown();
        }

        return 0;
}

/**
 *
 *Function : show_ifinfod_data
 *Purpose  : This function returns interface information to the cli,
 *           based on the granularity level.
 * 
 *
 * @param[in] *msp
 *       Pointer to management socket 
 *
 * @param[in] *csb
 *       Pointer to parse_status_t structure
 *
 * @param[in] *unparsed
 *       Pointer to unparsed string that might hold the alias or interface
 *       name.
 *
 * @return 0 on success and 1 on failure
 */
static int 
show_ifinfod_data(mgmt_sock_t *msp, parse_status_t *csb, char *unparsed)
{
    int granularity_level =  ms_parse_get_subcode(csb);
    const char *display_style;
    ifl_fmt_ptr  iflptr;
    kcom_ifdev_t *intf_ifdev;
    ifinfod_intf_t *ifdptr = NULL;
    int node_count = 0;
     
   
    iflptr = (ifl_fmt_ptr) malloc(sizeof(ifl_format_t));
    iflptr->msp = msp;
 
    intf_ifdev = calloc(1, sizeof(kcom_ifdev_t)); 

    if (granularity_level == LEVEL_SUMMARY) {
        iflptr->level = LEVEL_SUMMARY;
        display_style = 
	    xml_attr_style(JNX_IFINFO_INTERFACES_INFORMATION_STYLE_SUMMARY);
    } else if (granularity_level == LEVEL_BRIEF) {
        iflptr->level = LEVEL_BRIEF;
               display_style = 
		  xml_attr_style(JNX_IFINFO_INTERFACES_INFORMATION_STYLE_BRIEF);
    } else {
        iflptr->level = LEVEL_DETAIL;
        display_style = 
	    xml_attr_style(JNX_IFINFO_INTERFACES_INFORMATION_STYLE_DETAIL);
    }


    XML_SUBTREE_OPEN2(msp, ODCI_JNX_IFINFO_INTERFACES_INFORMATION,
		      "%s %s", xml_attr_xmlns(XML_NS), display_style);


    /*
     * if (unparsed) is a valid interface name call show_ifinfod_interfaces
     * directly
     * else lookup and call this for all nodes that match alias name
     */ 

    if ((junos_kcom_ifd_get_by_name(unparsed, intf_ifdev) == KCOM_OK) 
	|| (!unparsed)) {

        if (show_ifinfod_interfaces(granularity_level, unparsed, iflptr) != 0)
        {
            ERRMSG(JNX_IFINFOD_STAT_MSG, LOG_ERR,
                   "ifinfod failed to initialize the statistics session due "
                   "to %s", strerror(errno)); 
        }
        node_count++;

    } else {
        /* lookup alias tree structure and call for each matching node */

        for (ifdptr = ifinfod_node_first(); ifdptr;
	     ifdptr= ifinfod_node_next(ifdptr)) {
            if (memcmp(unparsed, ifdptr->intf_alias_name,
		       strlen(unparsed) + 1) == 0) {
                node_count++;
                if (show_ifinfod_interfaces(granularity_level,
					ifdptr->intf_name, iflptr) != 0) {
                    ERRMSG(JNX_IFINFOD_STAT_MSG, LOG_ERR,
                           "ifinfod failed to initialize the statistics "
                           "session due to %s", strerror(errno)); 
                }
            }
        }
    }

    junos_trace(JNX_IFINFOD_TRACEFLAG_CONFIG, "%s found %d nodes in lookup",
		__func__, node_count);
    XML_CLOSE(msp, ODCI_JNX_IFINFO_INTERFACES_INFORMATION);
    free(iflptr);
    return 0;
}


/**
 *
 *Function : show_ifinfod_status
 *Purpose  : This function returns interface information to the cli,
 *           based on the interface status requested
 *
 * @param[in] *msp
 *       Pointer to management socket
 *
 * @param[in] *csb
 *       Pointer to parse_status_t structure which contains sub arguments of
 *       the command.  
 *  
 * @param[in] *unparsed
 *       Pointer to unparsed string that might holds the alias or interface
 *       name if any requested by the cli user.
 *
 * @return 0 on success and 1 on failure
 */
static int 
show_ifinfod_status (mgmt_sock_t *mspi , parse_status_t *csb ,char *unparsed)
{
    int status =  ms_parse_get_subcode(csb);
    ifl_fmt_ptr  iflptr;
    const char *display_style;

    iflptr = (ifl_fmt_ptr) malloc(sizeof(ifl_format_t));
    iflptr->msp = mspi;

    display_style = 
	xml_attr_style(JNX_IFINFO_INTERFACES_INFORMATION_STYLE_SUMMARY);

    XML_SUBTREE_OPEN2(mspi, ODCI_JNX_IFINFO_INTERFACES_INFORMATION, 
		      "%s %s", xml_attr_xmlns(XML_NS), display_style);


    if (status == STATE_UP) {
        /*
         * Get all ifds, for each ifd that is admin and link up, get the 
         * ifls that are marked both admin and link up 
         */
         iflptr->level =  STATE_UP;
         junos_kcom_ifd_get_all(ifinfod_print_ifd_summary, unparsed,iflptr);
 
    } else {
        iflptr->level =  STATE_DOWN;
        junos_kcom_ifd_get_all(ifinfod_print_ifd_summary, unparsed, iflptr);
    }

    XML_CLOSE(mspi, ODCI_JNX_IFINFO_INTERFACES_INFORMATION);
    free(iflptr);
    return 0;
}
/******************************************************************************
 *                        Menu Structure  
 *****************************************************************************/

/* show ...*/
static const parse_menu_t show_ifinfod_interface_menu[] = {
    { "summary",     NULL,   LEVEL_SUMMARY,  NULL,    show_ifinfod_data},
    { "brief",       NULL,   LEVEL_BRIEF,    NULL,    show_ifinfod_data},
    { "detail",      NULL,   LEVEL_DETAIL,   NULL,    show_ifinfod_data},
    { "up",          NULL,   STATE_UP,       NULL,    show_ifinfod_status},
    { "down",        NULL,   STATE_DOWN,     NULL,    show_ifinfod_status},
    { "aliases",     NULL,   0,              NULL,    show_ifinfod_aliases},
    { NULL,          NULL,   0,              NULL,    NULL}
};
static const parse_menu_t show_ifinfod_version_menu[] = {
    { "brief",       NULL,   LEVEL_BRIEF,    NULL,    show_ifinfod_version},
    { "detail",      NULL,   LEVEL_DETAIL,   NULL,    show_ifinfod_version},
    { NULL,          NULL,   0,              NULL,    NULL}
};
static const parse_menu_t show_ifinfod_menu[] = {
    { "interfaces",  NULL,   0,    show_ifinfod_interface_menu,  NULL},
    { "version",     NULL,   0,    show_ifinfod_version_menu,    NULL},
    { NULL,          NULL,   0,    NULL,                         NULL}
};
static const parse_menu_t show_menu[] = {
    { "jnx-ifinfo",  NULL,   0,    show_ifinfod_menu,    NULL},
    { NULL,          NULL,   0,    NULL,                 NULL}
};
/* main menu */
const parse_menu_t master_menu[] = {
    { "show",       NULL,    0,    show_menu,     NULL },
    { NULL,         NULL,    0,    NULL,          NULL }
};
     
/* end of file */

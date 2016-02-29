/*
 * $Id: jnx-routeserviced_ui.c 346460 2009-11-14 05:06:47Z ssiano $
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
 *     jnx-routeserviced_ui.c
 * @brief 
 *     Handle various requests from cli
 *
 * Command line requests to jnx-routeservide from the host are delivered
 * and processed here.
 */

#include <string.h>

#include <sys/queue.h>

#include <isc/eventlib.h>

#include <jnx/bits.h>
#include <jnx/junos_init.h>
#include <jnx/patricia.h>
#include <jnx/stats.h>

#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>

#include JNX_ROUTESERVICED_ODL_H
#include JNX_ROUTESERVICED_OUT_H

#include "jnx-routeserviced_config.h"


/*
 * Global #defines 
 */
#define LEVEL_BRIEF     1    /* Style of display - brief */
#define LEVEL_DETAIL    2    /* Style of display - detail */
#define LEVEL_EXTENSIVE 3    /* Style of display - extensive */

/*
 * Forward declarations
 */
static int jnx_routeserviced_show_version (mgmt_sock_t *msp, 
                                           parse_status_t *csb,
                                           char *unparsed __unused);
static int jnx_routeserviced_show_data (mgmt_sock_t *msp, 
                                        parse_status_t *csb, 
                                        char *unparsed __unused);


/**
 * @brief
 *     Parse menu structure used by @a junos_daemon_init
 *     
 * Format of the parse menu structure is:
 *
 * @verbatim
   Menu structure format
   +---------+-------------+---------+------------+-----------------+
   | command | description | arg blk | child menu | command handler |
   |         |   or NULL   |  or 0   |   or NULL  |     or NULL     |
   +---------+-------------+---------+------------+-----------------+
   @endverbatim
 */

/* 
 * "show jnx-routeservice summary ..." 
 */
static const parse_menu_t show_rt_data_menu[] = {
    { "brief",  NULL, LEVEL_BRIEF, NULL, jnx_routeserviced_show_data},
    { "detail", NULL, LEVEL_DETAIL, NULL, jnx_routeserviced_show_data},
    { NULL, NULL, 0, NULL, NULL }
};

/* 
 * "show jnx-routeservice ..." 
 */
static const parse_menu_t show_jnx_routeservice_menu[] = {
    { "summary",  NULL, 0, show_rt_data_menu, NULL },
    { NULL,  NULL, 0, NULL,  NULL }
};

/* 
 * "show version ..." 
 */
static const parse_menu_t show_version_menu[] = {
    { "brief", NULL, LEVEL_BRIEF, NULL, jnx_routeserviced_show_version},
    { "detail", NULL, LEVEL_DETAIL, NULL, jnx_routeserviced_show_version},
    { "extensive", NULL, LEVEL_EXTENSIVE, NULL, jnx_routeserviced_show_version},
    { NULL, NULL, 0, NULL, NULL }
};

/* 
 * "show ..." 
 */
static const parse_menu_t show_menu[] = {
    { "jnx-routeservice", NULL, 0, show_jnx_routeservice_menu, NULL },
    { "version", NULL, 0, show_version_menu, NULL },  /* called internally
							 unless specified
							 in ddl */
    { NULL, NULL, 0, NULL, NULL }
};

/* 
 * Main menu 
 */
const parse_menu_t master_menu[] = {
    { "show", NULL, 0, show_menu, NULL }, 
    { NULL, NULL, 0, NULL, NULL }
};


/**
 * @brief
 *     Prints version information
 *
 * This routine prints the standard version information in either
 * a detailed, brief or extensive style.
 *
 * @param[in] msp
 *     Management socket pointer
 * @param[in] csb
 *     Parser status block pointer
 * @param[in] unparsed
 *     unused
 *     
 * @return 
 *     0  success (always) 
 */

static int
jnx_routeserviced_show_version (mgmt_sock_t *msp, parse_status_t *csb,
				char *unparsed __unused)
{
    int detail_level = ms_parse_get_subcode(csb);
    
    if (detail_level == LEVEL_BRIEF) {
        xml_show_version(msp, NULL, FALSE);
    } else if (detail_level == LEVEL_DETAIL ||
               detail_level == LEVEL_EXTENSIVE) {
        xml_show_version(msp, NULL, TRUE);
    }

    return 0;
}

/**
 * @brief
 *     Displays a single instance of data 
 *
 * This routine prints a particular instance of route data fields.
 * It also shows a status whether the route addition succeeded, failed
 * or is still pending.
 *
 * @param[in] rt_data
 *     Pointer to route data structure
 * @param[in] msp
 *     Management socket pointer
 * @param[in] detail_level
 *     unused
 */

static void
jnx_routeserviced_show_rt_data (jnx_routeserviced_data_t *rt_data, 
				mgmt_sock_t *msp,
				int32_t detail_level __unused)
{
    XML_OPEN(msp, ODCI_JNX_ROUTESERVICE_ROUTE_INFO);

    XML_ELT(msp, ODCI_JNX_ROUTESERVICE_ROUTE_INFO_DESTINATION, "%s/%d", 
	    rt_data->dest, rt_data->prefixlen);

    XML_ELT(msp, ODCI_JNX_ROUTESERVICE_ROUTE_INFO_NEXT_HOP, "%s", 
	    rt_data->nhop);

    XML_ELT(msp, ODCI_JNX_ROUTESERVICE_ROUTE_INFO_INTERFACE, "%s", 
	    rt_data->ifname);
    
    /*
     * Status information is asynchronously updated and hence, the 
     * actual status might not be what is displayed
     */
    if (rt_data->req_status == RT_ADD_REQ_STAT_SUCCESS) {
        XML_ELT(msp, ODCI_JNX_ROUTESERVICE_ROUTE_INFO_STATUS, "%s", "Success");
    } else if (rt_data->req_status == RT_ADD_REQ_STAT_FAILURE) {
        XML_ELT(msp, ODCI_JNX_ROUTESERVICE_ROUTE_INFO_STATUS, "%s", "Failure");
    } else if (rt_data->req_status == RT_ADD_REQ_STAT_PENDING) {
        XML_ELT(msp, ODCI_JNX_ROUTESERVICE_ROUTE_INFO_STATUS, "%s", "Pending");
    }

    XML_CLOSE(msp, ODCI_JNX_ROUTESERVICE_ROUTE_INFO);

    return;
}

/**
 * @brief
 *     Displays configured route data 
 *
 * This routine prints or all the instances of the route data field 
 * stored in the patricia tree depending on @a unparsed. Internally, 
 * it invokes @jnx_routeserviced_show_rt_data and hence the status of 
 * the route add request is also displayed.
 *
 * @param[in] msp
 *     Management socket pointer
 * @param[in] csb
 *     Parser status block pointer
 * @param[in] unparsed
 *     unused
 * @return
 *     0   success;
 */
static int
jnx_routeserviced_show_data (mgmt_sock_t *msp, parse_status_t *csb, 
			     char *unparsed __unused)
{
    jnx_routeserviced_data_t *rt_data;
    const char *style;
    int detail_level = ms_parse_get_subcode(csb);

    /*
     * Display top level xml tag and header
     */
    if (detail_level == LEVEL_BRIEF) {
        style = xml_attr_style(JNX_ROUTESERVICE_SUMMARY_INFORMATION_STYLE_BRIEF);
    } else {
        style = xml_attr_style(JNX_ROUTESERVICE_SUMMARY_INFORMATION_STYLE_DETAIL);
    }
    XML_OPEN(msp, ODCI_JNX_ROUTESERVICE_SUMMARY_INFORMATION, "%s %s",
             xml_attr_xmlns(XML_NS), style);

    /*
     * Display all the data stored in the patricia tree
     */
     for (rt_data = rt_data_first(); rt_data;
         rt_data = rt_data_next(rt_data)) {
         jnx_routeserviced_show_rt_data(rt_data, msp, detail_level);
     }

    XML_CLOSE(msp, ODCI_JNX_ROUTESERVICE_SUMMARY_INFORMATION);
    return 0;
}




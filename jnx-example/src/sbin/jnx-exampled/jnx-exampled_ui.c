/*
 * $Id: jnx-exampled_ui.c 365138 2010-02-27 10:16:06Z builder $
 *
 * jnx-exampled_ui.c - routines for jnx-exampled ui connections.
 *
 * Copyright (c) 2005-2007, Juniper Networks, Inc.
 * All rights reserved.
 */


#include <isc/eventlib.h>
#include <jnx/bits.h>

#include <jnx/junos_init.h>
#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>
#include <junoscript/xmlstr.h>

#include <string.h>
#include <jnx/patricia.h>

#include <jnx/stats.h>
#include JNX_EXAMPLED_ODL_H
#include JNX_EXAMPLED_OUT_H

#include "jnx-exampled_config.h"

/******************************************************************************
 * jnx-exampled_ui.c: Handle requests from cli or monitoring programs 
 *
 * Command line requests to jnx-exampled from the host are delivered
 * via a local unix domain socket connection and processed here.
 *****************************************************************************/

/******************************************************************************
 *                        Global Variables
 *****************************************************************************/

#define LEVEL_SUMMARY   0
#define LEVEL_BRIEF     1
#define LEVEL_DETAIL    2
#define LEVEL_EXTENSIVE 3

/******************************************************************************
 *                        Local Functions
 *****************************************************************************/

/******************************************************************************
 * Function : jnx_exampled_show_version
 * Purpose  : prints standard detailed/brief version info
 * Inputs   : msp - management socket pointer
 *            csb - parser status block(csb->subcode contains argument
 *                  from menu def)
 * Return   : 0 if no error; 1 to terminate connection
 *****************************************************************************/

static int
jnx_exampled_show_version (mgmt_sock_t *msp, parse_status_t *csb,
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

/******************************************************************************
 * Function : jnx_exampled_show_ex_data
 * Purpose  : displays a single instance of ex_data
 * Inputs   : msp - management socket pointer
 * Return   : 0 if no error; 1 to terminate connection
 *****************************************************************************/
static void
jnx_exampled_show_ex_data (jnx_exampled_data_t *ex_data, mgmt_sock_t *msp,
			   int32_t detail_level)
{
    XML_OPEN(msp, ODCI_JNX_EXAMPLE_DATA);

    XML_ELT(msp, ODCI_JNX_EXAMPLE_DATA_NAME, "%s", ex_data->exd_index);

    if (detail_level == LEVEL_DETAIL) {
        const char *type;

        switch (ex_data->exd_type) {
            case JNX_EXAMPLE_DATA_TYPE_FAT:
                type = JNX_EXAMPLE_DTYPE_FAT;
                break;

            case JNX_EXAMPLE_DATA_TYPE_SKINNY:
                type = JNX_EXAMPLE_DTYPE_SKINNY;
                break;

            case JNX_EXAMPLE_DATA_TYPE_NONE:
            default:
                type = JNX_EXAMPLE_DTYPE_NONE;
                break;
        }
        XML_ELT(msp, ODCI_JNX_EXAMPLE_DATA_DESCRIPTION, "%s", 
                ex_data->exd_descr);
        XML_ELT(msp, ODCI_JNX_EXAMPLE_DATA_TYPE, "%s", type);
    }
    
    XML_ELT(msp, ODCI_JNX_EXAMPLE_DATA_VALUE, "%s", ex_data->exd_value);
    
    XML_CLOSE(msp, ODCI_JNX_EXAMPLE_DATA);
    return;
}

/******************************************************************************
 * Function : jnx_exampled_show_data
 * Purpose  : displays configured ex_data
 * Inputs   : msp - management socket pointer
 * Return   : 0 if no error; 1 to terminate connection
 *****************************************************************************/
static int32_t
jnx_exampled_show_data (mgmt_sock_t *msp, parse_status_t *csb, char *unparsed)
{
    int32_t detail_level = ms_parse_get_subcode(csb);
    char *ex_name = unparsed;
    jnx_exampled_data_t *ex_data;
    const char *ex_style;
    int32_t retVal = 0;

    /*
     * display top level xml tag & header
     */
    if (detail_level == LEVEL_BRIEF) {
        ex_style = xml_attr_style(JNX_EXAMPLE_DATA_INFORMATION_STYLE_BRIEF);
    } else {
        ex_style = xml_attr_style(JNX_EXAMPLE_DATA_INFORMATION_STYLE_DETAIL);
    }
    XML_OPEN(msp, ODCI_JNX_EXAMPLE_DATA_INFORMATION, "%s %s",
             xml_attr_xmlns(XML_NS), ex_style);

    /*
     * if a name was specified, find that specific instance.  If not, display
     * all the configured data
     */
    if (ex_name) {
        if ((ex_data = ex_data_lookup(ex_name)) != NULL) {
            jnx_exampled_show_ex_data(ex_data, msp, detail_level);
        }
    } else {
        for (ex_data = ex_data_first(); ex_data;
             ex_data = ex_data_next(ex_data)) {
            jnx_exampled_show_ex_data(ex_data, msp, detail_level);
        }
    }

    XML_CLOSE(msp, ODCI_JNX_EXAMPLE_DATA_INFORMATION);
    return retVal;
}

/******************************************************************************
 * Function : jnx_exampled_show_statistics
 * Purpose  : prints some system statistics like cpu/memory usage
 * Inputs   : msp - management socket pointer
 * Return   : 0 if no error; 1 to terminate connection
 *****************************************************************************/

static int
jnx_exampled_show_statistics (mgmt_sock_t *msp, parse_status_t *csb __unused,
			      char *unparsed __unused)
{
    double cpu_usage;
    int mem_usage;
    
    cpu_usage = stats_get_cpu_usage();
    mem_usage = stats_get_memory_usage();
    
    if (cpu_usage < 0.0) {
        XML_RAW(msp, 0, xml_error_str(FALSE, getprogname(), NULL, NULL, NULL,
                NULL, NULL, "Unable to obtain CPU usage statistics"));
        return 0;
    }
    if (mem_usage < 0.0) {
        XML_RAW(msp, 0, xml_error_str(FALSE, getprogname(), NULL, NULL, NULL,
                NULL, NULL, "Unable to obtain memory usage statistics"));
        return 0;
    }
    
    XML_OPEN(msp, ODCI_JNX_EXAMPLE_STATISTICS);

    XML_ELT(msp, ODCI_CPU_USAGE, "%d", (int)cpu_usage);
    XML_ELT(msp, ODCI_MEMORY_USAGE, "%d", mem_usage);

    XML_CLOSE(msp, ODCI_JNX_EXAMPLE_STATISTICS);

    return 0;
}

/******************************************************************************
 *                        Menu Structure
 *****************************************************************************/

/*
 * format:
 *   command, desc, arg block(or 0), child menu(or 0), command(or 0)
 */
/* "show example data..." */
static const parse_menu_t show_ex_data_menu[] = {
    { "brief", NULL, LEVEL_BRIEF, NULL, jnx_exampled_show_data},
    { "detail", NULL, LEVEL_DETAIL, NULL, jnx_exampled_show_data},
    { NULL, NULL, 0, NULL, NULL }
};

/* "show jnx-example ..." */
static const parse_menu_t show_jnx_example_menu[] = {
    { "statistics", NULL, 0, NULL, jnx_exampled_show_statistics },
    { "data", "example data", 0, show_ex_data_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/* "show version ..." */
static const parse_menu_t show_version_menu[] = {
    { "brief", NULL, LEVEL_BRIEF, NULL, jnx_exampled_show_version},
    { "detail", NULL, LEVEL_DETAIL, NULL, jnx_exampled_show_version},
    { "extensive", NULL, LEVEL_EXTENSIVE, NULL, jnx_exampled_show_version},
    { NULL, NULL, 0, NULL, NULL }
};


/* "show ..." */
static const parse_menu_t show_menu[] = {
    { "jnx-example", NULL, 0, show_jnx_example_menu, NULL },
    { "version", NULL, 0, show_version_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/* main menu */
const parse_menu_t master_menu[] = {
    { "show", NULL, 0, show_menu, NULL }, 
    { NULL, NULL, 0, NULL, NULL }
};

/* end of file */

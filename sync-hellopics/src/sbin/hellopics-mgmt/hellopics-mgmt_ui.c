/*
 * $Id: hellopics-mgmt_ui.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file hellopics-mgmt_ui.c
 * @brief Relating to the user interface (commands) of hellopics-mgmt 
 * 
 * Contains callback functions that get executed by commands on the router
 */

#include <jnx/mgmt_sock_pub.h>
#include <jnx/ms_parse.h>
#include <junoscript/xmlrpc.h>
#include <junoscript/xmllib_pub.h>
#include <ddl/defs.h>
#include "hellopics-mgmt_conn.h"

#include HELLOPICS_ODL_H
#include HELLOPICS_OUT_H

/*** Data Structures ***/

extern hellopics_stats_t hellopics_stats; ///< global message stats

// See menu structure at the bottom of this file

/*** STATIC/INTERNAL Functions ***/

/**
 * Displays the configured message
 * 
 * @param[in] msp
 *     management socket pointer
 * 
 * @param[in] csb
 *     parsed info status (contains subcodes/arguments)
 * 
 * @param[in] unparsed
 *     unparsed command string
 * 
 * @return 0 if no error; 1 to terminate connection
 */
static int32_t
hellopics_show_stats (mgmt_sock_t *msp,
                        parse_status_t *csb __unused,
                        char *unparsed __unused)
{
    XML_OPEN(msp, ODCI_HELLOPICS_STATISTICS);

    XML_ELT(msp, ODCI_CYCLES_SENT, "%d", hellopics_stats.msgs_sent);
    XML_ELT(msp, ODCI_CYCLES_RECEIVED, "%d", hellopics_stats.msgs_received);
    XML_ELT(msp, ODCI_CYCLES_MISSED, "%d", hellopics_stats.msgs_missed);
    XML_ELT(msp, ODCI_CYCLES_OUT_OF_ORDER, "%d", hellopics_stats.msgs_badorder);

    XML_CLOSE(msp, ODCI_HELLOPICS_STATISTICS);
    
    return 0;
}


/*** GLOBAL/EXTERNAL Functions ***/



/******************************************************************************
 *                        Menu Structure
 *****************************************************************************/

/*
 * format (from libjuniper's ms_parse.h):
 *   command,
 *   help desc (or NULL),
 *   user data arg block (or 0),
 *   child (next) menu (or NULL),
 *   handler for command(or 0)
 */

/**
 *  show sync hellopics ... commands
 */
static const parse_menu_t show_sync_hellopics_menu[] = {
    { "statistics", NULL, 0, NULL, hellopics_show_stats },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  show sync ... commands
 */
static const parse_menu_t show_sync_menu[] = {
    { "hellopics", NULL, 0, show_sync_hellopics_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  show ... commands
 */
static const parse_menu_t show_menu[] = {
    { "sync", NULL, 0, show_sync_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  main menu of commands
 */
const parse_menu_t master_menu[] = {
    { "show", NULL, 0, show_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};


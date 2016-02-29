/*
 * $Id: counterd_ui.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file counterd_ui.c
 * @brief Relating to the user interface (commands) of counterd 
 * 
 * Contains callback functions that get executed by commands on the router
 */

#include <jnx/mgmt_sock_pub.h>
#include <jnx/ms_parse.h>
#include <junoscript/xmlrpc.h>
#include <junoscript/xmllib_pub.h>
#include <ddl/defs.h>
#include "counterd_config.h"
#include "counterd_message.h"

#include COUNTERD_ODL_H
#include COUNTERD_OUT_H

/*** Data Structures ***/

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
 * @return 0
 */
static int32_t
counterd_show_messages (mgmt_sock_t *msp,
                        parse_status_t *csb __unused,
                        char *unparsed __unused)
{
    XML_OPEN(msp, ODCI_COUNTER_MESSAGES);

    XML_ELT(msp, ODCI_MESSAGE, "%s", get_message());
    
    XML_ELT(msp, ODCI_COUNT, "%d", get_times_viewed());    

    XML_CLOSE(msp, ODCI_COUNTER_MESSAGES);
    
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
 *  show sync counter ... commands
 */
static const parse_menu_t show_sync_counter_menu[] = {
    { "message", NULL, 0, NULL, counterd_show_messages },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  show sync ... commands
 */
static const parse_menu_t show_sync_menu[] = {
    { "counter", NULL, 0, show_sync_counter_menu, NULL },
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


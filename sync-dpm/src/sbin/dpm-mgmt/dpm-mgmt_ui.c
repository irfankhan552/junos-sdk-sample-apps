/*
 * $Id: dpm-mgmt_ui.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */
 

/**
 * @file dpm-mgmt_ui.c
 * @brief Relating to the user interface (commands) of dpm-mgmt 
 * 
 * Contains callback functions that get executed by commands on the router
 */

#include <sync/common.h>
#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>
#include <ddl/defs.h>
#include "dpm-mgmt_main.h"
#include "dpm-mgmt_config.h"
#include "dpm-mgmt_conn.h"
#include "dpm-mgmt_logging.h"

#include DPM_ODL_H
#include DPM_OUT_H

/*** Constants ***/


/*** Data Structures ***/

extern class_list_t logged_in;

// See menu structure at the bottom of this file


/*** STATIC/INTERNAL Functions ***/


/**
 * Shows the dpm stats optionally just for one monitoring config group
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
dpm_show_subs(mgmt_sock_t * msp,
              parse_status_t * csb __unused,
              char * unparsed __unused)
{
    login_class_t * lc;
    login_sub_t * ls;
    
    XML_OPEN(msp, ODCI_DPM_SUBSCRIBERS);

    // go thru classes
    lc = TAILQ_FIRST(&logged_in);
    while(lc != NULL) {
        
        XML_OPEN(msp, ODCI_CLASS);
        XML_ELT(msp, ODCI_CLASS_NAME, "%s", lc->class);        
        XML_OPEN(msp, ODCI_SUBSCRIBERS);
        
        // go thru subscribers
        ls = TAILQ_FIRST(&lc->subs);
        while(ls != NULL) {
            XML_ELT(msp, ODCI_SUBSCRIBER, "%s", ls->name);
            ls = TAILQ_NEXT(ls, entries);      
        }
        
        XML_CLOSE(msp, ODCI_SUBSCRIBERS);
        XML_CLOSE(msp, ODCI_CLASS);
        
        lc = TAILQ_NEXT(lc, entries);
    }
    
    XML_CLOSE(msp, ODCI_DPM_SUBSCRIBERS);
    
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
 *  show sync dpm ... commands
 */
static const parse_menu_t show_sync_dpm_menu[] = {
    { "subscribers", NULL, 0, NULL, dpm_show_subs },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  show sync ... commands
 */
static const parse_menu_t show_sync_menu[] = {
    { "dpm", NULL, 0, show_sync_dpm_menu, NULL },
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


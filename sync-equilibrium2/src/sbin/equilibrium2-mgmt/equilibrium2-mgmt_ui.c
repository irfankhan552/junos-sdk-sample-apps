/*
 * $Id: equilibrium2-mgmt_ui.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium2-mgmt_ui.c
 * @brief Relating to the user interface (commands) of equilibrium2-mgmt 
 * 
 * Contains callback functions that get executed by commands on the router
 */

#include <sync/equilibrium2.h>
#include "equilibrium2-mgmt.h"

#include <arpa/inet.h>
#include <jnx/aux_types.h>
#include <jnx/mgmt_sock_pub.h>
#include <jnx/ms_parse.h>
#include <jnx/pconn.h>
#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>
#include <junoscript/xmlstr.h>
#include <ddl/defs.h>

#include EQUILIBRIUM2_ODL_H

/*** Constants ***/

/*** Data Structures ***/

/*** STATIC/INTERNAL Functions ***/

/**
 * @brief
 * Displays the service status. 
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
eq2_show_status (mgmt_sock_t *msp UNUSED, parse_status_t* csb UNUSED,
        char *unparsed UNUSED)
{
    client_t *client;
    msg_svr_group_t *group;
    msg_svr_addr_t *addr;
    struct in_addr addr_tmp;
    int group_count;
    int addr_count;

    XML_OPEN(msp, ODCI_EQUILIBRIUM2_STATUS);
    client = client_get_next(NULL);
    if (client == NULL) {
        XML_ELT(msp, ODCI_GET_STATUS, "%s", "No service running.");
        XML_CLOSE(msp, ODCI_EQUILIBRIUM2_STATUS);
        return 0;
    }

    while (client) {
        group_count = ntohl(*((uint32_t *)client->msg));
        XML_OPEN(msp, ODCI_SERVICE_INFO);

        XML_OPEN(msp, ODCI_SERVICE_LOCATION);
        XML_ELT(msp, ODCI_FPC_SLOT, "%d", client->info.ppi_fpc_slot);
        XML_ELT(msp, ODCI_PIC_SLOT, "%d", client->info.ppi_pic_slot);
        XML_CLOSE(msp, ODCI_SERVICE_LOCATION);

        XML_OPEN(msp, ODCI_SERVER_GROUP);
        group = (msg_svr_group_t *)(client->msg + sizeof(uint32_t));
        while (group_count--) {
            XML_ELT(msp, ODCI_SERVER_GROUP_NAME, "%s", group->group_name);

            XML_OPEN(msp, ODCI_ADDRESS_LIST);
            addr = (msg_svr_addr_t *)group->group_addr;
            addr_count = ntohs(group->group_addr_count);
            while (addr_count--) {
                addr_tmp.s_addr = addr->addr;
                XML_ELT(msp, ODCI_ADDRESS, "%s", inet_ntoa(addr_tmp));
                XML_ELT(msp, ODCI_SESSION_COUNT, "%d",
                        ntohs(addr->addr_ssn_count));
                addr++;
            }
            XML_CLOSE(msp, ODCI_ADDRESS_LIST);
            group = (msg_svr_group_t *)addr;
        }
        XML_CLOSE(msp, ODCI_SERVER_GROUP);

        XML_CLOSE(msp, ODCI_SERVICE_INFO);
        client = client_get_next(client);
    }
    XML_CLOSE(msp, ODCI_EQUILIBRIUM2_STATUS);
    return 0;
}

/**
 * @brief
 * Closes the session
 * 
 * @param[in] msp
 *      management socket pointer
 * 
 * @param[in] csb
 *      parsed info status
 * 
 * @param[in] unparsed
 *      unparsed command string
 * 
 * @return
 *      1 to terminate connection
 */
static int
eq2_quit (mgmt_sock_t *msp, parse_status_t *csb UNUSED, char *unparsed UNUSED)
{
    XML_SEND_SUCCESS_TAG(msp);
    return 1;        /* 1 = terminate */
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
 *  show sync equilibrium ... commands
 */
static const parse_menu_t show_sync_eq2_menu[] = {
    { "status", NULL, 0, NULL, eq2_show_status },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  show sync ... commands
 */
static const parse_menu_t show_sync_menu[] = {
    { "equilibrium2", NULL, 0, show_sync_eq2_menu, NULL },
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
    { "quit", NULL, 0, NULL, eq2_quit },
    { NULL, NULL, 0, NULL, NULL }
};


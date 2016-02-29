/*
 * $Id: jnx-msprsmd_ui.c 418048 2010-12-30 18:58:42Z builder $
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file
 *     jnx-msprsmd_ui.c
 * @brief
 *     Handle various requests from cli
 *
 * Command line requests to jnx-msprsmd from the host
 * are delivered and processed here.
 */

#include <string.h>
#include <isc/eventlib.h>

#include <jnx/junos_init.h>

#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>

#include JNX_MSPRSM_OUT_H

/*
 * Global #defines
 */
#define LEVEL_BRIEF     1    /* Style of display - brief */
#define LEVEL_DETAIL    2    /* Style of display - detail */
#define LEVEL_EXTENSIVE 3    /* Style of display - extensive */


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
jnx_msprsmd_show_version (mgmt_sock_t *msp, parse_status_t *csb,
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
 * "show version ..."
 */
static const parse_menu_t show_version_menu[] = {
    { "brief", NULL, LEVEL_BRIEF, NULL, jnx_msprsmd_show_version},
    { "detail", NULL, LEVEL_DETAIL, NULL, jnx_msprsmd_show_version},
    { "extensive", NULL, LEVEL_EXTENSIVE, NULL, jnx_msprsmd_show_version},
    { NULL, NULL, 0, NULL, NULL }
};

/*
 * "show ..."
 */
static const parse_menu_t show_menu[] = {
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

/* end of file */

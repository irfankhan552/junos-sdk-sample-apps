/*
 * $Id: ipprobe-manager_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ipprobe-manager_config.c
 * @brief Load and store the configuration data
 *
 * These functions will parse and load the configuration data.
 *
 */

#include "ipprobe-manager.h"
#include <ddl/dax.h>

/*** Data Structures ***/

u_short manager_port;   /**< The TCP port for probe manager */

/**
 * Path to traceoptions configuration 
 */
const char *probe_manager_config_path[] = {"sync", "ip-probe", "manager", NULL};

/*** STATIC/INTERNAL Functions ***/

/**
 * Invoke functions which depend on configuration.
 */
static void
post_config_proc (void)
{
    /* Open probe manager on configured TCP port. */
    manager_open(manager_port);
}


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Read daemon configuration from the database.
 *
 * @param[in] check
 *      1 if this function being invoked because of a commit check
 *
 * @return 0 on success, -1 on failure
 *
 * @note Do not use ERRMSG during config check.
 */
int
manager_config_read (int check)
{
    ddl_handle_t *top = NULL;

    manager_port = DEFAULT_MANAGER_PORT;

    junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
            "%s: Loading probe manager configuration...", __func__);

    /* Read probe manager port. */
    if(dax_get_object_by_path(NULL, probe_manager_config_path, &top, TRUE)) {
        dax_get_ushort_by_aid(top, PROBE_MANAGER_PORT, &manager_port);
    }

    junos_trace(PROBE_MANAGER_TRACEFLAG_NORMAL,
            "%s: Probe manager port: %d.", __func__, manager_port);

    if (top) {
        dax_release_object(&top);
    }

    if (check == 0) {
        post_config_proc();
    }

    return 0;
}

/*
 * $Id: jnx-msprsmd_config.c 418048 2010-12-30 18:58:42Z builder $
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file
 *     jnx-msprsmd_config
 * @brief
 *     Configurations
 *
 * This file contains the routine to read/check configuration database,
 * store the configuration in a local data structure and schedule the
 * init event routine.
 */


#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/queue.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>

#include <isc/eventlib.h>

#include <ddl/dax.h>

#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include <jnx/ssd_ipc.h>
#include <jnx/ssd_ipc_msg.h>
#include <jnx/junos_trace.h>
#include <jnx/junos_kcom.h>
#include <jnx/pconn.h>
#include <jnx/name_len_shared.h>

#include JNX_MSPRSM_OUT_H

#include "jnx-msprsmd.h"
#include "jnx-msprsmd_config.h"
#include "jnx-msprsmd_kcom.h"
#include "jnx-msprsmd_conn.h"
#include "jnx-msprsmd_ssd.h"


/*
 * Global Variables
 */
extern evContext event_ctx;
const char *config_path_msprsmd[] = { DDLNAME_JNX_MSPRSM, NULL };
const char *msprsmd_if_str[IF_MAX] = {"primary", "secondary"};

/**
 * @brief
 *     Loads the given  jnx-msprsmd's configuration
 *
 * Called from @a msprsmd_config_read.
 * The routine reads and traverses the configuration if it is correct,
 * and re-initializes the kcom, the pconn and the ssd subsystems.
 *
 *
 * @param[in] param
 *     A (void *) cast pointer to the config structure
 *
 * @return none
 */
static int
msprsmd_config_init (msprsmd_config_t *msprsmd_config)
{
    int error = 0;

    junos_trace(JNX_MSPRSMD_TRACEFLAG_CONFIG, "%s: "
                "\n%s interface is %s, %s interface is %s, floating_ip is %s",
                __func__,
                msprsmd_if_str[IF_PRI], msprsmd_config->ifname[IF_PRI],
                msprsmd_if_str[IF_SEC], msprsmd_config->ifname[IF_SEC],
                addr2ascii(AF_INET, &msprsmd_config->floating_ip,
                           sizeof(msprsmd_config->floating_ip), NULL));

    /*
     * Reopen kcom conn and the ssd, traverse the new configuration
     */
    error = msprsmd_kcom_init(event_ctx, msprsmd_config);
    if (error != 0) {
        ERRMSG(JNX_MSPRSMD_KCOM_INIT_FAIL, LOG_ERR, "%s: "
               "can't initialize kcom, error is %d",
               __func__, error);
        goto exit;
    }

    error = msprsmd_conn_init(msprsmd_config);
    if (error != 0) {
        ERRMSG(JNX_MSPRSMD_SSD_CONN_FAIL, LOG_ERR, "%s: "
               "connection failure with SSD with return code %d",
               __func__, error);
        goto exit;
    }

    /*
     * Call the SSD initialization routine - this will
     * create the routing service and the nexthops
     */
    error = msprsmd_ssd_init(event_ctx, msprsmd_config);
    if (error != 0) {
        ERRMSG(JNX_MSPRSMD_SSD_CONN_FAIL, LOG_ERR, "%s: "
               "connection failure with SSD with return code %d",
               __func__, error);
        goto exit;
    }

exit:
    if (error) {
        msprsmd_ssd_shutdown();
        msprsmd_kcom_shutdown();
    }

    return error;
}

/**
 * @brief
 *     Read jnx-msprsmd's configuration from database
 *
 * This routine is called as a callback from @a junos_daemon_init for
 * purposes like to check if the configuration exists and eventually
 * read the configurations if they exist.
 *
 * @note
 *     Do not use ERRMSG during configuration check. Could use during
 *     configuration read phase.
 *
 * @param[in] check
 *     Non-zero is configuration is to be checked only and zero if it
 *     has to be read.
 *
 * @return 0 on success or error on failure
 */

int
msprsmd_config_read (int check)
{
    ddl_handle_t *dop = NULL;
    const char *field = DDLNAME_JNX_MSPRSM;
    int error= 0;
    int af;
    msprsmd_config_t msprsmd_config;

    /*
     * Read the config itself
     */
    if (dax_get_object_by_path(NULL, config_path_msprsmd, &dop, FALSE)) {

        /*
         * Primary interface
         */
        field = DDLNAME_JNX_MSPRSM_PRIMARY_INTERFACE;
        if (!dax_get_stringr_by_name(dop, field,
                                     msprsmd_config.ifname[IF_PRI],
                                     KCOM_IFNAMELEN)) {

            error= ENOENT;
            goto exit;
        }

        /*
         * Secondary interface
         */
        field = DDLNAME_JNX_MSPRSM_SECONDARY_INTERFACE;
        if (!dax_get_stringr_by_name(dop, field,
                                     msprsmd_config.ifname[IF_SEC],
                                     KCOM_IFNAMELEN)) {

            error= ENOENT;
            goto exit;
        }

        /*
         * Floating ip
         */
        field = DDLNAME_JNX_MSPRSM_FLOATING_IP;
        if (!dax_get_ipaddr_by_name(dop, field, &af,
                                    &msprsmd_config.floating_ip,
                                    sizeof(struct in_addr))) {

            error= ENOENT;
            goto exit;
        }
        if (af != AF_INET) {

            error= EINVAL;
            goto exit;
        }
    }

exit:
    /*
     * Print error, if any
     */
    if (error) {

        if (!check) {

            ERRMSG(JNX_MSPRSMD_CONFIG_FAIL, LOG_ERR, "%s :"
               "error in '%s' field (%s)",
               __func__, field, strerror(error));
        }

        dax_error(dop, "jnx-msprsmd: error in '%s' field (%s)",
                  field, strerror(error));
    } else {

        if (!check) {

            error = msprsmd_config_init(&msprsmd_config);
            if (error) {

                ERRMSG(JNX_MSPRSMD_CONFIG_FAIL, LOG_ERR, "%s :"
                       "msprsmd_config_init() returned error %s",
                       __func__, strerror(error));
            }
        }
    }

    /*
     * Finish up before return
     */
    if (dop) {
        dax_release_object(&dop);
    }

    return error;
}

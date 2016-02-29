/*
 * $Id: jnx-msprsmd_kcom.c 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-msprsmd_kcom.c - kcom init and message handler functions
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */


/**
 *
 * @file msprsmd_kcom.c
 * @brief Handles kernel communication initialization
 * and message handler functions
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <jnx/trace.h>
#include <ddl/dtypes.h>

#include <jnx/provider_info.h>
#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>
#include <jnx/pconn.h>
#include <jnx/ssd_ipc_msg.h>
#include <jnx/name_len_shared.h>

#include JNX_MSPRSM_OUT_H

#include "jnx-msprsmd.h"
#include "jnx-msprsmd_config.h"
#include "jnx-msprsmd_kcom.h"
#include "jnx-msprsmd_conn.h"
#include "jnx-msprsmd_ssd.h"

/*
 * Global variables
 */
extern const char *msprsmd_if_str[IF_MAX];

/*
 * Global definitions
 */
#define KCOM_ID_MSPRSMD 55

#define KCOM_CHECK(__func) \
    CHECK(JNX_MSPRSMD_KCOM_ERR, LOG_ERR, (__func), #__func)

typedef enum {
    KCOM_STATUS_OFF = 0,
    KCOM_STATUS_ON,
    KCOM_STATUS_ACTIVE
} kcom_status_t;

/*
 * Static variables
 */
static kcom_status_t kcom_status = KCOM_STATUS_OFF;

//static int kcom_active = 0;
static kcom_ifl_t kcom_if[IF_MAX];

/**
 *
 * @brief
 *       Activates given interface by adding the nexthop
 *       and route and setting its ifa
 *
 * @param[in] msprsmd_if
 *       The index of the interface to activate
 *
 * @return 0 on success or error on failure
 */
static int
msprsmd_kcom_activate_if (msprsmd_if_t msprsmd_if)
{
    junos_trace(JNX_MSPRSMD_TRACEFLAG_KCOM, "%s: "
                "activating %s interface",
                __func__,  msprsmd_if_str[msprsmd_if]);

    KCOM_CHECK(msprsmd_conn_add_ifa(kcom_if[msprsmd_if].ifl_index));

    /*
     * After the nexthop is added, ssd is expected to add the route
     */
    KCOM_CHECK(msprsmd_ssd_add_nexthop(msprsmd_if));

    return 0;
}


/**
 *
 * @brief
 *       Deactivates given interface by removing its nexthop and route.
 *
 * @param[in] msprsmd_if
 *       The index of the interface to deactivate
 *
 * @return 0 on success or error on failure
 */
static int
msprsmd_kcom_deactivate_if(msprsmd_if_t msprsmd_if)
{
    junos_trace(JNX_MSPRSMD_TRACEFLAG_KCOM, "%s: "
                "deactivating %s interface",
                __func__,  msprsmd_if_str[msprsmd_if]);

    /*
     * After the route is removed, ssd is expected to remove the nexthop
     */
    KCOM_CHECK(msprsmd_ssd_del_route(msprsmd_if));

    return 0;
}


/**
 *
 * @brief
 *       Evaluates current status of primary and secondary interfaces
 *       and acts accordingly.
 *
 *  This function is called when interface status changes, it should:
 *  - if this is the first time both primary and secondary
 *    interfaces are up - acivate the primary interface
 *  - else if primary is up and secondary is down,
 *    deactivate secondary and activate primary
 *  - else if primary is down and secondary is up,
 *    deactivate primary and activate secondary
 *  - else - both interfaces are down - terminate the daemon!
 *
 * @return 0 on success or error on failure
 */
static int
msprsmd_kcom_switchover(void)
{
    kcom_ifdev_t kcom_ifd[IF_MAX];
    msprsmd_if_t msprsmd_if;

    /* Read ifd status instead of ifl status */
    for (msprsmd_if = 0; msprsmd_if < IF_MAX;  msprsmd_if++) {

        junos_kcom_ifd_get_by_index(kcom_if[msprsmd_if].ifl_devindex,
                                    &kcom_ifd[msprsmd_if]);

        junos_trace(JNX_MSPRSMD_TRACEFLAG_KCOM, "%s: %s interface is %s",
                    __func__, msprsmd_if_str[msprsmd_if],
                    junos_kcom_ifd_down(&kcom_ifd[msprsmd_if])?"DOWN":"UP");
    }

    /* Either primary or secondary interface's status change */
    if (!junos_kcom_ifd_down(&kcom_ifd[IF_PRI]) &&
        !junos_kcom_ifd_down(&kcom_ifd[IF_SEC])) {

        /* Both interfaces are up, activate primary */
        if (kcom_status != KCOM_STATUS_ACTIVE) {
            KCOM_CHECK(msprsmd_kcom_activate_if(IF_PRI));
            kcom_status = KCOM_STATUS_ACTIVE;
        }

    } else if (junos_kcom_ifd_down(&kcom_ifd[IF_PRI]) &&
               !junos_kcom_ifd_down(&kcom_ifd[IF_SEC])) {

        /* Primary down, secondary up -> activate secondary */
        KCOM_CHECK(msprsmd_kcom_deactivate_if(IF_PRI));
        KCOM_CHECK(msprsmd_kcom_activate_if(IF_SEC));

    } else if (!junos_kcom_ifd_down(&kcom_ifd[IF_PRI]) &&
               junos_kcom_ifd_down(&kcom_ifd[IF_SEC])) {

        /* Primary up, secondary down -> activate primary */
        KCOM_CHECK(msprsmd_kcom_deactivate_if(IF_SEC));
        KCOM_CHECK(msprsmd_kcom_activate_if(IF_PRI));

    } else {

        ERRMSG(JNX_MSPRSMD_KCOM_ERR, LOG_ERR, "%s: "
               "Both interfaces are down. "
               "Can't recover, exiting", __func__);

        /* Both interfaces are down, don't even try to recover */
        exit(1);
    }

    return 0;
}


/**
 *
 * @brief
 *       this function handles async ifl messages
 *
 * @param[in] kcom_ifl
 *       pointer to kcom_ifl_t structure
 * @param[in] void *
 *       Any User specific information.
 *       In this example, this field is unused.
 *
 *
 * @return 0 on success or error on failure
 */
static int
msprsmd_kcom_ifl_msg_handler (kcom_ifl_t *kcom_ifl, void *unused __unused)
{
    msprsmd_if_t msprsmd_if;

    for (msprsmd_if = IF_PRI; msprsmd_if < IF_MAX; msprsmd_if++) {

        if (!strncmp(kcom_ifl->ifl_name,
                     kcom_if[msprsmd_if].ifl_name,
                     KCOM_IFNAMELEN-1)) {

            /*
             * The ifl changed, store the new kcom_ifl_t structure
             */
            junos_trace(JNX_MSPRSMD_TRACEFLAG_KCOM, "%s: "
                        "Interface %s, replacing kcom structure: "
                        "old ifl_index %d, new ifl_index is %d",
                        __func__,
                        kcom_ifl->ifl_name,
                        ifl_idx_t_getval(kcom_if[msprsmd_if].ifl_index),
                        ifl_idx_t_getval(kcom_ifl->ifl_index));

            kcom_if[msprsmd_if] = *kcom_ifl;

            /*
             * Now run our switchcover logic and exit
             */
            junos_kcom_msg_free(kcom_ifl);
            return msprsmd_kcom_switchover();
        }
    }

    /*
     * If we reached here, we did not need this ifl...
     */
    junos_kcom_msg_free(kcom_ifl);

    return 0;
}

/**
 * @brief
 *      loads the kcom_ifl info by the given interface name
 *
 * @param[in] if_name
 *       Name of the interface, e.g. "ms-1/2/0.0"
 * @param[in] msprsmd_if
 *       The index of the interface to load
 *
 * @return 0 on success or error on failure
 */
static int
msprsmd_kcom_load_ifl (const char *if_name, msprsmd_if_t msprsmd_if)
{
    kcom_iff_t kcom_iff;
    kcom_ifa_t kcom_ifa;
    char loc_name[KCOM_IFNAMELEN];
    char *if_name_str, *if_subunit_str;
    if_subunit_t subunit;

    junos_trace(JNX_MSPRSMD_TRACEFLAG_KCOM, "%s: "
                "loading configuration for %s interface %s",
                __func__, msprsmd_if_str[msprsmd_if], if_name);

    /*
     * Copy interface name locally
     */
    if(strlcpy(loc_name, if_name, sizeof(loc_name)) > sizeof(loc_name)) {

        return EINVAL;
    }

    /*
     * Split the string at the first '.' occurence, if any
     */
    if_name_str = strtok(loc_name, ".");

    /*
     * If interface name has a subunit passed with it, extract it
     */
    if_subunit_str = strchr(loc_name, '.');
    if (if_subunit_str) {
        subunit = (if_subunit_t) strtol(if_subunit_str + 1, NULL, 0);
    } else {
        subunit = (if_subunit_t) 0;
    }

    /*
     * KCOM api to get the ifl associated with the logical
     * interface.  Note: if_name_str' does not contain the subunit.
     * Subunit is explicitly provided as another argument (here second).
     */
    junos_trace(JNX_MSPRSMD_TRACEFLAG_KCOM, "%s: "
                    "if_name_str=%s subunit=%d msprsmd_if=%d",
                    __func__,
                    if_name_str, subunit, msprsmd_if);

    KCOM_CHECK(junos_kcom_ifl_get_by_name(if_name_str, subunit,
                                       &kcom_if[msprsmd_if]));

    /*
     * Make sure there is an iff
     */
    KCOM_CHECK(junos_kcom_iff_get_by_index (kcom_if[msprsmd_if].ifl_index,
                                         AF_INET, &kcom_iff));
    /*
     * Make sure there is no ifa
     */
    if (!junos_kcom_ifa_get_first (kcom_if[msprsmd_if].ifl_index,
                                   AF_INET, &kcom_ifa)) {

        ERRMSG(JNX_MSPRSMD_KCOM_ERR, LOG_ERR, "%s: "
               "%s interface %s, ifa already exists",
               __func__, msprsmd_if_str[msprsmd_if], if_name);
        return EINVAL;
    }


    junos_trace(JNX_MSPRSMD_TRACEFLAG_KCOM, "%s: "
                "%s interface %s has ifl_index %d",
                __func__, msprsmd_if_str[msprsmd_if], if_name,
                ifl_idx_t_getval(kcom_if[msprsmd_if].ifl_index));

    return 0;
}


/**
 * @brief
 *     Traverses the user config
 *
 *  Reads the config from the CLI, performs constrain checks
 *  and fills in module's internal data structures.
 *
 * @return 0 on success or error on failure
 */
static int
msprsmd_kcom_read_config(msprsmd_config_t *msprsmd_config)
{
    msprsmd_if_t msprsmd_if;

    for (msprsmd_if = 0; msprsmd_if < IF_MAX; msprsmd_if++) {

        KCOM_CHECK(msprsmd_kcom_load_ifl(msprsmd_config->ifname[msprsmd_if],
                                         msprsmd_if));
    }

    return 0;
}


/*
 * Exported functions
 */


/**
 * @brief
 *     Gets intreface's chassis geometry
 *     by msprsmd interface index
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_kcom_pic_by_idx (msprsmd_if_t msprsmd_if,
                         uint32_t *fpc_slot, uint32_t *pic_slot)
{
    kcom_ifdev_t kcom_ifd;

    /*
     * Get ifd
     */
    KCOM_CHECK(junos_kcom_ifd_get_by_index(kcom_if[msprsmd_if].ifl_devindex,
                                          &kcom_ifd));
    /*
     * Bring back the results
     */
    if (fpc_slot) {
        *fpc_slot = kcom_ifd.ifdev_media_nic;
    }

    if (pic_slot) {
        *pic_slot = kcom_ifd.ifdev_media_pic;
    }

    return 0;
}


/**
 * @brief
 *     Gets ifl index by msprsmd interface index
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_kcom_ifl_by_idx (msprsmd_if_t msprsmd_if, ifl_idx_t *ifl_idx)
{
    if (msprsmd_if >= IF_MAX) {

        ERRMSG(JNX_MSPRSMD_KCOM_ERR, LOG_ERR, "%s :"
               "invalid msprsmd_if=%d", __func__, msprsmd_if);
        return ENOENT;
    }

    *ifl_idx = kcom_if[msprsmd_if].ifl_index;
    return 0;
}


/**
 * @brief
 *     Initializes kcom subsystem
 *
 * It reads the given config, closes old kcom session
 * if one was in place, and opens a new one.
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_kcom_init (evContext ctx,
                   msprsmd_config_t *msprsmd_config)
{
    provider_origin_id_t origin_id;
    int rc;
    
    msprsmd_kcom_shutdown();
    
    rc = provider_info_get_origin_id(&origin_id);
    
    if (rc) {
        ERRMSG(JNX_MSPRSMD_KCOM_ERR, LOG_ERR,
            "%s: Retrieving origin ID failed: %m", __func__);
        return rc;
    }

    /* init the kcom */
    KCOM_CHECK(junos_kcom_init(origin_id, ctx));

    KCOM_CHECK(msprsmd_kcom_read_config(msprsmd_config));

    junos_trace(JNX_MSPRSMD_TRACEFLAG_KCOM, "%s: "
                "kcom is ON", __func__);

    kcom_status = KCOM_STATUS_ON;
    return 0;
}


/**
 * @brief
 *      Initializes kcom switchover handler
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_kcom_start_switchover (void)
{
    /*
     * register the ifl_msg_handler
     */
    if (kcom_status == KCOM_STATUS_ACTIVE) {
        kcom_status = KCOM_STATUS_ON;
    }

    junos_trace(JNX_MSPRSMD_TRACEFLAG_KCOM, "%s: "
                "kcom starts switchover", __func__);

    KCOM_CHECK(msprsmd_kcom_switchover());

    KCOM_CHECK(junos_kcom_register_ifl_handler(NULL,
                      msprsmd_kcom_ifl_msg_handler));

    return 0;
}


/**
 * @brief
 *     Closes the kcom subsystem
 *
 *  Shuts kcom down - this will close all sessions
 *  with kcom message handlers assosiated with them.
 *
 */
void
msprsmd_kcom_shutdown (void)
{
    if (kcom_status == KCOM_STATUS_ON) {

        junos_kcom_shutdown ();
        junos_trace(JNX_MSPRSMD_TRACEFLAG_KCOM, "%s: "
                    "kcom is OFF", __func__);

        kcom_status = KCOM_STATUS_OFF;
    }
}

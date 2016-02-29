/*
 * $Id: ipprobe_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ipprobe_config.c
 * @brief Read probe configuration
 *
 * This code opens configuration database and loads probe configuration.
 *
 */

#include "../ipprobe-manager/ipprobe-manager.h"
#include "ipprobe.h"
#include "ddl/dax.h"

#include IPPROBE_SEQUENCE_H

extern int probe_start(void);

/*** Data Structures ***/

/** Probe manager TCP port. */
u_short manager_port = DEFAULT_MANAGER_PORT;

/** Probe parameter data structure. */
probe_params_t probe_params;

/**
 * The interface name through which, probe packets are transmitted
 * and received.
 */
char ifd_name[MAX_STR_LEN];

/** Username for authentication. */
char username[MAX_STR_LEN];

/** Password for authentication. */
char password[MAX_STR_LEN];

/*** STATIC/INTERNAL Functions ***/

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Initialize probe parameters to default value and
 * read probe configuration.
 *
 * @param[in] check
 *      1 if this function being invoked because of a commit check
 *
 * @return 0 on success, -1 on failure
 */
int
config_read (int check __unused)
{
    const char *probe_manager_config_path[] = {
            "sync", "ip-probe", "manager", NULL};
    const char *probe_config_path[] = {
            "sync", "ip-probe", "probe", NULL};
    const char *probe_interface_path[] = {
            "sync", "ip-probe", "interface", NULL};
    const char *probe_access_path[] = {
            "sync", "ip-probe", "access", NULL};
    ddl_handle_t *top = NULL;
    ddl_handle_t *dop = NULL;
    char probe_name[MAX_STR_LEN];
    char *p = NULL;
    int status = 0;

    /* Initialize all probe parameters. */
    ifd_name[0] = 0;
    username[0] = 0;
    password[0] = 0;
    manager_port = DEFAULT_MANAGER_PORT;
    probe_params.tos = DEFAULT_PACKET_TOS;
    probe_params.src_port = DEFAULT_PROBE_SRC_PORT;
    probe_params.dst_port = DEFAULT_PROBE_DST_PORT;
    probe_params.packet_size = DEFAULT_PACKET_SIZE;
    probe_params.packet_count = DEFAULT_PACKET_COUNT;
    probe_params.packet_interval = DEFAULT_PACKET_INTERVAL;

    /* Read probe manager port. */
    if (dax_get_object_by_path(NULL, probe_manager_config_path, &top,
            FALSE)) {
        dax_get_ushort_by_aid(top, PROBE_MANAGER_PORT, &manager_port);
    }

    /* Read interface name. */
    if (dax_get_object_by_path(NULL, probe_interface_path, &top, FALSE)) {
        dax_get_string_by_aid(top, PROBE_INTERFACE, &p);
        if (p) {
            strcpy(ifd_name, p);
        }
    }

    /* Read probe access information. */
    if (dax_get_object_by_path(NULL, probe_access_path, &top, FALSE)) {
        dax_get_string_by_aid(top, PROBE_USERNAME, &p);
        if (p) {
            strcpy(username, p);
        }
        dax_get_string_by_aid(top, PROBE_PASSWORD, &p);
        if (p) {
            strcpy(password, p);
        }
    }

    /* Read probe parameters. */
    if (!dax_get_object_by_path(NULL, probe_config_path, &top, FALSE)) {
        PRINT_ERROR("Read probe configuration!\n");
        status = -1;
        goto exit;
    }

    status = -1;
    while (dax_visit_container(top, &dop)) {

        if (!dax_get_stringr_by_aid(dop, PROBE_NAME,
                probe_name, sizeof(probe_name))) {
            PRINT_ERROR("Read probe name!\n");
            goto exit;
        }

        if (strcmp(probe_params.name, probe_name) == 0) {

            /* Protocol is mandatory. */
            if (!dax_get_ubyte_by_aid(dop, PROBE_PROTOCOL,
                    &probe_params.protocol)) {
                PRINT_ERROR("Read probe protocol!\n");
                goto exit;
            }

            /* The following parameters are optional. */
            dax_get_ubyte_by_aid(dop, PROBE_TOS, &probe_params.tos);
            dax_get_ushort_by_aid(dop, PROBE_SRC_PORT,
                    &probe_params.src_port);
            dax_get_ushort_by_aid(dop, PROBE_DST_PORT,
                    &probe_params.dst_port);
            dax_get_ushort_by_aid(dop, PACKET_SIZE,
                    &probe_params.packet_size);
            dax_get_ushort_by_aid(dop, PACKET_COUNT,
                    &probe_params.packet_count);
            dax_get_ushort_by_aid(dop, PACKET_INTERVAL,
                    &probe_params.packet_interval);

            status = 0;
            break;
        }
    }

    if (status < 0) {
        printf("Probe %s was not configured!\n", probe_name);
    }

exit:
    dax_release_object(&top);
    dax_release_object(&dop);

    if (status >= 0) {
        probe_start();
    }

    return status;
}


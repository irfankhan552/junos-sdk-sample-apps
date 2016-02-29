/*
 * $Id: jnx-exampled_config.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-exampled_config.c - read config functions.
 *
 * Copyright (c) 2005-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <string.h>
#include <sys/types.h>
#include <ddl/dax.h>
#include <sys/stat.h>

#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>

#include JNX_EXAMPLED_OUT_H
#include <jnx/junos_trace.h>

#include "jnx-exampled_config.h"

/******************************************************************************
 *                exampled config data management functions
 *****************************************************************************/
static patroot ex_data_root;
static u_int32_t ex_data_count; /* number of nodes in ex_data pat tree */
PATNODE_TO_STRUCT(ex_data_entry, jnx_exampled_data_t, exd_node)
const char *ex_config_name[] = { DDLNAME_JNX_EXAMPLE, NULL };

jnx_exampled_data_t *
ex_data_lookup (const char *ex_name)
{
    jnx_exampled_data_t *ex_data = NULL;
    if (ex_name) {
        ex_data = ex_data_entry(patricia_get(&ex_data_root,
                                strlen(ex_name) + 1, ex_name));
    }
    return ex_data;
}

static void
ex_data_free (jnx_exampled_data_t *ex_data)
{
    if (ex_data->exd_index) {
        free(ex_data->exd_index);
    }
    if (ex_data->exd_descr) {
        free(ex_data->exd_descr);
    }
    if (ex_data->exd_value) {
        free(ex_data->exd_value);
    }
    free(ex_data);
    return;
}

static void
ex_data_delete_entry (jnx_exampled_data_t *ex_data)
{
    junos_trace(JNX_EXAMPLED_TRACEFLAG_CONFIG, "%s: deleting ex_data %s",
                __FUNCTION__, ex_data->exd_index);

    /* remove ex_data from patricia tree */
    if (!patricia_delete(&ex_data_root, &ex_data->exd_node)) {
        ERRMSG(JNX_EXAMPLED_PATRICIA_ERROR, LOG_EMERG,
               "%s: patricia %s failure", __FUNCTION__, "delete");
    }

    /* free ex_data */
    ex_data_free(ex_data);
    ex_data_count--;
    return;
}

static jnx_exampled_data_t *
ex_data_add (const char *ex_name, const char *descr, const char *value,
             const u_int32_t type)
{
    jnx_exampled_data_t *ex_data;

    junos_trace(JNX_EXAMPLED_TRACEFLAG_CONFIG, "%s: adding ex_data %s",
                __FUNCTION__, ex_name);

    /* allocate & initialize ex_data */
    ex_data = calloc(1, sizeof(jnx_exampled_data_t));
    INSIST_ERR(ex_data != NULL);

    ex_data->exd_index = strdup(ex_name);
    INSIST_ERR(ex_data->exd_index != NULL);

    if (descr) {
        ex_data->exd_descr = strdup(descr);
        INSIST_ERR(ex_data->exd_descr != NULL);
    }
    if (value) {
        ex_data->exd_value = strdup(value);
        INSIST_ERR(ex_data->exd_value != NULL);
    }
    ex_data->exd_type = type;

    /* add to ex_data_root patricia tree */
    patricia_node_init_length(&ex_data->exd_node,
                              strlen(ex_data->exd_index) + 1);
    if (!patricia_add(&ex_data_root, &ex_data->exd_node)) {
        ERRMSG(JNX_EXAMPLED_PATRICIA_ERROR, LOG_EMERG,
               "%s: patricia %s failure", __FUNCTION__, "add");
    }
    ex_data_count++;
    return ex_data;
}

jnx_exampled_data_t *
ex_data_next (jnx_exampled_data_t *ex_data)
{
    return ex_data_entry(patricia_find_next(&ex_data_root,
                                           ex_data ? &ex_data->exd_node :
                                                     NULL));
}

jnx_exampled_data_t *
ex_data_first (void)
{
    return ex_data_entry(patricia_find_next(&ex_data_root, NULL));
}

static void
ex_data_clear (void)
{
    jnx_exampled_data_t *ex_data = NULL;

    while ((ex_data = ex_data_first()) != NULL) {
        ex_data_delete_entry(ex_data);
    }
    return;
}

void
ex_data_init (void)
{
    patricia_root_init(&ex_data_root, TRUE, EX_DATA_STR_SIZE, 0);
    return;
}


/******************************************************************************
 *                        Global Functions
 *****************************************************************************/

/******************************************************************************
 * Function : jnx_exampled_config_read
 * Purpose  : Read daemon configuration from the database
 * Inputs   : check - non-zero if configuration is to be checked only
 * Notes    : ERRMSG usage during config check is undefined.
 * Ret Vals : returns 0 on success, -1 on failure
 *****************************************************************************/

int
jnx_exampled_config_read (int check)
{
    ddl_handle_t *top = NULL;
    ddl_handle_t *dop = NULL;
    const char   *ex_data_config_name[] = { DDLNAME_JNX_EXAMPLE,
                                            DDLNAME_JNX_EXAMPLE_JNX_EXAMPLE_DATA,
                                            NULL };
    char          ex_name[EX_DATA_STR_SIZE];
    char          descr[EX_DATA_STR_SIZE];
    char          value[EX_DATA_STR_SIZE];
    u_int32_t     type;
    int           status = 0;

    /*
     * read example data configuration
     */
    if (dax_get_object_by_path(NULL, ex_data_config_name, &top, FALSE)) {

        if (dax_is_changed(top) == FALSE) {
            /* if top hasn't changed, ignore this config object */
            goto exit;
        }

        /*
         * Need not manipulate local data when configuration is being 
         * checked
         */
        if (check == 0) {
            /* clear stored config data, & repopulate with new config data */
            ex_data_clear();
        }

        while (status == 0 && dax_visit_container(top, &dop)) {
            if (dax_get_stringr_by_aid(dop, EX_DATA_NAME, ex_name,
                                       sizeof(ex_name) ) == FALSE) {
                /* this attribute is mandatory -- got to have an index */
                status = ENOENT;
            }

            if (dax_get_stringr_by_aid(dop, EX_DATA_DESC, descr,
                                       sizeof(descr)) == FALSE) {
                /* if not configured, set the value to NULL */
                descr[0] = '\0';
            }

            if (dax_get_stringr_by_aid(dop, EX_DATA_VALUE, value,
                                       sizeof(value)) == FALSE) {
                /* if not configured, set the value to NULL */
                value[0] = '\0';
            }

            if (dax_get_uint_by_aid(dop, EX_DATA_TYPE, &type) == 0) {
                /*
                 * this value has a default defined, so there must be
                 * something configured
                 */
                status = ENOENT;
            }

            if (status == 0) {
                /*
                 * Need not manipulate local data when configuration is being 
                 * checked
                 */
                if (check == 0) {
                    /*
                     * add ex_data to patricia tree
                     */
                    ex_data_add(ex_name, descr, value, type);
                }
            }
        }
    } else {
        /*
         * No configuration, clear any stored configuration data
         */
        if (check == 0) {
            ex_data_clear();
        }
    }

exit:
    if (top) {
        dax_release_object(&top);
    }

    if (dop) {
        dax_release_object(&dop);
    }
    return status;
}

/* end of file */

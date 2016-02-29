/*
 * $Id: helloworldd_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file helloworldd_config.c
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions and variables will parse, load, hold, and return
 * the configuration data.
 */

#include <ddl/dax.h>
#include <jnx/patricia.h>
#include <jnx/junos_trace.h>
#include "helloworldd_config.h"

#include HELLOWORLD_OUT_H

/*** Data Structures ***/


static patroot root; ///< patricia tree root


/*** STATIC/INTERNAL Functions ***/


/**
 * Generate the data_entry (static inline) function for internal usage only:
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(data_entry, helloworld_data_t, node)


/**
 * Remove a message from the configuration...used only internally
 * when emptying the patricia tree (in clear_messages)
 * 
 * @param[in] data
 *     Message/Data to delete from the configuration data storage
 * 
 * @return SUCCESS or EFAIL on failure to delete (from patricia tree)
 */
static int
delete_message (helloworld_data_t *data)
{
    /* remove data from patricia tree */
    if (!patricia_delete(&root, &data->node)) {
        free(data);
        return EFAIL;
    }

    free(data);
    return SUCCESS;
}


/**
 * Add a message to the configuration data in the patricia tree
 * 
 * @param message
 *      the message to add
 * 
 * @return pointer to the new data entry added or NULL on error
 */
static helloworld_data_t *
add_message (const char * message)
{
    helloworld_data_t *data;

    /* allocate & initialize data */
    data = calloc(1, sizeof(helloworld_data_t)); /* calloc zeros mem */
    INSIST_ERR(data != NULL); /* check that allocation worked */
    INSIST_ERR(strlen(message) < MESSAGE_STR_SIZE); /* check for length */
    
    strcpy(data->message, message);

    /* add to data_root patricia tree */
    patricia_node_init_length(&data->node,
                              strlen(data->message) + 1);
                              
    if (!patricia_add(&root, &data->node)) {
        free(data);
        return NULL;
    }

    return data;
}


/**
 * Clear the configuration info completely from the patricia tree
 * 
 * @return SUCCESS or EFAIL on failure to delete some contents
 */
static int
clear_messages (void)
{
    helloworld_data_t *data = NULL;

    while ((data = first_message()) != NULL) {
        if(delete_message(data) == EFAIL) {
            return EFAIL;
        }
    }
    return SUCCESS;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structure that will store configuration info,
 * or in other words, the message(s)
 */
void
init_messages_ds (void)
{
    patricia_root_init(&root, FALSE, MESSAGE_STR_SIZE, 0);
}


/**
 * Get the first of the messages that this daemon stores
 * 
 * @return pointer to the first data entry if one exists, o/w NULL
 */
helloworld_data_t *
first_message (void)
{
    return data_entry(patricia_find_next(&root, NULL));
}


/**
 * Get the next message that this daemon stores given the 
 * previously returned data (from first_message or next_message)
 * 
 * @param data
 *      previously return data
 * 
 * @return pointer to the first data entry if one exists, o/w NULL
 *****************************************************************************/
helloworld_data_t *
next_message (helloworld_data_t * data)
{
    return data_entry(patricia_find_next(&root, (data ? &(data->node) : NULL)));
}


/**
 * Read daemon configuration from the database
 * 
 * @param[in] check
 *     1 if this function being invoked because of a commit check
 * 
 * @return SUCCESS (0) if successfully loaded; otherwise EFAIL (1)
 * 
 * @note Do not use ERRMSG during config check.
 */
int
helloworld_config_read (int check __unused)
{

    ddl_handle_t  * dop = NULL; /* DDL Object Pointer */
    const char    * hw_data_config_name[] = 
                                            {DDLNAME_SYNC,
                                             DDLNAME_SYNC_HELLOWORLD,
                                             DDLNAME_SYNC_HELLOWORLD_MESSAGE,
                                             NULL};
    char          hw_msg[MESSAGE_STR_SIZE];
    int           status = SUCCESS;

    /*
     * read helloworldd data configuration
     */
    if (dax_get_object_by_path(NULL, hw_data_config_name, &dop, FALSE)) {

        if (dax_is_changed(dop)) {
            /* if message has changed */

            /* clear stored config data, & repopulate with new config data */
            if((status = clear_messages()) == EFAIL) {
                dax_error(dop, "Failed to clear stored configuration");
            }
    
            if (dax_get_stringr_by_aid(dop, HELLOWORLD_MESSAGE_CONTENTS, 
                    hw_msg, sizeof(hw_msg))) {
    
                if(add_message(hw_msg) == NULL) { /* add this message */
                    dax_error(dop, "Failed to add message to configuration");
                    status = EFAIL;
                }
    
            } else {
                dax_error(dop, "Couldn't read message from configuration");
                status = EFAIL;
            }
        }
    } else {
        /* clear stored config data because we have no configure data */
        if((status = clear_messages()) == EFAIL) {
            dax_error(dop, "Failed to clear stored configuration");
        }
    }

    if (dop) {
        dax_release_object(&dop);
    }
    
    return status;
}



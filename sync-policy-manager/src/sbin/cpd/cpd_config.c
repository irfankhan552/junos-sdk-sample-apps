/*
 * $Id: cpd_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file cpd_config.c
 * @brief Relating to getting and setting the configuration data 
 *        coming from the HTTP Server
 * 
 * These functions and variables will store and provide access to the 
 * configuration data which is the set of authorized users.
 */

#include <string.h>
#include "cpd_config.h"
#include "cpd_logging.h"

/*** Constants ***/


/*** Data structures: ***/

static in_addr_t pfd_address;  ///< PFD's address in network-byte order
static in_addr_t cpd_address;  ///< CPD's address in network-byte order
static patroot root; ///< patricia tree root


/*** STATIC/INTERNAL Functions ***/


/**
 * Generate the data_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(data_entry, cpd_auth_user_t, node)


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structure that will store configuration info,
 * or in other words, the set of authorized users
 */
void
init_config(void)
{
    patricia_root_init(&root, FALSE, sizeof(in_addr_t), 0);
                   // root, is key ptr, key size, key offset
    
    cpd_address = 0;
    pfd_address = 0;
}

/**
 * Clear the configuration data
 */
void
clear_config(void)
{
    cpd_auth_user_t * auth_user = NULL;

    while((auth_user = get_next_auth_user(NULL)) != NULL) {

        if (!patricia_delete(&root, &auth_user->node)) {
            LOG(LOG_ERR, "%s: patricia delete failed", __func__);
        }

        if(auth_user) {
            free(auth_user);
        }
    }
}


/**
 * Add an authorized user from the configured set
 * 
 * @param[in] addr
 *      The user's IP address in network byte order
 */
void
add_auth_user_addr(in_addr_t addr)
{
    cpd_auth_user_t * auth_user;
    
    auth_user = calloc(1, sizeof(cpd_auth_user_t));
    auth_user->address = addr; 
    
    /* add to data_root patricia tree */
    patricia_node_init_length(&auth_user->node, sizeof(in_addr_t));

    if (!patricia_add(&root, &auth_user->node)) {
        LOG(LOG_ERR, "%s: patricia_add failed", __func__);
        free(auth_user);
    }
}


/**
 * Delete an authorized user from the configured set
 * 
 * @param[in] addr
 *      The user's IP address in network byte order
 */
void
delete_auth_user_addr(in_addr_t addr)
{
    patnode * node = NULL;
    cpd_auth_user_t * auth_user = NULL;
        
    node = patricia_get(&root, sizeof(in_addr_t), &addr);

    if (node == NULL) {
        LOG(LOG_NOTICE, "%s: called with a user's that is already not "
            "authorized", __func__);
        return;
    }
    
    auth_user = data_entry(node);
        
    if (!patricia_delete(&root, node)) {
        LOG(LOG_ERR, "%s: patricia delete failed", __func__);
    }
    
    if(auth_user) {
        free(auth_user);
    }
}


/**
 * Is a user authorized
 * 
 * @param[in] addr
 *      The user's IP address in network byte order
 * 
 * @return TRUE if authorized or FALSE otherwise
 */
boolean
is_auth_user(in_addr_t addr)
{
    return (patricia_get(&root, sizeof(in_addr_t), &addr) != NULL);
}

/**
 * Get the next authorized user
 * 
 * @param[in] user
 *      The user returned from the previous call to this function or
 *      NULL if it's the first call
 * 
 * @return
 *      The next user or NULL if the set of users is empty
 */
cpd_auth_user_t * get_next_auth_user(cpd_auth_user_t * user)
{
    return data_entry(patricia_find_next(&root, (user ? &(user->node) : NULL)));
}


/**
 * Get the PFD address
 * 
 * @return
 *      The PFD address in network-byte order
 */
in_addr_t
get_pfd_address(void)
{
    return pfd_address;
}


/**
 * Set the PFD address
 * 
 * @param[in] addr
 *      The new PFD address in network-byte order
 */
void
set_pfd_address(in_addr_t addr)
{
    pfd_address = addr;
}


/**
 * Get the CPD address
 * 
 * @return
 *      The CPD address in network-byte order
 */
in_addr_t
get_cpd_address(void)
{
    return cpd_address;
}


/**
 * Set the CPD address
 * 
 * @param[in] addr
 *      The new CPD address in network-byte order
 */
void
set_cpd_address(in_addr_t addr)
{
    cpd_address = addr;
}


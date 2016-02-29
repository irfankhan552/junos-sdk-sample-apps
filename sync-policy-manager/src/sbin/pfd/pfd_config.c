/*
 * $Id: pfd_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file pfd_config.c
 * @brief Relating to getting and setting the configuration data 
 *        coming from the CPD
 * 
 * These functions and variables will store and provide access to the 
 * configuration data which is the set of authorized users.
 */

#include <string.h>
#include <pthread.h>
#include <jnx/atomic.h>
#include "pfd_config.h"
#include "pfd_logging.h"

/*** Constants ***/


/*** Data structures: ***/

/*
 * These are maintained as a fast pre-filter sort of like counting filters.
 * Indexes are the bytes in the 1st, 2nd, 3rd, and 4th authorized address.
 * Values are the counters for number of authorized user addresses that contain
 * the index's value.
 * 
 * so first_byte[i] = j <=> 
 *    address pattern i.*.*.* appears in j authorized users' addresses
 */
static uint32_t first_byte [256]; ///< first-byte counters for pre-filtering
static uint32_t second_byte[256]; ///< second-byte counters for pre-filtering
static uint32_t third_byte [256]; ///< third-byte counters for pre-filtering
static uint32_t fourth_byte[256]; ///< fourth-byte counters for pre-filtering

static in_addr_t pfd_address;  ///< PFD's address in network-byte order
static in_addr_t cpd_address;  ///< CPD's address in network-byte order

static patroot root; ///< patricia tree root
static pthread_rwlock_t config_lock; ///< tree lock

/*
 * Multiple dataloop threads will access this PATRICIA Tree for reading, and 
 * the mgmt thread will do some writing.
 * 
 * Only the mgmt thread reads and writes to the pfd/cpd addresses.
 * These get passed to the threads in a messaging fashion instead.
 */

/**
 * The current cached time. Cache it to prevent many system time() calls by the
 * data threads.
 */
static atomic_uint_t current_time;

/*** STATIC/INTERNAL Functions ***/

/**
 * Generate the data_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(data_entry, pfd_auth_user_t, node)


/**
 * Setup the connection to the CPD
 * 
 * @param[in] ctx
 *     The event context for this application
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] due
 *     The absolute time when the event is due (now)
 * 
 * @param[in] inter
 *     The period; when this will next be called 
 */
static void
update_time(evContext ctx __unused,
            void * uap  __unused,
            struct timespec due __unused,
            struct timespec inter __unused)
{
    atomic_add_uint(1, &current_time);
}

/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structure that will store configuration info,
 * or in other words, the set of authorized users
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_config(evContext ctx)
{
    patricia_root_init(&root, FALSE, sizeof(in_addr_t), 0);
                   // root, is key ptr, key size, key offset
    
    pthread_rwlock_init(&config_lock, NULL);

    bzero(&first_byte, sizeof(first_byte));
    bzero(&second_byte, sizeof(second_byte));
    bzero(&third_byte, sizeof(third_byte));
    bzero(&fourth_byte, sizeof(fourth_byte));
    
    pfd_address = 0;
    cpd_address = 0;
    
    current_time = (atomic_uint_t)(time(NULL) - 1);
    
    // cached system time
    if(evSetTimer(ctx, update_time, NULL,
        evNowTime(), evConsTime(1, 0), NULL)) {

        LOG(LOG_EMERG, "%s: Failed to initialize an eventlib timer to generate "
            "the cached time", __func__);
        return EFAIL;
    }
    
    return SUCCESS;
}

/**
 * Clear the configuration data
 */
void
clear_config(void)
{
    patnode * node = NULL;
    pfd_auth_user_t * auth_user = NULL;
    
    pthread_rwlock_wrlock(&config_lock);
    
    while((node = patricia_find_next(&root, NULL)) != NULL) {

        auth_user = data_entry(node);

        if (!patricia_delete(&root, node)) {
            LOG(LOG_ERR, "%s: patricia delete failed", __func__);
        }

        if(auth_user) {
            free(auth_user);
        }
    }
    
    bzero(&first_byte, sizeof(first_byte));
    bzero(&second_byte, sizeof(second_byte));
    bzero(&third_byte, sizeof(third_byte));
    bzero(&fourth_byte, sizeof(fourth_byte));
    
    pthread_rwlock_unlock(&config_lock);
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
    pfd_auth_user_t * auth_user;
    const uint8_t const * byte = (const uint8_t const *)&addr;
    
    auth_user = calloc(1, sizeof(pfd_auth_user_t));
    auth_user->address = addr; 
    
    /* add to data_root patricia tree */
    patricia_node_init_length(&auth_user->node, sizeof(in_addr_t));

    pthread_rwlock_wrlock(&config_lock);
    
    if (!patricia_add(&root, &auth_user->node)) {
        LOG(LOG_ERR, "%s: patricia_add failed", __func__);
        free(auth_user);
        pthread_rwlock_unlock(&config_lock);
        return;
    }
    
    // build pre-filter arrays
    ++first_byte[byte[0]];
    ++second_byte[byte[1]];
    ++third_byte[byte[2]];
    ++fourth_byte[byte[3]];
    
    pthread_rwlock_unlock(&config_lock);
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
    pfd_auth_user_t * auth_user = NULL;
    const uint8_t const * byte = (const uint8_t const *)&addr;
    
    pthread_rwlock_wrlock(&config_lock);
        
    node = patricia_get(&root, sizeof(in_addr_t), &addr);
    
    if (node == NULL) {
        LOG(LOG_NOTICE, "%s: called with a user's that is already not "
            "authorized", __func__);
        pthread_rwlock_unlock(&config_lock);
        return;
    }
    
    auth_user = data_entry(node);
    
    if (!patricia_delete(&root, node)) {
        LOG(LOG_ERR, "%s: patricia delete failed", __func__);
        pthread_rwlock_unlock(&config_lock);
        return;
    }
    
    if(auth_user) {
        free(auth_user);
    }
    
    // build pre-filter arrays
    --first_byte[byte[0]];
    --second_byte[byte[1]];
    --third_byte[byte[2]];
    --fourth_byte[byte[3]];
    
    pthread_rwlock_unlock(&config_lock);
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
    const uint8_t const * byte = (const uint8_t const *)&addr;
    patnode * p = NULL;
    
    pthread_rwlock_rdlock(&config_lock);
    
    // very fast and efficient pre-filter
    if(first_byte[byte[0]] && second_byte[byte[1]] &&
       third_byte[byte[2]] && fourth_byte[byte[3]]) {
        
        p = patricia_get(&root, sizeof(in_addr_t), &addr);
    }
    
    pthread_rwlock_unlock(&config_lock);
    
    return (p != NULL);
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


/**
 * Get the currently cached time
 * 
 * @return
 *      Current time
 */
time_t
get_current_time(void)
{
    return (time_t)current_time;
}

/*
 * $Id: helloworldd_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file helloworldd_config.h
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions and variables will parse, load, hold, and return
 * the configuration data.
 */
 
#ifndef __HELLOWORLDD_CONFIG_H__
#define __HELLOWORLDD_CONFIG_H__

#include <jnx/patricia.h>

#define MESSAGE_STR_SIZE 128 ///< size of our strings herein

/**
 * The structure we use to store our configuration 
 * information in the patricia tree:
 */
typedef struct helloworld_data_s {
    patnode    node;                          ///< Tree node
    char       message[MESSAGE_STR_SIZE];     ///< Message
} helloworld_data_t;


/**
 * Init the data structure that will store configuration info,
 * or in other words, the message
 */
void init_messages_ds(void);


/**
 * Get the first message this daemon stores from its configuration data
 * 
 * @return the first message in storage
 */
helloworld_data_t * first_message(void);


/**
 * Get (iterator-style) the next condition that this daemon stores
 *
 * @param[in] data
 *     The previously returned data from this function or
 *     NULL for the first condition
 * 
 * @return the next condition in storage
 */
helloworld_data_t * next_message(helloworld_data_t * data);


/**
 * Read daemon configuration from the database
 * 
 * @param[in] check
 *     1 if this function being invoked because of a commit check?
 * 
 * @return SUCCESS (0) if successfully loaded; otherwise EFAIL (1)
 * 
 * @note Do not use ERRMSG during config check.
 */
int helloworld_config_read(int check);

#endif


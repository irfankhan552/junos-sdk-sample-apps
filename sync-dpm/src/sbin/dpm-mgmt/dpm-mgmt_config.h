/*
 * $Id: dpm-mgmt_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-mgmt_config.h
 * @brief Relating to loading the configuration data
 * 
 * These functions will parse and load the configuration data.
 */
 
#ifndef __DPM_MGMT_CONFIG_H__
#define __DPM_MGMT_CONFIG_H__

#include <jnx/patricia.h>
#include <sync/dpm_ipc.h>

/*** Constants ***/


/*** Data structures ***/

typedef TAILQ_HEAD(cl_list_s, login_class_s) class_list_t; ///< list typedef

/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each policer
 */
typedef struct policer_s {
    patnode        node;         ///< Tree node
    policer_info_t policer;      ///< policer
} policer_t;


/**
 * The structure we use to bundle the list pointers with the data 
 * to store for each interface default
 */
typedef struct int_def_s {
    char     name_pattern[MAX_INT_NAME + 1];  ///< interface name pattern
    char     input_pol[MAX_POL_NAME + 1];     ///< input policer name
    char     output_pol[MAX_POL_NAME + 1];    ///< output policer name
    
    TAILQ_ENTRY(int_def_s) entries;           ///< ptrs to next/prev
} int_def_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each subscriber
 */
typedef struct subscriber_s {
    patnode    node;        ///< Tree node
    sub_info_t subscriber;  ///< subscriber
} subscriber_t;

/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each subscriber
 */
typedef struct login_sub_s {
    char name[MAX_SUB_NAME];            ///< subscriber name
    TAILQ_ENTRY(login_sub_s) entries; ///< ptrs to next/prev classes
} login_sub_t;

/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each subscriber
 */
typedef struct login_class_s {
    char class[MAX_CLASS_NAME];         ///< class name
    TAILQ_HEAD(, login_sub_s) subs;     ///< List of subscribers
    TAILQ_ENTRY(login_class_s) entries; ///< ptrs to next/prev classes
} login_class_t;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 */
void
init_config(void);


/**
 * Clear and reset the entire configuration, freeing all memory.
 */
void
clear_config(void);


/**
 * Clear status of all logins
 */
void
clear_logins(void);


/**
 * Send the entire configuration to the ctrl component
 */
void
send_configuration(void);


/**
 * Record the subscriber login
 * 
 * @param[in] s_name
 *     subscriber name
 * 
 * @param[in] c_name
 *     subscriber's class name
 */
void
subscriber_login(char * s_name, char * c_name);

/**
 * Remove the record of the previous subscriber login
 * 
 * @param[in] s_name
 *     subscriber name
 * 
 * @param[in] c_name
 *     subscriber's class name
 */
void
subscriber_logout(char * s_name, char * c_name);


/**
 * Read daemon configuration from the database
 * 
 * @param[in] check
 *     1 if this function being invoked because of a commit check
 * 
 * @return SUCCESS (0) successfully loaded, EFAIL if not
 * 
 * @note Do not use ERRMSG/LOG during config check normally.
 */
int
dpm_config_read(int check);


#endif


/*
 * $Id: monitube2-mgmt_config.h 347265 2009-11-19 13:55:39Z kdickman $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2009, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file monitube2-mgmt_config.h
 * @brief Relating to loading the configuration data
 * 
 * These functions will parse and load the configuration data.
 */
 
#ifndef __MONITUBE2_MGMT_CONFIG_H__
#define __MONITUBE2_MGMT_CONFIG_H__

#include <jnx/patricia.h>

/*** Constants ***/

#define STATS_BASE_WINDOW  60  ///< Window of X seconds to use for MDI stats 

#define MONITUBE2_MGMT_STRLEN 256  ///< Application name length

/*** Data structures ***/

/**
 * A list item
 */
typedef struct list_item_s {
    void * item; ///< generic item
    TAILQ_ENTRY(list_item_s) entries; ///< ptrs to next/prev items
} list_item_t;


typedef TAILQ_HEAD(list_s, list_item_s) list_t; ///< list typedef


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each piece of service-set info
 */
typedef struct ss_info_s {
    patnode    node;       ///< Tree node in ssets
    char       name[MONITUBE2_MGMT_STRLEN];  ///< service set name
    char       es[MONITUBE2_MGMT_STRLEN];    ///< extension service name
    uint16_t   id;                           ///< service set id number
    uint16_t   fpc_slot;                     ///< PIC info: where it is applied
    uint16_t   pic_slot;                     ///< PIC info: where it is applied
    evTimerID  timer;                        ///< notify timer
    u_int32_t  gen_num;                      ///< generation number
    u_int32_t  svc_id;                       ///< service id
    list_t     rules;                        ///< list of rules
} ss_info_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each destination address/prefix
 */
typedef struct address_s {
    patnode    node;       ///< Tree node in addresses of rule_t
    in_addr_t  address;    ///< monitor name
    in_addr_t  mask;       ///< mask
} address_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each rule
 */
typedef struct rule_s {
    patnode    node;                ///< Tree node in rules
    char       name[MONITUBE2_MGMT_STRLEN];  ///< rule name
    uint32_t   rate;                ///< monitoring rate
    in_addr_t  redirect;            ///< mirror to address
    patroot *  addresses;           ///< "from" dest addresses
    list_t     ssets;               ///< service sets that apply this rule
} rule_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each flow statistic
 */
typedef struct flowstat_ss_s {
    patnode     node;       ///< Tree node in flow_stats
    uint16_t    ssid;       ///< fpc-slot number where flow was seen
    patroot *   stats;      ///< tree of flowstat_t's nodes
} flowstat_ss_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each flow statistic
 */
typedef struct flowstat_s {
    patnode     node;       ///< Tree node in stats of a flowstat_ss_t
    
    uint16_t    fpc_slot;   ///< fpc-slot number where flow was seen
    uint16_t    pic_slot;   ///< pic-slot number where flow was seen
    in_addr_t   flow_addr;  ///< flow's dest address
    uint16_t    flow_port;  ///< flow's dest port

    evTimerID   timer_id;   ///< timer for aging
    uint32_t    reports;    ///< MDI reports received
    uint16_t    window_position;                ///< Current index to record MDI
    double      last_mdi_df[STATS_BASE_WINDOW]; ///< Last X MDI Delay factors
    uint32_t    last_mdi_mlr[STATS_BASE_WINDOW];///< Last X MDI Media loss rates
    
    flowstat_ss_t * ssi; // service set info about which tree it is in
} flowstat_t;


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 */
void
init_config(evContext ctx);


/**
 * Clear and reset the entire configuration, freeing all memory.
 */
void
clear_config(void);

/**
 * Clear all statistics
 */
void
clear_stats(void);


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
monitube_config_read(int check);


/**
 * Get the next rule in configuration given the previously returned data
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to a rule if one more exists, o/w NULL
 */
rule_t *
next_rule(rule_t * data);


/**
 * Given a rule, get the next address in configuration given the previously
 * returned data
 *
 * @param[in] rule
 *      Rule's configuration
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to an address if one more exists, o/w NULL
 */
address_t *
next_address(rule_t * rule, address_t * data);


/**
 * Get the next service set in configuration given the previously returned data
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to a service set if one more exists, o/w NULL
 */
ss_info_t *
next_serviceset(ss_info_t * data);


/**
 * Get the next flowstat service set in configuration given the previously
 * returned data
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to a flowstat if one more exists, o/w NULL
 */
flowstat_ss_t *
next_flowstat_ss(flowstat_ss_t * data);

/**
 * Get the next flowstat record in configuration given the previously
 * returned data
 *
 * @param[in] fss
 *      A flow stats service set 
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to a flowstat if one more exists, o/w NULL
 */
flowstat_t *
next_flowstat(flowstat_ss_t * fss, flowstat_t * data);


/**
 * Given a monitor's name and flow address add or update its statistics record
 *
 * @param[in] fpc_slot
 *      FPC slot #
 *
 * @param[in] pic_slot
 *      PIC slot #
 *
 * @param[in] ssid
 *      Service set id
 *
 * @param[in] flow_addr
 *      flow address (ID)
 *
 * @param[in] flow_port
 *      flow port (ID)
 *
 * @param[in] mdi_df
 *      MDI delay factor
 *
 * @param[in] mdi_mlr
 *      MDI media loss rate
 */
void
set_flow_stat(uint16_t fpc_slot,
              uint16_t pic_slot,
              uint16_t ssid,
              in_addr_t flow_addr,
              uint16_t flow_port,
              double mdi_df,
              uint32_t mdi_mlr);


/**
 * Clear all flow statistics records
 */
void
clear_all_flowstats(void);


#endif


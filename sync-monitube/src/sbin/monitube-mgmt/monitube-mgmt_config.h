/*
 * $Id: monitube-mgmt_config.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-mgmt_config.h
 * @brief Relating to loading the configuration data
 * 
 * These functions will parse and load the configuration data.
 */
 
#ifndef __MONITUBE_MGMT_CONFIG_H__
#define __MONITUBE_MGMT_CONFIG_H__

#include <jnx/patricia.h>

/*** Constants ***/

#define STATS_BASE_WINDOW  60  ///< Window of X seconds to use for MDI stats 

#define MAX_MON_NAME  256  ///< Application name length

/*** Data structures ***/

/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each monitor's network address
 */
typedef struct address_s {
    patnode    node;       ///< Tree node in addresses of monitor_t
    in_addr_t  address;    ///< monitor name
    in_addr_t  mask;       ///< mask
} address_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each monitor
 */
typedef struct monitor_s {
    patnode    node;                ///< Tree node in monitor_conf
    char       name[MAX_MON_NAME];  ///< monitor name
    uint32_t   rate;                ///< rate
    patroot *  addresses;           ///< addresses patricia tree
    patroot *  flow_stats;          ///< flow stats patricia tree
} monitor_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each mirror
 */
typedef struct mirror_s {
    patnode    node;       ///< Tree node in mirror_conf
    in_addr_t  original;   ///< mirror from address
    in_addr_t  redirect;   ///< mirror to address
} mirror_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each flow statistic
 */
typedef struct flowstat_s {
    patnode     node;       ///< Tree node in flow_stats of monitor_t
    /* Identifies where the flow was measured */
    uint16_t    fpc_slot;   ///< fpc-slot number where flow was seen
    uint16_t    pic_slot;   ///< pic-slot number where flow was seen
    /* Identifies the flow */
    in_addr_t   flow_addr;  ///< flow's dest address
    uint16_t    flow_port;  ///< flow's dest port
    evTimerID   timer_id;   ///< timer for aging
    monitor_t * mon;        ///< monitor this belongs to
    uint32_t    reports;    ///< MDI reports received
    uint16_t    window_position;                ///< Current index to record MDI
    double      last_mdi_df[STATS_BASE_WINDOW]; ///< Last X MDI Delay factors
    uint32_t    last_mdi_mlr[STATS_BASE_WINDOW];///< Last X MDI Media loss rates
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
 * Get the current replication_interval
 * 
 * @return the replication interval
 */
uint8_t
get_replication_interval(void);


/**
 * Get the next monitor in configuration given the previously returned data
 * 
 * @param[in] data
 *      previously returned data, should be NULL first time 
 * 
 * @return pointer to a monitor if one more exists, o/w NULL
 */
monitor_t *
next_monitor(monitor_t * data);


/**
 * Get the next mirror in configuration given the previously returned data
 * 
 * @param[in] data
 *      previously returned data, should be NULL first time 
 * 
 * @return pointer to a mirror if one more exists, o/w NULL
 */
mirror_t *
next_mirror(mirror_t * data);


/**
 * Given a monitor, get the next address in configuration given the previously
 * returned data
 * 
 * @param[in] mon
 *      Monitor's configuration 
 * 
 * @param[in] data
 *      previously returned data, should be NULL first time 
 * 
 * @return pointer to an address if one more exists, o/w NULL
 */
address_t *
next_address(monitor_t * mon, address_t * data);


/**
 * Given a monitor, get the next flowstat in configuration given the previously
 * returned data
 * 
 * @param[in] mon
 *      Monitor's configuration 
 * 
 * @param[in] data
 *      previously returned data, should be NULL first time 
 * 
 * @return pointer to a flowstat if one more exists, o/w NULL
 */
flowstat_t *
next_flowstat(monitor_t * mon, flowstat_t * data);


/**
 * Get the monitor configuration information by name
 * 
 * @param[in] name
 *      Monitor name
 * 
 * @return The monitor if one exists with the matching name, o/w NULL
 */
monitor_t *
find_monitor(char * name);


/**
 * Given a monitor's name and flow address add or update its statistics record
 *
 * @param[in] fpc_slot
 *      FPC slot #
 *
 * @param[in] pic_slot
 *      PIC slot #
 *
 * @param[in] name
 *      Monitor's name
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
              const char * name,
              in_addr_t flow_addr,
              uint16_t flow_port,
              double mdi_df,
              uint32_t mdi_mlr);


/**
 * Clear all flow statistics records
 */
void
clear_all_flowstats(void);


/**
 * Clear all flow statistics records associated with a monitor
 * 
 * @param[in] mon_name
 *      Monitor's name
 * 
 * @return 0 upon success;
 *        -1 if there's no monitor configured with the given name
 */
int
clear_flowstats(char * mon_name);


#endif


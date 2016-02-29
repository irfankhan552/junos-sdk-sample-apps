/*
 * $Id: monitube-data_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-data_config.c
 * @brief Relating to getting and setting the configuration data
 *
 *
 * These functions will store and provide access to the
 * configuration data which is essentially coming from the mgmt component.
 */

#include "monitube-data_main.h"
#include "monitube-data_config.h"
#include "monitube-data_ha.h"
#include "monitube-data_packet.h"
#include <jnx/radix.h>

/*** Constants ***/

#define MAX_MON_NAME_LEN  256     ///< Application name length

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
    patnode    node;                   ///< Tree node in monitor_conf
    char       name[MAX_MON_NAME_LEN]; ///< monitor name
    uint32_t   rate;                   ///< rate
    patroot *  addresses;              ///< addresses patricia tree
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
 * The structure we use to bundle the radix-tree node with a pointer to the
 * configuration we need depending on the tree (a monitor or mirror)
 */
typedef struct prefix_s {
    rnode_t     rnode;        ///< Radix tree node in mon/mir_prefixes
    in_addr_t   prefix;       ///< prefix
    monitor_t * monitor;      ///< pointer to a monitor
} prefix_t;


volatile boolean is_master; ///< mastership state of this data component

static evContext       m_ctx;         ///< event context
static patroot         monitors_conf; ///< Pat. root for monitors config
static patroot         mirrors_conf;  ///< Pat. root for mirrors config
static radix_root_t    mon_prefixes;  ///< Radix tree for monitor lookups
static uint8_t         replication_interval;   ///< replication interval

static msp_spinlock_t  mir_conf_lock; ///< lock for mirrors_conf config
static msp_spinlock_t  mon_conf_lock; ///< lock for mon_prefixes config


/**
 * The current cached time. Cache it to prevent many system time() calls by the
 * data threads.
 */
static atomic_uint_t current_time;


/*** STATIC/INTERNAL Functions ***/

/**
 * Generate the mon_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(mon_entry, monitor_t, node)


/**
 * Generate the mir_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(mir_entry, mirror_t, node)


/**
 * Generate the address_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(address_entry, address_t, node)


/**
 * Update cached time
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


/**
 * Count the number of bits in a uint32_t / in_addr_t
 *
 * @param[in] n
 *      The input (mask) that we want the number of bits for
 *
 * @return the number of 1 bits
 */
static uint8_t
bit_count(uint32_t n)
{
    register unsigned int tmp;

    tmp = n - ((n >> 1) & 033333333333)
            - ((n >> 2) & 011111111111);
    return ((tmp + (tmp >> 3)) & 030707070707) % 63;
}


/**
 * Delete all address from a monitor's configuration
 *
 * @param[in] mon
 *      The monitor's configuration
 */
static void
delete_all_addresses(monitor_t * mon)
{
    address_t * add = NULL;
    rnode_t * rn;

    while(NULL != (add =
        address_entry(patricia_find_next(mon->addresses, NULL)))) {

        if(!patricia_delete(mon->addresses, &add->node)) {
            LOG(LOG_ERR, "%s: Deleting address failed", __func__);
        }

        INSIST_ERR(msp_spinlock_lock(&mon_conf_lock) == MSP_OK);

        rn = radix_get(&mon_prefixes,
                bit_count(add->mask), (uint8_t *)&add->address);

        if(rn == NULL) {
            LOG(LOG_ERR, "%s: Failed to find monitored prefix to remove from "
                    "monitor's rtree configuration", __func__);
        } else {
            radix_delete(&mon_prefixes, rn);
        }


        INSIST_ERR(msp_spinlock_unlock(&mon_conf_lock) == MSP_OK);

        free(add);
    }
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
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
    m_ctx = ctx;

    is_master = FALSE;

    msp_spinlock_init(&mir_conf_lock);
    msp_spinlock_init(&mon_conf_lock);

    patricia_root_init(&monitors_conf, FALSE, MAX_MON_NAME_LEN, 0);
    patricia_root_init(&mirrors_conf, FALSE, sizeof(in_addr_t), 0);

    radix_init_root(&mon_prefixes);

    current_time = (atomic_uint_t)(time(NULL) - 1);
    replication_interval = 10; // default, but by the time it is used, it is set

    // cached system time
    if(evSetTimer(ctx, update_time, NULL, evNowTime(), evConsTime(1,0), NULL)) {
        LOG(LOG_EMERG, "%s: Failed to initialize an eventlib timer to generate "
            "the cached time", __func__);
        return EFAIL;
    }

    return SUCCESS;
}


/**
 * Configure mastership of this data component
 *
 * @param[in] state
 *      Mastership state (T=Master, F=Slave)
 *
 * @param[in] master_addr
 *      Master address (only present if state=F/slave)
 */
void
set_mastership(boolean state, in_addr_t master_addr)
{
    static boolean do_once = TRUE;

    if(do_once) {
        do_once = FALSE;
        if(init_replication(master_addr, m_ctx) == SUCCESS) {
            is_master = state;
        }
        return;
    }

    // Only recall init_replication when changing
    if(state != is_master &&
        init_replication(master_addr, m_ctx) == SUCCESS) {

        is_master = state;
    }
}


/**
 * Set the replication interval
 *
 * @param[in] interval
 *      Interval in second between replication data updates
 */
void
set_replication_interval(uint8_t interval)
{
    replication_interval = interval;
}


/**
 * Get the replication interval
 *
 * @return  The configured replication interval
 */
uint8_t
get_replication_interval(void)
{
    return replication_interval;
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


/**
 * Clear the configuration data
 */
void
clear_config(void)
{
    clear_monitors_configuration();
    clear_mirrors_configuration();
}


/**
 * Delete all configured monitors
 */
void
clear_monitors_configuration(void)
{
    monitor_t * mon;

    while(NULL != (mon = mon_entry(patricia_find_next(&monitors_conf, NULL)))) {

        if(!patricia_delete(&monitors_conf, &mon->node)) {
            LOG(LOG_ERR, "%s: Deleting monitor failed", __func__);
        }

        delete_all_addresses(mon);
        patricia_root_delete(mon->addresses);

        free(mon);
    }

    clean_flows_with_any_monitor();
}


/**
 * Delete all configured mirrors
 */
void
clear_mirrors_configuration(void)
{
    mirror_t * mir;

    INSIST_ERR(msp_spinlock_lock(&mir_conf_lock) == MSP_OK);

    while(NULL != (mir = mir_entry(patricia_find_next(&mirrors_conf, NULL)))) {

        if(!patricia_delete(&mirrors_conf, &mir->node)) {
                LOG(LOG_ERR, "%s: Deleting mirror failed", __func__);
        }

        free(mir);
    }

    INSIST_ERR(msp_spinlock_unlock(&mir_conf_lock) == MSP_OK);

    clean_flows_with_any_mirror();
}


/**
 * Find and delete a monitor given its name
 *
 * @param[in] name
 *      The monitor's name
 */
void
delete_monitor(char * name)
{
    monitor_t * mon;

    mon = mon_entry(patricia_get(&monitors_conf, strlen(name) + 1, name));

    if(mon == NULL) {
        LOG(LOG_WARNING, "%s: Could not find monitor to remove", __func__);
        return;
    }

    if(!patricia_delete(&monitors_conf, &mon->node)) {
        LOG(LOG_ERR, "%s: Deleting monitor failed", __func__);
        return;
    }

    delete_all_addresses(mon);
    patricia_root_delete(mon->addresses);

    free(mon);

    clean_flows_with_monitor(name);
}


/**
 * Find and delete a mirror given its from address
 *
 * @param[in] from
 *      The mirror's from address
 */
void
delete_mirror(in_addr_t from)
{
    mirror_t * mir;

    INSIST_ERR(msp_spinlock_lock(&mir_conf_lock) == MSP_OK);

    mir = mir_entry(patricia_get(&mirrors_conf, sizeof(from), &from));

    if(mir == NULL) {
        INSIST_ERR(msp_spinlock_unlock(&mir_conf_lock) == MSP_OK);
        LOG(LOG_WARNING, "%s: Could not find mirror to remove", __func__);
        return;
    }

    if(!patricia_delete(&mirrors_conf, &mir->node)) {
        INSIST_ERR(msp_spinlock_unlock(&mir_conf_lock) == MSP_OK);
        LOG(LOG_ERR, "%s: Deleting mirror failed", __func__);
        return;
    }
    INSIST_ERR(msp_spinlock_unlock(&mir_conf_lock) == MSP_OK);

    clean_flows_with_mirror(from);

    free(mir);
}


/**
 * Delete a given address from a monitor's config
 *
 * @param[in] name
 *      The monitor's name
 *
 * @param[in] addr
 *      Address
 *
 * @param[in] mask
 *      Address mask
 */
void
delete_address(char * name, in_addr_t addr, in_addr_t mask)
{
    monitor_t * mon;
    address_t * address = NULL;
    rnode_t * rn;

    mon = mon_entry(patricia_get(&monitors_conf, strlen(name) + 1, name));

    if(mon == NULL) {
        LOG(LOG_WARNING, "%s: Could not find monitor matching given name",
                __func__);
        return;
    }

    address = address_entry(patricia_get(mon->addresses,
                    sizeof(in_addr_t), &addr));

    if(address == NULL) {
        LOG(LOG_WARNING, "%s: Could not find address prefix to remove",
                __func__);
        return;
    }

    if(!patricia_delete(mon->addresses, &address->node)) {
        LOG(LOG_ERR, "%s: Deleting address failed", __func__);
        return;
    }

    INSIST_ERR(msp_spinlock_lock(&mon_conf_lock) == MSP_OK);

    rn = radix_get(&mon_prefixes, bit_count(mask), (uint8_t *)&addr);

    if(rn == NULL) {
        LOG(LOG_ERR, "%s: Failed to find monitored prefix to remove from "
                "monitor's rtree configuration", __func__);
    } else {
        radix_delete(&mon_prefixes, rn);
    }

    INSIST_ERR(msp_spinlock_unlock(&mon_conf_lock) == MSP_OK);

    clean_flows_in_monitored_prefix(name, addr, mask);

    free(address);
}


/**
 * Add an address prefix to a monitor's config
 *
 * @param[in] name
 *      The monitor's name
 *
 * @param[in] addr
 *      Address
 *
 * @param[in] mask
 *      Address mask
 */
void
add_address(char * name, in_addr_t addr, in_addr_t mask)
{
    monitor_t * mon;
    address_t * address = NULL;
    prefix_t * prefix;
    struct in_addr paddr;
    char * tmp;

    mon = mon_entry(patricia_get(&monitors_conf, strlen(name) + 1, name));

    if(mon == NULL) {
        LOG(LOG_WARNING, "%s: Could not find monitor matching given name",
                __func__);
        return;
    }

    address = calloc(1, sizeof(address_t));
    INSIST(address != NULL);

    address->address = addr;
    address->mask = mask;

    // Add to patricia tree

    if(!patricia_add(mon->addresses, &address->node)) {
        free(address);
        paddr.s_addr = addr;
        tmp = strdup(inet_ntoa(paddr));
        paddr.s_addr = mask;
        LOG(LOG_ERR, "%s: Cannot add address %s/%s as it conflicts with "
                "another prefix in the same monitored-networks group (%s)",
                __func__, tmp, inet_ntoa(paddr), mon->name);
        free(tmp);
        return;
    }

    prefix = calloc(1, sizeof(prefix_t));
    INSIST(prefix != NULL);
    prefix->prefix = addr;
    prefix->monitor = mon;

    radix_node_init(&prefix->rnode, bit_count(mask), 0);

    INSIST_ERR(msp_spinlock_lock(&mon_conf_lock) == MSP_OK);

    if(radix_add(&mon_prefixes, &prefix->rnode)) {

        LOG(LOG_ERR, "%s: Failed to add monitor prefix "
                "to rtree configuration", __func__);
        free(prefix);

        patricia_delete(mon->addresses, &address->node);
        free(address);
    }

    INSIST_ERR(msp_spinlock_unlock(&mon_conf_lock) == MSP_OK);

    // clean any flows that might be unmonitored, but match this prefix,
    // so that they become monitored
    clean_flows_in_monitored_prefix(NULL, addr, mask);
}


/**
 * Update or add a monitoring config
 *
 * @param[in] name
 *      The monitor name
 *
 * @param[in] rate
 *      The monitored group's bps (media) rate
 */
void
update_monitor(char * name, uint32_t rate)
{
    monitor_t * mon;

    mon = mon_entry(patricia_get(&monitors_conf, strlen(name) + 1, name));

    if(mon == NULL) {
        mon = calloc(1, sizeof(monitor_t));
        INSIST(mon != NULL);
        strncpy(mon->name, name, MAX_MON_NAME_LEN);

        // Add to patricia tree
        patricia_node_init_length(&mon->node, strlen(name) + 1);

        if(!patricia_add(&monitors_conf, &mon->node)) {
            LOG(LOG_ERR, "%s: Failed to add "
                    "monitor to configuration", __func__);
            free(mon);
            return;
        }

        // init addresses
        mon->addresses = patricia_root_init(NULL, FALSE, sizeof(in_addr_t), 0);
    }

    if(mon->rate != rate) {
        mon->rate = rate;
        clean_flows_with_monitor(name); // we don't reset stats, just clean
    }
}


/**
 * Update or add a mirroring config
 *
 * @param[in] from
 *      The mirror from address
 *
 * @param[in] to
 *      The mirror to address
 */
void
update_mirror(in_addr_t from, in_addr_t to)
{
    boolean is_new = FALSE;
    mirror_t * mir;

    INSIST_ERR(msp_spinlock_lock(&mir_conf_lock) == MSP_OK);

    mir = mir_entry(patricia_get(&mirrors_conf, sizeof(from), &from));

    if(mir == NULL) {
        mir = calloc(1, sizeof(mirror_t));
        INSIST(mir != NULL);
        mir->original = from;

        if(!patricia_add(&mirrors_conf, &mir->node)) {
            LOG(LOG_ERR, "%s: Failed to add "
                    "mirror to configuration", __func__);
            free(mir);
            INSIST_ERR(msp_spinlock_unlock(&mir_conf_lock) == MSP_OK);
            return;
        }
        is_new = TRUE;
    }

    mir->redirect = to;

    INSIST_ERR(msp_spinlock_unlock(&mir_conf_lock) == MSP_OK);

    if(!is_new) {
        redirect_flows_with_mirror(from, to);
    }
}


/**
 * Find the monitoring rate for an address if one exists
 *
 * @param[in] address
 *      The destination address
 *
 * @param[out] name
 *      The monitor name associated with the rate (set if found)
 *
 * @return the monitoring rate; or zero if no match is found
 */
uint32_t
get_monitored_rate(in_addr_t address, char ** name)
{
    rnode_t * rn;
    struct in_addr tmp;
    prefix_t * prefix;
    uint32_t result = 0;

    tmp.s_addr = address;
    LOG(LOG_INFO, "%s: Looking up rate for %s", __func__, inet_ntoa(tmp));

    INSIST_ERR(msp_spinlock_lock(&mon_conf_lock) == MSP_OK);

    rn = radix_lookup(&mon_prefixes, (uint8_t *)&address);

    if(rn == NULL) {
        INSIST_ERR(msp_spinlock_unlock(&mon_conf_lock) == MSP_OK);
        return 0;
    }

    prefix = (prefix_t *)((uint8_t *)rn - offsetof(prefix_t, rnode));

    *name = prefix->monitor->name;
    result = prefix->monitor->rate;

    INSIST_ERR(msp_spinlock_unlock(&mon_conf_lock) == MSP_OK);

    LOG(LOG_INFO, "%s: Match for flow to %s. Name: %s, Rate: %d", __func__,
            inet_ntoa(tmp), *name, result);

    return result;
}


/**
 * Find the redirect for a mirrored address if one exists
 *
 * @param[in] address
 *      The original destination
 *
 * @return the address to mirror to; or 0 if none found
 */
in_addr_t
get_mirror(in_addr_t address)
{
    mirror_t * mir;
    in_addr_t result = 0;

    INSIST_ERR(msp_spinlock_lock(&mir_conf_lock) == MSP_OK);

    mir = mir_entry(patricia_get(&mirrors_conf, sizeof(address), &address));

    if(mir != NULL) {
        result = mir->redirect;
    }

    INSIST_ERR(msp_spinlock_unlock(&mir_conf_lock) == MSP_OK);

    return result;
}

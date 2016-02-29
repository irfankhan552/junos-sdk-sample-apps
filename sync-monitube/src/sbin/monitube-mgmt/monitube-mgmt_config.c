/*
 * $Id: monitube-mgmt_config.c 365138 2010-02-27 10:16:06Z builder $
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
 * @file monitube-mgmt_config.c
 * @brief Relating to loading and storing the configuration data
 *
 * These functions will parse and load the configuration data.
 */

#include <sync/common.h>
#include <ddl/dax.h>
#include <jnx/pconn.h>
#include "monitube-mgmt_config.h"
#include "monitube-mgmt_conn.h"
#include "monitube-mgmt_logging.h"

#include MONITUBE_OUT_H

/*** Constants ***/

#define MAX_FLOW_AGE 300  ///< max flow statistic age in seconds (5 mins)


/*** Data Structures ***/


static patroot     monitors_conf;     ///< Pat. root for monitors config
static patroot     mirrors_conf;      ///< Pat. root for mirrors config
static monitor_t * current_monitor = NULL; ///< current monitor set while loading configuration
static uint8_t     replication_interval;   ///< replication interval
static evContext   m_ctx;                  ///< event context

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
 * Generate the fs_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(fs_entry, flowstat_t, node)


/**
 * Add an address to a monitor (uses @c current_monitor)
 *
 * @param[in] addr
 *      The address of the network
 *
 * @param[in] mask
 *      The address mask
 */
static void
update_address(in_addr_t addr, in_addr_t mask)
{
    address_t * address = NULL;
    struct in_addr paddr;
    char * tmp;

    INSIST_ERR(current_monitor != NULL);

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    address = calloc(1, sizeof(address_t));
    INSIST(address != NULL);

    address->address = addr;
    address->mask = mask;

    // Add to patricia tree

    if(!patricia_add(current_monitor->addresses, &address->node)) {
        free(address);
        paddr.s_addr = addr;
        tmp = strdup(inet_ntoa(paddr));
        paddr.s_addr = mask;
        LOG(TRACE_LOG_ERR, "%s: Cannot add address %s/%s as it conflicts with "
                "another address in the same monitored-networks group (%s)",
                __func__, tmp, inet_ntoa(paddr), current_monitor->name);
        free(tmp);
    }

    notify_address_update(current_monitor->name, addr, mask);
}


/**
 * Update or add a monitoring config
 *
 * @param[in] name
 *      The monitor name
 *
 * @param[in] rate
 *      The monitored group's bps (media) rate
 *
 * @return
 *      The added monitor
 */
static monitor_t *
update_monitor(const char * name, uint32_t rate)
{
    monitor_t * mon;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    mon = mon_entry(patricia_get(&monitors_conf, strlen(name) + 1, name));

    if(mon == NULL) {
        mon = calloc(1, sizeof(monitor_t));
        INSIST(mon != NULL);
        strncpy(mon->name, name, MAX_MON_NAME);

        // Add to patricia tree
        patricia_node_init_length(&mon->node, strlen(name) + 1);

        if(!patricia_add(&monitors_conf, &mon->node)) {
            LOG(TRACE_LOG_ERR, "%s: Failed to add "
                    "monitor to configuration", __func__);
            free(mon);
            return NULL;
        }

        // init addresses
        mon->addresses = patricia_root_init(NULL, FALSE, sizeof(in_addr_t), 0);

        // init flow_stats
        mon->flow_stats = patricia_root_init(NULL, FALSE,
                sizeof(in_addr_t) + (3 * sizeof(uint16_t)), 0);
    }

    if(mon->rate != rate) {
        mon->rate = rate;
        notify_monitor_update(name, rate);
    }

    return mon;
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
static void
update_mirror(in_addr_t from, in_addr_t to)
{
    mirror_t * mir;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    mir = mir_entry(patricia_get(&mirrors_conf, sizeof(from), &from));

    if(mir == NULL) {
        mir = calloc(1, sizeof(mirror_t));
        INSIST(mir != NULL);
        mir->original = from;

        if(!patricia_add(&mirrors_conf, &mir->node)) {
            LOG(TRACE_LOG_ERR, "%s: Failed to add "
                    "mirror to configuration", __func__);
            free(mir);
            return;
        }
    }

    // always true until we add more to a mirror...
    if(mir->redirect != to) {
        mir->redirect = to;
        notify_mirror_update(from, to);
    }
}


/**
 * Delete all address from a monitor's configuration
 *
 * @param[in] notify
 *      Whether or not to notify the data component
 */
static void
delete_all_addresses(boolean notify)
{
    address_t * add = NULL;

    INSIST_ERR(current_monitor != NULL);

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    while(NULL != (add =
        address_entry(patricia_find_next(current_monitor->addresses, NULL)))) {

        if(!patricia_delete(current_monitor->addresses, &add->node)) {
            LOG(TRACE_LOG_ERR, "%s: Deleting address failed", __func__);
            continue;
        }

        free(add);
    }

    if(notify) {
        // treat as a monitor delete since without any address a
        // monitor is useless to the data component
        notify_monitor_delete(current_monitor->name);
    }
}


/**
 * Delete all flow statistics from a monitor's configuration
 *
 * @param[in] mon
 *      The configuration for the monitor
 */
static void
delete_all_flowstats(monitor_t * mon)
{
    flowstat_t * fs = NULL;

    INSIST_ERR(mon != NULL);

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    while(NULL != (fs = fs_entry(patricia_find_next(mon->flow_stats, NULL)))) {

        if(evTestID(fs->timer_id)) {
           evClearTimer(m_ctx, fs->timer_id);
        }

        if(!patricia_delete(mon->flow_stats, &fs->node)) {
            LOG(TRACE_LOG_ERR, "%s: Deleting flowstat failed", __func__);
            continue;
        }

        free(fs);
    }
}


/**
 * Delete all configured monitors
 */
static void
delete_all_monitors(void)
{
    monitor_t * mon;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    while(NULL != (mon = mon_entry(patricia_find_next(&monitors_conf, NULL)))) {

        current_monitor = mon;
        delete_all_addresses(FALSE);
        patricia_root_delete(mon->addresses);
        delete_all_flowstats(mon);
        patricia_root_delete(mon->flow_stats);

        if(!patricia_delete(&monitors_conf, &mon->node)) {
            LOG(TRACE_LOG_ERR, "%s: Deleting monitor failed", __func__);
            continue;
        }

        free(mon);
    }

    current_monitor = NULL;

    notify_delete_all_monitors();
}


/**
 * Delete all configured mirrors
 */
static void
delete_all_mirrors(void)
{
    mirror_t * mir;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    while(NULL != (mir = mir_entry(patricia_find_next(&mirrors_conf, NULL)))) {

        if(!patricia_delete(&mirrors_conf, &mir->node)) {
            LOG(TRACE_LOG_ERR, "%s: Deleting mirror failed", __func__);
            continue;
        }

        free(mir);
    }

    notify_delete_all_mirrors();
}


/**
 * Delete a given address from a monitor's config (uses @c current_monitor)
 *
 * @param[in] addr
 *      Address
 *
 * @param[in] mask
 *      Address mask
 */
static void
delete_address(in_addr_t addr, in_addr_t mask)
{
    address_t * address = NULL;

    INSIST_ERR(current_monitor != NULL);

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    address = address_entry(patricia_get(current_monitor->addresses,
                    sizeof(in_addr_t), &addr));

    if(address == NULL) {
        LOG(TRACE_LOG_ERR, "%s: Could not find address to delete",__func__);
        return;
    }

    notify_address_delete(current_monitor->name, addr, mask);

    if(!patricia_delete(current_monitor->addresses, &address->node)) {
        LOG(TRACE_LOG_ERR, "%s: Deleting address failed", __func__);
        return;
    }

    free(address);


}


/**
 * Delete a monitor
 *
 * @param[in] mon
 *      The monitor
 */
static void
delete_monitor(monitor_t * mon)
{
    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    current_monitor = mon;
    delete_all_addresses(FALSE);
    patricia_root_delete(mon->addresses);
    delete_all_flowstats(mon);
    patricia_root_delete(mon->flow_stats);
    current_monitor = NULL;

    notify_monitor_delete(mon->name);

    if(!patricia_delete(&monitors_conf, &mon->node)) {
        LOG(TRACE_LOG_ERR, "%s: Deleting monitor failed", __func__);
        return;
    }

    free(mon);
}


/**
 * Find and delete a mirror given its from address
 *
 * @param[in] from
 *      The mirror's from address
 */
static void
delete_mirror(in_addr_t from)
{
    mirror_t * mir;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    mir = mir_entry(patricia_get(&mirrors_conf, sizeof(from), &from));

    if(mir == NULL) {
        LOG(TRACE_LOG_ERR, "%s: Could not find mirror to delete", __func__);
        return;
    }

    notify_mirror_delete(from);

    if(!patricia_delete(&mirrors_conf, &mir->node)) {
        LOG(TRACE_LOG_ERR, "%s: Deleting mirror failed", __func__);
        return;
    }

    free(mir);
}


/**
 * Handler for dax_walk_list to parse each configured address knob in a
 * monnitored-networks group
 *
 * @param[in] dwd
 *      Opaque dax data
 *
 * @param[in] dop
 *      DAX Object Pointer for server object
 *
 * @param[in] action
 *      The action on the given server object
 *
 * @param[in] data
 *      User data passed to handler (check flag)
 *
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int
parse_addresses(dax_walk_data_t * dwd,
                ddl_handle_t * dop,
                int action,
                void * data)
{
    char ip_str[32], * tmpstr, *tmpip;
    struct in_addr tmp;
    in_addr_t addr, prefix;
    int check = *((int *)data), mask;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    if(action == DAX_ITEM_DELETE_ALL) {
        // All servers were deleted
        delete_all_addresses(TRUE);
        return DAX_WALK_OK;
    }

    INSIST(dop != NULL);

    switch(action) {

    case DAX_ITEM_DELETE:
        // an address was deleted

        // get the (deleted) address
        // for deleted items we can only get a string form:
        if(!dax_get_stringr_by_dwd_ident(dwd, NULL, 0, ip_str, sizeof(ip_str))){
            dax_error(dop, "Failed to parse IP address and prefix on delete");
            return DAX_WALK_ABORT;
        }

        // break off prefix
        tmpip = strdup(ip_str);
        tmpstr = strtok(tmpip, "/");

        if(inet_aton(tmpstr, &tmp) != 1) { // if failed
            dax_error(dop, "Failed to parse prefix address portion: %s",
                    ip_str);
            return DAX_WALK_ABORT;
        }
        addr = tmp.s_addr;

        // parse prefix
        tmpstr = strtok(NULL, "/");

        errno = 0;
        mask = (int)strtol(tmpstr, (char **)NULL, 10);
        if(errno) {
            dax_error(dop, "Failed to parse prefix mask portion: %s", ip_str);
            return DAX_WALK_ABORT;
        }
        if(mask < 0 || mask > 32) {
            dax_error(dop, "Mask bits must be in range of 0-32: %s", ip_str);
            return DAX_WALK_ABORT;
        }

        if(mask == 32) {
            prefix = 0xFFFFFFFF;
        } else {
            prefix = (1 << mask) - 1;
            prefix = htonl(prefix);
        }

        free(tmpip);

        if(!check) {
            delete_address(addr, prefix);
        }

        break;

    case DAX_ITEM_CHANGED:
        // a server was added

        if(!dax_get_ipv4prefixmandatory_by_name(dop,
                DDLNAME_MONITORED_NETWORKS_ADDRESS, &addr, &prefix)) {
            dax_error(dop, "Failed to parse IP address and prefix");
            return DAX_WALK_ABORT;
        }

        if(((~prefix) & addr) != 0) {
            dax_error(dop, "Host bits in address must be zero");
            return DAX_WALK_ABORT;
        }

        if(!check) {
            update_address(addr, prefix);
        }

        break;

    case DAX_ITEM_UNCHANGED:
        junos_trace(MONITUBE_TRACEFLAG_CONF,
                "%s: DAX_ITEM_UNCHANGED observed", __func__);
        break;

    default:
        break;
    }

    return DAX_WALK_OK;
}


/**
 * Handler for dax_walk_list to parse each configured monitor knob
 *
 * @param[in] dwd
 *      Opaque dax data
 *
 * @param[in] dop
 *      DAX Object Pointer for application object
 *
 * @param[in] action
 *      The action on the given application object
 *
 * @param[in] data
 *      User data passed to handler (check flag)
 *
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int
parse_monitors(dax_walk_data_t * dwd,
               ddl_handle_t * dop,
               int action,
               void * data)
{
    char monitor_name[MAX_MON_NAME];
    uint32_t rate;
    monitor_t * mon;
    ddl_handle_t * mon_nets_dop;
    int check = *((int *)data);

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    switch (action) {

    case DAX_ITEM_DELETE_ALL:

        // All monitors were deleted
        delete_all_monitors();
        return DAX_WALK_OK;

        break;

    case DAX_ITEM_DELETE:

        // get the (deleted) monitor name
        if (!dax_get_stringr_by_dwd_ident(dwd, NULL, 0,
                monitor_name, sizeof(monitor_name))) {
            dax_error(dop, "Failed to parse a monitor name");
            return DAX_WALK_ABORT;
        }

        if(check) {
            break; // nothing to do
        }

        mon = mon_entry(patricia_get(&monitors_conf,
                    strlen(monitor_name) + 1, monitor_name));
        if(mon != NULL) {
            delete_monitor(mon);
        }

        break;

    case DAX_ITEM_CHANGED:

        INSIST_ERR(dop != NULL);

        // get the monitor name

        if (!dax_get_stringr_by_name(dop, DDLNAME_MONITOR_MONITOR_NAME,
                monitor_name, sizeof(monitor_name))) {
            dax_error(dop, "Failed to parse monitor name");
            return DAX_WALK_ABORT;
        }

        // read in some of the attributes

        if (!dax_get_uint_by_name(dop, DDLNAME_MONITOR_RATE, &rate)) {
            dax_error(dop, "Failed to parse rate");
            return DAX_WALK_ABORT;
        }

        if(!dax_get_object_by_name(dop, DDLNAME_MONITORED_NETWORKS,
                &mon_nets_dop, FALSE)) {

            dax_error(dop, "Failed to parse monitored-networks (required)");
            return DAX_WALK_ABORT;

        } else {

            // update
            if(!check) {
                current_monitor = update_monitor(monitor_name, rate);
                if(current_monitor == NULL) {
                    dax_release_object(&mon_nets_dop);
                    return DAX_WALK_ABORT;
                }
            }

            if(dax_walk_list(mon_nets_dop, DAX_WALK_DELTA, parse_addresses,data)
                        != DAX_WALK_OK) {

                dax_release_object(&mon_nets_dop);
                current_monitor = NULL;
                return DAX_WALK_ABORT;
            }
        }

        current_monitor = NULL;
        dax_release_object(&mon_nets_dop);

        break;

    case DAX_ITEM_UNCHANGED:
        junos_trace(MONITUBE_TRACEFLAG_CONF,
                "%s: DAX_ITEM_UNCHANGED observed", __func__);
        break;

    default:
        break;
    }

    return DAX_WALK_OK;
}



/**
 * Handler for dax_walk_list to parse each configured mirror knob
 *
 * @param[in] dwd
 *      Opaque dax data
 *
 * @param[in] dop
 *      DAX Object Pointer for application object
 *
 * @param[in] action
 *      The action on the given application object
 *
 * @param[in] data
 *      User data passed to handler (check flag)
 *
 * @return
 *      DAX_WALK_OK upon success or DAX_WALK_ABORT upon failure
 */
static int
parse_mirrors(dax_walk_data_t * dwd,
              ddl_handle_t * dop,
              int action,
              void * data)
{
    in_addr_t mirror_from;
    in_addr_t mirror_to;
    char ip_str[INET_ADDRSTRLEN];
    struct in_addr tmp;
    int check = *((int *)data);
    int addr_fam;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    switch (action) {

    case DAX_ITEM_DELETE_ALL:

        // All mirrors were deleted
        delete_all_mirrors();
        return DAX_WALK_OK;

        break;

    case DAX_ITEM_DELETE:

        // get the (deleted) mirror name (only available as a string)
        if (!dax_get_stringr_by_dwd_ident(dwd, NULL, 0,
                ip_str, sizeof(ip_str))) {
            dax_error(dop, "Failed to parse mirror address");
            return DAX_WALK_ABORT;
        }
        if(inet_aton(ip_str, &tmp) != 1) { // if failed
            dax_error(dop, "Failed to parse mirror IP from: %s", ip_str);
            return DAX_WALK_ABORT;
        }
        mirror_from = tmp.s_addr;

        if(!check) {
            delete_mirror(mirror_from);
        }

        break;

    case DAX_ITEM_CHANGED:

        INSIST_ERR(dop != NULL);

        // get the mirror info

        if(!dax_get_ipaddr_by_name(dop, DDLNAME_MIRROR_MIRRORED_ADDRESS,
                &addr_fam, &mirror_from, sizeof(mirror_from)) ||
                addr_fam != AF_INET) {
            dax_error(dop, "Failed to parse mirror IP");
            return DAX_WALK_ABORT;
        }

        if(!dax_get_ipaddr_by_name(dop, DDLNAME_MIRROR_DESTINATION,
                &addr_fam, &mirror_to, sizeof(mirror_to)) ||
                addr_fam != AF_INET) {
            dax_error(dop, "Failed to parse mirror destination IP");
            return DAX_WALK_ABORT;
        }

        // update
        if(!check) {
            update_mirror(mirror_from, mirror_to);
        }

        break;

    case DAX_ITEM_UNCHANGED:
        junos_trace(MONITUBE_TRACEFLAG_CONF,
                "%s: DAX_ITEM_UNCHANGED observed", __func__);
        break;

    default:
        break;
    }

    return DAX_WALK_OK;
}


/**
 * Delete the flow statistics because they have timed out
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
age_out_flow_stat(evContext ctx __unused, void * uap,
            struct timespec due __unused, struct timespec inter __unused)
{
    flowstat_t * fs = (flowstat_t *)uap;

    INSIST_ERR(fs != NULL);

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    if(evTestID(fs->timer_id)) {
       evClearTimer(m_ctx, fs->timer_id);
    }

    if(!patricia_delete(fs->mon->flow_stats, &fs->node)) {
        LOG(TRACE_LOG_ERR, "%s: Deleting flowstat failed", __func__);
        return;
    }

    free(fs);
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 */
void
init_config(evContext ctx)
{
    m_ctx = ctx;
    replication_interval = 0;

    patricia_root_init(&monitors_conf, FALSE, MAX_MON_NAME, 0);
    patricia_root_init(&mirrors_conf, FALSE, sizeof(in_addr_t), 0);
}


/**
 * Clear and reset the entire configuration, freeing all memory.
 */
void
clear_config(void)
{
    delete_all_monitors();
    delete_all_mirrors();
}


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
monitube_config_read(int check)
{
    const char * monitube_config[] =
        {DDLNAME_SYNC, DDLNAME_SYNC_MONITUBE, NULL};

    ddl_handle_t * top = NULL, * mon_dop = NULL, * mir_dop = NULL;
    uint8_t old_ri;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: Starting monitube "
            "configuration load", __func__);

    // Load the main configuration under sync monitube (if changed)...

    if (!dax_get_object_by_path(NULL, monitube_config, &top, FALSE))  {

        junos_trace(MONITUBE_TRACEFLAG_CONF,
                "%s: Cleared monitube configuration", __func__);

        clear_config();
        return SUCCESS;
    } else {
        if(dax_get_object_by_name(top, DDLNAME_MONITOR, &mon_dop, FALSE)) {
            if(dax_walk_list(mon_dop, DAX_WALK_DELTA, parse_monitors, &check)
                    != DAX_WALK_OK) {
    
                junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: walk monitors "
                    "list in sampling configuration failed", __func__);
                goto failed;
            }
        } else {
            delete_all_monitors();
        }
    
        if(dax_get_object_by_name(top, DDLNAME_MIRROR, &mir_dop, FALSE)) {
            if(dax_walk_list(mir_dop, DAX_WALK_DELTA, parse_mirrors, &check)
                    != DAX_WALK_OK) {
                junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: walk mirrors list in "
                        "sampling configuration failed", __func__);
                goto failed;
            }
        } else {
            delete_all_mirrors();
        }
    
        if(mon_dop == NULL && mir_dop == NULL) {
            dax_error(top, "Cannot have empty monitube service configuration");
            goto failed;
        }
    
        old_ri = replication_interval;
    
        dax_get_ubyte_by_name(top,
                DDLNAME_SYNC_MONITUBE_REPLICATION_INTERVAL,
                &replication_interval);
    
        if(old_ri != replication_interval) {
            notify_replication_interval(replication_interval);
        }
        
        dax_release_object(&top);
    }


    if(!check) {
        // send to the PIC(s)
        process_notifications();
    }

    junos_trace(MONITUBE_TRACEFLAG_CONF,
            "%s: Loaded monitube configuration", __func__);

    return SUCCESS;

failed:

    if(mon_dop)
        dax_release_object(&mon_dop);

    if(mir_dop)
        dax_release_object(&mir_dop);

    if(top)
        dax_release_object(&top);

    return EFAIL;
}


/**
 * Get the current replication_interval
 * 
 * @return the replication interval
 */
uint8_t
get_replication_interval(void)
{
    return replication_interval;
}

/**
 * Get the next monitor in configuration given the previously returned data
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to a monitor if one more exists, o/w NULL
 */
monitor_t *
next_monitor(monitor_t * data)
{
    return mon_entry(
            patricia_find_next(&monitors_conf, (data ? &(data->node) : NULL)));
}


/**
 * Get the next mirror in configuration given the previously returned data
 *
 * @param[in] data
 *      previously returned data, should be NULL first time
 *
 * @return pointer to a mirror if one more exists, o/w NULL
 */
mirror_t *
next_mirror(mirror_t * data)
{
    return mir_entry(
            patricia_find_next(&mirrors_conf, (data ? &(data->node) : NULL)));
}


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
next_address(monitor_t * mon, address_t * data)
{
    return address_entry(patricia_find_next(mon->addresses,
                                    (data ? &(data->node) : NULL)));
}


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
next_flowstat(monitor_t * mon, flowstat_t * data)
{
    return fs_entry(patricia_find_next(mon->flow_stats,
                                    (data ? &(data->node) : NULL)));
}


/**
 * Get the monitor configuration information by name
 *
 * @param[in] name
 *      Monitor name
 *
 * @return The monitor if one exists with the matching name, o/w NULL
 */
monitor_t *
find_monitor(char * name)
{
    return mon_entry(patricia_get(&monitors_conf, strlen(name) + 1, name));
}


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
              uint32_t mdi_mlr)
{
    monitor_t * mon;
    flowstat_t * fs;
    struct anp {
        uint16_t  fpc;
        uint16_t  pic;
        in_addr_t address;
        uint16_t  port;
    } key;

    // get monitor
    mon = mon_entry(patricia_get(&monitors_conf, strlen(name) + 1, name));

    if(mon == NULL) {
        LOG(TRACE_LOG_WARNING, "%s: statistic received for a flow in a monitor "
                "that is not configured (ignored)", __func__);
        return;
    }

    // get flow stat

    key.fpc = fpc_slot;
    key.pic = pic_slot;
    key.address = flow_addr;
    key.port = flow_port;

    fs = fs_entry(patricia_get(mon->flow_stats, sizeof(in_addr_t) +
            (3 * sizeof(uint16_t)), &key));

    if(fs == NULL) { // gotta create it
        fs = calloc(1, sizeof(flowstat_t));
        INSIST(fs != NULL);
        fs->fpc_slot = fpc_slot;
        fs->pic_slot = pic_slot;
        fs->flow_addr = flow_addr;
        fs->flow_port = flow_port;
        fs->mon = mon;

        if(!patricia_add(mon->flow_stats, &fs->node)) {
            LOG(TRACE_LOG_ERR, "%s: Failed to add a flow stat to configuration",
                    __func__);
            free(fs);
            return;
        }

        junos_trace(MONITUBE_TRACEFLAG_CONF, "%s: Added a new flow statistic "
                "for monitor %s", __func__, name);

    } else { // it already exists
        if(evTestID(fs->timer_id)) {
           evClearTimer(m_ctx, fs->timer_id);
        }
    }

    fs->last_mdi_df[fs->window_position] = mdi_df;
    fs->last_mdi_mlr[fs->window_position] = mdi_mlr;
    fs->reports++;
    fs->window_position++;
    if(fs->window_position == STATS_BASE_WINDOW) {
        fs->window_position = 0;
    }

    // reset age timer

    if(evSetTimer(m_ctx, age_out_flow_stat, fs,
            evAddTime(evNowTime(), evConsTime(MAX_FLOW_AGE, 0)),
            evConsTime(0, 0), &fs->timer_id)) {

        LOG(LOG_EMERG, "%s: evSetTimer() failed! Will not be "
                "able to age out flow statistics", __func__);
    }
}


/**
 * Clear all flow statistics records
 */
void
clear_all_flowstats(void)
{
    monitor_t * mon;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    mon = next_monitor(NULL);
    while(mon != NULL) {

        delete_all_flowstats(mon);

        mon = next_monitor(mon);
    }
}


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
clear_flowstats(char * mon_name)
{
    monitor_t * mon;

    junos_trace(MONITUBE_TRACEFLAG_CONF, "%s", __func__);

    // get monitor
    mon = mon_entry(patricia_get(&monitors_conf, strlen(mon_name)+1, mon_name));

    if(mon == NULL) {
        return -1;
    }

    delete_all_flowstats(mon);
    return 0;
}

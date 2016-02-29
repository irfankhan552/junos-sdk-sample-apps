/*
 * $Id: ped_config.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_config.c
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions and variables will parse, load, hold, and return
 * the configuration data.
 */

#include <sync/common.h>
#include <fnmatch.h>
#include <sync/psd_ipc.h>
#include <ddl/dax.h>
#include "ped_config.h"
#include "ped_conn.h"
#include "ped_filter.h"
#include "ped_schedule.h"
#include "ped_services.h"
#include "ped_service_route.h"
#include "ped_logging.h"

#include PE_OUT_H

/*** Constants ***/

#define FULL_INET_ADDR_MASK  0xFFFFFFFF  ///< a /32 mask
#define INT_NAME_STR_SIZE            64  ///< string size for interface names 

/*** Data structures: ***/

/**
 * Path to PED config
 */
const char *ped_top_config[] = { DDLNAME_SYNC,
                                 DDLNAME_SYNC_PE,
                                 NULL};

static patroot   root;               ///< Patricia tree root

static in_addr_t pfd_address;        ///< PFD's address (network-byte order)
static in_addr_t cpd_address;        ///< CPD's address (network-byte order)

static char pfd_interface[INT_NAME_STR_SIZE]; ///< PFD's receiving interface
static char pfd_nat_interface[INT_NAME_STR_SIZE]; ///< PFD's NAT interface

/*** STATIC/INTERNAL Functions ***/

/**
 * Generate the data_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(data_entry, ped_cond_t, node)

/**
 * Remove a condition from the configuration...used only internally
 * when emptying the patricia tree (in clear_config)
 * 
 * @param[in] data
 *     Condition/Data to delete from the configuration data storage
 */
static void
delete_condition(ped_cond_t *data)
{
    /* remove data from patricia tree */
    if (!patricia_delete(&root, &data->node)) {
        junos_trace(PED_TRACEFLAG_NORMAL,
               "%s: patricia delete FAILED!", __func__);
    }
    free(data);
    return;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Get the PFD interface
 * 
 * @return
 *      The PFD interface (IFL)
 */
char *
get_pfd_interface(void)
{
    return pfd_interface;
}


/**
 * Get the PFD NAT interface
 * 
 * @return
 *      The PFD NAT interface (IFL)
 */
char *
get_pfd_nat_interface(void)
{
    return pfd_nat_interface;
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
 * Init the data structure that will store configuration info,
 * or in other words, the condition(s)
 */
void
init_config(void)
{
    patricia_root_init(&root, FALSE, MAX_COND_NAME_LEN + 1, 0);
                   // root, is key ptr, key size, key offset
    
    pfd_address = 0;
    cpd_address = 0;
    bzero(pfd_interface, sizeof(pfd_interface));
    bzero(pfd_nat_interface, sizeof(pfd_nat_interface));
}


/**
 * Clear the configuration info completely from the patricia tree
 */
void
clear_config(void)
{
    ped_cond_t *data = NULL;

    while((data = first_condition()) != NULL) {
        delete_condition(data);
    }
    return;
}


/**
 * Get the first condition from configuration.
 * 
 * @return
 *      The first condition, or NULL if no conditions.
 */
ped_cond_t *
first_condition(void)
{
    return data_entry(patricia_find_next(&root, NULL));
}


/**
 * Get (iterator-style) the next condition from configuration.
 *
 * @param[in] data
 *     The previously returned data from this function or
 *     NULL for the first condition.
 * 
 * @return
 *      The next condition, or NULL if no more conditions.
 */
ped_cond_t *
next_condition(ped_cond_t *data)
{
    return data_entry(patricia_find_next(&root, (data ? &(data->node) : NULL)));
}


/**
 * Tells whether or not a condition matches this interface name/family pair
 * 
 * @param[in] ifname
 *     interface name to match
 * 
 * @param[in] af
 *     address family to match
 * 
 * @return TRUE if and only if this interface name matches a configured 
 *     condition's matching expression for interface names AND 
 *     if the family matches the configured family for that condition 
 *     unless the family is marked as any (in which case just the name matters);
 *     otherwise FALSE
 */
boolean 
find_matching_condition(char * ifname, uint8_t af)
{
    ped_cond_t *condition;
    
    condition = first_condition();
    
    while(condition != NULL) {
        if(fnmatch(condition->ifname, ifname, 0) == 0  &&
                (condition->af == AF_UNSPEC || af == condition->af)) {
            return TRUE;
        }
        condition = next_condition(condition);
    }

    return FALSE;
}


/**
 * Read configuration from the database.
 * 
 * @param[in] check
 *      SDK_CONFIG_READ_CHECK(1) if this function was called
 *      to check configuration, or SDK_CONFIG_READ_LOAD(0)
 *      if it's called to load configuration.
 *
 * @return
 *      SUCCESS, or -1 on error.
 * 
 * @note
 *      Do not use ERRMSG during config check.
 */
int
ped_config_read (int check)
{
    const char * ped_conditions_config[] = { DDLNAME_SYNC,
                                             DDLNAME_SYNC_PE,
                                             DDLNAME_SYNC_PE_CONDITION,
                                             NULL };
    ddl_handle_t *dop = NULL, *top = NULL; // DDL Object Pointers
    ped_cond_t * data;
    int        status = SUCCESS;
    int        temp_af; // we assume IPv4 is always returned
    in_addr_t  temp_address, prev_pfd_address;  ///< CPD's address
    in_addr_t  temp_mask;
    char       tmp[INT_NAME_STR_SIZE];
    boolean    address_changed = FALSE;
    
    junos_trace(PED_TRACEFLAG_CONFIG, 
            "%s: Loading ped configuration...", __func__);

    /* 
     * Read the config wrt the PFD and the CPD if there is any:
     * 
     * WRT these next four attributes either all of them are 
     * configured or none of them are configured.
     */
    
    if (dax_get_object_by_path(NULL, ped_top_config, &top, TRUE)) {
    
        // Read the CPD address
        if(dax_get_ipv4prefix_by_aid(top, CPD_ADDRESS,
                &temp_address, &temp_mask)) {
            
            // insist that it's a /32 mask b/c that's what we use
            if(temp_mask != FULL_INET_ADDR_MASK) {
                dax_error(dop, "Reading the CPD address with /32 mask FAILED!");
                dax_release_object(&top);
                return -1;
            }
            
            if(!check) {
                if(temp_address != cpd_address) { // CPD address has changed
                    cpd_address = temp_address;
                    address_changed = TRUE;
                }
            }
        } else if(!check && cpd_address != 0) { // they're removing the config
            cpd_address = 0;
        }
        
        // remember the last address if it was set
        prev_pfd_address = pfd_address;
        
        // Read the PFD address
        if(dax_get_ipaddr_by_aid(top, PFD_ADDRESS,
                &temp_af, &temp_address, sizeof(in_addr_t))) {
            
            // if its configured insist that it's IPv4
            if(temp_af != AF_INET) {
                dax_error(dop, "Reading the inet4 PFD address FAILED!");
                dax_release_object(&top);
                return -1;
            }
            
            // Also insist that it's not the default route
            if(temp_address == 0) {
                dax_error(dop, "PFD address cannot be 0 FAILED!");
                dax_release_object(&top);
                return -1;
            }
            
            if(!check && temp_address != pfd_address) { // PFD address has changed
                pfd_address = temp_address;
                address_changed = TRUE;
            }
        } else if(!check && pfd_address != 0) { // they're removing the config
            pfd_address = 0;
        }
        
        // Read the PFD interface to go in the pfd_forwarding RI
        if(dax_get_stringr_by_aid(top, PFD_INTERFACE, tmp, sizeof(tmp))) {
            
            if(!check && strcmp(pfd_interface, tmp) != 0) {
                
                // Delete and Add service routes on special pfd_forwarding RI
                
                // check if pfd_interface must also have been previoulsy set
                if(pfd_interface[0] != 0) {
                    delete_pfd_service_route(pfd_interface);
                }
                
                strcpy(pfd_interface, tmp);
                add_pfd_service_route(pfd_interface);
                
                init_pfd_filter(pfd_interface);
                turn_on_pfd_filter();
                update_policies(); // we changed the status of the PFD filter
                
                if(!get_serviceroute_ready()) {
                    // PFD RI may have just been created so tell the SR module
                    pfd_ri_created();
                }
            }
        } else if(!check && *pfd_interface) { // they're removing the config
            
            bzero(pfd_interface, sizeof(pfd_interface));
            
            turn_off_pfd_filter();  // turn off the PFD filter
            clean_service_routes(); // delete the service routes
            update_policies();      // we changed the status of the PFD filter
        }
        
        // Read the PFD interface in the default RI 
        if(dax_get_stringr_by_aid(top, PFD_NAT_INTERFACE, tmp, sizeof(tmp))) {

            if(!check && strcmp(pfd_nat_interface, tmp) != 0) {
                
                // Delete and Add service routes on inet.0 (normal) RI
                // for traffic coming back from CPD to the PFD address
                
                if(pfd_nat_interface[0] != 0) { // it was previously set
                    delete_service_route(pfd_nat_interface, prev_pfd_address);
                }

                strcpy(pfd_nat_interface, tmp); // set new value
                add_service_route(pfd_nat_interface, pfd_address);
            }
        } else if(!check && *pfd_nat_interface) { // they're removing the config

            bzero(pfd_nat_interface, sizeof(pfd_nat_interface));
        }
        
        if(!check && address_changed) {
            reconfigure_peers(); // Send PFD and CPD new addresses
        }
        
        dax_release_object(&top);
    }
    
    /* 
     * Read the config of the PED's conditions:
     */
    
    if (dax_get_object_by_path(NULL, ped_conditions_config, &top, FALSE)) {

        // top points to the configuration  policy-enforcement (cond. list)
        if(dax_is_changed(top)) {
            
            // clear stored config data, & repopulate with new config data
            clear_config();
            
            while (status == SUCCESS && dax_visit_container(top, &dop)) {

                // dop points to "match" object

                // allocate & initialize data
                data = calloc(1, sizeof(ped_cond_t)); // calloc zeros mem
                INSIST(data != NULL); // check that allocation worked */

                if(!dax_get_stringr_by_aid(dop, POLICY_CONDITION_NAME,
                    data->condition_name, sizeof(data->condition_name))) {

                    // this attribute is mandatory must have a name
                    dax_error(dop, "Read condition name FAILED!");
                    status = -1;
                    free(data);
                    break;
                }

                if(!dax_get_stringr_by_aid(dop, POLICY_INTERFACE_NAME,
                    data->ifname, sizeof(data->ifname))) {
    
                    // this attribute is mandatory must have an interface name
                    dax_error(dop, "Read interface name FAILED!");
                    status = -1;
                    free(data);
                    break;
                }

                if(!dax_get_ubyte_by_aid(dop, POLICY_FAMILY, &(data->af))) {
    
                    // if not configured, set the value to NULL
                    dax_error(dop, "Read address family FAILED!");
                    status = -1;
                    free(data);
                    break;
                }

                if (status == SUCCESS && !check) {

                    /* add to data_root patricia tree */
                    patricia_node_init_length(&data->node,
                                              strlen(data->condition_name) + 1);
                                              
                    if (!patricia_add(&root, &data->node)) {
                        dax_error(dop, "Adding policy to loaded configuration "
                            "(found a duplicate entry in pat tree) FAILED!");
                        status = -1;
                        free(data);
                        break;
                    }
                }
            }
        } else {
            junos_trace(PED_TRACEFLAG_CONFIG,
                "%s: No conditions configuration change", __func__);
        }
        
        if(status == SUCCESS && !check) {

            junos_trace(PED_TRACEFLAG_CONFIG, "%s: Finished loading"
                " configuration successfully", __func__);

            update_policies(); // Update all interfaces needed
            
        } else if (!check) {
            junos_trace(PED_TRACEFLAG_NORMAL, "%s: Load configuration FAILED!",
                __func__);
        }
    } else {
        /* clear stored config data because we have no configure data */
        clear_config();
        junos_trace(PED_TRACEFLAG_CONFIG, "%s: No conditions to configure",
            __func__);
    }

    dax_release_object(&top);
    dax_release_object(&dop);

    return status;
}

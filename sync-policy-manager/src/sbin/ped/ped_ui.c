/*
 * $Id: ped_ui.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_ui.c
 * @brief Relating to the user interface (commands) of ped 
 * 
 * Contains callback functions that get executed by commands on the router
 */
#include <sync/common.h>
#include <sync/psd_ipc.h>
#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>
#include <junoscript/xmlstr.h>
#include <jnx/ssd_ipc_msg.h>
#include "ped_config.h"
#include "ped_filter.h"
#include "ped_logging.h"
#include "ped_kcom.h"
#include "ped_policy_table.h"
#include "ped_services.h"
#include "ped_service_route.h"
#include "ped_schedule.h"
#include "ped_ssd.h"

#include PE_OUT_H
#include PE_ODL_H

/*** Constants ***/


#define FORMAT_TABLE       1        ///< display in table format
#define FORMAT_FAMILY      2        ///< display arranged by family
#define FORMAT_INTERFACE   3        ///< display arranged by interface

#define MAX_FAMILY_NAME_LENGTH 64   ///< max length of string for a family name


/*** Data Structures ***/

// NOTE: menu structure at the bottom of this file


/*** STATIC/INTERNAL Functions ***/


/**
 * Get a string from an interger for the family.
 * 
 * @param[in] protocol_family
 *     The protocol family for which a string is desired
 * 
 * @return statically allocated char * represenation (good until next call).
 */
static char * 
itoa_family (int protocol_family)
{
    static char family[MAX_FAMILY_NAME_LENGTH];  
    
    switch(protocol_family) {
        
    case AF_INET:
        sprintf(family, "inet");
        break;
        
    case AF_INET6:
        sprintf(family, "inet6");
        break;
        
    default:
        sprintf(family, "%d", protocol_family);
        break;
        
    }

    return family;
}


/**
 * Compare interface family given two table entries
 * 
 * @param[in] ent1
 *     Table entry one
 * 
 * @param[in] ent2
 *     Table entry two     
 * 
 * @return an integer greater than, equal to, or less than 0, accordingly 
 *         as the family in ent1 is greater than, equal to, or less than 
 *         the the family in ent2; or
 *         if the family if the same, then it compares/sorts by interface name.
 */
static int 
fam_cmp (const void * ent1, const void * ent2)
{
    // gotta respect those const qualifiers :P
    const policy_table_entry_t * t1 = * (policy_table_entry_t * const *)ent1;
    const policy_table_entry_t * t2 = * (policy_table_entry_t * const *)ent2;
    int order = t1->af - t2->af;
    
    if(order == 0)
        order = strcmp(t1->ifname, t2->ifname);
    return order;
}


/**
 * Compare interface names given two table entries
 * 
 * @param[in] ent1
 *     Table entry one
 * 
 * @param[in] ent2
 *     Table entry two     
 * 
 * @return integer result of strcmp between interface names; or
 *         if the family if the same, then it compares/sorts by interface name.
 */
static int 
intname_cmp (const void * ent1, const void * ent2)
{
    // gotta respect those const qualifiers :P
    const policy_table_entry_t * t1 = * (policy_table_entry_t * const *)ent1; 
    const policy_table_entry_t * t2 = * (policy_table_entry_t * const *)ent2;
    int order = strcmp(t1->ifname, t2->ifname);
    
    if(order == 0)
        order = t1->af - t2->af;
    return order;
}

/**
 * Convert next-hop type to string.
 * 
 * @param[in] type
 *     Next-hop type.
 * 
 * @return
 *      String pointer to next-hop type.
 */
static char *
itoa_hop_type(uint8_t type)
{
    static char type_str[16];

    switch(type) {
        case SSD_RNH_DISCARD:
            sprintf(type_str, "Discard");
            break;
        case SSD_RNH_REJECT:
            sprintf(type_str, "Reject");
            break;
        default:
            sprintf(type_str, "Unknown");
    }
    return type_str;
}

/**
 * Displays the managed interfaces
 * 
 * @param[in] msp
 *     management socket pointer
 * 
 * @param[in] csb
 *     parsed info status (contains subcodes/arguments)
 * 
 * @param[in] unparsed
 *     unparsed command string
 * 
 * @return 0 if no error; 1 to terminate connection
 */
static int32_t
show_managed (mgmt_sock_t *msp,
              parse_status_t *csb,
              char *unparsed __unused)
{
    int format = ms_parse_get_subcode(csb);    // how to format
    policy_table_entry_t * entry;          // tmp ptr to entry
    policy_table_entry_t ** all_entries;   // tmp array of ptrs to entries while we sort
    int num_entries = policy_table_entry_count(); // num elem.s in all_entries
    int i;                        // tmp index
    int current_family;
    struct in_addr  addr;
    ped_policy_route_t *route;
    
    junos_trace(PED_TRACEFLAG_NORMAL, "%s", __func__);

    policy_table_iterator_reset();
    
    // get style string and
    // open the container tag to hold everything with the style & NS attribute
    switch(format) {

    case FORMAT_FAMILY:
        XML_OPEN(msp, ODCI_PE_INFORMATION);
        XML_ELT(msp, ODCI_NUMBER_ENTRIES, "%d", num_entries);

        if(num_entries < 1) {
            XML_CLOSE(msp, ODCI_PE_INFORMATION);
            break;
        }

        // put all entries into an array of ptrs
        all_entries = (policy_table_entry_t **)malloc(
                            sizeof(policy_table_entry_t *) * num_entries);
        INSIST(all_entries != NULL);

        for(i = 0; i < num_entries; i++) {
            if((all_entries[i] = policy_table_next()) == NULL) {
                break;
            }
        }
        INSIST(i == num_entries); // check that we got them all

        // sort by interface name        
        qsort(all_entries, num_entries, sizeof(all_entries[0]), fam_cmp);

        i = 0;
        // group output by family (already sorted)
        // for all families, loop:
        while(i < num_entries) {
            entry = all_entries[i];

            XML_OPEN(msp, ODCI_FAMILY_GROUP);
            XML_ELT(msp, ODCI_FAMILY, "%s", itoa_family(entry->af));

            current_family = entry->af; // register the current family

            // for all interfaces with this family, loop:
            while(i < num_entries) {
                entry = all_entries[i];

                // if no longer the same family, then go to the next family
                if(current_family != entry->af)
                    break;

                // open container tag for this entry
                XML_OPEN(msp, ODCI_INTERFACE);

                // write tags containing data
                XML_ELT(msp, ODCI_NAME, "%s", entry->ifname);
                
                if(entry->filter) {
                    XML_ELT(msp, ODCI_INPUT_FILTER, "%s",
                            entry->filter->filter_data.input_filter);
                    XML_ELT(msp, ODCI_OUTPUT_FILTER, "%s",
                            entry->filter->filter_data.output_filter);
                }
                
                if(is_pfd_filter_on()) {
                	XML_ELT(msp, ODCI_PFD_FILTER, "%s", "ON");
                }

                XML_CLOSE(msp, ODCI_INTERFACE);

                ++i;
            }

            XML_CLOSE(msp, ODCI_FAMILY_GROUP);
        }

        free(all_entries);

        XML_CLOSE(msp, ODCI_PE_INFORMATION);
        break;

    case FORMAT_INTERFACE:
        XML_OPEN(msp, ODCI_PE);
        XML_ELT(msp, ODCI_NUMBER_ENTRIES, "%d", num_entries);

        if(num_entries < 1) {
            XML_CLOSE(msp, ODCI_PE);
            break;
        }

        // put all entries into an array of ptrs
        all_entries = (policy_table_entry_t **)malloc(
                            sizeof(policy_table_entry_t *) * num_entries);
        INSIST(all_entries != NULL);

        for(i = 0; i < num_entries; i++) {
            if((all_entries[i] = policy_table_next()) == NULL) {
                break;
            }
        }
        INSIST(i == num_entries); // check that we got them all

        // sort by interface name        
        qsort(all_entries, num_entries, sizeof(all_entries[0]), intname_cmp);

        XML_OPEN(msp, ODCI_MANAGED_INTERFACES, "%s %s", xml_attr_xmlns(XML_NS), 
             xml_attr_style(MANAGED_INTERFACES_STYLE_BY_INTERFACE));
        
        for(i = 0; i < num_entries; ++i) {

            entry = all_entries[i];
            
            // open container tag for this entry
            XML_OPEN(msp, ODCI_MANAGED_INTERFACE);
            
            // write tags containing data 
            XML_ELT(msp, ODCI_NAME, "%s", entry->ifname);
            XML_ELT(msp, ODCI_FAMILY, "%s", itoa_family(entry->af));
            

            if(is_pfd_filter_on()) {
            	XML_ELT(msp, ODCI_PFD_FILTER, "%s", "ON");
            }

            if(entry->filter) {
                XML_ELT(msp, ODCI_INPUT_FILTER, "%s",
                        entry->filter->filter_data.input_filter);
                XML_ELT(msp, ODCI_OUTPUT_FILTER, "%s",
                        entry->filter->filter_data.output_filter);
            }
            
            route = entry->route;
            while(route) {
                addr.s_addr = route->route_data.route_addr;
                XML_ELT(msp, ODCI_ROUTE_ADDRESS, "%s", inet_ntoa(addr));
                XML_ELT(msp, ODCI_ROUTE_PREFERENCES, "%d",
                        route->route_data.preferences);
                XML_ELT(msp, ODCI_ROUTE_METRICS, "%d",
                        route->route_data.metrics);
                if(route->route_data.nh_type != SSD_RNH_UNICAST) {
                    XML_ELT(msp, ODCI_NEXT_HOP_TYPE, "%s",
                        itoa_hop_type(route->route_data.nh_type));
                } else {
                    addr.s_addr = route->route_data.nh_addr;
                    XML_ELT(msp, ODCI_NEXT_HOP_ADDRESS, "%s", inet_ntoa(addr));
                }
                route = route->next;
            }
            XML_CLOSE(msp, ODCI_MANAGED_INTERFACE);
        }
        
        XML_CLOSE(msp, ODCI_MANAGED_INTERFACES);
        
        free(all_entries);
        
        XML_CLOSE(msp, ODCI_PE);
             
        break;
        
    case FORMAT_TABLE:
        XML_OPEN(msp, ODCI_PE);
        XML_ELT(msp, ODCI_NUMBER_ENTRIES, "%d", num_entries);

        if(num_entries < 1) {
            XML_CLOSE(msp, ODCI_PE);
            break;
        }

        // ordering doesn't matter for the table
        XML_OPEN(msp, ODCI_MANAGED_INTERFACES, "%s %s", xml_attr_xmlns(XML_NS), 
             xml_attr_style(MANAGED_INTERFACES_STYLE_TABLE));

        while((entry = policy_table_next())) {

            // open container tag for this entry
            XML_OPEN(msp, ODCI_MANAGED_INTERFACE);
            
            // write tags containing data 
            XML_ELT(msp, ODCI_NAME, "%s", entry->ifname);
            XML_ELT(msp, ODCI_FAMILY, "%s", itoa_family(entry->af));
            
            if(entry->filter) {
                XML_ELT(msp, ODCI_INPUT_FILTER, "%s",
                        entry->filter->filter_data.input_filter);
                XML_ELT(msp, ODCI_OUTPUT_FILTER, "%s",
                        entry->filter->filter_data.output_filter);
            }
            
            if(is_pfd_filter_on()) {
            	XML_ELT(msp, ODCI_PFD_FILTER, "%s", "ON");
            }
            
            XML_CLOSE(msp, ODCI_MANAGED_INTERFACE);
        }
        
        XML_CLOSE(msp, ODCI_MANAGED_INTERFACES);
        XML_CLOSE(msp, ODCI_PE);
        break;
        
    default:
        XML_RAW(msp, 0, xml_error_str(FALSE, NULL, NULL, NULL, NULL, 
           "level", NULL, "Unsupported level/format requested in command"));
    }
    
    return 0;
}


/*** GLOBAL/EXTERNAL Functions ***/


/******************************************************************************
 *                        Menu Structure
 *****************************************************************************/

/*
 * format:
 *   command, desc, arg block(or 0), child menu(or 0), command(or 0)
 */


/**
 * "show sync policy-enforcement managed-interfaces ..." commands
 */
static const parse_menu_t show_sync_pe_level_menu[] = 
{
    { "by-family", NULL, FORMAT_FAMILY, NULL, show_managed },
    { "by-interface", NULL, FORMAT_INTERFACE, NULL, show_managed },
    { "table", NULL, FORMAT_TABLE, NULL, show_managed },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 * "show sync policy-enforcement ..." commands
 */
static const parse_menu_t show_sync_pe_menu[] = 
{
    { "managed-interfaces", NULL, 0, show_sync_pe_level_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 * "show sync ..." commands
 */
static const parse_menu_t show_sync_menu[] = 
{
    { "policy-enforcement", NULL, 0, show_sync_pe_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 * "show ..." commands
 */
static const parse_menu_t show_menu[] = 
{
    { "sync", NULL, 0, show_sync_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  main menu of commands
 */
const parse_menu_t master_menu[] = 
{
    { "show", NULL, 0, show_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};


/*
 * $Id: monitube2-mgmt_ui.c 347265 2009-11-19 13:55:39Z kdickman $
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
 * @file monitube2-mgmt_ui.c
 * @brief Relating to the user interface (commands) of monitube-mgmt 
 * 
 * Contains callback functions that get executed by commands on the router
 */

#include <sync/common.h>
#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>
#include <junoscript/xmlstr.h>
#include <ddl/defs.h>
#include "monitube2-mgmt_main.h"
#include "monitube2-mgmt_config.h"
#include "monitube2-mgmt_conn.h"
#include "monitube2-mgmt_logging.h"

#include MONITUBE2_ODL_H
#include MONITUBE2_OUT_H

/*** Constants ***/


/*** Data Structures ***/


// See menu structure at the bottom of this file


/*** STATIC/INTERNAL Functions ***/


/**
 * Shows the monitube stats optionally just for one monitoring config group
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
 * @return 0
 */
static int32_t
monitube_show_stats(mgmt_sock_t * msp,
                    parse_status_t * csb UNUSED,
                    char * unparsed UNUSED)
{
	flowstat_ss_t * fss = NULL;
	flowstat_t * fs = NULL;
	ss_info_t * ssi;
    struct in_addr addr;
    int i;
    double max_df, min_df, avg_df;
    uint32_t max_mlr, min_mlr, avg_mlr, total_loss;

    XML_OPEN(msp, ODCI_MONITUBE_STATS);

    fss =  next_flowstat_ss(fss);
    while(fss) {
    	
    	XML_OPEN(msp, ODCI_SERVICE_SET);
    	
    	XML_ELT(msp, ODCI_SSID, "%d", fss->ssid);
    	
    	// get service set name
    	ssi = next_serviceset(NULL);
    	while(ssi) {
    		if (ssi->id == fss->ssid) {
            	XML_ELT(msp, ODCI_SSNAME, "%s", ssi->name);
            	break;
    		}
    		ssi = next_serviceset(ssi);
    	}
    	
    	if (!ssi) {
    		XML_ELT(msp, ODCI_SSNAME, "%s", "(not found)");
    	}
    	
        XML_OPEN(msp, ODCI_FLOW_STATS);
        
        fs = next_flowstat(fss, fs);
        while(fs) {

			// We don't break down flow stats per FPC/PIC SLOT although we store it
			// so some flows may have multiple statistics
			
			XML_OPEN(msp, ODCI_FLOW_STAT);
			addr.s_addr = fs->flow_addr;
			
			XML_ELT(msp, ODCI_FLOW_ADDRESS, "%s:%d",
						inet_ntoa(addr), fs->flow_port);
			
			if (fs->reports >= STATS_BASE_WINDOW) {
				if (fs->window_position == 0) {
					i = STATS_BASE_WINDOW;
				} else {
					i = fs->window_position - 1;
				}
				
				XML_ELT(msp, ODCI_LAST_MDI_STAT, "%.4f:%d",
						fs->last_mdi_df[i], fs->last_mdi_mlr[i]);
				
				max_df = min_df = avg_df = fs->last_mdi_df[0];
				max_mlr = min_mlr = avg_mlr = fs->last_mdi_mlr[0];
				
				for(i = 1; i < STATS_BASE_WINDOW; ++i) {
					if (fs->last_mdi_df[i] > max_df) {
						max_df = fs->last_mdi_df[i];
					} else if (fs->last_mdi_df[i] < min_df) {
						min_df = fs->last_mdi_df[i];
					}
					
					if (fs->last_mdi_mlr[i] > max_mlr) {
						max_mlr = fs->last_mdi_mlr[i];
					} else if (fs->last_mdi_mlr[i] < min_mlr) {
						min_mlr = fs->last_mdi_mlr[i];
					}
					
					avg_df += fs->last_mdi_df[i];
					avg_mlr += fs->last_mdi_mlr[i];
				}
				
				avg_df /= STATS_BASE_WINDOW;
				total_loss = avg_mlr;
				avg_mlr /= STATS_BASE_WINDOW;
				
				XML_ELT(msp, ODCI_MAX_MDI_STAT, "%.4f:%d", max_df, max_mlr);
				XML_ELT(msp, ODCI_MIN_MDI_STAT, "%.4f:%d", min_df, min_mlr);
				XML_ELT(msp, ODCI_AVG_MDI_STAT, "%.4f:%d", avg_df, avg_mlr);
				XML_ELT(msp, ODCI_TOTAL_LOSS, "%d", total_loss);
			}

			XML_CLOSE(msp, ODCI_FLOW_STAT);
        	
            fs = next_flowstat(fss, fs);
        }
        
        XML_CLOSE(msp, ODCI_FLOW_STATS);
        XML_CLOSE(msp, ODCI_SERVICE_SET);
        
        fss = next_flowstat_ss(fss);
    }
    
    XML_CLOSE(msp, ODCI_MONITUBE_STATS);
    
    return 0;
}



/**
 * Clear cached stats for one monitor's config group or all of them
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
 * @return 0 if no error
 */
static int32_t
monitube_clear_stats(mgmt_sock_t * msp,
                     parse_status_t * csb UNUSED,
                     char * unparsed UNUSED)
{
	clear_stats();
	
	XML_SEND_SUCCESS_TAG(msp);
    
    return 0;
}


/*** GLOBAL/EXTERNAL Functions ***/



/******************************************************************************
 *                        Menu Structure
 *****************************************************************************/

/*
 * format (from libjuniper's ms_parse.h):
 *   command,
 *   help desc (or NULL),
 *   user data arg block (or 0),
 *   child (next) menu (or NULL),
 *   handler for command(or 0)
 */


/**
 *  show sync monitube ... commands
 */
static const parse_menu_t show_sync_monitube_menu[] = {
    { "statistics", NULL, 0, NULL, monitube_show_stats },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  clear sync monitube ... commands
 */
static const parse_menu_t clear_sync_monitube_menu[] = {
    { "statistics", NULL, 0, NULL, monitube_clear_stats },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  show sync ... commands
 */
static const parse_menu_t show_sync_menu[] = {
    { "monitube", NULL, 0, show_sync_monitube_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  clear sync ... commands
 */
static const parse_menu_t clear_sync_menu[] = {
    { "monitube", NULL, 0, clear_sync_monitube_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  show ... commands
 */
static const parse_menu_t show_menu[] = {
    { "sync", NULL, 0, show_sync_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  clear ... commands
 */
static const parse_menu_t clear_menu[] = {
    { "sync", NULL, 0, clear_sync_menu, NULL },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  main menu of commands
 */
const parse_menu_t master_menu[] = {
    { "show", NULL, 0, show_menu, NULL },
    { "clear", NULL, 0, clear_menu, NULL }, 
    { NULL, NULL, 0, NULL, NULL }
};


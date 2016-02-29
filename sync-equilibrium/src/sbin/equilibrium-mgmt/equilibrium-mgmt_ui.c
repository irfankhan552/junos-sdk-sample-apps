/*
 * $Id: equilibrium-mgmt_ui.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-mgmt_ui.c
 * @brief Relating to the user interface (commands) of equilibrium-mgmt 
 * 
 * Contains callback functions that get executed by commands on the router
 */

#include <sync/common.h>
#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>
#include <junoscript/xmlstr.h>
#include <ddl/defs.h>
#include "equilibrium-mgmt_main.h"
#include "equilibrium-mgmt_kcom.h"
#include "equilibrium-mgmt_config.h"
#include "equilibrium-mgmt_conn.h"
#include "equilibrium-mgmt_logging.h"

#include EQUILIBRIUM_ODL_H
#include EQUILIBRIUM_OUT_H

/*** Constants ***/


/*** Data Structures ***/


// See menu structure at the bottom of this file


/*** STATIC/INTERNAL Functions ***/


/**
 * Output the XML for the application tag given the application information. 
 * 
 * @param[in] msp
 *     management socket pointer
 * 
 * @param[in] ss_name
 *     The service-set name
 * 
 * @param[in] app
 *     The application information
 */
static void
show_application_status(mgmt_sock_t * msp,
                        const char * ss_name,
                        const eq_app_t * app)
{
    eq_server_t * server = NULL;
    struct in_addr addr;
    
    XML_OPEN(msp, ODCI_APPLICATION_STATUS);
    
    XML_ELT(msp, ODCI_SERVICE_SET_NAME, "%s", ss_name);
    XML_ELT(msp, ODCI_APPLICATION_NAME, "%s", app->application_name);
    
    if(app->servers != NULL) {
        
        XML_OPEN(msp, ODCI_SERVER_LIST);
        
        server = TAILQ_FIRST(app->servers);
        while(server != NULL) {
            
            XML_OPEN(msp, ODCI_SERVER);
            addr.s_addr = server->server_addr;
            XML_ELT(msp, ODCI_ADDRESS, "%s", inet_ntoa(addr));
            if(app->server_mon_params != NULL) {
               XML_ELT(msp, ODCI_STATE, "%s", (server->status) ? "up" : "down");
            } else {
                XML_ELT(msp, ODCI_STATE, "%s", "up");
            }
            XML_CLOSE(msp, ODCI_SERVER);            
            
            server = TAILQ_NEXT(server, entries);
        }
        
        XML_CLOSE(msp, ODCI_SERVER_LIST);
    }
    
    XML_ELT(msp, ODCI_SESSION_COUNT, "%d", app->session_count);
    
    XML_CLOSE(msp, ODCI_APPLICATION_STATUS);
}


/**
 * Displays the configured message
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
equilibrium_show_status(mgmt_sock_t * msp,
                        parse_status_t * csb __unused,
                        char * unparsed)
{
    char * ss_name = NULL;
    char * app_name = NULL;
    char * token;
    eq_serviceset_t * ss;
    eq_app_t * app;
    
    if(unparsed != NULL) {
        token = strtok(unparsed, " "); // parse the rest
        while(token != NULL) {
            if(strcmp(token, DDLNAME_SHOW_EQ_STATUS_SERVICE_SET) == 0) {
                ss_name = strtok(NULL, " ");
            } else if(strcmp(token, DDLNAME_SHOW_EQ_STATUS_APPLICATION) == 0) {
                app_name = strtok(NULL, " ");
            } else {
                XML_RAW(msp, 0, xml_error_str(TRUE, NULL, NULL, NULL, NULL,
                        token, NULL, "Command was not parsed correctly"));
                return 0;
            }
            token = strtok(NULL, " ");
        }
        
        if(app_name != NULL && ss_name == NULL) {
            // this is not allowed but we can't enforce it through DDL 
            //  in SDK apps currently

            XML_RAW(msp, 0, xml_error_str(FALSE, NULL, NULL, NULL, NULL, 
               "application", NULL, 
               "Service set must be specified if an application is specified"));
            
            return 0;
        }
    }
    
    XML_OPEN(msp, ODCI_EQUILIBRIUM_STATUS);

    if(ss_name == NULL) {
        // show all service sets
        ss = next_service_set(NULL);
        while(ss != NULL) {
            // show all applications of this service set
            app = next_application(ss, NULL);
            while(app != NULL) {
                show_application_status(msp, ss->svc_set_name, app);
                app = next_application(ss, app);
            }
            ss = next_service_set(ss);
        }
    } else {
        if((ss = find_service_set(ss_name)) != NULL) {
            // show only selected service set
            if(app_name == NULL) {
                // show all applications
                app = next_application(ss, NULL);
                while(app != NULL) {
                    show_application_status(msp, ss->svc_set_name, app);
                    app = next_application(ss, app);
                }
            } else {
                // show only selected application
                if((app = find_application(ss, app_name)) != NULL) {
                    show_application_status(msp, ss->svc_set_name, app);
                } else {
                    XML_RAW(msp, 0, xml_error_str(FALSE, NULL, NULL, NULL, NULL, 
                       app_name, NULL, "Application not found"));
                }
                
            }
        } else {
            XML_RAW(msp, 0, xml_error_str(FALSE, NULL, NULL, NULL, NULL, 
               ss_name, NULL, "Service set not found"));
        }
    }
    
    XML_CLOSE(msp, ODCI_EQUILIBRIUM_STATUS);
    
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
 *  show sync equilibrium ... commands
 */
static const parse_menu_t show_sync_equilibrium_menu[] = {
    { "status", NULL, 0, NULL, equilibrium_show_status },
    { NULL, NULL, 0, NULL, NULL }
};

/**
 *  show sync ... commands
 */
static const parse_menu_t show_sync_menu[] = {
    { "equilibrium", NULL, 0, show_sync_equilibrium_menu, NULL },
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
 *  main menu of commands
 */
const parse_menu_t master_menu[] = {
    { "show", NULL, 0, show_menu, NULL }, 
    { NULL, NULL, 0, NULL, NULL }
};


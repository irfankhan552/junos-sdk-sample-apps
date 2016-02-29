/*
 * $Id: hellopics-mgmt_config.c 365138 2010-02-27 10:16:06Z builder $
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
 * @file hellopics-mgmt_config.c
 * @brief Relating to loading and storing the configuration data
 * 
 * These functions will parse and load the configuration data.
 */

#include <ddl/dax.h>
#include <sys/socket.h>
#include <netdb.h>
#include <jnx/junos_trace.h>
#include <jnx/junos_re_redundancy.h>
#include <jnx/junos_re_info.h>
#include "hellopics-mgmt_logging.h"
#include "hellopics-mgmt_config.h"
#include "hellopics-mgmt_conn.h"
#include "hellopics-mgmt_ha.h"

#include HELLOPICS_OUT_H

#define NAME_LENGTH 64
/*** Data Structures ***/
volatile boolean is_master;


/*** STATIC/INTERNAL Functions ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Read daemon configuration from the database.
 * (nothing to do except traceoptions)
 * 
 * @param[in] check
 *     1 if this function being invoked because of a commit check
 * 
 * @return SUCCESS (0) successfully loaded, -1 in case of error
 * 
 * @note Do not use ERRMSG during config check.
 */
int
hellopics_config_read (int check UNUSED)
{
    junos_trace(HELLOPICS_TRACEFLAG_NORMAL, 
            "%s: Loaded hellopics configuration", __func__);

    return SUCCESS;
}

/**
 * Initialize the application as Active or Backup depending on 
 * the mastership of RE. Initialize the replication module for
 * synchronization between the Active and Backup.
 */
void
init_config(void)
{
    boolean mastership;
    char master_re_name[NAME_LENGTH];
    in_addr_t re_addr;
    if((mastership = junos_re_redundancy_is_master()) == TRUE) {
        LOG(LOG_INFO,
            "%s: This is the master: Setting Mastership as MASTER",
            __func__);
         set_mastership(TRUE,0); 
    }
    else {
        LOG(LOG_INFO,
            "%s: This is the slave: Setting Mastership as SLAVE",
            __func__);
        if(junos_re_info_hostname(JUNOS_ROUTING_ENGINE_MASTER,
                                  master_re_name, NAME_LENGTH) == -1){
             LOG(LOG_ERR,
            "%s: This is the slave: Unable to get the Master RE hostname",
            __func__);
        }
        LOG(LOG_INFO,
            "%s: This is the slave: Master RE Name is %s",
            __func__, master_re_name);
      if(strcmp(master_re_name, "re0") == 0){
         re_addr = HELLOPICS_MASTER_ADDR;
      }
      else {
         re_addr = HELLOPICS_MASTER_ADDR+1;
      }
      set_mastership(FALSE,re_addr); 
    }
}


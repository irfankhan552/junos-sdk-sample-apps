/* $Id: jnx-ifinfod_config.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-ifinfod config declarations and global variables
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

#ifndef __JNX_IFINFOD_CONFIG_H__
#define __JNX_IFINFOD_CONFIG_H__

#include <jnx/junos_kcom.h>

#define ALIAS_NAMELEN 60

/**
 * ifinfod Interface  patricia tree node
 */
typedef struct ifinfod_intf_ {
   patnode intf_node;
   char intf_name[KCOM_IFNAMELEN+1];
   int  intf_id;
   int  intf_status;
   char intf_alias_name[ALIAS_NAMELEN+1];
} ifinfod_intf_t;

/**
 *
 *Function : ifinfod_config_init
 *Purpose  : This function initializes the ifinfod_root node.
 *           This uses patricia_root_init API to initialize the patroot.
 *
 * @param[in] N/A
 *
 *
 * @return N/A
 */
void ifinfod_config_init (void);

/**
 *
 *Function  : ifinfod_config_read
 *Purpose   : This function reads basic ifinfo configuration parameters and
 *            creates a file under /var/tmp/ifinfo_sdk_config file.
 *
 * @param[in] check
 *       int to check if it is a commit check or a actual commit
 *
 *
 * @return 0 on success and -1 on failure
 *
 */
int ifinfod_config_read (int);



/**
 *
 *Function : ifinfod_node_first
 *Purpose  : This function finds the first node the partricia tree.
 *           This uses patricia_find_next API to get the first node in the tree.
 *
 *
 * @param[in] *ifinfod_node
 *       Pointer to ifinfod_intf_t patricia tree node
 *
 * @return pointer to next node in the tree  on success or NULL on failure
 */
ifinfod_intf_t * ifinfod_node_first (void);

/**
 *
 *Function : ifinfod_node_next
 *Purpose  : This returns the next ifinfod_intf node in the patricia tree.
 *           This uses patricia_find_next API to get the next node in the tree.
 *
 * @param[in] *ifinfod_node
 *       Pointer to ifinfod_intf_t patricia tree node
 * 
 * @return pointer to next node in the tree  on success or NULL on failure
 */
ifinfod_intf_t * ifinfod_node_next (ifinfod_intf_t *);

#endif /* __JNX_IFINFOD_CONFIG_H__ */

/*
 * $Id: jnx-ifinfod_config.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 *jnx-ifinfod_config.c : jnx-ifinfod config read functions
 * 
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2005-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <string.h>
#include <sys/types.h>
#include <ddl/dax.h>
#include <sys/stat.h>
#include <string.h>

#include <unistd.h> /* For raw close */
#include <fcntl.h> /* For raw open */
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>

#include JNX_IFINFOD_OUT_H
#include JNX_IFINFOD_SEQUENCE_H

#include <jnx/junos_trace.h>
#include <jnx/junos_kcom.h>

#include "jnx-ifinfod_kcom.h"
#include "jnx-ifinfod_config.h"
#include "jnx-ifinfod_util.h"


static patroot ifinfod_root;
PATNODE_TO_STRUCT(ifinfod_find_entry, ifinfod_intf_t, intf_node)


/**
 *
 *Function : ifinfod_node_next 
 *Purpose  : This returns the next ifinfod_intf node in the patricia tree
 *           This uses patricia_find_next API to get the next node in the tree.
 *
 * @param[in] *ifinfod_node
 *       Pointer to ifinfod_intf_t patricia tree node 
 *
 *
 * @return pointer to next node in the tree  on success or NULL on failure
 */
ifinfod_intf_t *
ifinfod_node_next (ifinfod_intf_t *ifinfod_node)
{
    return ifinfod_find_entry(patricia_find_next(&ifinfod_root, 
                     ifinfod_node ? &ifinfod_node->intf_node :
                                                     NULL));
}
 

/**
 *
 *Function : ifinfod_node_first
 *Purpose  : This function finds the first node the partricia tree
 *           This uses patricia_find_next API to get the first node in the tree.
 *
 *
 * @param[in] *ifinfod_node
 *       Pointer to ifinfod_intf_t patricia tree node
 *
 *
 * @return pointer to next node in the tree  on success or NULL on failure
 */
ifinfod_intf_t *
ifinfod_node_first (void)
{
    return ifinfod_find_entry(patricia_find_next(&ifinfod_root, NULL));
}


/**
 *
 *Function : ifinfod_config_init
 *Purpose  : This function initializes the ifinfod_root node
 *           This uses patricia_root_init API to initialize the patroot
 *
 *
 * @param[in] N/A
 *
 *
 * @return N/A
 */
void 
ifinfod_config_init (void)
{
    patricia_root_init(&ifinfod_root, FALSE, KCOM_IFNAMELEN, 0);
}

/**
 *
 *Function : ifinfod_add_node
 *Purpose  : This function adds a ifinfod_intf node into the patricia tree
 *           This uses patricia_add API to get add a ifinfod_intf_t node
 *
 *
 * @param[in] *intf_name
 *       Interface name String
 *
 * @param[in] *intf_alias
 *       Interface name alias String
 *
 *
 * @return pointer to the added node in the tree  on success or NULL on failure
 */
static ifinfod_intf_t *
ifinfod_add_node (const char *intf_name, const char *intf_alias)
{

    kcom_ifdev_t *intf_ifdev;
    ifinfod_intf_t *ifinfod_data;

    junos_trace(JNX_IFINFOD_TRACEFLAG_CONFIG,
		"%s adding %s to ifinfo tree", __func__,intf_name);


    ifinfod_data = calloc(1, sizeof(ifinfod_intf_t));
    if (ifinfod_data == NULL) {
        SYSLOG(JNX_IFINFOD_ALLOC_MSG,LOG_EMERG,
               "%s memory allocation failure",__func__);
    }
    strlcpy(ifinfod_data->intf_name, intf_name, KCOM_IFNAMELEN);

    INSIST_ERR(ifinfod_data->intf_name != NULL);

    strlcpy(ifinfod_data->intf_alias_name, intf_alias, ALIAS_NAMELEN); 
    INSIST_ERR(ifinfod_data->intf_alias_name != NULL);


    /* Get kcom get_interface index for this interface */
    intf_ifdev = calloc(1, sizeof(kcom_ifdev_t));     
    if (intf_ifdev == NULL) {
        SYSLOG(JNX_IFINFOD_ALLOC_MSG,LOG_EMERG,
             "%s memory allocation failure",__func__);
    }
    junos_kcom_ifd_get_by_name(ifinfod_data->intf_name, intf_ifdev);

    /* Assign the interface index to ifinfod_data node */
    ifinfod_data->intf_id=intf_ifdev->ifdev_index;

    ifinfod_data->intf_status=(junos_kcom_ifd_down(intf_ifdev) ? 0 : 1);
   

    patricia_node_init_length(&ifinfod_data->intf_node,
			      strlen(ifinfod_data->intf_name)+1);

    if (!patricia_add(&ifinfod_root, &ifinfod_data->intf_node)) {

        junos_trace(JNX_IFINFOD_TRACEFLAG_CONFIG,
		    "%s patricia fail to add node",__func__);

    } else {
        junos_trace(JNX_IFINFOD_TRACEFLAG_CONFIG,
		    "%s patricia add node succeeded",__func__);
    }

    return ifinfod_data;
}


/**
 *
 *Function : ifinfod_free_node
 *Purpose  : This function frees ifinfod_intf node
 *
 *
 * @param[in] *ifinfod_data
 *       pointer to the ifinfod_intf_t node that needs to be freed
 *
 * @param[in] *intf_alias
 *       Interface name alias String
 *
 *
 * @return pointer to the added node in the tree  on success or NULL on failure
 */
static void
ifinfod_free_node (ifinfod_intf_t *ifinfod_data)
{
   free(ifinfod_data);
}


/**
 *
 *Function : ifinfod_delete_node
 *Purpose  : This function deletes ifinfod_intf node from the tree
 *           This uses patricia_delete API calls
 *
 * @param[in] *ifinfod_data
 *       pointer to the ifinfod_intf_t node that needs to be deleted
 *
 *
 * @return N/A
 */
static void
ifinfod_delete_node (ifinfod_intf_t *ifinfod_data) 
{
   junos_trace(JNX_IFINFOD_TRACEFLAG_CONFIG,"%s deleting node %s",__func__,
	       ifinfod_data->intf_name);

   if ( !patricia_delete(&ifinfod_root, &ifinfod_data->intf_node)) {

       junos_trace(JNX_IFINFOD_TRACEFLAG_CONFIG,"%s failed deleting node %s",
		   __func__,ifinfod_data->intf_name);
   }
  ifinfod_free_node(ifinfod_data);
}



/**
 *
 *Function : ifinfod_tree_cleanup
 *Purpose  : This function cleans up the pattree built to maintain
 *           ifinfod_intf_t list.This is called everytime the tree is 
 *           rebuilt on a SIGHUP
 *
 * @param[in] *ifinfod_data
 *       pointer to the ifinfod_intf_t node that needs to be deleted
 *
 *
 * @return N/A
 */
static void 
ifinfod_tree_cleanup (void)
{
    ifinfod_intf_t *ifinfod_data = NULL;
    
    while ( (ifinfod_data=ifinfod_node_first()) != NULL) {
        ifinfod_delete_node(ifinfod_data);
    }
}
  
/**
 *
 *Function  : ifinfod_config_read
 *Purpose   : This function reads basic ifinfo configuration parameters
 *            This also creates a file under /var/tmp/ifinfo_sdk_config file
 *
 * @param[in] check
 *      int to check if it is a commit check or a actual commit
 *
 *
 * @return true on success and -1 on failure
 *
 */
int
ifinfod_config_read (int check UNUSED)
{
    ddl_handle_t *top = NULL;
    ddl_handle_t *dop = NULL;

    const char *if_config_name[] = {"jnx-ifinfo","interfaces",NULL};
    char intf_name[KCOM_IFNAMELEN], intf_alias[ALIAS_NAMELEN] = "";
    char buf[80];
    int status = 0;

    int fd;

    if (dax_get_object_by_path(NULL, if_config_name, &top, FALSE)) {

        /* if top hasnt changed,ignore this object*/
        if (dax_is_changed(top) == FALSE){
            goto exit;
        }

        /* clear and delete the tree first */
        ifinfod_tree_cleanup();

        /*****write SDK config to a file in /var/tmp - Raw IO ***/
        umask(0);
        fd = open("/var/tmp/jnx_ifinfo_sdk_config",
                  O_CREAT|O_TRUNC|O_RDWR,S_IRWXU |S_IRGRP|S_IROTH);

        memset(buf, 0, sizeof(buf));
        strlcpy(buf,"jnx-ifinfo {\n", sizeof(buf));
        ifinfod_write(fd, buf, strlen(buf));

        memset(buf, 0, sizeof(buf));
        strlcpy(buf,"    interfaces { \n", sizeof(buf));
        ifinfod_write(fd, buf, strlen(buf));

        /* visit ifinfo container object */
        while (status == 0 && dax_visit_container(top, &dop)) {
        
            if (!dax_get_stringr_by_name(dop, "interface_name",
					 intf_name, sizeof(intf_name))) {
                status = ENOENT; /* This is a mandatory argument */
            }                    /* ENOENT -meaning no such file or directory */

            dax_get_stringr_by_aid(dop, IFINFO_ALIAS, 
				   intf_alias, sizeof(intf_alias));

            if (status == 0) { /* Everything is fine, add the node */
               strlcpy(buf,"\t", sizeof(buf));
               strlcat(buf, intf_name, sizeof(buf));
               strlcat(buf," {\n", sizeof(buf));
               ifinfod_write(fd, buf, strlen(buf));

               strlcpy(buf, "\t    alias-name ", sizeof(buf));
               strlcat(buf, intf_alias, sizeof(buf));
               strlcat(buf, ";\n\t}\n", sizeof(buf));
               ifinfod_write(fd, buf, strlen(buf));

               ifinfod_add_node(intf_name, intf_alias);
            }
        }

        strlcpy(buf, "    }\n}", sizeof(buf));
        ifinfod_write(fd, buf, strlen(buf));

        close(fd);
    } else {
        ifinfod_tree_cleanup();
    }

exit:
    if (top){
        dax_release_object(&top);
    }
    if (dop) {
        dax_release_object(&dop);
    }
    return status;
}

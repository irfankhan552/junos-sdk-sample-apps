/*
 * $Id: ped_filter.c 366969 2010-03-09 15:30:13Z taoliu $
 * 
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * $Id: ped_filter.c 366969 2010-03-09 15:30:13Z taoliu $
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file ped_filter.c
 * @brief Works with filters
 * 
 * Create PFD filter and applies filters to interfaces
 * 
 * Prior to 10.2 All filters were created with op scripts, but in 10.2 
 * we switched the PFD filter to work with with libdfwd to demonstrate the table
 * redirect action.
 * 
 */

#include <sync/common.h>
#include <jnx/provider_info.h>
#include <jnx/junos_dfw_api.h>
#include "ped_filter.h"
#include "ped_services.h"
#include "ped_script.h"

#include PE_OUT_H

/*** Constants ***/

/**
 * File name of the op script to create filter and routing instance
 */
#define INIT_FILTER_FILENAME "ped_init_filter.xsl"

/**
 * File name of the op script to apply filters
 */
#define APPLY_FILTER_FILENAME "ped_update_interface_filter.xsl"

/*** Data Structures ***/

static junos_dfw_session_handle_t dfw_handle; ///< the handle for all things DFW
static junos_dfw_client_id_t      cid;        ///< assigned ID w/ DFW
static boolean pfd_on = FALSE;                ///< Apply the PFD filter
static boolean ready = FALSE;                 ///< DFW is ready to use
static junos_dfw_filter_info_t pfd_filter_info; ///< the pfd filter
static char * make_pfd_filter_on_int;          ///< create(d) pfd on this interface

/*** STATIC/INTERNAL Functions ***/

/**
 * Abstract unit number from interface name and get rid of the ".<unit>" on the 
 * ifname
 * 
 * @param[in] ifname
 *     Name of interface
 * 
 * @return
 *      Unit number if successful; -1 otherwise
 */
static int
get_unit(char * ifname)
{
    char * p = ifname;
    
    while(*p != '\0' && *p != '.') {
        ++p;
    }
    
    if(*p != '\0') { // found dot
        *p = '\0'; // turn the dot to a \0
        ++p;
        return strtol(p, NULL, 0);
    } else {
        return -1;
    }
}


/**
 * The connection to DFWD was accepted or has failed
 * 
 * @param[in] handle
 *      DFW handle
 * 
 * @param[in] code
 *      code to indicate status of connection
 *
 * @param[in] client_id_list
 *      List of client IDs
 * 
 * @param[in] num_client_ids
 *      Number of IDs in client_id_list
 *  
 */
static void
session_connect(junos_dfw_session_handle_t handle,
                junos_dfw_session_connect_return_t code,
                junos_dfw_client_id_t * client_id_list,
                int num_client_ids)
{
    junos_dfw_term_info_t i_dti;
    junos_dfw_trans_handle_t trans_hdl;
    
    INSIST_ERR(handle == dfw_handle);
    
    if(code == JUNOS_DFW_SC_SUCCESS) {
        
        if(num_client_ids > 1) {
            ERRMSG(PED, LOG_ERR,
                    "%s: Found more than one client ID", __func__);            
        }
        
        if(client_id_list != NULL) {
            
            cid = client_id_list[0]; // get dynamic ID assigned 
            junos_trace(PED_TRACEFLAG_FILTER,
                        "%s: Connected to dfwd", __func__);
            
            if(junos_dfw_trans_purge(dfw_handle,
                    client_id_list, num_client_ids)) {
                ERRMSG(PED, LOG_ERR,
                        "%s: Failed to perform initial DFWD purge", __func__);
            }
            
            ready = TRUE;
            
            if(make_pfd_filter_on_int != NULL) {
                // Create filter pointing to PFD routing instance
                
                junos_trace(PED_TRACEFLAG_FILTER, "%s: creating PFD filter",
                        __func__);
                
                bzero(&pfd_filter_info, sizeof(pfd_filter_info));
                snprintf(pfd_filter_info.namestr_key, 
                     sizeof(pfd_filter_info.namestr_key), "TO_PFD_ON_INGRESS");
                pfd_filter_info.owner_client_id = cid;
                pfd_filter_info.addr_family = JUNOS_DFW_FILTER_AF_INET;

                pfd_filter_info.type = JUNOS_DFW_FILTER_TYPE_CLASSIC;

                // Create the filter add transaction 
                
                if(junos_dfw_filter_trans_alloc(&pfd_filter_info,
                        JUNOS_DFW_FILTER_OP_ADD, &trans_hdl)) {
                    ERRMSG(PED, LOG_ERR,
                           "%s: junos_dfw_filter_trans_alloc failed", __func__);
                    return;
                }

                // Add a term to the filter
                
                bzero(&i_dti, sizeof(i_dti));
                strlcpy(i_dti.namestr_key, "TO_PFD_TERM", 
                        sizeof(i_dti.namestr_key));
                i_dti.type = JUNOS_DFW_TERM_TYPE_ORDERED;
                i_dti.property.order.term_adj_type = JUNOS_DFW_TERM_ADJ_PREV;

                if(junos_dfw_term_start(trans_hdl, &i_dti,
                        JUNOS_DFW_TERM_OP_ADD)) {
                    ERRMSG(PED, LOG_ERR,
                            "%s: junos_dfw_term_start failed", __func__);
                    junos_dfw_trans_handle_free(trans_hdl);
                    return;
                }
                
                // Specify term action as redirect (routing-instance)
                // pfd_forwarding is the name of the new VR we just created above
                
                if(junos_dfw_term_action_redirect(trans_hdl, "pfd_forwarding")) {
                    ERRMSG(PED, LOG_ERR,
                        "%s: junos_dfw_term_action_policer failed", __func__);
                    junos_dfw_trans_handle_free(trans_hdl);
                    return;
                }
                
                if(junos_dfw_term_end(trans_hdl) < 0) {
                    ERRMSG(PED, LOG_ERR,
                            "%s: junos_dfw_term_end failed", __func__);
                    junos_dfw_trans_handle_free(trans_hdl);
                    return;
                }
                
                // Send transaction to finish the filter add
                
                if(junos_dfw_trans_send(dfw_handle, trans_hdl, cid, 0)) {
                    ERRMSG(PED, LOG_ERR,
                            "%s: junos_dfw_trans_send failed", __func__);
                    junos_dfw_trans_handle_free(trans_hdl);
                    return;
                }
                
                junos_dfw_trans_handle_free(trans_hdl);
                make_pfd_filter_on_int = NULL;
                update_policies(); // go thru policies to check for PFD filters
            }
            
            return;
        } else {
            ERRMSG(PED, LOG_ERR, "%s: Found no client ID", __func__);
        }
    } else if(code == JUNOS_DFW_SC_FAILED_VERSION) {
        ERRMSG(PED, LOG_ERR,
                "%s: Connecting to dfwd failed. Bad libdfwd version", __func__);
    } else {
        ERRMSG(PED, LOG_ERR, "%s: Connecting to dfwd failed", __func__);
    }
    ready = FALSE;
}


/**
 * The connection to DFWD has gone down unexpectedly
 * 
 * @param[in] handle
 *      DFW handle
 * 
 * @param[in] state
 *      connection state
 */
static void
session_state_changed(junos_dfw_session_handle_t handle,
                      junos_dfw_session_state_t state)
{
    INSIST_ERR(handle == dfw_handle);
    ERRMSG(PED, LOG_ALERT,
           "%s: PED's connection to DFWD going %s", __func__,
            (state == JUNOS_DFW_SS_DOWN) ? "DOWN" : "??");
    ready = FALSE;
    
    /*
     * release the session handle
     */
    junos_dfw_session_handle_free(handle);

    dfw_handle = NULL;

    // TODO start a retry timer if you want to handle this error case
}


/**
 * Report DFW transaction as rejected
 * 
 * @param[in] handle
 *      DFW handle
 * 
 * @param[in] ctx
 *      Transaction context
 * 
 * @param[in] reason_info
 *      Reason for rejection
 */
static void
transaction_rejected(junos_dfw_session_handle_t handle, 
                     uint64_t ctx, 
                     junos_dfw_trans_reject_reason_info_t reason_info)
{
    #define ERR_BUF_SIZE 256
    char errbuf[ERR_BUF_SIZE];
    
    INSIST_ERR(handle == dfw_handle);
    
    junos_dfw_trans_reject_reason_strerr(reason_info, errbuf, ERR_BUF_SIZE);
    
    ERRMSG(PED, LOG_WARNING, 
           "%s: Context %lld failed: %s", __func__, ctx, errbuf);
}


/**
 * Report DFW transaction as accepted
 * 
 * @param[in] handle
 *      DFW handle
 * 
 * @param[in] ctx
 *      Transaction context
 * 
 * @param[in] reason_info
 *      The transaction index assigned by DFWD
 */
static void
transaction_accepted(junos_dfw_session_handle_t handle, 
                     uint64_t ctx,
                     uint32_t dfw_idx)
{
    INSIST_ERR(handle == dfw_handle);
    
    junos_trace(PED_TRACEFLAG_FILTER, "%s: Context %lld accepted as %d",
                __func__, ctx, dfw_idx);
}



/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Is the PFD filter on
 */
boolean
is_pfd_filter_on(void)
{
    return pfd_on;
}


/**
 * Apply PFD filter automatically
 */
void
turn_on_pfd_filter(void)
{
    pfd_on = TRUE;
}


/**
 * Don't PFD filter automatically
 */
void
turn_off_pfd_filter(void)
{
    pfd_on = FALSE;
}


/**
 * Initialize the connection to the dfwd
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      0 if successful; otherwise -1 with an error message.
 */
int
init_dfw(evContext ctx)
{
    int rc;
    junos_dfw_client_functions_t funcs;
    junos_dfw_conn_addr_t conn_addr;
    junos_dfw_sdk_app_id_t app_id;
    
    junos_trace(PED_TRACEFLAG_FILTER, "%s", __func__);

    ready = FALSE;
    dfw_handle = NULL;
    make_pfd_filter_on_int = NULL;
    
    funcs.session_connect_cb = session_connect;
    funcs.session_state_change_cb = session_state_changed;
    funcs.trans_rejected_cb = transaction_rejected;
    funcs.trans_accepted_cb = transaction_accepted;

    rc = junos_dfw_session_handle_alloc(&dfw_handle, &funcs);
    
    if(rc != 0) {
        ERRMSG(PED, LOG_ERR, "%s: Cannot allocate handle for a dynamic "
                "firewall session (%m)", __func__);
        dfw_handle = NULL;
        return -1;
    }
    
    conn_addr.addr_family = JUNOS_DFW_CONN_AF_INET;
    conn_addr.addr.dfwd_inet.dfwd_server_port = JUNOS_DFW_DEFAULT_PORT;
        
    conn_addr.addr.dfwd_inet.dfwd_host_name = 
        malloc(strlen(JUNOS_DFW_DEFAULT_LOCAL_ADDR) + 1);
    INSIST_ERR(conn_addr.addr.dfwd_inet.dfwd_host_name != NULL);
    strlcpy(conn_addr.addr.dfwd_inet.dfwd_host_name, 
            JUNOS_DFW_DEFAULT_LOCAL_ADDR,
            strlen(JUNOS_DFW_DEFAULT_LOCAL_ADDR) + 1);
    
    app_id = 1; // Would be origin ID if done in the RE SDK
    
    rc = junos_dfw_session_open(dfw_handle, &conn_addr, app_id, ctx);
    
    if(rc != 0) {
        if(errno == EISCONN) {
            junos_trace(PED_TRACEFLAG_FILTER, "%s: Connection to DWFD established", __func__);
            ready = TRUE;            
        } else {
            ERRMSG(PED, LOG_ERR, "%s: Cannot setup dynamic firewall connection (%m)",
                    __func__);
            junos_dfw_session_handle_free(dfw_handle);
            dfw_handle = NULL;
            return -1;
        }
    }

    return 0;
}


/**
 * Close down and free all resources
 */
void
shutdown_dfw(void)
{
    ready = FALSE;
    if(junos_dfw_trans_purge(dfw_handle, &cid, 1)) {
        ERRMSG(PED, LOG_ERR, "%s: Failed to perform DFWD purge",
                __func__);
    }
    
    if(dfw_handle) {
        junos_dfw_session_close(dfw_handle);
        junos_dfw_session_handle_free(dfw_handle);
        dfw_handle = NULL;
    }
}



/**
 * Create the configuration necessary for the PFD service routes to work
 * 
 * @param[in] interface_name
 *     Name of interface in the PFD routing instance
 * 
 * @return
 *      TRUE if successful; FALSE otherwise
 */
boolean
init_pfd_filter(char * interface_name)
{
    char *cmd = NULL;
    
    junos_dfw_term_info_t i_dti;
    junos_dfw_trans_handle_t trans_hdl;
    
    INSIST(interface_name != NULL);
    INSIST_ERR(provider_info_get_prefix() != NULL);
    
    junos_trace(PED_TRACEFLAG_FILTER, "%s(%s)", __func__, interface_name);
    
    // using op scripts create this config:
    
    /*
    
    pfd_forwarding {
            instance-type virtual-router;
            interface ms-x/y/0.100;
    }
    
    */
    if(asprintf(&cmd, "op %s-" INIT_FILTER_FILENAME " interface %s ", 
            provider_info_get_prefix(), interface_name) < 0) {
        ERRMSG(PED, LOG_ERR,
                "%s: Assemble command failed!", __func__);
        return FALSE;
    } 
    if(exec_op_script(cmd) < 0) {
        ERRMSG(PED, LOG_ERR,
                "%s: Execute script failed!", __func__);
        free(cmd);
        return FALSE;
    }

    free(cmd);
    
    if(!ready) {
        ERRMSG(PED, LOG_WARNING,
                "%s: PFD filter not created due to DFW != ready", __func__);
        make_pfd_filter_on_int = strdup(interface_name);
        return TRUE;
    }

    // Create filter pointing to PFD routing instance
    
    bzero(&pfd_filter_info, sizeof(pfd_filter_info));
    snprintf(pfd_filter_info.namestr_key, sizeof(pfd_filter_info.namestr_key),
            "TO_PFD_ON_INGRESS");
    pfd_filter_info.owner_client_id = cid;
    pfd_filter_info.addr_family = JUNOS_DFW_FILTER_AF_INET;

    pfd_filter_info.type = JUNOS_DFW_FILTER_TYPE_CLASSIC;

    // Create the filter add transaction 
    
    if(junos_dfw_filter_trans_alloc(&pfd_filter_info,
            JUNOS_DFW_FILTER_OP_ADD, &trans_hdl)) {
        ERRMSG(PED, LOG_ERR,
                "%s: junos_dfw_filter_trans_alloc failed", __func__);
        return FALSE;
    }

    // Add a term to the filter
    
    bzero(&i_dti, sizeof(i_dti));
    strlcpy(i_dti.namestr_key, "TO_PFD_TERM", sizeof(i_dti.namestr_key));
    i_dti.type = JUNOS_DFW_TERM_TYPE_ORDERED;
    i_dti.property.order.term_adj_type = JUNOS_DFW_TERM_ADJ_PREV;

    if(junos_dfw_term_start(trans_hdl, &i_dti, JUNOS_DFW_TERM_OP_ADD)) {
        ERRMSG(PED, LOG_ERR, "%s: junos_dfw_term_start failed",
                __func__);
        goto failed;
    }
    
    // Specify term action as redirect (routing-instance)
    // pfd_forwarding is the name of the new VR we just created above
    
    if(junos_dfw_term_action_redirect(trans_hdl, "pfd_forwarding")) {
        ERRMSG(PED, LOG_ERR, "%s: junos_dfw_term_action_policer failed",
                __func__);
        goto failed;
    }
    
    if(junos_dfw_term_end(trans_hdl) < 0) {
        ERRMSG(PED, LOG_ERR, "%s: junos_dfw_term_end failed", __func__);
        goto failed;
    }
    
    // Send transaction to finish the filter add
    
    if(junos_dfw_trans_send(dfw_handle, trans_hdl, cid, 0)) {
        ERRMSG(PED, LOG_ERR,"%s: junos_dfw_trans_send failed",__func__);
        goto failed;
    }
    
    junos_dfw_trans_handle_free(trans_hdl);
    return TRUE;

failed:
    junos_dfw_trans_handle_free(trans_hdl);
    return FALSE;
}




/**
 * Apply PFD filters on an interface
 * 
 * @param[in] interface_name
 *     Name of interface to apply filters on
 * 
 * @return
 *      TRUE if successful; FALSE otherwise
 */
boolean
apply_pfd_filter_to_interface(char * interface_name)
{
    char *ifname = NULL;
    int unit;
    junos_dfw_filter_attach_info_t dfai;
    junos_dfw_trans_handle_t trans_hdl;

    INSIST(interface_name != NULL);
    
    junos_trace(PED_TRACEFLAG_FILTER, "%s(%s)", __func__, interface_name);
    
    ifname = strdup(interface_name);
    if(ifname == NULL) {
        ERRMSG(PED, LOG_ERR,
                "%s: Duplicate interface name failed!", __func__);
        return FALSE;
    }
    if((unit = get_unit(ifname) < 0)) {
        ERRMSG(PED, LOG_ERR,
                "%s: Get unit number failed!", __func__);
        free(ifname);
        return FALSE;
    }
    
    if(!pfd_on) {
        free(ifname);
        return TRUE;
    }
    
    if(!ready) {
        ERRMSG(PED, LOG_WARNING,
                "%s: PFD filter not attached due to DFW != ready", __func__);
        free(ifname);
        return TRUE;
    }
    
    // Apply the PFD filter to the interface
    
    dfai.attach_point = JUNOS_DFW_FILTER_ATTACH_POINT_INPUT_INTF;
    strlcpy(dfai.type.intf.ifd_name, ifname,
            sizeof(dfai.type.intf.ifd_name));
    dfai.type.intf.sub_unit = unit;
    
    free(ifname);
    
    if(junos_dfw_filter_attach_trans_alloc(&pfd_filter_info,
            &dfai, &trans_hdl)){
        ERRMSG(PED, LOG_ERR,
                "%s: junos_dfw_filter_attach_trans_alloc failed", __func__);
        junos_dfw_trans_handle_free(trans_hdl);
        return FALSE;
    }
    
    if(junos_dfw_trans_send(dfw_handle, trans_hdl, cid, 0)) {
        ERRMSG(PED, LOG_ERR,
                "%s: junos_dfw_trans_send (attach) failed", __func__);
        junos_dfw_trans_handle_free(trans_hdl);
        return FALSE;
    }
    
    junos_dfw_trans_handle_free(trans_hdl);
    return TRUE;
}



/**
 * Remove PFD filter from an interface
 * 
 * @param[in] interface_name
 *     Name of interface to remove filters on
 */
void
remove_pfd_filter_from_interface(char * interface_name)
{
    char *ifname = NULL;
    int unit;
    junos_dfw_filter_attach_info_t dfai;
    junos_dfw_trans_handle_t trans_hdl;
    
    INSIST(interface_name != NULL);

    junos_trace(PED_TRACEFLAG_FILTER, "%s(%s)", __func__, interface_name);
    
    ifname = strdup(interface_name);
    if(ifname == NULL) {
        ERRMSG(PED, LOG_ERR,
                "%s: Duplicate interface name failed!", __func__);
    
    } 
    if((unit = get_unit(ifname) < 0)) {
        ERRMSG(PED, LOG_ERR,
                "%s: Get unit number failed!", __func__);
        free(ifname);
        return;
    }
    
    if(!pfd_on) {
        free(ifname);
        return;
    }
    
    if(!ready) {
        ERRMSG(PED, LOG_WARNING,
                "%s: PFD filter not detached due to DFW != ready", __func__);
        free(ifname);
        return;
    }
    
    // detach the PFD filter from the interface
    
    dfai.attach_point = JUNOS_DFW_FILTER_ATTACH_POINT_INPUT_INTF;
    strlcpy(dfai.type.intf.ifd_name, ifname, sizeof(dfai.type.intf.ifd_name));
    dfai.type.intf.sub_unit = unit;
    
    free(ifname);
    
    if(junos_dfw_filter_detach_trans_alloc(&pfd_filter_info, &dfai, 
            &trans_hdl)){
        ERRMSG(PED, LOG_ERR,
                "%s: junos_dfw_filter_detach_trans_alloc failed", __func__);
        junos_dfw_trans_handle_free(trans_hdl);
        return;
    }
    
    if(junos_dfw_trans_send(dfw_handle, trans_hdl, cid, 0)) {
        ERRMSG(PED, LOG_ERR,
                "%s: junos_dfw_trans_send (attach) failed", __func__);
        junos_dfw_trans_handle_free(trans_hdl);
        return;
    }
    
    junos_dfw_trans_handle_free(trans_hdl);
}


/**
 * Apply filters on an interface
 * 
 * @param[in] interface_name
 *     Name of interface to apply filters on
 * 
 * @param[in] filters
 *     The filters to apply
 * 
 * @return
 *      TRUE if successful; FALSE otherwise
 */
boolean
apply_filters_to_interface(
            char * interface_name,
            ped_policy_filter_t * filters)
{
    char *ifname = NULL;
    char *cmd = NULL;
    int unit;

    INSIST(interface_name != NULL);
    INSIST(filters != NULL);
    
    junos_trace(PED_TRACEFLAG_FILTER, "%s(%s)", __func__, interface_name);
    
    // using op scripts apply all filters
    
    ifname = strdup(interface_name);
    if(ifname == NULL) {
        ERRMSG(PED, LOG_ERR,
                "%s: Duplicate interface name failed!", __func__);
        return FALSE;
    }
    if((unit = get_unit(ifname) < 0)) {
        ERRMSG(PED, LOG_ERR,
                "%s: Get unit number failed!", __func__);
        free(ifname);
        return FALSE;
    }
    if(asprintf(&cmd, "op %s-" APPLY_FILTER_FILENAME
            " interface %s unit %d %s %s %s %s",
            provider_info_get_prefix(), 
            ifname, unit,
            filters->filter_data.input_filter[0] ? "input" : "",
            filters->filter_data.input_filter[0] ?
                    filters->filter_data.input_filter : "",
            filters->filter_data.output_filter[0] ? "output" : "",
            filters->filter_data.output_filter[0] ?
                    filters->filter_data.output_filter : "") < 0) {
        
        ERRMSG(PED, LOG_ERR,
                "%s: Assemble command failed!", __func__);
        
        free(ifname);
        free(cmd);
        return FALSE;    
    }
    if(exec_op_script(cmd) < 0) {
        ERRMSG(PED, LOG_ERR,
                "%s: Execute script failed!", __func__);
        free(ifname);
        free(cmd);
        return FALSE;
    }

    free(cmd);
    free(ifname);
    
    return TRUE;
}


/**
 * Remove all configured filters from an interface
 * 
 * @param[in] interface_name
 *     Name of interface to remove filters on
 * 
 * @return
 *      TRUE if successful; FALSE otherwise
 */
boolean
remove_filters_from_interface(char * interface_name)
{
    char *ifname = NULL;
    char *cmd = NULL;
    int unit;

    INSIST(interface_name != NULL);
    
    junos_trace(PED_TRACEFLAG_FILTER, "%s(%s)", __func__, interface_name);

    ifname = strdup(interface_name);
    if(ifname == NULL) {
        ERRMSG(PED, LOG_ERR,
            "%s: Duplicate interface name failed!", __func__);
        return FALSE;
    }
    if((unit = get_unit(ifname) < 0)) {
        ERRMSG(PED, LOG_ERR,
            "%s: Get unit number failed!", __func__);
        free(ifname);
        return FALSE;
    }
    if(asprintf(&cmd, "op %s-" APPLY_FILTER_FILENAME
            " interface %s unit %d",
            provider_info_get_prefix(),
            ifname, unit) < 0) {
        
        ERRMSG(PED, LOG_ERR,
                "%s: Assemble command failed!", __func__);
        
        free(ifname);
        return FALSE;

    }
    if(exec_op_script(cmd) < 0) {
        ERRMSG(PED, LOG_ERR,
                "%s: Execute script failed!", __func__);
        free(ifname);
        free(cmd);
        return FALSE;
    }
    
    free(ifname);
    free(cmd);
    return TRUE;
}

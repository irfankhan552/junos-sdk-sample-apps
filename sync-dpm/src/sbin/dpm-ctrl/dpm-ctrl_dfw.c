/*
 * $Id: dpm-ctrl_dfw.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-ctrl_dfw.c
 * @brief Relating to managing the firewall filters
 * 
 * These functions and types will manage the firewall filters.
 */


#include "dpm-ctrl_main.h"
#include <jnx/junos_dfw_api.h>
#include <jnx/provider_info.h>
#include "dpm-ctrl_dfw.h"
#include <errno.h>
#include <limits.h>


/*** Constants ***/

#define KB_BYTES        1024 ///< Bytes in a KB

#define FULL_PREFIX_LEN   32 ///< bit needed for a full mask of an address

/*** Data Structures ***/

static junos_dfw_session_handle_t dfw_handle; ///< the handle for all things DFW
static junos_dfw_client_id_t      dpm_cid;    ///< assigned ID when ready
static boolean                    ready = FALSE;  ///< ready to use (after init)
static boolean                    use_classic_filters = FALSE;
                                        ///< filter application mode (T/F value)

/*** STATIC/INTERNAL Functions ***/


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
    INSIST_ERR(handle == dfw_handle);
    
    if(code == JUNOS_DFW_SC_SUCCESS) {
        
        if(num_client_ids > 1) {
            LOG(LOG_ERR, "%s: Found more than one client ID", __func__);            
        }
        
        if(client_id_list != NULL) {
            
            dpm_cid = client_id_list[0]; // get dynamic ID assigned 
            LOG(LOG_INFO, "%s: Connected to dfwd", __func__);
            
            if(junos_dfw_trans_purge(dfw_handle,
                    client_id_list, num_client_ids)) {
                LOG(LOG_ERR, "%s: Failed to perform initial DFWD purge",
                        __func__);
            }
            
            ready = TRUE;
            return;
        } else {
            LOG(LOG_ERR, "%s: Found no client ID", __func__);
        }
    } else if(code == JUNOS_DFW_SC_FAILED_VERSION) {
        LOG(LOG_ERR, "%s: Connecting to dfwd failed. Bad libdfwd version",
                __func__);
    } else {
        LOG(LOG_ERR, "%s: Connecting to dfwd failed", __func__);
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
    LOG(LOG_ALERT, "%s: DPM-CTRL Connection to DFWD going %s", __func__,
            (state == JUNOS_DFW_SS_DOWN) ? "DOWN" : "??");
    ready = FALSE;
    
    /*
     * release the session handle
     */
    junos_dfw_session_handle_free(handle);

    dfw_handle = NULL;

    // TODO start a retry timer
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
    
    LOG(LOG_WARNING, "%s: Context %lld failed: %s", __func__, ctx, errbuf);
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
    
    LOG(LOG_INFO,"%s: Context %lld accepted as %d", __func__, ctx, dfw_idx);
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the connection to the dfwd
 * 
 * @param[in] ctx
 *      event context
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_dfw(evContext ctx)
{
    int rc;
    junos_dfw_client_functions_t funcs;
    junos_dfw_conn_addr_t conn_addr;
    junos_dfw_sdk_app_id_t app_id;

    ready = FALSE;
    dfw_handle = NULL;
    
    funcs.session_connect_cb = session_connect;
    funcs.session_state_change_cb = session_state_changed;
    funcs.trans_rejected_cb = transaction_rejected;
    funcs.trans_accepted_cb = transaction_accepted;

    rc = junos_dfw_session_handle_alloc(&dfw_handle, &funcs);
    
    if(rc != 0) {
        LOG(LOG_ERR, "%s: Cannot allocate handle for a dynamic "
                "firewall session (%m)", __func__);
        dfw_handle = NULL;
        return EFAIL;
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
            LOG(LOG_INFO, "%s: Connection to DWFD established", __func__);
            ready = TRUE;            
        } else {
            LOG(LOG_ERR, "%s: Cannot setup dynamic firewall connection (%m)",
                    __func__);
            junos_dfw_session_handle_free(dfw_handle);
            dfw_handle = NULL;
            return EFAIL;
        }
    }

    return SUCCESS;
}


/**
 * Close down and free all resources
 */
void
shutdown_dfw(void)
{
    ready = FALSE;
    
    if(dfw_handle) {
        junos_dfw_session_close(dfw_handle);
        junos_dfw_session_handle_free(dfw_handle);
        dfw_handle = NULL;
    }
}


/**
 * Is the module ready to start using (sending requests)
 * 
 * @return TRUE or FALSE
 */
boolean
dfw_ready(void)
{
    return ready;
}


/**
 * Purge filters and reset mode
 * 
 * @param[in] new_filter_mode
 *      Use classic filters
 */
void
reset_all_filters(boolean new_filter_mode)
{
    if(ready) {
        LOG(LOG_ERR, "%s: Purging all existing filters", __func__);
        
        use_classic_filters = new_filter_mode;
        
        if(junos_dfw_trans_purge(dfw_handle, &dpm_cid, 1)) {
            LOG(LOG_ERR, "%s: Failed to perform DFWD purge", __func__);
        }
    }
}


/**
 * Create a policer using the DFWD
 * 
 * @param[in] policer
 *      The policer's information 
 */
void
create_policer(policer_info_t * policer)
{
    junos_dfw_policer_info_t dpi;
    junos_dfw_trans_handle_t trans_hdl;
    junos_dfw_burst_size_unit_t burst_size_unit;
    junos_dfw_rate_unit_t rate_unit;

    bzero(&dpi, sizeof(dpi));
    strlcpy(dpi.namestr_key, policer->name, sizeof(dpi.namestr_key));
    dpi.owner_client_id = dpm_cid;
    dpi.filter_specific = FALSE;

    if(junos_dfw_policer_trans_alloc(&dpi, 
            JUNOS_DFW_POLICER_OP_ADD, &trans_hdl)) {
        LOG(LOG_ERR, "%s: junos_dfw_policer_trans_alloc failed", __func__);
        return;
    }
    
    // we have to squeeze this uint64_t burst_size_limit into a uint32_t
    burst_size_unit = JUNOS_DFW_BURST_SIZE_UNIT_BYTE;
    while(policer->if_exceeding.burst_size_limit > UINT_MAX) {
        ++burst_size_unit;
        policer->if_exceeding.burst_size_limit /= KB_BYTES;
    }
    if(burst_size_unit < JUNOS_DFW_BURST_SIZE_UNIT_GBYTE) {
        LOG(LOG_ERR, "%s: burst_size_limit of policer %s is too big",
                __func__, policer->name);
        goto failed;
    }
    
    // configure policier depending on limit in percent or BPS
    
    if(policer->if_exceeding.bw_in_percent) {
        
        if (junos_dfw_policer_parameters(trans_hdl,
                policer->if_exceeding.bw_u.bandwidth_percent,
                JUNOS_DFW_RATE_UNIT_BANDWIDTH_PERCENT,
                (uint32_t)policer->if_exceeding.burst_size_limit,
                burst_size_unit)) {
            LOG(LOG_ERR,  "%s: junos_dfw_policer_parameters failed %m",
                    __func__);
            goto failed;
        }
    } else {
        
        // we have to squeeze this uint64_t bandwidth_limit into a uint32_t
        rate_unit = JUNOS_DFW_RATE_UNIT_BPS;
        while(policer->if_exceeding.burst_size_limit > UINT_MAX) {
            ++rate_unit;
            policer->if_exceeding.bw_u.bandwidth_limit /= KB_BYTES;
        }
        if(rate_unit < JUNOS_DFW_RATE_UNIT_GBPS) {
            LOG(LOG_ERR, "%s: bandwidth limit of policer %s is too high",
                    __func__, policer->name);
            goto failed;
        }
        
        if (junos_dfw_policer_parameters(trans_hdl,
                (uint32_t)policer->if_exceeding.bw_u.bandwidth_limit,
                rate_unit,
                (uint32_t)policer->if_exceeding.burst_size_limit,
                burst_size_unit)) {
            LOG(LOG_ERR, "%s: junos_dfw_policer_parameters failed %m",
                    __func__);
            goto failed;
        }
    }

    // Configure policier actions
    
    if(policer->action.discard) {
        if(junos_dfw_policer_action_discard(trans_hdl)) {
            LOG(LOG_ERR, "%s: junos_dfw_policer_action_discard failed",
                    __func__);
            goto failed;
        }        
    }

    if ((junos_dfw_trans_send(dfw_handle, trans_hdl, dpm_cid, 0))) {
        LOG(LOG_ERR, "%s: junos_dfw_trans_send failed", __func__);
        goto failed;
    }

failed: // or done
    junos_dfw_trans_handle_free(trans_hdl);
}


/**
 * Add an ingress and egress filter to the interface using the given policiers 
 * as actions
 * 
 * @param[in] int_name
 *      The interface (IFL) name
 * 
 * @param[in] ingress_pol
 *      The policier to use in the ingress filter
 * 
 * @param[in] egress_pol
 *      The policier to use in the egress filter 
 */
void
apply_default_int_policy(const char * int_name,
                         const char * ingress_pol,
                         const char * egress_pol)
{
    junos_dfw_policer_info_t ingress_dpi, egress_dpi;
    junos_dfw_filter_info_t i_filter_info, e_filter_info;
    junos_dfw_term_info_t i_dti, e_dti;
    junos_dfw_filter_attach_info_t dfai;
    junos_dfw_trans_handle_t i_trans_hdl, e_trans_hdl;
    const char * tname = "SYNC-DPM_DEFAULT_TERM";
    char * tmp;
    
    // Create policer info
    
    bzero(&ingress_dpi, sizeof(ingress_dpi));
    strlcpy(ingress_dpi.namestr_key, ingress_pol, 
            sizeof(ingress_dpi.namestr_key));
    ingress_dpi.owner_client_id = dpm_cid;
    ingress_dpi.filter_specific = FALSE;
    
    bzero(&egress_dpi, sizeof(egress_dpi));
    strlcpy(egress_dpi.namestr_key, egress_pol, sizeof(egress_dpi.namestr_key));
    egress_dpi.owner_client_id = dpm_cid;
    egress_dpi.filter_specific = FALSE;

    // Create filter infos
    
    bzero(&i_filter_info, sizeof(i_filter_info));
    snprintf(i_filter_info.namestr_key, sizeof(i_filter_info.namestr_key),
            "SYNC-DPM_%s_INGRESS", int_name);
    i_filter_info.owner_client_id = dpm_cid;
    i_filter_info.addr_family = JUNOS_DFW_FILTER_AF_INET;

    // Check what filter to use Classic/FUF and program accordingly
    if (use_classic_filters) {
        i_filter_info.type = JUNOS_DFW_FILTER_TYPE_CLASSIC;
    } else {
        i_filter_info.type = JUNOS_DFW_FILTER_TYPE_FAST_UPDATE;
    }
    
    bzero(&e_filter_info, sizeof(e_filter_info));
    snprintf(e_filter_info.namestr_key, sizeof(e_filter_info.namestr_key), 
            "SYNC-DPM_%s_EGRESS", int_name);
    e_filter_info.owner_client_id = dpm_cid;
    e_filter_info.addr_family = JUNOS_DFW_FILTER_AF_INET;
    
    // Check what filter to use Classic/FUF and program accordingly
    if (use_classic_filters) {
        e_filter_info.type = JUNOS_DFW_FILTER_TYPE_CLASSIC;
    } else {
        e_filter_info.type = JUNOS_DFW_FILTER_TYPE_FAST_UPDATE;
    }

    // Create the filter add transaction 
    
    if(junos_dfw_filter_trans_alloc(&i_filter_info,
            JUNOS_DFW_FILTER_OP_ADD, &i_trans_hdl)) {
        LOG(LOG_ERR, "%s: junos_dfw_filter_trans_alloc failed (i)\n",
                __func__);
        return;
    }
    if(junos_dfw_filter_trans_alloc(&e_filter_info,
            JUNOS_DFW_FILTER_OP_ADD, &e_trans_hdl)) {
        LOG(LOG_ERR, "%s: junos_dfw_filter_trans_alloc failed (e)\n",
                __func__);
        junos_dfw_trans_handle_free(i_trans_hdl);
        return;
    }

    // For FUF we have to set the order of the fields
    if (!use_classic_filters) {
        junos_dfw_filter_field_type_t  field_list[5];    

        field_list[0] = JUNOS_DFW_FILTER_FIELD_IP_PROTO;
        field_list[1] = JUNOS_DFW_FILTER_FIELD_IP_SRC_ADDR;
        field_list[2] = JUNOS_DFW_FILTER_FIELD_IP_DEST_ADDR;
        field_list[3] = JUNOS_DFW_FILTER_FIELD_SRC_PORT;
        field_list[4] = JUNOS_DFW_FILTER_FIELD_DEST_PORT;

        if (junos_dfw_filter_prop_ordered_field_list(i_trans_hdl, 5, field_list) < 0) {
            LOG(LOG_ERR, "%s: junos_dfw_filter_prop_ordered_field_list failed (i) : %s\n",
                       __func__, strerror(errno));
            return;
        }

        if (junos_dfw_filter_prop_ordered_field_list(e_trans_hdl, 5, field_list) < 0) {
            LOG(LOG_ERR, "%s: junos_dfw_filter_prop_ordered_field_list failed (i) : %s\n",
                       __func__, strerror(errno));
            return;
        }
    }
 
    // Add a term to each filter
    
    bzero(&i_dti, sizeof(i_dti));
    strlcpy(i_dti.namestr_key, tname, sizeof(i_dti.namestr_key));
    if (use_classic_filters) {
        i_dti.type = JUNOS_DFW_TERM_TYPE_ORDERED;
        i_dti.property.order.term_adj_type = JUNOS_DFW_TERM_ADJ_PREV;
    } else {
        i_dti.type = JUNOS_DFW_TERM_TYPE_PRIORITISED;
        i_dti.property.priority = 0;
    }

    bzero(&e_dti, sizeof(e_dti));
    strlcpy(e_dti.namestr_key, tname, sizeof(e_dti.namestr_key));
    if (use_classic_filters) {
        e_dti.type = JUNOS_DFW_TERM_TYPE_ORDERED;
        e_dti.property.order.term_adj_type = JUNOS_DFW_TERM_ADJ_PREV;
    } else {
        e_dti.type = JUNOS_DFW_TERM_TYPE_PRIORITISED;
        e_dti.property.priority = 0;
    }
    
    if(junos_dfw_term_start(i_trans_hdl, &i_dti, JUNOS_DFW_TERM_OP_ADD)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_start failed (i)", __func__);
        goto failed;
    }
    if(junos_dfw_term_start(e_trans_hdl, &e_dti, JUNOS_DFW_TERM_OP_ADD)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_start failed (e)", __func__);
        goto failed;
    }
    
    // Specify term action as policer
    
    if(junos_dfw_term_action_policer(i_trans_hdl, &ingress_dpi)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_action_policer failed (i)",
                __func__);
        goto failed;
    }
    if(junos_dfw_term_action_policer(e_trans_hdl, &egress_dpi)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_action_policer failed (e)",
                __func__);
        goto failed;
    }
    
    if(junos_dfw_term_end(i_trans_hdl) < 0) {
        LOG(LOG_ERR, "%s: junos_dfw_term_end failed (i)", __func__);
        goto failed;
    }
    if(junos_dfw_term_end(e_trans_hdl) < 0) {
        LOG(LOG_ERR, "%s: junos_dfw_term_end failed (e)", __func__);
        goto failed;
    }
    
    // Send transaction to finish the filter add
    
    if(junos_dfw_trans_send(dfw_handle, i_trans_hdl, dpm_cid, 0)) {
        LOG(LOG_ERR, "%s: junos_dfw_trans_send failed (i)", __func__);
        goto failed;
    }
    if(junos_dfw_trans_send(dfw_handle, e_trans_hdl, dpm_cid, 0)) {
        LOG(LOG_ERR, "%s: junos_dfw_trans_send failed (e)", __func__);
        goto failed;
    }
    
    junos_dfw_trans_handle_free(i_trans_hdl);
    junos_dfw_trans_handle_free(e_trans_hdl);
    
    // Apply the filters to the interface
    
    dfai.attach_point = JUNOS_DFW_FILTER_ATTACH_POINT_INPUT_INTF;
    strlcpy(dfai.type.intf.ifd_name, int_name, sizeof(dfai.type.intf.ifd_name));
    tmp = dfai.type.intf.ifd_name;
    strsep(&tmp, "."); // replace '.' with a '\0'
    dfai.type.intf.sub_unit = strtol(strstr(int_name, ".")+1, (char **)NULL,10);
    
    // Ingress side
    
    if(junos_dfw_filter_attach_trans_alloc(&i_filter_info, &dfai,&i_trans_hdl)){
        LOG(LOG_ERR, "%s: junos_dfw_filter_attach_trans_alloc failed (i)",
                __func__);
        goto failed;
    }
    if(junos_dfw_trans_send(dfw_handle, i_trans_hdl, dpm_cid, 0)) {
        LOG(LOG_ERR, "%s: junos_dfw_trans_send (attach) failed (i)",
                __func__);
        goto failed;
    }
    
    // Egress side
    
    dfai.attach_point = JUNOS_DFW_FILTER_ATTACH_POINT_OUTPUT_INTF;

    if(junos_dfw_filter_attach_trans_alloc(&e_filter_info, &dfai,&e_trans_hdl)){
        LOG(LOG_ERR, "%s: junos_dfw_filter_attach_trans_alloc failed (e)",
                __func__);
        goto failed;
    }
    if(junos_dfw_trans_send(dfw_handle, e_trans_hdl, dpm_cid, 0)) {
        LOG(LOG_ERR, "%s: junos_dfw_trans_send (attach) failed (e)",
                __func__);
        goto failed;
    }
    
failed: // or done
    junos_dfw_trans_handle_free(i_trans_hdl);
    junos_dfw_trans_handle_free(e_trans_hdl);
}


/**
 * Add a term to the ingress and egress filter where the subscriber's 
 * traffic gets routed through in order to police their traffic with their 
 * specific policer
 * 
 * @param[in] int_name
 *      The interface the subscriber's traffic gets routed through
 * 
 * @param[in] sub_name
 *      The subscriber name
 * 
 * @param[in] address
 *      The subscriber's address
 * 
 * @param[in] pol_name
 *      The policier to apply on subscriber traffic 
 * 
 * @return 0 upon success, -1 on failure (with error logged)
 */
int
apply_subscriber_policer(const char * int_name,
                         const char * sub_name,
                         in_addr_t address,
                         const char * pol_name)
{
    junos_dfw_policer_info_t dpi;
    junos_dfw_filter_info_t i_filter_info, e_filter_info;
    junos_dfw_term_info_t i_dti, e_dti;
    junos_dfw_trans_handle_t i_trans_hdl, e_trans_hdl;
    int rc = -1;
    
    // Create policer info
    
    bzero(&dpi, sizeof(dpi));
    strlcpy(dpi.namestr_key, pol_name, sizeof(dpi.namestr_key));
    dpi.owner_client_id = dpm_cid;
    dpi.filter_specific = FALSE;
    
    // Create filter infos
    
    bzero(&i_filter_info, sizeof(i_filter_info));
    snprintf(i_filter_info.namestr_key, sizeof(i_filter_info.namestr_key), 
            "SYNC-DPM_%s_INGRESS", int_name);
    i_filter_info.owner_client_id = dpm_cid;
    i_filter_info.addr_family = JUNOS_DFW_FILTER_AF_INET;
    
    // Check what filter to use Classic/FUF and program accordingly
    if (use_classic_filters) {
        i_filter_info.type = JUNOS_DFW_FILTER_TYPE_CLASSIC;
    } else {
        i_filter_info.type = JUNOS_DFW_FILTER_TYPE_FAST_UPDATE;
    }
    
    bzero(&e_filter_info, sizeof(e_filter_info));
    snprintf(e_filter_info.namestr_key, sizeof(e_filter_info.namestr_key),
            "SYNC-DPM_%s_EGRESS", int_name);
    e_filter_info.owner_client_id = dpm_cid;
    e_filter_info.addr_family = JUNOS_DFW_FILTER_AF_INET;
    
    // Check what filter to use Classic/FUF and program accordingly
    if (use_classic_filters) {
        e_filter_info.type = JUNOS_DFW_FILTER_TYPE_CLASSIC;
    } else {
        e_filter_info.type = JUNOS_DFW_FILTER_TYPE_FAST_UPDATE;
    }

    // Create the filter change transaction
    
    if(junos_dfw_filter_trans_alloc(&i_filter_info,
            JUNOS_DFW_FILTER_OP_CHANGE, &i_trans_hdl)) {
        LOG(LOG_ERR, "%s: junos_dfw_filter_trans_alloc failed (i)\n",
                __func__);
        return -1;
    }
    if(junos_dfw_filter_trans_alloc(&e_filter_info,
            JUNOS_DFW_FILTER_OP_CHANGE, &e_trans_hdl)) {
        LOG(LOG_ERR, "%s: junos_dfw_filter_trans_alloc failed (e)\n",
                __func__);
        junos_dfw_trans_handle_free(i_trans_hdl);
        return -1;
    }

    // For FUF we don't have to set the order of the fields if it is
    // a filter change transaction

    // Add a term to each filter
    
    bzero(&i_dti, sizeof(i_dti));
    strlcpy(i_dti.namestr_key, sub_name, sizeof(i_dti.namestr_key));
    if (use_classic_filters) {
        i_dti.type = JUNOS_DFW_TERM_TYPE_ORDERED;
        i_dti.property.order.term_adj_type = JUNOS_DFW_TERM_ADJ_PREV;
    } else {
        i_dti.type = JUNOS_DFW_TERM_TYPE_PRIORITISED;
        i_dti.property.priority = 0;
    }
    // if we specify prev and dti.property.order.term_adj_namestr_key is empty
    // like this, then this term that we are adding becomes first in the filter

    bzero(&e_dti, sizeof(e_dti));
    strlcpy(e_dti.namestr_key, sub_name, sizeof(e_dti.namestr_key));
    if (use_classic_filters) {
        e_dti.type = JUNOS_DFW_TERM_TYPE_ORDERED;
        e_dti.property.order.term_adj_type = JUNOS_DFW_TERM_ADJ_PREV;
    } else {
        e_dti.type = JUNOS_DFW_TERM_TYPE_PRIORITISED;
        e_dti.property.priority = 0;
    }
    // if we specify prev and dti.property.order.term_adj_namestr_key is empty
    // like this, then this term that we are adding becomes first in the filter
    
    if(junos_dfw_term_start(i_trans_hdl, &i_dti, JUNOS_DFW_TERM_OP_ADD)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_start failed (i)", __func__);
        goto failed;
    }
    if(junos_dfw_term_start(e_trans_hdl, &e_dti, JUNOS_DFW_TERM_OP_ADD)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_start failed (e)", __func__);
        goto failed;
    }
    
    // Match this subscriber's traffic (src for ingress/dest for egress)
    
    if(junos_dfw_term_match_src_prefix(i_trans_hdl, 
            &address, FULL_PREFIX_LEN, JUNOS_DFW_FILTER_OP_MATCH)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_match_src_prefix failed (i)",
                __func__);
        goto failed;
    }
    if(junos_dfw_term_match_dest_prefix(e_trans_hdl, 
            &address, FULL_PREFIX_LEN, JUNOS_DFW_FILTER_OP_MATCH)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_match_src_prefix failed (i)",
                __func__);
        goto failed;
    }    
    
    // Specify term action as the policer for this subscriber
    
    if(junos_dfw_term_action_policer(i_trans_hdl, &dpi)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_action_policer failed (i)",
                __func__);
        goto failed;
    }
    if(junos_dfw_term_action_policer(e_trans_hdl, &dpi)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_action_policer failed (e)",
                __func__);
        goto failed;
    }
    
    if(junos_dfw_term_end(i_trans_hdl) < 0) {
        LOG(LOG_ERR, "%s: junos_dfw_term_end failed (i)", __func__);
        goto failed;
    }
    if(junos_dfw_term_end(e_trans_hdl) < 0) {
        LOG(LOG_ERR, "%s: junos_dfw_term_end failed (e)", __func__);
        goto failed;
    }
    
    // Send transaction to finish the filter add
    
    if(junos_dfw_trans_send(dfw_handle, i_trans_hdl, dpm_cid, 0)) {
        LOG(LOG_ERR, "%s: junos_dfw_trans_send failed (i)", __func__);
        goto failed;
    }
    if(junos_dfw_trans_send(dfw_handle, e_trans_hdl, dpm_cid, 0)) {
        LOG(LOG_ERR, "%s: junos_dfw_trans_send failed (e)", __func__);
        goto failed;
    }

    rc = 0;
    
failed:
    junos_dfw_trans_handle_free(i_trans_hdl);
    junos_dfw_trans_handle_free(e_trans_hdl);
    
    return rc;
}


/**
 * Delete terms in the ingress and egress filter where the subscriber's 
 * traffic gets routed through in order to police their traffic with their 
 * specific policer  
 * 
 * @param[in] int_name
 *      The interface the subscriber's traffic gets routed through
 * 
 * @param[in] sub_name
 *      The subscriber name
 * 
 * @return 0 upon success, -1 on failure (with error logged)
 */
int
revoke_subscriber_policer(const char * int_name,
                          const char * sub_name)
{
    junos_dfw_filter_info_t i_filter_info, e_filter_info;
    junos_dfw_term_info_t i_dti, e_dti;
    junos_dfw_trans_handle_t i_trans_hdl, e_trans_hdl;
    int rc = -1;
    
    // Create filter infos
    
    bzero(&i_filter_info, sizeof(i_filter_info));
    snprintf(i_filter_info.namestr_key, sizeof(i_filter_info.namestr_key), 
            "SYNC-DPM_%s_INGRESS", int_name);
    i_filter_info.owner_client_id = dpm_cid;
    i_filter_info.addr_family = JUNOS_DFW_FILTER_AF_INET;
    
    // Check what filter to use Classic/FUF and program accordingly
    if (use_classic_filters) {
        i_filter_info.type = JUNOS_DFW_FILTER_TYPE_CLASSIC;
    } else {
        i_filter_info.type = JUNOS_DFW_FILTER_TYPE_FAST_UPDATE;
    }

    bzero(&e_filter_info, sizeof(e_filter_info));
    snprintf(e_filter_info.namestr_key, sizeof(e_filter_info.namestr_key),
            "SYNC-DPM_%s_EGRESS", int_name);
    e_filter_info.owner_client_id = dpm_cid;
    e_filter_info.addr_family = JUNOS_DFW_FILTER_AF_INET;
    
    // Check what filter to use Classic/FUF and program accordingly
    if (use_classic_filters) {
        e_filter_info.type = JUNOS_DFW_FILTER_TYPE_CLASSIC;
    } else {
        e_filter_info.type = JUNOS_DFW_FILTER_TYPE_FAST_UPDATE;
    }

    // Create the filter change transaction
    
    if(junos_dfw_filter_trans_alloc(&i_filter_info,
            JUNOS_DFW_FILTER_OP_CHANGE, &i_trans_hdl)) {
        LOG(LOG_ERR, "%s: junos_dfw_filter_trans_alloc failed (i)\n",
                __func__);
        return -1;
    }
    if(junos_dfw_filter_trans_alloc(&e_filter_info,
            JUNOS_DFW_FILTER_OP_CHANGE, &e_trans_hdl)) {
        LOG(LOG_ERR, "%s: junos_dfw_filter_trans_alloc failed (e)\n",
                __func__);
        junos_dfw_trans_handle_free(i_trans_hdl);
        return -1;
    }

    // Delete a term from each filter
    
    bzero(&i_dti, sizeof(i_dti));
    strlcpy(i_dti.namestr_key, sub_name, sizeof(i_dti.namestr_key));
    if (use_classic_filters) {
        i_dti.type = JUNOS_DFW_TERM_TYPE_ORDERED;
        i_dti.property.order.term_adj_type = JUNOS_DFW_TERM_ADJ_PREV;
    } else {
        i_dti.type = JUNOS_DFW_TERM_TYPE_PRIORITISED;
        i_dti.property.priority = 0;
    }


    bzero(&e_dti, sizeof(e_dti));
    strlcpy(e_dti.namestr_key, sub_name, sizeof(e_dti.namestr_key));
    if (use_classic_filters) {
        e_dti.type = JUNOS_DFW_TERM_TYPE_ORDERED;
        e_dti.property.order.term_adj_type = JUNOS_DFW_TERM_ADJ_PREV;
    } else {
        e_dti.type = JUNOS_DFW_TERM_TYPE_PRIORITISED;
        e_dti.property.priority = 0;
    }
    
    if(junos_dfw_term_start(i_trans_hdl, &i_dti, JUNOS_DFW_TERM_OP_DELETE)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_start failed (i)", __func__);
        goto failed;
    }
    if(junos_dfw_term_start(e_trans_hdl, &e_dti, JUNOS_DFW_TERM_OP_DELETE)) {
        LOG(LOG_ERR, "%s: junos_dfw_term_start failed (e)", __func__);
        goto failed;
    }
    
    if(junos_dfw_term_end(i_trans_hdl) < 0) {
        LOG(LOG_ERR, "%s: junos_dfw_term_end failed (i)", __func__);
        goto failed;
    }
    if(junos_dfw_term_end(e_trans_hdl) < 0) {
        LOG(LOG_ERR, "%s: junos_dfw_term_end failed (e)", __func__);
        goto failed;
    }
    
    // Send transaction to finish the filter change
    
    if(junos_dfw_trans_send(dfw_handle, i_trans_hdl, dpm_cid, 0)) {
        LOG(LOG_ERR, "%s: junos_dfw_trans_send failed (i)", __func__);
        goto failed;
    }
    if(junos_dfw_trans_send(dfw_handle, e_trans_hdl, dpm_cid, 0)) {
        LOG(LOG_ERR, "%s: junos_dfw_trans_send failed (e)", __func__);
        goto failed;
    }

    rc = 0;
    
failed:
    junos_dfw_trans_handle_free(i_trans_hdl);
    junos_dfw_trans_handle_free(e_trans_hdl);
    
    return rc;
}


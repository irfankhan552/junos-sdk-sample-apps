/*
 * $Id: jnx-gateway-mgmt_config.c 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-mgmt_config.c - config read routines.
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/* 
 * @file jnx-gateway-mgmt_config.c
 *    These routines read the configuration file for the 
 *    sample gateway application confugration items. 
 * There are three main configuration items,
 * 1. user profile policies
 * 2. control plane handling data pic address
 *    allocation, for tunnel self endpoints
 * 3. static gre tunnel confiugration
 * These are maintained as patricia trees in the
 * main global management control block
 */

#include <stdlib.h>
#include <sys/param.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <netinet/in.h>

#include <ddl/dax.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include <jnx/pconn.h>
#include <jnx/junos_kcom.h>
#include JNX_GATEWAY_MGMT_OUT_H
#include JNX_GATEWAY_MGMT_SEQUENCE_H
#include <jnx/junos_trace.h>
#include <jnx/vrf_util_pub.h>
#include <jnx/parse_ip.h>

#include <jnx/jnx-gateway.h>
#include <jnx/jnx-gateway_msg.h>

#include "jnx-gateway-mgmt.h"
#include "jnx-gateway-mgmt_config.h"

const char *jnx_gw_config_path[] = {
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_OBJ,
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_OBJ,
        DDLNAME_JNX_GATEWAY, NULL};

static uint32_t jnx_gw_mgmt_read_data_config(int check);
static void jnx_gw_mgmt_clear_data_config(void);
jnx_gw_mgmt_data_t * jnx_gw_mgmt_get_next_data(jnx_gw_mgmt_data_t * pdata);
jnx_gw_mgmt_data_t * jnx_gw_mgmt_get_first_data(void);
static uint32_t jnx_gw_mgmt_read_ctrl_config(int check);
static void jnx_gw_mgmt_clear_ctrl_config(void);
jnx_gw_mgmt_ctrl_t * jnx_gw_mgmt_get_next_ctrl(jnx_gw_mgmt_ctrl_t * pctrl);
jnx_gw_mgmt_ctrl_t * jnx_gw_mgmt_get_first_ctrl(void);
jnx_gw_mgmt_ctrl_t * jnx_gw_mgmt_lookup_ctrl(const char * vrf_name);
uint32_t jnx_gw_mgmt_read_user_config(int check);
static void jnx_gw_mgmt_clear_user_config(void);
jnx_gw_mgmt_data_t *
jnx_gw_mgmt_lookup_data(const char * policy_name);
jnx_gw_mgmt_user_t *
jnx_gw_mgmt_get_first_user(void);
void jnx_gw_mgmt_fill_gre_msg(jnx_gw_mgmt_data_session_t * pdata_pic,
                              jnx_gw_mgmt_data_t * pdata, uint8_t add_flag);

uint16_t jnx_gw_mgmt_fill_ipip_msg(jnx_gw_mgmt_data_t * pdata,
                                   jnx_gw_msg_sub_header_t   * subhdr,
                                   uint8_t add_flag);

/* Macros for functions, for extracting entries from the patnode
    offset, for user policy entry, control policy entry, &
    data static gre tunnel configuration entry */
PATNODE_TO_STRUCT(jnx_gw_mgmt_user_entry, jnx_gw_mgmt_user_t, user_node)
PATNODE_TO_STRUCT(jnx_gw_mgmt_ctrl_entry, jnx_gw_mgmt_ctrl_t, ctrl_node)
PATNODE_TO_STRUCT(jnx_gw_mgmt_data_entry, jnx_gw_mgmt_data_t, data_node)

/****************************************************************************
 *                                                                          *
 *          USER CONFIGURATION MANAGEMENT ROUTINES                          *
 *                                                                          *
 ****************************************************************************/
/****************************************************************************
 *                                                                          *
 *          USER CONFIGURATION LOOKUP ROUTINES                              *
 *                                                                          *
 ****************************************************************************/
/**
 * 
 * This function looks in the user profile db
 *
 * @params[in] user_name   user profile name
 * @return
 *    puser  user entry
 *    NULL   if not present
 */
jnx_gw_mgmt_user_t *
jnx_gw_mgmt_lookup_user (const char * user_name)
{
    patnode * pnode = NULL;
    if (user_name == NULL) {
        return NULL;
    }

    if ((pnode = patricia_get(&jnx_gw_mgmt.user_prof_db,
                              strlen(user_name) + 1, user_name))) {
        return jnx_gw_mgmt_user_entry(pnode);
    }
    jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG, 
                    LOG_DEBUG, "User policy \"%s\" is absent", user_name);
    return NULL;
}

/**
 * 
 * This function returns the first entry in the user profile db
 *
 * @returns puser   user profile entry
 */
jnx_gw_mgmt_user_t *
jnx_gw_mgmt_get_first_user(void)
{
    patnode * pnode;
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.user_prof_db, NULL))) {
        return jnx_gw_mgmt_user_entry(pnode);
    }
    return NULL;
}

/**
 * 
 * This function returns the next entry in the user profile db
 *
 * @params[in] puser   user profile entry
 * @returns    puser   next user profile entry
 */
jnx_gw_mgmt_user_t *
jnx_gw_mgmt_get_next_user(jnx_gw_mgmt_user_t * puser)
{
    patnode * pnode;
    if (puser == NULL) {
        return jnx_gw_mgmt_get_first_user();
    }
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.user_prof_db,
                                    &puser->user_node))) {
        return jnx_gw_mgmt_user_entry(pnode);
    }
    return NULL;
}

/****************************************************************************
 *                                                                          *
 *          USER CONFIGURATION ADD/DELETE/STATS UPDATE ROUTINES             *
 *                                                                          *
 ****************************************************************************/

/**
 * 
 * This function updates the user stat
 *
 * @params[in] puser   user profile entry
 */
void
jnx_gw_mgmt_update_user_stat(jnx_gw_mgmt_user_t *puser,
                             jnx_gw_msg_ctrl_user_stat_t *pstat)
{
    puser->user_active_sesn_count += ntohl(pstat->user_active_sesn_count);
    puser->user_sesn_count        += ntohl(pstat->user_sesn_count);
}
/**
 * 
 * This function adds an entry to the user profile db
 *
 * @params[in] ut   user profile configuration
 * @return
 *    puser   user profile entry
 *    NULL    addition failed
 */
static jnx_gw_mgmt_user_t *
jnx_gw_mgmt_add_user(jnx_gw_mgmt_user_t * ut)
{
    jnx_gw_mgmt_user_t * puser = NULL;
    int32_t vrf_id;

    if ((vrf_id = jnx_gw_get_vrf_id(ut->user_evrf_name)) == 
        JNX_GW_INVALID_VRFID) {

        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_ERR, "Could not resolve VRF \"%s\" to index",
                        ut->user_evrf_name);
        return (typeof(puser))vrf_id;
    }


    /* allocate the user entry */
    if ((puser = JNX_GW_MALLOC(JNX_GW_MGMT_ID,
                               sizeof(jnx_gw_mgmt_user_t))) == NULL) {
        jnx_gw_mgmt_log(JNX_GATEWAY_ALLOC, JNX_GATEWAY_TRACEFLAG_ERR, LOG_ERR,
                        "User policy \"%s\" %s failed",
                        ut->user_name, "malloc");
        return NULL;
    }

    /* set the values */
    strncpy(puser->user_name, ut->user_name, sizeof(puser->user_name));
    strncpy(puser->user_evrf_name, ut->user_evrf_name,
            sizeof(puser->user_evrf_name));
    strncpy(puser->user_ipip_gw_name, ut->user_ipip_gw_name,
              sizeof(puser->user_ipip_gw_name));

    puser->user_addr       = ntohl(ut->user_addr);
    puser->user_mask       = ntohl(ut->user_mask);
    puser->user_evrf_id    = vrf_id;
    puser->user_ipip_gw_ip = jnx_gw_get_ipaddr(puser->user_ipip_gw_name);

    /* init the key length */
    patricia_node_init_length(&puser->user_node,
                              strlen(puser->user_name) + 1);

    if (!patricia_add(&jnx_gw_mgmt.user_prof_db, &puser->user_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_EMERG, "User policy \"%s\" patricia %s failed",
                        puser->user_name, "add");
        JNX_GW_FREE(JNX_GW_MGMT_ID, puser);
        return NULL;
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                    LOG_INFO, "User policy \"%s\" vrf_id(%d) %s",
                    puser->user_name, vrf_id, "add");

    jnx_gw_mgmt.user_count++;
    return puser;
}

/**
 * 
 * This function frees the user profile entry
 *
 * @params[in] puser   user profile entry
 */
static void
jnx_gw_mgmt_free_user (jnx_gw_mgmt_user_t * puser)
{
    JNX_GW_FREE(JNX_GW_MGMT_ID, puser);
}

/**
 * 
 * This function deletes the user entry from the user profile db
 *
 * @params[in] puser   user profile entry
 */
static void
jnx_gw_mgmt_delete_user(jnx_gw_mgmt_user_t * puser)
{
    if (!patricia_delete(&jnx_gw_mgmt.user_prof_db, &puser->user_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG, "User policy \"%s\" patricia %s failed", 
                        puser->user_name, "delete");
    }
    jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                    LOG_INFO, "User policy \"%s\" %s",
                    puser->user_name, "delete");
    jnx_gw_mgmt_free_user(puser);
    jnx_gw_mgmt.user_count--;
}

/**
 * 
 * This function deletes all the user profile entries,
 * sends the user profile delete messages to the control
 * pic agent
 *
 */
static void
jnx_gw_mgmt_clear_user_config(void)
{
    jnx_gw_mgmt_user_t * puser = NULL;

    /* send the delete config message to all control plane agents */
    jnx_gw_mgmt_send_user_configs(NULL, JNX_GW_CONFIG_DELETE);

    /* clear the configuration */
    while ((puser = jnx_gw_mgmt_get_first_user()) != NULL) {
        jnx_gw_mgmt_delete_user(puser);
    }
}
/****************************************************************************
 *                                                                          *
 *          USER CONFIGURATION SEND ROUTINES                                *
 *                                                                          *
 ****************************************************************************/
/**
 * 
 * This function sends the user profile entries to control pic agent
 * for a specific vrf
 *
 * @params[in] pctrl_pic  control pic entry
 * @params[in] add_flag   add/delete/modify
 */
void
jnx_gw_mgmt_send_user_config(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                             uint32_t vrf_id, uint8_t add_flag)
{
    uint8_t msg_count = 0;
    uint16_t len = 0, sublen = 0;
    jnx_gw_mgmt_user_t * puser = NULL;
    jnx_gw_msg_header_t * hdr = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_msg_ctrl_config_user_t * puser_msg = NULL;

    /* user policies are not configured */
    if (jnx_gw_mgmt.user_count == 0) {
        return;
    }

    /* initialize the header */
    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    msg_count     = 0;
    len           = sizeof(*hdr);
    hdr->msg_type = JNX_GW_CONFIG_USER_MSG;

    /* set the sub header length */
    sublen = (sizeof(*subhdr) + sizeof(*puser_msg));

    /* send the user profiles */
    while ((puser = jnx_gw_mgmt_get_next_user(puser)) != NULL) {

        /* only for the intended vrf */
        if (puser->user_evrf_id != vrf_id) {
            continue;
        }

        /* the next message can overshoot the size
            of the buffer, send the packet out */
        if ((msg_count > JNX_GW_MAX_MSG_COUNT) ||
             ((len + sublen) > JNX_GW_MAX_PKT_BUF_SIZE)) {

            /* set the length & message count */
            hdr->count    = msg_count;
            hdr->msg_len  = htons(len);
            jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, 
                                      JNX_GW_CONFIG_USER_MSG, hdr, len);

            /* initialize the header again */
            hdr->msg_type = JNX_GW_CONFIG_USER_MSG;
            msg_count     = 0;
            len = sizeof(*hdr);
        }

        subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
        subhdr->sub_type = add_flag;
        subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
        subhdr->length   =  htons(sublen);

        puser_msg = (typeof(puser_msg))((uint8_t *)subhdr + sizeof(*subhdr));

        strncpy(puser_msg->user_name, puser->user_name, 
                sizeof(puser_msg->user_name));

        puser_msg->user_addr       = htonl(puser->user_addr);
        puser_msg->user_mask       = htonl(puser->user_mask);
        puser_msg->user_evrf_id    = htonl(puser->user_evrf_id);
        puser_msg->user_ipip_gw_ip = htonl(puser->user_ipip_gw_ip);

        len += sublen;
        msg_count++;
    }

    /* if there are sub header messages, send the message out */
    if (msg_count) {
        /* set the length & message count */
        hdr->count    = msg_count;
        hdr->msg_len  = htons(len);
        jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, JNX_GW_CONFIG_USER_MSG, hdr, len);
    }
}

/**
 * 
 * This function sends the user profile entries to control pic agent
 *
 * @params[in] pctrl_pic  control pic entry
 * @params[in] add_flag   add/delete/modify
 */
void
jnx_gw_mgmt_send_user_configs(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                             uint8_t add_flag)
{
    uint8_t msg_count = 0;
    uint16_t len = 0, sublen = 0;
    jnx_gw_mgmt_user_t * puser = NULL;
    jnx_gw_msg_header_t * hdr = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_msg_ctrl_config_user_t * puser_msg = NULL;

    /* user policies are not configured */
    if (jnx_gw_mgmt.user_count == 0) {
        return;
    }

    /* initialize the header */
    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    msg_count     = 0;
    len           = sizeof(*hdr);
    hdr->msg_type = JNX_GW_CONFIG_USER_MSG;

    /* set the sub header length */
    sublen = (sizeof(*subhdr) + sizeof(*puser_msg));

    /* send the user profiles */
    while ((puser = jnx_gw_mgmt_get_next_user(puser)) != NULL) {

        /* the next message can overshoot the size
            of the buffer, send the packet out */
        if ((msg_count > JNX_GW_MAX_MSG_COUNT) ||
             ((len + sublen) > JNX_GW_MAX_PKT_BUF_SIZE)) {

            /* set the length & message count */
            hdr->count    = msg_count;
            hdr->msg_len  = htons(len);
            jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, 
                                      JNX_GW_CONFIG_USER_MSG, hdr, len);

            /* initialize the header again */
            hdr->msg_type = JNX_GW_CONFIG_USER_MSG;
            msg_count     = 0;
            len = sizeof(*hdr);
        }

        subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
        subhdr->sub_type = add_flag;
        subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
        subhdr->length   =  htons(sublen);

        puser_msg = (typeof(puser_msg))((uint8_t *)subhdr + sizeof(*subhdr));

        strncpy(puser_msg->user_name, puser->user_name, 
                sizeof(puser_msg->user_name));

        puser_msg->user_addr       = htonl(puser->user_addr);
        puser_msg->user_mask       = htonl(puser->user_mask);
        puser_msg->user_evrf_id    = htonl(puser->user_evrf_id);
        puser_msg->user_ipip_gw_ip = htonl(puser->user_ipip_gw_ip);

        len += sublen;
        msg_count++;
    }

    /* if there are sub header messages, send the message out */
    if (msg_count) {
        /* set the length & message count */
        hdr->count    = msg_count;
        hdr->msg_len  = htons(len);
        jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, JNX_GW_CONFIG_USER_MSG, hdr, len);
    }
}

/****************************************************************************
 *                                                                          *
 *          USER CONFIGURATION READ ROUTINES                                *
 *                                                                          *
 ****************************************************************************/
/**
 * 
 * This function reads the user profile entries,
 * and calls the clear/add routine
 * Sends the configuration to the control agents
 *
 */
uint32_t
jnx_gw_mgmt_read_user_config(int check)
{
    ddl_handle_t *top = NULL, * dop = NULL;
    uint32_t status = FALSE;
    const char * jnx_gw_user_path[] = {
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_OBJ,
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_OBJ,
        DDLNAME_JNX_GATEWAY, DDLNAME_JNX_GATEWAY_USER, NULL};

    jnx_gw_mgmt_user_t ut;

    /* check user configuration */
    if (!dax_get_object_by_path(NULL, jnx_gw_user_path, &top, FALSE))  {
        jnx_gw_mgmt_clear_user_config();
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_INFO, "User policy configuration is absent");
        return status;
    }

    /* the user configuration has changed */
    if ((check != JNX_GW_CONFIG_READ_FORCE)  &&
        (dax_is_changed(top) == FALSE)) {
        if (top) dax_release_object(&top);
        return status;
    }

    /* clear existing user configuration */
    jnx_gw_mgmt_clear_user_config();

    /* read user config */
    while (dax_visit_container(top, &dop)) {

        ut.user_ipip_gw_name[0] = '\0';

        if ((status = dax_get_stringr_by_aid(dop, JNX_USER_NAME,
                                             ut.user_name,
                                             sizeof(ut.user_name)))
            == FALSE) {
            break;
        }

        if ((status = dax_get_ipv4prefix_by_aid(dop, JNX_USER_ADDR_RANGE,
                                                &ut.user_addr,
                                                &ut.user_mask)) == FALSE) {
            break;
        }

        if ((status = dax_get_stringr_by_aid(dop, JNX_USER_EGRESS_VRF,
                                             ut.user_evrf_name,
                                             sizeof(ut.user_evrf_name)))
            == FALSE) {
            break;
        }

        if ((status = dax_get_stringr_by_aid(dop, JNX_USER_IPIP_GW_ID,
                                             ut.user_ipip_gw_name,
                                             sizeof(ut.user_ipip_gw_name))) 
            == FALSE) {
            break;
        }

        /* add to the user database */
        if (!jnx_gw_mgmt_add_user(&ut)) {
            status = FALSE;
            break;
        }
    }

    /* release the acquired object, return */
    if (top) dax_release_object(&top);
    if (dop) dax_release_object(&dop);

    /* send the configuration to control agents */
    if ((status) && (jnx_gw_mgmt.user_count) && (check != TRUE))  {
        jnx_gw_mgmt_send_user_configs(NULL, JNX_GW_CONFIG_ADD);
    }

    return (!status);
}

/****************************************************************************
 *                                                                          *
 *          CONTROL CONFIGURATION MANAGEMENT ROUTINES                       *
 *                                                                          *
 ****************************************************************************/
/****************************************************************************
 *                                                                          *
 *          CONTROL CONFIGURATION LOOKUP ROUTINES                           *
 *                                                                          *
 ****************************************************************************/
/**
 * 
 * This function gets a control policy  attached
 * to a vrf
 *
 * @params[in]   vrf_name routing instance name
 * @returns
 *  pctrl  control policy, attached with the vrf
 *  NULL   if not present
 *
 */
jnx_gw_mgmt_ctrl_t *
jnx_gw_mgmt_lookup_ctrl(const char * vrf_name)
{
    patnode *pnode = NULL;
    if (vrf_name == NULL) {
        return NULL;
    }
    if ((pnode = patricia_get(&jnx_gw_mgmt.ctrl_pol_db,
                              strlen(vrf_name) + 1, vrf_name))) {
        return jnx_gw_mgmt_ctrl_entry(pnode);
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                    LOG_INFO, "Control policy \"%s\" %s failed",
                    vrf_name, "lookup");

    return NULL;
}

/**
 * 
 * This function returns the first control policy  entry
 * in the control policy db
 *
 * @erturns
 *  - pctrl  first entry in the db
 *  - NULL   if none is present
 */
jnx_gw_mgmt_ctrl_t *
jnx_gw_mgmt_get_first_ctrl(void)
{
    patnode * pnode;
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.ctrl_pol_db, NULL))) {
        return jnx_gw_mgmt_ctrl_entry(pnode);
    }
    return NULL;
}

/**
 * 
 * This function returns the next control policy  entry
 * in the control policy db
 *
 * @params[in]
 *  - pctrl  control policy entry in the db
 * @erturns
 *  - pctrl  next control policy entry in the db
 *  - NULL   if none is present
 */
jnx_gw_mgmt_ctrl_t *
jnx_gw_mgmt_get_next_ctrl(jnx_gw_mgmt_ctrl_t * pctrl)
{
    patnode * pnode = NULL;
    if (pctrl == NULL) {
        return jnx_gw_mgmt_get_first_ctrl();
    }
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.ctrl_pol_db,
                                    &pctrl->ctrl_node))) {
        return jnx_gw_mgmt_ctrl_entry(pnode);
    }
    return NULL;
}

/****************************************************************************
 *                                                                          *
 *          CONTROL CONFIGURATION ADD/DELETE ROUTINES                       *
 *                                                                          *
 ****************************************************************************/
/**
 * 
 * This function adds an entry to  the  the control policy db
 *
 * @params[in] ct control policy configuration
 * @returns    pctrl control policy entry
 *
 */
static jnx_gw_mgmt_ctrl_t *
jnx_gw_mgmt_add_ctrl (jnx_gw_mgmt_ctrl_t * ct)
{
    int32_t vrf_id;
    jnx_gw_mgmt_ctrl_t * pctrl = NULL;

    if ((vrf_id = jnx_gw_get_vrf_id(ct->ctrl_vrf_name)) == 
        JNX_GW_INVALID_VRFID) {
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_ERR, "Could not resolve VRF \"%s\" to index",
                        ct->ctrl_vrf_name);
        return (typeof(pctrl))vrf_id;
    }

    if ((pctrl = JNX_GW_MALLOC(JNX_GW_MGMT_ID,
                               sizeof(jnx_gw_mgmt_ctrl_t))) == NULL) {
        jnx_gw_mgmt_log(JNX_GATEWAY_ALLOC, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_ERR, "Control policy \"%s\" %s failed",
                        ct->ctrl_vrf_name, "malloc");
        return NULL;
    }

    /* set the values */
    strncpy(pctrl->ctrl_vrf_name, ct->ctrl_vrf_name,
              sizeof(pctrl->ctrl_vrf_name));
    pctrl->ctrl_addr   = ntohl(ct->ctrl_addr);
    pctrl->ctrl_mask   = ntohl(ct->ctrl_mask);
    pctrl->ctrl_vrf_id = vrf_id;

    /* add to the db */
    patricia_node_init_length(&pctrl->ctrl_node, 
                              strlen(pctrl->ctrl_vrf_name) + 1);

    if (!patricia_add(&jnx_gw_mgmt.ctrl_pol_db, &pctrl->ctrl_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG, "Control policy \"%s\" patricia %s failed",
                        pctrl->ctrl_vrf_name, "add");
        JNX_GW_FREE(JNX_GW_MGMT_ID, pctrl);
        return NULL;
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                    LOG_INFO, "Control policy \"%s\" %d %s",
                    pctrl->ctrl_vrf_name, vrf_id, "add");

    jnx_gw_mgmt.ctrl_count++;
    return pctrl;
}

/**
 * 
 * This function frees the control policy  entry
 *
 * @params[in]   pctrl control policy entry
 *
 */
static void
jnx_gw_mgmt_free_ctrl (jnx_gw_mgmt_ctrl_t * pctrl)
{
    JNX_GW_FREE(JNX_GW_MGMT_ID, pctrl);
}

/**
 * 
 * This function deletes the control policy  entry
 * from the control policy db
 *
 * @params[in] pctrl control policy entry
 *
 */
static void
jnx_gw_mgmt_delete_ctrl (jnx_gw_mgmt_ctrl_t * pctrl)
{
    if (!patricia_delete(&jnx_gw_mgmt.ctrl_pol_db, &pctrl->ctrl_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG, "Control policy \"%s\" patricia %s failed",
                        pctrl->ctrl_vrf_name, "delete");
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                    LOG_INFO, "Control policy \"%s\" %s",
                    pctrl->ctrl_vrf_name, "delete");

    jnx_gw_mgmt_free_ctrl(pctrl);
    jnx_gw_mgmt.ctrl_count--;
}

/**
 * 
 * This function clears the  the control policy db
 * Sends the delete message to the control agents
 *
 */
static void
jnx_gw_mgmt_clear_ctrl_config(void)
{
    jnx_gw_mgmt_ctrl_t * pctrl = NULL;

    /* send the delete config message to all control planes */
    jnx_gw_mgmt_send_ctrl_configs(NULL, JNX_GW_CONFIG_DELETE);

    while ((pctrl = jnx_gw_mgmt_get_first_ctrl()) != NULL) {
        jnx_gw_mgmt_delete_ctrl(pctrl);
    }
}

/****************************************************************************
 *                                                                          *
 *          CONTROL CONFIGURATION SEND ROUTINES                             *
 *                                                                          *
 ****************************************************************************/
/**
 * 
 * This function returns sends the control policy  entries
 * in the control policy db, to the control agents
 *
 * @params[in] pctrl_pic control pic agent
 * @params[in] add_flag  add/delete/modify flag
 */
void
jnx_gw_mgmt_send_ctrl_config(jnx_gw_mgmt_ctrl_session_t * pctrl_pic, 
                             uint32_t vrf_id, uint8_t add_flag)
{
    uint8_t msg_count = 0;
    uint16_t len = 0, sublen = 0;
    jnx_gw_msg_header_t * hdr = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_mgmt_ctrl_t * pctrl = NULL;
    jnx_gw_msg_ctrl_config_ctrl_t * pctrl_msg = NULL;

    /* control policies are not configured */
    if (jnx_gw_mgmt.ctrl_count == 0) {
        return;
    }

    /* initialize the header */
    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    msg_count     = 0;
    len           = sizeof(*hdr);
    hdr->msg_type = JNX_GW_CONFIG_CTRL_MSG;

    /* set the subheader length */
    sublen = sizeof(*subhdr) + sizeof(*pctrl_msg);

    /* send the control policies */
    while ((pctrl = jnx_gw_mgmt_get_next_ctrl(pctrl)) != NULL) {

        if (pctrl->ctrl_vrf_id != vrf_id) {
            continue;
        }
        /* the next message can overshoot the size
            of the buffer, send the packet out */
        if ((msg_count > JNX_GW_MAX_MSG_COUNT) ||
             ((len + sublen) > JNX_GW_MAX_PKT_BUF_SIZE)) {

            /* set the length & message count */
            hdr->count    = msg_count;
            hdr->msg_len  = htons(len);
            jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, JNX_GW_CONFIG_CTRL_MSG,
                                      hdr, len);

            /* initialize the header again */
            hdr->msg_type = JNX_GW_CONFIG_CTRL_MSG;
            msg_count     = 0;
            len = sizeof(*hdr);
        }


        subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
        subhdr->sub_type = add_flag;
        subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
        subhdr->length   =  htons(sublen);

        pctrl_msg  = (typeof(pctrl_msg))((uint8_t *)subhdr + sizeof(*subhdr));

        pctrl_msg->ctrl_vrf_id = htonl(pctrl->ctrl_vrf_id);
        pctrl_msg->ctrl_addr   = htonl(pctrl->ctrl_addr);
        pctrl_msg->ctrl_mask   = htonl(pctrl->ctrl_mask);

        len += sublen;
        msg_count ++;
    }

    /* if there are sub header messages
        send the message out */
    if (msg_count) {
        /* set the length & message count */
        hdr->count    = msg_count;
        hdr->msg_len  = htons(len);
        jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, JNX_GW_CONFIG_CTRL_MSG,
                                  hdr, len);
    }
}
/**
 * 
 * This function returns sends the control policy  entries
 * in the control policy db, to the control agents
 *
 * @params[in] pctrl_pic control pic agent
 * @params[in] add_flag  add/delete/modify flag
 */
void
jnx_gw_mgmt_send_ctrl_configs(jnx_gw_mgmt_ctrl_session_t * pctrl_pic, 
                             uint8_t add_flag)
{
    uint8_t msg_count = 0;
    uint16_t len = 0, sublen = 0;
    jnx_gw_msg_header_t * hdr = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_mgmt_ctrl_t * pctrl = NULL;
    jnx_gw_msg_ctrl_config_ctrl_t * pctrl_msg = NULL;

    /* control policies are not configured */
    if (jnx_gw_mgmt.ctrl_count == 0) return;

    /* initialize the header */
    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    msg_count     = 0;
    len           = sizeof(*hdr);
    hdr->msg_type = JNX_GW_CONFIG_CTRL_MSG;

    /* set the subheader length */
    sublen = sizeof(*subhdr) + sizeof(*pctrl_msg);

    /* send the control policies */
    while ((pctrl = jnx_gw_mgmt_get_next_ctrl(pctrl)) != NULL) {

        /* the next message can overshoot the size
            of the buffer, send the packet out */
        if ((msg_count > JNX_GW_MAX_MSG_COUNT) ||
             ((len + sublen) > JNX_GW_MAX_PKT_BUF_SIZE)) {

            /* set the length & message count */
            hdr->count    = msg_count;
            hdr->msg_len  = htons(len);
            jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, JNX_GW_CONFIG_CTRL_MSG,
                                      hdr, len);

            /* initialize the header again */
            hdr->msg_type = JNX_GW_CONFIG_CTRL_MSG;
            msg_count     = 0;
            len = sizeof(*hdr);
        }


        subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
        subhdr->sub_type = add_flag;
        subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
        subhdr->length   =  htons(sublen);

        pctrl_msg  = (typeof(pctrl_msg))((uint8_t *)subhdr + sizeof(*subhdr));

        pctrl_msg->ctrl_vrf_id = htonl(pctrl->ctrl_vrf_id);
        pctrl_msg->ctrl_addr   = htonl(pctrl->ctrl_addr);
        pctrl_msg->ctrl_mask   = htonl(pctrl->ctrl_mask);

        len += sublen;
        msg_count ++;
    }

    /* if there are sub header messages
        send the message out */
    if (msg_count) {
        /* set the length & message count */
        hdr->count    = msg_count;
        hdr->msg_len  = htons(len);
        jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, JNX_GW_CONFIG_CTRL_MSG,
                                  hdr, len);
    }
}

/****************************************************************************
 *                                                                          *
 *          CONTROL CONFIGURATION READ ROUTINES                             *
 *                                                                          *
 ****************************************************************************/
/**
 * 
 * This function reads the control configuration entries
 * from the config file
 * sends the configuration to all the control pics
 *
 * @returns  status
 * TRUE     successful read
 * FALSE    read fail
 *
 */

static uint32_t
jnx_gw_mgmt_read_ctrl_config(int check)
{
    uint32_t status = FALSE;
    ddl_handle_t *top = NULL, * dop = NULL;
    const char * jnx_gw_ctrl_path[] = { 
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_OBJ,
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_OBJ,
        DDLNAME_JNX_GATEWAY, DDLNAME_JNX_GATEWAY_CONTROL,
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_JNX_GATEWAY_CONTROL_POLICY_OBJ,
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_JNX_GATEWAY_CONTROL_POLICY_INSTANCE_POLICY_OBJ,
        NULL};
    jnx_gw_mgmt_ctrl_t ct;

    /* check control configuration */
    if (!dax_get_object_by_path(NULL, jnx_gw_ctrl_path, &top, FALSE)) {
        /* clear existing control configuration */
        jnx_gw_mgmt_clear_ctrl_config();
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_INFO, "Control policy configuration is absent");
        return status;
    }

    if ((check != JNX_GW_CONFIG_READ_FORCE) &&
        (dax_is_changed(top) == FALSE)) {
        if (top) dax_release_object(&top);
        return status;
    }

    /* clear existing control configuration */
    jnx_gw_mgmt_clear_ctrl_config();

    /* read control config */
    while (dax_visit_container(top, &dop)) {

        memset(&ct, 0, sizeof(ct));

        /* get the routing instance name */
        if ((status = dax_get_stringr_by_aid(dop, JNX_CTRL_VRF,
                                             ct.ctrl_vrf_name,
                                             sizeof(ct.ctrl_vrf_name)))
            == FALSE) {
            break;
        }

        /* get the data pic ip address range */
        if ((status = dax_get_ipv4prefix_by_aid(dop, JNX_CTRL_ADDR_RANGE,
                                  &ct.ctrl_addr, &ct.ctrl_mask)) == FALSE) {
            break;
        }

        /* add to the control policy database */
        if (!jnx_gw_mgmt_add_ctrl(&ct)) {
            status = FALSE;
            break;
        }
    }

    /* release the acquired object, return */
    if (top) dax_release_object(&top);
    if (dop) dax_release_object(&dop);

    /* send the new config to all the control agents */
    if ((status) && (jnx_gw_mgmt.ctrl_count) && (check != TRUE)) {
        jnx_gw_mgmt_send_ctrl_configs(NULL, JNX_GW_CONFIG_ADD);
    }

    return (!status);
}

    
/****************************************************************************
 *                                                                          *
 *          DATA CONFIGURATION MANAGEMENT ROUTINES                          *
 *                                                                          *
 ****************************************************************************/

/****************************************************************************
 *                                                                          *
 *          DATA CONFIGURATION LOOKUP ROUTINES                              *
 *                                                                          *
 ****************************************************************************/

/**
 * 
 * This function gets a data policy  
 *
 * @params[in]   policy name
 * @returns
 *  pdata  data policy 
 *  NULL   if not present
 *
 */
jnx_gw_mgmt_data_t *
jnx_gw_mgmt_lookup_data(const char * policy_name)
{
    patnode *pnode = NULL;
    if (policy_name == NULL) {
        return NULL;
    }
    if ((pnode = patricia_get(&jnx_gw_mgmt.data_pol_db,
                              strlen(policy_name) + 1, policy_name))) {
        return jnx_gw_mgmt_data_entry(pnode);
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                    LOG_INFO, "Static GRE policy \"%s\" %s failed",
                    policy_name, "lookup");
    return NULL;
}

/**
 * 
 * This function returns the first data policy entry 
 * from the profile db 
 *
 * @returns  pdata  the first entry in the db
 * @returns  NULL   if none
 *
 */
jnx_gw_mgmt_data_t *
jnx_gw_mgmt_get_first_data(void)
{
    patnode * pnode;
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.data_pol_db, NULL))) {
        return jnx_gw_mgmt_data_entry(pnode);
    }
    return NULL;
}

/**
 * 
 * This function returns the next data policy entry 
 * from the profile db 
 *
 * @params[in]  pdata  the data entry in the db
 *
 * @returns  pdata  the next entry in the db
 * @returns  NULL   if none
 *
 */
jnx_gw_mgmt_data_t *
jnx_gw_mgmt_get_next_data(jnx_gw_mgmt_data_t * pdata)
{
    patnode * pnode;
    if (pdata == NULL) {
        return jnx_gw_mgmt_get_first_data();
    }
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.data_pol_db,
                                    &pdata->data_node))) {
        return jnx_gw_mgmt_data_entry(pnode);
    }
    return NULL;
}

/****************************************************************************
 *                                                                          *
 *          DATA CONFIGURATION ADD/DELETE ROUTINES                          *
 *                                                                          *
 ****************************************************************************/
/**
 * 
 * This function adds a data policy entry into the profile db
 *
 * @params[in] dt   data configuration item
 *
 * @returns  pdata  data policy entry
 *           NULL   if creation of the data policy entry failed
 *
 */
static jnx_gw_mgmt_data_t *
jnx_gw_mgmt_add_data(jnx_gw_mgmt_data_t * dt)
{
    jnx_gw_mgmt_data_t * pdata = NULL;
    int32_t ivrf_id, evrf_id;

    if ((ivrf_id = jnx_gw_get_vrf_id(dt->data_ivrf_name)) ==
        JNX_GW_INVALID_VRFID)  {
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_ERR, "Could not resolve VRF \"%s\" to index",
                        dt->data_ivrf_name);
        return (typeof(pdata))ivrf_id;
    }

    if ((evrf_id = jnx_gw_get_vrf_id(dt->data_evrf_name)) ==
        JNX_GW_INVALID_VRFID) {
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_ERR, "Could not resolve VRF \"%s\" to index",
                        dt->data_evrf_name);
        return (typeof(pdata))evrf_id;
    }

    /* allocate the data entry */
    if ((pdata = JNX_GW_MALLOC(JNX_GW_MGMT_ID,
                               sizeof(jnx_gw_mgmt_data_t))) == NULL) {
        jnx_gw_mgmt_log(JNX_GATEWAY_ALLOC, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_ERR, "Static GRE policy \"%s\" %s failed",
                        dt->data_policy_name, "malloc");
        return NULL;
    }

    /* set the values */

    strncpy(pdata->data_policy_name, dt->data_policy_name,
              sizeof(pdata->data_policy_name));

    strncpy(pdata->data_pic_name, dt->data_pic_name,
              sizeof(pdata->data_pic_name));

    strncpy(pdata->data_clnt_name, dt->data_clnt_name,
              sizeof(pdata->data_clnt_name));

    strncpy(pdata->data_svr_name, dt->data_svr_name,
              sizeof(pdata->data_svr_name));

    strncpy(pdata->data_ivrf_name, dt->data_ivrf_name,
              sizeof(pdata->data_ivrf_name));

    strncpy(pdata->data_iself_name, dt->data_iself_name,
              sizeof(pdata->data_iself_name));

    strncpy(pdata->data_gre_gw_name , dt->data_gre_gw_name,
              sizeof(pdata->data_gre_gw_name));

    strncpy(pdata->data_evrf_name, dt->data_evrf_name,
              sizeof(pdata->data_evrf_name));

    if (dt->data_ipip_gw_name) {
        strncpy(pdata->data_ipip_gw_name, dt->data_ipip_gw_name,
                  sizeof(pdata->data_ipip_gw_name));
    }

    if (dt->data_eself_name) {
        strncpy(pdata->data_eself_name, dt->data_eself_name,
                  sizeof(pdata->data_eself_name));
    }

    /* client information */
    pdata->data_clnt_ip       = jnx_gw_get_ipaddr(dt->data_clnt_name);
    pdata->data_start_port    = (dt->data_start_port);
    pdata->data_port_range    = (dt->data_port_range);
    pdata->data_proto         = (dt->data_proto) ? dt->data_proto : IPPROTO_TCP;

    /* server information */
    pdata->data_svr_ip        = jnx_gw_get_ipaddr(dt->data_svr_name);
    pdata->data_svr_port      = (dt->data_svr_port);

    /* ingress vrf & gre information */
    pdata->data_ivrf_id       = ivrf_id;
    pdata->data_iself_ip      = jnx_gw_get_ipaddr(dt->data_iself_name);
    pdata->data_gre_gw_ip     = jnx_gw_get_ipaddr(dt->data_gre_gw_name);
    pdata->data_start_gre_key = (dt->data_start_gre_key);

    /* egress vrf & gateway information */
    pdata->data_evrf_id   = evrf_id;

    /* the ipip gateway id may not be set (it is optional) */
    if (dt->data_ipip_gw_name) {
        pdata->data_ipip_gw_ip = jnx_gw_get_ipaddr(dt->data_ipip_gw_name);
    }

    if (dt->data_eself_name) {
        pdata->data_eself_ip   = jnx_gw_get_ipaddr(dt->data_eself_name);
    }

    /* init the key length */
    patricia_node_init_length(&pdata->data_node,
                              strlen(pdata->data_policy_name) + 1);

    /* add to the data policy db */
    if (!patricia_add(&jnx_gw_mgmt.data_pol_db, &pdata->data_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG,
                        "Static GRE policy \"%s\" patricia %s failed",
                        pdata->data_policy_name, "add");
        JNX_GW_FREE(JNX_GW_MGMT_ID, pdata);
        return NULL;
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                    LOG_INFO, "Static GRE policy \"%s\" %s", 
                    pdata->data_policy_name, "add");

    jnx_gw_mgmt.data_count++;
    return pdata;
}

/**
 * 
 * This function frees the data policy entry
 *
 *
 */
static void
jnx_gw_mgmt_free_data(jnx_gw_mgmt_data_t * pdata)
{
    if (!patricia_delete(&jnx_gw_mgmt.data_pol_db, &pdata->data_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG,
                        "Static GRE policy \"%s\" patricia %s failed", 
                        pdata->data_policy_name, "delete");
    }
    jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG, 
                    LOG_INFO, "Static GRE policy \"%s\" %s", 
                    pdata->data_policy_name, "delete");
    JNX_GW_FREE(JNX_GW_MGMT_ID, pdata);
}

/**
 * 
 * This function deletes a data policy entry
 * from the profile db 
 *
 * @returns  status
 * TRUE     successful read
 * FALSE    read fail
 *
 */
static void
jnx_gw_mgmt_delete_data(jnx_gw_mgmt_data_t * pdata)
{
    jnx_gw_mgmt_free_data(pdata);
    jnx_gw_mgmt.data_count--;
}


/**
 * 
 * This function clears the data policies from the
 * profile db
 * sends gre tunnel delete message to the data pic agent
 *
 */
static void
jnx_gw_mgmt_clear_data_config(void)
{
    jnx_gw_mgmt_data_t * pdata = NULL;

    /* send the message to the data pics */
    jnx_gw_mgmt_send_data_config(NULL, JNX_GW_DEL_GRE_SESSION);

    /* delete the configuration */
    while ((pdata = jnx_gw_mgmt_get_first_data()) != NULL) {
        jnx_gw_mgmt_delete_data(pdata);
    }
}

/****************************************************************************
 *                                                                          *
 *          DATA CONFIGURATION SEND ROUTINES                                *
 *                                                                          *
 ****************************************************************************/

/**
 * 
 * This function sends the static gre tunnel configuration
 * entries into the data agent
 *
 * @params[in]  pdata      data policy pointer
 * @params[in]  subhdr     subhdr pointer
 * @params[in]  add_flag   add/delete
 *
 * @returns sublen   filled  subheader length
 */
uint16_t
jnx_gw_mgmt_fill_ipip_msg(jnx_gw_mgmt_data_t * pdata,
                          jnx_gw_msg_sub_header_t * subhdr, uint8_t add_flag)
{
    uint16_t sublen;
    jnx_gw_msg_ip_ip_info_t  * pipip_tunnel;
    jnx_gw_msg_tunnel_type_t * ptunnel_type;

    /* fill sub header */
    memset(subhdr, 0, sizeof(*subhdr));
    sublen           = sizeof(*subhdr);
    subhdr->sub_type = add_flag;
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;

    /* fill egress ipip tunnel type */
    ptunnel_type   = (typeof(ptunnel_type))((uint8_t *)subhdr + sublen);
    memset(ptunnel_type, 0, sizeof(*ptunnel_type));

    ptunnel_type->tunnel_type = JNX_GW_TUNNEL_TYPE_IPIP;
    ptunnel_type->flags       = 0;
    ptunnel_type->length      = htons(sizeof(*ptunnel_type) +
                                      sizeof(*pipip_tunnel));
    sublen                += sizeof(*ptunnel_type);

    /* fill egress ipip tunnel info */
    pipip_tunnel   = (typeof(pipip_tunnel))((uint8_t *)subhdr + sublen);
    memset(pipip_tunnel, 0, sizeof(*pipip_tunnel));

    pipip_tunnel->vrf        = htonl(pdata->data_evrf_id);
    pipip_tunnel->gateway_ip = htonl(pdata->data_ipip_gw_ip);
    pipip_tunnel->self_ip    = htonl(pdata->data_eself_ip);
    sublen               += sizeof(*pipip_tunnel);

    subhdr->length = htons(sublen);
    return sublen;
}

/**
 * 
 * This function sends the static gre tunnel configuration
 * entries into the data agent
 *
 * @params[in]  pdata_pic  data agent pointer
 * @params[in]  pdata      data policy pointer
 * @params[in]  add_flag   add/delete
 *
 * @returns sublen   filled  subheader length
 */
void 
jnx_gw_mgmt_fill_gre_msg( jnx_gw_mgmt_data_session_t * pdata_pic,
                         jnx_gw_mgmt_data_t * pdata, uint8_t add_flag)
{
    uint8_t msg_count;
    uint16_t sublen, len;
    uint16_t port_range, clnt_port;
    uint32_t session_id, gre_key;
    jnx_gw_msg_ip_t           * pip_tunnel;
    jnx_gw_msg_gre_info_t     * pgre_tunnel;
    jnx_gw_msg_ip_ip_info_t   * pipip_tunnel;
    jnx_gw_msg_tunnel_type_t  * ptunnel_type;
    jnx_gw_msg_session_info_t * psession;
    jnx_gw_msg_header_t       * hdr;
    jnx_gw_msg_sub_header_t   * subhdr;

    /* fill up the message header */
    session_id = 1;
    msg_count  = 0;
    len        = sizeof(*hdr);
    hdr        = (typeof(hdr))(jnx_gw_mgmt.send_buf);

    memset(hdr, 0,  sizeof(*hdr));
    hdr->msg_type = JNX_GW_GRE_SESSION_MSG;

    /* get the initial session values */
    gre_key    = pdata->data_start_gre_key;
    clnt_port  = pdata->data_start_port;
    port_range = pdata->data_port_range;

    for (;(port_range); gre_key++, clnt_port++, msg_count++, session_id++,
         port_range--, len += sublen) {

        /* if we are filling up the buffer, send message */
        if (((len + sizeof(jnx_gw_msg_gre_t)) > JNX_GW_MAX_PKT_BUF_SIZE) ||
            (msg_count > JNX_GW_MAX_MSG_COUNT)) {
            hdr->count   = msg_count;
            hdr->msg_len = htons(len);
            jnx_gw_mgmt_data_send_opcmd(pdata_pic, JNX_GW_GRE_SESSION_MSG,
                                        hdr, len);
            jnx_gw_mgmt_data_recv_opcmd(pdata_pic);
            hdr->msg_type = JNX_GW_GRE_SESSION_MSG;
            msg_count     = 0;
            len = sizeof(*hdr);
        }

        subhdr  = (typeof(subhdr))((uint8_t *)hdr + len);
        sublen  = sizeof(*subhdr);

        /* fill sub header */
        memset(subhdr, 0, sizeof(*subhdr));
        subhdr->sub_type = add_flag;
        subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;

        if (add_flag == JNX_GW_DEL_GRE_SESSION) {

            /* fill ingress gre tunnel info */
            pgre_tunnel  = (typeof(pgre_tunnel))((uint8_t *)subhdr + sublen);
            memset(pgre_tunnel, 0, sizeof(*pgre_tunnel));

            pgre_tunnel->vrf        = htonl(pdata->data_ivrf_id);
            pgre_tunnel->gateway_ip = htonl(pdata->data_gre_gw_ip);
            pgre_tunnel->self_ip    = htonl(pdata->data_iself_ip);
            pgre_tunnel->gre_key    = htonl(gre_key);
            pgre_tunnel->gre_seq    = 0;
            sublen              += sizeof(*pgre_tunnel);

            subhdr->length = htons(sublen);
            continue;
        }

        /* for gre add session messages */

        /* fill session info */
        psession  = (typeof(psession))((uint8_t *)subhdr + sublen);
        memset(psession, 0, sizeof(*psession));

        psession->session_id = htonl(session_id);
        psession->proto      = pdata->data_proto;
        psession->sip        = htonl(pdata->data_clnt_ip);
        psession->dip        = htonl(pdata->data_svr_ip);
        psession->dport      = htons(pdata->data_svr_port);
        psession->sport      = htons(clnt_port);
        sublen           += sizeof(*psession);

        /* ingress tunnel info */
        ptunnel_type  = (typeof(ptunnel_type))((uint8_t *)subhdr + sublen);

        /* fill ingress tunnel type */
        ptunnel_type->tunnel_type = JNX_GW_TUNNEL_TYPE_GRE;
        ptunnel_type->flags       = JNX_GW_GRE_KEY_PRESENT;
        ptunnel_type->length      = htons(sizeof(*ptunnel_type)
                                          + sizeof(*pgre_tunnel));
        sublen                   += sizeof(*ptunnel_type);

        /* fill ingress gre tunnel info */
        pgre_tunnel  = (typeof(pgre_tunnel))((uint8_t *)subhdr + sublen);

        pgre_tunnel->vrf        = htonl(pdata->data_ivrf_id);
        pgre_tunnel->gateway_ip = htonl(pdata->data_gre_gw_ip);
        pgre_tunnel->self_ip    = htonl(pdata->data_iself_ip);
        pgre_tunnel->gre_key    = htonl(gre_key);
        pgre_tunnel->gre_seq    = 0;
        sublen              += sizeof(*pgre_tunnel);

        /* egress tunnel info */
        ptunnel_type   = (typeof(ptunnel_type))((uint8_t *)subhdr + sublen);

        if (pdata->data_ipip_gw_ip) {

            /* fill egress ipip tunnel type */
            ptunnel_type->tunnel_type = JNX_GW_TUNNEL_TYPE_IPIP;
            ptunnel_type->flags   = 0;
            ptunnel_type->length  = htons(sizeof(*ptunnel_type)
                                          + sizeof(*pipip_tunnel));
            sublen               += sizeof(*ptunnel_type);

            /* fill egress ipip tunnel info */
            pipip_tunnel   = (typeof(pipip_tunnel))((uint8_t *)subhdr + sublen);

            pipip_tunnel->vrf        = htonl(pdata->data_evrf_id);
            pipip_tunnel->gateway_ip = htonl(pdata->data_ipip_gw_ip);
            pipip_tunnel->self_ip    = htonl(pdata->data_eself_ip);
            sublen                  += sizeof(*pipip_tunnel);

        } else {

            /* fill egress native tunnel type */
            ptunnel_type->tunnel_type = JNX_GW_TUNNEL_TYPE_IP;
            ptunnel_type->flags  = 0;
            ptunnel_type->length = htons(sizeof(*ptunnel_type)
                                         + sizeof(*pip_tunnel));
            sublen              += sizeof(*ptunnel_type);

            /* fill egress native tunnel info */
            pip_tunnel  = (typeof(pip_tunnel))((uint8_t *)subhdr + sublen);

            pip_tunnel->vrf  = htonl(pdata->data_evrf_id);
            sublen          += sizeof(*pip_tunnel);
        }

        subhdr->length = htons(sublen);
    }

    /* send the message */
    if (msg_count) {
        hdr->count   = msg_count;
        hdr->msg_len = htons(len);
        jnx_gw_mgmt_data_send_opcmd(pdata_pic, JNX_GW_GRE_SESSION_MSG,
                                    hdr, len);
        jnx_gw_mgmt_data_recv_opcmd(pdata_pic);
    }

    return;
}

/**
 * 
 * This function loops through the data policies
 * & invokes the ipip & gre session messages
 * sends to the data pic
 *
 * @params[in]  pdata_pic  the data pic entry 
 * @params[in]  add_flag   add/delete
 *
 */
void
jnx_gw_mgmt_send_data_config(jnx_gw_mgmt_data_session_t * pdata_pic,
                             uint8_t add_flag)
{
    uint8_t  msg_count, pic_flag = FALSE;
    uint16_t len;
    jnx_gw_mgmt_data_t      * pdata = NULL;
    jnx_gw_msg_header_t     * hdr;
    jnx_gw_msg_sub_header_t * subhdr;


    if (pdata_pic) pic_flag = TRUE;
    /* this is for ipip tunnel add/delete messages */
    /* fill up the message header */
    msg_count     = 0;
    len           = sizeof(*hdr);
    hdr           = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    hdr->msg_type = JNX_GW_IPIP_TUNNEL_MSG;

    /* this is for ipip tunnel add/delete messages */
    while ((pdata = jnx_gw_mgmt_get_next_data(pdata))) {

        if (!pdata->data_ipip_gw_ip) continue;

        if (pic_flag == TRUE) {
            if (strcmp(pdata->data_pic_name, pdata_pic->pic_name)) {
                continue;
            }
        } else if (!(pdata_pic =
                     jnx_gw_mgmt_data_sesn_lookup (pdata->data_pic_name))) {
            continue;
        }

        subhdr  = (typeof(subhdr))((uint8_t *)hdr + len);
        len    += jnx_gw_mgmt_fill_ipip_msg(pdata, subhdr, add_flag);
        msg_count++;

        /* send the message */
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_INFO,
                        "Static IP-IP tunnel \"%s\" on \"%s\" %s sent",
                        pdata->data_policy_name, pdata->data_pic_name,
                        (add_flag) ? "delete" : "add");

        hdr->count   = msg_count;
        hdr->msg_len = htons(len);
        jnx_gw_mgmt_data_send_opcmd(pdata_pic, JNX_GW_IPIP_TUNNEL_MSG,
                                    hdr, len);
        jnx_gw_mgmt_data_recv_opcmd(pdata_pic);
        hdr->msg_type = JNX_GW_IPIP_TUNNEL_MSG;
        msg_count     = 0;
        len = sizeof(*hdr);
    }

    /* now send the gre session messages */

    while ((pdata = jnx_gw_mgmt_get_next_data(pdata))) {

        if (pic_flag == TRUE) {
            if (strcmp(pdata_pic->pic_name, pdata->data_pic_name)) {
                continue;
            }
        } else if (!(pdata_pic =
                     jnx_gw_mgmt_data_sesn_lookup(pdata->data_pic_name))) {
            continue;
        }
        jnx_gw_mgmt_fill_gre_msg(pdata_pic, pdata, add_flag);
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_INFO, "Static GRE tunnel \"%s\" on \"%s\" %s sent",
                        pdata->data_policy_name, pdata->data_pic_name,
                        (add_flag) ? "delete" : "add");
    }
    return;
}

/****************************************************************************
 *                                                                          *
 *          DATA CONFIGURATION READ ROUTINES                                *
 *                                                                          *
 ****************************************************************************/
/**
 * 
 * This function reads the data policy configuration from
 * the configuration file
 * if the configuration items for the data policies are
 * changed, clears the db, & adds the new entries
 * into the db
 *
 */

static uint32_t
jnx_gw_mgmt_read_data_config(int check)
{
    uint32_t status = FALSE, idx;
    ddl_handle_t *top = NULL, * dop = NULL, * obj = NULL;
    const char * jnx_gw_data_path[] = { DDLNAME_DDC_JUNIPER_CONFIG_SDK_OBJ,
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_OBJ,
        DDLNAME_JNX_GATEWAY, DDLNAME_JNX_GATEWAY_DATA,
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_JNX_GATEWAY_DATA_POLICY_OBJ,
        NULL};
    const char * jnx_gw_data_policy_obj[] = {
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_JNX_GATEWAY_DATA_POLICY_SESSION_OBJ,
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_JNX_GATEWAY_DATA_POLICY_INGRESS_TUNNEL_OBJ,
        DDLNAME_DDC_JUNIPER_CONFIG_SDK_JNPR_JNX_GATEWAY_DATA_POLICY_EGRESS_TUNNEL_OBJ,
        NULL};
    jnx_gw_mgmt_data_t dt;

    memset(&dt, 0, sizeof(dt));

    /* check data configuration, if the path has been
       deleted just remove every thing*/
    if (!dax_get_object_by_path(NULL, jnx_gw_data_path, &top, FALSE)) {

        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_DEBUG, "Static GRE policy configuration is absent");
        /* clear any existing data configuration */
        jnx_gw_mgmt_clear_data_config();
        return status;
    }

    if ((check != JNX_GW_CONFIG_READ_FORCE) &&
        (dax_is_changed(top) == FALSE)) {
        /* release the acquired object, return */
        if (top) dax_release_object(&top);
        return status;
    }

    /* clear existing data configuration */
    jnx_gw_mgmt_clear_data_config();

    /* read data config */
    while (dax_visit_container(top, &dop)) {

        dt.data_pic_name[0]     = '\0';
        dt.data_eself_name[0]   = '\0';
        dt.data_ipip_gw_name[0] = '\0';

        /* get the data policy name */
        if ((status = dax_get_stringr_by_aid(dop, JNX_DATA_POLICY_NAME,
                                            dt.data_policy_name,
                                            sizeof(dt.data_policy_name)))
            == FALSE)
            break;

        if ((status = dax_get_stringr_by_aid(dop, JNX_DATA_POLICY_INTERFACE,
                                dt.data_pic_name,
                                sizeof(dt.data_pic_name)))
            == FALSE) {
            break;
        }

        idx = strlen(dt.data_pic_name);

        /* adjust the ifd name */
        while (idx) {
            if (dt.data_pic_name[idx] == '.') {
                dt.data_pic_name[idx] = '\0';
                break;
            }
            if (dt.data_pic_name[idx] == '-') {
                break;
            }
            idx--;
        }


        if ((status = dax_get_object_by_name(dop, jnx_gw_data_policy_obj[0], 
                                            &obj, FALSE)) == FALSE)
            break;

        /* get the client id */
        if ((status = dax_get_stringr_by_aid(obj, JNX_DATA_CLIENT_ID,
                                            dt.data_clnt_name,
                                            sizeof(dt.data_clnt_name)))
            == FALSE)
            break;

        /* get the client start port & range */
        if ((status = dax_get_ushort_by_aid(obj, JNX_DATA_CLIENT_START_PORT,
                                          &dt.data_start_port)) == FALSE)
            break;

        if ((status = dax_get_ushort_by_aid(obj, JNX_DATA_CLIENT_PORT_RANGE,
                                          &dt.data_port_range)) == FALSE)
            break;

        /* get the server id & port */
        if ((status = dax_get_stringr_by_aid(obj, JNX_DATA_SERVER_ID,
                                            dt.data_svr_name,
                                            sizeof(dt.data_svr_name))) == FALSE)
            break;

        if ((status = dax_get_ushort_by_aid(obj, JNX_DATA_SERVER_PORT,
                                          &dt.data_svr_port)) == FALSE)
            break;

        if (obj) dax_release_object(&obj);
        /* get the ingress vrf, gre gateway id  & start gre key id */

        if ((status = dax_get_object_by_name(dop, jnx_gw_data_policy_obj[1],
                                            &obj, FALSE)) == FALSE)
            break;

        if ((status = dax_get_stringr_by_aid(obj, JNX_DATA_INGRESS_VRF,
                                            dt.data_ivrf_name,
                                            sizeof(dt.data_ivrf_name)))
            == FALSE)
            break;

        if ((status = dax_get_stringr_by_aid(obj, JNX_DATA_INGRESS_SELF_ID,
                                            dt.data_iself_name,
                                            sizeof(dt.data_iself_name)))
            == FALSE)
            break;

        if ((status = dax_get_stringr_by_aid(obj, JNX_DATA_GRE_GATEWAY,
                                            dt.data_gre_gw_name,
                                            sizeof(dt.data_gre_gw_name)))
            == FALSE)
            break;

        if ((status = dax_get_uint_by_aid(obj, JNX_DATA_GRE_KEY,
                                         &dt.data_start_gre_key)) == FALSE)
            break;

        if (obj) dax_release_object(&obj);
        /* get the egress vrf & ipip gateway id */

        if ((status = dax_get_object_by_name(dop, jnx_gw_data_policy_obj[2],
                                            &obj, FALSE)) == FALSE)
            break;

        if ((status = dax_get_stringr_by_aid(obj, JNX_DATA_EGRESS_VRF,
                                            dt.data_evrf_name,
                                            sizeof(dt.data_evrf_name)))
            == FALSE)
            break;

        /* these are optionals */
        dax_get_stringr_by_aid(obj, JNX_DATA_IPIP_GATEWAY, dt.data_ipip_gw_name,
                               sizeof(dt.data_ipip_gw_name));

        dax_get_stringr_by_aid(obj, JNX_DATA_EGRESS_SELF_ID, dt.data_eself_name,
                               sizeof(dt.data_eself_name));

        if (obj) dax_release_object(&obj);

        /* add to the data static gre tunnel policy database */
        if (!jnx_gw_mgmt_add_data(&dt)) {
            status = FALSE;
            break;
        }
    }

    /* release the acquired object, return */
    if (top) dax_release_object(&top);
    if (dop) dax_release_object(&dop);
    if (obj) dax_release_object(&obj);

    if ((status) && (jnx_gw_mgmt.data_count) && (check != TRUE)) {
        jnx_gw_mgmt_send_data_config(NULL, JNX_GW_CONFIG_ADD);
    }

    return (!status);
}

/****************************************************************************
 *                                                                          *
 *          SAMPLE GATEWAY APP CONFIGURATION READ ROUTINES                  *
 *                                                                          *
 ****************************************************************************/
/**
 * 
 * This function is called on sighup, for reading the configuration
 * items for the sample gateway application
 *
 */
int
jnx_gw_mgmt_config_read (int check)
{
    int32_t status = FALSE;
    ddl_handle_t *top = NULL;

    if ((check == JNX_GW_CONFIG_READ_FORCE) &&
        dax_open_db(NULL, DNAME_JNX_GATEWAYD, PACKAGE_NAME,
                    SEQUENCE_NUMBER, FALSE)) {
        return status;
    }
    /* read the trace config for jnx gateway */
    
    if (check == JNX_GW_CONFIG_READ_FORCE &&
            junos_trace_read_config(FALSE, jnx_gw_config_path) != 0) {
        status = -1;
        goto done;
    }

    /* check application specific configuration */
    if (!dax_get_object_by_path(NULL, jnx_gw_config_path, &top, FALSE)){
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_DEBUG, "Configuration is absent");
        goto done;
    }

    /* if the configuration has changed, 
        read the configuration */
    if ((check == JNX_GW_CONFIG_READ_FORCE) ||
        (dax_is_changed(top) != FALSE)) {

        /* read user profile configuration */
        status = jnx_gw_mgmt_read_user_config(check);

        /* read control configuration */
        if (!status) {
            status = jnx_gw_mgmt_read_ctrl_config(check);
        }

        /* read data configuration */
        if (!status) {
            status = jnx_gw_mgmt_read_data_config(check);
        }
    }

    if (((jnx_gw_mgmt.user_count) && (jnx_gw_mgmt.ctrl_count)) ||
        (jnx_gw_mgmt.data_count)) {
        jnx_gw_mgmt.status = JNX_GW_MGMT_STATUS_UP;
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_INFO, "Configuration is valid!");
    } else {
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_INFO, "Configuration is not valid!");
        jnx_gw_mgmt.status = JNX_GW_MGMT_STATUS_INIT;
    }

    if (status) {
        jnx_gw_mgmt_log(JNX_GATEWAY_CONFIG, JNX_GATEWAY_TRACEFLAG_CONFIG,
                        LOG_ERR, "Configuration validatation failed!");
    }
    /* release the acquired object */
    if (top) {
        dax_release_object(&top);
    }

done:
    if (check == JNX_GW_CONFIG_READ_FORCE) {
        dax_close_db();
    }
    return status;
}

/****************************************************************************
 *                                                                          *
 *          SAMPLE GATEWAY APP CONFIGURATION INIT/SHUT ROUTINES             *
 *                                                                          *
 ****************************************************************************/
/**
 * This function initializes the configuation policy data bases.
 */
status_t
jnx_gw_mgmt_config_init()
{

    patricia_root_init (&jnx_gw_mgmt.user_prof_db, FALSE, JNX_GW_STR_SIZE, 0);
    patricia_root_init (&jnx_gw_mgmt.ctrl_pol_db, FALSE, JNX_GW_STR_SIZE, 0);
    patricia_root_init (&jnx_gw_mgmt.data_pol_db, FALSE, JNX_GW_STR_SIZE, 
                        (offsetof(jnx_gw_mgmt_data_t, data_policy_name) -
                         offsetof(jnx_gw_mgmt_data_t, data_node) -
                         sizeof(((jnx_gw_mgmt_data_t *)0)->data_node)));
    patricia_root_init (&jnx_gw_mgmt.intf_db, FALSE, sizeof(uint32_t), 0);

    jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_CONFIG, LOG_INFO,
                    "Configuration Module Initialization done");
    return EOK;
}

/**
 * This function cleans-up the configuation policy data bases.
 */
void
jnx_gw_mgmt_config_cleanup(void)
{
    jnx_gw_mgmt_clear_user_config();
    jnx_gw_mgmt_clear_ctrl_config();
    jnx_gw_mgmt_clear_data_config();

    jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_CONFIG, LOG_INFO, 
           "Configuration Module clear done");

}

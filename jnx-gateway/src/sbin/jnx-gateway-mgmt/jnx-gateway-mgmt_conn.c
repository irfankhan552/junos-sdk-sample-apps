/*
 * $Id: jnx-gateway-mgmt_conn.c 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-mgmt_conn.c - connection management with data & control agents
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyrignt (c) 2005-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <ctype.h>


#include <isc/eventlib.h>
#include <jnx/bits.h>
#include <ddl/ddl.h>
#include <jnx/junos_init.h>
#include <string.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/pconn.h>
#include <jnx/junos_kcom.h>
#include <jnx/vrf_util_pub.h>
#include <jnx/parse_ip.h>
#include JNX_GATEWAY_MGMT_OUT_H

#include <jnx/trace.h>
#include <jnx/junos_trace.h>

#include <jnx/jnx-gateway.h>
#include <jnx/jnx-gateway_msg.h>

#include "jnx-gateway-mgmt.h"
#include "jnx-gateway-mgmt_config.h"

void 
jnx_gw_mgmt_send_ctrl_pic_config(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                                 jnx_gw_mgmt_data_session_t * pdata_pic,
                                 uint8_t add_flag);
jnx_gw_mgmt_data_session_t * 
jnx_gw_mgmt_init_data_conn(pconn_session_t * session, char * pic_name);

jnx_gw_mgmt_data_session_t *
jnx_gw_mgmt_get_first_data_sesn(void);

jnx_gw_mgmt_ctrl_session_t * 
jnx_gw_mgmt_init_ctrl_conn(pconn_session_t * psesn, char * pic_name);

jnx_gw_mgmt_ctrl_session_t * jnx_gw_mgmt_get_first_ctrl_sesn(void);

jnx_gw_mgmt_data_session_t *
jnx_gw_mgmt_get_first_ctrl_data_sesn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic);

jnx_gw_mgmt_data_session_t *
jnx_gw_mgmt_get_next_ctrl_data_sesn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                                    jnx_gw_mgmt_data_session_t * pdata_pic);
status_t
jnx_gw_mgmt_add_ctrl_data_sesn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                               jnx_gw_mgmt_data_session_t * pdata_pic);
status_t
jnx_gw_mgmt_delete_ctrl_data_sesn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                                  jnx_gw_mgmt_data_session_t * pdata_pic);

/* patnode to structure converion function macros, for
 * control pic session, data pic session agent structures
 */

PATNODE_TO_STRUCT(jnx_gw_mgmt_ctrl_sesn_entry, jnx_gw_mgmt_ctrl_session_t, pic_node)
PATNODE_TO_STRUCT(jnx_gw_mgmt_data_sesn_entry, jnx_gw_mgmt_data_session_t, pic_node)
PATNODE_TO_STRUCT(jnx_gw_mgmt_ctrl_data_sesn_entry, jnx_gw_mgmt_data_session_t, ctrl_node)
PATNODE_TO_STRUCT(jnx_gw_mgmt_intf_entry, jnx_gw_mgmt_intf_t, intf_node)

#define JNX_GW_MGMT_FILL_PIC_MSG(pic_msg, pdata_pic) \
        ({\
                memset(pic_msg, 0, sizeof(pic_msg));\
                strncpy(pic_msg->pic_name, pdata_pic->pic_name, \
                        sizeof(pic_msg->pic_name));\
                pic_msg->pic_peer_type = htonl(pdata_pic->peer_type);\
                pic_msg->pic_ifd_id    = htonl(pdata_pic->ifd_id);\
                pic_msg->pic_fpc_id    = htonl(pdata_pic->fpc_id);\
                pic_msg->pic_pic_id    = htonl(pdata_pic->pic_id);\
         })
/****************************************************************************
 *                                                                          *
 *          CONTROL PIC SESSION MANAGEMENT ROUTINES                         *
 *                                                                          *
 ****************************************************************************/

/****************************************************************************
 *                                                                          *
 *               CONTROL PIC SESSION LOOKUP ROUTINES                        *
 *                                                                          *
 ****************************************************************************/

/**
 * This function returns the control pic session entry
 * for the pic name
 *
 * @params  pic_name     pic ifd name
 * @returns pctrl_sesn   control pic session entry
 *          NULL         if none
 */

jnx_gw_mgmt_ctrl_session_t *
jnx_gw_mgmt_ctrl_sesn_lookup (char * pic_name)
{
    patnode *pnode;

    if (pic_name == NULL) {
        return NULL;
    }
    if ((pnode = patricia_get(&jnx_gw_mgmt.ctrl_sesn_db,
                              strlen(pic_name) + 1, pic_name))) {
        return jnx_gw_mgmt_ctrl_sesn_entry(pnode);
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_CONN, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_DEBUG, "Control agent \"%s\" %s failed",
                    pic_name, "lookup");

    return NULL;
}

/**
 * This function returns the first control pic session entry
 * for the management agent
 *
 * @returns pctrl_sesn   control pic session entry
 *          NULL         if none
 */
jnx_gw_mgmt_ctrl_session_t *
jnx_gw_mgmt_get_first_ctrl_sesn(void)
{
    patnode *pnode;
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.ctrl_sesn_db, NULL))) {
        return jnx_gw_mgmt_ctrl_sesn_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next control pic session entry
 * for the management agent
 *
 * @params  pctrl_sesn   control pic session entry
 * @returns pctrl_sesn   next control pic session entry
 *          NULL         if none
 */
jnx_gw_mgmt_ctrl_session_t *
jnx_gw_mgmt_get_next_ctrl_sesn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic)
{
    patnode *pnode;
    if (pctrl_pic == NULL) {
        return jnx_gw_mgmt_get_first_ctrl_sesn();
    }
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.ctrl_sesn_db,
                                    &pctrl_pic->pic_node))) {
        return jnx_gw_mgmt_ctrl_sesn_entry(pnode);
    }
    return NULL;
}
                    
/**
 * This function returns the first data pic entry
 * assigned to the control pic agent entry
 *
 * @returns pdata_sesn   data pic session entry
 *          NULL         if none
 */
jnx_gw_mgmt_data_session_t *
jnx_gw_mgmt_get_first_ctrl_data_sesn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic)
{
    patnode *pnode;
    if ((pnode = patricia_find_next(&pctrl_pic->data_pics, NULL))) {
        return jnx_gw_mgmt_ctrl_data_sesn_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next data pic session entry
 * for the control pic agent
 *
 * @params  pdata_sesn   data pic session entry
 * @returns pdata_sesn   next data pic session entry
 *          NULL         if none
 */
jnx_gw_mgmt_data_session_t *
jnx_gw_mgmt_get_next_ctrl_data_sesn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                                    jnx_gw_mgmt_data_session_t * pdata_pic)
{
    patnode *pnode;
    if (pdata_pic == NULL) {
        return jnx_gw_mgmt_get_first_ctrl_data_sesn(pctrl_pic);
    }
    if ((pnode = patricia_find_next(&pctrl_pic->data_pics,
                                    &pdata_pic->ctrl_node))) {
        return jnx_gw_mgmt_ctrl_data_sesn_entry(pnode);
    }
    return NULL;
}


/****************************************************************************
 *                                                                          *
 *               CONTROL PIC SESSION ADD/DELETE ROUTINES                    *
 *                                                                          *
 ****************************************************************************/
/**
 * This function assigns a data pic agent to a control
 * pic agent
 * if the control pic agent has ateleast one data
 * pic agent, its status is marked as UP
 *
 * @params  pctrl_pic     control pic agent
 * @params  pdata_pic     data pic agent
 */
status_t
jnx_gw_mgmt_add_ctrl_data_sesn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                               jnx_gw_mgmt_data_session_t * pdata_pic)
{
    /* init the key length */
    patricia_node_init_length(&pdata_pic->ctrl_node,
                              strlen(pdata_pic->pic_name) + 1);

    /* add to the tree */
    if (!patricia_add(&pctrl_pic->data_pics, &pdata_pic->ctrl_node)) {

        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_WARNING, "Data Agent \"%s\" patricia %s to"
                        " Control agent \"%s\" failed",
                        pdata_pic->pic_name, "add", pctrl_pic->pic_name);
        return EFAIL;
    }

    pdata_pic->pic_status = JNX_GW_MGMT_STATUS_UP;
    pdata_pic->pctrl_pic  = pctrl_pic;

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT, LOG_INFO,
                    "Data Agent \"%s\" added to Control agent \"%s\"",
                    pdata_pic->pic_name, pctrl_pic->pic_name);

    /* set the control pic status to UP */
    pctrl_pic->data_pic_count++;

    if ((pctrl_pic->data_pic_count > 0) &&
        (pctrl_pic->pic_status != JNX_GW_MGMT_STATUS_UP)) {

        jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                        LOG_INFO, "Control agent \"%s\" marked UP",
                        pctrl_pic->pic_name);

        pctrl_pic->pic_status = JNX_GW_MGMT_STATUS_UP;
    }
    return EOK;
}

/**
 * This function deletes the data pic agent from a control
 * pic agent
 * if the control pic agent has no data pic agent,
 * its status is marked as INIT
 *
 * @params  pctrl_pic     control pic agent
 * @params  pdata_pic     data pic agent
 */
status_t
jnx_gw_mgmt_delete_ctrl_data_sesn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                                  jnx_gw_mgmt_data_session_t * pdata_pic)
{
    pdata_pic->pic_status = JNX_GW_MGMT_STATUS_INIT;
    pdata_pic->pctrl_pic = NULL;

    /* delete from the control agent data pic list */
    if (!patricia_delete(&pctrl_pic->data_pics, &pdata_pic->ctrl_node)) {

        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG, "Data Agent \"%s\" patricia %s "
                        "from Control agent \"%s\" failed",
                        pdata_pic->pic_name, "delete", pctrl_pic->pic_name);

        return EFAIL;
    }

    /*decrement the data agent counter for the control agent */
    pctrl_pic->data_pic_count--;

    /*
     * reset the control pic status, if it does
     * not handle any data pic agent
     */
    if (pctrl_pic->data_pic_count == 0) {

        jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                        LOG_INFO, "Marking Control agent \"%s\" as INIT",
                        pctrl_pic->pic_name);

        pctrl_pic->pic_status = JNX_GW_MGMT_STATUS_INIT;
    }
    return EOK;
}

/**
 *
 * This function adds a control pic session
 * entry, when the control pic comes up,
 * and the control agent on the pic sends
 * the register message
 *
 * @params  psesn       pconn server session entry
 * @params  pic_name    control pic ifd name
 * @returns pctrl_pic   if the add successful
 *          NULL        otherwise
 */
jnx_gw_mgmt_ctrl_session_t * 
jnx_gw_mgmt_init_ctrl_conn(pconn_session_t * psession, char * pic_name)
{
    jnx_gw_mgmt_ctrl_session_t * pctrl_pic;
    pconn_peer_info_t peer_info;
    pconn_client_params_t params;

    if (pconn_session_get_peer_info(psession, &peer_info) != PCONN_OK) {
        return NULL;
    }

    if ((pctrl_pic = JNX_GW_MALLOC(JNX_GW_MGMT_ID,
                                   sizeof(jnx_gw_mgmt_ctrl_session_t)))
        == NULL) {
        jnx_gw_mgmt_log(JNX_GATEWAY_ALLOC, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_ERR, "Control agent \"%s\" %s failed",
                        pic_name, "malloc");
        return NULL;
    }

    pctrl_pic->psession   = psession;
    pctrl_pic->peer_type  = peer_info.ppi_peer_type; 
    pctrl_pic->fpc_id     = peer_info.ppi_fpc_slot;
    pctrl_pic->pic_id     = peer_info.ppi_pic_slot;
    pctrl_pic->ifd_id     = jnx_gw_ifd_get_id(pic_name);
    pctrl_pic->pic_status = JNX_GW_MGMT_STATUS_INIT;

    strncpy(pctrl_pic->pic_name, pic_name, sizeof(pctrl_pic->pic_name));

    /* initialize the data pic agent list for the control agent */
    patricia_root_init (&pctrl_pic->data_pics, FALSE, JNX_GW_STR_SIZE, 
                        offsetof(jnx_gw_mgmt_data_session_t, pic_name) -
                        offsetof(jnx_gw_mgmt_data_session_t, ctrl_node) -
                        sizeof(((jnx_gw_mgmt_data_session_t *)0)->ctrl_node));

    /* init the key length */
    patricia_node_init_length(&pctrl_pic->pic_node,
                              strlen(pctrl_pic->pic_name) + 1);
    /* add to the tree */
    if (!patricia_add(&jnx_gw_mgmt.ctrl_sesn_db, &pctrl_pic->pic_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_ERR, "Control agent \"%s\" patricia %s failed",
                        pctrl_pic->pic_name, "add");
        /* do trace log */
        JNX_GW_FREE(JNX_GW_MGMT_ID, pctrl_pic);
        return NULL;
    }


    memset(&params, 0, sizeof(pconn_client_params_t));
    params.pconn_port      = JNX_GW_CTRL_PORT;
    params.pconn_peer_info.ppi_peer_type = pctrl_pic->peer_type; 
    params.pconn_peer_info.ppi_fpc_slot  = pctrl_pic->fpc_id;
    params.pconn_peer_info.ppi_pic_slot  = pctrl_pic->pic_id;


    if (!(pctrl_pic->opcmd_conn = pconn_client_connect(&params))) {

        jnx_gw_mgmt_log(JNX_GATEWAY_CONN, JNX_GATEWAY_TRACEFLAG_EVENT,
                        LOG_ERR, "Control agent \"%s\" %s failed",
                        pic_name, "client connect");

        if (!patricia_delete(&jnx_gw_mgmt.ctrl_sesn_db, &pctrl_pic->pic_node)) {
            jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                            LOG_EMERG,
                            "Control agent \"%s\" patricia %s failed",
                            pic_name, "delete");
        }
        JNX_GW_FREE(JNX_GW_MGMT_ID, pctrl_pic);
        return NULL;
    }

    /* set the pconn server session cookie */
    pconn_server_set_session_cookie(psession, pctrl_pic);

    /* pic up some data pics, & send the pic add message */
    jnx_gw_mgmt_send_ctrl_pic_config(pctrl_pic, NULL, JNX_GW_CONFIG_ADD);

    jnx_gw_mgmt_ctrl_update_config(pctrl_pic, JNX_GW_CONFIG_ADD);

    jnx_gw_mgmt.ctrl_sesn_count++;

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT, LOG_INFO, 
                    "Control Agent \"%s\" %s", pic_name, "add");

    /* do success trace log */
    return pctrl_pic;
}

/**
 *
 * This function sends configurations/interfaces
 * to the control pic agent
 *
 * @params  pctrl_pic   control pic session entry
 * @params  add_flag    add/delete
 */

void
jnx_gw_mgmt_ctrl_update_config(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                             uint8_t add_flag)
{
    /* send the configuration to the up control plane agent */
    jnx_gw_mgmt_send_user_configs(pctrl_pic, add_flag);
    jnx_gw_mgmt_send_ctrl_configs(pctrl_pic, add_flag);

    /* send the interfaces to the control pic */
    jnx_gw_mgmt_send_ctrl_intf_config(pctrl_pic, NULL, add_flag);
}

/**
 *
 * This function deletes a control pic session
 * entry, when the control pic goes down,
 * this is indicated either by the pconn server
 * session hutdown message, or the ifd delete event
 * from kcom
 * it deletes the set of data pics assigned to itself
 * from its responsibility list, & assignment of
 * these data pics to other control agents are 
 * tried
 *
 * @params  pctrl_pic    control pic session entry
 */
status_t 
jnx_gw_mgmt_destroy_ctrl_conn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                              uint32_t shutdown_flag)
{

    jnx_gw_mgmt_data_session_t * pdata_pic = NULL;

    /* close the control pic connections */
    if (pctrl_pic->psession) {
        pconn_session_close(pctrl_pic->psession);
        pctrl_pic->psession = NULL;
    }

    if (pctrl_pic->opcmd_conn) {
        pconn_client_close(pctrl_pic->opcmd_conn);
        pctrl_pic->opcmd_conn = NULL;
    }

    /* update the control pic config */
    jnx_gw_mgmt_ctrl_update_config(pctrl_pic, JNX_GW_CONFIG_DELETE);

    /* send the associated data pic delete message */
    jnx_gw_mgmt_send_ctrl_pic_config(pctrl_pic, NULL, JNX_GW_CONFIG_DELETE);

    /* delete from the tree & free the structure */
    if (!patricia_delete(&jnx_gw_mgmt.ctrl_sesn_db, &pctrl_pic->pic_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG, "Control agent \"%s\" patricia %s failed",
                        pctrl_pic->pic_name, "delete");
    }


    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT, 
                    LOG_INFO, "Control agent \"%s\" %s",
                    pctrl_pic->pic_name, "delete");

    JNX_GW_FREE(JNX_GW_MGMT_ID, pctrl_pic);
    jnx_gw_mgmt.ctrl_sesn_count--;

    if (shutdown_flag) { 
        return EOK;
    }

    /* try to reassign the orphaned data agent 
     * pics to some other control pic agent
     */

    while ((pdata_pic = jnx_gw_mgmt_get_next_data_sesn(pdata_pic)) != NULL) {

        if (pdata_pic->pic_status == JNX_GW_MGMT_STATUS_UP) {
            continue;
        }
        jnx_gw_mgmt_send_ctrl_pic_config(NULL, pdata_pic, JNX_GW_CONFIG_ADD);
    }

    return EOK;
}

/****************************************************************************
 *                                                                          *
 *               CONTROL PIC SESSION EVENT/MESSAGE HANDLER ROUTINES         *
 *                                                                          *
 ****************************************************************************/

/**
 *
 * This function handles the pconn server
 * session menagement for the control 
 * pic agent,
 * on  event established, means a new  control
 * pic agent has come up, creates the control
 * pic session entry
 * on event shutdown/fail, cleans up the 
 * control pic session entry
 *
 * @params  session    pconn session entry
 * @params  event      pconn event
 * @params  cookie     unused
 */
void
jnx_gw_mgmt_ctrl_event_handler(pconn_session_t * session,
                               pconn_event_t event, void * cookie __unused)
{
    jnx_gw_mgmt_ctrl_session_t * pctrl_pic;
    char pic_name[JNX_GW_STR_SIZE];

    jnx_gw_mgmt_get_pconn_pic_name(session, pic_name);
    pctrl_pic = jnx_gw_mgmt_ctrl_sesn_lookup(pic_name);

    if (jnx_gw_mgmt.status == JNX_GW_MGMT_STATUS_INIT) {
        jnx_gw_mgmt_config_read(JNX_GW_CONFIG_READ_FORCE);
    }

    switch (event) {
        case PCONN_EVENT_ESTABLISHED:
            /* the entry already exists, return */
            jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                            LOG_INFO, "Control Agent \"%s\" connect received",
                            pic_name);

            if (pctrl_pic) {
                break;
            }
            jnx_gw_mgmt_init_ctrl_conn(session, pic_name);
            break;

        case PCONN_EVENT_SHUTDOWN:
        case PCONN_EVENT_FAILED:
            /* the entry does not exist, return */
            jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                            LOG_INFO, "Control Agent \"%s\" shutdown received",
                            pic_name);
            if (pctrl_pic == NULL) {
                break;
            }
            pctrl_pic->psession = NULL;
            jnx_gw_mgmt_destroy_ctrl_conn(pctrl_pic, FALSE);
            break;

        default:
            jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                            LOG_WARNING, "Control Agent \"%s\" "
                            " unknown event(%d) received",
                            pic_name, event);
            return;
    }

    /* here do an explicit scan ifls/rtbs to be update
     * with the ifl/rtb states for the box
     */
    junos_kcom_ifl_get_all(jnx_gw_mgmt_get_ifl, NULL, NULL);
    junos_kcom_rtb_get_all(jnx_gw_mgmt_get_rtb, AF_INET, NULL);

    return;
}

/**
 *
 * This function handles the messages from the 
 * control pic agent, this is for periodic
 * status update messages imitted by the control
 * pic agents for session status information
 *
 * @params  session     pconn server session entry
 * @params  msg         message pointer
 * @params  cookie      cookie
 * @return  status      
 *            EOK       if control pic agent found
 *            EFAIL     otherwise
 */
status_t
jnx_gw_mgmt_ctrl_msg_handler (pconn_session_t * session,
                              ipc_msg_t * ipc_msg, void * cookie)
{
    char pic_name[JNX_GW_STR_SIZE];
    jnx_gw_msg_header_t      *pmsg = NULL;
    jnx_gw_msg_sub_header_t  *sub_hdr = NULL;
    jnx_gw_periodic_stat_t   *stat_p = NULL;
    jnx_gw_mgmt_ctrl_session_t *pctrl_pic = NULL;

    /*
     * return incase, the session does not exist, is not in up state
     * or, the message type is not valid
     */
    pctrl_pic = (typeof(pctrl_pic))cookie;

    jnx_gw_mgmt_get_pconn_pic_name (session, pic_name);

    if  (!strlen(pic_name)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                        LOG_DEBUG, "Could not resolve the control agent "
                        " on message receipt");
        return EOK;
    }

    if (!pctrl_pic) {
        if ((pctrl_pic = jnx_gw_mgmt_ctrl_sesn_lookup(pic_name)) == NULL) {
            jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                            LOG_DEBUG, 
                            "Unknown control agent \"%s\" message received",
                            pic_name);
            return EOK;
        }
    }

    if (ipc_msg->subtype != JNX_GW_STAT_PERIODIC_MSG) {
        jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO, 
                        LOG_DEBUG,
                        "Control agent \"%s\" unknown message (%d) received",
                        pic_name, ipc_msg->type);
        return EOK;
    }

    if (pctrl_pic->pic_status != JNX_GW_MGMT_STATUS_UP) {
        jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                        LOG_DEBUG, "Control agent \"%s\" (DOWN) Hello received",
                        pic_name);
        return EOK;
    }

    pmsg    = (typeof(pmsg))(ipc_msg->data);
    sub_hdr = (typeof(sub_hdr))((char *)pmsg + sizeof(*pmsg));
    stat_p  = (typeof(stat_p))((char *)sub_hdr + sizeof(*sub_hdr));

    /* session info for the whole control pic */
    pctrl_pic->num_sesions       = ntohl(stat_p->total_sessions);
    pctrl_pic->active_sessions   = ntohl(stat_p->active_sessions);
    pctrl_pic->ipip_tunnel_count = ntohl(stat_p->ipip_tunnel_count);

    /* reset the control agent hello missed counter */
    pctrl_pic->tstamp            = 0;

    jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                    LOG_DEBUG, "Control agent \"%s\" Hello received", pic_name);

    return EOK;
}

/**
 *
 * This function sends the data pic assignement
 * configuration information to the control
 * pic agent
 *
 * @params  pctrl_pic     control pic session entry
 * @params  pdata_pic     data pic session entry
 * @params  add_flag      add/delete
 */
void 
jnx_gw_mgmt_send_ctrl_pic_config(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                                 jnx_gw_mgmt_data_session_t * pdata_pic,
                                 uint8_t add_flag)
{
    uint8_t msg_count = 0;
    uint16_t len = 0, sublen = 0;
    jnx_gw_msg_ctrl_config_pic_t * pic_msg = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_msg_header_t * hdr = NULL;

    /* put the message header */
    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    msg_count     = 0;
    len           = sizeof(*hdr);
    hdr->msg_type = JNX_GW_CONFIG_PIC_MSG;

    sublen = sizeof(*subhdr) + sizeof(*pic_msg);

    /* we may not fill up the whole buffer, while sending
       the pic info messages */

    switch (add_flag) {

        case JNX_GW_CONFIG_ADD:

            /* add them to the list */
            if (pdata_pic) {

                /* find an appropriate control pic,
                 * for now take the first one
                 */

                if ((pctrl_pic == NULL) && 
                    ((pctrl_pic = jnx_gw_mgmt_get_first_ctrl_sesn())
                     == NULL)) {
                    return;
                }

                /* add the ctrl data session */
                jnx_gw_mgmt_add_ctrl_data_sesn(pctrl_pic, pdata_pic);

                /* subheader fill */
                subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
                subhdr->sub_type = add_flag;
                subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
                subhdr->length   = htons(sublen);

                /* message fill */
                pic_msg = (typeof(pic_msg))
                    ((uint8_t *)subhdr + sizeof(*subhdr));

                JNX_GW_MGMT_FILL_PIC_MSG(pic_msg, pdata_pic);

                len += sublen;
                msg_count++;
                break;
            }

            /* if no data pic is supplied, this is control pic agent
             * coming up, try to get some data pics for the control
             * pic agent
             */
            while ((pdata_pic = jnx_gw_mgmt_get_next_data_sesn(pdata_pic))) {

                /* this data pic agent is already assigned */
                if (pdata_pic->pic_status != JNX_GW_MGMT_STATUS_INIT)  {
                    continue;
                }

                /* add the ctrl data session */
                jnx_gw_mgmt_add_ctrl_data_sesn(pctrl_pic, pdata_pic);

                /* subheader fill */
                subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
                subhdr->sub_type = add_flag;
                subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
                subhdr->length   = htons(sublen);

                /* message fill */
                pic_msg = (typeof(pic_msg))((uint8_t *)subhdr +
                                            sizeof(*subhdr));

                JNX_GW_MGMT_FILL_PIC_MSG(pic_msg, pdata_pic);

                len += sublen;
                msg_count++;
            }

            break;

        case JNX_GW_CONFIG_DELETE:

            /* has not been assigned to any control pic
             *  yet, just return 
             */
            if (pctrl_pic == NULL) return;

            /* the data pic is going down */
            if (pdata_pic) {

                /* delete the data pic session */
                jnx_gw_mgmt_delete_ctrl_data_sesn(pctrl_pic, pdata_pic);

                subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
                subhdr->sub_type = add_flag;
                subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
                subhdr->length   = htons(sublen);

                /* message fill */
                pic_msg = (typeof(pic_msg))
                    ((uint8_t *)subhdr + sizeof(*subhdr));

                JNX_GW_MGMT_FILL_PIC_MSG(pic_msg, pdata_pic);

                len += sublen;
                msg_count++;
                break;
            }

            /* the control pic is going down, try to
             * reassign the data pics to some other control
             * pics
             */

            /* delete the data pics from the list */
            while ((pdata_pic = 
                    jnx_gw_mgmt_get_first_ctrl_data_sesn(pctrl_pic))) {

                /* delete the data pic session */
                jnx_gw_mgmt_delete_ctrl_data_sesn(pctrl_pic, pdata_pic);

                subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
                subhdr->sub_type = add_flag;
                subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
                subhdr->length   = htons(sublen);

                /* message fill */
                pic_msg = (typeof(pic_msg))((uint8_t *)subhdr +
                                            sizeof(*subhdr));

                JNX_GW_MGMT_FILL_PIC_MSG(pic_msg, pdata_pic);

                len += sublen;
                msg_count++;
            }

            break;

        default:
            break;
    }

    /* if only there are messages, send it across */
    if (msg_count) {
        /* set the length & message count */
        hdr->count    = msg_count;
        hdr->msg_len  = htons(len);
        jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, JNX_GW_CONFIG_PIC_MSG, hdr, len);
    }
    return;
}

/****************************************************************************
 *                                                                          *
 *               CONTROL PIC SESSION SEND/RECEIVE HANDLER ROUTINES          *
 *                                                                          *
 ****************************************************************************/

/**
  * This function handles the operational command responses
  * from the control pic session agent
  * when the control pic session  entry is NULL, it means try to
  * receive from all the control pic sessions
  * @param   pctrl_pic  - control pic session entry
  *          NULL       - do it for all the control pic sesions
  * @returns msg       - message received from the control pic session
  *          NULL      - if none
  */

void *
jnx_gw_mgmt_ctrl_recv_opcmd(jnx_gw_mgmt_ctrl_session_t * pctrl_pic)
{
    void * pmsg;

    if (pctrl_pic) {
        if (pctrl_pic->opcmd_conn) {
            return pconn_client_recv(pctrl_pic->opcmd_conn);
        }
        return NULL;
    }

    /* if the pctrl_pic entry is NULL, we need to receive from
        the to all active ctrl agents */
    while ((pctrl_pic = jnx_gw_mgmt_get_next_ctrl_sesn(pctrl_pic))) {
        if (!pctrl_pic->opcmd_conn) continue;
        if ((pmsg = pconn_client_recv(pctrl_pic->opcmd_conn)))
            return pmsg;
    }
    return NULL;
}

/**
 * This function sends the operational command request
 * message to the control pic session agents
 * when the data pic session entry is NULL, it means try to
 * send to all the data pic sessions
 * @param  pctrl_pic  - control pic session entry
 *         NULL       - do it for all the control pic sesion entries
 * @returns status 
 *         EOK      - message delivered
 *         EFAIL    - message send failed
 */

status_t 
jnx_gw_mgmt_ctrl_send_opcmd(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                            int32_t type, void * msg, int16_t len)
{
    if (pctrl_pic) {
        if (pctrl_pic->opcmd_conn) {
            return pconn_client_send(pctrl_pic->opcmd_conn, type, msg, len);
        }
        return EOK;
    }

    /* if the pctrl_pic entry is NULL, we need to send the message
       to all active control agents */
    while ((pctrl_pic = jnx_gw_mgmt_get_next_ctrl_sesn(pctrl_pic)) != NULL) {
        if (!pctrl_pic->opcmd_conn) continue;
        pconn_client_send(pctrl_pic->opcmd_conn, type, msg, len);
    }
    return EOK;
}

/**
 * This function sends a message to the control pic agent
 * through the pconn server session entry 
 * if the control pic session entry is null, sends to all
 * the control pics
 * @params    pctrl_pic    control pic session entry
 * @params    type         message type
 * @params    msg          message buffer
 * @params    len          message length
 * @returns   status  
 *            EOK          message sent
 *            EFAIL        message send failed
 */
int32_t
jnx_gw_mgmt_send_ctrl_msg(jnx_gw_mgmt_ctrl_session_t *pctrl_pic, int type,
                          void * msg, int16_t len)
{
    
    if (pctrl_pic) {
        if (pctrl_pic->opcmd_conn) {
            return pconn_client_send(pctrl_pic->opcmd_conn, type, msg, len);
        }
        return EOK;
    }

    /* if the pctrl_pic entry is NULL, we need to send the message
       to all active control agents */
    while ((pctrl_pic = jnx_gw_mgmt_get_next_ctrl_sesn(pctrl_pic)) != NULL) {
        if (!pctrl_pic->opcmd_conn) continue;
        pconn_client_send(pctrl_pic->opcmd_conn, type, msg, len);
    }
    return EOK;
}

/****************************************************************************
 *                                                                          *
 *               DATA PIC SESSION MANAGEMENT ROUTINES                       *
 *                                                                          *
 ****************************************************************************/

/****************************************************************************
 *                                                                          *
 *               INTERFACE MANAGEMENT ROUTINES                              *
 *                                                                          *
 ****************************************************************************/
/*
 * This function finds an  interface entry 
 *
 * @params  intf_index   interface index
 * @returns pintf        interface entry
 *          NULL         if none
 */
jnx_gw_mgmt_intf_t * 
jnx_gw_mgmt_intf_lookup(uint32_t intf_index)
{
    patnode * pnode;
    if ((pnode = patricia_get(&jnx_gw_mgmt.intf_db,
                              sizeof(intf_index), &intf_index))) {
        return jnx_gw_mgmt_intf_entry(pnode);
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT, LOG_DEBUG,
                    "Interface \"%d\" %s failed", intf_index, "lookup");

    return NULL;
}

jnx_gw_mgmt_intf_t * jnx_gw_mgmt_get_first_intf(void);
jnx_gw_mgmt_intf_t * jnx_gw_mgmt_get_next_intf(jnx_gw_mgmt_intf_t * pintf);
/**
 * This function returns the first interface entry
 * on the mangement agent
 *
 * @returns pintf   interface entry
 *          NULL    if none
 */
jnx_gw_mgmt_intf_t *
jnx_gw_mgmt_get_first_intf(void)
{
    patnode * pnode;
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.intf_db, NULL))) {
        return jnx_gw_mgmt_intf_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next data pic session entry
 * on the mangement agent
 *
 * @params  pdata_sesn   data pic session entry
 * @returns pdata_sesn   next data pic session entry
 *          NULL         if none
 */
jnx_gw_mgmt_intf_t *
jnx_gw_mgmt_get_next_intf(jnx_gw_mgmt_intf_t * pintf)
{
    patnode * pnode;
    if (pintf == NULL) {
        return jnx_gw_mgmt_get_first_intf();
    }
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.intf_db, 
                                    &pintf->intf_node))) {
        return jnx_gw_mgmt_intf_entry(pnode);
    }
    return NULL;
}

/*
 * This function adds an interface entry to the interface db
 *
 * @params  intf_name    ifl_name
 * @params  intf_index   ifl_index
 * @params  vrf_index    vrf_index
 * @params  subunit      ifl subunit number
 * @returns pintf        intf entry
 *          NULL         if none
 */

jnx_gw_mgmt_intf_t * 
jnx_gw_mgmt_intf_add(char * intf_name, uint32_t intf_index, uint32_t vrf_id,
                     uint32_t subunit)
{
    jnx_gw_mgmt_intf_t *  pintf;

    if ((pintf = JNX_GW_MALLOC(JNX_GW_MGMT_ID, sizeof(*pintf))) == NULL) {
        jnx_gw_mgmt_log(JNX_GATEWAY_ALLOC, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_ERR, "Interface \"%s.%d\" %s failed",
                        intf_name, subunit, "malloc");
        return NULL;
    }

    strncpy(pintf->intf_name, intf_name, sizeof(pintf->intf_name));
    pintf->intf_index   = intf_index;
    pintf->intf_subunit = subunit;
    pintf->intf_vrf_id  = vrf_id;
    pintf->intf_status  = JNX_GW_MGMT_STATUS_INIT;

    /* init the key length */
    patricia_node_init_length(&pintf->intf_node, sizeof(pintf->intf_index));

    if (!patricia_add(&jnx_gw_mgmt.intf_db, &pintf->intf_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG, "Interface \"%s.%d\" patricia %s failed",
                        intf_name, subunit, "add");
        JNX_GW_FREE(JNX_GW_MGMT_ID, pintf);
        return NULL;
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_INFO, "Interface \"%s.%d\" %s",
                    intf_name, subunit, "add");

    jnx_gw_mgmt.intf_count++;
    return pintf;
}

/*
 * This function delete an interface entry from the interface db
 *
 * @params pintf        intf entry
 */
void
jnx_gw_mgmt_intf_delete( jnx_gw_mgmt_intf_t * pintf)
{

    if (!patricia_delete(&jnx_gw_mgmt.intf_db, &pintf->intf_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG,
                        "Interface \"%s.%d\" patricia %s failed",
                        pintf->intf_name, pintf->intf_subunit, "delete");
    }
    JNX_GW_FREE(JNX_GW_MGMT_ID, pintf);
    jnx_gw_mgmt.intf_count--;
    return;
}



/*
 * This function sends the rtb messages to the control agents
 *
 * @params pctrl_pic    control agent entry
 * @params rtb          kcom rtb information structure pointer
 * @params add_flag     add or delete the rtb entry
 * @params sig_flag     signaling on the vrf (ok/nop/down)
 * @params flags        route mangement flags
 */
void
jnx_gw_mgmt_ctrl_send_vrf_msg(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                              int32_t vrf_id, uint8_t add_flag,
                              uint8_t sig_flag, uint8_t flags)
{
    uint8_t msg_count = 0;
    uint8_t vrf_name[JNX_GW_STR_SIZE];
    uint16_t len = 0, sublen = 0;
    jnx_gw_msg_ctrl_config_vrf_t * vrf_msg = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_msg_header_t * hdr = NULL;

    /* invalid/internal vrf, return */
    if ((vrf_id == JNX_GW_INVALID_VRFID) || (vrf_id == 1) || (vrf_id == 2)) {
        return;
    }

    jnx_gw_get_vrf_name(vrf_id, vrf_name, sizeof(vrf_name));

    /* send the vrf messages */

    /* put the message header */
    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    msg_count     = 0;
    len           = sizeof(*hdr);
    hdr->msg_type = JNX_GW_CONFIG_VRF_MSG;
    sublen = sizeof(*subhdr) + sizeof(*vrf_msg);

    /* subheader fill */
    subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
    subhdr->sub_type = add_flag;
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
    subhdr->length   = htons(sublen);

    /* message fill */
    vrf_msg = (typeof(vrf_msg))((uint8_t *)subhdr + sizeof(*subhdr));

    strncpy(vrf_msg->vrf_name, vrf_name, sizeof(vrf_msg->vrf_name));
    vrf_msg->vrf_id   = htonl(vrf_id);
    vrf_msg->sig_flag = sig_flag;
    vrf_msg->flags    = flags;

    len += sublen;
    msg_count++;

    /* set the length & message count */
    hdr->count    = msg_count;
    hdr->msg_len  = htons(len);
    jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, JNX_GW_CONFIG_VRF_MSG,
                              hdr, len);

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_INFO, "VRF \"%s\" (%d) %s, %s",  vrf_name,
                    vrf_id, (add_flag) ? "delete" : "add", 
                    (sig_flag == JNX_GW_CTRL_SIGNALING_ENABLE) ?
                    "signaling" : (sig_flag == JNX_GW_CTRL_SIGNALING_NOOP) ?
                    "no-op" : "no-signaling");

    /* now push the policy configs  with the same add flag*/
    jnx_gw_mgmt_send_user_config(pctrl_pic, vrf_id, add_flag);
    jnx_gw_mgmt_send_ctrl_config(pctrl_pic, vrf_id, add_flag);
}

/**
 *
 * This function sends the data pic assignement
 * configuration information to the control
 * pic agent
 *
 * @params  pctrl_pic     control pic session entry
 * @params  pdata_pic     data pic session entry
 * @params  pintf         data intf entry
 * @params  add_flag      add/delete
 */
void 
jnx_gw_mgmt_send_ctrl_intf_config(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                                  jnx_gw_mgmt_intf_t * pintf, 
                                  uint8_t add_flag)
{
    uint8_t msg_count = 0, intf_flag = FALSE;
    uint16_t len = 0, sublen = 0;
    jnx_gw_msg_ctrl_config_intf_t * intf_msg = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_msg_header_t * hdr = NULL;

    if (pintf) {
        intf_flag = TRUE;
    }


    /* 
     * for the control pic, pluck the interfaces vrf
     * with the same ifd, for enabling signaling
     */

    if (pctrl_pic) {

        /* send the vrf messages */
        if (pintf) {
            if (!strncmp(pctrl_pic->pic_name, pintf->intf_name,
                         sizeof(pctrl_pic->pic_name))) {
                jnx_gw_mgmt_ctrl_send_vrf_msg(pctrl_pic, pintf->intf_vrf_id,
                                              add_flag, 
                                              JNX_GW_CTRL_SIGNALING_ENABLE, 0);
            }
        } else while ((pintf = jnx_gw_mgmt_get_next_intf(pintf))) {

            if (!strncmp(pctrl_pic->pic_name, pintf->intf_name,
                         sizeof(pctrl_pic->pic_name))) {
                jnx_gw_mgmt_ctrl_send_vrf_msg(pctrl_pic, pintf->intf_vrf_id,
                                              add_flag, 
                                              JNX_GW_CTRL_SIGNALING_ENABLE, 0);
            }
        }
    } else if (pintf) {
        pctrl_pic = jnx_gw_mgmt_ctrl_sesn_lookup(pintf->intf_name);
        if ((pctrl_pic) &&
            !strncmp(pctrl_pic->pic_name, pintf->intf_name,
                     sizeof(pctrl_pic->pic_name))) {
            jnx_gw_mgmt_ctrl_send_vrf_msg(pctrl_pic, pintf->intf_vrf_id,
                                          add_flag, 
                                          JNX_GW_CTRL_SIGNALING_ENABLE, 0);
        }
        pctrl_pic = NULL;
    } else while ((pintf = jnx_gw_mgmt_get_next_intf(pintf))) {

        pctrl_pic = jnx_gw_mgmt_ctrl_sesn_lookup(pintf->intf_name);

        if ((pctrl_pic) &&
            !strncmp(pctrl_pic->pic_name, pintf->intf_name,
                     sizeof(pctrl_pic->pic_name))) {
            jnx_gw_mgmt_ctrl_send_vrf_msg(pctrl_pic, pintf->intf_vrf_id,
                                          add_flag, 
                                          JNX_GW_CTRL_SIGNALING_ENABLE, 0);
        }
        pctrl_pic = NULL;
    } /* if (pctrl_pic) */

    /* send the interface messages */

    /* put the message header */
    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    msg_count     = 0;
    len           = sizeof(*hdr);
    hdr->msg_type = JNX_GW_CONFIG_INTF_MSG;

    sublen = sizeof(*subhdr) + sizeof(*intf_msg);

    if (intf_flag == TRUE) {
        goto ctrl_config_intf_fill;
    }

    while ((pintf = jnx_gw_mgmt_get_next_intf(pintf))) {

        if (intf_flag == TRUE) {
            break;
        }

        /* if only there are messages, send it across */
        if ((msg_count > JNX_GW_MAX_MSG_COUNT) ||
            ((len + sublen) > JNX_GW_MAX_PKT_BUF_SIZE)) {

            /* set the length & message count */
            hdr->count    = msg_count;
            hdr->msg_len  = htons(len);
            jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, JNX_GW_CONFIG_INTF_MSG,
                                      hdr, len);
            /* reset the header, length etc. */
            msg_count     = 0;
            len           = sizeof(*hdr);
            hdr->msg_type = JNX_GW_CONFIG_INTF_MSG;
        }

ctrl_config_intf_fill:

        /* subheader fill */
        subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
        subhdr->sub_type = add_flag;
        subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
        subhdr->length   = htons(sublen);

        /* message fill */
        intf_msg = (typeof(intf_msg))((uint8_t *)subhdr + sizeof(*subhdr));

        strncpy(intf_msg->intf_name, pintf->intf_name, 
                sizeof(intf_msg->intf_name));

        intf_msg->intf_index  = htonl(pintf->intf_index);
        intf_msg->intf_vrf_id = htonl(pintf->intf_vrf_id);
        intf_msg->intf_subunit = htonl(pintf->intf_subunit);

        len += sublen;
        msg_count++;

    }

    /* if only there are messages, send it across */
    if (msg_count) {
        /* set the length & message count */
        hdr->count    = msg_count;
        hdr->msg_len  = htons(len);
        jnx_gw_mgmt_send_ctrl_msg(pctrl_pic, JNX_GW_CONFIG_INTF_MSG,
                                  hdr, len);
    }

    return;
}

/****************************************************************************
 *                                                                          *
 *               DATA PIC SESSION LOOKUP ROUTINES                           *
 *                                                                          *
 ****************************************************************************/

/**
 * This function returns the data pic session entry
 * for the pic name
 *
 * @params  pic_name     pic ifd name
 * @returns pdata_sesn   data pic session entry
 *          NULL         if none
 */
jnx_gw_mgmt_data_session_t *
jnx_gw_mgmt_data_sesn_lookup (char * pic_name)
{
    patnode *pnode;

    if (pic_name) {
        if ((pnode = patricia_get(&jnx_gw_mgmt.data_sesn_db,
                                  strlen(pic_name) + 1, pic_name))) {
            return jnx_gw_mgmt_data_sesn_entry(pnode);
        }
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_INFO, "Data Agent \"%s\" %s failed",
                    pic_name, "lookup");

    return NULL;
}
                    
/**
 * This function returns the first data pic entry
 * on the mangement agent
 *
 * @returns pdata_sesn   data pic session entry
 *          NULL         if none
 */
jnx_gw_mgmt_data_session_t *
jnx_gw_mgmt_get_first_data_sesn(void)
{
    patnode * pnode;
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.data_sesn_db, NULL))) {
        return jnx_gw_mgmt_data_sesn_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next data pic session entry
 * on the mangement agent
 *
 * @params  pdata_sesn   data pic session entry
 * @returns pdata_sesn   next data pic session entry
 *          NULL         if none
 */
jnx_gw_mgmt_data_session_t *
jnx_gw_mgmt_get_next_data_sesn(jnx_gw_mgmt_data_session_t * pdata_pic)
{
    patnode * pnode;
    if (pdata_pic == NULL) {
        return jnx_gw_mgmt_get_first_data_sesn();
    }
    if ((pnode = patricia_find_next(&jnx_gw_mgmt.data_sesn_db, 
                                    &pdata_pic->pic_node))) {
        return jnx_gw_mgmt_data_sesn_entry(pnode);
    }
    return NULL;
}

/****************************************************************************
 *                                                                          *
 *               DATA PIC SESSION ADD/DELETE ROUTINES                       *
 *                                                                          *
 ****************************************************************************/
/**
 *
 * This function adds a data pic session
 * entry, when the data pic comes up,
 * and the data agent on the pic sends
 * the register message
 *
 * @params  psesn       pconn server session entry
 * @params  pic_name    control pic ifd name
 * @returns pdata_pic   if the add successful
 *          NULL        otherwise
 */
jnx_gw_mgmt_data_session_t * 
jnx_gw_mgmt_init_data_conn(pconn_session_t * psession, char * pic_name)
{
    pconn_peer_info_t peer_info;
    pconn_client_params_t params;
    jnx_gw_mgmt_data_session_t * pdata_pic;

    if (pconn_session_get_peer_info(psession, &peer_info) != PCONN_OK) {
        return NULL;
    }

    if ((pdata_pic = JNX_GW_MALLOC(JNX_GW_MGMT_ID,
                                   sizeof(*pdata_pic))) == NULL) {
        jnx_gw_mgmt_log(JNX_GATEWAY_ALLOC, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_ERR, "Data agent \"%s\" %s failed",
                        pic_name, "malloc");
        return NULL;
    }

    pdata_pic->psession   = psession;
    pdata_pic->peer_type  = peer_info.ppi_peer_type; 
    pdata_pic->fpc_id     = peer_info.ppi_fpc_slot;
    pdata_pic->pic_id     = peer_info.ppi_pic_slot;
    pdata_pic->ifd_id     = jnx_gw_ifd_get_id(pic_name);
    pdata_pic->pic_status = JNX_GW_MGMT_STATUS_INIT;
    strncpy(pdata_pic->pic_name, pic_name, sizeof(pdata_pic->pic_name));

    /* init the key length */
    patricia_node_init_length(&pdata_pic->pic_node,
                              strlen(pdata_pic->pic_name) + 1);
    /* add to the tree */
    if (!patricia_add(&jnx_gw_mgmt.data_sesn_db, &pdata_pic->pic_node)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG, "Data agent \"%s\" patricia %s failed",
                        pic_name, "add");
        /* do trace log */
        JNX_GW_FREE(JNX_GW_MGMT_ID, pdata_pic);
        return NULL;
    }

    memset(&params, 0, sizeof(pconn_client_params_t));
    params.pconn_port       = JNX_GW_DATA_PORT;
    params.pconn_peer_info.ppi_peer_type = pdata_pic->peer_type; 
    params.pconn_peer_info.ppi_fpc_slot  = pdata_pic->fpc_id;
    params.pconn_peer_info.ppi_pic_slot  = pdata_pic->pic_id;

    if (!(pdata_pic->opcmd_conn = pconn_client_connect(&params))) {

        jnx_gw_mgmt_log(JNX_GATEWAY_CONN, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_ERR, "Data agent \"%s\" %s failed",
                        pic_name, "client connect");

        /* do trace log */
        if (!patricia_delete(&jnx_gw_mgmt.data_sesn_db, &pdata_pic->pic_node)) {
            jnx_gw_mgmt_log(JNX_GATEWAY_PATRICIA, JNX_GATEWAY_TRACEFLAG_ERR,
                            LOG_EMERG, "Data agent \"%s\" patricia %s failed",
                            pic_name, "delete");
        }
        JNX_GW_FREE(JNX_GW_MGMT_ID, pdata_pic);
        return NULL;
    }

    /* set the session cookie */
    pconn_server_set_session_cookie(psession, pdata_pic);

    /* assign it to a control pic agent */
    jnx_gw_mgmt_send_ctrl_pic_config(NULL, pdata_pic, JNX_GW_CONFIG_ADD);

    /* send the static gre tunnel configuration to the data agent */
    jnx_gw_mgmt_send_data_config(pdata_pic, JNX_GW_CONFIG_ADD);

    jnx_gw_mgmt.data_sesn_count++;

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_INFO, "Data agent \"%s\" %s", pic_name, "add");
    /* do success trace log */
    return pdata_pic;
}

/**
 *
 * This function deletes a data pic session
 * entry, when the data pic goes down,
 * this is indicated either by the pconn server
 * session hutdown message, or the ifd delete event
 * from kcom
 * it deletes the data pic from the assigned
 * control pic data pic list 
 *
 * @params  pdata_pic    data pic session entry
 */
status_t
jnx_gw_mgmt_destroy_data_conn(jnx_gw_mgmt_data_session_t * pdata_pic)
{

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_INFO, "Data agent \"%s\" %s",
                    pdata_pic->pic_name, "delete");

    /* close the client connection */
    if (pdata_pic->psession) {
        pconn_session_close(pdata_pic->psession);
        pdata_pic->psession = NULL;
    }

    if (pdata_pic->opcmd_conn) {
        pconn_client_close(pdata_pic->opcmd_conn);
        pdata_pic->opcmd_conn = NULL;
    }

    /* delete from the data pic db & free the resource */
    patricia_delete(&jnx_gw_mgmt.data_sesn_db, &pdata_pic->pic_node); 
    jnx_gw_mgmt.data_sesn_count--;

    /* TBD: clean up the data agent resources */
    jnx_gw_mgmt_send_ctrl_pic_config(pdata_pic->pctrl_pic,
                                     pdata_pic, JNX_GW_CONFIG_DELETE);

    JNX_GW_FREE(JNX_GW_MGMT_ID, pdata_pic);
    return EOK;
}

/****************************************************************************
 *                                                                          *
 *               DATA PIC SESSION EVENT/MESSAGE HANDLER ROUTINES            *
 *                                                                          *
 ****************************************************************************/
/**
 *
 * This function handles the pconn server
 * session menagement for the data 
 * pic agent,
 * on  event established, means a new data
 * pic agent has come up, creates the data
 * pic session entry
 * on event shutdown/fail, cleans up the 
 * data pic session entry
 *
 * @params  session    pconn session entry
 * @params  event      pconn event
 * @params  cookie     unused
 */
void
jnx_gw_mgmt_data_event_handler(pconn_session_t * session,
                               pconn_event_t event, void * cookie __unused)
{
    jnx_gw_mgmt_data_session_t * pdata_pic;
    char pic_name[JNX_GW_STR_SIZE];

    jnx_gw_mgmt_get_pconn_pic_name(session, pic_name);
    pdata_pic = jnx_gw_mgmt_data_sesn_lookup(pic_name);

    if (jnx_gw_mgmt.status == JNX_GW_MGMT_STATUS_INIT) {
        jnx_gw_mgmt_config_read(JNX_GW_CONFIG_READ_FORCE);
    }

    switch (event) {
        case PCONN_EVENT_ESTABLISHED:
            /* data pic session entry exists, return */
            jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                            LOG_INFO, "Data Agent \"%s\" connect received",
                            pic_name);
            if (pdata_pic) break;
            jnx_gw_mgmt_init_data_conn(session, pic_name);
            break;

        case PCONN_EVENT_SHUTDOWN:
        case PCONN_EVENT_FAILED:
            /* data pic session entry does not exist, return */
            jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                            LOG_INFO, "Data Agent \"%s\" shutdown received",
                            pic_name);
            if (pdata_pic == NULL) break;
            pdata_pic->psession = NULL;
            jnx_gw_mgmt_destroy_data_conn(pdata_pic);
            break;

        default:
            jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                            LOG_WARNING, "Data Agent \"%s\" "
                            "unknown event(%d) received",
                   pic_name, event);
            return;
    }

    /* here do an explicit scan ifls/rtbs to be update
     * with the ifl/rtb states for the box
     */
    junos_kcom_ifl_get_all(jnx_gw_mgmt_get_ifl, NULL, NULL);
    junos_kcom_rtb_get_all(jnx_gw_mgmt_get_rtb, AF_INET, NULL);

    return;
}

/**
 *
 * This function handles the messages from the 
 * data pic agent, this is for periodic
 * status update messages imitted by the data
 * pic agents for session status information
 *
 * @params  session     pconn server session entry
 * @params  msg         message pointer
 * @params  cookie      cookie
 * @return  status      
 *            EOK       if data pic agent found
 *            EFAIL     otherwise
 */
status_t
jnx_gw_mgmt_data_msg_handler(pconn_session_t * session __unused,
                             ipc_msg_t * ipc_msg,
                             void * cookie)
{
    char pic_name[JNX_GW_STR_SIZE];
    jnx_gw_msg_header_t        *pmsg = NULL;
    jnx_gw_msg_sub_header_t    *sub_hdr = NULL;
    jnx_gw_periodic_stat_t     *stat_p = NULL;
    jnx_gw_mgmt_data_session_t *pdata_pic;

    /* return incase, the session does not exist, is not in up state
     *  or, the message type is not valid
     */
    
    pdata_pic = (typeof(pdata_pic))cookie;
    jnx_gw_mgmt_get_pconn_pic_name (session, pic_name);

    if (!strlen(pic_name)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                        LOG_INFO, "Invalid data agent messge received");
        return EOK;
    }

    if (!pdata_pic) {
        if ((pdata_pic = jnx_gw_mgmt_data_sesn_lookup(pic_name)) == NULL) {
            jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                            LOG_DEBUG, 
                            "Unknown data agent \"%s\" message received", 
                   pic_name);
            return EOK;
        }
    }

    if (ipc_msg->subtype != JNX_GW_STAT_PERIODIC_MSG) {
        jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                        LOG_DEBUG, 
                        "Data agent \"%s\" unknown message(%d) received",
                        pic_name, ipc_msg->type);
        return EOK;
    }

    if (pdata_pic->pic_status != JNX_GW_MGMT_STATUS_UP) {
        jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                        LOG_DEBUG, "Data agent \"%s\" (DOWN) Hello received",
                        pic_name);
    }

    pmsg = (typeof(pmsg))(ipc_msg->data);
    sub_hdr = (typeof(sub_hdr))((char *)pmsg + sizeof(*pmsg));
    stat_p  = (typeof(stat_p))((char *)sub_hdr + sizeof(*sub_hdr));

    /* session info for the whole data pic */
    pdata_pic->num_sesions     = ntohl(stat_p->total_sessions);
    pdata_pic->active_sessions = ntohl(stat_p->active_sessions);
    pdata_pic->rx_bytes        = ntohl(stat_p->bytes_in);
    pdata_pic->tx_bytes        = ntohl(stat_p->bytes_out);
    pdata_pic->rx_pkts         = ntohl(stat_p->packets_in);
    pdata_pic->tx_pkts         = ntohl(stat_p->packets_out);

    /* reset the data agent hello missed counter */
    pdata_pic->tstamp = 0;

    jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                    LOG_DEBUG, "Data agent \"%s\" Hello received", pic_name);
    return EOK;
}

/****************************************************************************
 *                                                                          *
 *               DATA PIC SESSION SEND/RECEIVE HANDLER ROUTINES             *
 *                                                                          *
 ****************************************************************************/
/**
  * This function handles the operational command responses
  * from the data pic session agent
  * when the pdata_pic entry is NULL, it means try to
  * receive from all the data pic sessions
  * @param  pdata_pic  - data pic session entry
  *          NULL      - do it for all the data pic sesions
  * @returns msg       - message received from the data pic session
  *          NULL      - if none
  */

void * 
jnx_gw_mgmt_data_recv_opcmd(jnx_gw_mgmt_data_session_t * pdata_pic)
{
    void * pmsg;
    if (pdata_pic) {
        if (pdata_pic->opcmd_conn) {
            return pconn_client_recv(pdata_pic->opcmd_conn);
        }
        return NULL;
    }

    /* if the pdata_pic entry is NULL, we need to receive from
       all active data agents */
    while ((pdata_pic = jnx_gw_mgmt_get_next_data_sesn(pdata_pic)) != NULL) {
        if (!pdata_pic->opcmd_conn) continue;
        if ((pmsg = pconn_client_recv(pdata_pic->opcmd_conn))) {
            return pmsg;
        }
    }
    return NULL;
}

/**
 * This function sends the operational command request
 * message to the data pic session agents
 * when the pdata_pic entry is NULL, it means try to
 * send to all the data pic sessions
 * @param  pdata_pic  - data pic session entry
 *         NULL       - do it for all the data pic sesion entries
 * @returns status 
 *         EOK      - message delivered
 *         EFAIL    - message send failed
 */
status_t 
jnx_gw_mgmt_data_send_opcmd(jnx_gw_mgmt_data_session_t * pdata_pic,
                            int32_t type, void * msg, int16_t len)
{
    if (pdata_pic) {
        if (pdata_pic->opcmd_conn) {
            return pconn_client_send(pdata_pic->opcmd_conn, type, msg, len);
        }
        return EOK;
    }

    /* if the pdata_pic entry is NULL, we need to send the message
        to all active control agents */
    while ((pdata_pic = jnx_gw_mgmt_get_next_data_sesn(pdata_pic)) != NULL) {
        if (!pdata_pic->opcmd_conn) continue;
        pconn_client_send(pdata_pic->opcmd_conn, type, msg, len);
    }
    return EOK;
}

/**
  * This function sends a message to the data pic agent
  * through the server session entry 
  * if the data pic session entry is null, sends to all
  * the data pics
  * @params    pdata_pic    data pic session entry
  * @params    type         message type
  * @params    msg          message buffer
  * @params    len          message length
  */
int32_t
jnx_gw_mgmt_send_data_msg(jnx_gw_mgmt_data_session_t *pdata_pic, int32_t type,
                          void * msg, int16_t len)
{
    if (pdata_pic) {
        if (pdata_pic->opcmd_conn) {
            return pconn_client_send(pdata_pic->opcmd_conn, type, msg, len);
        }
        return EOK;
    }

    /* if the pdata_pic entry is NULL, we need to send the message
       to all active data agents */
    while ((pdata_pic = jnx_gw_mgmt_get_next_data_sesn(pdata_pic)) != NULL) {
        if (!pdata_pic->opcmd_conn) continue;
        pconn_client_send(pdata_pic->opcmd_conn, type, msg, len);
    }
    return EOK;
}

/**
  * This function periodically called to check 
  * the status of the control agent and data agent
  * modules
  */
static void
jnx_gw_mgmt_periodic_timer_handler(evContext ctxt __unused,
                                   void * uap __unused,
                                   struct timespec due __unused,
                                   struct timespec inter __unused)
{
    jnx_gw_mgmt_ctrl_session_t * pctrl_pic = NULL, *pdel_ctrl_pic = NULL;
    jnx_gw_mgmt_data_session_t * pdata_pic = NULL, *pdel_data_pic = NULL;

    junos_trace(JNX_GATEWAY_TRACEFLAG_HELLO, "Periodic timer event");

    if (jnx_gw_mgmt.status == JNX_GW_MGMT_STATUS_INIT) {
        jnx_gw_mgmt_config_read(JNX_GW_CONFIG_READ_FORCE);
    }

    /* check out the data agents */
    while ((pdata_pic = jnx_gw_mgmt_get_next_data_sesn(pdel_data_pic))) {

        if (pdata_pic->pic_status != JNX_GW_MGMT_STATUS_UP)  {
            pdel_data_pic = pdata_pic;
            continue;
        }

        if (pdata_pic->tstamp <= JNX_GW_MGMT_HELLO_TIMEOUT) {
            pdata_pic->tstamp++;
            pdel_data_pic = pdata_pic;
            continue;
        }

        jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                        LOG_INFO, "Data agent \"%s\" Hello timeout",
                        pdata_pic->pic_name);
        jnx_gw_mgmt_destroy_data_conn(pdata_pic);
    }


    /* check out the control agents */
    while ((pctrl_pic = jnx_gw_mgmt_get_next_ctrl_sesn(pdel_ctrl_pic))) {

        if (pctrl_pic->pic_status != JNX_GW_MGMT_STATUS_UP)  {
            pdel_ctrl_pic = pctrl_pic;
            continue;
        }
        if (pctrl_pic->tstamp <= JNX_GW_MGMT_HELLO_TIMEOUT) {
            pctrl_pic->tstamp++;
            pdel_ctrl_pic = pctrl_pic;
            continue;
        }
        jnx_gw_mgmt_log(JNX_GATEWAY_HELLO, JNX_GATEWAY_TRACEFLAG_HELLO,
                        LOG_ERR, "Control agent \"%s\" hello timeout",
                        pctrl_pic->pic_name);
        jnx_gw_mgmt_destroy_ctrl_conn(pctrl_pic, FALSE);
    }

    return;
}


/****************************************************************************
 *                                                                          *
 *               PIC SESSION MANAGEMENT INIT/SHUTDOWN ROUTINES              *
 *                                                                          *
 ****************************************************************************/
/**
 * This function initializes the control data agent
 * session specific resources, opens the server
 * connection for listening to the register messages
 * from the data and control pic sessions
 *
 * @params   ctxt   Eventlib context
 *
 * @returns  status
 *           EOK    Successful
 *           EFAIL  Otherwise
 */
status_t
jnx_gw_mgmt_conn_init(evContext ctxt)
{
    pconn_server_params_t params;

    /* initialize the pic agent databases */
    jnx_gw_mgmt.ctrl_sesn_count = 0;
    jnx_gw_mgmt.data_sesn_count = 0;
    patricia_root_init (&jnx_gw_mgmt.ctrl_sesn_db, FALSE, JNX_GW_STR_SIZE,
                        offsetof(jnx_gw_mgmt_ctrl_session_t, pic_name) -
                        offsetof(jnx_gw_mgmt_ctrl_session_t, pic_node) -
                        sizeof(((jnx_gw_mgmt_ctrl_session_t *)0)->pic_node)); 

    patricia_root_init (&jnx_gw_mgmt.data_sesn_db, FALSE, JNX_GW_STR_SIZE, 
                        offsetof(jnx_gw_mgmt_data_session_t, pic_name) -
                        offsetof(jnx_gw_mgmt_data_session_t, pic_node) -
                        sizeof(((jnx_gw_mgmt_data_session_t *)0)->pic_node)); 

    /* the control app server connection for periodic hellos,
       & register messages */
    memset(&params, 0, sizeof(pconn_server_params_t));
    params.pconn_port            = JNX_GW_MGMT_CTRL_PORT;
    params.pconn_max_connections = JNX_GW_MGMT_MAX_CTRL_IDX;
    params.pconn_event_handler   = jnx_gw_mgmt_ctrl_event_handler;
    if (!(jnx_gw_mgmt.ctrl_server  = 
          pconn_server_create(&params, ctxt,
                              jnx_gw_mgmt_ctrl_msg_handler, NULL))) {
        jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_INIT,
                        LOG_EMERG, 
                        "Control Agent Server connection create failed");
        goto conn_cleanup;
    }

    /* the data app server connection for periodic hellos,
       & register messages */
    memset(&params, 0, sizeof(pconn_server_params_t));
    params.pconn_port            = JNX_GW_MGMT_DATA_PORT;
    params.pconn_max_connections = JNX_GW_MGMT_MAX_DATA_IDX;
    params.pconn_event_handler   = jnx_gw_mgmt_data_event_handler;
    if (!(jnx_gw_mgmt.data_server = 
          pconn_server_create(&params, ctxt, 
                              jnx_gw_mgmt_data_msg_handler, NULL))) {
        jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG,
                        "Data Agent Server connection create failed");
        goto conn_cleanup;
    }

    /* schedule the periodic timer routine to check the health of
     * control and data agents 
     */
    if (evSetTimer(ctxt, jnx_gw_mgmt_periodic_timer_handler,
                   NULL, evNowTime(),
                   evConsTime((JNX_GW_MGMT_PERIODIC_SEC), 0),
                   &jnx_gw_mgmt.periodic_timer_id)
        < 0) {
        jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_ERR,
                        LOG_EMERG, "Periodic Hello Timer event create failed");
        goto conn_cleanup;
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_INIT, 
                    LOG_INFO, "Connection Module Initialization done");
    return EOK;

conn_cleanup:

    if (jnx_gw_mgmt.ctrl_server) {
        pconn_server_shutdown(jnx_gw_mgmt.ctrl_server);
        jnx_gw_mgmt.ctrl_server = NULL;
    }

    if (jnx_gw_mgmt.data_server) {
        pconn_server_shutdown(jnx_gw_mgmt.data_server);
        jnx_gw_mgmt.data_server = NULL;
    }

    if  (jnx_gw_mgmt.periodic_timer_id.opaque) {
        evClearTimer(jnx_gw_mgmt.ctxt, 
                     jnx_gw_mgmt.periodic_timer_id);
        jnx_gw_mgmt.periodic_timer_id.opaque = NULL;
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_INIT,
                    LOG_ERR, "Connection Module Initialization failed");
    return EFAIL;
}

/**
 * This function clears the management plance
 * connection control module
 * @params   ctxt   Eventlib context
 *
 * @returns  status
 *           EOK    Successful
 *           EFAIL  Otherwise
 */
status_t
jnx_gw_mgmt_conn_cleanup(evContext ctxt __unused)
{
    jnx_gw_mgmt_data_session_t * pdata_pic = NULL;
    jnx_gw_mgmt_ctrl_session_t * pctrl_pic = NULL;

    /* clean up the data agents */
    while ((pdata_pic = jnx_gw_mgmt_get_first_data_sesn())) {
        jnx_gw_mgmt_destroy_data_conn(pdata_pic);
    }

    /* clean up the control agents */
    while ((pctrl_pic = jnx_gw_mgmt_get_first_ctrl_sesn())) {
        jnx_gw_mgmt_destroy_ctrl_conn(pctrl_pic, TRUE);
    }

    /* clear the pconn server connections */
    pconn_server_shutdown(jnx_gw_mgmt.ctrl_server);
    pconn_server_shutdown(jnx_gw_mgmt.data_server);

    jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_INIT,
                    LOG_INFO, "Connection Module destroy done");
    return EOK;
}

/*
 * $Id: jnx-flow-mgmt_conn.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-flow-mgmt_conn.c - connection management with data & control agents
 *
 * Vivek Gupta, Aug 2007
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

/**
 * @file jnx-flow-mgmt_conn.c
 * @brief
 * This file contains the connection handler routines for the
 * data agent application running on the ms-pics
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

#include <ddl/dax.h>
#include <syslog.h>
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
#include JNX_FLOW_MGMT_OUT_H

#include <jnx/jnx-flow.h>
#include <jnx/jnx-flow_msg.h>

#include "jnx-flow-mgmt.h"
#include "jnx-flow-mgmt_config.h"

jnx_flow_mgmt_data_session_t * 
jnx_flow_mgmt_init_data_conn(pconn_session_t * session, char * pic_name);

status_t jnx_flow_mgmt_destroy_data_conn(jnx_flow_mgmt_data_session_t *
                                         pdata_pic);

/**
 * get the client peer pic name
 */
static int
jnx_flow_mgmt_get_pconn_pic_name (pconn_session_t * session, char *pic_name)
{
    pconn_peer_info_t peer_info;

    if (pconn_session_get_peer_info(session, &peer_info) == PCONN_OK) {
        if (peer_info.ppi_peer_type == PCONN_PEER_TYPE_RE) {
            strncpy(pic_name, PCONN_HOSTNAME_RE, sizeof(pic_name));
        } else {
            sprintf(pic_name, "ms-%d/%d/0", peer_info.ppi_fpc_slot,
                    peer_info.ppi_pic_slot);
        }
    } else {
        strncpy(pic_name, PCONN_HOSTNAME_RE, sizeof(pic_name));
    }
    return EOK;
}
/**
 * This function returns the data pic session entry
 * for the pic name
 *
 * @params  pic_name     pic ifd name
 * @returns pdata_sesn   data pic session entry
 *          NULL         if none
 */
jnx_flow_mgmt_data_session_t *
jnx_flow_mgmt_data_sesn_lookup (char * pic_name)
{
    patnode *pnode;

    if (pic_name) {
        if ((pnode = patricia_get(&jnx_flow_mgmt.data_sesn_db,
                                  strlen(pic_name) + 1, pic_name))) {
            return (jnx_flow_mgmt_data_session_t*)(pnode);
        }
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
jnx_flow_mgmt_data_session_t * 
jnx_flow_mgmt_init_data_conn(pconn_session_t * psession, char * pic_name)
{
    pconn_peer_info_t peer_info;
    pconn_client_params_t params;
    jnx_flow_mgmt_data_session_t * pdata_pic;

    jnx_flow_log(JNX_FLOW_CONN, LOG_INFO,"init data connection %s",
                 pic_name);

    memset (&params, 0, sizeof(pconn_client_params_t));

    if (pconn_session_get_peer_info(psession, &peer_info) != PCONN_OK) {
        pconn_session_close(psession);
        jnx_flow_log(JNX_FLOW_CONN, LOG_INFO,"%s:%d: Closing the session",
                     __func__, __LINE__);
        return NULL;
    }

    if ((pdata_pic = calloc(1, sizeof(*pdata_pic))) == NULL) {
        pconn_session_close(psession);
        jnx_flow_log(JNX_FLOW_CONN, LOG_INFO,"init data conn alloc fail");
        return NULL;
    }

    pdata_pic->psession   = psession;
    pdata_pic->fpc_id     = peer_info.ppi_fpc_slot;
    pdata_pic->pic_id     = peer_info.ppi_pic_slot;
    pdata_pic->pic_status = JNX_FLOW_MGMT_STATUS_INIT;
    strncpy(pdata_pic->pic_name, pic_name, sizeof(pdata_pic->pic_name));

    /* init the key length */
    patricia_node_init_length(&pdata_pic->pic_node,
                              strlen(pdata_pic->pic_name) + 1);
    /* add to the tree */
    if (!patricia_add(&jnx_flow_mgmt.data_sesn_db, &pdata_pic->pic_node)) {
        pconn_session_close(psession);
        jnx_flow_log(JNX_FLOW_CONN, LOG_INFO,"init data conn patricia add failed");
        /* do trace log */
        free(pdata_pic);
        return NULL;
    }

    params.pconn_port       = JNX_FLOW_DATA_PORT;
    params.pconn_peer_info.ppi_peer_type = peer_info.ppi_peer_type;
    params.pconn_peer_info.ppi_fpc_slot  = pdata_pic->fpc_id;
    params.pconn_peer_info.ppi_pic_slot  = pdata_pic->pic_id;
    
    if (!(pdata_pic->cmd_conn = pconn_client_connect(&params))) {
        /* do trace log */
        pconn_session_close(psession);
        patricia_delete(&jnx_flow_mgmt.data_sesn_db, &pdata_pic->pic_node); 
        jnx_flow_log(JNX_FLOW_CONN, LOG_INFO,"init data conn client connect failed");
        free(pdata_pic);
        return NULL;
    }

    /* set the session cookie */
    pconn_server_set_session_cookie(psession, pdata_pic);
    jnx_flow_mgmt_send_rule_config(pdata_pic, NULL);
    jnx_flow_mgmt_send_svc_config(pdata_pic, NULL);
    jnx_flow_mgmt_send_svc_rule_config(pdata_pic, NULL, FALSE);
    jnx_flow_log(JNX_FLOW_CONN, LOG_INFO,"init data conn %s successful",pic_name);

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
jnx_flow_mgmt_destroy_data_conn(jnx_flow_mgmt_data_session_t * pdata_pic)
{

    /* close the client connection */
    if (pdata_pic->cmd_conn) {
        pconn_client_close(pdata_pic->cmd_conn);
        pdata_pic->cmd_conn = NULL;
    }
    if (pdata_pic->psession) {
        pconn_session_close(pdata_pic->psession);
        pdata_pic->psession = NULL;
    }

    /* delete from the data pic db & free the resource */
    patricia_delete(&jnx_flow_mgmt.data_sesn_db, &pdata_pic->pic_node); 
    free(pdata_pic);
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
static void
jnx_flow_mgmt_data_event_handler(pconn_session_t * session,
                               pconn_event_t event, void * cookie __unused)
{
    jnx_flow_mgmt_data_session_t * pdata_pic;
    char pic_name[JNX_FLOW_STR_SIZE];

    jnx_flow_mgmt_get_pconn_pic_name(session, pic_name);
    pdata_pic = jnx_flow_mgmt_data_sesn_lookup(pic_name);

    switch (event) {
        case PCONN_EVENT_ESTABLISHED:
            /* data pic session entry exists, return */
            jnx_flow_log(JNX_FLOW_CONN, LOG_INFO,"data agent %s connect event",
                         pic_name);
            if (pdata_pic) {
                jnx_flow_mgmt_destroy_data_conn(pdata_pic);
            }
            jnx_flow_mgmt_init_data_conn(session, pic_name);
            break;

        case PCONN_EVENT_SHUTDOWN:
            /* data pic session entry does not exist, return */
            jnx_flow_log(JNX_FLOW_CONN, LOG_INFO,"data agent %s shutdown event",
                         pic_name);
            if (pdata_pic == NULL) break;
            pdata_pic->psession = NULL;
            jnx_flow_mgmt_destroy_data_conn(pdata_pic);
            break;

        case PCONN_EVENT_FAILED:
            /* data pic session entry does not exist, return */
            jnx_flow_log(JNX_FLOW_CONN, LOG_INFO,"data agent %s connect fail event",
                         pic_name);
            if (pdata_pic == NULL) break;
            pdata_pic->psession = NULL;
            jnx_flow_mgmt_destroy_data_conn(pdata_pic);
            break;

        default:
            jnx_flow_log(JNX_FLOW_CONN,LOG_INFO,"data agent %s unknown event",
                         pic_name);
            break;
    }

    return;
}


/**
 *
 * This function sends the configuration messages to the 
 * data application agents running on the ms-pic
 */
status_t
jnx_flow_mgmt_send_config_msg(jnx_flow_mgmt_data_session_t * pdata_pic, 
                              void * msg_hdr, uint32_t msg_type,
                              uint32_t msg_len)
{
    if (pdata_pic) {
        if (pdata_pic->cmd_conn) {
            return pconn_client_send(pdata_pic->cmd_conn, msg_type,
                                     msg_hdr, msg_len); 
        }
        return EFAIL;
    }

    for (pdata_pic =
        (typeof(pdata_pic))patricia_find_next(&jnx_flow_mgmt.data_sesn_db, NULL);
         pdata_pic != NULL;
         pdata_pic = (typeof(pdata_pic))
             patricia_find_next(&jnx_flow_mgmt.data_sesn_db, &pdata_pic->pic_node)) {

        if (pdata_pic->cmd_conn) {
            pconn_client_send(pdata_pic->cmd_conn, msg_type,
                              msg_hdr, msg_len); 
        }
    }
    return EOK;
}

/**
 *
 * This function sends the operational command messages to the 
 * data application agents running on the ms-pic
 */
jnx_flow_msg_header_info_t * 
jnx_flow_mgmt_send_opcmd_msg(jnx_flow_mgmt_data_session_t * pdata_pic, 
                             jnx_flow_msg_header_info_t *  msg_hdr, 
                             uint32_t msg_type, uint32_t msg_len)
{
    ipc_msg_t * ipc_msg = NULL;

    jnx_flow_trace(JNX_FLOW_TRACEFLAG_EVENT, "%d:%d:%d:%d",
                 msg_type, msg_hdr->msg_type,
                 msg_hdr->msg_count, msg_hdr->more);

    if ((pdata_pic == NULL) || (pdata_pic->cmd_conn == NULL)) {
        return NULL;
    }

    if  (pconn_client_send(pdata_pic->cmd_conn, msg_type,
                           msg_hdr, msg_len) == EOK) {
        ipc_msg = pconn_client_recv(pdata_pic->cmd_conn);
    }

    if (ipc_msg) {
        msg_hdr = (typeof(msg_hdr))ipc_msg->data;

        jnx_flow_trace(JNX_FLOW_TRACEFLAG_EVENT, "%p:%d:%d:%d:%d",
                     msg_hdr,
                     ntohs(ipc_msg->subtype), msg_hdr->msg_type,
                     msg_hdr->msg_count, msg_hdr->more);
        return msg_hdr;
    }

    return NULL;
}

/**
 * This function receives the operational command response messages
 * from the data application agents running on the ms-pic
 */
jnx_flow_msg_header_info_t * 
jnx_flow_mgmt_recv_opcmd_msg(jnx_flow_mgmt_data_session_t * pdata_pic)
{
    ipc_msg_t * ipc_msg = NULL;

    if ((pdata_pic == NULL) || (pdata_pic->cmd_conn == NULL)) {
        return NULL;
    }

    if ((ipc_msg = pconn_client_recv(pdata_pic->cmd_conn))) {
        return (typeof(jnx_flow_msg_header_info_t *))ipc_msg->data;
    }
    return NULL;
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
static status_t
jnx_flow_mgmt_data_msg_handler(pconn_session_t * session __unused,
                             ipc_msg_t * ipc_msg __unused,
                             void * cookie __unused)
{
    return EOK;
}

/****************************************************************************
 *                                                                          *
 *               DATA PIC SESSION SEND/RECEIVE HANDLER ROUTINES             *
 *                                                                          *
 ****************************************************************************/

/**
 * This function creates a server socket handler for receiving
 * the registration connection events from the data application
 * agents running on the ms-pic
 */
status_t
jnx_flow_mgmt_conn_init(evContext ctxt)
{
    pconn_server_params_t params;

    /* initialize the pic agent databases */

    patricia_root_init (&jnx_flow_mgmt.data_sesn_db, FALSE, JNX_FLOW_STR_SIZE,0); 

    /* server connection for the data app periodic hellos & register messages */
    memset(&params, 0, sizeof(pconn_server_params_t));
    params.pconn_port            = JNX_FLOW_MGMT_PORT;
    params.pconn_max_connections = JNX_FLOW_MGMT_MAX_DATA_IDX;
    params.pconn_event_handler   = jnx_flow_mgmt_data_event_handler;

    if (!(jnx_flow_mgmt.data_server = 
          pconn_server_create(&params, ctxt, 
                              jnx_flow_mgmt_data_msg_handler, NULL))) {
        return EFAIL;
    }

    return EOK;
}

/**
 * This function clears the management plane
 * connection 
 * @params   ctxt   Eventlib context
 *
 * @returns  status
 *           EOK    Successful
 *           EFAIL  Otherwise
 */
status_t
jnx_flow_mgmt_conn_cleanup(evContext ctxt __unused)
{
    pconn_server_shutdown (jnx_flow_mgmt.data_server);
    return EOK;
}

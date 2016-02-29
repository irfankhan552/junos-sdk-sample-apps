/*
 * $Id: jnx-gateway-ctrl_gre.c 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-ctrl_gre.c - gre gateway connection signaling routines
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

/** 
 * @file jnx-gateway-ctrl_gre.c
 * @brief
 * This file contains gre session signaling
 * management routines with gre gateways
 * the control module listens to the gre
 * signaling messages per vrf, on separate
 * threads,
 * when it receives a session init message 
 * from a gre gateway, creates the gateway entry
 * if not present,
 * validates the session, creates a session entry
 * selects a data pic forwards the request to the
 * data pic, 
 * on receipt of a session termination request
 * marks the session entry as down, & forwards
 * the request to the data pic
 * 
 */

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>

#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include <jnx/pconn.h>

/* include the comman header files for
   the sample gateway application */
#include <jnx/jnx-gateway.h>
#include <jnx/jnx-gateway_msg.h>

/* include control agent header file */
#include "jnx-gateway-ctrl.h"


PATNODE_TO_STRUCT(jnx_gw_ctrl_thread_vrf_entry,
                  jnx_gw_ctrl_vrf_t, vrf_tnode)

/***********************************************************
 *                                                         *
 *        LOCAL FUNCTION PROTOTYPE DEFINITIONS             *
 *                                                         *
 ***********************************************************/

status_t
jnx_gw_ctrl_gre_gw_msg_handler(jnx_gw_ctrl_vrf_t * pvrf,
                               jnx_gw_ctrl_gre_gw_t * pgre_gw,
                               void * pmsg, uint32_t msg_len);
status_t 
jnx_gw_ctrl_send_gw_gre_msg(jnx_gw_ctrl_vrf_t * pvrf);

status_t
jnx_gw_ctrl_sig_gre_session_delete(jnx_gw_ctrl_vrf_t *pvrf,
                                jnx_gw_ctrl_gre_gw_t  *pgre_gw, 
                                jnx_gw_ctrl_gre_msg_t *pmsg);
status_t
jnx_gw_ctrl_sig_gre_session_add(jnx_gw_ctrl_vrf_t     *pvrf,
                             jnx_gw_ctrl_gre_gw_t  *pgre_gw, 
                             jnx_gw_ctrl_gre_msg_t *pmsg);
status_t 
jnx_gw_ctrl_generate_gre_key(jnx_gw_ctrl_vrf_t * pvrf,
                             jnx_gw_ctrl_gre_gw_t * pgre_gw,
                             jnx_gw_ctrl_gre_session_t * pgre_session);

jnx_gw_ctrl_buf_t * jnx_gw_ctrl_get_buf(void);
void jnx_gw_ctrl_release_buf(jnx_gw_ctrl_buf_t * pbuf);
jnx_gw_ctrl_proc_thread_t * jnx_gw_ctrl_queue_buf(jnx_gw_ctrl_buf_t * pbuf);
jnx_gw_ctrl_buf_t * jnx_gw_ctrl_dequeue_buf(jnx_gw_ctrl_buf_list_t *pqueue);
jnx_gw_ctrl_sock_list_t * jnx_gw_ctrl_add_fdlist(jnx_gw_ctrl_rx_thread_t * pthread);
jnx_gw_ctrl_sock_list_t * jnx_gw_ctrl_find_fdmin(jnx_gw_ctrl_rx_thread_t * pthread);
void jnx_gw_ctrl_thread_handle_vrf_events(jnx_gw_ctrl_rx_thread_t * prx_thread);
list_t * jnx_gw_ctrl_get_list_entry(jnx_gw_ctrl_rx_thread_t * prx_thread);
void jnx_gw_ctrl_release_list_entry(jnx_gw_ctrl_rx_thread_t * prx_thread,
                                    list_t * plist);
void
jnx_gw_ctrl_clear_proc_event_pending(jnx_gw_ctrl_rx_thread_t * prx_thread);

/**
 * This function returns a gre key for a new gre session 
 * @params pvrf         vrf structure pointer
 * @params pgre_gw      gre gateway structure pointer
 * @params pgre_session    gre session structure pointer
 */
status_t 
jnx_gw_ctrl_generate_gre_key(jnx_gw_ctrl_vrf_t * pvrf,
                             jnx_gw_ctrl_gre_gw_t * pgre_gw,
                             jnx_gw_ctrl_gre_session_t * pgre_session)
{
    jnx_gw_ctrl_gre_session_t * psesn = NULL;

    JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pvrf);

    /* can not have more than this active sessions */
    if (pvrf->gre_active_sesn_count == pvrf->vrf_max_gre_sesn) {
        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pvrf);
        jnx_gw_log(LOG_DEBUG, "GRE Session setup GRE KEY alloc failed");
        return EFAIL;
    }

    /* try finding a gap, if the current gre key is still in use */
    if ((psesn = jnx_gw_ctrl_lookup_gre_session(pvrf, pgre_gw,
                                             pvrf->vrf_gre_key_cur, FALSE))) {

        /* we will definitely find an entry */
        while ((psesn = jnx_gw_ctrl_get_next_gre_session(pvrf, NULL, NULL,
                                                         NULL, psesn, FALSE))) {
            /* find a gap */
            pvrf->vrf_gre_key_cur++;

            if (psesn->ingress_gre_key > pvrf->vrf_gre_key_cur) {

                pgre_session->ingress_gre_key = pvrf->vrf_gre_key_cur;

                pvrf->vrf_gre_key_cur++;

                if (pvrf->vrf_gre_key_cur == pvrf->vrf_gre_key_end) {
                    pvrf->vrf_gre_key_cur = pvrf->vrf_gre_key_start;
                }

                JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pvrf);
                return EOK;
            }
            /*
             * wrap around
             */
            if (pvrf->vrf_gre_key_cur == pvrf->vrf_gre_key_end) {
                pvrf->vrf_gre_key_cur = pvrf->vrf_gre_key_start;
                psesn = NULL;
            }
        }
    }

    /* get the gre key */
    pgre_session->ingress_gre_key = pvrf->vrf_gre_key_cur;

    /* wrap around the cur gre key */
    if (pvrf->vrf_gre_key_cur == pvrf->vrf_gre_key_end) {
        pvrf->vrf_gre_key_cur = pvrf->vrf_gre_key_start;
    } else
        pvrf->vrf_gre_key_cur++;

    JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pvrf);
    return EOK;
}

/**
 * This function handles a new gre session add message
 * from the gre gateway
 * @params pvrf         vrf structure pointer
 * @params pgre_gw      gre gateway structure pointer
 * @params pmsg         gre message pointer
 */
status_t
jnx_gw_ctrl_sig_gre_session_add(jnx_gw_ctrl_vrf_t     *pvrf,
                             jnx_gw_ctrl_gre_gw_t  *pgre_gw, 
                             jnx_gw_ctrl_gre_msg_t *pmsg)
{
    jnx_gw_ctrl_gre_session_t * pgre_session, gre_config;
    jnx_gw_ctrl_clnt_5t_info_t * puser_info;

    jnx_gw_log(LOG_DEBUG, "GRE Session setup from %s", 
               JNX_GW_IP_ADDRA(pgre_gw->gre_gw_ip));

    /* get the user information from the init request message */
    puser_info = &pmsg->user_5t;

    /* memset */
    memset(&gre_config, 0, sizeof(gre_config));

    gre_config.sesn_msgid     = ntohl(pmsg->msg_id);
    gre_config.sesn_flags     = pmsg->flags;
    gre_config.sesn_proto     = puser_info->proto;
    gre_config.sesn_dport     = ntohs(puser_info->dst_port);
    gre_config.sesn_sport     = ntohs(puser_info->src_port);
    gre_config.sesn_server_ip = ntohl(puser_info->dst_ip);
    gre_config.sesn_client_ip = ntohl(puser_info->src_ip);

    gre_config.ingress_gw_ip  = pgre_gw->gre_gw_ip;
    gre_config.ingress_vrf_id = pvrf->vrf_id;

    gre_config.pingress_vrf   = pvrf;
    gre_config.pgre_gw        = pgre_gw;

    /* check whether the session exists */
    pgre_session = 
        jnx_gw_ctrl_get_gw_gre_session(pvrf, pgre_gw,
                                       (jnx_gw_ctrl_session_info_t *)
                                       &gre_config.sesn_proto);

    if (pgre_session) {

        if (pgre_session->sesn_msgid == gre_config.sesn_msgid) {
            jnx_gw_log(LOG_DEBUG, "GRE session setup, response pending %d",
                       pgre_session->ingress_gre_key);
            return EOK;
        }

        /* if fail, send error message  & return */
        jnx_gw_log(LOG_DEBUG, "GRE session setup, already present");
        gre_config.sesn_errcode = JNX_GW_MSG_ERR_GRE_SESS_EXISTS;

        goto sesn_add_fail;
    }

    /* match  user profile */
    if (jnx_gw_ctrl_match_user(&gre_config) == EFAIL) {

        jnx_gw_log(LOG_DEBUG, "GRE session setup, user match failed");

        /* if fail, send error message  & return */
        gre_config.sesn_errcode = JNX_GW_MSG_ERR_SESSION_AUTH_FAIL;
        goto sesn_add_fail;
    }

    /* select a data pic*/
    jnx_gw_ctrl_select_data_pic(&gre_config);

    if (!gre_config.pdata_pic) {
        jnx_gw_log(LOG_DEBUG, "GRE session setup, data agent select failed");
        /* if fail, send error message  & return */
        gre_config.sesn_errcode = JNX_GW_MSG_ERR_RESOURCE_UNAVAIL;
        goto sesn_add_fail;
    }

    /* would have already set the interfaces also */

    /* generate a gre key */
    if (jnx_gw_ctrl_generate_gre_key(pvrf, pgre_gw, &gre_config) == EFAIL) {

        /* if fail, send error message  & return */
        gre_config.sesn_errcode = JNX_GW_MSG_ERR_RESOURCE_UNAVAIL;
        goto sesn_add_fail;
    }

    /* create a new session entry for the gre session, */
    if (!(pgre_session = 
          jnx_gw_ctrl_add_gre_session(pvrf, pgre_gw, &gre_config))) {

        jnx_gw_log(LOG_DEBUG, "GRE Session add failed");
        /* if fail, send error message  & return */
        gre_config.sesn_errcode = JNX_GW_MSG_ERR_RESOURCE_UNAVAIL;

        goto sesn_add_fail;
    }

    jnx_gw_log(LOG_DEBUG, "GRE Session mux add (%d) to data agent \"%s\"",
               gre_config.ingress_gre_key, gre_config.pdata_pic->pic_name);

    /* fill the session init request to the data pic */
    jnx_gw_ctrl_fill_data_pic_msg(JNX_GW_GRE_SESSION_MSG,
                                  JNX_GW_ADD_GRE_SESSION,
                                  pgre_session->pdata_pic, pgre_session);

    return EOK;

sesn_add_fail:
    jnx_gw_log(LOG_DEBUG, "GRE Session setup failed for %s", 
               JNX_GW_IP_ADDRA(pgre_gw->gre_gw_ip));
    /* send the failure message */
    jnx_gw_ctrl_fill_gw_gre_msg(JNX_GW_CTRL_GRE_SESN_ERR,
                                pvrf, pgre_gw,&gre_config);
    return EFAIL;
}

/**
 * This function handles a new gre session delete message
 * from the gre gateway
 * @params pvrf         vrf structure pointer
 * @params pgre_gw      gre gateway structure pointer
 * @params pmsg         gre message pointer
 */
status_t
jnx_gw_ctrl_sig_gre_session_delete(jnx_gw_ctrl_vrf_t *pvrf,
                                jnx_gw_ctrl_gre_gw_t  *pgre_gw, 
                                jnx_gw_ctrl_gre_msg_t *pmsg)
{
    uint32_t gre_key;
    jnx_gw_ctrl_data_pic_t * pdata_pic;
    jnx_gw_ctrl_gre_session_t gre_config, * pgre_session;
    jnx_gw_ctrl_clnt_5t_info_t * puser_info;

    /* get the GRE Key */
    gre_key = ntohl(pmsg->tun.gre_key);

    jnx_gw_log(LOG_DEBUG, "GRE Session delete %d from %s", gre_key,
               JNX_GW_IP_ADDRA(pgre_gw->gre_gw_ip));

    /* get the user information from the init request message */
    puser_info = &pmsg->user_5t;

    if (!(pgre_session = jnx_gw_ctrl_lookup_gre_session(pvrf, pgre_gw,
                                                        gre_key, TRUE))) {

        /* send the failure message */
        gre_config.sesn_msgid      = ntohl(pmsg->msg_id);
        gre_config.ingress_gre_key = ntohl(pmsg->tun.gre_key);
        gre_config.sesn_status     = JNX_GW_CTRL_STATUS_FAIL;
        gre_config.sesn_errcode    = JNX_GW_MSG_ERR_GRE_SESS_NOT_EXIST;
        gre_config.sesn_proto      = puser_info->proto;
        gre_config.sesn_dport      = ntohs(puser_info->dst_port);
        gre_config.sesn_sport      = ntohs(puser_info->src_port);
        gre_config.sesn_server_ip  = ntohl(puser_info->dst_ip);
        gre_config.sesn_client_ip  = ntohl(puser_info->src_ip);
        gre_config.ingress_gw_ip   = pgre_gw->gre_gw_ip;
        gre_config.ingress_vrf_id  = pvrf->vrf_id;
        gre_config.pingress_vrf    = pvrf;
        gre_config.pgre_gw         = pgre_gw;

        jnx_gw_ctrl_fill_gw_gre_msg(JNX_GW_CTRL_GRE_SESN_ERR,
                                    pvrf, pgre_gw, &gre_config);

        /* no need to intimate any data pic,
           replay back to the client gateway */
        jnx_gw_log(LOG_INFO, "GRE Session %d delete, does not exist", gre_key);
        return EFAIL;
    }

    if ((pgre_session->sesn_msgid == ntohl(pmsg->msg_id)) ||
        (pgre_session->sesn_status == JNX_GW_CTRL_STATUS_DOWN)) {
        jnx_gw_log(LOG_DEBUG, "GRE session %d delete, response pending %d", 
                   gre_key, pgre_session->sesn_msgid);
        return EOK;
    }

    /* set the status as down */
    pgre_session->sesn_msgid  = ntohl(pmsg->msg_id);
    pgre_session->sesn_status = JNX_GW_CTRL_STATUS_DOWN;

    pdata_pic = pgre_session->pdata_pic;

    /* intimate the data pic agent module */
    jnx_gw_ctrl_fill_data_pic_msg(JNX_GW_GRE_SESSION_MSG,
                                  JNX_GW_DEL_GRE_SESSION,
                                  pdata_pic, pgre_session);

    /* removal of the session entry, on receipt of 
       response from the data pic, is done in the data pic
       message handler */
    return EOK;
}

/**
 * This function fills the message to be sent to the
 * gre gateway
 * @params pvrf         vrf structure pointer
 * @params pgre_gw      gre gateway structure pointer
 * @params pgre_session    gre session structure pointer
 */
status_t 
jnx_gw_ctrl_fill_gw_gre_msg(uint8_t msg_type, jnx_gw_ctrl_vrf_t * pvrf,
                            jnx_gw_ctrl_gre_gw_t * pgre_gw,
                            jnx_gw_ctrl_gre_session_t * pgre_session)
{
    jnx_gw_ctrl_gre_msg_t *pgre_msg;


    JNX_GW_CTRL_VRF_SEND_LOCK(pvrf);

    /* push the current buffer to the gre gateway, incase
       we will overflob the mesage, or, it was for a 
       different gre gateway */

    if (((pvrf->gw_ip) && (pvrf->gw_ip != pgre_gw->gre_gw_ip)) ||
        ((pvrf->send_len + sizeof(*pgre_msg)) > JNX_GW_CTRL_MAX_PKT_BUF_SIZE)) {

        pvrf->send_sock.sin_family      = AF_INET;
        pvrf->send_sock.sin_port        = htons(pvrf->gw_port);
        pvrf->send_sock.sin_addr.s_addr = htonl(pvrf->gw_ip);

        jnx_gw_log(LOG_DEBUG, "GRE Gateway %s in %s message sent",
                   JNX_GW_IP_ADDRA(pvrf->gw_ip), pvrf->vrf_name);

        sendto(pvrf->ctrl_fd, pvrf->send_buf, pvrf->send_len, MSG_EOR,
               (struct sockaddr *)&pvrf->send_sock,
               sizeof(struct sockaddr_in));

        pvrf->send_len = 0;
    }

    /* set to the current gatway ip */
    if (pvrf->send_len == 0) {
        pvrf->gw_ip   = pgre_gw->gre_gw_ip;
        pvrf->gw_port = pgre_gw->gre_gw_port;
    }

    /* fill the header */
    pgre_msg = (typeof(pgre_msg)) ((uint8_t *)pvrf->send_buf + pvrf->send_len);
    pgre_msg->msg_type  = msg_type;
    pgre_msg->prof_type = pgre_session->sesn_proftype;
    pgre_msg->err_code  = pgre_session->sesn_errcode;
    pgre_msg->flags     = pgre_session->sesn_flags;
    pgre_msg->msg_len   = htons(sizeof(*pgre_msg));
    pgre_msg->resv      = 0;
    pgre_msg->msg_id    = htonl(pgre_session->sesn_msgid);

    /* fill the user info */
    pgre_msg->user_5t.err_code = pgre_session->sesn_errcode;
    pgre_msg->user_5t.msg_len  = sizeof(pgre_msg->user_5t);
    pgre_msg->user_5t.flags    = pgre_session->sesn_flags;
    pgre_msg->user_5t.proto    = pgre_session->sesn_proto;
    pgre_msg->user_5t.dst_port = htons(pgre_session->sesn_dport);
    pgre_msg->user_5t.src_port = htons(pgre_session->sesn_sport);
    pgre_msg->user_5t.src_ip   = htonl(pgre_session->sesn_client_ip);
    pgre_msg->user_5t.dst_ip   = htonl(pgre_session->sesn_server_ip);

    /* fill the tunnel info */
    pgre_msg->tun.err_code = pgre_session->sesn_errcode;
    pgre_msg->tun.msg_len  = sizeof(pgre_msg->tun);
    pgre_msg->tun.flags    = pgre_session->sesn_flags;
    pgre_msg->tun.tun_type = JNX_GW_TUNNEL_TYPE_GRE;
    pgre_msg->tun.gre_key  = htonl(pgre_session->ingress_gre_key);
    pgre_msg->tun.data_ip  = htonl(pgre_session->ingress_self_ip);
    pgre_msg->tun.gw_ip    = htonl(pgre_session->ingress_gw_ip);

    /* update the length */
    pvrf->send_len  += sizeof(*pgre_msg);

    JNX_GW_CTRL_VRF_SEND_UNLOCK(pvrf);

    return EOK;
}

/**
 * This function pushes the current message to the
 * gre gateway in the vrf
 * @params pvrf vrf structure pointer
 */
status_t 
jnx_gw_ctrl_send_gw_gre_msg(jnx_gw_ctrl_vrf_t * pvrf)
{
    /* nothing has been put, return */
    if ((pvrf->send_len == 0) || (!pvrf->gw_ip)) {
        return EOK;
    }

    JNX_GW_CTRL_VRF_SEND_LOCK(pvrf);

    /* set the send socket params */
    pvrf->send_sock.sin_family      = AF_INET;
    pvrf->send_sock.sin_port        = htons(pvrf->gw_port);
    pvrf->send_sock.sin_addr.s_addr = htonl(pvrf->gw_ip);

    /* send it out */
    sendto(pvrf->ctrl_fd, pvrf->send_buf, pvrf->send_len, MSG_EOR,
           (struct sockaddr *)&pvrf->send_sock, sizeof(struct sockaddr_in));

    /* reset the buffer length */
    pvrf->send_len = 0;
    JNX_GW_CTRL_VRF_SEND_UNLOCK(pvrf);

    jnx_gw_log(LOG_DEBUG, "GRE Gateway %s in %s message sent",
           JNX_GW_IP_ADDRA(pvrf->gw_ip), pvrf->vrf_name);

    return EOK;
}

/**
 * This function pushes the current messages 
 * to the gateways in every vrf
 */
status_t 
jnx_gw_ctrl_send_gw_gre_msgs(void)
{
    jnx_gw_ctrl_vrf_t * pvrf = NULL;

    /* push all the messages to the gre gateways */
    while ((pvrf = jnx_gw_ctrl_get_next_vrf(pvrf))) {
        jnx_gw_ctrl_send_gw_gre_msg(pvrf);
    }
    return EOK;
}

/**
 * This function handles the messages from the
 * a specific gre gateway in a vrf
 * @params pvrf      vrf structure pointer
 * @params pgre_gw   gre gateway structure pointer
 * @params pmsg      message bufer
 * @params msg_len   message length
 */
status_t
jnx_gw_ctrl_gre_gw_msg_handler(jnx_gw_ctrl_vrf_t * pvrf,
                               jnx_gw_ctrl_gre_gw_t * pgre_gw,
                               void * pmsg, uint32_t msg_len)
{
    uint32_t len = 0;
    jnx_gw_ctrl_gre_msg_t  * pgre_msg = NULL;
    jnx_gw_ctrl_data_pic_t * pdata_pic = NULL;

    /* acquire the read lock */
    JNX_GW_CTRL_CONFIG_READ_LOCK();

    /* while message length, for the gre gateway, process
     * the message
     */
    pgre_msg = pmsg; 
    while (len < msg_len) {

        switch(pgre_msg->msg_type) {
            case JNX_GW_CTRL_GRE_SESN_INIT_REQ:
                jnx_gw_ctrl_sig_gre_session_add(pvrf, pgre_gw, pgre_msg);
                break;

            case JNX_GW_CTRL_GRE_SESN_END_REQ:
            case JNX_GW_CTRL_GRE_SESN_ERR_REQ:
            case JNX_GW_CTRL_GRE_SESN_ERR:
                jnx_gw_ctrl_sig_gre_session_delete(pvrf, pgre_gw, pgre_msg);
                break;
            default:
                break;
        }
        len += ntohs(pgre_msg->msg_len);
        pgre_msg = (typeof(pgre_msg))((uint8_t *)pmsg + len);
    }

    /* release the read lock */
    JNX_GW_CTRL_CONFIG_READ_UNLOCK();

    /* now push messages to the data pics */
    while ((pdata_pic = jnx_gw_ctrl_get_next_data_pic(pdata_pic))) {
        jnx_gw_ctrl_send_data_pic_msg(pdata_pic);
    }

    /* also, if there are any messages pending for
     * the gre gateway for the vrf,  send out
     */
    jnx_gw_ctrl_send_gw_gre_msg(pvrf);
    return EOK;
}

/**
 * Initialize ths free list for the receive
 * thread, the vrf add/delete messages will
 * be using the list entries from this free
 * pool
 * @params prx_thread receive thread pointer
 */
status_t
jnx_gw_ctrl_init_list(jnx_gw_ctrl_rx_thread_t * prx_thread)
{
    uint32_t idx = 0;
    pthread_mutexattr_t attr;

    if (!(prx_thread->rx_thread_free_list
          = JNX_GW_MALLOC(JNX_GW_CTRL_ID, (FD_SETSIZE + 1) *
                          sizeof(*prx_thread->rx_thread_free_list)))) {
        return EFAIL;
    }

    /* link the free list */
    for (idx = 1; idx < (FD_SETSIZE + 1); idx++) {
        prx_thread->rx_thread_free_list[idx - 1].next =
            &prx_thread->rx_thread_free_list[idx];
    }

    prx_thread->rx_thread_free_list[idx - 1].next = NULL;

    /* create the mutex for this list, make it recursive */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&prx_thread->rx_thread_list_mutex, &attr);

    return EOK;
}

/**
 * This function returns a list pointer
 * from the free list pool for a receive
 * thread
 * @params prx_thread receive thread pointer
 * @returns plist list structure pointer
 */
list_t *
jnx_gw_ctrl_get_list_entry(jnx_gw_ctrl_rx_thread_t * prx_thread)
{
    list_t * plist;

    JNX_GW_CTRL_RX_LIST_LOCK(prx_thread);

    plist = prx_thread->rx_thread_free_list;

    if (plist) {
        prx_thread->rx_thread_free_list = plist->next;
        plist->next = NULL;
    }

    JNX_GW_CTRL_RX_LIST_UNLOCK(prx_thread);
    return plist;
}

/**
 * This function returns the list pointer
 * to the free list pool for a receive
 * thread
 * @params prx_thread receive thread pointer
 * @params plist list structure pointer
 */
void
jnx_gw_ctrl_release_list_entry(jnx_gw_ctrl_rx_thread_t * prx_thread,
                               list_t * plist)
{
    JNX_GW_CTRL_RX_LIST_LOCK(prx_thread);

    plist->next = prx_thread->rx_thread_free_list;
    prx_thread->rx_thread_free_list = plist;

    JNX_GW_CTRL_RX_LIST_UNLOCK(prx_thread);
    return; 
}

/**
 * This function initializes the buffer
 * pool for message queues between
 * the receive threads and the processing threads
 */

status_t
jnx_gw_ctrl_init_buf(void)
{
    uint32_t idx;
    jnx_gw_ctrl_buf_t * pbuf, * prev;

    jnx_gw_log(LOG_INFO, "Packet buffer pool create");

    /* initialize the free buffer pool buffer count */
    jnx_gw_ctrl.buf_pool_count = JNX_GW_CTRL_BUF_COUNT;

    /* initialize the mutex for the free pool */
    pthread_mutex_init(&jnx_gw_ctrl.buf_pool_mutex, 0);

    /* allocate the memory for the free pool buffer */
    if (!(pbuf = JNX_GW_MALLOC(JNX_GW_CTRL_ID,
                               jnx_gw_ctrl.buf_pool_count *
                               sizeof(*jnx_gw_ctrl.buf_pool)))) {
        jnx_gw_log(LOG_ERR, "Packet buffer pool malloc failed");
        return EFAIL;
    }

    jnx_gw_ctrl.buf_pool = pbuf;
    prev = pbuf;
    pbuf = (typeof(pbuf))((uint8_t *)prev + sizeof(*pbuf));

    /* initialize the free buffer pool */
    for (idx = 1; idx < (jnx_gw_ctrl.buf_pool_count); idx++) {
        prev->buf_next = pbuf;
        prev->buf_flags = JNX_GW_CTRL_BUF_IN_FREE_LIST;
        prev = pbuf;
        pbuf = (typeof(pbuf))((uint8_t *)prev + sizeof(*pbuf));
    }

    prev->buf_next = NULL;
    prev->buf_flags = JNX_GW_CTRL_BUF_IN_FREE_LIST;
    return EOK;
}

/**
 * This function returns a buffer pointer
 * from the free buffer pool for packet
 * store by the receive threads
 */

jnx_gw_ctrl_buf_t *
jnx_gw_ctrl_get_buf(void)
{
    jnx_gw_ctrl_buf_t * pbuf = NULL;

    JNX_GW_CTRL_BUF_POOL_LOCK();
    if (jnx_gw_ctrl.buf_pool_count) {
        pbuf                 = jnx_gw_ctrl.buf_pool;
        jnx_gw_ctrl.buf_pool = pbuf->buf_next;
        pbuf->buf_next       = NULL;
        jnx_gw_ctrl.buf_pool_count--;
    }
    JNX_GW_CTRL_BUF_POOL_UNLOCK();
    return pbuf;
}

/**
 * This function returns the buffer pointer
 * to the free buffer pool for packet
 * after the packet message is processed
 * by a processing thread
 * @params pbuf buffer structure pointer
 */

void 
jnx_gw_ctrl_release_buf(jnx_gw_ctrl_buf_t * pbuf)
{
    JNX_GW_CTRL_BUF_POOL_LOCK();
    pbuf->buf_next  = jnx_gw_ctrl.buf_pool;
    pbuf->buf_flags = JNX_GW_CTRL_BUF_IN_FREE_LIST;
    jnx_gw_ctrl.buf_pool = pbuf;
    jnx_gw_ctrl.buf_pool_count++;
    JNX_GW_CTRL_BUF_POOL_UNLOCK();
    return;
}

/**
 * This function queues a buffer to a
 * processing thread, this function is called
 * on the context of the receive thread
 * This is not a fair round robin scheme
 * rather a weigthed load balance method
 * of queueing to the processing queues
 * @params pbuf buffer structure pointer
 */

jnx_gw_ctrl_proc_thread_t *
jnx_gw_ctrl_queue_buf(jnx_gw_ctrl_buf_t * pbuf)
{
    jnx_gw_ctrl_buf_list_t *prx_queue, *pmin_queue = NULL;
    jnx_gw_ctrl_proc_thread_t *pthread, *pmin_thread = NULL;

    /* try soft attach */

    for (pthread = jnx_gw_ctrl.proc_threads; (pthread);
         pthread = pthread->proc_thread_next) {

        prx_queue = &pthread->proc_thread_rx;

        /* try the lock, otherwise go for the next */
        if (!JNX_GW_CTRL_PROC_RX_QUEUE_TRY_LOCK(prx_queue)) {

            if (prx_queue->queue_tail == NULL) {
                prx_queue->queue_head = pbuf;
            }
            else {
                prx_queue->queue_tail->buf_next = pbuf;
            }

            prx_queue->queue_tail = pbuf;
            pbuf->buf_next = NULL;
            pbuf->buf_flags = JNX_GW_CTRL_BUF_IN_QUEUE;
            prx_queue->queue_length++;

            JNX_GW_CTRL_PROC_RX_QUEUE_UNLOCK(prx_queue);
            return pthread;
        }

        if (!pmin_queue) {
            pmin_queue = prx_queue;
            pmin_thread = pthread;
            continue;
        }

        if (pmin_queue->queue_length > prx_queue->queue_length) {
            pmin_queue = prx_queue;
            pmin_thread = pthread;
        }
    }

    /* now hard wait & attach to the min thread  */
    if (pmin_queue) {

        JNX_GW_CTRL_PROC_RX_QUEUE_LOCK(pmin_queue);

        if (pmin_queue->queue_tail == NULL) {
            pmin_queue->queue_head = pbuf;
        }
        else {
            pmin_queue->queue_tail->buf_next = pbuf;
        }
        pmin_queue->queue_tail = pbuf;

        pbuf->buf_next = NULL;
        pbuf->buf_flags = JNX_GW_CTRL_BUF_IN_QUEUE;
        pmin_queue->queue_length++;

        JNX_GW_CTRL_PROC_RX_QUEUE_UNLOCK(pmin_queue);
        return pmin_thread;
    }

    /* could not put into any of the processing threads,
     * release the buffer */
    jnx_gw_ctrl_release_buf(pbuf);
    return NULL;
}

/**
 * This function dequeues a buffer for a
 * processing thread, 
 * @params  pqueue processing thread rx queue
 * @returns pbuf   buffer structure pointer
 */

jnx_gw_ctrl_buf_t * 
jnx_gw_ctrl_dequeue_buf(jnx_gw_ctrl_buf_list_t *pqueue)
{
    jnx_gw_ctrl_buf_t * pbuf = NULL;

    /* do not wait for a message to be queued,
     * try the lock, if it is available catch
     * any available message in the queue,
     * otherwise return
     */
    if (!JNX_GW_CTRL_PROC_RX_QUEUE_TRY_LOCK(pqueue)) {

        if ((pbuf = pqueue->queue_head)) {
            pqueue->queue_head = pbuf->buf_next;
            if (pqueue->queue_head == NULL) {
                pqueue->queue_tail = NULL;
            }
            pbuf->buf_next = NULL;
            pqueue->queue_length--;
        }
        JNX_GW_CTRL_PROC_RX_QUEUE_UNLOCK(pqueue);
    }
    return pbuf;
}

/**
 * This function creates a new fd list structure
 * for a receive  thread
 * initializes the vrf list for the fd list
 * This fd list structure listens for a 
 * set of vrfs packet messages on a receive
 * thread context
 * @params pthread receive thread pointer
 * @returns socklist socketlist pointer
 */
jnx_gw_ctrl_sock_list_t *
jnx_gw_ctrl_add_fdlist(jnx_gw_ctrl_rx_thread_t * pthread)
{
    jnx_gw_ctrl_sock_list_t * socklist;

    if (!(socklist = JNX_GW_MALLOC(JNX_GW_CTRL_ID, sizeof(*socklist)))) {
        jnx_gw_log(LOG_ERR, "fd-set list add malloc failed");
        return NULL;
    }

    FD_ZERO(&socklist->recv_fdset);
    socklist->recv_tval.tv_usec = 100;
    socklist->recv_tval.tv_sec  = 0;
    socklist->next_socklist     = pthread->rx_thread_fdset_list;
    pthread->rx_thread_fdset_list = socklist;
    patricia_root_init(&socklist->recv_vrf_db, FALSE, 
                       fldsiz(jnx_gw_ctrl_vrf_t, ctrl_fd),
                       fldoff(jnx_gw_ctrl_vrf_t, ctrl_fd) -
                       fldoff(jnx_gw_ctrl_vrf_t, vrf_tnode) -
                       fldsiz(jnx_gw_ctrl_vrf_t, vrf_tnode));


    return socklist;
}

/**
 * This function deletes a fd list structure
 * for a receive  thread & other resources
 * @params pthread receive thread pointer
 */
static void
jnx_gw_ctrl_clear_rx_thread(jnx_gw_ctrl_rx_thread_t * prthread)
{
    list_t * plist = NULL, * pnext = NULL;
    jnx_gw_ctrl_vrf_t * pvrf;
    jnx_gw_ctrl_sock_list_t * socklist, * cur;
    jnx_gw_ctrl_rx_thread_t * pthread, * prev_thread;

    /* clear the sock lists */
    socklist = prthread->rx_thread_fdset_list;

    while (socklist) {

        /* release the vrfs, from the receive thread context */
        while ((pvrf = jnx_gw_ctrl_thread_get_first_vrf(socklist))) {

            pvrf->vrf_sig_status = JNX_GW_CTRL_STATUS_INIT;
            pvrf->vrf_socklist  = NULL;
            pvrf->vrf_rx_thread = NULL;

            /* delete from the sock list */
            patricia_delete(&socklist->recv_vrf_db, &pvrf->vrf_tnode);

            patricia_node_init_length(&pvrf->vrf_tnode, sizeof(pvrf->ctrl_fd));

            /* try attaching to a different receive thread */
            jnx_gw_ctrl_attach_to_thread(pvrf);
        }

        pvrf = NULL;
        cur = socklist;
        socklist = cur->next_socklist;
        free(cur);
    }

    /* clear the add lists */
    for (plist = prthread->rx_thread_vrf_add_list; (plist);
         pnext = plist->next, plist->next = prthread->rx_thread_free_list,
         prthread->rx_thread_free_list = plist, plist = pnext) {

        /* try attaching to some other receive thread */
        pvrf = (typeof(pvrf))plist->ptr;
        jnx_gw_ctrl_attach_to_thread(pvrf);
    }

    /* clear the delete lists */

    for (plist = prthread->rx_thread_vrf_del_list; (plist);
         pnext = plist->next, plist->next = prthread->rx_thread_free_list,
         prthread->rx_thread_free_list = plist, plist = pnext) {

        /* clear the vrf */
        pvrf = (typeof(pvrf))plist->ptr;

        close(pvrf->ctrl_fd);
        pvrf->ctrl_fd = 0;
        pvrf->vrf_socklist  = NULL;
        pvrf->vrf_rx_thread = NULL;

        if (pvrf->vrf_status == JNX_GW_CTRL_STATUS_DELETE) {

            /* acquire the config write lock */
            JNX_GW_CTRL_CONFIG_WRITE_LOCK();

            jnx_gw_ctrl_delete_vrf(pvrf);

            /* release the config write lock */
            JNX_GW_CTRL_CONFIG_WRITE_LOCK();

        }
    }

    /* delete the receive thread */
    prthread->rx_thread_fdset_list = NULL;
    prev_thread = NULL;
    pthread = jnx_gw_ctrl.recv_threads;
    while (pthread) {
        if (pthread != prthread) {
            prev_thread = pthread;
            prthread = pthread->rx_thread_next;
            continue;
        }
        if (prev_thread) {
            prev_thread->rx_thread_next 
                = pthread->rx_thread_next;
        } else {
            jnx_gw_ctrl.recv_threads
                = pthread->rx_thread_next;
        }
        break;
    }

    JNX_GW_FREE (JNX_GW_CTRL_ID, prthread->rx_thread_free_list);
    JNX_GW_FREE (JNX_GW_CTRL_ID, prthread);
    return;
}

/**
 * This function returns the minimum fdset entry for the
 * receive thread entry 
 * @params pthread  rx thread pointer
 */

jnx_gw_ctrl_sock_list_t *
jnx_gw_ctrl_find_fdmin(jnx_gw_ctrl_rx_thread_t * pthread)
{
    jnx_gw_ctrl_sock_list_t *socklist = NULL,
                            *min_socklist = NULL;

    socklist = pthread->rx_thread_fdset_list;
    while (socklist) {
        if (!min_socklist) {
            min_socklist = socklist;
            continue;
        }
        if (min_socklist->recv_fdcount > socklist->recv_fdcount) {
            min_socklist = socklist;
        }
        socklist = socklist->next_socklist;
    }

    /* if no sock set exists, or, all the
     * socklist count is reaching the fd set size,
     * add one more socklist entry to the receive
     * thread
     */
    if (!min_socklist || min_socklist->recv_fdcount == 100) {
        return jnx_gw_ctrl_add_fdlist(pthread);
    }
    return min_socklist;
}

/**
 * This function handles the vrf add/delete
 * messages for a receive thread
 * it dequeues the vrf add/delete messages
 * from the queue, & processes them
 * @params pthread receive thread pointer
 */
void 
jnx_gw_ctrl_thread_handle_vrf_events(jnx_gw_ctrl_rx_thread_t * prx_thread)
{
    list_t * plist = NULL, * pfree = NULL;
    jnx_gw_ctrl_sock_list_t * socklist = NULL;
    jnx_gw_ctrl_vrf_t * pvrf;

    JNX_GW_CTRL_RX_LIST_LOCK(prx_thread);

    /* delete the vrfs, from the fdset */
    for (plist = prx_thread->rx_thread_vrf_del_list; (plist);
         plist = plist->next) {

        pvrf  = (typeof(pvrf))plist->ptr;

        pfree = plist;

        jnx_gw_log(LOG_INFO, "Receive thread, "
                   " routing instance \"%s\" deleted",
                   pvrf->vrf_name);

        if (!(socklist = pvrf->vrf_socklist)) {

            close(pvrf->ctrl_fd);

            pvrf->ctrl_fd = 0;

            if (pvrf->vrf_status == JNX_GW_CTRL_STATUS_DELETE) {

                /* acquire the config write lock */
                JNX_GW_CTRL_CONFIG_WRITE_LOCK();

                jnx_gw_ctrl_delete_vrf(pvrf);

                /* release the config write lock */
                JNX_GW_CTRL_CONFIG_WRITE_LOCK();
            }
            continue;
        }

        /* clear the fd entry from the fdset */
        FD_CLR(pvrf->ctrl_fd, &socklist->recv_fdset);

        close(pvrf->ctrl_fd);

        /* reset the vrf structure back pointers */
        pvrf->ctrl_fd = 0;
        pvrf->vrf_socklist  = NULL;
        pvrf->vrf_rx_thread = NULL;

        /* delete from the sock list */
        patricia_delete(&socklist->recv_vrf_db, &pvrf->vrf_tnode);

        /* delete the vrf */
        if (pvrf->vrf_status == JNX_GW_CTRL_STATUS_DELETE) {
            jnx_gw_ctrl_delete_vrf(pvrf);
        }

        socklist->recv_fdcount--;
        prx_thread->rx_thread_vrf_count--;
    }

    /* reclaim back to the free list */
    if (pfree) {
        pfree->next = prx_thread->rx_thread_free_list;
        prx_thread->rx_thread_free_list = prx_thread->rx_thread_vrf_del_list;
    }

    pfree = NULL;
    prx_thread->rx_thread_vrf_del_list = NULL;

    /* the vrf add message, add the vrfs to the socklists */

    for (plist = prx_thread->rx_thread_vrf_add_list; (plist);
         plist = plist->next) {

        pvrf  = (typeof(pvrf))plist->ptr;

        pfree = plist;

        /* select an fdset */
        if (!(socklist = jnx_gw_ctrl_find_fdmin(prx_thread))) {
            continue;
        }

        /* add to the vrf list for the socket list */
        if (!patricia_add(&socklist->recv_vrf_db, &pvrf->vrf_tnode)) {
            continue;
        }

        jnx_gw_log(LOG_INFO, "Receive thread, routing instance \"%s\" added",
                     pvrf->vrf_name);

        /* get the back pointers in place */
        pvrf->vrf_socklist   = socklist;
        pvrf->vrf_rx_thread  = prx_thread;
        pvrf->vrf_sig_status = JNX_GW_CTRL_STATUS_UP;

        /* set the fd entry in the fdset */
        FD_SET(pvrf->ctrl_fd, &socklist->recv_fdset);

        socklist->recv_fdcount++;
        prx_thread->rx_thread_vrf_count++;
    }

    /* reclaim back to the free list */

    if (pfree) {
        pfree->next = prx_thread->rx_thread_free_list;
        prx_thread->rx_thread_free_list = prx_thread->rx_thread_vrf_add_list;
    }

    prx_thread->rx_thread_vrf_add_list = NULL;

    JNX_GW_CTRL_RX_LIST_UNLOCK(prx_thread);
    return;
}


/**
 * This function is called in the context of config
 * main thread context, to queue an vrf add message
 * to a receive thread.
 * A receive thread is selected based on the load
 * interms of number of vrfs it is listening for
 * message  receives
 * @params pvrf  vrf structure pointer
 * @returns EOK  if the message queue is successful
 *          EFAIL otherwise
 */
status_t
jnx_gw_ctrl_attach_to_thread(jnx_gw_ctrl_vrf_t * pvrf)
{
    jnx_gw_ctrl_rx_thread_t * prx_thread = NULL, * pmin_rx_thread = NULL;
    list_t *plist = NULL;

    /* either this vrf is not present on the
     * control pic, attached to any ifl, or it is
     * already attached to some recv thread,
     * return 
     * can not support more than FD_SETSIZE sockets
     * for listening
     */

    if ((pvrf->vrf_sig_status != JNX_GW_CTRL_STATUS_INIT) ||
        (pvrf->vrf_rx_thread) ||
        ((unsigned int)pvrf->ctrl_fd >= FD_SETSIZE)) {
        return EOK;
    }

    /* select the minimally loaded recv thread,
     * interms of number of vrfs
     */
    for (prx_thread = jnx_gw_ctrl.recv_threads; (prx_thread);
         prx_thread = prx_thread->rx_thread_next) {
        if (prx_thread->rx_thread_status != JNX_GW_CTRL_STATUS_UP) {
            continue;
        }
        if (!pmin_rx_thread) {
            pmin_rx_thread = prx_thread;
            continue;
        }
        if (pmin_rx_thread->rx_thread_vrf_count >
            prx_thread->rx_thread_vrf_count) {
            pmin_rx_thread = prx_thread;
        }
    }

    /* get a list entry for the recv thread */
    if ((!pmin_rx_thread) ||
        !(plist = jnx_gw_ctrl_get_list_entry(pmin_rx_thread))) {
        return EFAIL;
    }

    /* add to the vrf-add list for the receive thread */
    plist->ptr = pvrf;

    JNX_GW_CTRL_RX_LIST_LOCK(pmin_rx_thread);

    plist->next = pmin_rx_thread->rx_thread_vrf_add_list;
    pmin_rx_thread->rx_thread_vrf_add_list = plist;

    JNX_GW_CTRL_RX_LIST_UNLOCK(pmin_rx_thread);

    return EOK;
}

/**
 * This function is called in the context of config
 * eventlib thread(main), to queue a vrf delete message
 * to a receive thread.
 * @params pvrf  vrf structure pointer
 * @returns EOK  if the message queue is successful
 *          EFAIL otherwise
 */
status_t
jnx_gw_ctrl_detach_from_thread(jnx_gw_ctrl_vrf_t * pvrf)
{
    jnx_gw_ctrl_rx_thread_t * prx_thread = NULL;
    list_t * plist = NULL;

    /* this vrf is still not down, or it is
     * not attached to any recv thread 
     * return
     */

    if ((pvrf->vrf_sig_status != JNX_GW_CTRL_STATUS_UP) ||
        (!pvrf->vrf_rx_thread)) {
        return EOK;
    }

    prx_thread = pvrf->vrf_rx_thread;

    /* get a list entry for the recv thread */
    if (!(plist = jnx_gw_ctrl_get_list_entry(prx_thread))) {
        return EFAIL;
    }

    /* set the vrf status as delete */
    pvrf->vrf_status = JNX_GW_CTRL_STATUS_DELETE;

    /* add to the delete vrf list */
    plist->ptr = pvrf;

    /* add the vrf del list of the recv thread */
    JNX_GW_CTRL_RX_LIST_LOCK(prx_thread);

    plist->next = prx_thread->rx_thread_vrf_del_list;
    prx_thread->rx_thread_vrf_del_list = plist;

    JNX_GW_CTRL_RX_LIST_UNLOCK(prx_thread);

    return EOK;
}

/**
 * This function is the receive thread entry point for receiving packets
 * on multiple vrfs. Multiple fd sets are created, to address more than
 * FD_SETSIZE limitations.
 * This design may induce a letency for receiving vrf messages by the
 * control application. These messages are then queued to multiple
 * processing threads for further processing
 * @params pthread_ptr  rereceive thread pointer
 */
void *
jnx_gw_ctrl_rx_msg_thread(void * thread_ptr)
{
    int32_t readyfds, len, sock_len;
    struct fd_set    recv_fdset;
    jnx_gw_ctrl_gre_gw_t * pgre_gw;
    jnx_gw_ctrl_rx_thread_t * prx_thread;
    jnx_gw_ctrl_proc_thread_t * proc_thread;
    jnx_gw_ctrl_vrf_t * pvrf = NULL;
    jnx_gw_ctrl_buf_t * pbuf = NULL;
    jnx_gw_ctrl_sock_list_t *socklist;
    sigset_t sigmask;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

    prx_thread = (typeof(prx_thread))thread_ptr;


    /* add fd socklists */
    socklist = jnx_gw_ctrl_add_fdlist(prx_thread);

    socklist = jnx_gw_ctrl_add_fdlist(prx_thread);

    prx_thread->rx_thread_status = JNX_GW_CTRL_STATUS_UP;

    /* later on, we may need to pin this to a specific
     * processor hardware thread (XXX)
     */

    /* receive messages from the client gateways, on 
     * the ready to read sockets, through the socket
     * fd list status, the sockets are set to non
     * blocking mode udp socket
     */

    while (1) {

        FD_COPY(&socklist->recv_fdset, &recv_fdset);
        socklist->recv_tval.tv_usec = 500;
        socklist->recv_tval.tv_sec  = 0;

        if ((readyfds = select(FD_SETSIZE,
                               &recv_fdset, 0, 0,
                               &socklist->recv_tval)) < 0)  {

            /* select call error, break */
            prx_thread->rx_thread_status = JNX_GW_CTRL_STATUS_DOWN;
            break;
        }

        if (readyfds == 0) {

            /* if the thread is marked as delete, break &
             * do a clean up of the thread
             */
            if (prx_thread->rx_thread_status == JNX_GW_CTRL_STATUS_DELETE) {
                break;
            }

            /* handle the vrf add/delete messages for
             * the recv thread 
             */
            jnx_gw_ctrl_thread_handle_vrf_events(prx_thread);

            /* do select on the next fdset for packet recv */
            if (!(socklist = socklist->next_socklist)) {
                socklist = prx_thread->rx_thread_fdset_list;
            }
            continue;
        }

        pvrf = NULL;

        /* for the vrfs in the socklist listen socket list */
        while ((pvrf = jnx_gw_ctrl_thread_get_next_vrf(socklist, pvrf))) {

            /* if not packet pending event continue */
            if (!FD_ISSET(pvrf->ctrl_fd, &recv_fdset))
                continue;

            /* get a free buffer */
            if (!(pbuf = jnx_gw_ctrl_get_buf())) break;

            pbuf->buf_flags = JNX_GW_CTRL_BUF_IN_RECVFROM;

            sock_len = sizeof(struct sockaddr_in);

            /* while there are messages for this vrf, drain all */

            while ((len = recvfrom(pvrf->ctrl_fd, pbuf->buf_ptr,
                                   JNX_GW_CTRL_MAX_PKT_BUF_SIZE, 0,
                                   (struct sockaddr *)&socklist->recv_sock,
                                   &sock_len))) {


                pbuf->buf_flags = JNX_GW_CTRL_BUF_IN_RX;

                /* get the sender (client gateway) ip addr, port */
                pbuf->src_addr = 
                    ntohl(*(uint32_t *)&socklist->recv_sock.sin_addr.s_addr);
                pbuf->src_port = ntohs(socklist->recv_sock.sin_port);
                pbuf->pvrf     = pvrf;
                pbuf->buf_len  = len;

                /* acquire the config write lock */
                JNX_GW_CTRL_CONFIG_WRITE_LOCK();

                /* get the gateway entry, if not present create a new one */
                if (!(pgre_gw = jnx_gw_ctrl_lookup_gre_gw(pvrf,
                                                          pbuf->src_addr))) {
                    pgre_gw = jnx_gw_ctrl_add_gre_gw(pvrf, pbuf->src_addr, 
                                                     pbuf->src_port);
                }

                /* release the config write lock */
                JNX_GW_CTRL_CONFIG_WRITE_UNLOCK();

                /* attach the buffer to one of the  process thread queue */

                if (!(proc_thread = jnx_gw_ctrl_queue_buf(pbuf))) {
                    continue;
                }

                /* signal receive threads on packet receive 
                 * when there are messages, put the messages into
                 * the process threads, send the signals 
                 * so that the process thread can process
                 * the messages
                 * Send packet event to the receive thread */
                JNX_GW_CTRL_PROC_SIG_EVENT(proc_thread);

                /* get a free buffer */
                if (!(pbuf = jnx_gw_ctrl_get_buf())) break;
            }
        }

        /* do select, on the next fdset for packet recv */
        if (!(socklist = socklist->next_socklist)) {
            socklist = prx_thread->rx_thread_fdset_list;
        }
    }

    /* shutdown scenario */
    if (prx_thread->rx_thread_status == JNX_GW_CTRL_STATUS_DELETE) {
        return NULL;
    }

    /* release the vrfs from the receive thread,
     * so that they can be attached to some other receive
     * thread
     */
    prx_thread->rx_thread_status = JNX_GW_CTRL_STATUS_DOWN;
    jnx_gw_ctrl_clear_rx_thread(prx_thread);

    /* print error message */
    return NULL;
}

/**
 * This function is the entry point for the gre message procesing threads,
 * It seats in a forever loop, waiting for a packet receive event, to 
 * start processing the packets
 * @params thread_ptr processing thread pointer
 */
void *
jnx_gw_ctrl_proc_msg_thread (void * thread_ptr)
{
    jnx_gw_ctrl_vrf_t * pvrf;
    jnx_gw_ctrl_buf_t * pbuf;
    jnx_gw_ctrl_gre_gw_t * pgre_gw;
    jnx_gw_ctrl_proc_thread_t * proc_thread;
    uint32_t msg_len, src_addr;
    uint16_t src_port;
    sigset_t sigmask;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigmask, NULL);

    proc_thread = (typeof(proc_thread))thread_ptr;

    proc_thread->proc_thread_status = JNX_GW_CTRL_STATUS_UP;

    while (1) {

        /* wait for the message receive signal event */
        JNX_GW_CTRL_PROC_EVENT(proc_thread);

        /* if the thread is marked delete, clean up the thread */
        if (proc_thread->proc_thread_status == JNX_GW_CTRL_STATUS_DELETE) {
            break;
        }

        /* dequeue the packets from the receive queue */
        while ((pbuf = jnx_gw_ctrl_dequeue_buf(&proc_thread->proc_thread_rx))) {

            /* mark the buffer as in procesing thread */
            pbuf->buf_flags = JNX_GW_CTRL_BUF_IN_PROC;

            /* get the sender (client gateway) ip addr, port */
            src_addr = pbuf->src_addr;
            src_port = pbuf->src_port;

            /* vrf pointer & packet length */
            pvrf     = pbuf->pvrf;
            msg_len  = pbuf->buf_len;

            if (!pvrf || !msg_len) {
                jnx_gw_ctrl_release_buf(pbuf);
                continue;
            }

            /* acquire the config write lock */
            JNX_GW_CTRL_CONFIG_WRITE_LOCK();

            /* get the gateway entry, if not present create a new one */
            if (!(pgre_gw = jnx_gw_ctrl_lookup_gre_gw(pvrf, src_addr))) {
                pgre_gw = jnx_gw_ctrl_add_gre_gw(pvrf, src_addr, src_port);
            }

            /* release the config write lock */
            JNX_GW_CTRL_CONFIG_WRITE_UNLOCK();

            /* now handle the message */
            jnx_gw_ctrl_gre_gw_msg_handler(pvrf, pgre_gw, pbuf->buf_ptr,
                                           msg_len);

            /* release the buffer to the free pool */
            jnx_gw_ctrl_release_buf(pbuf);
        }
    }

    /* delete self from the process thread list,
       so that no further packets are queued */
    /* release the locks holds if any */
    /* clean up the buffers */
    /* print error message */
    return NULL;
}

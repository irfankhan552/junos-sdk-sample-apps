/*
 * $Id: jnx-gateway-ctrl_mgmt.c 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-ctrl_mgmt.c - op command routines
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
 * @file jnx-gateway-ctrl_mgmt.c
 * @brief
 * This file contains routines for handling
 * operational command request messages and
 * configuration messages from the
 * management application
 */

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>

#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include <jnx/pconn.h>
#include <jnx/interface_info_pub.h>

/* include the comman header files for
   the sample gateway application */
#include <jnx/jnx-gateway.h>
#include <jnx/jnx-gateway_msg.h>

/* include control agent header file */
#include "jnx-gateway-ctrl.h"

/***********************************************************
 *                                                         *
 *        LOCAL FUNCTION PROTOTYPE DEFINITIONS             *
 *                                                         *
 ***********************************************************/
static void
jnx_gw_ctrl_fill_gre_session_summary(jnx_gw_ctrl_vrf_t * pvrf,
                       jnx_gw_ctrl_gre_gw_t * pgre_gw,
                       jnx_gw_msg_header_t * hdr,
                       uint16_t * msg_len,
                       uint8_t * msg_count);

static void 
jnx_gw_ctrl_fill_gre_session_info(jnx_gw_ctrl_gre_session_t * pgre_session,
                               jnx_gw_msg_header_t * hdr,
                               uint16_t * msg_len, uint8_t * msg_count);

static void
jnx_gw_ctrl_fill_gw_gre_session_info(jnx_gw_ctrl_gre_gw_t * pgre_gw,
                                  jnx_gw_msg_header_t * hdr,
                                  uint16_t * msg_len, uint8_t * msg_count);

static void 
jnx_gw_ctrl_fill_vrf_gre_session_info(jnx_gw_ctrl_vrf_t * pvrf,
                                   jnx_gw_msg_header_t * hdr,
                                   uint16_t *msg_len, uint8_t *msg_count);

static status_t 
jnx_gw_ctrl_send_gre_session_info(jnx_gw_msg_sub_header_t * preq_msg, uint16_t msg_id);

static void
jnx_gw_ctrl_fill_ipip_tunnel_sum_info(jnx_gw_ctrl_vrf_t * pvrf,
                                      jnx_gw_msg_header_t * hdr,
                                      uint16_t * msg_len, uint8_t * msg_count);
static void
jnx_gw_ctrl_fill_ipip_tunnel_info(jnx_gw_ctrl_ipip_gw_t * pipip_gw,
                                  jnx_gw_msg_header_t * hdr,
                                  uint16_t *msg_len, uint8_t *msg_count);
static void
jnx_gw_ctrl_fill_all_ipip_tun_info(jnx_gw_ctrl_vrf_t * pvrf,
                                   jnx_gw_msg_header_t * hdr,
                                   uint16_t *msg_len, uint8_t *msg_count);
static status_t
jnx_gw_ctrl_send_ipip_tunnel_info(jnx_gw_msg_sub_header_t * preq_msg,
                                  uint16_t msg_id);

static void
jnx_gw_ctrl_fill_user_info(jnx_gw_ctrl_user_t * puser,
                           jnx_gw_msg_header_t * hdr,
                           uint16_t * msg_len, uint8_t  * msg_count);
static status_t
jnx_gw_ctrl_send_user_info(jnx_gw_msg_sub_header_t * preq_msg, uint16_t msg_id);

static status_t 
jnx_gw_ctrl_handle_opcmd(jnx_gw_msg_header_t * msg);

static status_t
jnx_gw_ctrl_config_ctrl(jnx_gw_msg_ctrl_config_ctrl_t * pmsg,
                        uint8_t add_flag);

static status_t
jnx_gw_ctrl_config_data(jnx_gw_msg_ctrl_config_data_t * pmsg,
                        uint8_t add_flag);
static status_t
jnx_gw_ctrl_config_user(jnx_gw_msg_ctrl_config_user_t * pmsg,
                        uint8_t add_flag);
static status_t
jnx_gw_ctrl_config_pic(jnx_gw_msg_ctrl_config_pic_t * pmsg,
                       uint8_t add_flag);
static status_t
jnx_gw_ctrl_handle_config_msg(jnx_gw_msg_header_t * hdr);
status_t
jnx_gw_ctrl_mgmt_msg_handler(pconn_session_t * session,
                             ipc_msg_t *  ipc_msg,
                             void * cookie __unused);
void
jnx_gw_ctrl_mgmt_event_handler(pconn_session_t * session,
                               pconn_event_t event,
                               void * cookie __unused);
static status_t 
jnx_gw_ctrl_mgmt_send_opcmd(jnx_gw_msg_header_t * hdr,
                            uint8_t msg_type, uint16_t msg_len);

status_t jnx_gw_ctrl_init_mgmt(void);
status_t jnx_gw_ctrl_destroy_mgmt(void);

/****************************************************************
 *                                                              *
 *               OP COMMAND HANDLER FUNCTIONS                   *
 *                                                              *
 ****************************************************************/

/**
 *
 * This function fills in the gre summary information
 * to the op command send buffer
 * @params pvrf        vrf structure pointer
 * @params pgre_gw     gre gateway structure pointer
 * @params hdr         buffer header
 * @params msg_len     message length pointer
 * @params msg_count   message count pointer
 *
 */
static void
jnx_gw_ctrl_fill_gre_session_summary(jnx_gw_ctrl_vrf_t * pvrf,
                                  jnx_gw_ctrl_gre_gw_t * pgre_gw,
                                  jnx_gw_msg_header_t * hdr,
                                  uint16_t * msg_len,
                                  uint8_t * msg_count)
{
    uint16_t sublen = 0;
    jnx_gw_msg_sub_header_t * subhdr;
    jnx_gw_msg_ctrl_gre_sum_stat_t *psum = NULL;

    /* set the sub header information */
    subhdr           = (typeof(subhdr)) ((uint8_t *)hdr + (*msg_len));
    sublen           = sizeof(*hdr) + sizeof(*psum);
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
    subhdr->length   = htons(sublen);
    subhdr->sub_type = JNX_GW_FETCH_GRE_SESN_SUM;

    /* set the payload */
    psum = (typeof(psum)) ((uint8_t *)subhdr + sizeof(*subhdr));

    if (pgre_gw) {
        psum->gre_vrf_id            = htonl(pvrf->vrf_id);
        psum->gre_gw_ip             = htonl(pgre_gw->gre_gw_ip);
        psum->gre_active_sesn_count = htonl(pgre_gw->gre_active_sesn_count);
        psum->gre_sesn_count        = htonl(pgre_gw->gre_sesn_count);
    } else if (pvrf) {
        psum->gre_vrf_id            = htonl(pvrf->vrf_id);
        psum->gre_gw_ip             = 0;
        psum->gre_active_sesn_count = htonl(pvrf->gre_active_sesn_count);
        psum->gre_sesn_count        = htonl(pvrf->gre_sesn_count);
    } else {
        psum->gre_vrf_id            = 0xFFFFFFFF;
        psum->gre_gw_ip             = 0;
        psum->gre_active_sesn_count = htonl(jnx_gw_ctrl.gre_active_sesn_count);
        psum->gre_sesn_count        = htonl(jnx_gw_ctrl.gre_sesn_count);
    }

    (*msg_count)++;
    (*msg_len) += sublen;
    return;
}

/**
 *
 * This function fills in the gre session information
 * to the op command send buffer
 * @params pgre_session   gre session structure pointer
 * @params hdr         buffer header
 * @params msg_len     message length pointer
 * @params msg_count   message count pointer
 *
 */
static void 
jnx_gw_ctrl_fill_gre_session_info(jnx_gw_ctrl_gre_session_t * pgre_session,
                               jnx_gw_msg_header_t * hdr,
                               uint16_t * msg_len, uint8_t * msg_count)
{
    int16_t sublen = 0;
    jnx_gw_msg_sub_header_t     *subhdr;
    jnx_gw_msg_ctrl_gre_sesn_stat_t  *psession;

    /* fill sub header */
    subhdr           = (typeof(subhdr)) ((uint8_t *)hdr + (*msg_len));
    sublen           = sizeof(*subhdr) + sizeof(*psession);
    subhdr->sub_type = JNX_GW_FETCH_GRE_SESN;
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
    subhdr->length   = htons(sublen);


    psession = (typeof(psession)) ((uint8_t *)subhdr + sizeof(*subhdr));

    psession->gre_key        = htonl(pgre_session->ingress_gre_key);
    psession->gre_vrf_id     = htonl(pgre_session->ingress_vrf_id);
    psession->gre_if_id      = htonl(pgre_session->ingress_intf_id);
    psession->gre_data_ip    = htonl(pgre_session->ingress_self_ip);
    psession->gre_gw_ip      = htonl(pgre_session->ingress_gw_ip);

    psession->sesn_client_ip = htonl(pgre_session->sesn_client_ip);
    psession->sesn_server_ip = htonl(pgre_session->sesn_server_ip);
    psession->sesn_protocol  = pgre_session->sesn_proto;
    psession->sesn_flags     = pgre_session->sesn_flags;
    psession->sesn_sport     = htons(pgre_session->sesn_sport);
    psession->sesn_dport     = htons(pgre_session->sesn_dport);
    psession->sesn_up_time   = htonl(pgre_session->sesn_up_time);

    psession->ipip_vrf_id    = htonl(pgre_session->egress_vrf_id);
    psession->ipip_if_id     = htonl(pgre_session->egress_intf_id);
    psession->ipip_gw_ip     = htonl(pgre_session->egress_gw_ip);
    psession->ipip_data_ip   = htonl(pgre_session->egress_self_ip);

    if (pgre_session->puser) {
        strncpy(psession->sesn_user_name, pgre_session->puser->user_name,
                sizeof(psession->sesn_user_name));
    }

    (*msg_count)++;
    (*msg_len) += sublen;

    return;
}

/**
 *
 * This function calls the gre session information
 * filler function for a specific gre gateway
 * @params pgre_gw     gre gateway structure pointer
 * @params hdr         buffer header
 * @params msg_len     message length pointer
 * @params msg_count   message count pointer
 *
 */
static void
jnx_gw_ctrl_fill_gw_gre_session_info(jnx_gw_ctrl_gre_gw_t * pgre_gw,
                                  jnx_gw_msg_header_t * hdr,
                                  uint16_t * msg_len, uint8_t * msg_count)
{
    uint16_t sublen = 0;
    jnx_gw_ctrl_vrf_t * pvrf = NULL;
    jnx_gw_ctrl_gre_session_t * pgre_session = NULL;


    pvrf = pgre_gw->pvrf;

    /* fill gre gateway session summary information */
    jnx_gw_ctrl_fill_gre_session_summary(pvrf, pgre_gw, hdr, msg_len, msg_count);

    sublen = sizeof(jnx_gw_msg_sub_header_t) +
        sizeof(jnx_gw_msg_ctrl_gre_sesn_stat_t);

    /* get the gateway's session information */
    while ((pgre_session = jnx_gw_ctrl_get_next_gre_session(NULL, pgre_gw,
                                                            NULL, NULL,
                                                            pgre_session,
                                                            TRUE))) {

        /* send the message if, we are crossing the
           buffer or, the message count */

        if (((*msg_count) > 250) ||
            (((*msg_len) + sublen) > JNX_GW_CTRL_MAX_PKT_BUF_SIZE)) {

            /* fill the header */
            hdr->count   = (*msg_count);
            hdr->msg_len = htons((*msg_len));
            hdr->more    = TRUE;
            jnx_gw_ctrl_mgmt_send_opcmd(hdr, JNX_GW_STAT_FETCH_MSG, (*msg_len));

            /* reset & initialize the header again */
            hdr->msg_type = JNX_GW_STAT_FETCH_MSG;
            (*msg_len)    = sizeof(*hdr);
            (*msg_count)  = 0;
        }

        /* now fill the gre session information */
        jnx_gw_ctrl_fill_gre_session_info(pgre_session, hdr, msg_len, msg_count);
    }
    return;
}

/**
 *
 * This function fills calls the gre gateway filler
 * function for a specific vrf
 * @params pvrf        vrf structure pointer
 * @params hdr         buffer header
 * @params msg_len     message length pointer
 * @params msg_count   message count pointer
 *
 */
static void 
jnx_gw_ctrl_fill_vrf_gre_session_info(jnx_gw_ctrl_vrf_t * pvrf,
                                   jnx_gw_msg_header_t * hdr,
                                   uint16_t *msg_len, uint8_t *msg_count)
{
    jnx_gw_ctrl_gre_gw_t * pgre_gw = NULL;

    /* fill vrf gre session summary information */
    jnx_gw_ctrl_fill_gre_session_summary(pvrf, NULL, hdr, msg_len, msg_count);

    while ((pgre_gw = jnx_gw_ctrl_get_next_gre_gw(pvrf, pgre_gw))) {
        jnx_gw_ctrl_fill_gw_gre_session_info(pgre_gw, hdr, msg_len, msg_count);
    }
    return;
}

/**
 * This function handles the gre session information 
 * request from the mangement module
 * @params preq_msg  request message subheader pointer
 */

static status_t
jnx_gw_ctrl_send_gre_session_info(jnx_gw_msg_sub_header_t * preq_msg, uint16_t msg_id)
{
    uint8_t msg_count = 0, req_type = 0;
    uint16_t sublen = 0, msg_len = 0;
    uint32_t vrf_id = 0, gre_key = 0, gw_ip = 0;
    jnx_gw_msg_header_t     *hdr = NULL;
    jnx_gw_msg_sub_header_t *subhdr = NULL;
    jnx_gw_ctrl_vrf_t       *pvrf = NULL;
    jnx_gw_ctrl_gre_gw_t    *pgre_gw = NULL;
    jnx_gw_ctrl_gre_session_t  *pgre_session = NULL;
    jnx_gw_msg_ctrl_gre_sesn_stat_t *psession = NULL;

    /* get the request information */
    req_type = preq_msg->sub_type;
    psession = (typeof(psession)) ((uint8_t*)preq_msg + sizeof(*preq_msg));

    /* set the response message header & subheader pointer */
    hdr           = (typeof(hdr))jnx_gw_ctrl.opcmd_buf;
    hdr->msg_type = JNX_GW_STAT_FETCH_MSG;
    hdr->msg_id   = msg_id;
    msg_len       = sizeof(*hdr);
    subhdr        = (typeof(subhdr))((uint8_t *)hdr + msg_len);

    /* get the request information */

    /* vrf is set, get the vrf structure pointer */
    if (req_type & JNX_GW_CTRL_VRF_SET) {

        vrf_id  = ntohl(psession->gre_vrf_id);

        if (!(pvrf = jnx_gw_ctrl_lookup_vrf(vrf_id))) {
            jnx_gw_log(LOG_DEBUG, "Routing instance %d lookup failed", vrf_id);
            goto gre_gw_ctrl_gre_session_resp_err;
        }
    }

    /* gateway is set, get the gateway structure pointer */
    if (req_type & JNX_GW_CTRL_GW_SET) {

        /* vrf is not set, return */
        if (!pvrf) {
            goto gre_gw_ctrl_gre_session_resp_err;
        }

        gw_ip = ntohl(psession->gre_gw_ip);

        /* gre gateway not found, return */
        if (!(pgre_gw = jnx_gw_ctrl_lookup_gre_gw(pvrf, gw_ip))) {
            jnx_gw_log(LOG_DEBUG, "GRE gateway %s in %d lookup failed",
                   JNX_GW_IP_ADDRA(gw_ip), vrf_id);
            goto gre_gw_ctrl_gre_session_resp_err;
        }
    }

    /* summary verbose */
    if (!(req_type & JNX_GW_CTRL_VERBOSE_SET)) {
        jnx_gw_ctrl_fill_gre_session_summary(pvrf, pgre_gw, hdr,
                                          &msg_len, &msg_count);

    } else if (req_type & JNX_GW_CTRL_GRE_KEY_SET) {
        /* gre key is set, get the gre session information */

        if (!pvrf || !pgre_gw) {
            goto gre_gw_ctrl_gre_session_resp_err;
        }

        gre_key  = ntohl(psession->gre_key);

        if (!(pgre_session = jnx_gw_ctrl_lookup_gre_session(pvrf, pgre_gw,
                                                         gre_key, TRUE))) {
            jnx_gw_log(LOG_DEBUG, "GRE session %d in %d lookup failed",
                       gre_key, vrf_id);
            goto gre_gw_ctrl_gre_session_resp_err;
        }

        jnx_gw_ctrl_fill_gre_session_info(pgre_session, hdr, &msg_len, &msg_count);

    } else {

        /* verbose extensive */

        if (pgre_gw) {
            /* gre gateway is specified */
            jnx_gw_ctrl_fill_gw_gre_session_info(pgre_gw, hdr, &msg_len,
                                              &msg_count);
        } else if (pvrf) {
            /* vrf is specified */
            jnx_gw_ctrl_fill_vrf_gre_session_info(pvrf, hdr,
                                               &msg_len, &msg_count);
        } else {

            /* fill control agent gre session summary information */
            jnx_gw_ctrl_fill_gre_session_summary(NULL, NULL, hdr, &msg_len,
                                              &msg_count);

            /* get all the vrfs session information */
            while ((pvrf = jnx_gw_ctrl_get_next_vrf(pvrf))) {
                jnx_gw_ctrl_fill_vrf_gre_session_info(pvrf, hdr, &msg_len,
                                                   &msg_count);
            }
        }
    }

    hdr->msg_len  = htons(msg_len);
    hdr->count    = msg_count;
    hdr->more     = FALSE;

    jnx_gw_ctrl_mgmt_send_opcmd(hdr, JNX_GW_STAT_FETCH_MSG, msg_len);
    return EOK;

gre_gw_ctrl_gre_session_resp_err:
    msg_count++;
    sublen            = sizeof(*subhdr);
    subhdr->sub_type  = req_type;
    subhdr->err_code  = JNX_GW_MSG_ERR_RESOURCE_UNAVAIL;
    subhdr->length    = htons(sublen);
    msg_len          += sublen;
    hdr->msg_len      = htons(msg_len);
    hdr->count        = msg_count;
    hdr->more         = FALSE;

    jnx_gw_log(LOG_DEBUG, "GRE session stat fetch error response sent");
    jnx_gw_ctrl_mgmt_send_opcmd(hdr, JNX_GW_STAT_FETCH_MSG, msg_len);
    return EOK;
}

/**
 * This function fills the ipip tunnel summary information
 * for a vrf
 * @params pvrf      vrf structure pointer
 * @params hdr       response buffer header pointer
 * @params msg_len   message length pointer
 * @params msg_count message count pointer
 */
static void
jnx_gw_ctrl_fill_ipip_tunnel_sum_info(jnx_gw_ctrl_vrf_t * pvrf,
                                      jnx_gw_msg_header_t * hdr,
                                      uint16_t * msg_len,
                                      uint8_t * msg_count)
{
    uint16_t sublen = 0;
    jnx_gw_msg_sub_header_t * subhdr;
    jnx_gw_msg_ctrl_ipip_sum_stat_t * psum;

    /* set summary information */
    subhdr = (typeof(subhdr))((uint8_t *)hdr + (*msg_len));
    sublen = sizeof(*subhdr) + sizeof(*psum);
    subhdr->sub_type = JNX_GW_FETCH_IPIP_SESN_SUM;
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
    subhdr->length   = htons(sublen);

    psum = (typeof(psum))((uint8_t *)subhdr + sizeof(*subhdr));
    memset(psum, 0, sizeof(psum));

    if (pvrf) {
        psum->ipip_vrf_id            = htonl(pvrf->vrf_id);
        psum->ipip_tun_count         = htonl(pvrf->ipip_gw_count);
        psum->ipip_active_sesn_count = htonl(pvrf->gre_active_sesn_count);
        psum->ipip_sesn_count        = htonl(pvrf->gre_sesn_count);
    } else {
        psum->ipip_vrf_id            = 0xFFFFFFFF;
        psum->ipip_tun_count         = htonl(jnx_gw_ctrl.ipip_tunnel_count);
        psum->ipip_active_sesn_count = htonl(jnx_gw_ctrl.gre_active_sesn_count);
        psum->ipip_sesn_count        = htonl(jnx_gw_ctrl.gre_sesn_count);
    }

    /* update the subheader offset */
    (*msg_len) += sublen;
    (*msg_count)++;
    return;
}

/**
 * This function fills the ipip tunnel information
 * for a ipip gateway
 * @params pipip_gw  ipip gateway structure pointer
 * @params hdr       response buffer header pointer
 * @params msg_len   message length pointer
 * @params msg_count message count pointer
 */
static void
jnx_gw_ctrl_fill_ipip_tunnel_info(jnx_gw_ctrl_ipip_gw_t * pipip_gw,
                                  jnx_gw_msg_header_t * hdr,
                                  uint16_t *msg_len, uint8_t *msg_count)
{
    uint16_t sublen;
    jnx_gw_msg_sub_header_t      *subhdr;
    jnx_gw_msg_ctrl_ipip_sesn_stat_t  *psesn = NULL;


    sublen = sizeof(*subhdr) + sizeof(*psesn);
    subhdr = (typeof(subhdr)) ((uint8_t *)hdr + (*msg_len));

    subhdr->sub_type = JNX_GW_FETCH_IPIP_SESN;
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
    subhdr->length   = htons(sublen);


    psesn = (typeof(psesn)) ((uint8_t *)subhdr + sizeof(*subhdr));

    psesn->ipip_vrf_id      = htonl(pipip_gw->ipip_vrf_id);
    psesn->ipip_gw_ip       = htonl(pipip_gw->ipip_gw_ip);
    psesn->ipip_sesn_count  = htonl(pipip_gw->gre_sesn_count);
    psesn->ipip_active_sesn_count = htonl(pipip_gw->gre_active_sesn_count);

    (*msg_len) += sublen;
    (*msg_count)++;
    return;
}

/**
 * This function calls the  the ipip tunnel information
 * filler function for all the vrfs 
 * @params hdr       response buffer header pointer
 * @params msg_len   message length pointer
 * @params msg_count message count pointer
 */
static void
jnx_gw_ctrl_fill_all_ipip_tun_info(jnx_gw_ctrl_vrf_t * pvrf,
                                   jnx_gw_msg_header_t * hdr,
                                   uint16_t *msg_len, uint8_t *msg_count)
{
    uint16_t sublen, vrf_flag = FALSE;
    jnx_gw_ctrl_ipip_gw_t * pipip_gw;

    /* print control pic ipip tunnel summary */
    if (!pvrf) {
        jnx_gw_ctrl_fill_ipip_tunnel_sum_info(pvrf, hdr, msg_len, msg_count);
    }

    sublen = sizeof(jnx_gw_msg_sub_header_t *) +
        sizeof(jnx_gw_msg_ctrl_ipip_sesn_stat_t);

    if (pvrf != NULL) {
        vrf_flag = TRUE;
        goto vrf_label;
    }
    /* for every vrf, print the information */

    while ((pvrf = jnx_gw_ctrl_get_next_vrf(pvrf))) {

vrf_label:
        jnx_gw_ctrl_fill_ipip_tunnel_sum_info(pvrf, hdr, msg_len, msg_count);

        pipip_gw = NULL;

        while ((pipip_gw = jnx_gw_ctrl_get_next_ipip_gw(pvrf, pipip_gw))) {

            /* send the message if, we are crossing the
               buffer or, the message count */
            if (((*msg_count) > 250) ||
                (((*msg_len) + sublen) > JNX_GW_CTRL_MAX_PKT_BUF_SIZE)) {

                /* fill the header */
                hdr->count   = (*msg_count);
                hdr->msg_len = htons(*msg_len);
                hdr->more    = TRUE;
                jnx_gw_ctrl_mgmt_send_opcmd(hdr, JNX_GW_STAT_FETCH_MSG,
                                            (*msg_len));

                /* reset & initialize the header again */
                hdr->msg_type = JNX_GW_STAT_FETCH_MSG;
                (*msg_len)    = sizeof(*hdr);
                (*msg_count)  = 0;

            }

            /* fill the ipip tunnel gateway info */
            jnx_gw_ctrl_fill_ipip_tunnel_info(pipip_gw, hdr,
                                              msg_len, msg_count);
        }

        if (vrf_flag == TRUE) {
            break;
        }
    }
    return;
}

/**
 * This function handles the response to the
 * show ipip gateway reqest from the management
 * module
 * @params preq_msg   request message buffer pointer
 */
static status_t
jnx_gw_ctrl_send_ipip_tunnel_info(jnx_gw_msg_sub_header_t * preq_msg, uint16_t msg_id)
{
    uint8_t msg_count = 0, req_type = 0;
    uint16_t sublen = 0, msg_len = 0;
    uint32_t vrf_id = 0, gw_ip = 0;
    jnx_gw_ctrl_vrf_t           *pvrf = NULL;
    jnx_gw_ctrl_ipip_gw_t       *pipip_gw = NULL;
    jnx_gw_msg_header_t         *hdr = NULL;
    jnx_gw_msg_sub_header_t     *subhdr = NULL;
    jnx_gw_msg_ctrl_ipip_sesn_stat_t *psesn = NULL;


    req_type = preq_msg->sub_type;
    psesn    = (typeof(psesn)) ((uint8_t*)preq_msg + sizeof(*preq_msg));
    sublen   = ntohs(subhdr->length);

    /* fill up the response message */
    hdr = (typeof(hdr))jnx_gw_ctrl.opcmd_buf;

    memset(hdr, 0, sizeof(*hdr));
    hdr->msg_type = JNX_GW_STAT_FETCH_MSG;
    hdr->msg_id   = msg_id;
    msg_len       = sizeof(*hdr);
    subhdr        = (typeof(subhdr))
        ((uint8_t *)hdr + msg_len);

    /* if vrf id is passed, get the vrf structure */
    if (req_type & JNX_GW_CTRL_VRF_SET) {
        vrf_id  = ntohl(psesn->ipip_vrf_id);
        if (!(pvrf = jnx_gw_ctrl_lookup_vrf(vrf_id))) {
            jnx_gw_log(LOG_DEBUG, "Routing instance %d lookup failed", vrf_id);
            goto jnx_gw_ctrl_ipip_resp_err;
        }
    }

    if (req_type & JNX_GW_CTRL_GW_SET) {

        gw_ip = ntohl(psesn->ipip_gw_ip);

        if (!(pipip_gw = jnx_gw_ctrl_lookup_ipip_gw(pvrf, gw_ip))) {

            jnx_gw_log(LOG_DEBUG, "IPIP gateway %s in vrf %d lookup failed",
                   JNX_GW_IP_ADDRA(gw_ip), vrf_id);
            goto jnx_gw_ctrl_ipip_resp_err;
        }
    }

    /* print summary */
    if (!(req_type & JNX_GW_CTRL_VERBOSE_SET)) {

        jnx_gw_ctrl_fill_ipip_tunnel_sum_info(pvrf, hdr, &msg_len, &msg_count);

    } else if (pipip_gw) {

        jnx_gw_ctrl_fill_ipip_tunnel_info(pipip_gw, hdr, &msg_len, &msg_count);

    } else  {

        jnx_gw_ctrl_fill_all_ipip_tun_info(pvrf, hdr, &msg_len, &msg_count);

    }

    if (msg_count) {
        /* fill the header */
        hdr->msg_len   = htons(msg_len);
        hdr->count     = msg_count;
        hdr->more      = FALSE;
        jnx_gw_ctrl_mgmt_send_opcmd(hdr, JNX_GW_STAT_FETCH_MSG, msg_len);

    }
    return EOK;

jnx_gw_ctrl_ipip_resp_err:
    msg_count++;
    sublen            = sizeof(*subhdr);
    subhdr->sub_type  = req_type;
    subhdr->err_code  = JNX_GW_MSG_ERR_RESOURCE_UNAVAIL;
    subhdr->length    = htons(sublen);
    msg_len          += sublen;
    hdr->msg_len      = htons(msg_len);
    hdr->count        = msg_count;
    hdr->more         = FALSE;
    jnx_gw_log(LOG_DEBUG, "Fetch IPIP stat command error response sent");
    jnx_gw_ctrl_mgmt_send_opcmd(hdr, JNX_GW_STAT_FETCH_MSG, msg_len);
    return EOK;
}

/**
 *
 * This function fills in the user  information
 * to the op command send buffer
 * @params puser       user structure pointer
 * @params hdr         buffer header
 * @params msg_len     message length pointer
 * @params msg_count   message count pointer
 *
 */
static void
jnx_gw_ctrl_fill_user_info(jnx_gw_ctrl_user_t * puser,
                           jnx_gw_msg_header_t * hdr,
                           uint16_t * msg_len, uint8_t  * msg_count)
{
    uint32_t sublen;
    jnx_gw_msg_sub_header_t     *subhdr = NULL;
    jnx_gw_msg_ctrl_user_stat_t *pstat = NULL;


    subhdr = (typeof(subhdr)) ((uint8_t *)hdr + (*msg_len));
    sublen = sizeof(*subhdr) + sizeof(*pstat);

    subhdr->sub_type = JNX_GW_FETCH_USER_STAT;
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
    subhdr->length   = htons(sublen);

    pstat = (typeof(pstat)) ((uint8_t *)subhdr + sizeof(*subhdr));

    strncpy(pstat->user_name, puser->user_name,
            sizeof(pstat->user_name));

    pstat->user_active_sesn_count = htonl(puser->gre_active_sesn_count);
    pstat->user_sesn_count        = htonl(puser->gre_sesn_count);

    (*msg_count)++;
    (*msg_len) += sublen;
    return;
}

/**
 *
 * This function handles the show jnx-gateway user 
 * request messages from the management application
 * @params msg       message header pointer
 *
 */
static status_t
jnx_gw_ctrl_send_user_info(jnx_gw_msg_sub_header_t * preq_msg, uint16_t msg_id)
{
    uint8_t msg_count = 0, req_type = 0;
    uint16_t sublen = 0, msg_len = 0;
    jnx_gw_ctrl_user_t          *puser = NULL;
    jnx_gw_msg_header_t         *hdr = NULL;
    jnx_gw_msg_sub_header_t     *subhdr = NULL;
    jnx_gw_msg_ctrl_user_stat_t *pstat = NULL;

    hdr = (typeof(hdr))jnx_gw_ctrl.opcmd_buf;

    memset(hdr, 0, sizeof(*hdr));

    hdr->msg_type = JNX_GW_STAT_FETCH_MSG;
    hdr->msg_id   = msg_id;
    msg_len       = sizeof(*hdr);
    subhdr        = (typeof(subhdr)) ((uint8_t *)hdr + msg_len);
    sublen        = sizeof(*subhdr) + sizeof(*pstat);

    pstat  = (typeof(pstat)) ((uint8_t*)preq_msg + sizeof(*preq_msg));
    sublen = ntohs(subhdr->length);

    /* the user name is provided */
    if (strlen(pstat->user_name)) {

        if (!(puser = jnx_gw_ctrl_lookup_user(pstat->user_name))) {
            jnx_gw_log(LOG_DEBUG, "User \"%s\" lookup failed",
                       pstat->user_name);
            goto jnx_gw_ctrl_user_resp_err;
        }

        jnx_gw_ctrl_fill_user_info(puser, hdr, &msg_len, &msg_count);
    } else {

        /* othewise, get all users stats */
        while ((puser = jnx_gw_ctrl_get_next_user(puser))) {

            /* send the message if, we are crossing the
               buffer or, the message count */
            if ((msg_count > 250) ||
                ((msg_len + sublen) > JNX_GW_CTRL_MAX_PKT_BUF_SIZE)) {

                hdr->count     = msg_count;
                hdr->msg_len   = htons(msg_len);
                hdr->more      = FALSE;
                jnx_gw_ctrl_mgmt_send_opcmd(hdr, JNX_GW_STAT_FETCH_MSG,
                                            msg_len);

                /* reset & initialize the header again */
                hdr->msg_type = JNX_GW_STAT_FETCH_MSG;
                msg_len       = sizeof(*hdr);
                subhdr        = (typeof(subhdr)) ((uint8_t *)hdr + msg_len);
                msg_count     = 0;
            }

            jnx_gw_ctrl_fill_user_info(puser, hdr, &msg_len, &msg_count);
        }
    }
    hdr->msg_len = htons(msg_len);
    hdr->count   = msg_count;
    hdr->more    = FALSE;
    jnx_gw_ctrl_mgmt_send_opcmd(hdr, JNX_GW_STAT_FETCH_MSG, msg_len);
    return EOK;

jnx_gw_ctrl_user_resp_err:
    sublen            = sizeof(*subhdr);
    subhdr->sub_type  = req_type;
    subhdr->err_code  = JNX_GW_MSG_ERR_RESOURCE_UNAVAIL;
    subhdr->length    = htons(sublen);
    msg_len          += sublen;
    hdr->msg_len      = htons(msg_len);
    hdr->count        = msg_count;
    hdr->more         = FALSE;
    jnx_gw_log(LOG_DEBUG, "User stats fetch command error response sent");
    jnx_gw_ctrl_mgmt_send_opcmd(hdr, JNX_GW_STAT_FETCH_MSG, msg_len);
    return EOK;
}

/**
 *
 * This function handles the operational command
 * request messages from the management application
 * @params msg       message header pointer
 *
 */
static status_t 
jnx_gw_ctrl_handle_opcmd(jnx_gw_msg_header_t * msg)
{
    uint8_t count = 0, req_type = 0;
    uint16_t sublen = 0;
    jnx_gw_msg_sub_header_t * subhdr;

    jnx_gw_log(LOG_DEBUG, "Operational command fetch request");

    JNX_GW_CTRL_CONFIG_READ_LOCK();
    count  = msg->count;
    subhdr = (typeof(subhdr))((uint8_t *)msg + sizeof(*msg));

    while (count) {

        sublen   = ntohs(subhdr->length);
        req_type = (subhdr->sub_type & 0x0F);

        switch(req_type) {
            case JNX_GW_FETCH_USER_STAT:
                jnx_gw_ctrl_send_user_info(subhdr, msg->msg_id);
                break;

            case JNX_GW_FETCH_GRE_SESN:
                jnx_gw_ctrl_send_gre_session_info(subhdr, msg->msg_id);
                break;

            case JNX_GW_FETCH_IPIP_SESN:
                jnx_gw_ctrl_send_ipip_tunnel_info(subhdr, msg->msg_id);
                break;

            default:
                break;
        }
        subhdr = (typeof(subhdr))((uint8_t*)subhdr + sublen);
        count--;
    }

    JNX_GW_CTRL_CONFIG_READ_UNLOCK();

    return EOK;
}

/***********************************************************
 *                                                         *
 *             CONFIG MESSAGE HANDLER FUNCTIONS            *
 *                                                         *
 ***********************************************************/

/**
 *
 * This function handles the control policy add/delete messages 
 * posted by the management application
 * @params msg       message header pointer
 * @params add_flag  add/delete
 *
 */
static status_t
jnx_gw_ctrl_config_ctrl(jnx_gw_msg_ctrl_config_ctrl_t * pmsg,
                        uint8_t add_flag)
{
    int32_t vrf_id ;
    jnx_gw_ctrl_vrf_t * pvrf;
    jnx_gw_ctrl_policy_t * pctrl, config;

    vrf_id  = ntohl(pmsg->ctrl_vrf_id);

    if (vrf_id == JNX_GW_INVALID_VRFID) {
        return (EOK);
    }

    config.ctrl_vrf_id  = ntohl(pmsg->ctrl_vrf_id);
    config.ctrl_addr    = ntohl(pmsg->ctrl_addr);
    config.ctrl_mask    = ntohl(pmsg->ctrl_mask);

    if (!(pvrf = jnx_gw_ctrl_lookup_vrf(config.ctrl_vrf_id))) {
        if (add_flag == JNX_GW_CONFIG_DELETE) {
            return EOK;
        }
        if (!(pvrf = jnx_gw_ctrl_add_vrf(config.ctrl_vrf_id, NULL))) {
            return EFAIL;
        }
    }

    pctrl = jnx_gw_ctrl_lookup_ctrl(pvrf, config.ctrl_addr, config.ctrl_mask);

    switch (add_flag) {

        case JNX_GW_CONFIG_ADD:
            if (pctrl != NULL) return EFAIL;
            jnx_gw_ctrl_add_ctrl(pvrf, &config);
            break;

        case JNX_GW_CONFIG_DELETE:
            if (pctrl == NULL) return EFAIL;
            jnx_gw_ctrl_delete_ctrl(pvrf, pctrl);
            break;

        case JNX_GW_CONFIG_MODIFY:
            if (pctrl == NULL) return EFAIL;
            jnx_gw_ctrl_modify_ctrl(pvrf, pctrl, &config);
            break;

        default:
            return EFAIL;
    }

    return EOK;
}

/**
 *
 * This function handles the static gre session add/delete
 * messages posted by the management application
 * @params msg       message header pointer
 * @params add_flag  add/delete
 *
 */

static status_t
jnx_gw_ctrl_config_data(jnx_gw_msg_ctrl_config_data_t * pmsg __unused,
                        uint8_t add_flag __unused)
{
    return EOK;
}

/**
 *
 * This function handles the user profile add/delete messages 
 * posted by the management application
 * @params msg       message header pointer
 * @params add_flag  add/delete
 *
 */

static status_t
jnx_gw_ctrl_config_user(jnx_gw_msg_ctrl_config_user_t * pmsg,
                        uint8_t add_flag)
{
    int32_t vrf_id;
    jnx_gw_ctrl_vrf_t  * pvrf;
    jnx_gw_ctrl_user_t * puser;
    jnx_gw_ctrl_ipip_gw_t * pipip_gw = NULL;
    static jnx_gw_ctrl_user_t config;

    vrf_id = ntohl(pmsg->user_evrf_id);
    if (vrf_id == JNX_GW_INVALID_VRFID) {
        return (EOK);
    }

    strncpy (config.user_name, pmsg->user_name, sizeof(config.user_name));

    config.user_evrf_id    = vrf_id;
    config.user_addr       = ntohl(pmsg->user_addr);
    config.user_mask       = ntohl(pmsg->user_mask);
    config.user_ipip_gw_ip = ntohl(pmsg->user_ipip_gw_ip);


    /* find the egress vrf */
    if (!(pvrf = jnx_gw_ctrl_lookup_vrf(config.user_evrf_id))) {

        if (add_flag == JNX_GW_CONFIG_DELETE) {
            return EOK;
        }

        /* if it is a add message, add the vrf */
        if (!(pvrf = jnx_gw_ctrl_add_vrf(config.user_evrf_id, NULL))) {
            return EFAIL;
        }
    }

    /* try to find the ipip gateway */
    if ((config.user_ipip_gw_ip) &&
        (!(pipip_gw =
           jnx_gw_ctrl_lookup_ipip_gw(pvrf, config.user_ipip_gw_ip)))) {

        if (add_flag == JNX_GW_CONFIG_DELETE) {
            return EOK;
        }

        /* add, if not present */
        if (!(pipip_gw = 
              jnx_gw_ctrl_add_ipip_gw(pvrf, config.user_ipip_gw_ip))) {
            return EFAIL;
        }
    }

    /* find the user entry */
    puser = jnx_gw_ctrl_lookup_user(config.user_name);

    switch (add_flag) {

        case JNX_GW_CONFIG_ADD:
            /* user is present */
            if (puser != NULL) return EFAIL;
            jnx_gw_ctrl_add_user(pvrf, pipip_gw, &config);
            break;

        case JNX_GW_CONFIG_DELETE:
            /* user is not present */
            if (puser == NULL) return EFAIL;
            jnx_gw_ctrl_delete_user(pvrf, puser);
            break;

        case JNX_GW_CONFIG_MODIFY:
            /* user is not present */
            if (puser == NULL) return EFAIL;
            jnx_gw_ctrl_modify_user(pvrf, puser, &config);
            break;

        default:
            return EFAIL;
    }
    return EOK;
}

/**
 *
 * This function handles the data pic add/delete messages 
 * posted by the management application
 * @params msg       message header pointer
 * @params add_flag  add/delete
 *
 */
static status_t
jnx_gw_ctrl_config_pic(jnx_gw_msg_ctrl_config_pic_t * pmsg, uint8_t add_flag)
{
    jnx_gw_ctrl_data_pic_t * pdata_pic = NULL;

    pdata_pic = jnx_gw_ctrl_lookup_data_pic(pmsg->pic_name, NULL);

    switch (add_flag) {

        case JNX_GW_CONFIG_ADD:
            /* data pic is present */
            if (pdata_pic != NULL) return EFAIL;
            jnx_gw_ctrl_add_data_pic(pmsg);
            break;

        case JNX_GW_CONFIG_DELETE:
            /* data pic is not present */
            if (pdata_pic == NULL) return EFAIL;
            jnx_gw_ctrl_delete_data_pic(pdata_pic);
            break;

        default:
            return EFAIL;
    }

    return EOK;
}

/**
 *
 * This function handles the vrf add delete notification
 * for self pic, ifls
 * @params msg       message pointer
 * @params add_flag  add/delete
 *
 */
static status_t
jnx_gw_ctrl_config_vrf(jnx_gw_msg_ctrl_config_vrf_t * pmsg, uint8_t add_flag)
{
    int32_t vrf_id;
    uint32_t oval = 1;
    uint8_t sig_flag = 0, rtr_flags = 0;
    uint16_t sin_port = JNX_GW_CTRL_GRE_SIG_PORT_NUM;
    struct sockaddr_in sock;
    jnx_gw_ctrl_vrf_t * pvrf = NULL;

    vrf_id = ntohl(pmsg->vrf_id);

    if (vrf_id == JNX_GW_INVALID_VRFID) {
        return (EOK);
    }

    jnx_gw_log(LOG_INFO, "VRF \"%s\" (%d) %s, %s request",
               pmsg->vrf_name, vrf_id, (add_flag == JNX_GW_CONFIG_ADD) ?
               "add" : "delete",
               (pmsg->sig_flag == JNX_GW_CTRL_SIGNALING_ENABLE) ?
               "signaling" : (pmsg->sig_flag == JNX_GW_CTRL_SIGNALING_NOOP) ?
               "no-op" : "no-signaling");

    /* get the vrf */
    pvrf = jnx_gw_ctrl_lookup_vrf(vrf_id);

    switch (add_flag) {

        case JNX_GW_CONFIG_ADD:

            /* create the vrf */
            if (!(pvrf = jnx_gw_ctrl_add_vrf(vrf_id, pmsg->vrf_name))) {
                return EFAIL;
            }

            pvrf->vrf_status = JNX_GW_CTRL_STATUS_UP;

            sig_flag  = pmsg->sig_flag;
            rtr_flags = pmsg->flags;


            /*
             * either signaling is not required,
             * or the signaling is already up, do not
             * set up the signaling again
             */
            if (!(sig_flag == JNX_GW_CTRL_SIGNALING_ENABLE) ||
                (pvrf->ctrl_fd > 0)) {
                break;
            }

            /* open the receive socket */
            pvrf->vrf_sig_status = JNX_GW_CTRL_STATUS_INIT;

            /* now open the socket, & bind to the listen port on the vrf */

            /* could not open the socket */
            if ((pvrf->ctrl_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
                break;
            }

            /* set source socket port number */
            memset(&sock, 0, sizeof(sock));
            sock.sin_family = AF_INET;
            sock.sin_port   = htons(sin_port);

            /* attach the socket to the vrf instance */
            if (setsockopt(pvrf->ctrl_fd, IPPROTO_IP, IP_RTBL_INDEX,
                           &pvrf->vrf_id, sizeof(pvrf->vrf_id))) {
                close(pvrf->ctrl_fd);
                pvrf->ctrl_fd = 0;
                break;
            }

            if (setsockopt(pvrf->ctrl_fd,SOL_SOCKET,SO_REUSEADDR,
                           &oval,sizeof(oval)) < 0) {
                close(pvrf->ctrl_fd);
                pvrf->ctrl_fd = 0;
                break;
            }

            if (setsockopt(pvrf->ctrl_fd,SOL_SOCKET,SO_REUSEPORT,
                           &oval,sizeof(oval)) < 0) {
                close(pvrf->ctrl_fd);
                pvrf->ctrl_fd = 0;
                break;
            }

            /* bind to the listen port */
            if (bind(pvrf->ctrl_fd, (struct sockaddr *)&sock,
                     sizeof(struct sockaddr_in)) < 0) {
                close(pvrf->ctrl_fd);
                pvrf->ctrl_fd = 0;
                break;
            }

            /* set this to non-block receive */
            ioctl(pvrf->ctrl_fd, F_SETFD, O_NONBLOCK);

            jnx_gw_log(LOG_INFO, "VRF \"%s\" (%d) signaling schedule",
                       pvrf->vrf_name, pvrf->vrf_id);
            /* select a recv thread */
            jnx_gw_ctrl_attach_to_thread(pvrf);
            break;

        case JNX_GW_CONFIG_DELETE:

            /* scheduled to be deleted on the packet receive thread */
            if ((pvrf == NULL) || 
                (pvrf->vrf_status == JNX_GW_CTRL_STATUS_DELETE)) {
                break;
            }

            /* if signaling status is not up, delete the entry */
            if (pvrf->ctrl_fd == 0) {
                jnx_gw_ctrl_delete_vrf(pvrf);
                break;
            }

            /*
             * otherwise, schedule it to be deleted 
             * on the context of the packet receive thread
             */
            jnx_gw_log(LOG_INFO, "VRF \"%s\" (%d) no-signaling schedule",
                       pvrf->vrf_name, pvrf->vrf_id);
            pvrf->vrf_status = JNX_GW_CTRL_STATUS_DELETE;
            jnx_gw_ctrl_detach_from_thread(pvrf);

            break;

        default:
            return EOK;
    }

    return EOK;
}

/**
 *
 * This function handles the service pic interface add/delete messages 
 * posted by the management application
 * @params msg       message pointer
 * @params add_flag  add/delete
 *
 */
static status_t
jnx_gw_ctrl_config_intf(jnx_gw_msg_ctrl_config_intf_t * pmsg, uint8_t add_flag)
{
    int32_t vrf_id;
    uint32_t intf_index, intf_ip, intf_subunit, pic_flag = TRUE;
    char pic_name[JNX_GW_STR_SIZE];
    jnx_gw_ctrl_vrf_t * pvrf = NULL;
    jnx_gw_ctrl_intf_t * pintf = NULL;
    jnx_gw_ctrl_policy_t   * pctrl = NULL;
    jnx_gw_ctrl_data_pic_t * pdata_pic = NULL;

    if (strncmp(pmsg->intf_name,  MSP_PREFIX, strlen(MSP_PREFIX))) {
        return EOK;
    }

    strlcpy(pic_name, pic_flag ? pmsg->intf_name : PCONN_HOSTNAME_RE,
	    sizeof(pic_name));

    /* does not have the data pic here, return */
    if ((pdata_pic = jnx_gw_ctrl_lookup_data_pic(pic_name, NULL))
        == NULL) {
        return EOK;
    }

    intf_index = ntohl(pmsg->intf_index);
    vrf_id = ntohl(pmsg->intf_vrf_id);
    intf_subunit = ntohl(pmsg->intf_subunit);

    if (vrf_id == JNX_GW_INVALID_VRFID) {
        return (EOK);
    }

    /* get the vrf */
    pvrf = jnx_gw_ctrl_lookup_vrf(vrf_id);

    pintf = jnx_gw_ctrl_lookup_intf(pvrf, pdata_pic, NULL, intf_index);

    switch (add_flag) {

        case JNX_GW_CONFIG_ADD:

            /* get the vrf */
            if (!(pvrf = jnx_gw_ctrl_add_vrf(vrf_id, NULL))) {
                /* could not create the vrf */
                return EFAIL;
            }

            if (pintf == NULL) {
                /* assign an ip address to the data pic
                 * interface, source the ip address from the
                 * vrf control policy
                 */

                /* could not get a valid ctrl entry */
                if (!(pctrl = jnx_gw_ctrl_add_intf_to_ctrl(pvrf, pdata_pic,
                                                           &intf_ip))) {
                    break;
                }

            }

            pintf = jnx_gw_ctrl_add_intf(pvrf, pdata_pic, pintf, pctrl,
                                         pmsg->intf_name, intf_index,
                                         intf_subunit, intf_ip);

            /* add the gateways to the data pic */
            jnx_gw_ctrl_send_ipip_msg_to_data_pic(pvrf, pdata_pic, pintf,
                                                  JNX_GW_ADD_IP_IP_SESSION);
            break;

        case JNX_GW_CONFIG_DELETE:

            if (!pintf) break;

            /* delete the gateways from the data pic */
            jnx_gw_ctrl_send_ipip_msg_to_data_pic(pvrf, pdata_pic, pintf,
                                                  JNX_GW_DEL_IP_IP_SESSION);

            /* delete the interface entry from the data pic */
            jnx_gw_ctrl_delete_intf(pvrf, pdata_pic, pintf);

            break;

        default:
            return EFAIL;
    }

    return EOK;
}

/**
 *
 * This function handles the config add/delete messages 
 * posted by the management application
 * @params msg  message header pointer
 *
 */

static status_t
jnx_gw_ctrl_handle_config_msg(jnx_gw_msg_header_t * hdr)
{
    uint8_t  msg_count = 0;
    uint16_t sublen = 0;
    void * pconfig_msg = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;

    msg_count = hdr->count;
    subhdr    = (typeof(subhdr))((uint8_t*)hdr + sizeof(*hdr));


    JNX_GW_CTRL_CONFIG_WRITE_LOCK();

    while (msg_count) {
        sublen      = ntohs(subhdr->length);
        pconfig_msg = ((uint8_t*)subhdr + sizeof(*subhdr));

        switch(hdr->msg_type) {

            case JNX_GW_CONFIG_CTRL_MSG:
                jnx_gw_ctrl_config_ctrl(pconfig_msg, subhdr->sub_type);
                break;

            case JNX_GW_CONFIG_USER_MSG:
                jnx_gw_ctrl_config_user(pconfig_msg, subhdr->sub_type);
                break;

            case JNX_GW_CONFIG_PIC_MSG:
                jnx_gw_ctrl_config_pic(pconfig_msg, subhdr->sub_type);
                break;

            case JNX_GW_CONFIG_DATA_MSG:
                jnx_gw_ctrl_config_data(pconfig_msg, subhdr->sub_type);
                break;

            case JNX_GW_CONFIG_INTF_MSG:
                jnx_gw_ctrl_config_intf(pconfig_msg, subhdr->sub_type);
                break;

            case JNX_GW_CONFIG_VRF_MSG:
                jnx_gw_ctrl_config_vrf(pconfig_msg, subhdr->sub_type);
                break;

            default:
                jnx_gw_log(LOG_ERR, "Unknown configuration message");
                break;
        }
        subhdr = (typeof(subhdr))((uint8_t*)subhdr + sublen);
        msg_count--;
    }
    JNX_GW_CTRL_CONFIG_WRITE_UNLOCK();
    return EOK;
}

/**
 * This function handles the config messages & operational
 * command messages posted by the management application
 * @params session   pconn session pointer
 * @params ipc_msg   message buffer
 * @params coookie   cookie
 */

status_t
jnx_gw_ctrl_mgmt_msg_handler(pconn_session_t * session __unused,
                             ipc_msg_t *  ipc_msg, void * cookie __unused)
{
    jnx_gw_msg_header_t * msg;

    msg = (typeof(msg))(ipc_msg->data);

    /* it can be either a config message,
       or a statistics fetch message */

    switch (msg->msg_type) {

        case JNX_GW_STAT_FETCH_MSG:
            jnx_gw_ctrl_handle_opcmd(msg);
            break;

        case JNX_GW_CONFIG_CTRL_MSG:
        case JNX_GW_CONFIG_DATA_MSG:
        case JNX_GW_CONFIG_PIC_MSG:
        case JNX_GW_CONFIG_USER_MSG:
        case JNX_GW_CONFIG_VRF_MSG:
        case JNX_GW_CONFIG_INTF_MSG:
        default:
            jnx_gw_ctrl_handle_config_msg(msg);
            break;
    }
    return EOK;
}

/**
 * This function handles the connection status events from
 * the management application
 * @params session   pconn session pointer
 * @params event     pconn event type
 * @params cookie    cookie
 */
void
jnx_gw_ctrl_mgmt_event_handler(pconn_session_t * session,
                               pconn_event_t event, void * cookie __unused)
{

    switch (event) {

        /* connection to the management app is up */
        case PCONN_EVENT_ESTABLISHED:
            jnx_gw_ctrl.mgmt_sesn  = session;
            jnx_gw_ctrl_init_mgmt();
            jnx_gw_log(LOG_INFO, "Management connect event");
            break;

            /* connection to the management app is down */
        case PCONN_EVENT_FAILED:
        case PCONN_EVENT_SHUTDOWN:
            /* mark the application state as init */ 
            jnx_gw_log(LOG_INFO, "Management shutdown event");
            jnx_gw_ctrl.ctrl_status = JNX_GW_CTRL_STATUS_INIT;
            jnx_gw_ctrl.mgmt_sesn = NULL;
            if (jnx_gw_ctrl.periodic_conn) {
                pconn_client_close(jnx_gw_ctrl.periodic_conn);
                jnx_gw_ctrl.periodic_conn = NULL;
            }
            break;

        default:
            jnx_gw_log(LOG_ERR, "Management unsupported event");
            break;
    }
    return;
}


/**
 * send back the opcommand response to the
 *  management agent
 */
static status_t 
jnx_gw_ctrl_mgmt_send_opcmd(jnx_gw_msg_header_t * hdr, uint8_t msg_type,
                            uint16_t msg_len)
{
    /* only valid messages */
    if (msg_len && jnx_gw_ctrl.mgmt_sesn) {
        pconn_server_send(jnx_gw_ctrl.mgmt_sesn, msg_type, hdr, msg_len);
    }
    return EOK;
}

/**
 * Set the control agent status to up,
 * on reeceipt of connection established
 * event from the management appliacation
 */
status_t
jnx_gw_ctrl_init_mgmt(void)
{
    jnx_gw_ctrl.ctrl_status = JNX_GW_CTRL_STATUS_UP;
    return EOK;
}

/**
 * Reset the connections to the management
 * appliaction
 */
status_t
jnx_gw_ctrl_destroy_mgmt(void)
{
    if (jnx_gw_ctrl.mgmt_conn) {
        pconn_server_shutdown(jnx_gw_ctrl.mgmt_conn); 
        jnx_gw_ctrl.mgmt_conn = NULL;
    }
    if (jnx_gw_ctrl.periodic_conn) {
        pconn_client_close(jnx_gw_ctrl.periodic_conn);
        jnx_gw_ctrl.periodic_conn = NULL;
    }
    return EOK;
}

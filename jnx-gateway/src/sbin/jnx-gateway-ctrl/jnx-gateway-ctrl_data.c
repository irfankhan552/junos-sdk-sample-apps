/*
 * $Id: jnx-gateway-ctrl_data.c 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-ctrl_data.c - data pic session management
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
 * @file jnx-gateway-ctrl_data.c
 * @brief
 * This file contains the routines for
 * interfacing with the data pic agent
 * running on RE/PICs.
 * It handles the messages received
 * from the data agent module & also
 * routines for sending messages to
 * the data pic modules
 */

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>

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

/**
 * This function handles the gre messages received from a 
 * data pic gateway agent module
 * sends appropriate response to the client gre 
 * gateway module
 * @params   pdata_pic data pic structure pointer
 * @params   pmsg      messge structure pointer
 */

static status_t
jnx_gw_ctrl_handle_data_gre_msg(jnx_gw_ctrl_data_pic_t * pdata_pic,
                                jnx_gw_msg_header_t * pmsg)
{
    uint8_t  count, msg_type;
    uint16_t sublen = 0, len;
    uint32_t gre_key, vrf_id, gw_ip;
    jnx_gw_ctrl_vrf_t * pvrf = NULL;
    jnx_gw_ctrl_gre_gw_t * pgre_gw = NULL;
    jnx_gw_msg_gre_info_t * pgre_info = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_ctrl_gre_session_t    * pgre_session = NULL;
    jnx_gw_msg_gre_add_session_t * padd_msg = NULL;
    jnx_gw_msg_gre_del_session_t * pdel_msg = NULL;

    JNX_GW_CTRL_CONFIG_READ_LOCK();

    len   = ntohs(pmsg->msg_len);
    subhdr = (typeof(subhdr))((uint8_t *) pmsg + sizeof(*pmsg));

    for (count = pmsg->count; (count);
         count--, sublen = ntohs(subhdr->length),
         subhdr = (typeof(subhdr))((uint8_t *)subhdr + sublen)) {

        /* get the tunnel info pointer */
        if (subhdr->sub_type == JNX_GW_ADD_GRE_SESSION) {

            padd_msg  = (typeof(padd_msg))((uint8_t *)subhdr + sizeof(*subhdr));
            pgre_info = (typeof(pgre_info))(&padd_msg->ing_tunnel_info);

        } else if (subhdr->sub_type == JNX_GW_DEL_GRE_SESSION) {

            pdel_msg  = (typeof(pdel_msg))((uint8_t *)subhdr + sizeof(*subhdr));
            pgre_info = (typeof(pgre_info))(&pdel_msg->gre_tunnel);

        } else { 
            jnx_gw_log(LOG_DEBUG, "Data agent \"%s\" unknown GRE message type",
                   pdata_pic->pic_name);
            continue;
        }

        /* extract the gre key, gre gateway ip and vrf id from
           the tunnel info */
        gre_key = ntohl(pgre_info->gre_key);
        gw_ip   = ntohl(pgre_info->gateway_ip);
        vrf_id  = ntohl(pgre_info->vrf);

        /* find the vrf */
        if ((pvrf = jnx_gw_ctrl_lookup_vrf(vrf_id)) == NULL) {
            jnx_gw_log(LOG_DEBUG, "Routing instance %d is absent", vrf_id);
            continue;
        }

        /* find the gateway */
        if ((pgre_gw = jnx_gw_ctrl_lookup_gre_gw(pvrf, gw_ip)) == NULL) {
            jnx_gw_log(LOG_DEBUG, "GRE Gateway \"%s\" is absent",
                       JNX_GW_IP_ADDRA(gw_ip));
            continue;
        }

        /* find the gre session */
        if ((pgre_session =
             jnx_gw_ctrl_lookup_gre_session(pvrf, pgre_gw, gre_key, TRUE))
            == NULL) {
            jnx_gw_log(LOG_DEBUG, "GRE Session \"%d\" is absent", gre_key);
            continue;
        }

        switch (subhdr->sub_type) {

            /* session add response message */
            case JNX_GW_ADD_GRE_SESSION:

                if (pgre_session->sesn_status != JNX_GW_CTRL_STATUS_INIT) {
                    continue;
                }
                /* if error code is set,
                 * set the session status to fail
                 */
                pgre_session->sesn_errcode = subhdr->err_code;

                if (subhdr->err_code == JNX_GW_MSG_ERR_NO_ERR) {
                    pgre_session->sesn_status = JNX_GW_CTRL_STATUS_UP;
                    msg_type               = JNX_GW_CTRL_GRE_SESN_TRANSMIT;
                    jnx_gw_log(LOG_DEBUG, "GRE Session setup %d successful"
                               " with data agent \"%s\"",
                               gre_key, pdata_pic->pic_name);
                }
                else {
                    pgre_session->sesn_status = JNX_GW_CTRL_STATUS_FAIL;
                    msg_type               = JNX_GW_CTRL_GRE_SESN_ERR;
                    jnx_gw_log(LOG_DEBUG, "GRE Session setup %d failed"
                               " with data agent \"%s\" %d",
                               gre_key, pdata_pic->pic_name, subhdr->err_code);
                }
                break;

            case JNX_GW_DEL_GRE_SESSION:

                if (pgre_session->sesn_status != JNX_GW_CTRL_STATUS_DOWN) {
                    continue;
                }
                /* if error code is set,
                 * set the session status to fail
                 */

                pgre_session->sesn_errcode = subhdr->err_code;

                if (subhdr->err_code == JNX_GW_MSG_ERR_NO_ERR) {
                    pgre_session->sesn_status = JNX_GW_CTRL_STATUS_DOWN;
                    msg_type               = JNX_GW_CTRL_GRE_SESN_DONE;
                    jnx_gw_log(LOG_DEBUG, "GRE Session shutdown %d successful"
                               " with data agent \"%s\"",
                               gre_key, pdata_pic->pic_name);
                }
                else {
                    pgre_session->sesn_status = JNX_GW_CTRL_STATUS_FAIL;
                    msg_type               = JNX_GW_CTRL_GRE_SESN_ERR;
                    jnx_gw_log(LOG_DEBUG, "GRE Session shutdown %d failed"
                               " with data agent \"%s\" %d",
                               gre_key, pdata_pic->pic_name, subhdr->err_code);
                }
                break;

            default:
                continue;
        }

        /* if the status is not up, clear up the gre session */
        if (pgre_session->sesn_status != JNX_GW_CTRL_STATUS_UP) {

            if (pgre_session->sesn_status != JNX_GW_CTRL_STATUS_DOWN) {
                jnx_gw_log(LOG_DEBUG, "GRE Session setup %d failed"
                           " with data agent \"%s\"",
                           gre_key, pdata_pic->pic_name);
            }

            jnx_gw_ctrl_delete_gre_session(pvrf, pgre_gw, pgre_session);
            continue;
        }
        /* fill the message to be sent to the client
         * gre gateway
         */
        jnx_gw_ctrl_fill_gw_gre_msg(msg_type, pvrf, pgre_gw, pgre_session);
    }

    JNX_GW_CTRL_CONFIG_READ_UNLOCK();

    /* now, push the messages to the client gre gateway */
    jnx_gw_log(LOG_DEBUG, "GRE session setup response to GRE gateways");
    jnx_gw_ctrl_send_gw_gre_msgs();

    return EOK;
}

/**
 * This function handles the ipip  messages received from a 
 * data pic gateway agent module
 * @params   pdata_pic data pic structure pointer
 * @params   pmsg      messge structure pointer
 */
static status_t
jnx_gw_ctrl_handle_data_ipip_msg(jnx_gw_ctrl_data_pic_t * pdata_pic,
                                 jnx_gw_msg_header_t * hdr)
{
    uint8_t  count;
    uint16_t sublen = 0, len;
    uint32_t vrf_id, gw_ip;
    jnx_gw_ctrl_vrf_t * pvrf = NULL;
    jnx_gw_ctrl_ipip_gw_t * pipip_gw = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_msg_ip_ip_info_t * pipip_info = NULL;
    jnx_gw_msg_ipip_add_tunnel_t * padd_msg = NULL;
    jnx_gw_msg_ipip_del_tunnel_t * pdel_msg = NULL;
    jnx_gw_ctrl_intf_t * pintf = NULL;

    JNX_GW_CTRL_CONFIG_WRITE_LOCK();

    len    = ntohs(hdr->msg_len);
    subhdr = (typeof(subhdr)) ((uint8_t *) hdr + sizeof(*hdr));

    for (count = hdr->count; (count);
         count--, sublen = ntohs(subhdr->length),
         subhdr = (typeof(subhdr)) ((uint8_t *)subhdr + sublen)) {


        /* if error continue */
        if (subhdr->err_code != JNX_GW_MSG_ERR_NO_ERR) {
            if (subhdr->err_code != JNX_GW_MSG_ERR_IPIP_SESS_EXISTS) {
                jnx_gw_log(LOG_DEBUG, 
                           "Data agent \"%s\" IPIP tunnel add failed",
                           pdata_pic->pic_name);
            } else
                continue;
        }

        /* extract the ip ip info pointer */
        if (subhdr->sub_type == JNX_GW_ADD_IP_IP_SESSION) {
            padd_msg   = (typeof(padd_msg))((uint8_t *)subhdr +
                                            sizeof(*subhdr));
            pipip_info = &padd_msg->ipip_tunnel;

        } else if (subhdr->sub_type == JNX_GW_DEL_IP_IP_SESSION) {
            pdel_msg   = (typeof(padd_msg))((uint8_t *)subhdr +
                                            sizeof(*subhdr));
            pipip_info = &pdel_msg->ipip_tunnel;

        } else  {
            jnx_gw_log(LOG_DEBUG, "Data agent \"%s\" unknown IPIP message",
                       pdata_pic->pic_name);
            continue;
        }


        /* get the vrf and ipip gateway ip */
        vrf_id = ntohl(pipip_info->vrf);
        gw_ip  = ntohl(pipip_info->gateway_ip);

        /* find the vrf */
        if (!(pvrf = jnx_gw_ctrl_lookup_vrf(vrf_id))) {
            jnx_gw_log(LOG_DEBUG, "Routing instance %d is absent", vrf_id);
            continue;
        }

        /* find the gateway */
        if (!(pipip_gw = jnx_gw_ctrl_lookup_ipip_gw(pvrf, gw_ip))) {
            jnx_gw_log(LOG_DEBUG, "IPIP Gateway %s is absent",
                       JNX_GW_IP_ADDRA(gw_ip));
            continue;
        }

        /* get the interface on the same vrf for the data pic */
        pintf = NULL;

        while ((pintf = jnx_gw_ctrl_get_next_intf(pvrf, NULL, NULL, pintf))) {
            if (pintf->pdata_pic == pdata_pic)
                break;
        }

        if ((pintf) && (pintf->pdata_pic != pdata_pic)) {
            jnx_gw_log(LOG_DEBUG, "Data agent \"%s\" no interface on"
                       " routing instance %d", pdata_pic->pic_name,
                       vrf_id);
            pintf = NULL;
        }

        switch (subhdr->sub_type) {

            case JNX_GW_ADD_IP_IP_SESSION:

                /* this ipip gateway can be reached through this data pic,
                 * set the ipip gateway status to up
                 */
                pipip_gw->datapic_count++;

                if (pipip_gw->ipip_gw_status == JNX_GW_CTRL_STATUS_INIT) {

                    pipip_gw->ipip_gw_status = JNX_GW_CTRL_STATUS_UP;
                }

                /* also update the associate
                 * interface statistics
                 */
                /* this may change for native format egress tunnels, 
                   we may not need to count the interface references
                   to determine the status of the interface,
                   for now lets maintain this */
                if (pintf) {
                    pintf->ipip_tunnel_count++;

                    if (pintf->intf_status == JNX_GW_CTRL_STATUS_INIT) {
                        pintf->intf_status = JNX_GW_CTRL_STATUS_UP;
                    }
                }
                break;

            case JNX_GW_DEL_IP_IP_SESSION:

                /* set the status of the ip ip gateway
                   tunnel to down, if no data pic can reach
                   this ipip gateway */

                pipip_gw->datapic_count--;

                if ((pipip_gw->datapic_count == 0) &&
                    (pipip_gw->ipip_gw_status == JNX_GW_CTRL_STATUS_UP)) {

                    pipip_gw->ipip_gw_status = JNX_GW_CTRL_STATUS_INIT;
                }

                /* simillarly for the associated interface */
                if (pintf) {
                    pintf->ipip_tunnel_count--;

                    /* this may change for native format egress
                     * tunnels, we may not need to count the
                     * interface references to determine the
                     * status of the interface, for now lets
                     * maintain this
                     */

                    if ((pintf->ipip_tunnel_count == 0) &&
                        (pintf->intf_status == JNX_GW_CTRL_STATUS_UP)) {

                        pintf->intf_status = JNX_GW_CTRL_STATUS_INIT;
                    }
                }
                break;

            default:
                break;
        }
    }
    JNX_GW_CTRL_CONFIG_WRITE_UNLOCK();
    return EOK;
}

/**
 * This function handles the messages from the data pic modules
 * @param pclient pconn client pointer
 * @param pmsg    message pointer
 */
status_t
jnx_gw_ctrl_data_msg_handler(pconn_client_t * pclient, ipc_msg_t * ipc_msg,
                             void * cookie __unused)
{
    jnx_gw_msg_header_t * hdr;
    jnx_gw_ctrl_data_pic_t * pdata_pic;

    /* get the data pic */
    JNX_GW_CTRL_CONFIG_READ_LOCK();

    pdata_pic = jnx_gw_ctrl_lookup_data_pic(NULL, pclient);

    JNX_GW_CTRL_CONFIG_READ_UNLOCK();

    if (!pdata_pic) {
        jnx_gw_log(LOG_DEBUG, "Non-existent data agent message!");
        return EOK;
    }

    jnx_gw_log(LOG_DEBUG, "Data agent \"%s\" message received",
               pdata_pic->pic_name);

    if ((hdr = (typeof(hdr))ipc_msg->data) == NULL)  {
        return EOK;
    }

    switch (hdr->msg_type) {
        /* call the gre session handler function */
        case JNX_GW_GRE_SESSION_MSG:
            jnx_gw_ctrl_handle_data_gre_msg(pdata_pic, hdr);
            break;

            /* call the ipip tunnel handler function */
        case JNX_GW_IPIP_TUNNEL_MSG:
            jnx_gw_ctrl_handle_data_ipip_msg(pdata_pic, hdr);
            break;

        default:
            jnx_gw_log(LOG_DEBUG, "Data agent \"%s\" unsupported message",
                       pdata_pic->pic_name);
            break;
    }
    return EOK;
}

/**
 * This function handles the events from the data pic modules
 * @param pclient pconn client pointer
 * @param event   event type
 */
void
jnx_gw_ctrl_data_event_handler(pconn_client_t * pclient, pconn_event_t event,
                               void * cookie __unused)
{
    jnx_gw_ctrl_intf_t * pintf;
    jnx_gw_ctrl_data_pic_t * pdata_pic;

    /* could not find the data pic entry,
     * have to be intimated by the management
     * agent before hand, for creating this
     * entry, return
     */
    JNX_GW_CTRL_CONFIG_READ_LOCK();
    pdata_pic = jnx_gw_ctrl_lookup_data_pic(NULL, pclient);

    if (!pdata_pic) {
        JNX_GW_CTRL_CONFIG_READ_UNLOCK();
        jnx_gw_log(LOG_INFO, "Non-existent data agent event!");
        return;
    }

    switch (event) {

        case PCONN_EVENT_ESTABLISHED:
            jnx_gw_log(LOG_INFO,
                       "Data agent \"%s\" up event received",
                       pdata_pic->pic_name);

            /* set the pic status to UP */

            pdata_pic->pic_status = JNX_GW_CTRL_STATUS_UP;

            for (pintf = jnx_gw_ctrl_get_first_intf(NULL, pdata_pic, NULL);
                 (pintf);
                 jnx_gw_ctrl_get_next_intf(NULL, pdata_pic, NULL, pintf)) {
                jnx_gw_ctrl_add_nexthop(pintf->pvrf, pintf);
            }

            break;

        case PCONN_EVENT_SHUTDOWN:
        case PCONN_EVENT_FAILED:
            jnx_gw_log(LOG_INFO,
                       "Data agent \"%s\" shutdown event received",
                       pdata_pic->pic_name);
            pdata_pic->pic_status = JNX_GW_CTRL_STATUS_DELETE;
            pdata_pic->pic_data_conn = NULL;

            /* delete the data pic entry from the database */
            jnx_gw_ctrl_delete_data_pic(pdata_pic);

            break;

        default:
            jnx_gw_log(LOG_INFO,
                       "Data agent \"%s\" unnknown event received",
                       pdata_pic->pic_name);
            break;
    }
    JNX_GW_CTRL_CONFIG_READ_UNLOCK();
    return;
}

/**
 * This function fills the message into the send buffer for
 * a data pic agent connection,
 * if during filing of the information, the send buffer
 * is overflowed, the message is pused
 * @params  msg_type   message type
 * @params  add_flag   add/delete/modify flag(sub header type)
 * @params  pdata_pic  data pic structure pointer
 * @params  sesn       points to the gre sesn or ipip gateway
 */
status_t
jnx_gw_ctrl_fill_data_pic_msg(uint8_t msg_type, uint8_t add_flag,
                              jnx_gw_ctrl_data_pic_t * pdata_pic, void * pdata)
{
    uint16_t sublen = 0;
    jnx_gw_msg_header_t       *hdr    = NULL;
    jnx_gw_msg_sub_header_t   *subhdr = NULL;
    jnx_gw_msg_session_info_t *psesn_info = NULL;
    jnx_gw_msg_tunnel_type_t  *ptun_type = NULL;
    jnx_gw_msg_ip_t           *pip_tun_info   = NULL;
    jnx_gw_msg_gre_info_t     *pgre_tun_info  = NULL;
    jnx_gw_msg_ip_ip_info_t   *pipip_tun_info = NULL;
    jnx_gw_ctrl_ipip_gw_t     *pipip_gw = NULL; 
    jnx_gw_ctrl_gre_session_t *pgre_session = NULL; 

    if (!pdata_pic || (pdata_pic->pic_status != JNX_GW_CTRL_STATUS_UP) ||
        !(pdata_pic->pic_data_conn)) {
        return (EFAIL);
    }


    JNX_GW_CTRL_DATA_PIC_SEND_LOCK(pdata_pic);

    /* if there is pending messages, send them across */
    hdr = (typeof(hdr))pdata_pic->send_buf;

    /* the message may overflow the buffer, the current message type is
       different, subheader counter may overflow, push it out */

    if ((pdata_pic->cur_len) &&
        ((hdr->count > 250) || (hdr->msg_type != msg_type) ||
         ((pdata_pic->cur_len) + sizeof(jnx_gw_msg_gre_t) >
          JNX_GW_CTRL_MAX_PKT_BUF_SIZE))) {

        /* only valid messages */
        if (pdata_pic->cur_len != sizeof(*hdr))  {

            hdr->msg_len = htons(pdata_pic->cur_len);

            pconn_client_send(pdata_pic->pic_data_conn, hdr->msg_type,
                              pdata_pic->send_buf, pdata_pic->cur_len);
        }
        pdata_pic->cur_len  = 0;
    }

    /* set the message header fields */
    if (pdata_pic->cur_len == 0) {
        pdata_pic->cur_len  = sizeof(*hdr);
        hdr->msg_type       = msg_type;
        hdr->count          = 0;
    }

    /* get the current buffer pointer */
    subhdr = (typeof(subhdr)) ((uint8_t *)hdr + pdata_pic->cur_len);

    /* this is a gre message */
    if (msg_type == JNX_GW_GRE_SESSION_MSG) {

        /* set the pointer */
        pgre_session  = pdata;

        /* set the subheader fields */
        subhdr->sub_type = add_flag;
        subhdr->err_code = pgre_session->sesn_errcode;
        sublen           = sizeof(*subhdr);

        if (add_flag == JNX_GW_ADD_GRE_SESSION) {

            /* fill gre session info */
            psesn_info = (typeof(psesn_info)) ((uint8_t *)subhdr + sublen);

            psesn_info->session_id = htons(pgre_session->sesn_id);
            psesn_info->proto      = (pgre_session->sesn_proto);
            psesn_info->sip        = htonl(pgre_session->sesn_client_ip);
            psesn_info->dip        = htonl(pgre_session->sesn_server_ip);
            psesn_info->sport      = htons(pgre_session->sesn_sport);
            psesn_info->dport      = htons(pgre_session->sesn_dport);
            sublen                += sizeof(*psesn_info);


            /* fill ingress gre tunnel type */
            ptun_type              = (typeof(ptun_type))((uint8_t *)subhdr +
                                                         sublen);
            ptun_type->tunnel_type = JNX_GW_TUNNEL_TYPE_GRE;
            ptun_type->flags       = pgre_session->sesn_flags;
            ptun_type->length      = htons(sizeof(*ptun_type) +
                                           sizeof(*pgre_tun_info));
            sublen                += sizeof(*ptun_type);


            /* fill ingress gre tunnel info */
            pgre_tun_info = (typeof(pgre_tun_info)) ((uint8_t *)subhdr +
                                                     sublen);
            pgre_tun_info->vrf        = htonl(pgre_session->ingress_vrf_id);
            pgre_tun_info->gateway_ip = htonl(pgre_session->ingress_gw_ip);
            pgre_tun_info->self_ip    = htonl(pgre_session->ingress_self_ip);
            pgre_tun_info->gre_key    = htonl(pgre_session->ingress_gre_key);
            pgre_tun_info->gre_seq    = 0;
            sublen                   += sizeof(*pgre_tun_info);

            /* fill egress tunnel info */
            ptun_type = (typeof(ptun_type)) ((uint8_t *)subhdr + sublen);

            if (pgre_session->pipip_gw) {

                /* fill egress ipip tunnel type */
                ptun_type->tunnel_type = JNX_GW_TUNNEL_TYPE_IPIP;
                ptun_type->flags       = pgre_session->sesn_flags;
                ptun_type->length      = htons(sizeof(*ptun_type) +
                                               sizeof(*pipip_tun_info));
                sublen                 += sizeof(*ptun_type);

                /* fill egress ipip tunnel info */
                pipip_tun_info = (typeof(pipip_tun_info))
                    ((uint8_t *)subhdr + sublen);

                pipip_tun_info->vrf        = htonl(pgre_session->egress_vrf_id); 
                pipip_tun_info->gateway_ip = htonl(pgre_session->egress_gw_ip);
                pipip_tun_info->self_ip    = htonl(pgre_session->egress_self_ip);
                sublen                    += sizeof(*pipip_tun_info);

            } else {

                /* fill egress native tunnel type */
                ptun_type->tunnel_type = JNX_GW_TUNNEL_TYPE_IP;
                ptun_type->flags       = pgre_session->sesn_flags;
                ptun_type->length      = htons(sizeof(*ptun_type) +
                                               sizeof(*pip_tun_info));
                sublen                 += sizeof(*ptun_type);

                /* fill egress native tunnel info */
                pip_tun_info       = (typeof(pip_tun_info))((uint8_t *)subhdr +
                                                            sublen);
                pip_tun_info->vrf  = htonl(pgre_session->egress_vrf_id);
                sublen            += sizeof(*pip_tun_info);
            }
        } else if (add_flag == JNX_GW_DEL_GRE_SESSION) {

            /* fill ingress gre tunnel info */
            pgre_tun_info = (typeof(pgre_tun_info)) ((uint8_t *)subhdr +
                                                     sublen);
            pgre_tun_info->vrf        = htonl(pgre_session->ingress_vrf_id);
            pgre_tun_info->gateway_ip = htonl(pgre_session->ingress_gw_ip);
            pgre_tun_info->self_ip    = htonl(pgre_session->ingress_self_ip);
            pgre_tun_info->gre_key    = htonl(pgre_session->ingress_gre_key);
            pgre_tun_info->gre_seq    = 0;
            sublen                   += sizeof(*pgre_tun_info);
        } else  {
            goto data_pic_fill_fail;
        }

        /* update the message header counter,
           current buffer length, & subheader length */
        hdr->count++;
        subhdr->length      = htons(sublen);
        pdata_pic->cur_len += sublen;

    } else if (msg_type == JNX_GW_IPIP_TUNNEL_MSG) {
        /* its an ip ip message type */
        /* set the pointer */
        pipip_gw  = pdata;

        /* fill subheader */
        subhdr->sub_type = add_flag;
        subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
        sublen           = sizeof(*subhdr);

        if ((add_flag == JNX_GW_ADD_IP_IP_SESSION) ||
            (add_flag == JNX_GW_DEL_IP_IP_SESSION)) {

            /* set the ipip tunnel type */
            ptun_type              = (typeof(ptun_type)) ((uint8_t *)subhdr +
                                                          sublen);
            ptun_type->tunnel_type = JNX_GW_TUNNEL_TYPE_IPIP;
            ptun_type->flags       = 0;
            ptun_type->length      = htons(sizeof(*ptun_type) +
                                           sizeof(*pipip_tun_info));
            sublen                += sizeof(*ptun_type);

            /* set the ip ip tunnel info */
            pipip_tun_info             = (typeof(pipip_tun_info))
                ((uint8_t *)subhdr + sublen);
            pipip_tun_info->vrf        = htonl(pipip_gw->ipip_vrf_id);
            pipip_tun_info->gateway_ip = htonl(pipip_gw->ipip_gw_ip);

            /* do not set the source address */
            pipip_tun_info->self_ip    = 0;
            sublen                    += sizeof(*pipip_tun_info);
        } else {
            goto data_pic_fill_fail;
        }
        hdr->count++;
        subhdr->length      = htons(sublen);
        pdata_pic->cur_len += sublen;
    } else {
        goto data_pic_fill_fail;
    }

    JNX_GW_CTRL_DATA_PIC_SEND_UNLOCK(pdata_pic);
    return EOK;

data_pic_fill_fail:
    pdata_pic->cur_len  = 0;
    JNX_GW_CTRL_DATA_PIC_SEND_UNLOCK(pdata_pic);
    jnx_gw_log(LOG_DEBUG, "Data agent \"%s\" unknown message request",
               pdata_pic->pic_name);
    return EFAIL;
}

/**
 * This function fills the message into the send buffer for
 * all data pic agent connection.
 * @params  msg_type   message type
 * @params  add_flag   add/delete/modify flag(sub header type)
 * @params  sesn       points to the gre sesn or ipip gateway
 */
status_t
jnx_gw_ctrl_fill_data_pic_msgs(uint8_t msg_type, uint8_t add_flag, void * pdata)
{
    jnx_gw_ctrl_data_pic_t * pdata_pic = NULL;

    while ((pdata_pic = jnx_gw_ctrl_get_next_data_pic(pdata_pic))) {
        jnx_gw_ctrl_fill_data_pic_msg(msg_type, add_flag, pdata_pic,
                                      pdata);
    }
    return EOK;
}


/**
 * This function pushes the message to the data pic
 * @params pdata_pic data pic structure pointer
 */
status_t
jnx_gw_ctrl_send_data_pic_msg(jnx_gw_ctrl_data_pic_t * pdata_pic)
{
    jnx_gw_msg_header_t * hdr;

    if (!pdata_pic || (pdata_pic->pic_status != JNX_GW_CTRL_STATUS_UP) ||
        !(pdata_pic->pic_data_conn)) {
        return (EFAIL);
    }

    JNX_GW_CTRL_DATA_PIC_SEND_LOCK(pdata_pic);

    hdr = (typeof(hdr))pdata_pic->send_buf;

    /* only valid messages */
    if ((pdata_pic->cur_len) && 
        (pdata_pic->cur_len != sizeof(*hdr)))  {

        hdr->msg_len = htons(pdata_pic->cur_len);

        jnx_gw_log(LOG_DEBUG, "Data agent \"%s\" message sent", 
                   pdata_pic->pic_name);

        pconn_client_send(pdata_pic->pic_data_conn, hdr->msg_type,
                          pdata_pic->send_buf, pdata_pic->cur_len);
    }
    pdata_pic->cur_len  = 0;

    JNX_GW_CTRL_DATA_PIC_SEND_UNLOCK(pdata_pic);
    return EOK;
}

/**
 * This function pushes the message to the data pics
 * @params none
 */
status_t
jnx_gw_ctrl_send_data_pic_msgs(void)
{
    jnx_gw_ctrl_data_pic_t * pdata_pic = NULL;

    while ((pdata_pic = jnx_gw_ctrl_get_next_data_pic(pdata_pic))) {
        jnx_gw_ctrl_send_data_pic_msg(pdata_pic);
    }
    return EOK;
}

/**
 * This function sends the ip ip gateway messages to the
 * data pic
 * @params pvrf        vrf structure pointer
 * @params pdata_pic   data pic structure pointer
 * @params pintf  data interface structure pointer
 * @params add_flag    add delete flag
 */

status_t 
jnx_gw_ctrl_send_ipip_msg_to_data_pic(jnx_gw_ctrl_vrf_t * pvrf,
                                      jnx_gw_ctrl_data_pic_t * pdata_pic,
                                      jnx_gw_ctrl_intf_t * pintf __unused,
                                      uint8_t add_flag)
{
    jnx_gw_ctrl_ipip_gw_t * pipip_gw = NULL;

    /* collect the ipip gateways for the vrf */
    while ((pipip_gw = jnx_gw_ctrl_get_next_ipip_gw(pvrf, pipip_gw))) {

        jnx_gw_ctrl_fill_data_pic_msg(JNX_GW_IPIP_TUNNEL_MSG, add_flag,
                                      pdata_pic, pipip_gw);
    }

    /* now push the messages to the data pic */
    jnx_gw_ctrl_send_data_pic_msg(pdata_pic);
    return EOK;
}

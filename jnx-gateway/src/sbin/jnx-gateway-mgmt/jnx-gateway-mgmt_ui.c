/*
 * $Id: jnx-gateway-mgmt_ui.c 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-mgmt_ui.c - SDK gateway application UI routines
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
#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>
#include <jnx/vrf_util_pub.h>
#include <jnx/parse_ip.h>

#include JNX_GATEWAY_MGMT_SEQUENCE_H
#include JNX_GATEWAY_MGMT_OUT_H
#include JNX_GATEWAY_MGMT_ODL_H

#include <jnx/jnx-gateway.h>
#include <jnx/jnx-gateway_msg.h>

#include "jnx-gateway-mgmt.h"
#include "jnx-gateway-mgmt_config.h"

#define LEVEL_SUMMARY    0
#define LEVEL_BRIEF      1
#define LEVEL_DETAIL     2
#define LEVEL_EXTENSIVE  3
#define LEVEL_TERSE      4

#define TOKEN_ALL       "all"
#define TOKEN_EXTENSIVE "extensive"
#define TOKEN_SUMMARY   "summary"
#define TOKEN_DETAIL    "detail"
#define TOKEN_TERSE     "terse"

/**
  * This file contains the mgd interface functions for displaying
  * operational command outputs
  * These routines fill up the messages, with the stats gathered
  * from the control and data pic agents across services pics.
  *
  */

/**
 * This function shows the version of the sample gateway module
 */
static int
jnx_gw_mgmt_show_version(mgmt_sock_t * msp, parse_status_t * csb,
                         char * unparsed __unused)
{
    int32_t detail_level = ms_parse_get_subcode(csb);

    if (detail_level == LEVEL_BRIEF) {
        xml_show_version(msp, NULL, FALSE);
    } if (detail_level == LEVEL_DETAIL ||
            detail_level == LEVEL_EXTENSIVE) {
        xml_show_version(msp, NULL, TRUE);
    }
    return 0;
}

/**
 * This function fills the control pic header information 
 */
static void 
jnx_gw_mgmt_show_ctrl_pic (mgmt_sock_t * msp,
                           jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                           char * vrf_name __unused)
{
    XML_OPEN(msp, ODCI_JNX_GATEWAY_PIC);
    XML_ELT(msp, ODCI_INTERFACE_NAME, "%s", pctrl_pic->pic_name);
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_PIC);
}

/**
 * This function fills the data pic header information 
 */
static void 
jnx_gw_mgmt_show_data_pic (mgmt_sock_t * msp,
                           jnx_gw_mgmt_data_session_t * pdata_pic,
                           char * vrf_name __unused)
{
    XML_OPEN(msp, ODCI_JNX_GATEWAY_PIC);
    XML_ELT(msp, ODCI_INTERFACE_NAME, "%s", pdata_pic->pic_name);
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_PIC);
}

/**
 * This function fills the gre session header information 
 * for a control pic agent
 */
static void
jnx_gw_mgmt_show_gre_sesn_sum (mgmt_sock_t * msp,
                               jnx_gw_mgmt_ctrl_session_t * pctrl_pic __unused,
                               jnx_gw_msg_ctrl_gre_sum_stat_t * pgre_hdr)
{
    char vrf_name[JNX_GW_STR_SIZE];
    char addr_name[JNX_GW_STR_SIZE];

    jnx_gw_get_vrf_name(ntohl(pgre_hdr->gre_vrf_id),
                        vrf_name, sizeof(vrf_name));

    XML_OPEN(msp, ODCI_JNX_GATEWAY_GRE_SESN_HEADER);

    if (vrf_name[0] != 0) {
        XML_ELT(msp, ODCI_JNX_GATEWAY_VRF, "%s", vrf_name);
    }

    if (pgre_hdr->gre_gw_ip) {
        XML_ELT(msp, ODCI_JNX_GATEWAY_CLIENT_GATEWAY_ID,"%s",
                inet_ntop(AF_INET, &pgre_hdr->gre_gw_ip,
                          addr_name, sizeof(addr_name)));
    }

    XML_ELT(msp, ODCI_JNX_GATEWAY_ACTIVE_SESN_COUNT, "%u",
            ntohl(pgre_hdr->gre_active_sesn_count));
    XML_ELT(msp, ODCI_JNX_GATEWAY_SESN_COUNT, "%u",
            ntohl(pgre_hdr->gre_sesn_count));
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_GRE_SESN_HEADER);
}


/**
 * This function fills the gre tunnel session information 
 * for a gre tunnel session for a control pic agent
 */
static void 
jnx_gw_mgmt_show_gre_sesn (mgmt_sock_t * msp, void * pmsg)
{
    char addr_name[JNX_GW_STR_SIZE];
    jnx_gw_msg_ctrl_gre_sesn_stat_t * psesn = pmsg;

    XML_OPEN(msp, ODCI_JNX_GATEWAY_SESN);

    XML_ELT(msp, ODCI_JNX_GATEWAY_CLIENT_ID,  "%s",
            inet_ntop(AF_INET, &psesn->sesn_client_ip,
                      addr_name, sizeof(addr_name)));

    XML_ELT(msp, ODCI_JNX_GATEWAY_SERVER_ID, "%s",
            inet_ntop(AF_INET, &psesn->sesn_server_ip,
                      addr_name, sizeof(addr_name)));

    XML_ELT(msp, ODCI_JNX_GATEWAY_PROTOCOL, "%u", psesn->sesn_protocol);
    XML_ELT(msp, ODCI_JNX_GATEWAY_SRC_PORT, "%u", ntohs(psesn->sesn_sport));
    XML_ELT(msp, ODCI_JNX_GATEWAY_DST_PORT, "%u", ntohs(psesn->sesn_dport));
    XML_ELT(msp, ODCI_JNX_GATEWAY_USER_NAME, "\"%s\"", psesn->sesn_user_name);

    /* XXX:TBD : GATEWAY_UP_TIME*/
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_SESN);
}

/**
 * This function fills the gre tunnel ingress tunnel information 
 * for a gre tunnel session for a control pic agent
 */
static void 
jnx_gw_mgmt_show_gre_ingress (mgmt_sock_t * msp, void * pmsg)
{
    jnx_gw_msg_ctrl_gre_sesn_stat_t * psesn = pmsg;
    char if_name[JNX_GW_STR_SIZE], vrf_name[JNX_GW_STR_SIZE];
    char addr_name[JNX_GW_STR_SIZE];
    uint32_t ifl_id = ntohl(psesn->gre_if_id);

    jnx_gw_ifl_get_name(ifl_id, if_name, sizeof(if_name));
    jnx_gw_get_vrf_name(ntohl(psesn->gre_vrf_id),
                        vrf_name, sizeof(vrf_name));

    XML_OPEN(msp, ODCI_JNX_GATEWAY_INGRESS);
    XML_ELT(msp, ODCI_JNX_GATEWAY_GRE_KEY, "%u", ntohl(psesn->gre_key));
    if (vrf_name[0] != 0) {
        XML_ELT(msp, ODCI_JNX_GATEWAY_INGRESS_VRF, "%s", vrf_name);
    }

    XML_ELT(msp, ODCI_JNX_GATEWAY_SELF_ID,"%s",
            inet_ntop(AF_INET, &psesn->gre_data_ip,
                      addr_name, sizeof(addr_name)));

    XML_ELT(msp, ODCI_JNX_GATEWAY_CLIENT_GATEWAY_ID,"%s",
            inet_ntop(AF_INET, &psesn->gre_gw_ip,
                      addr_name, sizeof(addr_name)));

    XML_ELT(msp, ODCI_INTERFACE_NAME, "%s", if_name);
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_INGRESS);
}

/**
 * This function fills the gre tunnel egress tunnel information 
 * for a gre tunnel session for a control pic agent
 */
static void
jnx_gw_mgmt_show_gre_egress (mgmt_sock_t * msp, void * pmsg)
{
    jnx_gw_msg_ctrl_gre_sesn_stat_t * psesn = pmsg;
    char if_name[JNX_GW_STR_SIZE], vrf_name[JNX_GW_STR_SIZE],
         addr_name[JNX_GW_STR_SIZE];
    uint32_t ifl_id = ntohl(psesn->ipip_if_id);

    jnx_gw_ifl_get_name(ifl_id, if_name, sizeof(if_name));

    jnx_gw_get_vrf_name(ntohl(psesn->ipip_vrf_id),
                        vrf_name, sizeof(vrf_name));
    XML_OPEN(msp, ODCI_JNX_GATEWAY_EGRESS);
    XML_ELT(msp, ODCI_JNX_GATEWAY_EGRESS_VRF, "%s", vrf_name);
    if (psesn->ipip_data_ip) {
        XML_ELT(msp, ODCI_JNX_GATEWAY_SELF_ID, "%s",
                inet_ntop(AF_INET, &psesn->ipip_data_ip,
                          addr_name, sizeof(addr_name)));
    }

    if (psesn->ipip_gw_ip) {
        XML_ELT(msp, ODCI_JNX_GATEWAY_IPIP_ID, "%s",
                inet_ntop(AF_INET, &psesn->ipip_gw_ip,
                          addr_name, sizeof(addr_name)));
    }

    XML_ELT(msp, ODCI_INTERFACE_NAME, "%s", if_name);
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_EGRESS);
}

/**
 * This function fills the ipip tunnel header/summary information 
 * for a control pic agent
 */
static void
jnx_gw_mgmt_show_ipip_header(mgmt_sock_t *msp, void * pmsg)
{
    char vrf_name[JNX_GW_STR_SIZE];
    jnx_gw_msg_ctrl_ipip_sum_stat_t * pip_msg = pmsg;

    XML_OPEN(msp, ODCI_JNX_GATEWAY_IPIP_HEADER);

    jnx_gw_get_vrf_name(ntohl(pip_msg->ipip_vrf_id),
                        vrf_name, sizeof(vrf_name));

    if (vrf_name[0] != 0) {
        XML_ELT(msp, ODCI_JNX_GATEWAY_VRF, "%s", vrf_name);
    }

    XML_ELT(msp, ODCI_JNX_GATEWAY_IPIP_TUN_COUNT, "%u",
              ntohl(pip_msg->ipip_tun_count));
    XML_ELT(msp, ODCI_JNX_GATEWAY_ACTIVE_SESN_COUNT, "%u",
              ntohl(pip_msg->ipip_active_sesn_count));
    XML_ELT(msp, ODCI_JNX_GATEWAY_SESN_COUNT, "%u",
            ntohl(pip_msg->ipip_sesn_count));
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_IPIP_HEADER);
}

/* ipip tunnel id */
/**
 * This function fills the ipip tunnel information 
 * for an ipip tunnel session for a control pic agent
 */
static void
jnx_gw_mgmt_show_ipip_tunnel(mgmt_sock_t *msp, void * pmsg)
{
    char vrf_name[JNX_GW_STR_SIZE], addr_name[JNX_GW_STR_SIZE];
    jnx_gw_msg_ctrl_ipip_sesn_stat_t * pipip_msg = pmsg;

    jnx_gw_get_vrf_name(ntohl(pipip_msg->ipip_vrf_id),
                        vrf_name, sizeof(vrf_name));
    XML_OPEN(msp, ODCI_JNX_GATEWAY_IPIP_TUNNEL);

    if (vrf_name[0] != 0) {
        XML_ELT(msp, ODCI_JNX_GATEWAY_VRF, "%s", vrf_name);
    }

    XML_ELT(msp, ODCI_JNX_GATEWAY_IPIP_ID, "%s",
            inet_ntop(AF_INET, &pipip_msg->ipip_gw_ip,
                      addr_name, sizeof(addr_name)));

    XML_ELT(msp, ODCI_JNX_GATEWAY_ACTIVE_SESN_COUNT, "%u",
              ntohl(pipip_msg->ipip_active_sesn_count));
    XML_ELT(msp, ODCI_JNX_GATEWAY_SESN_COUNT, "%u",
            ntohl(pipip_msg->ipip_sesn_count));
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_IPIP_TUNNEL);
}

/* summary stats */
/**
 * This function fills the statistics summary information
 * for a data pic agent
 */
static void
jnx_gw_mgmt_show_stat_summary(mgmt_sock_t * msp, 
                              jnx_gw_mgmt_data_session_t * pdata_pic,
                              void * msg)
{
    uint32_t vrf_id = 0;
    char vrf_name[JNX_GW_STR_SIZE];
    jnx_gw_vrf_stat_t * pvrf;
    jnx_gw_common_stat_t * pcom;

    vrf_id = ntohl(*(uint32_t *)msg);

    pvrf = (typeof(pvrf))((uint8_t *)msg + sizeof(vrf_id));
    pcom = (typeof(pcom))((uint8_t *)pvrf + sizeof(*pvrf));

    jnx_gw_get_vrf_name(vrf_id, vrf_name, sizeof(vrf_name));

    XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_SUMMARY);

    XML_ELT(msp, ODCI_INTERFACE_NAME, "%s", pdata_pic->pic_name);

    if (vrf_name[0] != 0) {
        XML_ELT(msp, ODCI_JNX_GATEWAY_VRF, "%s", vrf_name);
    }

    XML_ELT(msp, ODCI_JNX_GATEWAY_ACTIVE_SESN_COUNT, "%u",
              ntohl(pvrf->active_sessions));

    XML_ELT(msp, ODCI_JNX_GATEWAY_SESN_COUNT, "%u",
            ntohl(pvrf->total_sessions));

#if 0 /* TBD */
    XML_ELT(msp, ODCI_JNX_GATEWAY_IPIP_TUN_COUNT, "%u",
            ntohl(pvrf->ipip_tun_count));
#endif

    XML_ELT(msp, ODCI_JNX_GATEWAY_TX_BYTES, "%u", ntohl(pcom->bytes_out));
    XML_ELT(msp, ODCI_JNX_GATEWAY_RX_BYTES, "%u", ntohl(pcom->bytes_in));
    XML_ELT(msp, ODCI_JNX_GATEWAY_TX_PACKETS, "%u", ntohl(pcom->packets_out));
    XML_ELT(msp, ODCI_JNX_GATEWAY_RX_PACKETS, "%u", ntohl(pcom->packets_in));

    XML_ELT(msp, ODCI_JNX_GATEWAY_CKSUM_PACKETS, "%u",
            ntohl(pcom->checksum_fail));
    XML_ELT(msp, ODCI_JNX_GATEWAY_TTL_PACKETS, "%u", ntohl(pcom->ttl_drop));
    XML_ELT(msp, ODCI_JNX_GATEWAY_CONG_PACKETS, "%u", ntohl(pcom->cong_drop));

    XML_ELT(msp, ODCI_JNX_GATEWAY_PROTO_PACKETS, "%u",
            ntohl(pvrf->invalid_pkt));
    XML_ELT(msp, ODCI_JNX_GATEWAY_TUNNEL_PACKETS, "%u",
            ntohl(pvrf->tunnel_not_present));

    XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_SUMMARY);
}

/**
 * This function fills the gre tunnel session statistics information
 * for a data pic agent gre tunnel session
 */
static void
jnx_gw_mgmt_show_stat_gre_sesn(mgmt_sock_t * msp,
                               jnx_gw_mgmt_data_session_t * pdata_pic __unused,
                               void * msg)
{
    jnx_gw_gre_key_t * pgre;
    jnx_gw_common_stat_t * pcom;
    char vrf_name[JNX_GW_STR_SIZE];

    pgre = (typeof(pgre))msg;
    pcom = (typeof(pcom))((uint8_t *)pgre + sizeof(*pgre));

    XML_OPEN(msp, ODCI_JNX_GATEWAY_GRE_SESN_STAT);

    jnx_gw_get_vrf_name(ntohl(pgre->vrf), vrf_name, sizeof(vrf_name));
    XML_ELT(msp, ODCI_JNX_GATEWAY_GRE_KEY, "%u", ntohl(pgre->gre_key));
    if (vrf_name[0] != 0) {
        XML_ELT(msp, ODCI_JNX_GATEWAY_INGRESS_VRF, "%s", vrf_name);
    }
    XML_ELT(msp, ODCI_JNX_GATEWAY_TX_BYTES, "%u", ntohl(pcom->bytes_out));
    XML_ELT(msp, ODCI_JNX_GATEWAY_RX_BYTES, "%u", ntohl(pcom->bytes_in));
    XML_ELT(msp, ODCI_JNX_GATEWAY_TX_PACKETS, "%u", ntohl(pcom->packets_out));
    XML_ELT(msp, ODCI_JNX_GATEWAY_RX_PACKETS, "%u", ntohl(pcom->packets_in));
    XML_ELT(msp, ODCI_JNX_GATEWAY_CKSUM_PACKETS,
              "%u", ntohl(pcom->checksum_fail));
    XML_ELT(msp, ODCI_JNX_GATEWAY_TTL_PACKETS, "%u", ntohl(pcom->ttl_drop));
    XML_ELT(msp, ODCI_JNX_GATEWAY_CONG_PACKETS, "%u", ntohl(pcom->cong_drop));
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_GRE_SESN_STAT);
}

/**
 * This function fills the ipip tunnel statistics information
 * for a data pic agent ipip tunnel 
 */
static void
jnx_gw_mgmt_show_stat_ipip_tun(mgmt_sock_t * msp,
                               jnx_gw_mgmt_data_session_t * pdata_pic __unused,
                               void * msg)
{
    jnx_gw_common_stat_t * pcom;
    jnx_gw_ipip_tunnel_key_t * pipip;
    char vrf_name [JNX_GW_STR_SIZE], addr_name[JNX_GW_STR_SIZE];

    pipip = msg;
    pcom  = (typeof(pcom))((uint8_t *)pipip + sizeof(*pipip));

    jnx_gw_get_vrf_name(pipip->vrf, vrf_name, sizeof(vrf_name));

    XML_OPEN(msp, ODCI_JNX_GATEWAY_IPIP_TUN_STAT);

    if (vrf_name[0]!= 0)
        XML_ELT(msp, ODCI_JNX_GATEWAY_INGRESS_VRF, "%s", vrf_name);

    XML_ELT(msp, ODCI_JNX_GATEWAY_IPIP_ID, "%s",
            inet_ntop(AF_INET, &pipip->gateway_ip,
                      addr_name, sizeof(addr_name)));

    XML_ELT(msp, ODCI_JNX_GATEWAY_TX_BYTES, "%u", ntohl(pcom->bytes_out));
    XML_ELT(msp, ODCI_JNX_GATEWAY_RX_BYTES, "%u", ntohl(pcom->bytes_in));
    XML_ELT(msp, ODCI_JNX_GATEWAY_TX_PACKETS, "%u", ntohl(pcom->packets_out));
    XML_ELT(msp, ODCI_JNX_GATEWAY_RX_PACKETS, "%u", ntohl(pcom->packets_in));
    XML_ELT(msp, ODCI_JNX_GATEWAY_CKSUM_PACKETS, "%u",
            ntohl(pcom->checksum_fail));
    XML_ELT(msp, ODCI_JNX_GATEWAY_TTL_PACKETS, "%u", ntohl(pcom->ttl_drop));
    XML_ELT(msp, ODCI_JNX_GATEWAY_CONG_PACKETS, "%u", ntohl(pcom->cong_drop));
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_IPIP_TUN_STAT);
}

/* show user display routines */
/**
 * This function fills the user summary statistics information
 * for the sample gateway application
 */
static void
jnx_gw_mgmt_show_user_header(mgmt_sock_t * msp)
{
    XML_OPEN(msp, ODCI_JNX_GATEWAY_USER_HEADER);
    XML_ELT(msp, ODCI_JNX_GATEWAY_USER_COUNT, "%u", jnx_gw_mgmt.user_count);
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_USER_HEADER);
}

/**
 * This function fills the user statistics information
 * for the sample gateway application
 */
static void
jnx_gw_mgmt_show_user(mgmt_sock_t * msp, jnx_gw_mgmt_user_t * puser)
{
    char vrf_name[JNX_GW_STR_SIZE];
    char addr_name[JNX_GW_STR_SIZE];
    char mask_name[JNX_GW_STR_SIZE];
    uint32_t addr, mask;

    XML_OPEN(msp, ODCI_JNX_GATEWAY_USER);
    XML_ELT(msp, ODCI_JNX_GATEWAY_USER_NAME, "\"%s\"", puser->user_name);

    addr = htonl(puser->user_addr);
    mask = htonl(puser->user_mask);
    XML_ELT(msp, ODCI_JNX_GATEWAY_USER_ADDR_RANGE,"%s/%s",
            inet_ntop(AF_INET, &addr, addr_name, sizeof(addr_name)),
            inet_ntop(AF_INET, &mask, mask_name, sizeof(mask_name)));

    jnx_gw_get_vrf_name(puser->user_evrf_id, vrf_name, sizeof(vrf_name));
    if (vrf_name[0] != 0) {
        XML_ELT(msp, ODCI_JNX_GATEWAY_EGRESS_VRF, "%s", vrf_name);
    }

    addr = htonl(puser->user_ipip_gw_ip);

    XML_ELT(msp, ODCI_JNX_GATEWAY_IPIP_ID, "%s",
            inet_ntop(AF_INET, &addr, addr_name, sizeof(addr_name)));

    XML_ELT(msp, ODCI_JNX_GATEWAY_ACTIVE_SESN_COUNT, "%u",
              puser->user_active_sesn_count);
    XML_ELT(msp, ODCI_JNX_GATEWAY_SESN_COUNT, "%u", puser->user_sesn_count);

    XML_CLOSE(msp, ODCI_JNX_GATEWAY_USER);
}

/* top level operational commands */

/**
 * This function parses the mgd params for extracting the
 * filters, for "show jnx-gateway gre-tunnels"
 * sends the request to the control pic agents,
 * waits & collects the information from the control 
 * pic agents, & calles appropriate fillup routines
 * & dispatches them to the mgd for display 
 */
static int
jnx_gw_mgmt_show_gre_tunnels(mgmt_sock_t *msp, parse_status_t *csb,
                             char * unparsed)
{
    uint8_t msg_count = 0, sub_type = 0;
    uint8_t more = TRUE, ctrl_flag = FALSE;
    uint16_t sublen, len;
    int32_t vrf_id = JNX_GW_INVALID_VRFID;
    uint32_t gw_ip = 0, gre_key = 0;
    uint32_t verbose = ms_parse_get_subcode(csb);
    ipc_msg_t * ipc_msg = NULL;
    void * pmsg = NULL;
    char * vrf_name = NULL, * word = NULL;
    jnx_gw_msg_header_t  * hdr;
    jnx_gw_msg_sub_header_t * subhdr;
    jnx_gw_mgmt_ctrl_session_t * pctrl_pic = NULL;
    jnx_gw_msg_ctrl_gre_sesn_stat_t * pgre_stat = NULL;

    if (verbose >= LEVEL_DETAIL) {
        sub_type |= JNX_GW_CTRL_VERBOSE_SET;
    }

    if (unparsed) {

        while ((word = strsep(&unparsed, MGMT_PARSE_TOKEN_SEP))) {

            if (!strncmp(word, "INSTANCE", strlen(word))) {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                if ((vrf_id = jnx_gw_get_vrf_id(word))
                    != JNX_GW_INVALID_VRFID) { 
                    vrf_name = word;
                    sub_type |= JNX_GW_CTRL_VRF_SET;
                    continue;
                }

                /* no such vrf, return */
                XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION,
                         xml_attr_xmlns(XML_NS));
                XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION);

                continue;
            }

            if (!strncmp(word, "GATEWAY", strlen(word))) {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                gw_ip = jnx_gw_get_ipaddr(word);
                sub_type |= JNX_GW_CTRL_GW_SET;

                continue;
            }

            if (!strncmp(word, "INTERFACE", strlen(word))) {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                if ((pctrl_pic = jnx_gw_mgmt_ctrl_sesn_lookup(word))) {
                    continue;
                }

                /* this ifd does not have active control agent, return */
                XML_OPEN(msp, ODCI_JNX_GATEWAY_GRE_INFORMATION,
                         xml_attr_xmlns(XML_NS));
                XML_CLOSE(msp, ODCI_JNX_GATEWAY_GRE_INFORMATION);

                return 0;
            }

            if (!strncmp(word, "GREKEY", strlen(word))) {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                if ((gre_key = atoi(word))) {
                    sub_type |= JNX_GW_CTRL_GRE_KEY_SET;
                }

                continue;
            }
        }
    }

    /* for gateway/gre-key, the vrf id is needed ! */
    if (vrf_id == JNX_GW_INVALID_VRFID) {
        if (gw_ip != 0) {
            XML_OPEN(msp, ODCI_JNX_GATEWAY_GRE_INFORMATION,
                     xml_attr_xmlns(XML_NS));
            XML_CLOSE(msp, ODCI_JNX_GATEWAY_GRE_INFORMATION);
            return 0;
        }

        if (gre_key != 0) {
            XML_OPEN(msp, ODCI_JNX_GATEWAY_GRE_INFORMATION,
                     xml_attr_xmlns(XML_NS));
            XML_CLOSE(msp, ODCI_JNX_GATEWAY_GRE_INFORMATION);
            return 0;
        }
    }

    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    memset(hdr, 0, sizeof(*hdr));

    /* now fill the request message */
    hdr->msg_type  = JNX_GW_STAT_FETCH_MSG;
    len    = sizeof(*hdr);

    subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
    sublen = sizeof(*subhdr);

    subhdr->sub_type = JNX_GW_FETCH_GRE_SESN | sub_type;
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;

    pgre_stat = (typeof(pgre_stat))((uint8_t*)subhdr + sublen);
    memset(pgre_stat, 0, sizeof(*pgre_stat));

    pgre_stat->gre_vrf_id = htonl(vrf_id);
    pgre_stat->gre_gw_ip  = htonl(gw_ip);

    sublen          += sizeof(*pgre_stat);
    subhdr->length   = ntohs(sublen);

    len += sublen;
    msg_count++;

    /* update the header fields, with length & count */
    hdr->count   = msg_count;
    hdr->msg_len = htons(len);

    /* send the fetch messsage to the control pics */
    jnx_gw_mgmt_ctrl_send_opcmd(pctrl_pic, JNX_GW_STAT_FETCH_MSG, hdr, len);

    XML_OPEN(msp, ODCI_JNX_GATEWAY_GRE_INFORMATION, xml_attr_xmlns(XML_NS));

    /* if control pic id is provided, just query the speicific
        control pic */
    if (pctrl_pic != NULL) {
        ctrl_flag = TRUE;
        goto show_gre_recv_msg;
    }

    while ((pctrl_pic = jnx_gw_mgmt_get_next_ctrl_sesn(pctrl_pic))) {

show_gre_recv_msg:

        jnx_gw_mgmt_show_ctrl_pic (msp, pctrl_pic, vrf_name);

        more = TRUE;
        XML_OPEN(msp, ODCI_JNX_GATEWAY_GRE_TUNNEL);

        while ((more) && (ipc_msg = jnx_gw_mgmt_ctrl_recv_opcmd(pctrl_pic))) {

            hdr = (typeof(hdr))ipc_msg->data;

            assert (hdr->msg_type == JNX_GW_STAT_FETCH_MSG);

            more   = hdr->more;
            len    = ntohs(hdr->msg_len);
            subhdr = (typeof(subhdr))((uint8_t *)hdr + sizeof(*hdr));
            sublen = ntohs(subhdr->length);

            /* for each subheader */
            for (msg_count = hdr->count; (msg_count); (msg_count--),
                 subhdr = (typeof(subhdr))((uint8_t *)subhdr + sublen),
                 sublen = ntohs(subhdr->length)) {

                if (subhdr->err_code != JNX_GW_MSG_ERR_NO_ERR) {
                    continue;
                }

                pmsg = (typeof(pmsg))((uint8_t *)subhdr + sizeof(*subhdr));

                /* this is a gre session/summary stat message */

                switch (subhdr->sub_type) {

                    case JNX_GW_FETCH_GRE_SESN:
                        XML_OPEN(msp, ODCI_JNX_GATEWAY_SESN_SET);
                        jnx_gw_mgmt_show_gre_sesn(msp, pmsg);
                        jnx_gw_mgmt_show_gre_ingress(msp, pmsg);
                        jnx_gw_mgmt_show_gre_egress(msp, pmsg);
                        XML_CLOSE(msp, ODCI_JNX_GATEWAY_SESN_SET);
                        break;

                    case JNX_GW_FETCH_GRE_SESN_SUM :
                    case JNX_GW_FETCH_GRE_SESN_VRF_SUM:
                    case JNX_GW_FETCH_GRE_SESN_GW_SUM:
                        jnx_gw_mgmt_show_gre_sesn_sum (msp, pctrl_pic, pmsg);
                        break;


                    default:
                        break;
                }
            }
        }
        XML_CLOSE(msp, ODCI_JNX_GATEWAY_GRE_TUNNEL);
        if (ctrl_flag == TRUE) break;
    }

    XML_CLOSE(msp, ODCI_JNX_GATEWAY_GRE_INFORMATION);
    return 0;
}

/**
 * This function parses the mgd params for extracting the
 * filters, for "show jnx-gateway ipip-tunnels"
 * sends the request to the control pic agents,
 * waits & collects the information from the control 
 * pic agents, & calles appropriate fillup routines
 * & dispatches them to the mgd for display 
 */
static int
jnx_gw_mgmt_show_ipip_tunnels(mgmt_sock_t *msp, parse_status_t *csb,
                              char * unparsed)
{
    uint8_t msg_count = 0, sub_type = 0;
    uint8_t more = TRUE, ctrl_flag = FALSE;
    uint16_t sublen, len;
    int32_t vrf_id = JNX_GW_INVALID_VRFID;
    uint32_t gw_ip = 0;
    uint32_t verbose = ms_parse_get_subcode(csb);
    char * vrf_name = NULL, * word = NULL;
    ipc_msg_t * ipc_msg = NULL;
    void * pmsg = NULL;
    jnx_gw_msg_header_t  * hdr = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_mgmt_ctrl_session_t * pctrl_pic = NULL;
    jnx_gw_msg_ctrl_ipip_sesn_stat_t * pipip_stat = NULL;
    
    if (verbose >= LEVEL_DETAIL)
        sub_type |= JNX_GW_CTRL_VERBOSE_SET;

    if (unparsed) {

        while ((word = strsep(&unparsed, MGMT_PARSE_TOKEN_SEP))) {

            if (!strncmp(word, "INSTANCE", strlen(word))) {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                if ((vrf_id = jnx_gw_get_vrf_id(word))
                    != JNX_GW_INVALID_VRFID) { 
                    vrf_name = word;
                    sub_type |= JNX_GW_CTRL_VRF_SET;
                    continue;
                }

                /* no such vrf, return */
                XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION,
                         xml_attr_xmlns(XML_NS));
                XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION);

                return 0;

                continue;
            }

            if (!strncmp(word, "IPIP", strlen(word))) {

                word  = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                gw_ip = jnx_gw_get_ipaddr(word);
                sub_type |= JNX_GW_CTRL_GW_SET;

                continue;
            }

            if (!strncmp(word, "INTERFACE", strlen(word))) {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                if ((pctrl_pic = jnx_gw_mgmt_ctrl_sesn_lookup(word))) {
                    continue;
                }

                /* this ifd does not have active control agent, return */
                XML_OPEN(msp, ODCI_JNX_GATEWAY_IPIP_INFORMATION,
                         xml_attr_xmlns(XML_NS));

                XML_CLOSE(msp, ODCI_JNX_GATEWAY_IPIP_INFORMATION);
                return 0;
            }
        }
    }

    /* for gateway, we need the vrf id also */

    if ((gw_ip != 0) && (vrf_id == JNX_GW_INVALID_VRFID)) {
        XML_OPEN(msp, ODCI_JNX_GATEWAY_IPIP_INFORMATION,
                 xml_attr_xmlns(XML_NS));

        XML_CLOSE(msp, ODCI_JNX_GATEWAY_IPIP_INFORMATION);
        return 0;
    }

    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);

    /* fillup the request message */
    hdr->msg_type  = JNX_GW_STAT_FETCH_MSG;
    len = sizeof(*hdr);
    sublen = sizeof(*subhdr) + sizeof(*pipip_stat);

    subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
    subhdr->sub_type = JNX_GW_FETCH_IPIP_SESN | sub_type;
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
    subhdr->length   = ntohs(sublen);

    pipip_stat = (typeof(pipip_stat))((uint8_t*)subhdr + sizeof(*subhdr));
    memset(pipip_stat, 0, sizeof(*pipip_stat));
    pipip_stat->ipip_vrf_id = htonl(vrf_id);
    pipip_stat->ipip_gw_ip  = htonl(gw_ip);

    len += sublen;

    msg_count++;

    /* update the header fields, with length & count */
    hdr->count   = msg_count;
    hdr->msg_len = htons(len);

    /* send the fetch messsage to the control pics */
    jnx_gw_mgmt_ctrl_send_opcmd(pctrl_pic, JNX_GW_STAT_FETCH_MSG, hdr, len);

    XML_OPEN(msp, ODCI_JNX_GATEWAY_IPIP_INFORMATION, xml_attr_xmlns(XML_NS));

    /* if control pic id is provided,
       send query to the speicific control pic */
    if (pctrl_pic != NULL) {
        ctrl_flag = TRUE;
        goto show_ipip_recv_msg;
    }

    while ((pctrl_pic = jnx_gw_mgmt_get_next_ctrl_sesn(pctrl_pic))) {

show_ipip_recv_msg:

        jnx_gw_mgmt_show_ctrl_pic (msp, pctrl_pic, vrf_name);
        more = TRUE;
        XML_OPEN(msp, ODCI_JNX_GATEWAY_IPIP);

        while ((more) && (ipc_msg = jnx_gw_mgmt_ctrl_recv_opcmd(pctrl_pic))) {

            hdr = (typeof(hdr))ipc_msg->data;

            assert (hdr->msg_type == JNX_GW_STAT_FETCH_MSG);

            more   = hdr->more;
            len    = ntohs(hdr->msg_len);
            subhdr = (typeof(subhdr))((uint8_t *)hdr + sizeof(*hdr));
            sublen = ntohs(subhdr->length);

            /* for each subheader */
            for (msg_count = hdr->count; (msg_count); (msg_count--),
                 subhdr = (typeof(subhdr))((uint8_t *)subhdr + sublen),
                 sublen = ntohs(subhdr->length)) {

                if (subhdr->err_code != JNX_GW_MSG_ERR_NO_ERR) {
                    continue;
                }

                pmsg = (typeof(pmsg))((uint8_t *)subhdr + sizeof(*subhdr));

                /* this is a ipip gateway sesn/summary stat message */

                switch(subhdr->sub_type) {

                    case JNX_GW_FETCH_IPIP_SESN:
                        XML_OPEN(msp, ODCI_JNX_GATEWAY_IPIP_TUNNEL_SET);
                        jnx_gw_mgmt_show_ipip_tunnel(msp, pmsg);
                        XML_CLOSE(msp, ODCI_JNX_GATEWAY_IPIP_TUNNEL_SET);
                        break;

                    case JNX_GW_FETCH_IPIP_SESN_SUM:
                        XML_OPEN(msp, ODCI_JNX_GATEWAY_IPIP_HEADER);
                        jnx_gw_mgmt_show_ipip_header(msp, pmsg);
                        XML_CLOSE(msp, ODCI_JNX_GATEWAY_IPIP_HEADER);
                        break;

                    default:
                        break;
                } 
            }
        }
        XML_CLOSE(msp, ODCI_JNX_GATEWAY_IPIP);
        if (ctrl_flag == TRUE) break;
    }
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_IPIP_INFORMATION);
    return 0;
}

/**
 * This function parses the mgd params for extracting the
 * filters, for "show jnx-gateway statistics"
 * sends the request to the data pic agents,
 * waits & collects the information from the data 
 * pic agents, & calles appropriate fillup routines
 * & dispatches them to the mgd for display 
 */
static int
jnx_gw_mgmt_show_stat(mgmt_sock_t *msp, parse_status_t *csb,
                      char * unparsed __unused)
{
    uint8_t msg_count = 0, more = TRUE, data_flag = FALSE;
    uint16_t sublen, len;
    uint32_t gw_ip = 0, gre_key = 0;
    int32_t vrf_id = JNX_GW_INVALID_VRFID;
    ipc_msg_t * ipc_msg = NULL;
    char * vrf_name = NULL;
    jnx_gw_msg_header_t  * hdr = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_mgmt_data_session_t * pdata_pic = NULL;
    int32_t verbose = ms_parse_get_subcode(csb);
    uint8_t msg_type = JNX_GW_FETCH_SUMMARY_STAT;
    char * word = NULL;
    void * pmsg = NULL;

    if (verbose == LEVEL_EXTENSIVE)
        msg_type = JNX_GW_FETCH_EXTENSIVE_STAT;

    if (unparsed) {

        while ((word = strsep(&unparsed, MGMT_PARSE_TOKEN_SEP))) {

            if (!strncmp(word, "INSTANCE", strlen(word))) {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                if ((vrf_id = jnx_gw_get_vrf_id(word))
                    != JNX_GW_INVALID_VRFID) { 
                    vrf_name = word;
                    if (msg_type == JNX_GW_FETCH_EXTENSIVE_STAT)
                        msg_type = JNX_GW_FETCH_EXTENSIVE_VRF_STAT;
                    else
                        msg_type = JNX_GW_FETCH_SUMMARY_VRF_STAT;
                    continue;
                }

                /* no such vrf, return */
                XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION,
                         xml_attr_xmlns(XML_NS));
                XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION);

                return 0;
            }

            if (!strncmp(word, "IPIP", strlen(word))) {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                if ((gw_ip = jnx_gw_get_ipaddr(word))) {
                    msg_type = JNX_GW_FETCH_IPIP_STAT;
                }

                continue;
            } 

            if (!strncmp(word, "GREKEY", strlen(word))) {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                if ((gre_key = atoi(word))) {
                    msg_type = JNX_GW_FETCH_GRE_STAT;
                }

                continue;
            }

            if (!strncmp(word, "INTERFACE", strlen(word))) {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }

                if ((pdata_pic = jnx_gw_mgmt_data_sesn_lookup(word))) {
                    continue;
                }

                /* this ifd does not have active data agent, return */
                XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION,
                         xml_attr_xmlns(XML_NS));
                XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION);

                return 0;
            }
        }
    }

    /* for gre key, & ipip gateway, we need the 
       vrf id also */
    if (((msg_type == JNX_GW_FETCH_IPIP_STAT) ||
         (msg_type == JNX_GW_FETCH_GRE_STAT)) &&
        (vrf_id == JNX_GW_INVALID_VRFID)) {

        XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION,
                 xml_attr_xmlns(XML_NS));
        XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION);

        return 0;
    }

    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    memset(hdr, 0, sizeof(*hdr));

    /* fill up the request message */
    len = sizeof(*hdr);
    hdr->msg_type  = JNX_GW_STAT_FETCH_MSG;

    sublen = sizeof(*subhdr);
    subhdr = (typeof(subhdr))((uint8_t *)hdr + len);

    subhdr->sub_type = msg_type;
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;

    switch (msg_type) {

        case JNX_GW_FETCH_EXTENSIVE_VRF_STAT:
            {
                jnx_gw_msg_fetch_ext_vrf_t * pstat;
                pstat = (typeof(pstat))((uint8_t *)subhdr + sublen);
                pstat->vrf = htonl(vrf_id);
                sublen    += sizeof(*pstat);
            }
            break;

        case JNX_GW_FETCH_SUMMARY_VRF_STAT:
            {
                jnx_gw_msg_fetch_summary_vrf_t * pstat;
                pstat = (typeof(pstat))((uint8_t *)subhdr + sublen);
                pstat->vrf = htonl(vrf_id);
                sublen    += sizeof(*pstat);
            }
            break;

        case JNX_GW_FETCH_IPIP_STAT:
            {
                jnx_gw_msg_fetch_ipip_t * pstat;
                pstat = (typeof(pstat))((uint8_t *)subhdr + sublen);
                pstat->vrf        = htonl(vrf_id);
                pstat->gateway_ip = htonl(gw_ip);
                sublen           += sizeof(*pstat);
            }
            break;

        case JNX_GW_FETCH_GRE_STAT:
            {
                jnx_gw_msg_fetch_gre_t * pstat;
                pstat = (typeof(pstat))((uint8_t *)subhdr + sublen);
                pstat->vrf     = htonl(vrf_id);
                pstat->gre_key = htonl(gre_key);
                sublen        += sizeof(*pstat);
            }
            break;
        default:
            break;
    }

    subhdr->length  = ntohs(sublen);

    len += sublen;

    msg_count++;

    /* update the header fields, with length & count */
    hdr->count   = msg_count;
    hdr->msg_len = htons(len);

    /* send the fetch messsage to the data pics */
    jnx_gw_mgmt_data_send_opcmd(pdata_pic, JNX_GW_STAT_FETCH_MSG, hdr, len);

    XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION, xml_attr_xmlns(XML_NS));

    /* if data pic id is provided, send the query to the data pic */

    if (pdata_pic != NULL) {
        data_flag = TRUE;
        goto show_stat_recv_msg;
    }

    while ((pdata_pic = jnx_gw_mgmt_get_next_data_sesn(pdata_pic))) {

show_stat_recv_msg:

        jnx_gw_mgmt_show_data_pic (msp, pdata_pic, vrf_name);

        more = TRUE;

        while ((more) && (ipc_msg = jnx_gw_mgmt_data_recv_opcmd(pdata_pic))) {

            hdr = (typeof(hdr))ipc_msg->data;

            assert (hdr->msg_type == JNX_GW_STAT_FETCH_MSG);

            more   = hdr->more;
            len    = ntohs(hdr->msg_len);
            subhdr = (typeof(subhdr))((uint8_t *)hdr + sizeof(*hdr));
            sublen = ntohs(subhdr->length);

            /* for each subheader */
            for (msg_count = hdr->count; (msg_count); (msg_count--),
                 subhdr = (typeof(subhdr))((uint8_t *)subhdr + sublen),
                 sublen = ntohs(subhdr->length)) {
                
                if (subhdr->err_code != JNX_GW_MSG_ERR_NO_ERR) {
                    continue;
                }

                pmsg = (typeof(pmsg))((uint8_t *)subhdr + sizeof(*subhdr));

                /* process the stats message */
                switch (subhdr->sub_type) {

                    case JNX_GW_FETCH_SUMMARY_STAT:
                    case JNX_GW_FETCH_SUMMARY_VRF_STAT:
                        XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_SUMMARY);
                        jnx_gw_mgmt_show_stat_summary(msp, pdata_pic, pmsg);
                        XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_SUMMARY);
                        break;

                    case JNX_GW_FETCH_IPIP_STAT:
                        XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_EXTENSIVE);
                        XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_IPIP_TUN_SET);
                        jnx_gw_mgmt_show_stat_ipip_tun(msp, pdata_pic, pmsg);
                        XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_IPIP_TUN_SET);
                        XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_EXTENSIVE);
                        break;

                    case JNX_GW_FETCH_GRE_STAT:
                        XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_EXTENSIVE);
                        XML_OPEN(msp, ODCI_JNX_GATEWAY_STAT_GRE_SESN_SET);
                        jnx_gw_mgmt_show_stat_gre_sesn(msp, pdata_pic, pmsg);
                        XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_GRE_SESN_SET);
                        XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_EXTENSIVE);
                        break;
                }
            }
        }
        if (data_flag == TRUE) break;
    }
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_STAT_INFORMATION);
    return 0;
}

/**
 * This function parses the mgd params for extracting the
 * filters, for "show jnx-gateway user"
 * sends the request to the control pic agents,
 * waits & collects the information from the control 
 * pic agents, & calles appropriate fillup routines
 * & dispatches them to the mgd for display 
 */
static int
jnx_gw_mgmt_show_users(mgmt_sock_t *msp, parse_status_t *csb __unused,
                       char * unparsed)
{
    uint8_t msg_count = 0, more = TRUE;
    uint16_t len = 0, sublen = 0;
    char *user_name = NULL, * word = NULL;
    ipc_msg_t * ipc_msg = NULL;
    jnx_gw_mgmt_user_t * puser = NULL;
    jnx_gw_msg_header_t * hdr = NULL;
    jnx_gw_msg_sub_header_t * subhdr = NULL;
    jnx_gw_msg_ctrl_user_stat_t  * puser_stat = NULL;
    jnx_gw_mgmt_ctrl_session_t * pctrl_pic = NULL;

    /* if user name is specified, and the user does not exist return */
    if (unparsed) {

        while ((word = strsep(&unparsed, MGMT_PARSE_TOKEN_SEP))) {

            if (!strncmp(word, "POLICY", strlen(word)))  {

                word = strsep(&unparsed,MGMT_PARSE_TOKEN_SEP);

                if (!strncmp(word, "all", strlen(word))) 
                    continue;

                user_name = word;

                if ((puser = jnx_gw_mgmt_lookup_user (word))) {
                    continue;
                }

                /* could not find the user, return */
                XML_OPEN(msp, ODCI_JNX_GATEWAY_USER_INFORMATION,
                         xml_attr_xmlns(XML_NS));
                XML_CLOSE(msp, ODCI_JNX_GATEWAY_USER_INFORMATION);
                return 0;
            }
        }
    }

    hdr = (typeof(hdr))(jnx_gw_mgmt.send_buf);
    memset(hdr, 0, sizeof(*hdr));
    len = sizeof(*hdr);

    /* fill up the request message */
    hdr->msg_type  = JNX_GW_STAT_FETCH_MSG;

    subhdr = (typeof(subhdr))((uint8_t *)hdr + len);
    subhdr->sub_type = JNX_GW_FETCH_USER_STAT;
    subhdr->err_code = JNX_GW_MSG_ERR_NO_ERR;
    sublen           = sizeof(*subhdr);

    puser_stat = (typeof(puser_stat))((uint8_t*)subhdr + sublen);

    /* clear the stats in local structures */

    if (puser) {
        puser->user_sesn_count        = 0;
        puser->user_active_sesn_count = 0;

        strncpy (puser_stat->user_name, user_name,
                    sizeof(puser_stat->user_name));
    } else {

        /* reset the counters, we will summate
         * the session stat from the control pics
         */
        while ((puser = jnx_gw_mgmt_get_next_user(puser))) {
            puser->user_active_sesn_count = 0;
            puser->user_sesn_count        = 0;
        }

        /*
         * send one subhdr, with the user name field reset, that will mean
         * retrieve all the user stat information
         */

        memset (puser_stat, 0, sizeof(*puser_stat));
    }

    msg_count++;
    sublen        += sizeof(*puser_stat);
    subhdr->length = ntohs(sublen);
    len           += sublen;

    /* update the header fields, with length & count */
    hdr->count   = msg_count;
    hdr->msg_len = htons(len);

    /* send the fetch messsage to all control pics */
    jnx_gw_mgmt_ctrl_send_opcmd(NULL, JNX_GW_STAT_FETCH_MSG, hdr, len);

    /* all the request messages are sent, process the 
       replies from the control app agent(s) */

    /* loop for all the control pics */
    while ((pctrl_pic = jnx_gw_mgmt_get_next_ctrl_sesn(pctrl_pic))) {

        more = TRUE;

        while ((more) && (ipc_msg = jnx_gw_mgmt_ctrl_recv_opcmd(pctrl_pic))) {

            hdr = (typeof(hdr))ipc_msg->data;

            assert (hdr->msg_type == JNX_GW_STAT_FETCH_MSG);

            more   = hdr->more;
            len    = ntohs(hdr->msg_len);
            subhdr = (typeof(subhdr))((uint8_t *)hdr + sizeof(*hdr));
            sublen = ntohs(subhdr->length);

            /* for each subheader */
            for (msg_count = hdr->count; (msg_count); (msg_count--),
                 subhdr = (typeof(subhdr))((uint8_t *)subhdr + sublen),
                 sublen = ntohs(subhdr->length)) {

                /* check whether this is a user stat message */

                if ((subhdr->sub_type != JNX_GW_FETCH_USER_STAT) ||
                    (subhdr->err_code != JNX_GW_MSG_ERR_NO_ERR)) {
                    continue;
                }

                puser_stat = 
                    (typeof(puser_stat))((uint8_t *)subhdr + sizeof(*subhdr));

                /* get the user based on the user name */
                if ((puser = 
                     jnx_gw_mgmt_lookup_user (puser_stat->user_name))) {
                    /* update the user stat in the config structure */
                    jnx_gw_mgmt_update_user_stat(puser, puser_stat);
                }
            }
        }
    }

    /* open XML for the user stat */
    XML_OPEN(msp, ODCI_JNX_GATEWAY_USER_INFORMATION, xml_attr_xmlns(XML_NS));

    /* print the header information */
    jnx_gw_mgmt_show_user_header(msp);

    XML_OPEN(msp, ODCI_JNX_GATEWAY_USER_SET, xml_attr_xmlns(XML_NS));

    if (user_name) {
        if ((puser = jnx_gw_mgmt_lookup_user (user_name))) {
            jnx_gw_mgmt_show_user(msp, puser);
        }
    }
    else { 
        puser = NULL;
        /* show all users */
        while ((puser = jnx_gw_mgmt_get_next_user(puser))) {
            jnx_gw_mgmt_show_user(msp, puser);
        }
    }

    XML_CLOSE(msp, ODCI_JNX_GATEWAY_USER_SET);
    XML_CLOSE(msp, ODCI_JNX_GATEWAY_USER_INFORMATION);
    return 0;
}


/**
 * data structures for operational command structure
 * registration with the mgd infrastructure
 */
static const parse_menu_t show_jnx_gw_gre_menu[] = {
    { "summary", NULL, LEVEL_BRIEF, NULL, jnx_gw_mgmt_show_gre_tunnels},
    { "extensive", NULL, LEVEL_EXTENSIVE, NULL, jnx_gw_mgmt_show_gre_tunnels},
    { NULL, NULL, 0, NULL, NULL}
};

static const parse_menu_t show_jnx_gw_ipip_menu[] = {
    { "summary", NULL, LEVEL_BRIEF, NULL, jnx_gw_mgmt_show_ipip_tunnels},
    { "extensive", NULL, LEVEL_EXTENSIVE, NULL, jnx_gw_mgmt_show_ipip_tunnels},
    { NULL, NULL, 0, NULL, NULL}
};

static const parse_menu_t show_jnx_gw_stat_menu[] = {
    { "summary", NULL, LEVEL_BRIEF, NULL, jnx_gw_mgmt_show_stat},
    { "extensive", NULL, LEVEL_EXTENSIVE, NULL, jnx_gw_mgmt_show_stat},
    { NULL, NULL, 0, NULL, NULL}
};

static const parse_menu_t show_jnx_gw_menu [] = {
    { "gre-tunnels", NULL, 0, show_jnx_gw_gre_menu, NULL},
    { "ipip-tunnels", NULL, 0, show_jnx_gw_ipip_menu, NULL},
    { "statistics", NULL, 0, show_jnx_gw_stat_menu, NULL},
    { "user-profiles", NULL, 0, NULL, jnx_gw_mgmt_show_users},
    { NULL, NULL, 0, NULL, NULL}
};

static const parse_menu_t show_version_menu[] = {
    { "brief", NULL, LEVEL_BRIEF, NULL, jnx_gw_mgmt_show_version},
    { "detail", NULL, LEVEL_DETAIL, NULL, jnx_gw_mgmt_show_version},
    { "extensive", NULL, LEVEL_EXTENSIVE, NULL, jnx_gw_mgmt_show_version},
    { NULL, NULL, 0, NULL, NULL }
};

static const parse_menu_t show_menu[] = {
    { "jnx-gateway", NULL, 0, show_jnx_gw_menu, NULL},
    { "version", NULL, 0, show_version_menu, NULL},
    { NULL, NULL, 0, NULL, NULL}
};

const parse_menu_t master_menu[]  = {
    { "show", NULL, 0, show_menu, NULL},
    { NULL, NULL, 0, NULL, NULL}
};

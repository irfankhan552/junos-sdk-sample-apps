/*
 * $Id: jnx-flow-mgmt_ui.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-gateway-mgmt_ui.c - SDK gateway application UI routines
 *
 * Srinibas Maharana, Jan 2007
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
 * @file : jnx-flow-mgmt_ui.c
 * @brief
 * This file contains the ui display handler routines
 *
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
#include <junoscript/xmllib_pub.h>
#include <junoscript/xmlrpc.h>
#include <jnx/parse_ip.h>

#include JNX_FLOW_MGMT_SEQUENCE_H
#include JNX_FLOW_MGMT_OUT_H
#include JNX_FLOW_MGMT_ODL_H

#include <jnx/jnx-flow.h>
#include <jnx/jnx-flow_msg.h>

#include "jnx-flow-mgmt.h"
#include "jnx-flow-mgmt_config.h"


#define LEVEL_SUMMARY   0
#define LEVEL_BRIEF     1
#define LEVEL_DETAIL    2
#define LEVEL_EXTENSIVE 3
#define LEVEL_TERSE     4

#define TOKEN_ALL       "all"
#define TOKEN_EXTENSIVE "extensive"
#define TOKEN_SUMMARY   "summary"
#define TOKEN_DETAIL    "detail"
#define TOKEN_TERSE     "terse"

/**
 * @file  jnx-flow-mgmt_ui.c
 * @brief
 * This file contains the mgd interface functions for displaying
 * operational command outputs
 * These routines fill up the messages, with the stats gathered
 * from the data pic agents across services pics.
 *
 */

inline void
jnx_flow_mgmt_show_flow_summary(mgmt_sock_t * msp,
                                jnx_flow_msg_stat_flow_summary_info_t * pinfo);

inline void
jnx_flow_mgmt_show_flow_entry(mgmt_sock_t * msp,
                             jnx_flow_msg_stat_flow_info_t * pinfo);

/**
 * This function dispatches the xml-tagged flow summary information
 * to the cli agent
 */
inline void
jnx_flow_mgmt_show_flow_summary(mgmt_sock_t * msp,
                                jnx_flow_msg_stat_flow_summary_info_t * pinfo)
{
    XML_OPEN(msp, ODCI_JNX_FLOW_FLOW_TABLE_SUMMARY);

    XML_ELT(msp, ODCI_JNX_FLOW_SERVICE_SET_COUNT, "%d",
            ntohl(pinfo->stats.svc_set_count));

    XML_ELT(msp, ODCI_JNX_FLOW_RULE_COUNT, "%d",
            ntohl(pinfo->stats.rule_count));
    XML_ELT(msp, ODCI_JNX_FLOW_APPLIED_RULE_COUNT, "%d",
            ntohl(pinfo->stats.applied_rule_count));

    XML_ELT(msp, ODCI_JNX_FLOW_ACTIVE_SESN_COUNT, "%d",
            ntohl(pinfo->stats.active_flow_count));
    XML_ELT(msp, ODCI_JNX_FLOW_SESN_COUNT, "%d",
            ntohl(pinfo->stats.total_flow_count));

    XML_ELT(msp, ODCI_JNX_FLOW_ALLOW_ACTIVE_SESN_COUNT, "%d",
            ntohl(pinfo->stats.active_allow_flow_count));
    XML_ELT(msp, ODCI_JNX_FLOW_ALLOW_SESN_COUNT, "%d",
            ntohl(pinfo->stats.total_allow_flow_count));

    XML_ELT(msp, ODCI_JNX_FLOW_DROP_ACTIVE_SESN_COUNT, "%d",
            ntohl(pinfo->stats.active_drop_flow_count));
    XML_ELT(msp, ODCI_JNX_FLOW_DROP_SESN_COUNT, "%d",
            ntohl(pinfo->stats.total_drop_flow_count));

    XML_CLOSE(msp, ODCI_JNX_FLOW_FLOW_TABLE_SUMMARY);
}

/**
 * This function dispatches the xml-tagged flow entry information
 * to the cli agent
 */
inline void
jnx_flow_mgmt_show_flow_entry(mgmt_sock_t * msp,
                             jnx_flow_msg_stat_flow_info_t * pinfo)
{
    char addr_name[JNX_FLOW_STR_SIZE];

    XML_OPEN(msp, ODCI_JNX_FLOW_FLOW_TABLE_FLOW_SET);
    XML_OPEN(msp, ODCI_JNX_FLOW_FLOW_TABLE_FLOW_STAT);

    if (pinfo->flow_info.svc_set_name[0]) {
        XML_ELT(msp, ODCI_JNX_FLOW_SERVICE_SET_NAME, "\"%s\"",
                pinfo->flow_info.svc_set_name);
    }

    XML_ELT(msp, ODCI_JNX_FLOW_SRC_IP, "\"%s\"",
            inet_ntop(AF_INET, &pinfo->flow_info.src_addr, 
                      addr_name, sizeof(addr_name)));

    XML_ELT(msp, ODCI_JNX_FLOW_DEST_IP, "\"%s\"",
            inet_ntop(AF_INET, &pinfo->flow_info.dst_addr, 
                      addr_name, sizeof(addr_name)));

    XML_ELT(msp, ODCI_JNX_FLOW_SRC_PORT, "\"%d\"",
            ntohs(pinfo->flow_info.src_port));

    XML_ELT(msp, ODCI_JNX_FLOW_DST_PORT, "\"%d\"",
            ntohs(pinfo->flow_info.dst_port));

    XML_ELT(msp, ODCI_JNX_FLOW_PROTOCOL, "\"%d\"",
            pinfo->flow_info.proto);

    XML_ELT(msp, ODCI_JNX_FLOW_FLOW_DIRECTION, "\"%s\"",
            jnx_flow_dir_str[pinfo->flow_info.flow_dir]);

    XML_ELT(msp, ODCI_JNX_FLOW_FLOW_ACTION, "\"%s\"",
            jnx_flow_action_str[pinfo->flow_info.flow_action]);

    XML_ELT(msp, ODCI_JNX_FLOW_IN_BYTES, "%d",
            ntohl(pinfo->flow_stats.bytes_in));

    XML_ELT(msp, ODCI_JNX_FLOW_IN_PACKETS, "%d",
            ntohl(pinfo->flow_stats.pkts_in));

    if (pinfo->flow_info.flow_action == JNX_FLOW_RULE_ACTION_ALLOW) {
        XML_ELT(msp, ODCI_JNX_FLOW_ALLOWED_BYTES, "%d",
                ntohl(pinfo->flow_stats.bytes_out));

        XML_ELT(msp, ODCI_JNX_FLOW_ALLOWED_PACKETS, "%d",
                ntohl(pinfo->flow_stats.pkts_out));
    } else {

        XML_ELT(msp, ODCI_JNX_FLOW_DROPPED_BYTES, "%d",
                ntohl(pinfo->flow_stats.bytes_dropped));

        XML_ELT(msp, ODCI_JNX_FLOW_DROPPED_PACKETS, "%d",
                ntohl(pinfo->flow_stats.pkts_dropped));
    }

    XML_CLOSE(msp, ODCI_JNX_FLOW_FLOW_TABLE_FLOW_STAT);

    XML_CLOSE(msp, ODCI_JNX_FLOW_FLOW_TABLE_FLOW_SET);
}

/**
 * This function displays the flow table information
 */
static int
jnx_flow_display_flow_table(mgmt_sock_t * msp,
                            jnx_flow_msg_sub_header_info_t * sub_hdr,
                            uint32_t msg_count)
{
    uint32_t sub_len = 0;
    jnx_flow_msg_stat_flow_info_t         *flow_info;
    jnx_flow_msg_stat_flow_summary_info_t *summary_info;

    XML_OPEN(msp, ODCI_JNX_FLOW_FLOW_TABLE_EXTENSIVE);
    for (;(msg_count); msg_count--, 
         sub_hdr = (typeof(sub_hdr))((uint8_t*)sub_hdr + sub_len)) {

         sub_len = ntohs(sub_hdr->msg_len);

        if (sub_hdr->err_code != JNX_FLOW_ERR_NO_ERROR) {
            continue;
        }
        switch (sub_hdr->msg_type) {

            case JNX_FLOW_MSG_FETCH_FLOW_SUMMARY:
                summary_info = (typeof(summary_info))
                    ((uint8_t *)sub_hdr + sizeof(*sub_hdr));
                jnx_flow_mgmt_show_flow_summary(msp, summary_info);
                break;

            case JNX_FLOW_MSG_FETCH_FLOW_ENTRY:
                flow_info = (typeof(flow_info))
                    ((uint8_t *)sub_hdr + sizeof(*sub_hdr));
                jnx_flow_mgmt_show_flow_entry(msp, flow_info);
                break;
            default:
                break;
        }
    }
    XML_CLOSE(msp, ODCI_JNX_FLOW_FLOW_TABLE_EXTENSIVE);
    return 0;
}

/**
 * This function displays the ms-pic agent information
 */
static status_t
jnx_flow_mgmt_show_pic_stats(mgmt_sock_t * msp,
                             jnx_flow_mgmt_data_session_t * pdata_pic,
                             jnx_flow_msg_header_info_t * msg_hdr)
{
    uint32_t more = TRUE, msg_count = 0;
    jnx_flow_msg_sub_header_info_t * sub_hdr = NULL;

    XML_OPEN(msp, ODCI_JNX_FLOW_PIC);
    XML_ELT(msp, ODCI_INTERFACE_NAME, "\"%s\"", pdata_pic->pic_name);
    XML_CLOSE(msp, ODCI_JNX_FLOW_PIC);

    while (msg_hdr) {
        sub_hdr = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));

        msg_count = msg_hdr->msg_count;
        more      = msg_hdr->more;

        switch (msg_hdr->msg_type) {
            case JNX_FLOW_MSG_FETCH_FLOW_INFO:
                jnx_flow_display_flow_table(msp, sub_hdr, msg_count);
                break;
            default:
                return EFAIL;
        }
        msg_hdr = (more) ? jnx_flow_mgmt_recv_opcmd_msg(pdata_pic) : NULL;
    }
    return EOK;
}

/**
 * This function handles various show commands responses
 */
static status_t
jnx_flow_mgmt_show_cmd(mgmt_sock_t * msp,
                       jnx_flow_mgmt_data_session_t * pdata_pic,
                       jnx_flow_msg_header_info_t * msg_hdr,
                       uint32_t msg_type, uint32_t msg_len)
{
    jnx_flow_msg_header_info_t * rsp_hdr = NULL;

    if (pdata_pic) {
        if ((rsp_hdr = jnx_flow_mgmt_send_opcmd_msg(pdata_pic, msg_hdr,
                                                    msg_type, msg_len))) {
            jnx_flow_mgmt_show_pic_stats(msp, pdata_pic, rsp_hdr);
        }
        return EOK;
    }

    while ((pdata_pic = (typeof(pdata_pic))
            patricia_find_next(&jnx_flow_mgmt.data_sesn_db,
                               &pdata_pic->pic_node))) {
        if ((rsp_hdr = jnx_flow_mgmt_send_opcmd_msg(pdata_pic, msg_hdr,
                                                    msg_type, msg_len))) {
            jnx_flow_mgmt_show_pic_stats(msp, pdata_pic, rsp_hdr);
        }
    }
    return EOK;
}

/**
 * This function handles flow table show command request
 */
static int
jnx_flow_mgmt_show_flow_table (mgmt_sock_t * msp, parse_status_t *csb,
                               char * unparsed)
{
    char * word = NULL;
    uint32_t msg_len = 0, sub_len = 0;
    uint32_t verbose = ms_parse_get_subcode(csb);

    jnx_flow_msg_type_t msg_type = JNX_FLOW_MSG_FETCH_FLOW_INFO;
    jnx_flow_fetch_flow_type_t sub_type = JNX_FLOW_MSG_FETCH_FLOW_INVALID;

    jnx_flow_msg_header_info_t * msg_hdr = NULL;
    jnx_flow_msg_sub_header_info_t* sub_hdr = NULL;

    jnx_flow_mgmt_data_session_t * pdata_pic = NULL;
    jnx_flow_flow_info_t       * flow_info = NULL;

    if (verbose >= LEVEL_DETAIL)
        sub_type = JNX_FLOW_MSG_FETCH_FLOW_EXTENSIVE;
    else 
        sub_type = JNX_FLOW_MSG_FETCH_FLOW_SUMMARY; 

    if (unparsed) {

        while ((word = strsep(&unparsed, MGMT_PARSE_TOKEN_SEP))) {
            /*
             * get the ms-pic name
             */
            if (!strncmp(word, "INTERFACE", strlen(word))) {
                word = strsep(&unparsed, MGMT_PARSE_TOKEN_SEP);
                if (!strncmp(word, "all", strlen(word))) {
                    continue;
                }
                if ((pdata_pic = jnx_flow_mgmt_data_sesn_lookup(word)) &&
                    (pdata_pic->cmd_conn != NULL)) {
                    continue;
                }
                XML_OPEN(msp, ODCI_JNX_FLOW_FLOW_TABLE_INFORMATION,
                         xml_attr_xmlns(XML_NS));
                XML_CLOSE(msp, ODCI_JNX_FLOW_FLOW_TABLE_INFORMATION);
		return 0;
            }
        }
    }

    sub_len = sizeof(*sub_hdr) + sizeof(*flow_info);
    msg_len = sizeof(*msg_hdr) + sub_len;

    msg_hdr   = (typeof(msg_hdr))jnx_flow_mgmt.send_buf;
    sub_hdr   = (typeof(sub_hdr))((uint8_t *)msg_hdr + sizeof(*msg_hdr));
    flow_info = (typeof(flow_info))((uint8_t *)sub_hdr + sizeof(*sub_hdr));

    memset(msg_hdr, 0, msg_len);

    msg_hdr->msg_type  = msg_type;
    msg_hdr->msg_count = 1;
    msg_hdr->msg_len   = htons(msg_len);

    sub_hdr->msg_type = sub_type;
    sub_hdr->err_code = JNX_FLOW_ERR_NO_ERROR;
    sub_hdr->msg_len  = htons(sub_len);

    XML_OPEN(msp, ODCI_JNX_FLOW_FLOW_TABLE_INFORMATION, xml_attr_xmlns(XML_NS));

    jnx_flow_mgmt_show_cmd(msp, pdata_pic, msg_hdr, msg_type, msg_len);

    XML_CLOSE(msp, ODCI_JNX_FLOW_FLOW_TABLE_INFORMATION);
    return 0;
}

/**
 * This function handles show version request
 */
static int
jnx_flow_mgmt_show_version (mgmt_sock_t * msp __unused,
                            parse_status_t *csb __unused,
                            char * unparsed __unused)
{
    uint32_t verbose = ms_parse_get_subcode(csb);

    if (verbose == LEVEL_BRIEF) {
        xml_show_version(msp, NULL, FALSE);
    } else {
        xml_show_version(msp, NULL, TRUE);
    }
    return 0;
}


/*
 * jnx-flow operational commands hierarchy menus
 */

/*
 * show jnx-flow flow-table
 */
const parse_menu_t jnx_flow_show_flow_table_menu[]  = {
    {"summary", NULL, LEVEL_BRIEF, NULL, jnx_flow_mgmt_show_flow_table},
    {"extensive", NULL, LEVEL_EXTENSIVE, NULL, jnx_flow_mgmt_show_flow_table},
    { NULL, NULL, 0, NULL, NULL}
};

/*
 * show jnx-flow menu
 */
const parse_menu_t jnx_flow_show_menu[]  = {
    {"flow-table", NULL, 0, jnx_flow_show_flow_table_menu, NULL},
    { NULL, NULL, 0, NULL, NULL}
};

/*
 * show version menu
 */
static const parse_menu_t jnx_flow_show_version_menu[] = {
    { "brief", NULL, LEVEL_BRIEF, NULL, jnx_flow_mgmt_show_version},
    { "detail", NULL, LEVEL_DETAIL, NULL, jnx_flow_mgmt_show_version},
    { "extensive", NULL, LEVEL_EXTENSIVE, NULL, jnx_flow_mgmt_show_version},
    { NULL, NULL, 0, NULL, NULL }
};

/*
 * show menu
 */
const parse_menu_t show_menu[]  = {
    {"jnx-flow", NULL, 0, jnx_flow_show_menu, NULL},
    {"version", NULL, 0, jnx_flow_show_version_menu, NULL},
    { NULL, NULL, 0, NULL, NULL}
};

/*
 * master hierarchy menu
 */
const parse_menu_t master_menu[]  = {
    {"show", NULL, 0, show_menu, NULL},
    { NULL, NULL, 0, NULL, NULL}
};

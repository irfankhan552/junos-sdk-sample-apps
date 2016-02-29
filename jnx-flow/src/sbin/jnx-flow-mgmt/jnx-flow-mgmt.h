/*
 * $Id: jnx-flow-mgmt.h 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * jnx-flow-mgmt.h - mgmt control block, data & control structures
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
 * @file : jnx-flow-mgmt.h
 * @brief
 * This file contains the jnx-flow-mgmt top level data strutcures
 * and configuration macros
 */
#ifndef __JNX_FLOW_MGMT_H__
#define __JNX_FLOW_MGMT_H__

#include <isc/eventlib.h>
#include <jnx/pconn.h>
#include <ddl/dax.h>
#include <syslog.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>
#include JNX_FLOW_MGMT_OUT_H

#include <jnx/jnx-flow.h>
#include <jnx/jnx-flow_msg.h>

/* global definitions */
#define JNX_FLOW_MGMT_STATUS_INIT  1

#define JNX_FLOW_MAX_PKT_BUF_SIZE  4096
#define JNX_FLOW_KCOM_TIMER_PERIOD 200

#define KCOM_ID_JNX_FLOW_MGMT      100

#define MGMT_PARSE_TOKEN_SEP       "\t "

#define DNAME_JNX_FLOWD            "jnx-flow-mgmt"

#define MSP_PREFIX                 "ms"

#define JNX_FLOW_MGMT_LOG          0
#define JNX_FLOW_MGMT_TRACE        1

/* management app main control block structure definition */

typedef struct jnx_flow_mgmt_s {

    uint32_t          status;          /**< mgmt app status */
    /* configuration databases */
    patroot           data_sesn_db;    /**< data sessions list */
    patroot           rule_db;         /**< Rules config database */
    patroot           svc_set_db;      /**< Service Set Database */
    patroot           ssrb_db;         /**< SSRB Database */
    evContext         ctxt;
    /* control and data application server connections */
    pconn_server_t   *data_server;     /**< data app server socket */
    /* counters */
    uint32_t          rule_count;      /**< user policy count */
    uint32_t          sset_count;      /**< control policy count */
    /* packet buffer */
    uint8_t           send_buf[JNX_FLOW_MAX_PKT_BUF_SIZE];
} jnx_flow_mgmt_t;


typedef struct jnx_flow_mgmt_data_session_s {
    patnode         pic_node;      /**< add to control block */
    char            pic_name[JNX_FLOW_STR_SIZE]; /**< pic ifd name */
    uint32_t        ifd_idx;       /**< pic ifd index */
    uint32_t        fpc_id;        /**< pic fpc index */
    uint32_t        pic_id;        /**< pic index */

    /* pconn specific connection pointers */
    uint32_t         pic_status;   /**< pic status */
    pconn_session_t *psession;     /**< pconn server session handle */
    pconn_client_t  *cmd_conn;     /**< Config & Op cmd connection handle */

} jnx_flow_mgmt_data_session_t;

/* management control block */
extern jnx_flow_mgmt_t jnx_flow_mgmt;

extern status_t  jnx_flow_mgmt_conn_init(evContext ctxt);

extern jnx_flow_mgmt_data_session_t* 
jnx_flow_mgmt_data_sesn_lookup (char * pic_name);

extern status_t
jnx_flow_mgmt_conn_cleanup(evContext ctxt __unused);

extern status_t
jnx_flow_mgmt_send_config_msg(jnx_flow_mgmt_data_session_t * pdata_pic, 
                              void * msg_hdr, uint32_t msg_type,
                              uint32_t msg_len);

jnx_flow_msg_header_info_t * 
jnx_flow_mgmt_send_opcmd_msg(jnx_flow_mgmt_data_session_t * pdata_pic, 
                             jnx_flow_msg_header_info_t *  msg_hdr, 
                             uint32_t msg_type,
                             uint32_t msg_len);
jnx_flow_msg_header_info_t * 
jnx_flow_mgmt_recv_opcmd_msg(jnx_flow_mgmt_data_session_t * pdata_pic);

int jnx_flow_mgmt_shutdown(evContext ctxt);
status_t
jnx_flow_mgmt_destroy_data_conn(jnx_flow_mgmt_data_session_t * pdata_pic);

#define jnx_flow_trace(_prio, fmt...)\
    ({ junos_trace(_prio, fmt); })

#define jnx_flow_log(event, _prio, fmt...)\
    ({ ERRMSG(event, _prio, fmt); })

#endif /*__JNX_FLOW_MGMT_H__ */

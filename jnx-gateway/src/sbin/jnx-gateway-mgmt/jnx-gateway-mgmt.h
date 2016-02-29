/*
 * $Id: jnx-gateway-mgmt.h 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-mgmt.h - mgmt control block, data & control structures
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
#ifndef __JNX_GW_MGMT_H__
#define __JNX_GW_MGMT_H__

/* global definitions */
#define JNX_GW_MAX_PKT_BUF_SIZE    4096
#define JNX_GW_KCOM_TIMER_PERIOD   200
#define JNX_GW_MGMT_MAX_CTRL_IDX   PCONN_MAX_CONN
#define JNX_GW_STR_SIZE            256
#define JNX_GW_MGMT_MAX_DATA_IDX   PCONN_MAX_CONN
#define JNX_GW_MAX_MSG_COUNT       250
#define MGMT_PARSE_TOKEN_SEP       "\t "
#define DNAME_JNX_GATEWAYD         "jnx-gateway-mgmt"
#define JNX_GW_MGMT_HELLO_TIMEOUT  10  /* in secs */
#define JNX_GW_MGMT_PERIODIC_SEC   10  /* in secs */


/* pic status */
#define JNX_GW_MGMT_STATUS_DOWN    0
#define JNX_GW_MGMT_STATUS_INIT    1
#define JNX_GW_MGMT_STATUS_UP      2
#define JNX_GW_MGMT_STATUS_DELETE  3
#define JNX_GW_CONFIG_READ_FORCE   1

#ifndef MSP_PREFIX
#define MSP_PREFIX       "ms"
#define SERVICES_PREFIX  "sp"
#define LO_PREFIX        "lo"
#endif

/* management app main control block structure definition */

typedef struct jnx_gw_mgmt {

    uint32_t              status;          /**< mgmt app status */

    /* configuration databases */
    patroot               ctrl_sesn_db;    /**< control sessions list */
    patroot               data_sesn_db;    /**< data sessions list */
    patroot               intf_db;         /**< interfaces list */
    patroot               user_prof_db;    /**< user profile config list */
    patroot               ctrl_pol_db;     /**< control policy config list */
    patroot               data_pol_db;     /**< data  policy config list */

    evContext             ctxt;             /**< Eventlib context */
    evTimerID             periodic_timer_id;/**< periodic timer handler */
    evTimerID             rtb_scan_timer_id;/**< rtb scan timer handler */
    evTimerID             ifl_scan_timer_id;/**< ifl scan timer handler */

    /* control and data application server connections */
    pconn_server_t       *ctrl_server;     /**< ctrl app server socket */
    pconn_server_t       *data_server;     /**< data app server socket */

    /* counters */
    uint32_t              user_count;      /**< user policy count */
    uint32_t              ctrl_count;      /**< control policy count */
    uint32_t              data_count;      /**< data policy count */
    uint32_t              ctrl_sesn_count; /**< ctrl session count */
    uint32_t              data_sesn_count; /**< data session count */
    uint32_t              intf_count;      /**< interface count */

    /* packet buffer */
    uint8_t               send_buf[JNX_GW_MAX_PKT_BUF_SIZE];
} jnx_gw_mgmt_t;

/* control app pic structure definition */
typedef struct jnx_gw_mgmt_ctrl_session_s {
    patnode          pic_node;         /**< add to control block */
    char             pic_name[JNX_GW_STR_SIZE]; /* pic ifd name */
    uint32_t         peer_type;        /**< peer type */
    uint32_t         ifd_id;           /**< ifd index */
    uint32_t         fpc_id;           /**< fpc index */
    uint32_t         pic_id;           /**< pic index */

    /* pconn specific connection pointers */
    pconn_session_t  *psession;        /**< pconn server session handle */
    pconn_client_t   *opcmd_conn;      /**< operational command */

    /* set of attached data pic agents */
    uint32_t         pic_status;       /**< pic status */
    patroot          data_pics;        /**< list of data pics attached */
    uint32_t         data_pic_count;   /**< attached data pic count */

    /* statistics */
    /* vrf specicific periodic data */
    patroot          vrf_sesn_info;    /**< periodic vrf stats information */
    int              num_sesions;      /**< Number of GRE sessions */
    uint32_t         active_sessions;  /**< Number of active sessions */
    uint32_t         total_sessions;   /**< Total number of sessions */
    uint32_t         ipip_tunnel_count;/**< Total number of ipip tunnels */
    time_t           tstamp;           /**< last periodic message ts */
} jnx_gw_mgmt_ctrl_session_t;

typedef struct jnx_gw_mgmt_data_session_s {
    patnode         pic_node;      /**< add to control block */
    patnode         ctrl_node;     /**< add to control pic agent */
    char            pic_name[JNX_GW_STR_SIZE]; /**< pic ifd name */
    uint32_t        peer_type;     /**< peer type */
    uint32_t        ifd_id;        /**< pic ifd index */
    uint32_t        fpc_id;        /**< pic fpc index */
    uint32_t        pic_id;        /**< pic index */

    /* pconn specific connection pointers */
    uint32_t         pic_status;   /**< pic status */
    pconn_session_t *psession;     /**< pconn server session handle */
    pconn_client_t  *opcmd_conn;   /**< operational cmd connection handle */

    /* attached to this control agent */
    jnx_gw_mgmt_ctrl_session_t *pctrl_pic;  /**< control pic association */

    /* statistics */
    int32_t         num_sesions;    /**< GRE sessions */
    int32_t         active_sessions;/**< active GRE sessions */
    int32_t         num_ip_tunnels; /**< IP-IP tunnels */
    time_t          tstamp;         /**< last periodic message ts */

    /* data traffic */
    uint32_t        rx_bytes;       /**< rx bytes */
    uint32_t        tx_bytes;       /**< tx bytes */
    uint32_t        rx_pkts;        /**< rx packets */
    uint32_t        tx_pkts;        /**< tx packets */

} jnx_gw_mgmt_data_session_t;

/* interface structure */
typedef struct jnx_gw_mgmt_intf_s {
    patnode                      intf_node;      /**< add to control block */
    uint32_t                     intf_index;     /**< ifl index */
    char                         intf_name[JNX_GW_STR_SIZE]; /**< name */
    uint32_t                     intf_status;    /**< interface status */
    uint32_t                     intf_vrf_id;    /**< interface vrf mapping */
    uint32_t                     intf_subunit;   /**< interface subunit idx */
    jnx_gw_mgmt_data_session_t * pdata_pic;      /**< data pic association */
} jnx_gw_mgmt_intf_t;

/* management control block */
extern jnx_gw_mgmt_t jnx_gw_mgmt;

void jnx_gw_mgmt_ctrl_send_vrf_msg(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                                   int32_t vrf_id, uint8_t add_flag,
                                   uint8_t sig_flag, uint8_t flags);
int jnx_gw_mgmt_get_ifl (kcom_ifl_t * iflm, void * arg);
int jnx_gw_mgmt_get_rtb (kcom_rtb_t * rtb, void * arg);

#define JNX_GW_IFD_IDX(ifdm)     ((ifdm)->ifdev_index)

#define JNX_GW_IFL_IFD_IDX(iflm) ((iflm)->ifl_devindex)
#define JNX_GW_IFL_IFL_IDX(if_idx, iflm) \
    (if_idx = *(typeof(&if_idx))(&(iflm)->ifl_index))

#endif /*__JNX_GW_MGMT_H__ */

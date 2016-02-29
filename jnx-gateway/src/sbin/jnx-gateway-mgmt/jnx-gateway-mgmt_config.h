/*
 * $Id: jnx-gateway-mgmt_config.h 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-mgmt_config.h - config data structures.
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

#ifndef __JNX_GW_MGMT_CONFIG_H__
#define __JNX_GW_MGMT_CONFIG_H__

/* user profile configuration structure definition */
typedef struct jnx_gw_mgmt_user_s {
    patnode   user_node;                  /* add to the mgmt cb userlist */
    char      user_name[JNX_GW_STR_SIZE]; /* user name */
    uint32_t  user_addr;                  /* user address */
    uint32_t  user_mask;                  /* user mask */
    uint32_t  user_evrf_id;               /* egress vrf id */
    uint32_t  user_ipip_gw_ip;            /* ipip gateway id */
    uint32_t  user_sesn_count;            /* total session count */
    uint32_t  user_active_sesn_count;     /* active session count */
    char      user_evrf_name[JNX_GW_STR_SIZE];
    char      user_ipip_gw_name[JNX_GW_STR_SIZE];
} jnx_gw_mgmt_user_t;

/* control policy configuration structure definition */
typedef struct jnx_gw_mgmt_ctrl_s {
    patnode   ctrl_node;             /* add to the mgmt cb ctrl list */
    uint32_t  ctrl_vrf_id;           /* associated vrf id */
    uint32_t  ctrl_addr;             /* address */
    uint32_t  ctrl_mask;             /* mask */
    char      ctrl_vrf_name[JNX_GW_STR_SIZE];
} jnx_gw_mgmt_ctrl_t;

/* data policy static gre tunnel configuration structure definition */
typedef struct jnx_gw_mgmt_data_s {
    patnode   data_node;              /* add to the management db */
    uint32_t  data_pic_id;            /* ifd for the data pic */
    uint32_t  data_iself_ip;          /* self ip address */
    uint32_t  data_eself_ip;          /* self ip address */
    uint32_t  data_gre_gw_ip;         /* gre gateway id */
    uint32_t  data_ipip_gw_ip;        /* ip ip gateway id */
    uint32_t  data_clnt_ip;           /* client id */
    uint32_t  data_svr_ip;            /* server id */
    uint32_t  data_ivrf_id;           /* ingress vrf id */
    uint32_t  data_evrf_id;           /* egress vrf id */
    uint32_t  data_start_gre_key;     /* start gre key */
    uint16_t  data_start_port;        /* client start port */
    uint16_t  data_port_range;        /* client port range */
    uint16_t  data_svr_port;          /* server listen port */
    uint8_t   data_proto;
    uint8_t   data_flags;
    char      data_policy_name[JNX_GW_STR_SIZE];
    char      data_evrf_name[JNX_GW_STR_SIZE];
    char      data_ivrf_name[JNX_GW_STR_SIZE];
    char      data_clnt_name[JNX_GW_STR_SIZE];
    char      data_svr_name[JNX_GW_STR_SIZE];
    char      data_gre_gw_name[JNX_GW_STR_SIZE];
    char      data_ipip_gw_name[JNX_GW_STR_SIZE];
    char      data_iself_name[JNX_GW_STR_SIZE];
    char      data_eself_name[JNX_GW_STR_SIZE];
    char      data_pic_name[JNX_GW_STR_SIZE];
} jnx_gw_mgmt_data_t;


/* cross file function reference function definitions */

static inline int
jnx_gw_mgmt_get_pconn_pic_name (pconn_session_t * session, char *pic_name)
{
    pconn_peer_info_t peer_info;

    pic_name[0] = '\0';
    if (pconn_session_get_peer_info(session, &peer_info) == PCONN_OK) {
        if (peer_info.ppi_peer_type == PCONN_PEER_TYPE_RE) {
            strncpy(pic_name, PCONN_HOSTNAME_RE, sizeof(pic_name));
        } else {
            sprintf(pic_name, "ms-%d/%d/0", peer_info.ppi_fpc_slot,
                    peer_info.ppi_pic_slot);
        }
    }
    return EOK;
}

static inline int32_t
jnx_gw_ifl_get_name(uint32_t ifl_id, char * ifl_name, uint32_t len)
{
    kcom_ifl_t ifl;

    if (ifl_id == 0) {
        strncpy(ifl_name, PCONN_HOSTNAME_RE, len);
        return 0;
    }

    if (junos_kcom_ifl_get_by_index(*((ifl_idx_t *)&ifl_id), &ifl))
        return -1;
    sprintf(ifl_name, "%s.%d", ifl.ifl_name, ifl.ifl_subunit);
    return 0;
}

static inline int32_t
jnx_gw_ifd_get_id(char * ifd_name)
{
    kcom_ifdev_t ifd;
    /* if the peer is RE, return 0 */
    if (!strcmp(ifd_name, PCONN_HOSTNAME_RE)) {
        return 0;
    }
    if (junos_kcom_ifd_get_by_name(ifd_name, &ifd))
        return -1;
    return ifd.ifdev_index;
}

static inline uint32_t
jnx_gw_get_ipaddr(char * name)
{
    uint32_t addr = 0;
    struct hostent *host;

    if (valid_ipv4_hostaddr(name)) {
        addr = inet_addr(name);
    } else if (!strncmp(name, "all", strlen(name)))  {
        return addr;
    }
    else if ((host = gethostbyname(name))) {
        bcopy(host->h_addr, (uint8_t *)&addr, host->h_length);
    }
    return ntohl(addr);
}

#define jnx_gw_get_vrf_name(vrf_id, vrf_name, len)\
    vrf_getvrfnamebyindex(vrf_id, AF_INET, vrf_name, len)

#define jnx_gw_get_vrf_id(vrf_name)\
    vrf_getindexbyvrfname(vrf_name, NULL, AF_INET)

extern int32_t jnx_gw_mgmt_send_ctrl_msg(jnx_gw_mgmt_ctrl_session_t *pctrl_pic, 
                                         int type, void * msg, int16_t len);

extern jnx_gw_mgmt_ctrl_session_t *
jnx_gw_mgmt_ctrl_sesn_lookup (char * pic_name);

extern jnx_gw_mgmt_data_session_t *
jnx_gw_mgmt_data_sesn_lookup (char * pic_name);

extern jnx_gw_mgmt_ctrl_session_t *
jnx_gw_mgmt_get_next_ctrl_sesn(jnx_gw_mgmt_ctrl_session_t * pctrl);

extern jnx_gw_mgmt_data_session_t *
jnx_gw_mgmt_get_next_data_sesn(jnx_gw_mgmt_data_session_t * pctrl);

extern status_t jnx_gw_mgmt_config_init(void);

extern status_t jnx_gw_mgmt_conn_init(evContext ctxt);

extern status_t jnx_gw_mgmt_kcom_init(evContext ctxt);

extern char * jnx_gw_get_ifl_name(uint32_t if_idx);

extern status_t 
jnx_gw_mgmt_destroy_ctrl_conn(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                              uint32_t shutdown_flag);

extern status_t
jnx_gw_mgmt_destroy_data_conn(jnx_gw_mgmt_data_session_t * pdata_pic);

extern int jnx_gw_mgmt_config_read(int );

extern boolean
jnx_gw_mgmt_cleanup(evContext ctxt);

status_t 
jnx_gw_mgmt_data_send_opcmd(jnx_gw_mgmt_data_session_t * pdata_pic,
                            int32_t type, void * msg, int16_t len);

extern void * 
jnx_gw_mgmt_data_recv_opcmd(jnx_gw_mgmt_data_session_t * pdata_pic);

status_t 
jnx_gw_mgmt_ctrl_send_opcmd(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                            int32_t type, void * msg, int16_t len);
extern void *
jnx_gw_mgmt_ctrl_recv_opcmd(jnx_gw_mgmt_ctrl_session_t * pctrl_pic);

extern jnx_gw_mgmt_user_t *
jnx_gw_mgmt_lookup_user (const char * user_name);


extern jnx_gw_mgmt_user_t *
jnx_gw_mgmt_get_next_user(jnx_gw_mgmt_user_t * puser);

extern int
jnx_gw_mgmt_kcom_cleanup(evContext ctxt);

extern void
jnx_gw_mgmt_config_cleanup(void);

extern status_t
jnx_gw_mgmt_conn_cleanup(evContext ctxt);

extern void
jnx_gw_mgmt_update_user_stat(jnx_gw_mgmt_user_t *puser,
                             jnx_gw_msg_ctrl_user_stat_t *pstat);

extern int32_t
jnx_gw_mgmt_send_data_msg(jnx_gw_mgmt_data_session_t *pdata_pic,
                          int32_t type,
                          void * msg, int16_t len);

extern void
jnx_gw_mgmt_ctrl_event_handler(pconn_session_t * session,
                               pconn_event_t event,
                               void * cookie __unused);

extern status_t
jnx_gw_mgmt_ctrl_msg_handler (pconn_session_t * session,
                              ipc_msg_t * ipc_msg,
                              void * cookie __unused);
extern void
jnx_gw_mgmt_data_event_handler(pconn_session_t * session,
                               pconn_event_t event,
                               void * cookie __unused);

extern status_t
jnx_gw_mgmt_data_msg_handler(pconn_session_t * session,
                             ipc_msg_t * ipc_msg,
                             void * cookie __unused);

extern void
jnx_gw_mgmt_send_data_config(jnx_gw_mgmt_data_session_t * pdata_pic,
                             uint8_t add_flag);


extern void
jnx_gw_mgmt_send_ctrl_config(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                             uint32_t vrf_id, uint8_t add_flag);

extern void
jnx_gw_mgmt_send_ctrl_configs(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                             uint8_t add_flag);

extern void
jnx_gw_mgmt_send_user_config(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                             uint32_t vrf_id, uint8_t add_flag);

extern void
jnx_gw_mgmt_send_user_configs(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                             uint8_t add_flag);

extern jnx_gw_mgmt_intf_t * 
jnx_gw_mgmt_intf_add(char * intf_name, uint32_t intf_index,
                     uint32_t vrf_id, uint32_t subunit);

extern jnx_gw_mgmt_intf_t * 
jnx_gw_mgmt_intf_lookup(uint32_t intf_index);

extern void
jnx_gw_mgmt_intf_delete( jnx_gw_mgmt_intf_t * pintf);

void 
jnx_gw_mgmt_send_ctrl_intf_config(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                                  jnx_gw_mgmt_intf_t * pintf, 
                                  uint8_t add_flag);
void
jnx_gw_mgmt_ctrl_update_config(jnx_gw_mgmt_ctrl_session_t * pctrl_pic,
                             uint8_t add_flag);
#endif /* __JNX_GW_MGMT_CONFIG_H__ */

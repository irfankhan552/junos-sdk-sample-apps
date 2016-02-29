/*
 * $Id: jnx-gateway_msg.h 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway_msg.h - This file defines the structuresa of
 * the various messages which flow between JNX-GATEWAY-DATA, 
 * JNX-GATEWAY-CTRL, & JNX-GATEWAY-MGMT
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
 * @file jnx-gateway-common-msg.h
 * @brief It describes the various message structures used by
 * JNX-GATEWAY-CTRL, JNX-GATEWAY-DATA, & JNX-GATEWAY-MGMT
 *
 * The message structures are defined to have the following 
 * type of format
 *
 *------------------------------------------------------------
 *|                     Message Header                        |  
 *|___________________________________________________________|  
 *|                     Message Sub Header                    |
 *|___________________________________________________________|  
 *|                     Message Body                          |
 *|___________________________________________________________|  
 *
 */

#ifndef _JNX_GATEWAY_MSG_H_
#define _JNX_GATEWAY_MSG_H_

/**
 * These defines represent the various types
 * of Messages which will sent by the Control
 * App, Data App & Mgmt App.
 */
    
#define JNX_GW_GRE_SESSION_MSG    0    /**< Message used for GRE Session Mgmt*/
#define JNX_GW_IPIP_TUNNEL_MSG    1    /**< Message used for IPIP Tunnel Mgmt*/ 
#define JNX_GW_STAT_FETCH_MSG     2    /**< Message used to the fetch stats */
#define JNX_GW_STAT_CLEAR_MSG     3    /**< Message used to clear stats */
#define JNX_GW_STAT_PERIODIC_MSG  4    /**< Message sent peiodically from DATA & CTRL to MGMT */
#define JNX_GW_CONFIG_CTRL_MSG    5
#define JNX_GW_CONFIG_USER_MSG    6
#define JNX_GW_CONFIG_DATA_MSG    7
#define JNX_GW_CONFIG_PIC_MSG     8 
#define JNX_GW_CONFIG_INTF_MSG    9
#define JNX_GW_CONFIG_VRF_MSG     10

/**
 * These defines represent the various sub types
 * which will be present in various messages sent 
 * by the application
 */

/**
 * Subtypes for JNX_GW_GRE_SESSION_MSG 
 */
#define JNX_GW_ADD_GRE_SESSION    0    /**<SUB-TYPE to ADD a GRE SESSION */
#define JNX_GW_DEL_GRE_SESSION    1    /**<SUB-TYPE to DELETE a GRE SESSION */

/**
 * Subtypes for JNX_GW_IPIP_TUNNEL_MSG 
 */
#define JNX_GW_ADD_IP_IP_SESSION    0  /**<SUB-TYPE to add an IP-IP Tunnel */
#define JNX_GW_DEL_IP_IP_SESSION    1  /**<SUB-TYPE to delete an IP-IP Tunnel */

#define JNX_GW_STAT_PERIODIC_DATA_AGENT  0  /**< Message sent peiodically from DATA to MGMT */
#define JNX_GW_STAT_PERIODIC_CTRL_AGENT  1  /**< Message sent peiodically from CTRL to MGMT */

/* 
 * Subtypes for JNX_GW_STAT_FETCH_MSG
 */ 
#define JNX_GW_FETCH_SUMMARY_STAT        0  /**<SUB-TYPE to fetch summary stats for all VRFs */
#define JNX_GW_FETCH_SUMMARY_VRF_STAT    1  /**<SUB-TYPE to fetch summary stats for a particular VRF */
#define JNX_GW_FETCH_EXTENSIVE_STAT      2  /**SUB-TYPE to fetch extensive stats for all VRFs */
#define JNX_GW_FETCH_EXTENSIVE_VRF_STAT  3  /**<SUB-TYPE to fetch extensive stats for a particular VRF */
#define JNX_GW_FETCH_IPIP_STAT           4  /**<SUB-TYPE to fetch stats for a particular IP-IP tunnel */
#define JNX_GW_FETCH_GRE_STAT            5  /**<SUB_TYPE to fetch stats for a particular GRE Tunnel */

/* for control pic */
#define JNX_GW_FETCH_USER_STAT           6
#define JNX_GW_FETCH_GRE_SESN            7
#define JNX_GW_FETCH_GRE_SESN_SUM        8
#define JNX_GW_FETCH_GRE_SESN_VRF_SUM    9    
#define JNX_GW_FETCH_GRE_SESN_GW_SUM     10   
#define JNX_GW_FETCH_IPIP_SESN           11
#define JNX_GW_FETCH_IPIP_SESN_SUM       12
#define JNX_GW_FETCH_IPIP_SESN_VRF_SUM   13     
#define JNX_GW_FETCH_IPIP_SESN_GW_SUM    14      

#define JNX_GW_CTRL_VERBOSE_SET          0x10
#define JNX_GW_CTRL_VRF_SET              0x20
#define JNX_GW_CTRL_GW_SET               0x40
#define JNX_GW_CTRL_GRE_KEY_SET          0x80

/**
 * Subtypes for JNX_GW_STAT_CLEAR_MSG
 */
#define JNX_GW_CLEAR_SUMMARY_STAT        0
#define JNX_GW_CLEAR_SUMMARY_VRF_STAT    1
#define JNX_GW_CLEAR_EXTENSIVE_STAT      2
#define JNX_GW_CLEAR_EXTENSIVE_VRF_STAT  3
#define JNX_GW_CLEAR_IP_IP_STAT          4
#define JNX_GW_CLEAR_GRE_STAT            5

/**
 * Subtypes for JNX_GW_STAT_PERIODIC_MSG
 */
#define JNX_GW_PERIODIC_DATA_STAT        0  /**<Periodic message sent by JNX-GATEWAY-DATA */
#define JNX_GW_PERIODIC_CNTL_STAT        1  /**<Periodic message sent by JNX-GATEWAY-CTRL */

/** 
 * Sub types for the config message 
 */
#define JNX_GW_CONFIG_ADD                0
#define JNX_GW_CONFIG_DELETE             1
#define JNX_GW_CONFIG_MODIFY             2

/**
 * These defines represent the error code sent in response 
 * to the various messages sent across between Control App,
 * Mgmt App & Data App.
 */
#define JNX_GW_MSG_ERR_NO_ERR                   0
#define JNX_GW_MSG_ERR_SESSION_AUTH_FAIL        1
#define JNX_GW_MSG_ERR_RESOURCE_UNAVAIL         2
#define JNX_GW_MSG_ERR_SESSION_RETRY            3
#define JNX_GW_MSG_ERR_MEM_ALLOC_FAIL           4
#define JNX_GW_MSG_ERR_IPIP_SESS_NOT_EXIST      4
#define JNX_GW_MSG_ERR_GRE_SESS_NOT_EXIST       6
#define JNX_GW_MSG_ERR_IPIP_SESS_EXISTS         7
#define JNX_GW_MSG_ERR_GRE_SESS_EXISTS          8
#define JNX_GW_MSG_ERR_INVALID_TUNNEL_TYPE      9
#define JNX_GW_MSG_ERR_IP_IP_TUNNEL_IN_USE      10
#define JNX_GW_MSG_ERR_INVALID_ING_TUNNEL_TYPE  11
#define JNX_GW_MSG_ERR_INVALID_EG_TUNNEL_TYPE   12
#define JNX_GW_MSG_ERR_VRF_NOT_EXIST            13

/**
 * This structure represents the header of any message. It will
 * be present in all the message types.
 *-------------------------------------------------------------------
 *|  Msg-Type (1B) |    Count (1B)     |   Message Length (2B)       |  
 *|________________|___________________|_____________________________|  
 *|         Message ID  (2B)           | M| Reserved                 |
 *|____________________________________|__|__________________________|  
 */
typedef struct jnx_gw_msg_header_s{

    u_int8_t   msg_type;    /**<Type of Message */
    u_int8_t   count;       /**<Count of Sub Headers in the message */
    u_int16_t  msg_len;     /**<Length of the message */
    u_int16_t  msg_id;      /**<Sequence ID of the Message */
#if BYTE_ORDER == BIG_ENDIAN
    u_int8_t   more:1,      /**<More bit, indicating more messges for this ID */
               rsvd:7;      /**<Reserved flags */
#elif BYTE_ORDER == LITTLE_ENDIAN
    u_int8_t   rsvd:7,      /**<Reserved flags */
               more:1;      /**<More bit, indicating more messges for this ID */
#endif

    u_int8_t   rsvd1;       /**<Reserved for future use */ 
    
}jnx_gw_msg_header_t;

/* 
 * This structure represents the various sub headers which can be present in a 
 * message. For eg: a single messge can have request for adding multiple
 * sessions, modifying sessions, deleting the sessions
 *-------------------------------------------------------------------
 *|  Sub-Type (1B) |    Err-Code (1B)  |    Length (2B)              |  
 *|________________|___________________|_____________________________|  
 * 
 */
typedef struct jnx_gw_msg_sub_header_s{

    u_int8_t    sub_type;   /**<Sub-type of the message. Multiple subheaders 
                              can be there in message */
    u_int8_t    err_code;   /**<Error Code, will come in response */
    u_int16_t   length;     
}jnx_gw_msg_sub_header_t;

/**
 * These structures are for communicating the configuration 
 * information from the management agent to the control 
 * agent 
 */

/* user profile configuration message */
typedef struct jnx_gw_msg_ctrl_config_user_s {
	uint8_t    user_name[JNX_GW_STR_SIZE];
	uint32_t   user_addr;
	uint32_t   user_mask;
	uint32_t   user_evrf_id;
	uint32_t   user_ipip_gw_ip;
} jnx_gw_msg_ctrl_config_user_t;

/* control policy configuration message */
typedef struct jnx_gw_msg_ctrl_config_ctrl_s {
	uint32_t   ctrl_vrf_id;
	uint32_t   ctrl_addr;
	uint32_t   ctrl_mask;
} jnx_gw_msg_ctrl_config_ctrl_t;

/* data pic configuration message */
typedef struct jnx_gw_msg_ctrl_config_pic_s {
    char       pic_name[JNX_GW_STR_SIZE];
    uint32_t   pic_peer_type;
    uint32_t   pic_fpc_id;
    uint32_t   pic_pic_id;
    uint32_t   pic_ifd_id;
} jnx_gw_msg_ctrl_config_pic_t;

typedef struct jnx_gw_msg_ctrl_config_intf_t {
    char       intf_name[JNX_GW_STR_SIZE];
    uint32_t   intf_index;
    uint32_t   intf_vrf_id;
    uint32_t   intf_subunit;
} jnx_gw_msg_ctrl_config_intf_t;

#define JNX_GW_CTRL_SIGNALING_NOOP      0x00
#define JNX_GW_CTRL_SIGNALING_ENABLE    0x01
#define JNX_GW_CTRL_SIGNALING_DISABLE   0x02

typedef struct jnx_gw_msg_ctrl_config_vrf_t {
    char       vrf_name[JNX_GW_STR_SIZE];
    uint32_t   vrf_id;
    uint8_t    sig_flag; /* OK/NOP/DOWN */
    uint8_t    flags;    /* route set/clear flags */
    uint16_t   resv0; 
} jnx_gw_msg_ctrl_config_vrf_t;

typedef struct jnx_gw_msg_ctrl_config_data_s {
    uint32_t   data_iself_ip;        /* self ip address */
    uint32_t   data_eself_ip;        /* self ip address */
    uint32_t   data_gre_gw_ip;       /* gre gateway id */
    uint32_t   data_ipip_gw_ip;      /* ip ip gateway id */
    uint32_t   data_clnt_ip;         /* client id */
    uint32_t   data_svr_ip;          /* server id */
    uint32_t   data_ivrf_id;         /* ingress vrf id */
    uint32_t   data_evrf_id;         /* egress vrf id */
    uint32_t   data_start_gre_key;   /* start gre key */
    uint32_t   data_pic_idx;         /* data pic index */
    uint16_t   data_start_port;      /* client start port */
    uint16_t   data_port_range;      /* client port range */
    uint16_t   data_svr_port;        /* server listen port */
    uint8_t    data_proto;           /* protocol type */
    uint8_t    data_flags;           /* flags */
} jnx_gw_msg_ctrl_config_data_t;

/*
 * These message structures are for communication
 * for op command responses from the control 
 * pic to the management application on RE
 */

/* vrf status */
typedef struct jnx_gw_msg_ctrl_vrf_stat_s {
	uint32_t   vrf_id;
	uint32_t   vrf_active_sesn_count;
	uint32_t   vrf_sesn_count;
} jnx_gw_msg_ctrl_vrf_stat_t;

/* ipip tunnel summary status */
typedef struct jnx_gw_ctrl_ipip_sum_stat_s {
	uint32_t   ipip_vrf_id;
	uint32_t   ipip_gw_ip;
	uint32_t   ipip_data_ip;
	uint32_t   ipip_tun_count;
	uint32_t   ipip_active_sesn_count;
	uint32_t   ipip_sesn_count;
} jnx_gw_msg_ctrl_ipip_sum_stat_t;

/* gre tunnel session vrf/gateway summary status */
typedef struct jnx_gw_ctrl_gre_sum_stat_s {
	uint32_t   gre_vrf_id;
	uint32_t   gre_gw_ip;
	uint32_t   gre_gw_count;
	uint32_t   gre_active_sesn_count;
	uint32_t   gre_sesn_count;
} jnx_gw_msg_ctrl_gre_sum_stat_t;

/* gre tunnel session information */
typedef struct jnx_gw_msg_ctrl_gre_sesn_stat_s {
	uint32_t   gre_vrf_id;
	uint32_t   gre_key;
	uint32_t   gre_gw_ip;
	uint32_t   gre_data_ip;
	uint32_t   gre_if_id;
	uint32_t   sesn_client_ip;
	uint32_t   sesn_server_ip;
	uint16_t   sesn_dport;
	uint16_t   sesn_sport;
	uint16_t   sesn_resv;
	uint8_t    sesn_protocol;
	uint8_t    sesn_flags;
	uint32_t   sesn_up_time;
	uint32_t   ipip_vrf_id;
	uint32_t   ipip_data_ip;
	uint32_t   ipip_gw_ip;
	uint32_t   ipip_if_id;
	uint8_t    sesn_user_name[JNX_GW_STR_SIZE];
} jnx_gw_msg_ctrl_gre_sesn_stat_t;

/* ip ip gateway tunnel status */
typedef struct jnx_gw_msg_ctrl_ipip_sesn_stat_s {
	uint32_t   ipip_vrf_id;
	uint32_t   ipip_gw_ip;
	uint32_t   ipip_active_sesn_count;
	uint32_t   ipip_sesn_count;
} jnx_gw_msg_ctrl_ipip_sesn_stat_t;

/* data pic status message */
typedef struct jnx_gw_msg_ctrl_user_stat_s {
	char       user_name[JNX_GW_STR_SIZE];
	uint32_t   user_active_sesn_count;
	uint32_t   user_sesn_count;
} jnx_gw_msg_ctrl_user_stat_t;

/**
 *  The structures below represent the various structures used in 
 *  JNX_GW_GRE_SESSION_MSG i.e for add session, delete session
 *  & modify session.
 */

/**
 * This structure represents the information corresponding to the client 
 * session It needs to be present in the ADD_GRE_SESSION Message.
 */
typedef struct jnx_gw_msg_session_info_s{
    
    u_int16_t    session_id;/**<ID of the session */ 
    u_int8_t     proto;     /**<Protocol */ 
    u_int8_t     rsvd;      /**<Reserved for alignment */ 
    u_int32_t    sip;       /**<Source IP of the packets from Clients*/ 
    u_int32_t    dip;       /**<Destination IP of the packets from Client*/   
    u_int16_t    sport;     /**<Source Port */
    u_int16_t    dport;     /**<Destination Port */

}jnx_gw_msg_session_info_t;


#define JNX_GW_GRE_KEY_PRESENT      0x01
#define JNX_GW_GRE_SEQ_PRESENT      0x02
#define JNX_GW_GRE_CHECKSUM_PRESENT 0x04
/**
 * This structure represents the information regarding the tunnel which 
 * needs to be created for the session 
 */
typedef struct jnx_gw_msg_tunnel_type_s{
    
    u_int8_t    tunnel_type;  /**<Tunnel Type */
    u_int8_t    flags;        /**<Flags associated with the tunnel */
    u_int16_t   length;       /**<Length of the tunnel specific info */

}jnx_gw_msg_tunnel_type_t;

/**
 * This structure represents the information regarding the GRE Tunnel 
 */

typedef struct jnx_gw_msg_gre_info_s{

   u_int32_t    vrf;        /**<VRF associated with the Tunnel */ 
   u_int32_t    gateway_ip; /**<Gre Client Gateway IP Address */
   u_int32_t    self_ip;    /**<IP Address of the tunnel end-point */
   u_int32_t    gre_key;    /**<GRE Key associted with the tunnel */
   u_int32_t    gre_seq;    /**GRE Sequence Number associaated */
}jnx_gw_msg_gre_info_t;
 
/**
 * This structure represents the information regarding the IP-IP Tunnel 
 */
typedef struct jnx_gw_msg_ip_ip_info_s{

    u_int32_t    vrf;       /** VRF associated with the IP-IP Tunnel */
    u_int32_t    gateway_ip;/** IP Address of the IP-IP Gateway */
    u_int32_t    self_ip;   /** IP Address of the tunnel endpoint */
}jnx_gw_msg_ip_ip_info_t;


typedef struct jnx_gw_msg_ip_s{
    u_int32_t    vrf;       /**VRF */
}jnx_gw_msg_ip_t;

typedef struct jnx_gw_msg_gre_add_session_s{
    jnx_gw_msg_session_info_t session_info;
    jnx_gw_msg_tunnel_type_t  ing_tunnel;

    union {

       jnx_gw_msg_gre_info_t   gre_tunnel;
       jnx_gw_msg_ip_ip_info_t ip_ip_tunnel;
       jnx_gw_msg_ip_t         ip;
    }ing_tunnel_info;

    jnx_gw_msg_tunnel_type_t  eg_tunnel;

    union {
        jnx_gw_msg_gre_info_t   gre_tunnel;
        jnx_gw_msg_ip_ip_info_t ip_ip_tunnel;
        jnx_gw_msg_ip_t         ip;
    }eg_tunnel_info;

}jnx_gw_msg_gre_add_session_t;

typedef struct jnx_gw_msg_gre_del_session_s{

    jnx_gw_msg_gre_info_t   gre_tunnel;
}jnx_gw_msg_gre_del_session_t;

/**
 *             GRE ADD-SESSION Message Structure
 *
 *-------------------------------------------------------------------
 *|  Msg-Type (1B) |    Count (1B)     |   Message Length (2B)       |  
 *|________________|___________________|_____________________________|  
 *|         Message ID  (2B)           | M| Reserved                 |
 *|                                    |  |                          | 
 *-------------------------------------------------------------------|
 *|  Sub-Type (1B) |    Err-Code (1B)  |    Length (2B)              |  
 *|________________|___________________|_____________________________|  
 *|      Session Id (2B)               |  Proto (1B) |  Rsvd   (1B)  |    
 *|                                    |             |               | 
 *|------------------------------------------------------------------|
 *|                       Source IP  (4B)                            |
 *|__________________________________________________________________|                 
 *|                       Destination IP (4B)                        |
 *|                                                                  |
 *|------------------------------------------------------------------|
 *|        Source Port (2B)            |   Destination Port  (2B)    | 
 *|                                    |                             |
 *|------------------------------------------------------------------|
 *| Tunnel Type(1B)|    Flags (1B)     |     Length (2B)             |                             
 *|                |                   |                             | 
 *|------------------------------------------------------------------|
 *|                      Ingress  VRF (4B)                           |   
 *|                                                                  |                   
 *|------------------------------------------------------------------|
 *|                      Gateway IP  (4B)                            |            
 *|                                                                  |                  
 *|------------------------------------------------------------------|
 *|                      SELF IP (4B)                                |
 *|                                                                  |
 *|------------------------------------------------------------------|
 *|                      GRE KEY (4B)                                |
 *|                                                                  |
 *|------------------------------------------------------------------|
 *|                      GRE SEQ (4B)                                |
 *|                                                                  |                           
 *|------------------------------------------------------------------|
 *| Tunnel Type(1B)|    Flags (1B)     |     Length (2B)             |                             
 *|                |                   |                             | 
 *|------------------------------------------------------------------|
 *|                      Ingress  VRF (4B)                           |   
 *|                                                                  |                   
 *|------------------------------------------------------------------|
 *|                      Gateway IP  (4B)                            |            
 *|                                                                  |                  
 *|------------------------------------------------------------------|
 *|                      SELF IP (4B)                                |
 *|                                                                  |
 *|------------------------------------------------------------------|
 *
 * 
 *
 *              GRE DEL Session Message Structure
 * 
 *-------------------------------------------------------------------
 *|  Msg-Type (1B) |    Count (1B)     |   Message Length (2B)       |  
 *|________________|___________________|_____________________________|  
 *|         Message ID  (2B)           | M| Reserved                 |
 *|                                    |  |                          | 
 *|------------------------------------------------------------------|
 *|  Sub-Type (1B) |    Err-Code (1B)  |    Length (2B)              |  
 *|________________|___________________|_____________________________|  
 *|                      Ingress  VRF (4B)                           |   
 *|                                                                  |                   
 *|------------------------------------------------------------------|
 *|                      Gateway IP  (4B)                            |            
 *|                                                                  |                  
 *|------------------------------------------------------------------|
 *|                      SELF IP (4B)                                |
 *|                                                                  |
 *|------------------------------------------------------------------|
 *|                      GRE KEY (4B)                                |
 *|                                                                  |
 *|------------------------------------------------------------------|
 *|                      GRE SEQ (4B)                                |
 *|                                                                  |                           
 *|------------------------------------------------------------------|
 * 
 */

typedef struct jnx_gw_gre_msg_s{
    jnx_gw_msg_sub_header_t        sub_header;

    union {
        jnx_gw_msg_gre_add_session_t    add_session;
        jnx_gw_msg_gre_del_session_t    del_session;
        char                            data[1];
    }info;
}jnx_gw_msg_gre_t;

/*
 * The structures below define the message structures for
 * various sub types of JNX_GW_IPIP_TUNNEL_MSG
 */

typedef struct jnx_gw_ipip_add_tunnel_s{
    
    jnx_gw_msg_tunnel_type_t    tunnel_type;
    jnx_gw_msg_ip_ip_info_t     ipip_tunnel;

}jnx_gw_msg_ipip_add_tunnel_t;

typedef jnx_gw_msg_ipip_add_tunnel_t   jnx_gw_msg_ipip_del_tunnel_t;

typedef struct jnx_gw_ipip_msg_s{
    jnx_gw_msg_sub_header_t        sub_header;

    union {
        jnx_gw_msg_ipip_add_tunnel_t    add_tunnel;
        jnx_gw_msg_ipip_del_tunnel_t    del_tunnel;
    }info;
}jnx_gw_msg_ipip_t;

/*
 * The structures below define the message strucutres for
 * various sub types of JNX_GW_STAT_FETCH_MSG
 */ 

typedef struct jnx_gw_fetch_summary_vrf_s{

    u_int32_t       vrf;                   
}jnx_gw_msg_fetch_summary_vrf_t;

typedef struct jnx_gw_fetch_ext_vrf_s{

    u_int32_t       vrf;
}jnx_gw_msg_fetch_ext_vrf_t;
 

typedef struct jnx_gw_fetch_ipip_s{
    
    u_int32_t       vrf;
    u_int32_t       gateway_ip;
}jnx_gw_msg_fetch_ipip_t;

typedef struct jnx_gw_fetch_gre_s{
    
    u_int32_t       vrf;
    u_int32_t       gre_key;
}jnx_gw_msg_fetch_gre_t;

typedef struct jnx_gw_stat_msg_s{
    jnx_gw_msg_sub_header_t     sub_header;

    union {

        jnx_gw_msg_fetch_summary_vrf_t  summary_vrf_stat;
        jnx_gw_msg_fetch_ext_vrf_t      extensive_vrf_stat;
        jnx_gw_msg_fetch_ipip_t         ipip_stat;
        jnx_gw_msg_fetch_gre_t          gre_stat;
        char                            data[1];
    }info;
}jnx_gw_msg_stat_t;

typedef struct {

   jnx_gw_msg_sub_header_t     sub_header; /* JNX_GW_FETCH_SUMMARY_VRF_STAT */
   u_int32_t                   vrf;
   jnx_gw_vrf_stat_t           vrf_stats;
   jnx_gw_common_stat_t        summary_stats;
    
}jnx_gw_msg_summary_stat_rsp_t;

typedef struct jnx_gw_gre_stat_rsp_s{

    jnx_gw_msg_sub_header_t     sub_header; /* JNX_GW_FETCH_GRE_STAT */
    jnx_gw_gre_key_t            gre_key;
    jnx_gw_common_stat_t        stats;

}jnx_gw_msg_gre_stat_rsp_t;


typedef struct jnx_gw_ip_ip_stat_rsp_s{

    jnx_gw_msg_sub_header_t     sub_header; /* JNX_GW_FETCH_IPIP_STAT */
    jnx_gw_ipip_tunnel_key_t    ipip_key;
    jnx_gw_common_stat_t        stats;

}jnx_gw_msg_ipip_stat_rsp_t;


typedef struct jnx_gw_extensive_vrf_stat_rsp_s{

   jnx_gw_msg_sub_header_t         sub_header; /* JNX_GW_FETCH_EXTENSIVE_VRF_STAT */ 
   u_int32_t                       vrf;          
   jnx_gw_msg_summary_stat_rsp_t   vrf_stats;
   jnx_gw_msg_gre_stat_rsp_t       gre_stat;
   jnx_gw_msg_ipip_stat_rsp_t      ip_ip_stat;

}jnx_gw_msg_extensive_vrf_stat_rsp_t;

typedef struct jnx_gw_msg_stat_rsp_s{
    union {
        jnx_gw_msg_summary_stat_rsp_t         summary_stat;
        jnx_gw_msg_summary_stat_rsp_t         summary_vrf_stat;
        jnx_gw_msg_extensive_vrf_stat_rsp_t   extensive_stat;
        jnx_gw_msg_extensive_vrf_stat_rsp_t   extensive_vrf_stat;
        jnx_gw_msg_ipip_stat_rsp_t            ipip_stat;
        jnx_gw_msg_gre_stat_rsp_t             gre_stat;
        char                                  data[1];
    }info;
    
}jnx_gw_msg_stat_rsp_t;


/*
 * The structures below define the message strucutres for
 * various sub types of JNX_GW_STAT_CLEAR_MSG
 */ 
typedef struct jnx_gw_msg_clear_summary_vrf_s{

    u_int32_t       vrf;                   
}jnx_gw_msg_clear_summary_vrf_t;

typedef struct jnx_gw_msg_clear_ext_vrf_s{

    u_int32_t       vrf;
}jnx_gw_msg_clear_ext_vrf_t;
 

typedef struct jnx_gw_msg_clear_ipip_s{
    
    u_int32_t       vrf;
    u_int32_t       gateway_ip;
}jnx_gw_msg_clear_ipip_t;

typedef struct jnx_gw_msg_clear_gre_s{
    
    u_int32_t       vrf;
    u_int32_t       gre_key;
}jnx_gw_msg_clear_gre_t;

typedef struct jnx_gw_msg_clear_stat_msg_s{

    jnx_gw_msg_sub_header_t     sub_header;

    union {

        jnx_gw_msg_clear_summary_vrf_t  summary_vrf_stat;
        jnx_gw_msg_clear_ext_vrf_t      extensive_vrf_stat;
        jnx_gw_msg_clear_ipip_t         ip_ip_stat;
        jnx_gw_msg_clear_gre_t          gre_stat;
        char                            data[1];
    }info;
}jnx_gw_msg_clear_stat_t;

/**
 * This structure represents the Message structure used by 
 * the jnx_gw for communication between MGMT, CONTROL
 * & DATA App.
 *
 * This structure will be used to encode & decode the messages.
 */
typedef struct jnx_gw_msg_s{

    jnx_gw_msg_header_t    msg_header;        /**< Header of the Message> */

    union {

        jnx_gw_msg_gre_t        gre_msg;       /**<Msg info about GRE TUnnels    */
        jnx_gw_msg_ipip_t       ipip_msg;       /**<Msg info about IPIP Tunnels   */
        jnx_gw_msg_stat_t       stat_msg;       /**<Msg info about the stats      */
        jnx_gw_msg_clear_stat_t clear_stat_msg; /**<Msg info about clearing stats */
        jnx_gw_msg_stat_rsp_t   stat_rsp_msg;   /**<Msg info about the response to stat message */
    }msg_info;
}jnx_gw_msg_t;

#endif

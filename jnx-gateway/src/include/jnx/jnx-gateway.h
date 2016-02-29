/*
 *$Id: jnx-gateway.h 418048 2010-12-30 18:58:42Z builder $
 *
 *jnx-gateway.h - Definition of the various common structures
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
 * @file jnx-gateway-common.h
 * @brief This file describes the various common structures used across
 * JNX-GATEWAY-DATA, JNX_GATEWAY-CTRL & JNX_GATEWAY-MGMT
 */
#ifndef _JNX_GATEWAY_COMMON_H_
#define _JNX_GATEWAY_COMMON_H_

#include <sys/types.h>
#include <syslog.h>

/**
 * These are the defines for the various tunnel types 
 */ 
#define JNX_GW_TUNNEL_TYPE_GRE    1     /**< GRE Tunnel   */
#define JNX_GW_TUNNEL_TYPE_IPIP   2     /**< IP-IP Tunnel */
#define JNX_GW_TUNNEL_TYPE_IP     3     /**< Pure IP, No Tunnel*/

#define JNX_GW_MGMT_PORT         5000    /** Port reserved for use by MGMT */
#define JNX_GW_MGMT_CTRL_PORT    8001    /** Port reserved for use by MGMT */
#define JNX_GW_MGMT_DATA_PORT    8002    /** Port reserved for use by MGMT */
#define JNX_GW_CTRL_PORT         5010    /** Port reserved for use by CTRL */ 
#define JNX_GW_DATA_PORT         5020    /** Port reserved for use by DATA */
#define JNX_GW_PERIODIC_MSG_PORT 5030
#define JNX_GW_STR_SIZE          256
#define JNX_GW_INVALID_VRFID     (-1)

/**
 * This structure represents the key used for lookups in the GRE DB 
 */
typedef struct jnx_gw_gre_key_s {

    u_int32_t   vrf;        /**<VRF associated with the GRE Tunnel */
    u_int32_t   gre_key;    /**<GRE Key allocated to the tunnel    */
}jnx_gw_gre_key_t;

/**
 * This structure is used to compute the hash value for the key
 */
typedef union {
   jnx_gw_gre_key_t key;
   u_int32_t        val[2];
}jnx_gw_gre_key_hash_t;


/**
 * This structure represents the key used for lookups in the IPIP Tunnel  DB 
 */
typedef struct jnx_gw_ipip_tunnel_key_s{

    u_int32_t   gateway_ip;     /**<Gateway of the IP-IP Tunnel */
    u_int32_t   vrf;            /**<VRF Associated with the tunnel */
}jnx_gw_ipip_tunnel_key_t;

/**
 * This is used to compute the hash value of the IP-IP Tunnel
 */
typedef union {
    jnx_gw_ipip_tunnel_key_t    key;
    u_int32_t                   val[2];
}jnx_gw_ipip_tunnel_key_hash_t;

typedef u_int32_t   jnx_gw_vrf_key_t;

/**
 * This structure represents the various stats maintained per tunnel and 
 * also on vrf basis.
 */
typedef struct jnx_gw_common_stat_s{

    u_int32_t       packets_in;      /**< Packet In */
    u_int32_t       packets_out;     /**< Packets Out */
    u_int32_t       bytes_in;        /**< Bytes In */
    u_int32_t       bytes_out;       /**< Bytes Out */ 
    u_int32_t       checksum_fail;   /**< Checksum fail*/ 
    u_int32_t       ttl_drop;        /**< TTL Drop */ 
    u_int32_t       cong_drop;       /**< Congestion Drop */ 
    u_int32_t       inner_ip_invalid;/**<Invalid Inner IP Packet */
}jnx_gw_common_stat_t;

/**
 * This structure represents the periodic stats maintained per vrf basis.
 */
typedef struct jnx_gw_periodic_stat_s {
    u_int32_t       active_sessions;    /**< Number of active sessions */
    u_int32_t       total_sessions;     /**< Total number of sessions */
    u_int32_t       ipip_tunnel_count;  /**< Total number of ipip tunnels */
    u_int32_t       invalid_pkt;        /**< Packet has some error */
    u_int32_t       tunnel_not_present; /**< Packet for non-existing tunnels */
    u_int32_t       packets_in;         /**< Packet In */
    u_int32_t       packets_out;        /**< Packets Out */
    u_int32_t       bytes_in;           /**< Bytes In */
    u_int32_t       bytes_out;          /**< Bytes Out */ 
    u_int32_t       checksum_fail;      /**< Checksum fail*/ 
    u_int32_t       ttl_drop;           /**< TTL Drop */ 
    u_int32_t       cong_drop;          /**< Congestion Drop */ 
    u_int32_t       inner_ip_invalid;   /**< Invalid Inner IP Packet */
} jnx_gw_periodic_stat_t ;

/**
 * This structure represents the various stats maintained per vrf basis.
 */
typedef struct jnx_gw_vrf_stat_s{
    
    u_int32_t       tunnel_not_present; /**<Packet rcvd for a tunnel no configured */
    u_int32_t       invalid_pkt;        /**<Packet has some error */
    u_int32_t       active_sessions;    /**<NUmber of active sessions in this VRF */
    u_int32_t       total_sessions;     /**<Total number of sessions in this VRF */
}jnx_gw_vrf_stat_t;

/*
 * This enum defines the Ids of the various applications which will be supported by the 
 * sdk
 */
typedef enum {
    JNX_GW_MGMT_ID,    /**<Id for the Sample Management Application */
    JNX_GW_DATA_ID,    /**<Id for the Sample Data Application */
    JNX_GW_CTRL_ID,    /**<Id for the Sample Control Application */
    JNX_APP_MAX        /**<Max Number of applications which will be supported */

}jnx_gw_app_id_t;

#define JNX_GW_MALLOC(idx, size)    calloc(1, size) 
#define JNX_GW_FREE(idx, _x)        free(_x) 

#define JNX_GW_MAX_APP_AGENTS       32
#define JNX_GW_IP_ADDRA(addr)       inet_ntoa(*(struct in_addr *)&addr)

#define fldsiz(name, field)         (sizeof(((name *)0)->field))
#define fldoff(name, field)         ((int)&(((name *)0)->field))

#define JNX_GW_TRACE_FILE_COUNT     4
#define JNX_GW_TRACE_FILE_SIZE      1000000

#define TRACE_EVENT                 0x0100
#define TRACE_SIGNAL                0x0200
#define TRACE_ERR                   0x0300
#define TRACE_EMERG                 0x0300
#define TRACE_DEBUG                 0x0400
#define TRACE_MGMT                  0x0500

#define jnx_gw_log(_prio, fmt...)\
    ({ syslog(_prio, fmt); })

#define jnx_gw_mgmt_log(_event, _trace_flag, _prio, fmt...)\
    ({\
     junos_trace(_trace_flag, fmt);\
     if ((_prio == LOG_ERR) || (_prio == LOG_EMERG))\
     ERRMSG(_event, _prio, fmt);\
     })

#define jnx_gw_prefix_len(mask) \
    ({ int len = 32, idx = 0;\
     while (!(mask & (0x01 << idx)))\
     { idx++; len--; }\
     len; })

#endif

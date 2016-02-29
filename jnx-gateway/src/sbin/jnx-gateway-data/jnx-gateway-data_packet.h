/*
 *$Id: jnx-gateway-data_packet.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-gateway-data_packet.h - Header file for the Packet Processing
 *                             Threads
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

#ifndef JNX_GW_DATA_PACKET_H_
#define JNX_GW_DATA_PACKET_H_


/**
 * @file jnx-gateway-data-packet.h
 * @brief Header file fo packet processing threads
 *
 * This header file depicts the core structure used by the packet processing
 * threads, i.e. jnx_gw_pkt_proc_ctxt_t and the various stats updated by the 
 * data app.
 */

#include <jnx/mpsdk.h>
#include <netinet/tcp.h>
#include "jnx-gateway-data_db.h"

#define JNX_GW_GRE_HEADER_WITH_CHECKSUM             12 
#define JNX_GW_GRE_HEADER_WITHOUT_CHECKSUM          8 

#ifndef jbuf_getvrf
#define jbuf_getvrf(jb)             (jb->jb_rcv_vrf)
#define jbuf_setvrf(jb, vrf_id) \
    ({jb->jb_flags |= JBUF_FLAG_XMIT_VRF;\
     jb->jb_xmit_subunit = vrf_id;})
#endif

#define GRE_FIELD_WIDTH  4
#define GRE_CKSUM_OFFSET (sizeof(struct ip) + 2)

/**
 * This enum represents the various types of stats updates by the packet
 * processing thread. They represent both the success path and the error 
 * path stats. 
 */
typedef enum {

    JNX_GW_DATA_STAT_TYPE_NONE,                     /**<No stats should be upated */
    JNX_GW_DATA_ERR_INVALID_PKT,                    /**< Invalid packet recived, error in Outer IP */
    JNX_GW_DATA_PACKET_IN,                          /**< Packets IN */
    JNX_GW_DATA_PACKET_OUT,                         /**< Packets Out */
    JNX_GW_DATA_BYTE_IN,                            /**< Bytes In */    
    JNX_GW_DATA_BYTE_OUT,                           /**< Bytes Out */
    JNX_GW_DATA_CONG_DROP,                          /**< Drop due to congestion */
    JNX_GW_DATA_GRE_INNER_IP_TTL,                   /**< Gre Packet rcvd had TTL = 0/1*/
    JNX_GW_DATA_ERR_GRE_PKT_WITHOUT_KEY,            /**< Gre Packet rcvd without any GRE Key */ 
    JNX_GW_DATA_ERR_GRE_PKT_WITH_SEQ,               /**< Gre packet rcvd with sequence number bit set */
    JNX_GW_DATA_ERR_GRE_PKT_INVALID_PROTO,          /**< Gre packet rcvd with invalid protocol */
    JNX_GW_DATA_ERR_GRE_CHECKSUM,                   /**< Gre packet rcvd with invalid checksum */
    JNX_GW_DATA_ERR_GRE_TUNNEL_NOT_PRESENT,         /**< Gre tunnel is no present for the key configured */
    JNX_GW_DATA_ERR_GRE_TUNNEL_NOT_READY,           /**< Gre tunnel is not yet initialized */
    JNX_GW_DATA_ERR_IPIP_INNER_IP_TTL,              /**< IPIP packet rcvd had TTL for inner IP pkt */
    JNX_GW_DATA_ERR_IPIP_TUNNEL_INVALID_INNER_PKT,  /**< IPIP packet rcvd had invalid inner IP packet */
    JNX_GW_DATA_ERR_IPIP_TUNNEL_NOT_PRESENT,        /**< IPIP Tunnel is not configured */
    JNX_GW_DATA_ERR_IPIP_TUNNEL_NOT_READY,          /**< IPIP Tunnel is not yet initialized */
}jnx_gw_stat_type_t;

/**
 * This structure defines the packet processing context for each data thread.
 * Most of the variables required by Packet Processing thread are present in
 * this strucutre. This is used to ensure localization on data and better 
 * cache utilization
 */
typedef struct jnx_gw_pkt_proc_ctxt_s{


    struct jnx_gw_data_cb_s*                app_cb;             /**<Pointer to the Control Block */
    msp_data_handle_t                       dhandle;            /**<Data thread handler */
    struct jbuf                            *pkt_buf;            /**<Pointer to the packet received */
    uint32_t                                ing_vrf;            /**<Ingress VRF of the packet */
    uint32_t                                eg_vrf;             /**<Egress VRF of the packet */
    jnx_gw_data_gre_tunnel_t*               gre_tunnel;         /**<Pointer to the Gre Tunnel */
    jnx_gw_data_ipip_sub_tunnel_t*          ipip_sub_tunnel;    /**<Pointer to the IPIP sub tunnel */
    struct ip*                              ip_hdr;             /**<Pointer to the IP Header in the Packet */
    jnx_gw_data_vrf_stat_t*                 ing_vrf_entry;      /**<Pointer to the ingress vrf entry */
    jnx_gw_data_vrf_stat_t*                 eg_vrf_entry;       /**<Pointer to the egress vrf entry */
    jnx_gw_gre_key_hash_t                   gre_key_info;       /**<Gre Tunnel Key */
    jnx_gw_data_ipip_sub_tunnel_key_hash_t  ipip_sub_key_info;  /**<IP-IP Sub Tunnel Key */
    jnx_gw_gre_encap_header_t*              ip_gre_hdr;         /**<Pointer to the Outer IP&GRE Header */
    jnx_gw_ipip_encap_header_t*             ipip_hdr;           /**<Pointer to the Outer IP-IP header */
    jnx_gw_stat_type_t                      stat_type;          /**<Pointer to the stat type to be incremented */
}jnx_gw_pkt_proc_ctxt_t;


/* Function to process the data packets received by the JNX-GW-DATA */
extern void* jnx_gw_data_process_packet(void*    args);

#endif


/*
 *$Id: jnx-gateway-data_db.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 *jnx-gateway-data_db.h - Definition of the various tunnel DataBases,
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
 * @file jnx-gateway-data-db.h
 * @brief This file describes the Various DBs associated with the GRE & IPIP
 * Tunnels.
 * Organization of various tunnel DB.
 *
 * a. GRE_TUNNEL_DB represents the various GRE Tunnels and
 *    the stats for that tunnel. There is one entry for each
 *    GRE_KEY & VRF combination. It points to IP-IP TUNNEL DB and 
 *    the VRF DB, 
 *    When the packet comes, a lokup is done to findout the
 *    entry and then GRE Stats are updated directly in the
 *    entry and IP-IP stats are updated in the IP-IP TUNNEL DB
 *    and the VRF Stats are updated in the VRF_STAT DB. There
 *    are two VRF stats, ingress vrf stats & egress vrf stats.
 *
 * b. IP_IP_SUB_TUNNEL_DB is used to get the GRE KEY for a packet 
 *    arriving on an IP-IP tunnel. The key is IP-IP Gateway, 
 *    VRF, Destination Addr & Port. (This doesn't represent
 *    the actual IP-IP Tunnel, but is kind of sub-tunnel which
 *    maps to the outgoing GRE Tunnel). When the packet comes
 *    in, the lookup is done in this DB to get the entry. OUtgoing
 *    GRE KEy & VRF are retrieved. Entry points to the IP-IP TUNNEL
 *    DB, VRF_STAT DB and GRE_TUNNEL_DB. Using the pointers,
 *    incoming stats are updated in IP-IP stat, vrf stats are updated
 *    in the VRF STAT DB.
 *
 *
 * c. IP_IP_TUNNEL_DB represents the actual IP-IP Tunnel. It is an 
 *    aggragate of the IP-P sub Tunnels (present in IP_IP_SUB_TUNNEL_DB)
 *    going to the same VRF & the same gateway. It maintains the
 *    stats for the IP-IP Tunnels.
 *
 * d. VRF_STAT DB is used to maintain the aggregate stats of a particular
 *    VRF. Both the incoming and the outgoing stats are maintained for
 *    each VRF irrespective of the tunnel. This can be used later
 *    on to have the load balancing mechanisms.
 * 
 *
 *              --------------          --------------
 *  ----------->|            |          |            |          
 *  |           |   VRF      |          | IP-IP      |  
 *  |           |   STAT DB  |          | TUNNEL DB  |       
 *  |           |            |          |            |
 *  |           |____________|          |____________|   
 *  |               ^                      ^   ^
 *  |               |                      |   |
 *  |               |  --------------------|   |                
 *  |               |  |                       |        
 *  |               |  |                       |
 *  |   --------------------            ------------    
 *  |   |                   |           |           |
 *  |   |       GRE TUNNEL  |           |  IP-IP SUB|   
 *  |   |       DB          |<--------- |  TUNNEL DB|
 *  |   |                   |           |           |
 *  |   |                   |           |           |
 *  |   |                   |   ________|           |
 *  |   |___________________|   |       |___________|
 *  |                           |                       
 *  |___________________________|
 *
 *
 */

#ifndef _JNX_GATEWAY_DATA_DB_H_
#define _JNX_GATEWAY_DATA_DB_H_

#include <jnx/mpsdk.h>
#include <jnx/jnx-gateway.h>
#include <sys/types.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

typedef msp_spinlock_t jnx_gw_data_lock_t;
/**
 * This is the jnx_gw_gre_header_s strucutre. It is used by the packet
 * processing code to decode the GRE Packet Received and to encode the
 * packet to be sent to the GRE Tunnel.
 *
 * For bit-fields two defintions are provided, one for the little endian
 * case and the other for the big endian case.
 */ 
typedef struct jnx_gw_gre_header_s{

    union {
        struct {
#if BYTE_ORDER == BIG_ENDIAN
            uint16_t        checksum:1,     /**<Checksum bit in the GRE Header*/
                            rsvd1:1,        /**<Reserved bit */
                            key_present:1,  /**<Key Present bit */
                            seq_num:1,      /**<Sequence NUmber bit */
                            rsvd2:8,        /**<Reserved Bits */ 
                            version:4;      /**<Version Bits */
#else
            uint16_t        version:4,      /**<Version Bits */
                            rsvd2:8,        /**<Reserved Bits */
                            seq_num:1,      /**<Sequence NUmber bit */
                            key_present:1,  /**<Key Present bit */
                            rsvd1:1,        /**<Reserved bit */
                            checksum:1;     /**<Checksum bit in GRE Header */
#endif
        }info;
        uint16_t            flags;              /**< Flags in the GRE Header */
    }hdr_flags;

    uint16_t           protocol_type;      
    
    /*
     * Currently, we are supporting only two options in the GRE HEader,
     * i.e. GRE KEY & CHECKSUM, also, GRE KEY is mandatory but CHECKSUM is
     * optional
     */
     
    union {

        struct {
            uint16_t           checksum; 
            uint16_t           rsvd;
            uint32_t           gre_key;
        } checksum_key;

        struct {
            uint16_t           checksum; 
            uint16_t           rsvd;
            uint32_t           gre_key;
            uint32_t           gre_seq;
        } checksum_key_seq_key;

        struct {
            uint32_t           gre_key;
            uint32_t           gre_seq;
        } key_seq_key;

        uint32_t   gre_key;
        uint32_t   gre_seq;
    }info;

}jnx_gw_gre_header_t;


/**
 * This is the jnx_gw_gre_encap_header_s strucutre. It combines the IP Header
 * & the GRE Header together in one strucuture. It used by the packet
 * processing code to decode the GRE Packet Received and to encode the
 * packet to be sent to the GRE Tunnel
 */ 
typedef struct jnx_gw_gre_encap_header_s{

    struct ip               outer_ip_hdr;       /**< Outer IP Header */
    jnx_gw_gre_header_t     gre_header;         /**< Gre HEader structure */

} jnx_gw_gre_encap_header_t;

/**
 * This is the jnx_gw_ipip_encap_header_s  strucutre. It combines the outer IP 
 * Header,Inner IP Header, TCP header to form one structure. Although the inner
 * header may be UDP also, but we are still taking the TCP Header because the 
 * fields which we are interested are present at the same offset in TCP/UDP
 * Header.
 */ 
typedef struct jnx_gw_ipip_encap_header_s{

    struct ip      outer_ip_hdr;    /**< Outer IP Header */
    struct ip      inner_ip_hdr;    /**< Inner IP Header */
    struct tcphdr  tcp_hdr;         /**< TCP Header */ 
}jnx_gw_ipip_encap_header_t;

/**
 * This enum defines the states of the indiviual entry in the various DBs
 * i.e GRE-TUNNEL DB, IP-IP TUNNEL DB, IPIP-SUB-TUNNEL DB & VRF DB
 */
typedef enum {

    JNX_GW_DATA_ENTRY_STATE_INIT, /**< State INIT: Created but not fuilly populated */
    JNX_GW_DATA_ENTRY_STATE_READY,/**< State READY: Fully Populted Entry */
    JNX_GW_DATA_ENTRY_STATE_DEL   /**< State DELETED: Entry deleted from DB but still not freed*/
}jnx_gw_data_db_states_t;

/**
 * This structure defines the Key for IPIP-SUB-TUNNEL. It is used while
 * performing a lookup in the IPIP-SUB-TUNNEL DB.
 */
typedef struct jnx_gw_data_ipip_sub_tunnel_key_s{
    
    uint32_t   vrf;          /**<VRF of the IP_IP Tunnel */
    uint32_t   gateway_addr; /**<Gateway Address of the IP-IP Tunnel */ 
    uint32_t   client_addr;  /**<IP Address of the Client using the GRE Tunnel */  
    uint16_t   client_port;  /**<Port of the Client  using the GRE Tunnel */
    uint16_t   rsvd;         /**<rsvd for alignment reasons */
}jnx_gw_data_ipip_sub_tunnel_key_t;

#define JNX_GW_DATA_MAX_GRE_BUCKETS             1024
#define JNX_GW_DATA_MAX_IP_IP_BUCKETS           128
#define JNX_GW_DATA_MAX_VRF_BUCKETS             64
#define JNX_GW_DATA_HASH_MAGIC_NUMBER           0x5f5f
#define JNX_GW_DATA_GRE_TUNNEL_HASH_MASK        (JNX_GW_DATA_MAX_GRE_BUCKETS - 1)
#define JNX_GW_DATA_IPIP_TUNNEL_HASH_MASK       (JNX_GW_DATA_MAX_IP_IP_BUCKETS - 1)
#define JNX_GW_DATA_VRF_HASH_MASK               (JNX_GW_DATA_MAX_VRF_BUCKETS - 1)

/**
 * This structure defines the IPIP_TUNNEL structure
 */
typedef struct jnx_gw_data_ipip_tunnel_s{
    
    struct jnx_gw_data_ipip_tunnel_s*    next_in_bucket; /**<Pointer to the next entry in hash bucket*/
    struct jnx_gw_data_ipip_tunnel_s*    next_in_vrf;    /**<Pointer to the next tunnel in the VRF */    
    jnx_gw_ipip_tunnel_key_t             key;            /**< Key of the Tunnel */
    jnx_gw_data_db_states_t              tunnel_state;   /**<State of this tunnel */
    jnx_gw_data_lock_t                        lock;           /**<Lock on the Tunnel */ 
    struct jnx_gw_data_vrf_stat_s*       ing_vrf;        /**<Pointer to the ingress vrf */   
    uint32_t                             use_count;      /**<Count of GRE sessions through this tunnel*/
    uint32_t                             self_ip;        /**<IP address to be used for this tunnel */
    jnx_gw_common_stat_t                 stats;          /**<Statistics for the tunnel */ 
    time_t                               time_stamp;     /**<Time stamp when the entry got deleted */ 
}jnx_gw_data_ipip_tunnel_t;

/**
 * Definition of the GRE  Tunnel Structure 
 */
typedef struct jnx_gw_data_gre_tunnel_s{

    struct jnx_gw_data_gre_tunnel_s*          next_in_bucket; /**<Next Entry in the Hash Bucket */
    jnx_gw_gre_key_t                          key;            /**<Key for the entry */
    uint32_t                                  egress_vrf;     /**<Egress VRF for the packet */
    uint32_t                                  gre_seq;        /**<Current GRE Sequence number */
    jnx_gw_data_db_states_t                   tunnel_state;   /**<State of the tunnel */   
    jnx_gw_data_ipip_tunnel_t*                ipip_tunnel;    /**<Pointer to the egress tunnel stats */
    struct jnx_gw_data_vrf_stat_s*            ing_vrf_stat;   /**<Pointer to the ingress vrf stat  */   
    struct jnx_gw_data_vrf_stat_s*            eg_vrf_stat;    /**<Pointer to the egress vrf stat   */
    jnx_gw_common_stat_t                      stats;          /**<Stats for the tunnel */ 
    struct ip                                 ip_hdr;         /**<IP Header which needs to be put on the outgoing packet */
    uint8_t                                   tunnel_type;    /**<Tunnel type of the egress packet */
    union {
        struct {

            uint32_t       gateway_addr;  /**<IP Address of the IP-IP Gateway */
            uint32_t       self_ip_addr;  /**<Local Address on the tunnel */
        }ip_ip;
    }egress_info;
    struct jnx_gw_data_gre_tunnel_s*          next_in_vrf;    /**<Pointer to the next GRE Tunnel in same VRF */ 
    jnx_gw_data_lock_t                        lock;           /**<Lock for the Gre Tunnel */
    uint32_t                                  self_ip_addr;   /**<Local endpoint address of tunnel */
    time_t                                    time_stamp;     /**<Time stamp when the entry got deleted */
    struct jnx_gw_data_ipip_sub_tunnel_s*     ipip_sub_tunnel;
    
}jnx_gw_data_gre_tunnel_t;


/**
 * Definition of the VRF STAT Structure 
 */
typedef struct jnx_gw_data_vrf_stat_s{

    struct jnx_gw_data_vrf_stat_s*   next_in_bucket;    /**<Pointer to the next entry in the hash bucket*/
    jnx_gw_data_gre_tunnel_t*        next_gre_tunnel;   /**<Pointer to next GRE Tunnel    */
    jnx_gw_data_ipip_tunnel_t*       next_ipip_tunnel;  /**<Pointer to next IP IP Tunnel  */
    uint32_t                         key;               /**<VRF value is the key */
    jnx_gw_data_db_states_t          state;             /**<State of the VRF entry */
    jnx_gw_common_stat_t             stats;             /**<Stats for the tunnel */ 
    jnx_gw_data_lock_t               lock;              /**<Lock for the Gre Tunnel */
    jnx_gw_vrf_stat_t                vrf_stats;         /**<VRF specific stats */

}jnx_gw_data_vrf_stat_t;


/**
 * Definition of the IP-IP Sub Tunnel Structure 
 */
typedef struct jnx_gw_data_ipip_sub_tunnel_s{
    
    struct jnx_gw_data_ipip_sub_tunnel_s*    next_in_bucket; /**<Pointer to the next entry in the hash bucket*/
    jnx_gw_data_ipip_sub_tunnel_key_t        key;            /**<Key for the IPIP SUB Tunnel*/
    jnx_gw_data_lock_t                       lock;           /**<Lock for the IPIP SUB Tunnel */
    struct jnx_gw_gre_encap_header_s         ip_gre_hdr;     /**<Pointer to the Pre Computed Header IP & GRE header */
    uint32_t                                 ip_gre_hdr_len; /**<IP GRE header length */
    uint32_t                                 ip_gre_hdr_cksum_offset; /**<GRE checksum offset */
    uint32_t                                 ip_gre_hdr_key_offset; /**<GRE key offset */
    uint32_t                                 ip_gre_hdr_seq_offset; /**<GRE seq offset */
    uint32_t                                 egress_vrf;     /**<Egress VRF for the tunnel */   
    uint32_t                                 gre_seq;        /**< Gre sequence number */
    jnx_gw_data_db_states_t                  tunnel_state;   /**<State of the tunnel */   
    jnx_gw_data_gre_tunnel_t*                gre_tunnel;     /**<Pointer to the GRE Tunnel bounded with this Tunnel */
    jnx_gw_data_ipip_tunnel_t*               ipip_tunnel;    /**<Pointer to the main IPIP-TUNNEL associated with it */
    jnx_gw_data_vrf_stat_t*                  ing_vrf_stat;   /**<Pointer to the ingress vrf entry */
    jnx_gw_data_vrf_stat_t*                  eg_vrf_stat;    /**<Pointer to the egress vrf entry */
    time_t                                   time_stamp;     /**<Time stamp when the entry got deletd */
    
}jnx_gw_data_ipip_sub_tunnel_t;

/**
 * This union is used to compute the hash value of the GRE KEY
 */
typedef union {
   jnx_gw_gre_key_t         key;
   uint32_t                 val[2];
}jnx_gw_data_gre_key_hash_t;

/**
 * This union is used to compute the hash value of the IPIP-SUB TUNNEL KEY
 */
typedef union {
   jnx_gw_data_ipip_sub_tunnel_key_t         key;
   uint32_t                                  val[4];
}jnx_gw_data_ipip_sub_tunnel_key_hash_t;

/**
 * This union is used to compute the hash value of the IPIP-TUNNEL KEY
 */
typedef union {
   jnx_gw_ipip_tunnel_key_t      key;
   uint32_t                      val[2];
}jnx_gw_data_ipip_tunnel_key_hash_t;


/**
 * This structure represents the GRE DB Hash Bucket
 */
typedef struct jnx_gw_data_gre_hash_bucket_s{
    
    jnx_gw_data_lock_t          bucket_lock;    /**<Lock for the complete bucket */
    jnx_gw_data_gre_tunnel_t*   chain;          /**<Pointer to the first entry in the chain */
    int                         count;          /**<Number of entries in the chain */
}jnx_gw_data_gre_hash_bucket_t;

/**
 * This structure represents the GRE DB Hash Bucket
 */
typedef struct jnx_gw_data_gre_tunnel_db_s{

    jnx_gw_data_gre_hash_bucket_t    hash_bucket[JNX_GW_DATA_MAX_GRE_BUCKETS]; /**<Buckets in the hash table */
    
}jnx_gw_data_gre_tunnel_db_t;

/**
 * This structure represents the IPIP SUB TUNNEL DB Hash Bucket
 */
typedef struct jnx_gw_data_ipip_sub_tunnel_hash_bucket_s{
    
    jnx_gw_data_lock_t               bucket_lock;    /**<Lock for the complete bucket */
    jnx_gw_data_ipip_sub_tunnel_t*   chain;          /**<Pointer to the first entry in the chain */
    int                              count;          /**<Number of entries in the chain */
}jnx_gw_data_ipip_sub_tunnel_hash_bucket_t;

/**
 * This structure represents the IPIP SUB TUNNEL DB 
 */
typedef struct jnx_gw_data_ipip_sub_tunnel_db_s{

    jnx_gw_data_ipip_sub_tunnel_hash_bucket_t  hash_bucket[JNX_GW_DATA_MAX_GRE_BUCKETS]; /**<Buckets in the hash table */
    
}jnx_gw_data_ipip_sub_tunnel_db_t;


/**
 * This structure represents the IPIP TUNNEL DB HASH Bucket 
 */
typedef struct jnx_gw_data_ipip_tunnel_hash_bucket_s{
    
    jnx_gw_data_lock_t               bucket_lock;    /**<Lock for the complete bucket */
    jnx_gw_data_ipip_tunnel_t*       chain;          /**<Pointer to the first entry in the chain */
    int                              count;          /**<Number of entries in the chain */
}jnx_gw_data_ipip_tunnel_hash_bucket_t;


/**
 * This structure represents the IPIP TUNNEL DB 
 */
typedef struct jnx_gw_data_ipip_tunnel_db_s{

    jnx_gw_data_ipip_tunnel_hash_bucket_t   hash_bucket[JNX_GW_DATA_MAX_IP_IP_BUCKETS];

}jnx_gw_data_ipip_tunnel_db_t;


/**
 * This structure represents the VRF STAT HASH DB HASH Bucket 
 */
typedef struct {

    jnx_gw_data_lock_t               bucket_lock;    /**<Lock for the complete bucket */
    jnx_gw_data_vrf_stat_t*          chain;          /**<Pointer to the first entry in the chain */
    int                              count;          /**<Number of entries in the chain */
    
}jnx_gw_data_vrf_stat_hash_bucket_t;

/**
 * This structure represents the VRF STAT HASH DB 
 */
typedef struct {

    jnx_gw_data_vrf_stat_hash_bucket_t  hash_bucket[JNX_GW_DATA_MAX_VRF_BUCKETS]; /**<Buckets in the hash table */

}jnx_gw_data_vrf_db_t;

/**
 * This structure represents the Deleted Tunnels List. Tunnels are added in this
 * list once they are deleted by the configuration commands. Later on after
 * cleanup timer expiry the tunnels will be cleaned up 
 */
typedef struct {

    jnx_gw_data_gre_tunnel_t*       gre_tunnel;     /**<List of deleted gre tunnels      */
    jnx_gw_data_ipip_tunnel_t*      ipip_tunnel;    /**<List of deleted IPIP Tunnels     */
    jnx_gw_data_ipip_sub_tunnel_t*  ipip_sub_tunnel;/**<List of deleted IPIP-SUB Tunnels */

}jnx_gw_data_del_tunnels_list_t;

#endif

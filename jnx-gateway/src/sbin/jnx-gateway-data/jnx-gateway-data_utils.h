/*
 * $Id: jnx-gateway-data_utils.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-gateway-data_utils.h - Prototypes of the Common routines used by 
 *                            the control thread & data threads of 
 *                            JNX-GATEWAY_DATA application.
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
 * @file jnx-gateway-data-utils.h
 * @brief Prototypes of the various utility functions used by JNX-GATEWAY-DATA
 *
 * This header file contains the prototypes of the various utility functions,
 * used by the jnx-gateway-data.These functions are primarily used to search the
 * various entries in their respectives DB.
 * 
 */

#ifndef _JNX_GATEWAY_DATA_UTILS_H_
#define _JNX_GATEWAY_DATA_UTILS_H_

#include "jnx-gateway-data.h"

#define offsetof(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

/**
 * This function is used to compute the HASH Value of the GRE-TUNNEL 
 * Key.
 * 
 * @param[in] key       Key of the GRE Tunnel
 *
 * @return Result of the operation i.e. the Hash Value.
 */
extern uint32_t jnx_gw_data_compute_gre_hash(jnx_gw_gre_key_hash_t*  key);

/**
 * This function is used to compute the HASH Value of the IPIP-TUNNEL 
 * Key.
 * 
 * @param[in] key       Key of the IPIP Tunnel
 *
 * @return Result of the operation i.e. the Hash Value.
 */
extern uint32_t jnx_gw_data_compute_ipip_tunnel_hash(
                            jnx_gw_ipip_tunnel_key_hash_t* key);

/**
 * This function is used to compute the HASH Value of the IPIP-SUB-TUNNEL 
 * Key.
 * 
 * @param[in] key       Key of the IPIP-SUB- Tunnel
 *
 * @return Result of the operation i.e. the Hash Value.
 */
extern uint32_t jnx_gw_data_compute_ipip_sub_tunnel_hash(
                             jnx_gw_data_ipip_sub_tunnel_key_hash_t* key);

/**
 * This function is used to compute the HASH Value of the VRF
 * Key.
 * 
 * @param[in] key      VRF 
 *
 * @return Result of the operation i.e. the Hash Value.
 */
extern uint32_t jnx_gw_data_compute_vrf_hash(jnx_gw_vrf_key_t vrf);



/**
 * This function is used to perform a lookup in the GRE Tunnel DB.
 * This function performs the lookup operation after acquiring a lock
 * on the bucket. It is primarily used by the control thread during 
 * the configuration of the tunnel.
 *
 * @param[in] app_cb   State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] key      Key of the GRE Tunnel.
 *
 * @return  Result of the lookup
 *   @li   jnx_gw_data_gre_tunnel_t*       Pointer to the Tunnel, which matches the
 *                                         key
 *   @li   NULL                            Tunnel corresponding to the Key doesn't
 *                                         exist.
 */
extern jnx_gw_data_gre_tunnel_t* jnx_gw_data_db_gre_tunnel_lookup_with_lock(
                        jnx_gw_data_cb_t* app_cb, jnx_gw_gre_key_hash_t*  key);

/**
 * This function is used to perform a lookup in the GRE Tunnel DB.
 * This function performs the lookup operation without acquiring a lock
 * on the bucket. It is primarily used by the data thread during 
 * the packet processing.
 *
 * @param[in] app_cb   State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] key      Key of the GRE Tunnel.
 *
 * @return  Result of the lookup
 *     @li  jnx_gw_data_gre_tunnel_t*       Pointer to the Tunnel, which matches the
 *                                          key
 *     @li  NULL                            Tunnel corresponding to the Key doesn't
 *                                          exist.
 */
extern jnx_gw_data_gre_tunnel_t* jnx_gw_data_db_gre_tunnel_lookup_without_lock(
                        jnx_gw_data_cb_t*  app_cb, jnx_gw_gre_key_hash_t*  gre_key);

/**
 * This function is used to add a GRE Tunnel in the GRE-TUNNEL-DB
 *
 * @param[in] app_cb   State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] key      Key of the GRE Tunnel to be added.
 *
 * @return  Result of the lookup
 *    @li  jnx_gw_data_gre_tunnel_t*       Pointer to the newly added GRE Tunnel
 *    @li   NULL                           Tunnel couldn't be added in the DB
 */
extern jnx_gw_data_gre_tunnel_t* jnx_gw_data_db_add_gre_tunnel(
                            jnx_gw_data_cb_t*   app_cb, jnx_gw_gre_key_hash_t*   key); 

/**
 * This function is used to del the  GRE Tunnel from the GRE-TUNNEL-DB.
 * Tunnel is just removed from the hash bucket. HOwever, the pointer
 * still remains valid. Pointer is not freed in this routine 
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] gre_tunnel     Pointer of the tunnel to be deleted.
 *
 * @return  Result of the operation
 *   @li     JNX_GW_DATA_SUCCESS     Operation Successful
 *   @li     JNX_GW_DATA_FAILURE     Operation Failed
 */
extern int jnx_gw_data_db_del_gre_tunnel(jnx_gw_data_cb_t*          app_cb,
                                         jnx_gw_data_gre_tunnel_t*  gre_tunnel); 

/**
 * This function is used to delete the  GRE Tunnel from the list of tunnels in a
 * VRF.
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] gre_tunnel     Pointer of the tunnel to be deleted.
 *
 * @return  Result of the operation
 *    @li      JNX_GW_DATA_SUCCESS     Operation Successful
 *    @li      JNX_GW_DATA_FAILURE     Operation Failed
 */
extern int jnx_gw_data_db_remove_gre_tunnel_from_vrf(
         jnx_gw_data_cb_t*   app_cb, jnx_gw_data_gre_tunnel_t*  gre_tunnel); 

/**
 * This function is used to perform a lookup in the IPIP-SUB TUNNEL DB to
 * find a tunnel corresponding to the key. The lookup is done after acquiring 
 * a lock on the hash bucket corresponding to the key This function is 
 * primarily used by the data processing threads.
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] key            Key of the IP-IP Sub Tunnel.
 *
 * @return  Result of the operation
 *    @li    jnx_gw_data_ipip_sub_tunnel_t*   Pointer to the Tunnel
 *    @li    NULL                             Tunnel doesn't exist 
 */
extern jnx_gw_data_ipip_sub_tunnel_t*  
jnx_gw_data_db_ipip_sub_tunnel_lookup_with_lock(jnx_gw_data_cb_t*  app_cb,
                                   jnx_gw_data_ipip_sub_tunnel_key_hash_t* key);

/**
 * This function is used to perform a lookup in the IPIP-SUB TUNNEL DB to
 * find a tunnel corresponding to the key. The lookup is done without acquiring a
 * lock on the the hash bucket. This function is primarily used by the control
 * threads.  
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] Key            Pointer to the Key
 *
 * @return  Result of the operation
 *    @li     jnx_gw_data_ipip_sub_tunnel_t*    Pointer to the tunnel
 *    @li     JNX_GW_DATA_FAILURE               Operation Failed
 */
extern jnx_gw_data_ipip_sub_tunnel_t*  
jnx_gw_data_db_ipip_sub_tunnel_lookup_without_lock(jnx_gw_data_cb_t*  app_cb,
                                     jnx_gw_data_ipip_sub_tunnel_key_hash_t* key);

/**
 * This function is used to add an IP-IP Sub tunnnel in the IPIP_SUB-TUNNEL HASH DB
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] Key            Pointer to the key of the tunnel to be deleted.
 *
 * @return  Result of the operation
 *    @li     jnx_gw_data_ipip_sub_tunnel_t*    Pointer to the tunnel
 *    @li     JNX_GW_DATA_FAILURE               Operation Failed
 */
extern jnx_gw_data_ipip_sub_tunnel_t*  jnx_gw_data_db_add_ipip_sub_tunnel(
          jnx_gw_data_cb_t*     app_cb, jnx_gw_data_ipip_sub_tunnel_key_hash_t* key);

/**
 * This function is used to add an IP-IP Sub tunnnel in the IPIP_SUB-TUNNEL HASH DB
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] tunnel         Pointer to the tunnel to be deleted.
 *
 * @return  Result of the operation
 *    @li     JNX_GW_DATA_SUCCESS               Operation succeeded. 
 *    @li     JNX_GW_DATA_FAILURE               Operation Failed.
 */
extern int jnx_gw_data_db_del_ipip_sub_tunnel(jnx_gw_data_cb_t*     app_cb,
                                 jnx_gw_data_ipip_sub_tunnel_t*     tunnel);

/**
 * This function is used to perform a lookup in the IPIP-TUNNEL DB to
 * find a tunnel corresponding to the key. The lookup is done after acquiring a
 * lock on the the hash bucket. This function is primarily used by the data
 * processing threads.  
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] Key            Pointer to the Key
 *
 * @return  Result of the operation
 *    @li     jnx_gw_data_ipip_tunnel_t*    Pointer to the tunnel
 *    @li     NULL                          Tunnel doesn't exist
 */
extern jnx_gw_data_ipip_tunnel_t*  jnx_gw_data_db_ipip_tunnel_lookup_with_lock(
              jnx_gw_data_cb_t*  app_cb, jnx_gw_ipip_tunnel_key_hash_t*    key);

/**
 * This function is used to perform a lookup in the IPIP-TUNNEL DB to
 * find a tunnel corresponding to the key. The lookup is done without acquiring a
 * lock on the the hash bucket. This function is primarily used by the control
 * threads.  
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] Key            Pointer to the Key
 *
 * @return  Result of the operation
 *    @li     jnx_gw_data_ipip_tunnel_t*    Pointer to the tunnel
 *    @li     NULL                          Tunnel doesn't exist
 */
extern jnx_gw_data_ipip_tunnel_t*  jnx_gw_data_db_ipip_tunnel_lookup_without_lock(
                   jnx_gw_data_cb_t*  app_cb, jnx_gw_ipip_tunnel_key_hash_t*  key);

/**
 * This function is used to add an IP-IP tunnnel in the IPIP-TUNNEL HASH DB
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] Key            Pointer to the key of the tunnel to be added
 *
 * @return  Result of the operation
 *    @li     jnx_gw_data_ipip_tunnel_t*    Pointer to the tunnel
 *    @li     NULL                          Tunnel couldn't be added
 */
extern jnx_gw_data_ipip_tunnel_t*  jnx_gw_data_db_add_ipip_tunnel(
         jnx_gw_data_cb_t*     app_cb, jnx_gw_ipip_tunnel_key_hash_t*  key);

/**
 * This function is used to delete an IP-IP tunnnel in the IPIP-TUNNEL HASH DB
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] tunnel         Pointer to the tunnel to be deleted
 *
 * @return  Result of the operation
 *    @li    JNX_GW_DATA_SUCCESS      Operation Succeeded.
 *    @li    JNX_GW_DATA_FAILURE      Operation Failed.
 */
extern int jnx_gw_data_db_del_ipip_tunnel(jnx_gw_data_cb_t*          app_cb,
                                          jnx_gw_data_ipip_tunnel_t* tunnel);

/**
 * This function is used to remove an  IP-IP tunnnel from the VRF. This function
 * is called once the tunnel is removed from the IPIP-TUNNEL-DB
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] tunnel         Pointer to the tunnel to be deleted
 *
 * @return  Result of the operation
 *    @li    JNX_GW_DATA_SUCCESS      Operation Succeeded.
 *    @li    JNX_GW_DATA_FAILURE      Operation Failed.
 */
extern int jnx_gw_data_db_remove_ipip_tunnel_from_vrf(jnx_gw_data_cb_t*  app_cb,
                                   jnx_gw_data_ipip_tunnel_t*  ipip_tunnel); 

/**
 * This function is used to add a VRF entry. If the VRF Entry already exists
 * then it resturns the pointer of the existing entry. 
 *
 * @param[in] app_cb    State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] vrf       VRF
 *
 * @return  Result of the operation
 *    @li    jnx_gw_data_vrf_stat_t*  Pointer of the VRF entry
 *    @li    NULL                     Entry couldn't be added.
 */
extern jnx_gw_data_vrf_stat_t*  jnx_gw_data_db_add_vrf_entry(
                jnx_gw_data_cb_t*    app_cb, jnx_gw_vrf_key_t  vrf);

/**
 * This function is used to lookup vrf entry in the VRF Database
 *
 * @param[in] app_cb         State Control Block of the JNX_GATEWAY_CB_T 
 * @param[in] tunnel         Pointer to the tunnel to be deleted
 *
 * @return  Result of the operation
 *    @li    jnx_gw_data_vrf_stat_t   Pointer to the VRF Entry
 *    @li    NULL                     Entry doesn't exist      
 */
extern jnx_gw_data_vrf_stat_t*  jnx_gw_data_db_vrf_entry_lookup(
                jnx_gw_data_cb_t*    app_cb, jnx_gw_vrf_key_t     vrf);
#endif

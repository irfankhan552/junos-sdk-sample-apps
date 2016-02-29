/*
 * $Id: jnx-gateway-data.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-gateway-data.h - Main Header file of the JNX_GATEWAY-DATA
 *                      application. 
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
 * @file jnx-gateway-data.h
 * @brief Main Header file of the JNX-GW-DATA application. 
 *
 * This header file represents the core of the jnx-gateway-data application. It defines
 * the control block viz. JNX_GW_DATA_CB_T for the application. This control
 * block maintains the following:-
 * 1. state of the application
 * 2. GRE-TUNNEL DB, IPIP-TUNNEL DB, IPIP-SUB TUNNEL DB & VRF DB 
 * 3. Binding of the Rx & Tx FIFOs to their indvidual threads 
 * 4. Packet processing context for the individual thread
 * 5. List of tunnels which have been deleted by configuration, but 
 *    have been kept to allow for in flight packets processing.
 * 6. Pre allocated buffer used by the JNX-GW-DATA to send responses to
 *    the JNX-GW-CTRL & JNX-GW-MGMT.
 * 7. Name of the RE (JNX-GW-MGMT runs there), CONTORL-PIC (JNX-GW-CTRL runs
 *    here) & DATA-PIC(it's own name).
 *
 * It also defines the various parameters    
 */

#ifndef JNX_GATEWAY_DATA_H_
#define JNX_GATEWAY_DATA_H_

#include <stdlib.h>
#include <jnx/aux_types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <isc/eventlib.h>
#include <jnx/pconn.h>
#include <isc/eventlib.h>
#include <jnx/atomic.h>
#include <pthread.h>
#include <sys/jnx/jbuf.h>
#include <jnx/mpsdk.h>
#include <jnx/trace.h>
#include <jnx/msp_locks.h>
#include <jnx/msp_objcache.h>
#include <jnx/jnx-gateway.h>
#include "jnx-gateway-data_db.h"
#include "jnx-gateway-data_packet.h"

#define JNX_GW_DATA_MAX_BUF_SIZE 4096
#define JNX_GW_DATA_SOCK_TIMEOUT 10
#define JNX_GW_DATA_MAX_SVR_SESN_COUNT 2 /* one each for management & control */

#define JNX_GW_DATA_PERIODIC_STAT_TIME_SEC         10
#define JNX_GW_DATA_PERIODIC_CLEANUP_TIME_SEC      10

#define PATH_JNX_GW_TRACE "/var/log/jnx-gateway-data"


/**
 * This enum defines the state of the SAMPLE DATA APP
 */
typedef enum {

    JNX_GW_DATA_STATE_INIT,     /**<Application is initializing, no packet processing will be done */
    JNX_GW_DATA_STATE_READY,    /**<Application is ready for packet processing */  
    JNX_GW_DATA_STATE_SHUTDOWN  /**<Application is shutting down, no packets will be accpeted. Only
                                    inflight packets will get processed */
}jnx_gw_data_states;

/**
 * This enum defines the various errors returned by the JNX-GATEWAY-DATA
 * application.
 */
typedef enum {

    JNX_GW_DATA_SUCCESS,   
    JNX_GW_DATA_DROP_PKT,  
    JNX_GW_DATA_DROP_MSG,
    JNX_GW_DATA_FAILURE
}jnx_gw_data_err_t;

/**
 * This structure defines the Control Block for the application. It is
 * responsible for holding all the information related to the application
 * and governs the behavior of the application
 */
typedef struct jnx_gw_data_cb_s{

   jnx_gw_data_lock_t                app_cb_lock;          /**<Lock on the Application CB */
   uint8_t                           num_agents;          /**<Number of threads application is running */
   uint8_t                           session_count;       /**< Count of the number of sessions ready */
   pconn_server_t*                   conn_server;         /**<Server Socket to communicatw with RE (Mgmt App)i & Control PIc (Ctrl APP)*/
   pconn_session_t*                  session[2];          /** Sessions with RE & CTRL */
   pconn_client_t*                   conn_client;         /**<Client for Pconn server on JNX_GATEWAY_MGMT */
   jnx_gw_data_states                app_state;           /**<State of the application */
   jnx_gw_data_ipip_sub_tunnel_db_t  ipip_sub_tunnel_db;  /**<Hash Table of IP-IP Sub Tunnels */
   jnx_gw_data_ipip_tunnel_db_t      ipip_tunnel_db;      /**<Hash Table of IP-IP Tunnels */
   jnx_gw_data_gre_tunnel_db_t       gre_db;              /**<Hash TAble of GRE Tunnels   */
   jnx_gw_data_vrf_db_t              vrf_db;              /**<Hash Table of VRF based summary stats */
   jnx_gw_pkt_proc_ctxt_t            pkt_ctxt[JNX_GW_MAX_APP_AGENTS];/* Packet processign contect for each thread */
   char*                             buffer;                 /**<Preallocated buffer used by control thread to send messages
                                                                 to the control/mgmt */
   jnx_gw_data_del_tunnels_list_t    del_tunnels;            /**<List of tunnels which have been deleted but are kept for 
                                                                 some time to ensure that the in flight packets are processed*/
   uint32_t                         ip_id;                 /*IP ID to sent in the packets */
} jnx_gw_data_cb_t;

/**
 * This structure defines the paramters to be passed to the
 * data threads at the time of spawning them
 */ 
typedef struct {
    
    jnx_gw_data_cb_t*   app_cb;     /**<Control Blcok of the Data App */
    uint32_t           agent_num;  /**<Agent Num(this is not the value passed by thread create API */
    int                 cpu_id;     /**<Cpu on which the thread would be running */

}jnx_gw_data_thread_args_t;

extern trace_file_t * jnx_gw_trace_file;

/* Function to periodically cleanup the deleted tunnels */
void jnx_gw_periodic_cleanup_timer_expiry(evContext context, void* uap, struct timespec due,
                                    struct timespec inter);

/* macros for alloc & mutex functions */
#define jnx_gw_data_lock_init(lock) msp_spinlock_init(lock)
#define jnx_gw_data_acquire_lock(lock) msp_spinlock_lock(lock)
#define jnx_gw_data_release_lock(lock) msp_spinlock_unlock(lock)

/* Function to shut down the application */
void jnx_gw_data_shutdown(jnx_gw_data_cb_t* app_cb);

uint32_t jnx_gw_data_compute_checksum(struct jbuf * jb, uint32_t offset,
                                  uint32_t len);
void jnx_gw_data_update_ttl_compute_inc_checksum(struct ip * ip_hdr);
void
jnx_gw_data_pconn_client_event_handler(pconn_client_t* client __unused,
                                       pconn_event_t   event);
void 
jnx_gw_data_pconn_event_handler(pconn_session_t *session, 
                                pconn_event_t   event, 
                                void*           cookie);
status_t
jnx_gw_data_pconn_msg_handler(pconn_session_t*    session, 
                              ipc_msg_t*          ipc_msg,
                              void*               cookie);
jnx_gw_data_err_t
jnx_gw_data_connect_mgmt(jnx_gw_data_cb_t* app_cb, 
                          evContext  ev_ctxt);
void
jnx_gw_data_pconn_client_msg_handler(pconn_client_t*   client __unused,
                                     void*            msg __unused,
                                     void*            cookie __unused);

#endif

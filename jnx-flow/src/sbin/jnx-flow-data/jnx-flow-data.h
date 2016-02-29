/*
 * $Id: jnx-flow-data.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-flow-data.h - contains the control block structure definition
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
 * @file jnx-flow-data.h
 * @brief  This file describes various data structures and enum/macro
 * definitions for jnx-flow-data agent module
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <net/if_802.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#include <pthread.h>
#include <signal.h>
#include <syslog.h>

#include <jnx/trace.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/mpsdk.h>
#include <jnx/pconn.h>
#include <jnx/jnx_paths.h>
#include <sys/jnx/jbuf.h>
#include <jnx/atomic.h>
#include <jnx/msp_hw_ts.h>
#include <jnx/msp_locks.h>
#include <jnx/msp_objcache.h>

#include <jnx/jnx-flow.h>
#include <jnx/jnx-flow_msg.h>

#define JNX_FLOW_DATA_FLOW_BUCKET_COUNT             (512*1024)
#define JNX_FLOW_DATA_FLOW_EXPIRY_TIME_SEC          (20)
#define JNX_FLOW_DATA_PERIODIC_SEC                  (5)
#define JNX_FLOW_DATA_FLOW_BUCKET_COUNT_PER_SEC     (JNX_FLOW_DATA_FLOW_BUCKET_COUNT >>10)

#define JNX_FLOW_HASH_MAGIC_NUMBER    0x5f5f
#define JNX_FLOW_DATA_FLOW_HASH_MASK  (JNX_FLOW_DATA_FLOW_BUCKET_COUNT -1)

/*
 * typedef declarations
 */

typedef struct jnx_flow_data_cb jnx_flow_data_cb_t;

/*
 * Enum for the data application status
 */
typedef enum jnx_flow_data_status {
    JNX_FLOW_DATA_STATUS_INVALID = 0,   /**< invalid status */
    JNX_FLOW_DATA_STATUS_INIT,          /**< init status */
    JNX_FLOW_DATA_STATUS_UP,            /**< up status */
    JNX_FLOW_DATA_STATUS_DOWN,          /**< down status */
    JNX_FLOW_DATA_STATUS_DELETE,        /**< delete status */
    JNX_FLOW_DATA_STATUS_SHUTDOWN,      /**< shudown status */
    JNX_FLOW_DATA_STATUS_MAX            /**< maximum status */
} jnx_flow_data_status_t;

/*
 * maximum number of packet processing threads
 */
#define JNX_FLOW_DATA_PKT_THREAD_COUNT    32 /**< maximum data threads */
#define JNX_FLOW_DATA_USER_THREAD_COUNT    32 /**< maximum user threads */

/*
 * list entry information structure
 */
typedef struct jnx_flow_data_list_entry {
    void                             *ptr;    /**< payload */
    struct jnx_flow_data_list_entry  *next;   /**< next in the list */
} jnx_flow_data_list_entry_t;

/*
 * list head information structure
 */
typedef struct jnx_flow_data_list_head {
    uint32_t                      list_id;    /**< list index */
    uint32_t                      list_count; /**< list entry count */
    jnx_flow_data_list_entry_t   *list_head;  /**< list head entry */
    jnx_flow_data_list_entry_t   *list_tail;  /**< list tail entry */
} jnx_flow_data_list_head_t;


/*
 * rule entry information structure 
 */
typedef struct jnx_flow_data_rule_entry {
    patnode                     rule_node;      /**< rule patricia node */
    char                        rule_name[JNX_FLOW_STR_SIZE]; /**< rule name */
    uint32_t                    rule_id;        /**< rule index, key*/
    uint32_t                    rule_flags;     /**< rule flags, if any */
    jnx_flow_data_status_t      rule_status;    /**< rule status*/
    jnx_flow_rule_action_type_t rule_action;    /**< rule action type*/
    jnx_flow_rule_match_type_t  rule_match;     /**< rule match type*/
    jnx_flow_rule_dir_type_t    rule_direction; /**< rule direction */
    jnx_flow_rule_match_t       rule_session;   /**< rule match info*/
    jnx_flow_rule_stats_t       rule_stats;     /**< rule statistics*/
} jnx_flow_data_rule_entry_t;

/*
 * service-set entry information structure 
 */
typedef struct jnx_flow_data_svc_set {
    patnode                  svc_node;      /**< serivce patnode */
    patnode                  svc_id_node;   /**< serivce patnode */
    jnx_flow_svc_key_t       svc_key;       /**< serivce key */
    jnx_flow_data_status_t   svc_status;    /**< serivce status */
    jnx_flow_svc_type_t      svc_type;      /**< serivce flags */
    uint32_t                 svc_id;        /**< serivce index */
    char                     svc_name[JNX_FLOW_STR_SIZE]; /**< serivce name */
    char                     svc_intf[JNX_FLOW_STR_SIZE]; /**< interface name */
                                           
    uint32_t                 svc_iif;       /**< serivce ingress subunit */
    uint32_t                 svc_oif;       /**< serivce egress subunit */
                                     
    uint32_t                   svc_rule_count;/**< serivce rule count*/
    jnx_flow_data_list_head_t  svc_rule_set;  /**< serivce rule set */
    jnx_flow_svc_stats_t       svc_stats;     /**< serivce statistics */
} jnx_flow_data_svc_set_t;

/*
 * flow entry information structure 
 */
typedef struct jnx_flow_data_flow_entry {
    struct jnx_flow_data_flow_entry   *flow_next;    /**< next flow */
    struct jnx_flow_data_flow_entry   *flow_entry;   /**< reverse flow */
    jnx_flow_data_status_t             flow_status;  /**< flow status */
    jnx_flow_rule_action_type_t        flow_action;  /**< flow action */
    jnx_flow_rule_dir_type_t           flow_dir;     /**< flow direction */
    jnx_flow_session_key_t             flow_key;     /**< flow key */
    uint32_t                           flow_svc_id;  /**< svc set index */
    uint32_t                           flow_rule_id; /**< flow rule index */
    uint32_t                           flow_oif;     /**< flow egress subunit */
    uint64_t                           flow_ts;      /**< flow timestamp */
    msp_spinlock_t                     flow_lock;    /**< flow lock */
    jnx_flow_flow_stats_t              flow_stats;   /**< flow statistics */
} jnx_flow_data_flow_entry_t;


/*
 * data thread context information structure 
 */
typedef struct jnx_flow_data_pkt_ctxt {
    uint32_t                      thread_idx;     /**< thread cpu num */
    msp_data_handle_t             thread_handle;  /**< thread handle */
    jnx_flow_data_cb_t           *data_cb;        /**< data control block */
                                                  
    struct jbuf                  *pkt_buf;        /**< packet buffer */
    struct ip                    *ip_hdr;         /**< ip header */
    struct tcphdr                *tcp_hdr;        /**< tcp header */
                                                  
    uint32_t                      pkt_len;        /**< packet length */
    uint64_t                      pkt_ts;         /**< packet timestamp */
                                                  
    jnx_flow_svc_key_t            svc_key;        /**< service key */
    jnx_flow_session_key_t        flow_key;       /**< flow key */
                                                  
    jnx_flow_data_flow_entry_t   *flow_entry;     /**< flow entry */
    jnx_flow_data_rule_entry_t   *rule_entry;     /**< rule entry */
    jnx_flow_svc_type_t           flow_type;      /**< flow type */
    jnx_flow_rule_dir_type_t      flow_direction; /**< flow direction */
    jnx_flow_rule_action_type_t   flow_action;    /**< flow action */
    uint32_t                      rule_count;     /**< rule count */
    uint32_t                      flow_hash;      /**< flow hash */

} jnx_flow_data_pkt_ctxt_t;

typedef int (*jnx_flow_data_flow_hash_func_t)(jnx_flow_data_flow_entry_t * ptr);

typedef struct jnx_flow_data_flow_hash_bucket {
    uint32_t                    hash_count;
    msp_spinlock_t              hash_lock;
    jnx_flow_data_flow_entry_t *hash_chain;
    jnx_flow_data_flow_entry_t *hash_last;
} jnx_flow_data_flow_hash_bucket_t;

typedef struct jnx_flow_data_flow_db { 
    jnx_flow_data_flow_hash_bucket_t hash_bucket [JNX_FLOW_DATA_FLOW_BUCKET_COUNT];
    jnx_flow_data_flow_hash_func_t   hash_func;
} jnx_flow_data_flow_db_t;

/*
 * jnx-flow data application control block information structure 
 */
struct jnx_flow_data_cb {
    jnx_flow_data_status_t    cb_status;         /**< jnx-flow data status */
    evContext                 cb_ctxt;           /**< event context */
    pconn_server_t           *cb_conn_server;    /**< pconn server handle */
    pconn_session_t          *cb_conn_session;   /**< pconn session handle */
    pconn_client_t           *cb_conn_client;    /**< pconn client handle */
                                                 
    msp_spinlock_t            cb_config_lock;    /**< config lock */
    patroot                   cb_svc_db;         /**< service set db */
    patroot                   cb_svc_id_db;      /**< service set db */
    patroot                   cb_rule_db;        /**< rule set db */

    msp_shm_handle_t          shm_handle;        /**< Shared memory handle */
                                                 
    msp_oc_handle_t           cb_buf_oc;         /**< buffer oc handle */
    msp_oc_handle_t           cb_hash_oc;        /**< hash oc handle */
    msp_oc_handle_t           cb_flow_oc;        /**< flow oc handle */
    msp_oc_handle_t           cb_rule_oc;        /**< rule oc handle */
    msp_oc_handle_t           cb_svc_oc;         /**< svc oc handle */
    msp_oc_handle_t           cb_list_oc;        /**< list oc handle */
                                                 
    jnx_flow_data_flow_db_t  *cb_flow_db;        /**< flow db */

    uint64_t                  cb_periodic_ts;    /**< periodic time stamp */
    char                     *cb_send_buf;       /**< send buffer */
    uint32_t                  cb_msg_len;        /**< send buffer length */
    uint32_t                  cb_msg_count;      /**< message count */

    jnx_flow_data_pkt_ctxt_t  cb_pkt_ctxt[JNX_FLOW_DATA_PKT_THREAD_COUNT];
                                                /**< thread context array */
    jnx_flow_data_stat_t      cb_stats;         /**< application statistics */
    uint32_t                 user_cpu[JNX_FLOW_DATA_USER_THREAD_COUNT];
                                                /**< Max number of user
                                                 * threads*/
    uint32_t                hash_bucket_num;   /** <Hash buckets scanned till
                                                    now */
};

/*
 * log macro
 */
#define jnx_flow_log(_prio, fmt...) \
    ({syslog(_prio, fmt); })

#define JNX_FLOW_DATA_HASH_CHAIN(data_cb, hash)\
       ((data_cb)->cb_flow_db->hash_bucket[hash].hash_chain)

#define JNX_FLOW_DATA_HASH_LAST(data_cb, hash)\
       ((data_cb)->cb_flow_db->hash_bucket[hash].hash_last)

#define JNX_FLOW_DATA_HASH_COUNT(data_cb, hash)\
       ((data_cb)->cb_flow_db->hash_bucket[hash].hash_count)
/*
 * locking primitive definitions
 */
#define JNX_FLOW_DATA_CONFIG_LOCK_INIT(data_cb) \
    ({ msp_spinlock_init(&data_cb->cb_config_lock); })

#define JNX_FLOW_DATA_ACQUIRE_CONFIG_READ_LOCK(data_cb) \
    ({ msp_spinlock_lock(&data_cb->cb_config_lock); })

#define JNX_FLOW_DATA_RELEASE_CONFIG_READ_LOCK(data_cb) \
    ({ msp_spinlock_unlock(&data_cb->cb_config_lock); })

#define JNX_FLOW_DATA_ACQUIRE_CONFIG_WRITE_LOCK(data_cb) \
    ({ msp_spinlock_lock(&data_cb->cb_config_lock); })

#define JNX_FLOW_DATA_RELEASE_CONFIG_WRITE_LOCK(data_cb)\
    ({ msp_spinlock_unlock(&data_cb->cb_config_lock); })

#define JNX_FLOW_DATA_HASH_LOCK_INIT(flow_db, hash) \
    ({ msp_spinlock_init(&(flow_db)->hash_bucket[hash].hash_lock); })

#define JNX_FLOW_DATA_HASH_LOCK(flow_db, hash) \
    ({ msp_spinlock_lock(&(flow_db)->hash_bucket[hash].hash_lock); })

#define JNX_FLOW_DATA_HASH_UNLOCK(flow_db, hash) \
     ({ msp_spinlock_unlock(&(flow_db)->hash_bucket[hash].hash_lock); })

#define JNX_FLOW_DATA_FLOW_LOCK_INIT(flow) \
    ({ msp_spinlock_init(&(flow)->flow_lock); })

#define JNX_FLOW_DATA_FLOW_LOCK(flow) \
    ({ msp_spinlock_lock(&(flow)->flow_lock); })

#define JNX_FLOW_DATA_FLOW_UNLOCK(flow) \
     ({ msp_spinlock_unlock(&(flow)->flow_lock); })

/*
 * use objcache
 */
#define FLOW_ALLOC(oc, cpu_num) ({ msp_objcache_alloc(oc, cpu_num, 0); })

#define FLOW_FREE(oc, entry, cpu_num) \
    ({ msp_objcache_free(oc, entry, cpu_num, 0); })

#define RULE_ALLOC(oc, cpu_num) ({ msp_objcache_alloc(oc, cpu_num, 0); })

#define RULE_FREE(oc, entry, cpu_num) \
    ({ msp_objcache_free(oc, entry, cpu_num, 0); })

#define SVC_ALLOC(oc, cpu_num)  ({ msp_objcache_alloc(oc, cpu_num, 0); })

#define SVC_FREE(oc, entry, cpu_num) \
    ({ msp_objcache_free(oc, entry, cpu_num, 0); })

#define LIST_ALLOC(oc, cpu_num) ({ msp_objcache_alloc(oc, cpu_num, 0); })

#define LIST_FREE(oc, entry, cpu_num) \
    ({ msp_objcache_free(oc, entry, cpu_num, 0); })

#define BUF_ALLOC(oc, cpu_num)  ({ msp_objcache_alloc(oc, cpu_num, 0); })

#define BUF_FREE(oc, entry, cpu_num) \
    ({ msp_objcache_free(oc, entry, cpu_num, 0); })

#define HASH_ALLOC(oc, cpu_num) ({ msp_objcache_alloc(oc, cpu_num, 0); })

#define HASH_FREE(oc, entry, cpu_num) \
    ({ msp_objcache_free(oc, entry, cpu_num, 0); })

/*
 * macro for setting the out subunit in the jbuf structure
 * for nexthop style services
 */

#define jbuf_set_ointf(pkt_buf, ointf)  ({(pkt_buf)->jb_xmit_subunit = ointf;})


/*
 * Function prototype defintions
 */
extern void * jnx_flow_data_process_packet(void * loop_args);
extern status_t
jnx_flow_data_mgmt_msg_handler(pconn_session_t * session __unused,
                               ipc_msg_t * ipc_msg, void * cookie);
extern void
jnx_flow_data_mgmt_event_handler(pconn_session_t * session,
                                 pconn_event_t event, 
                                 void * cookie);
extern status_t 
jnx_flow_data_update_ts(jnx_flow_data_cb_t * data_cb, uint64_t * ts);

extern jnx_flow_data_svc_set_t *  
jnx_flow_data_svc_set_lookup(jnx_flow_data_cb_t * data_cb,
                             jnx_flow_svc_key_t * svc_key);

jnx_flow_data_svc_set_t *  
jnx_flow_data_svc_set_id_lookup(jnx_flow_data_cb_t * data_cb, uint32_t svc_id);

extern uint32_t 
jnx_flow_data_get_flow_hash(jnx_flow_session_key_t * flow_key);

extern jnx_flow_data_rule_entry_t *  
jnx_flow_data_rule_lookup(jnx_flow_data_cb_t * data_cb, uint32_t rule_id);

extern jnx_flow_data_svc_set_t *  
jnx_flow_data_svc_set_get_next(jnx_flow_data_cb_t * data_cb,
                               jnx_flow_data_svc_set_t * svc_set);
extern void
jnx_flow_data_print_all(jnx_flow_data_cb_t * data_cb);

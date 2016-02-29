/*
 * $Id: jnx-gateway-ctrl.h 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-ctrl.h - data structure definitions
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

#include <jnx/ssd_ipc.h>
#include <jnx/ssd_ipc_msg.h>

/*
 * jnx-gateway-ctrl trace file 
 */

#define PATH_JNX_GW_TRACE "/var/log/jnx-gateway-ctrl"

/* receives this size messages from the gateway module */
#define JNX_GW_CTRL_MAX_PKT_BUF_SIZE   1460
#define JNX_GW_CTRL_DFLT_CPU_COUNT     4
#define JNX_GW_CTRL_BUF_COUNT          1024

#ifndef MSP_PREFIX
#define MSP_PREFIX "ms"
#define SERVICES_PREFIX "sp"
#define LO_PREFIX "lo"
#endif

typedef struct jnx_gw_ctrl_intf_s jnx_gw_ctrl_intf_t;
typedef struct jnx_gw_ctrl_vrf_s jnx_gw_ctrl_vrf_t;
typedef struct jnx_gw_ctrl_ipip_gw_s jnx_gw_ctrl_ipip_gw_t;
typedef struct jnx_gw_ctrl_gre_gw_s jnx_gw_ctrl_gre_gw_t;
typedef struct jnx_gw_ctrl_data_pic_s jnx_gw_ctrl_data_pic_t;
typedef struct jnx_gw_ctrl_user_s jnx_gw_ctrl_user_t;
typedef struct jnx_gw_ctrl_route_s jnx_gw_ctrl_route_t;
typedef struct jnx_gw_ctrl_policy_s jnx_gw_ctrl_policy_t;
typedef struct jnx_gw_ctrl_gre_session_s jnx_gw_ctrl_gre_session_t;
typedef struct jnx_gw_ctrl_buf_s jnx_gw_ctrl_buf_t;
typedef struct jnx_gw_ctrl_buf_list_s jnx_gw_ctrl_buf_list_t;
typedef struct jnx_gw_ctrl_rx_thread_s jnx_gw_ctrl_rx_thread_t;
typedef struct jnx_gw_ctrl_proc_thread_s jnx_gw_ctrl_proc_thread_t;
typedef struct jnx_gw_ctrl_sock_list_s jnx_gw_ctrl_sock_list_t;

#define JNX_GW_DFLT_PREFIX_LEN   32
#define JNX_GW_HOST_MASK         0xFFFFFFFF
#define JNX_GW_DFLT_NUM_GATEWAYS 1
/*
 * route type, whether its an interface route, or,
 * a client route set for directing native reverse
 * traffic
 */
enum {
    JNX_GW_CTRL_INTF_RT = 1,
    JNX_GW_CTRL_CLIENT_RT,
};

/* application route structure */
struct jnx_gw_ctrl_route_s {
    patnode                 route_node; /**< add to the interface node */
    uint32_t                key;        /**< key .. */
    uint32_t                addr;       /**< prefix address */
    uint32_t                mask;       /**< prefix mask */
    uint32_t                intf_id;    /**< interface index */
    uint32_t                vrf_id;     /**< vrf index */
    struct ssd_route_parms  rtp;        /**< route add */
    jnx_gw_ctrl_intf_t     *pintf;      /**< interface back pointer */
    uint8_t                 status;     /**< route status */
    uint8_t                 flags;      /**< route flags */
    uint32_t                ref_count;  /**< reference count */
};

/* client session 5 tuple */
typedef struct jnx_gw_ctrl_session_info_s {
    uint8_t          sesn_proto;        /**< protocol */
    uint32_t         sesn_client_ip;    /**< source ip */
    uint32_t         sesn_server_ip;    /**< destination ip */
    uint16_t         sesn_sport;        /**< source port */
    uint16_t         sesn_dport;        /**< destination port */
} jnx_gw_ctrl_session_info_t;

/* gre session structure */
struct jnx_gw_ctrl_gre_session_s {
    /* add to the vrf structure */
    patnode                   gre_vrf_sesn_node; 
    /* add to the gre gateway structure */
    patnode                   gre_sesn_node; 
    /* add to the ip-ip gateway structure */
    patnode                   gre_ipip_sesn_node;
    /* add to the user structure */
    patnode                   gre_user_sesn_node;

    /* ingress gre tunnel information */
    uint32_t                  ingress_gre_key;    /* key .. */
    uint32_t                  ingress_vrf_id; 
    uint32_t                  ingress_gw_ip;
    uint32_t                  ingress_intf_id;
    uint32_t                  ingress_self_ip;

    /* status & flags */
    uint32_t                  sesn_msgid;
    uint16_t                  sesn_resv1;
    uint8_t                   sesn_proftype;
    uint8_t                   sesn_resv0;
    uint8_t                   sesn_status;
    uint8_t                   sesn_flags;  /* egress-ipip/checksum/grekey/seq */
    uint8_t                   sesn_errcode; 

    /* session information */
    uint8_t                   sesn_proto;
    uint32_t                  sesn_client_ip;
    uint32_t                  sesn_server_ip;
    uint16_t                  sesn_sport;
    uint16_t                  sesn_dport;
    uint16_t                  sesn_id;     /* unique for the data/ctrl pic */
    uint16_t                  sesn_resv;   /* */
    uint32_t                  sesn_up_time;

    /* egress ipip information */
    uint32_t                  egress_vrf_id;
    uint32_t                  egress_intf_id;
    uint32_t                  egress_gw_ip;
    uint32_t                  egress_self_ip;

    /* data pic information */
    jnx_gw_ctrl_vrf_t        *pingress_vrf;
    jnx_gw_ctrl_vrf_t        *pegress_vrf;
    jnx_gw_ctrl_data_pic_t   *pdata_pic;
    jnx_gw_ctrl_intf_t       *pingress_intf;
    jnx_gw_ctrl_intf_t       *pegress_intf;
    jnx_gw_ctrl_gre_gw_t     *pgre_gw;
    jnx_gw_ctrl_ipip_gw_t    *pipip_gw;

    /* policy used information */
    jnx_gw_ctrl_user_t       *puser;

    /* this is the route set for the gre connection,
       its the data pic interface on the ingress vrf
       as the next hop */
    jnx_gw_ctrl_route_t      *pingress_route;

    /* if host route is set for the client,
       incase of native server connection,
       otherwise its the data pic egress ip
       route
     */
    jnx_gw_ctrl_route_t      *pegress_route;
};

/* ipip gateway structure */
struct jnx_gw_ctrl_ipip_gw_s {
    patnode            ipip_gw_node; /* add to the vrf structure */
    uint32_t           ipip_gw_ip;   /* key .. */
    uint32_t           ipip_vrf_id;
    uint8_t            ipip_gw_status;
    uint8_t            ipip_gw_flags;
    uint16_t           ipip_gw_resv0;
    jnx_gw_ctrl_vrf_t *pvrf;
    pthread_rwlock_t   gre_sesn_db_lock; /* lock for the gre sessions */
    patroot            gre_sesn_db;

    /* session statistics */
    uint32_t           datapic_count;
    uint32_t           user_policy_count;
    uint32_t           gre_gateway_count;
    uint32_t           gre_sesn_count;
    uint32_t           gre_active_sesn_count;
};

/* gre gateway structure */
struct jnx_gw_ctrl_gre_gw_s {
    patnode            gre_gw_node;      /* add to the vrf structure */
    uint32_t           gre_gw_ip;        /* key, gateway IP address */
    uint32_t           gre_vrf_id;
    pthread_rwlock_t   gre_sesn_db_lock; /* lock for the gre sessions */
    patroot            gre_sesn_db;      /* list of sesns */
    jnx_gw_ctrl_vrf_t *pvrf;
    uint8_t            gre_gw_status;
    uint8_t            gre_gw_flags;
    uint16_t           gre_gw_port;

    /* gateway session statistics */
    uint32_t           gre_sesn_count;
    uint32_t           gre_active_sesn_count;
};

/* user profile structure */
struct jnx_gw_ctrl_user_s {
    patnode                user_node;
    uint32_t               user_key;
    uint8_t                user_name[JNX_GW_STR_SIZE]; /* key */
    uint8_t                user_status;
    uint8_t                user_flags;    /* ipip gateway info */
    uint32_t               user_addr;
    uint32_t               user_mask;
    uint32_t               user_evrf_id;
    uint32_t               user_ipip_gw_ip;
    jnx_gw_ctrl_ipip_gw_t *pipip_gw;
    jnx_gw_ctrl_vrf_t     *pvrf;
    pthread_rwlock_t       gre_sesn_db_lock; /* lock for the gre sessions */
    patroot                gre_sesn_db;

    /* statistics */
    uint32_t               gre_sesn_count;
    uint32_t               gre_active_sesn_count;
};

/* control policy structure */
struct jnx_gw_ctrl_policy_s {
    patnode            ctrl_node;
    patnode            ctrl_vrf_node;
    uint32_t           ctrl_key;        /* key: addr & mask */
    uint32_t           ctrl_vrf_id; 
    uint32_t           ctrl_addr;
    uint32_t           ctrl_mask;
    uint32_t           ctrl_cur_addr;
    uint32_t           ctrl_start_addr;
    uint32_t           ctrl_end_addr;
    uint32_t           ctrl_intf_count;
    uint8_t            ctrl_status;
    uint8_t            ctrl_flags;
    uint16_t           ctrl_resv;
    patroot            ctrl_intf_db;
    jnx_gw_ctrl_vrf_t *pvrf;
};

/* data pic interface structure */
struct jnx_gw_ctrl_intf_s {
    patnode                 intf_vrf_node;  /**< add to the vrf structure */
    patnode                 intf_ctrl_node; /**< add to the ctrl structure */
    patnode                 intf_node;      /**< add to the pic structure */
    uint16_t                intf_id;        /**< key */
    uint32_t                intf_ip;        /**< use this as the self ip */
    uint32_t                intf_vrf_id;    /**< vrf id*/
    uint32_t                intf_subunit;   /**< subunit index */
    uint8_t                 intf_name[JNX_GW_STR_SIZE];
    uint8_t                 intf_status;    /**< inteface status */
    uint8_t                 intf_flags;     /**< interface flags */
    nh_idx_t                intf_nhid;      /**< ifl nexthop index */
    jnx_gw_ctrl_route_t    *intf_rt;        /**< interface route */

    /* the list of routes, for this interface, set
     * by the control app agent, on signaling phase
     * for directing traffic to the data pics
     */
    patroot                 intf_route_db; /**< interface route db */

    /* back pointers */
    jnx_gw_ctrl_vrf_t      *pvrf;
    jnx_gw_ctrl_data_pic_t *pdata_pic;
    jnx_gw_ctrl_policy_t   *pctrl;

    /* statistics */       
    uint32_t                ipip_tunnel_count;
    uint32_t                gre_gateway_count;
    uint32_t                gre_sesn_count;
    uint32_t                gre_active_sesn_count;
};

/* vrf structure */
struct jnx_gw_ctrl_vrf_s {
    patnode            vrf_node;       /**< add to the main control block */
    patnode            vrf_tnode;      /**< add to the receive thread */
    uint32_t           vrf_id;         /**< the key */
    int32_t            ctrl_fd;        /**< gateway socket fd */
    uint32_t           vrf_status;     /**< vrf status */
    uint32_t           vrf_sig_status; /**< vrf signaling status */
    uint32_t           vrf_rt_status;  /**< vrf rt status */
    uint32_t           vrf_rtid;       /**< vrf.inet0 table index */
    pthread_t          vrf_tid;
    uint8_t            vrf_name[JNX_GW_STR_SIZE];

    /* gre key management information */
    uint32_t           vrf_gre_key_start;
    uint32_t           vrf_gre_key_end;
    uint32_t           vrf_gre_key_cur;
    uint32_t           vrf_max_gre_sesn;

    /* data pic & tunnel information */
    uint32_t           gre_gw_count;
    uint32_t           ipip_gw_count;
    uint32_t           ctrl_policy_count;

    patroot            gre_sesn_db;    /* gre session db */
    patroot            gre_gw_db;      /* gre gateway db */
    patroot            ipip_gw_db;     /* ipip gaeway db */
    patroot            ctrl_policy_db; /* control policy db */
    patroot            vrf_intf_db;    /* vrf interface db */

    /* these are the socket resources for talking to the 
       client gateways */
    jnx_gw_ctrl_sock_list_t *vrf_socklist;
    jnx_gw_ctrl_rx_thread_t *vrf_rx_thread;
    uint32_t           gw_ip;         /* current gateway ip */
    uint16_t           gw_port;       /* current gateway port */
    pthread_mutex_t    vrf_send_lock; /* for send buffer */
    pthread_rwlock_t   gre_sesn_db_lock; /* lock for the gre sessions */
    struct sockaddr_in send_sock;
    uint16_t           send_len;
    uint8_t            send_buf[JNX_GW_CTRL_MAX_PKT_BUF_SIZE];

    /* statistics */
    uint32_t           gre_sesn_count;
    uint32_t           gre_active_sesn_count;
};

/* data pic structure */
struct jnx_gw_ctrl_data_pic_s {
    patnode          pic_node;   /* add to the cb structure */
    uint8_t          pic_name[JNX_GW_STR_SIZE]; /* key */
    uint32_t         peer_type;
    uint32_t         ifd_id;
    uint32_t         fpc_id;
    uint32_t         pic_id;
    uint8_t          pic_status;
    uint8_t          pic_flags;
    patroot          pic_intf_db; /* list of interfaces on this data pic */
    pconn_client_t  *pic_data_conn; /* for data conn */
    pthread_mutex_t  pic_send_lock;
    uint8_t          send_buf[JNX_GW_CTRL_MAX_PKT_BUF_SIZE];
    uint16_t         cur_len;

    /* statistics */ 
    uint32_t         intf_count;
    uint32_t         ipip_tunnel_count;
    uint32_t         gre_gateway_count;
    uint32_t         gre_sesn_count;
    uint32_t         gre_active_sesn_count;
};

typedef struct list_s list_t;

struct list_s {
    void   *ptr;
    list_t *next;
};

enum {
    JNX_GW_CTRL_BUF_IN_QUEUE=1,
    JNX_GW_CTRL_BUF_IN_FREE_LIST,
    JNX_GW_CTRL_BUF_IN_RX,
    JNX_GW_CTRL_BUF_IN_RECVFROM,
    JNX_GW_CTRL_BUF_IN_SENDTO,
    JNX_GW_CTRL_BUF_IN_PROC,
};
/* buffer structure */
struct jnx_gw_ctrl_buf_s {
    jnx_gw_ctrl_buf_t    *buf_next;
    jnx_gw_ctrl_vrf_t    *pvrf;
    uint32_t              src_addr;
    uint16_t              buf_flags; /* in queue/freelist/recvsocket*/
    uint16_t              buf_len;
    uint16_t              recv;
    uint16_t              src_port;
    uint8_t               buf_ptr[JNX_GW_CTRL_MAX_PKT_BUF_SIZE];
};

/* queues, for free & allocated queues also */
struct jnx_gw_ctrl_buf_list_s {
    uint32_t                 queue_thread_id;
    uint32_t                 queue_length;
    pthread_mutex_t          queue_lock;
    jnx_gw_ctrl_buf_t       *queue_head;
    jnx_gw_ctrl_buf_t       *queue_tail;
    jnx_gw_ctrl_buf_list_t  *queue_next;
};

struct jnx_gw_ctrl_sock_list_s {
    jnx_gw_ctrl_sock_list_t *next_socklist;
    patroot                  recv_vrf_db;
    uint32_t                 recv_fdcount;
    struct fd_set            recv_fdset;
    struct timeval           recv_tval;
    struct sockaddr_in       recv_sock;
};

struct jnx_gw_ctrl_rx_thread_s {
    jnx_gw_ctrl_rx_thread_t *rx_thread_next;
    pthread_t                rx_thread_id;
    pthread_mutex_t          rx_thread_list_mutex;
    list_t                  *rx_thread_vrf_add_list;
    list_t                  *rx_thread_vrf_del_list;
    list_t                  *rx_thread_free_list;
    uint32_t                 rx_thread_status;
    uint32_t                 rx_thread_vrf_count;
    list_t                  *rx_proc_thread_event_pending_list;
    jnx_gw_ctrl_sock_list_t *rx_thread_fdset_list;
};

struct jnx_gw_ctrl_proc_thread_s {
    jnx_gw_ctrl_proc_thread_t *proc_thread_next;
    pthread_t                  proc_thread_id;
    pthread_mutex_t            proc_event_mutex;
    pthread_cond_t             proc_pkt_event;
    uint32_t                   proc_thread_status;
    jnx_gw_ctrl_buf_list_t     proc_thread_rx;
};

/* control agent main control block structure */
typedef struct jnx_gw_ctrl_s {
    patroot           vrf_db;       /**< vrf list */
    patroot           pic_db;       /**< data pic list */
    patroot           user_db;      /**< user profile list */
    patroot           data_db;      /**< static gre policy list */
    patroot           ctrl_db;      /**< ctrl policy list */
    uint8_t           ifd_name[JNX_GW_STR_SIZE]; /* self ifd name */
    uint32_t          gre_sesn_id;  /**< for assigning a session id */
    uint16_t          message_id;   /**< message id for client conns */
    evContext         ctxt;         /**< event context */
    pthread_rwlock_t  config_lock;  /**< for config items */
    pconn_server_t   *mgmt_conn;    /**< for receiving opcmd/config msgs */
    pconn_session_t  *mgmt_sesn;    /**< for config/operational command */
    pconn_client_t   *periodic_conn;/**< for periodic responses */
    int32_t           ssd_fd;       /**< ssd connection fd */
    uint32_t          ssd_id;       /**< ssd client connection id */
    uint8_t           ctrl_status;  /**< control-mgmt connection status */
    uint8_t           rt_status;    /**< ssd connection status */
    uint8_t           opcmd_buf[JNX_GW_CTRL_MAX_PKT_BUF_SIZE];
    uint8_t           recv_buf[JNX_GW_CTRL_MAX_PKT_BUF_SIZE];
    uint32_t          min_prefix_len; /**< minimum prefix length */

    /* threads */
    jnx_gw_ctrl_rx_thread_t   *recv_threads;
    jnx_gw_ctrl_proc_thread_t *proc_threads;
    uint32_t                   recv_thread_count;
    uint32_t                   proc_thread_count;

    /* buffers */
    pthread_mutex_t     buf_pool_mutex; /* recv buffer pool mutex*/
    uint32_t            buf_pool_count; /* recv buffer pool count */
    jnx_gw_ctrl_buf_t  *buf_pool;       /* recv buffer pool */

    /* statistics */        
    uint32_t          ctrl_cpu_count;
    uint32_t          vrf_count;
    uint32_t          pic_count;
    uint32_t          user_count;
    uint32_t          data_count;
    uint32_t          ctrl_count;

    uint32_t          policy_match_fail;
    uint32_t          sesn_set_fail;

    uint32_t          ipip_tunnel_count;
    uint32_t          gre_gateway_count;
    uint32_t          gre_sesn_count;
    uint32_t          gre_active_sesn_count;

} jnx_gw_ctrl_t;

/* GRE gateway signaling message types */
enum {
    JNX_GW_CTRL_GRE_SESN_INIT_REQ = 5,
    JNX_GW_CTRL_GRE_SESN_TRANSMIT,
    JNX_GW_CTRL_GRE_SESN_END_REQ,
    JNX_GW_CTRL_GRE_SESN_DONE,
    JNX_GW_CTRL_GRE_SESN_ERR_REQ,
    JNX_GW_CTRL_GRE_SESN_ERR,
    JNX_GW_CTRL_GRE_SESN_HELLO
};


/* GRE gateway macro flags */
#define JNX_GW_CTRL_GRE_SIG_PORT_NUM      3000
#if 0
#define JNX_GW_CTRL_GRE_KEY_PRESENT       0x01
#define JNX_GW_CTRL_GRE_SEQ_PRESENT       0x02
#define JNX_GW_CTRL_GRE_CKSUM_PRESENT     0x04
#endif
#define JNX_GW_CTRL_GRE_KEY_START         0x0001
#define JNX_GW_CTRL_GRE_KEY_END           0xFFFF

/* gre user info structure */
typedef struct jnx_gw_ctrl_clnt_5t_info_s {
    uint8_t       err_code;
    uint8_t       msg_len;
    uint8_t       flags;
    uint8_t       proto;
    uint16_t      dst_port;
    uint16_t      src_port;
    uint32_t      src_ip;
    uint32_t      dst_ip;
} jnx_gw_ctrl_clnt_5t_info_t;

/* gre tunnel info structure */
typedef struct jnx_gw_ctrl_gre_tun_info_s {
    uint8_t       err_code;
    uint8_t       msg_len;
    uint8_t       flags;
    uint8_t       tun_type;
    uint32_t      gre_key;
    uint32_t      data_ip;
    uint32_t      gw_ip;
} jnx_gw_ctrl_gre_tun_info_t;

/* gre signaling message structure */
typedef struct jnx_gw_ctrl_gre_msg_s {
    uint8_t       msg_type;
    uint8_t       prof_type;
    uint8_t       err_code;
    uint8_t       flags;
    uint16_t      msg_len;
    uint16_t      resv;
    uint32_t      msg_id;
    union {
        jnx_gw_ctrl_clnt_5t_info_t user_5t;
    };
    jnx_gw_ctrl_gre_tun_info_t tun;
} jnx_gw_ctrl_gre_msg_t;


#define JNX_GW_CTRL_PERIODIC_SEC     10
#define JNX_GW_IFD_IDX(ifdm)         ((ifdm)->ifdev_index)

#define JNX_GW_IFL_IFD_IDX(iflm)     ((iflm)->ifl_devindex)
#define JNX_GW_IFL_IFL_IDX(iflm)     (*(uint32_t *)(&((iflm)->ifl_index)))
#define JNX_GW_IFL_VRF_IDX(iflm)     ((iflm)->ifl_subunit)

#define JNX_GW_IFF_IFD_IDX(ifdm)     ((iffm)->iff_devindex)
#define JNX_GW_IFF_VRF_IDX(ifdm)     ((iffm)->iff_subunit)

#define JNX_GW_CTRL_MAX_CONN_IDX     PCONN_MAX_CONN

enum {
    JNX_GW_CTRL_STATUS_INIT = 0,
    JNX_GW_CTRL_STATUS_UP,
    JNX_GW_CTRL_STATUS_DOWN,
    JNX_GW_CTRL_STATUS_FAIL,
    JNX_GW_CTRL_STATUS_DELETE,
    JNX_GW_CTRL_RT_TABLE_LOOKUP_PENDING,
    JNX_GW_CTRL_NH_ADD_PENDING,
    JNX_GW_CTRL_NH_DELETE_PENDING,
    JNX_GW_CTRL_RT_ADD_PENDING,
    JNX_GW_CTRL_RT_DELETE_PENDING
} jnx_gw_ctrl_status_type;


/* mutex lock macros */

#define JNX_GW_CTRL_CONFIG_WRITE_LOCK() \
    pthread_rwlock_wrlock(&jnx_gw_ctrl.config_lock)

#define JNX_GW_CTRL_CONFIG_WRITE_UNLOCK() \
    pthread_rwlock_unlock(&jnx_gw_ctrl.config_lock)

#define JNX_GW_CTRL_CONFIG_READ_LOCK() \
    pthread_rwlock_rdlock(&jnx_gw_ctrl.config_lock)

#define JNX_GW_CTRL_CONFIG_READ_UNLOCK() \
    pthread_rwlock_unlock(&jnx_gw_ctrl.config_lock)

#define JNX_GW_CTRL_BUF_POOL_LOCK()\
    pthread_mutex_lock(&jnx_gw_ctrl.buf_pool_mutex)

#define JNX_GW_CTRL_BUF_POOL_UNLOCK()\
    pthread_mutex_unlock(&jnx_gw_ctrl.buf_pool_mutex)

#define JNX_GW_CTRL_RX_LIST_LOCK(pthread)\
    pthread_mutex_lock(&(pthread)->rx_thread_list_mutex)

#define JNX_GW_CTRL_RX_LIST_UNLOCK(pthread)\
    pthread_mutex_unlock(&(pthread)->rx_thread_list_mutex)

#define JNX_GW_CTRL_PROC_RX_QUEUE_LOCK(pqueue)\
    pthread_mutex_lock(&(pqueue)->queue_lock)

#define JNX_GW_CTRL_PROC_RX_QUEUE_TRY_LOCK(pqueue)\
    pthread_mutex_trylock(&(pqueue)->queue_lock)

#define JNX_GW_CTRL_PROC_RX_QUEUE_UNLOCK(pqueue)\
    pthread_mutex_unlock(&(pqueue)->queue_lock)

#define JNX_GW_CTRL_PROC_EVENT_LOCK(pthread)\
    pthread_mutex_lock(&(pthread)->proc_event_mutex)

#define JNX_GW_CTRL_PROC_EVENT_TRY_LOCK(pthread)\
    pthread_mutex_trylock(&(pthread)->proc_event_mutex)

#define JNX_GW_CTRL_PROC_EVENT_UNLOCK(pthread)\
    pthread_mutex_unlock(&(pthread)->proc_event_mutex)

#define JNX_GW_CTRL_DATA_PIC_SEND_LOCK(pdata_pic) \
    pthread_mutex_lock(&((pdata_pic)->pic_send_lock))

#define JNX_GW_CTRL_DATA_PIC_SEND_UNLOCK(pdata_pic) \
    pthread_mutex_unlock(&((pdata_pic)->pic_send_lock))

#define JNX_GW_CTRL_VRF_SEND_LOCK(pvrf) \
    pthread_mutex_lock(&((pvrf)->vrf_send_lock))

#define JNX_GW_CTRL_VRF_SEND_UNLOCK(pvrf) \
    pthread_mutex_unlock(&((pvrf)->vrf_send_lock))

/* VRF GRE SESSION DB LOCKS */
/* GRE GATEWAY GRE SESSION DB LOCKS */
/* IPIP GATEWAY GRE SESSION DB LOCKS */
/* USER POLICY GRE SESSION DB LOCKS */
#define JNX_GW_CTRL_SESN_DB_RW_LOCK_INIT(_x)\
    pthread_rwlock_init(&(_x)->gre_sesn_db_lock, 0)

#define JNX_GW_CTRL_SESN_DB_RW_LOCK_DELETE(_x)\
    pthread_rwlock_destroy(&(_x)->gre_sesn_db_lock)

#define JNX_GW_CTRL_SESN_DB_WRITE_LOCK(_x)\
    pthread_rwlock_wrlock(&((_x)->gre_sesn_db_lock))

#define JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(_x)\
    pthread_rwlock_unlock(&((_x)->gre_sesn_db_lock))

#define JNX_GW_CTRL_SESN_DB_READ_LOCK(_x)\
    pthread_rwlock_rdlock(&((_x)->gre_sesn_db_lock))

#define JNX_GW_CTRL_SESN_DB_READ_UNLOCK(_x)\
    pthread_rwlock_unlock(&((_x)->gre_sesn_db_lock))

#define JNX_GW_CTRL_PROC_EVENT(proc_thread)\
    ({ pthread_cond_wait(&proc_thread->proc_pkt_event,\
                         &proc_thread->proc_event_mutex); })

#define JNX_GW_CTRL_PROC_SIG_EVENT(proc_thread)\
    ({pthread_cond_signal(&proc_thread->proc_pkt_event); })

extern jnx_gw_ctrl_t jnx_gw_ctrl;

extern status_t
jnx_gw_ctrl_init_list(jnx_gw_ctrl_rx_thread_t * prx_thread);

extern status_t jnx_gw_ctrl_init_buf(void);
extern status_t
jnx_gw_ctrl_mgmt_msg_handler(pconn_session_t * session,
                             ipc_msg_t *  ipc_msg,
                             void * cookie __unused);
extern void
jnx_gw_ctrl_mgmt_event_handler(pconn_session_t * session,
                               pconn_event_t event,
                               void * cookie __unused);
extern void
jnx_gw_ctrl_data_event_handler(pconn_client_t * pclient,
                               pconn_event_t event,
                               void * cookie);
extern status_t
jnx_gw_ctrl_data_msg_handler(pconn_client_t * pclient,
                             ipc_msg_t * ipc_msg, void * cookie);


extern uint32_t jnx_gw_ctrl_get_pic_id(char * pic_name);
extern void jnx_gw_ctrl_cleanup(int);

extern void * jnx_gw_ctrl_rx_msg_thread (void * pthread);
extern void * jnx_gw_ctrl_proc_msg_thread (void * pthread);

extern status_t jnx_gw_ctrl_clear_data_pics(void);
extern status_t jnx_gw_ctrl_clear_vrfs(void);
extern status_t jnx_gw_ctrl_init_mgmt(void);
extern status_t jnx_gw_ctrl_destroy_mgmt(void);
extern status_t jnx_gw_ctrl_kcom_init(evContext ctxt);
extern jnx_gw_ctrl_intf_t *
jnx_gw_ctrl_get_next_intf(jnx_gw_ctrl_vrf_t * pvrf,
                          jnx_gw_ctrl_data_pic_t * pdata_pic,
                          jnx_gw_ctrl_policy_t * pctrl,
                          jnx_gw_ctrl_intf_t * pintf);
extern status_t
jnx_gw_ctrl_delete_intf(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_data_pic_t * pdata_pic,
                        jnx_gw_ctrl_intf_t * pintf);
extern status_t 
jnx_gw_ctrl_send_ipip_msg_to_data_pic(jnx_gw_ctrl_vrf_t * pvrf,
                                      jnx_gw_ctrl_data_pic_t * pdata_pic,
                                      jnx_gw_ctrl_intf_t * pintf,
                                      uint8_t add_flag);
jnx_gw_ctrl_policy_t *
jnx_gw_ctrl_add_intf_to_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                             jnx_gw_ctrl_data_pic_t * pdata_pic,
                             uint32_t * intf_ip);
jnx_gw_ctrl_intf_t *
jnx_gw_ctrl_add_intf(jnx_gw_ctrl_vrf_t * pvrf,
                     jnx_gw_ctrl_data_pic_t * pdata_pic,
                     jnx_gw_ctrl_intf_t * pintf,
                     jnx_gw_ctrl_policy_t * pctrl,
                     uint8_t *intf_name, uint16_t intf_id, 
                     uint32_t intf_subunit, uint32_t intf_ip);
extern status_t
jnx_gw_ctrl_attach_to_thread(jnx_gw_ctrl_vrf_t * pvrf);

extern status_t
jnx_gw_ctrl_detach_from_thread(jnx_gw_ctrl_vrf_t * pvrf);

extern status_t 
jnx_gw_ctrl_fill_data_pic_msg(uint8_t msg_type, uint8_t add_flag,
                              jnx_gw_ctrl_data_pic_t * pdata_pic,
                              void * sesn);
extern status_t
jnx_gw_ctrl_fill_data_pic_msgs(uint8_t msg_type, uint8_t add_flag,
                               void * pdata);

extern status_t 
jnx_gw_ctrl_select_data_pic(jnx_gw_ctrl_gre_session_t * pgre_sesn);

extern jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_lookup_vrf(uint32_t vrf_id);

extern jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_get_next_vrf(jnx_gw_ctrl_vrf_t * pvrf);

extern jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_thread_get_first_vrf(jnx_gw_ctrl_sock_list_t * psocklist);

extern jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_thread_get_next_vrf(jnx_gw_ctrl_sock_list_t * psocklist,
                                jnx_gw_ctrl_vrf_t * pvrf);

extern jnx_gw_ctrl_vrf_t * jnx_gw_ctrl_add_vrf(uint32_t vrf_id,
 uint8_t * vrf_name);

extern status_t jnx_gw_ctrl_delete_vrf(jnx_gw_ctrl_vrf_t * pvrf);

extern jnx_gw_ctrl_intf_t *
jnx_gw_ctrl_lookup_intf(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_data_pic_t * pdata_pic,
                        jnx_gw_ctrl_policy_t * pctrl,
                        uint16_t intf_id);

extern jnx_gw_ctrl_data_pic_t *
jnx_gw_ctrl_lookup_data_pic(uint8_t * pic_name, pconn_client_t * pclient);

extern jnx_gw_ctrl_data_pic_t *
jnx_gw_ctrl_get_next_data_pic(jnx_gw_ctrl_data_pic_t * pdata_pic);

extern jnx_gw_ctrl_data_pic_t *
jnx_gw_ctrl_add_data_pic(jnx_gw_msg_ctrl_config_pic_t * pmsg);

extern status_t
jnx_gw_ctrl_delete_data_pic(jnx_gw_ctrl_data_pic_t * pdata_pic);

extern jnx_gw_ctrl_user_t *
jnx_gw_ctrl_lookup_user(uint8_t * user_name);

extern jnx_gw_ctrl_user_t *
jnx_gw_ctrl_get_next_user(jnx_gw_ctrl_user_t * puser);

extern status_t 
jnx_gw_ctrl_match_user(jnx_gw_ctrl_gre_session_t * pgre_sesn);

extern status_t
jnx_gw_ctrl_modify_user(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_user_t * puser,
                        jnx_gw_ctrl_user_t * pconfig_msg);

extern jnx_gw_ctrl_user_t * 
jnx_gw_ctrl_add_user(jnx_gw_ctrl_vrf_t * pvrf,
                     jnx_gw_ctrl_ipip_gw_t * pipip_gw,
                     jnx_gw_ctrl_user_t * pconfig_msg);

extern status_t
jnx_gw_ctrl_delete_user(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_user_t * puser);

extern jnx_gw_ctrl_ipip_gw_t *
jnx_gw_ctrl_lookup_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf, uint32_t gateway_ip);

extern jnx_gw_ctrl_ipip_gw_t * 
jnx_gw_ctrl_get_next_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf,
                             jnx_gw_ctrl_ipip_gw_t *pipip_gw);

extern jnx_gw_ctrl_ipip_gw_t *
jnx_gw_ctrl_add_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf, uint32_t ipip_gw_ip);

status_t
jnx_gw_ctrl_delete_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf,
                           jnx_gw_ctrl_ipip_gw_t  * pipip_gw);

extern jnx_gw_ctrl_policy_t * 
jnx_gw_ctrl_lookup_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                        uint32_t addr, uint32_t mask);

extern jnx_gw_ctrl_policy_t *
jnx_gw_ctrl_modify_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_policy_t * pctrl, 
                        jnx_gw_ctrl_policy_t * pconfig_msg);

extern status_t
jnx_gw_ctrl_delete_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_policy_t * pctrl);

extern jnx_gw_ctrl_policy_t *
jnx_gw_ctrl_add_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                     jnx_gw_ctrl_policy_t *pconfig_msg);

extern jnx_gw_ctrl_gre_gw_t * 
jnx_gw_ctrl_get_next_gre_gw(jnx_gw_ctrl_vrf_t * pvrf,
                            jnx_gw_ctrl_gre_gw_t * pgre_gw);

extern jnx_gw_ctrl_gre_gw_t * 
jnx_gw_ctrl_lookup_gre_gw(jnx_gw_ctrl_vrf_t * pvrf, uint32_t gre_gw_ip);

extern jnx_gw_ctrl_gre_gw_t * 
jnx_gw_ctrl_add_gre_gw(jnx_gw_ctrl_vrf_t * pvrf, uint32_t gre_gw_ip,
                       uint16_t gre_gw_port);

extern status_t
jnx_gw_ctrl_delete_gre_gw(jnx_gw_ctrl_vrf_t * pvrf,
                          jnx_gw_ctrl_gre_gw_t * pgre_gw);

extern jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_lookup_gre_session(jnx_gw_ctrl_vrf_t * pvrf,
                               jnx_gw_ctrl_gre_gw_t * pgre_gw,
                               uint32_t gre_key, int32_t lock);

extern jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_get_next_gre_session(jnx_gw_ctrl_vrf_t * pvrf,
                                 jnx_gw_ctrl_gre_gw_t * pgre_gw,
                                 jnx_gw_ctrl_ipip_gw_t * pipip_gw,
                                 jnx_gw_ctrl_user_t   * puser,
                                 jnx_gw_ctrl_gre_session_t * pgre_sesn, 
                                 uint32_t lock);


extern jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_get_gw_gre_session(jnx_gw_ctrl_vrf_t * pvrf __unused,
                               jnx_gw_ctrl_gre_gw_t * pgre_gw,
                               jnx_gw_ctrl_session_info_t * psesn);

extern jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_get_ipip_gre_session(jnx_gw_ctrl_ipip_gw_t * pipip_gw,
                                 jnx_gw_ctrl_session_info_t * psesn);

extern jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_lookup_user_gre_session(jnx_gw_ctrl_user_t * puser,
                                    jnx_gw_ctrl_session_info_t * psesn);
extern jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_add_gre_session(jnx_gw_ctrl_vrf_t * pvrf,
                            jnx_gw_ctrl_gre_gw_t * pgre_gw,
                            jnx_gw_ctrl_gre_session_t * pgre_config);

extern status_t 
jnx_gw_ctrl_delete_gre_session(jnx_gw_ctrl_vrf_t * pvrf,
                            jnx_gw_ctrl_gre_gw_t * pgre_gw,
                            jnx_gw_ctrl_gre_session_t * pgre_sesn);

extern status_t 
jnx_gw_ctrl_send_data_pic_msg(jnx_gw_ctrl_data_pic_t * pdata_pic);

extern status_t 
jnx_gw_ctrl_fill_gw_gre_msg(uint8_t msg_type, jnx_gw_ctrl_vrf_t * pvrf,
                            jnx_gw_ctrl_gre_gw_t * pgre_gw,
                            jnx_gw_ctrl_gre_session_t * pgre_sesn);
extern status_t jnx_gw_ctrl_send_gw_gre_msgs(void);
extern status_t jnx_gw_ctrl_send_data_pic_msgs(void);

extern int jnx_gw_ctrl_ssd_connect_handler(int fd);
extern void jnx_gw_ctrl_ssd_close_handler(int fd, int cause);
extern void jnx_gw_ctrl_ssd_msg_handler(int fd, struct ssd_ipc_msg * msg);

int
jnx_gw_ctrl_route_add(jnx_gw_ctrl_vrf_t * pvrf,
                      jnx_gw_ctrl_data_pic_t * pdata_pic,
                      jnx_gw_ctrl_policy_t * pctrl,
                      jnx_gw_ctrl_intf_t * pintf);

int
jnx_gw_ctrl_route_delete(jnx_gw_ctrl_vrf_t * pvrf,
                         jnx_gw_ctrl_data_pic_t * pdata_pic,
                         jnx_gw_ctrl_policy_t * pctrl,
                         jnx_gw_ctrl_intf_t * pintf);


extern trace_file_t * jnx_gw_trace_file;

status_t
jnx_gw_ctrl_add_nexthop(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_intf_t * pintf);
jnx_gw_ctrl_intf_t *
jnx_gw_ctrl_get_first_intf(jnx_gw_ctrl_vrf_t * pvrf,
                           jnx_gw_ctrl_data_pic_t * pdata_pic,
                           jnx_gw_ctrl_policy_t * pctrl);

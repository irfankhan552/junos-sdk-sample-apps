/*
 * $Id$
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2010, Juniper Networks, Inc.
 * All rights reserved.
 */
 
/**
 * @file route-manager.h
 * 
 * @brief Contains includes and definitions and prototypes
 * 
 */

#ifndef __ROUTE_MANAGER_H__
#define __ROUTE_MANAGER_H__

/** The maximum length of name string. */
#define RM_NAME_SIZE                64

/** The maximum number of next-hops. */
#define RM_NH_MAX                   64

/** The blob ID for SSD client ID. */
#define RM_BLOB_ID_CLIENT_ID        1

/** Default route preference count. */
#define RM_RT_PREFERENCE_DEFAULT    3

/** The route state */
typedef enum {
    RT_STATE_ADD_PENDING = 1,
    RT_STATE_ADD_OK,
    RT_STATE_ADD_ERR,
    RT_STATE_DEL_PENDING,
    RT_STATE_DEL_OK,
    RT_STATE_DEL_ERR
} route_op_state_e;

/** The next-hop state */
typedef enum {
    NH_STATE_FREE = 0,
    NH_STATE_ADD_PENDING,
    NH_STATE_ADD_OK,
    NH_STATE_ADD_ERR,
    NH_STATE_DEL_PENDING,
    NH_STATE_DEL_OK,
    NH_STATE_DEL_ERR
} nh_op_state_e;

/** The macro to log tracing message. */
#define RM_TRACE(_msg_type, _fmt, ...) \
    junos_trace((_msg_type), _fmt, ##__VA_ARGS__)

/** The macro to log syslog message. */
#define RM_LOG(_level, _fmt, ...)   \
        ERRMSG(ROUTE_MANAGER, (_level), _fmt, ##__VA_ARGS__)

/** Route */
typedef struct route_s {
    LIST_ENTRY(route_s)     entry;      /**< list entry */
    ssd_sockaddr_un         dst_addr;   /**< destination address */
    uint16_t                prefix_len; /**< prefix length */
    int                     rtt_id;     /**< routing table ID */
    uint32_t                preference; /**< route preference */
    uint16_t                state;      /**< route state */
    uint16_t                flag;       /**< route flag */
    uint8_t                 nh_type;    /**< next-hop type */
    uint8_t                 gw_num;     /**< number of gateways */
    ssd_sockaddr_un         gw_addr[SSD_ROUTE_N_MULTIPATH];
                                        /**< gateway address */
    ssd_sockaddr_un         gw_ifl_addr[SSD_ROUTE_N_MULTIPATH];
                                        /**< gateway IFL address */
    char                    gw_ifl_name[SSD_ROUTE_N_MULTIPATH][RM_NAME_SIZE];
                                        /**< gateway IFL name */
    int                     gw_ifl_idx; /**< gateway IFL index */
    int                     nh_id[SSD_ROUTE_N_MULTIPATH];
                                        /**< next-hop ID */
    int                     ctx_id;     /**< context ID */
    route_op_state_e        op_state;   /**< operation state */
} route_t;

/** Next-hop */
typedef struct {
    char              name[RM_NAME_SIZE];  /**< IFL or routing table name */
    int               idx;        /**< next-hop index or routing table ID */
    int               id;         /**< client next-hop ID */
    int               count;      /**< reference count */
    nh_op_state_e     op_state;   /**< state */
} nh_t;

int config_init(void);
void config_clear(void);
int config_read(int check);
void config_rt_proc(route_t *rt);
int config_rt_add(route_t *rt);
int config_rt_del(route_t *rt);
int kcom_init(evContext ev_ctx);
void kcom_close(void);
int kcom_client_id_save(int id);
int kcom_client_id_restore(void);
int kcom_ifl_get_idx_by_name(char *name);
int ssd_open(evContext ev_ctx);
void ssd_close(void);
int ssd_nh_del(route_t *rt, int nh_id);
int ssd_nh_add(route_t *rt, int nh_id);
int ssd_rt_add(route_t *rt);
int ssd_rt_del(route_t *rt);

#endif


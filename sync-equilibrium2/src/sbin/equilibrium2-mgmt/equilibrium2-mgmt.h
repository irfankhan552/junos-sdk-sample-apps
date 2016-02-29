/*
 * $Id: equilibrium2-mgmt.h 366969 2010-03-09 15:30:13Z taoliu $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */
 
/**
 * @file equilibrium2-mgmt.h
 * 
 * @brief Contains includes and definitions and prototypes
 * 
 */

#ifndef __EQUILIBRIUM2_MGMT_H__
#define __EQUILIBRIUM2_MGMT_H__

#include <netinet/in.h>
#include <jnx/ipc_types.h>
#include <jnx/aux_types.h>
#include <jnx/junos_kcom_pub_blob.h>
#include <jnx/pconn.h>

/** Constants **/

/** Constant string for the daemon name. */
#define DNAME_EQUILIBRIUM2_MGMT    "equilibrium2-mgmt"

/** The heartbeat interval. */
#define PMON_HB_INTERVAL   30

/** The macro to log tracing message. */
#define EQ2_TRACE(_msg_type, _fmt, ...) \
    junos_trace((_msg_type), _fmt, ##__VA_ARGS__)

/** The macro to log syslog message. */
#define EQ2_LOG(_level, _fmt, ...)   \
        ERRMSG(EQ2_MGMT, (_level), _fmt, ##__VA_ARGS__)

/** Config SSRB operation commands. */
typedef enum config_ssrb_op_e {
    CONFIG_SSRB_ADD = 1,  /**< add SSRB */
    CONFIG_SSRB_CHANGE,   /**< update SSRB */
    CONFIG_SSRB_DEL       /**< delete SSRB */
} config_ssrb_op_t;

/** Config SSRB state. */
typedef enum ssrb_state_e {
    SSRB_IDLE = 0,
    SSRB_PENDING,
    SSRB_UPDATED
} ssrb_state_t;

/** List item of service gate. */
typedef struct svc_gate_s {
    LIST_ENTRY(svc_gate_s) entry;                   /**< list entry */
    char                   gate_name[MAX_NAME_LEN]; /**< service gate name */
    in_addr_t              gate_addr;               /**< service gate address */
} svc_gate_t;

/** List head of service gate. */
typedef LIST_HEAD(svc_gate_head_s, svc_gate_s) svc_gate_head_t;

/** List item of service type. */
typedef struct svc_type_s {
    LIST_ENTRY(svc_type_s) entry;                   /**< list entry */
    char                   type_name[MAX_NAME_LEN]; /**< service type name */
    in_port_t              type_port;               /**< service port number */
} svc_type_t;

/** List head of service type. */
typedef LIST_HEAD(svc_type_head_s, svc_type_s) svc_type_head_t;

/** List item of Equilibrium II service term. */
typedef struct svc_term_s {
    LIST_ENTRY(svc_term_s)  entry;                        /**< list entry */
    char                    term_name[MAX_NAME_LEN];      /**< term name */
    int                     term_match;                   /**< match ID */
    char                    term_match_val[MAX_NAME_LEN]; /**< match value */
    int                     term_act;                     /**< action ID */
    char                    term_act_val[MAX_NAME_LEN];   /**< action value */
} svc_term_t;

/** List head of Equilibrium II service term. */
typedef LIST_HEAD(svc_term_head_s, svc_term_s) svc_term_head_t;

/** List item of Equilibrium II service rule. */
typedef struct svc_rule_s {
    LIST_ENTRY(svc_rule_s) entry;                   /**< list entry */
    char                   rule_name[MAX_NAME_LEN]; /**< rule name */
    svc_term_head_t        rule_term_head;          /**< head of term list */
    int                    rule_term_count;         /**< number of terms */
} svc_rule_t;

/** List head of Equilibrium II service rule. */
typedef LIST_HEAD(svc_rule_head_s, svc_rule_s) svc_rule_head_t;

/** List item of service-set rule. */
typedef struct svc_set_rule_s {
    LIST_ENTRY(svc_set_rule_s) entry;                   /**< list entry */
    char                       rule_name[MAX_NAME_LEN]; /**< rule name */
} svc_set_rule_t;

/** List head of service-set rule. */
typedef LIST_HEAD(svc_set_rule_head_s, svc_set_rule_s) svc_set_rule_head_t;

/** List itme of service-set. */
typedef struct svc_set_s {
    LIST_ENTRY(svc_set_s) entry;              /**< list entry */
    char           ss_name[MAX_NAME_LEN];     /**< service set name */
    char           ss_if_name[MAX_NAME_LEN];  /**< service interface name */
    uint16_t       ss_id;                     /**< service set id */
    uint8_t        ss_eq2_svc_id;             /**< Equilibrium II service ID */
    svc_set_rule_head_t  ss_rule_head;        /**< list head of rules */
    int            ss_rule_count;             /**< number of rules */
} svc_set_t;

/** List head of service-set. */
typedef LIST_HEAD(svc_set_head_s, svc_set_s) svc_set_head_t;

/** List item of service-set service interface. */
typedef struct svc_if_s {
    LIST_ENTRY(svc_if_s) entry;     /**< list entry */
    char                 *if_name;  /**< pointer to interface name */
} svc_if_t;

/** List head of service-set service interface. */
typedef LIST_HEAD(svc_if_head_s, svc_if_s) svc_if_head_t;

/** List item of SSRB. */
typedef struct ssrb_node_s {
    LIST_ENTRY(ssrb_node_s) entry;       /**< entry node */
    junos_kcom_pub_ssrb_t   ssrb;        /**< SSRB */
    ssrb_state_t            ssrb_state;  /**< SSRB state */
} ssrb_node_t;

/** List head of SSRB. */
typedef LIST_HEAD(ssrb_head_s, ssrb_node_s) ssrb_head_t;

/** List item of client. */
typedef struct client_s {
    LIST_ENTRY(client_s) entry;     /**< list entry */
    pconn_session_t      *session;  /**< client session */
    pconn_peer_info_t    info;      /**< client info */
    char                 *msg;      /**< client message */
} client_t;

/** List head of client. */
typedef LIST_HEAD(client_head_s, client_s) client_head_t;


provider_origin_id_t  eq2_origin_id;   /**< original ID */
provider_id_t         eq2_provider_id; /**< provider ID */
svc_if_head_t         svc_if_head;     /**< list head of service interface */
int                   svc_if_count;    /**< number of service interfaces */

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * @brief
 * Init KCOM library and register handlers for asynchronous KCOM messages
 * 
 * @param[in] ctx
 *     Newly created event context 
 * 
 * @return
 *      KCOM_OK (0) on success, or -1 on error.
 */
int kcom_init(evContext ctx);

/**
 * @brief
 * Shutdown KCOM library and register handlers for asynchronous KCOM messages
 */
void kcom_close(void);

/**
 * @brief
 * Get all configuration blobs.
 *
 * @return
 *      0 on success, -1 on failure
 */
int kcom_get_config_blob(void);

/**
 * @brief
 * Add a configuration blob.
 *
 * @param[in] key
 *      Pointer to the blob key
 *
 * @param[in] blob
 *      Pointer to the blob
 *
 * @return
 *      0 on success, -1 on failure
 */
int kcom_add_config_blob(config_blob_key_t *key, void *blob);

/**
 * @brief
 * Delete a configuration blob.
 *
 * @param[in] key
 *      Pointer to the blob key
 *
 * @return
 *      0 on success, -1 on failure
 */
int kcom_del_config_blob(config_blob_key_t *key);

/**
 * @brief
 * Open the manager server.
 * 
 * @param[in] ctx
 *     Event context 
 *
 * @return 
 *      0 on success; -1 on failure
 */
int server_open(evContext ctx);

/**
 * @brief
 * Close existing connections and shutdown server
 */
void server_close(void);

/**
 * @brief
 * Init the data structure that will store configuration info,
 * or in other words, the condition(s)
 */
void config_init(void);

/**
 * @brief
 * Clear the (non-SSRB) configuration info
 */
void config_clear(void);

/**
 * @brief
 * Clear the SSRB configuration info
 */
void clear_ssrb_config(void);

/**
 * @brief
 * Add/delete/update SSRB in the configuration.
 * 
 * @param[in] ssrb
 *      The SSRB data
 *
 * @param[in] op
 *      SSRB operation
 * 
 * @return
 *      0 on success, -1 on failure
 */
int config_ssrb_op(junos_kcom_pub_ssrb_t *ssrb, config_ssrb_op_t op);

/**
 * @brief
 * Read daemon configuration from the database.
 * (nothing to do except traceoptions)
 * 
 * @param[in] check
 *     1 if this function being invoked because of a commit check
 * 
 * @return SUCCESS (0) successfully loaded, EFAIL if not
 * 
 * @note Do not use ERRMSG/LOG during config check normally.
 */
int eq2_config_read(int check);

/**
 * @brief
 * Process server group blob.
 *
 * @param[in] key
 *      Pointer to the configuration blob key
 *
 * @param[in] blob
 *      Pointer to the blob
 */
void config_svr_group_blob_proc(config_blob_key_t *key, void *blob);

/**
 * @brief
 * Process service-set config blob.
 *
 * @param[in] key
 *      Pointer to the configuration blob key
 *
 * @param[in] blob
 *      Pointer to the blob
 */
void config_svc_set_blob_proc(config_blob_key_t *key, void *blob);

/**
 * @brief
 * Get the next client.
 *
 * @param[in] client
 *      Client
 *
 * @return
 *      Pointer to the next client on success, NULL on failure
 */
client_t *client_get_next(client_t *client);

#endif


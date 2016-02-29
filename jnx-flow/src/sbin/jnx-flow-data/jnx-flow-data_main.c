/*
 * $Id: jnx-flow-data_main.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-flow-data_main.c - initialization & signal handler routines
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
 *@file jnx-flow-data_main.c
 *@brief This file contains the initialization and signal handler
 * routines for the jnx-flow monitoring data application running
 * on the ms-pic.
 * 
 * The jnx-flow data agent initialization consists of the following:-
 * 1. jnx-flow data agent control block initialization
 * 2. Flow Table Initialization
 * 3. Policy Table Initialization
 * 4. Data Packet processing thread launch
 * 5. RE Management Agent connection Initialization
 *    a. server socket for listening to op-commands & configration updates
 *    b. client socket for registering with the RE management agent
 * 6. Setting up Periodic Timer Event for clearing up stale flows
 *
 * The jnx-flow signal handler routines are meant for updating/clearing-up.
 */


#include "jnx-flow-data.h"

const char * jnx_flow_err_str[] = {"no error", "alloc fail", 
    "free fail", "entry op fail", "invalid entry", "entry present", 
    "entry absent", "invalid message", "invalid configration"};
const char * jnx_flow_svc_str[] = {"invalid", "interface", "nexthop" };
const char * jnx_flow_match_str[] = {"invalid", "5tuple"};
const char * jnx_flow_action_str[] = {"invalid", "allow", "drop"};
const char * jnx_flow_dir_str[] = {"invalid", "input", "output"};
const char * jnx_flow_mesg_str[] = {"invalid", "service config",
    "rule config", "service rule config", "fetch flow", "fetch rule",
    "fetch service", "clear"};
const char * jnx_flow_config_op_str[] = {"invalid", "add", "delete", "change"};
const char * jnx_flow_fetch_op_str[] = {"invalid", "entry", "summary", 
    "extensive"};
const char * jnx_flow_clear_op_str[] = {"invalid", "all", "entry",
    "rule", "service", "service type" };

jnx_flow_data_cb_t jnx_flow_data_cb;


static void*
jnx_flow_data_flow_aging_proc(void* arg)
{
    jnx_flow_data_svc_set_t    * psvc_set = NULL;
    jnx_flow_data_rule_entry_t * prule = NULL;
    jnx_flow_data_flow_entry_t * pflow = NULL, * prev = NULL, * pnext = NULL;
    jnx_flow_rule_action_type_t  flow_action;
    uint32_t                    hash;
    jnx_flow_data_cb_t*          data_cb = (jnx_flow_data_cb_t*)arg; 

    /*
     * application is still not initialized, wait
     */
    while (data_cb->cb_status == JNX_FLOW_DATA_STATUS_INIT) {
        sleep(1);
    }

    while(1) {

        if (data_cb->cb_status == JNX_FLOW_DATA_STATUS_SHUTDOWN) {
            break;
        }

        msp_objcache_reclaim(data_cb->shm_handle);

        /*
         * for each hash bucket of the flow table
         */
        for (hash = 0; (hash < JNX_FLOW_DATA_FLOW_BUCKET_COUNT); hash++)  {

            /*
             * acquire the hash bucket lock
             */

            pflow = JNX_FLOW_DATA_HASH_CHAIN(data_cb, hash);

            if (pflow == NULL)
                continue;

            JNX_FLOW_DATA_HASH_LOCK(data_cb->cb_flow_db, hash);

            /*
             * For each flow table entry, in the hash bucket
             */
            prev = NULL;
            pflow = JNX_FLOW_DATA_HASH_CHAIN(data_cb, hash);

            JNX_FLOW_DATA_HASH_UNLOCK(data_cb->cb_flow_db, hash);

            for (;(pflow); prev = pflow, pflow = pnext) {

                /* 
                 * cache the next entry 
                 */
                pnext = pflow->flow_next;

                /* 
                 * if the flow entry is marked INIT or, (UP and active)
                 * continue
                 */

                if (pflow->flow_status == JNX_FLOW_DATA_STATUS_INIT) {
                    continue;
                }

                if ((pflow->flow_status == JNX_FLOW_DATA_STATUS_UP) &&
                    ((data_cb->cb_periodic_ts - pflow->flow_ts) <
                     JNX_FLOW_DATA_FLOW_EXPIRY_TIME_SEC)) {
                    continue;
                }

                /*
                 * Check for the reverse flow, remove the entry only
                 * if both the forward & reverse flows have timed out.
                 */

                if (pflow->flow_entry) {
                    
                    if (((data_cb->cb_periodic_ts - pflow->flow_ts) >= 
                          JNX_FLOW_DATA_FLOW_EXPIRY_TIME_SEC) &&
                        ((data_cb->cb_periodic_ts - pflow->flow_entry->flow_ts) <
                         JNX_FLOW_DATA_FLOW_EXPIRY_TIME_SEC)) {

                        pflow->flow_entry->flow_ts = pflow->flow_ts;
                        continue;
                    }
                }

                /*
                 * These are failed flow setup entries 
                 * Or, the expired entries
                 */

                /*
                 * Remove the flow entry from the hash bucket
                 */
                JNX_FLOW_DATA_HASH_LOCK(data_cb->cb_flow_db, hash);

                if (prev) {
                    prev->flow_next = pnext;
                } else {
                    JNX_FLOW_DATA_HASH_CHAIN(data_cb, hash) = pnext;
                }

                if (pnext == NULL) {
                    JNX_FLOW_DATA_HASH_LAST(data_cb, hash) = prev;
                }

                JNX_FLOW_DATA_HASH_UNLOCK(data_cb->cb_flow_db, hash);

                /* Acquire lock on the flow Entry now */

                /* 
                 * Get the flow action
                 */
                flow_action = pflow->flow_action;

                /*
                 * mark the reverse flow entry as down
                 * reset the flow entry pointer in the reverse flow
                 */
                if (pflow->flow_entry) {
                    if (pflow->flow_entry->flow_status == JNX_FLOW_DATA_STATUS_UP) {
                        pflow->flow_entry->flow_status = JNX_FLOW_DATA_STATUS_DOWN;
                    }
                    pflow->flow_entry->flow_entry = NULL;
                }

                /*
                 * If not marked as delete, update the statistics
                 */

                if (pflow->flow_status != JNX_FLOW_DATA_STATUS_DELETE) {
                    /*
                     * Get the rule entry
                     */
                    prule = jnx_flow_data_rule_lookup(data_cb,
                                                      pflow->flow_rule_id);

                    /*
                     * Get the service set entry
                     */
                    psvc_set = jnx_flow_data_svc_set_id_lookup(data_cb,
                                                               pflow->flow_svc_id);

                    /*
                     * update the rule statistics
                     */
                    if (prule) {
                        jnx_flow_log(LOG_INFO, "%s:%d:\"%s\" %d",
                                     __func__, __LINE__, 
                                     prule->rule_name,
                                     prule->rule_stats.rule_active_flow_count);
                    }

                    if (psvc_set) {
                        jnx_flow_log(LOG_INFO, "%s:%d:\"%s\" %d",
                                     __func__, __LINE__, 
                                     psvc_set->svc_name,
                                     psvc_set->svc_stats.active_flow_count);
                    }

                    if (prule) {
                        atomic_sub_uint(1, &prule->rule_stats.
                                        rule_active_flow_count);
                    }
                    /*
                     * Update the service set statistics 
                     */
                    if (psvc_set) {

                        atomic_sub_uint(1, &psvc_set->svc_stats.
                                       active_flow_count);

                        if (flow_action == JNX_FLOW_RULE_ACTION_DROP) {
                            atomic_sub_uint(1, &psvc_set->svc_stats.
                                           active_drop_flow_count);
                        } else {
                            atomic_sub_uint(1, &psvc_set->svc_stats.
                                           active_allow_flow_count);
                        }
                    }

                    /*
                     * Update the control block statistics 
                     */

                    atomic_sub_uint(1, &data_cb->cb_stats.active_flow_count);

                    if (flow_action == JNX_FLOW_RULE_ACTION_DROP) {
                        atomic_sub_uint(1, &data_cb->cb_stats.
                                       active_drop_flow_count);
                    } else {
                        atomic_sub_uint(1, &data_cb->cb_stats.
                                       active_allow_flow_count);
                    }
                }

                /*
                 * Free the flow entry
                 */
                FLOW_FREE(data_cb->cb_flow_oc, pflow, data_cb->user_cpu[0]);

                /* 
                 * Pick the previous entry as current 
                 */
                pflow = prev;
            }

        }
    }

    return NULL;
}

static status_t
jnx_flow_data_register_with_mgmt(jnx_flow_data_cb_t * data_cb);

/**
 * This function wakes up periodically to clean up the flow
 * table inactive/failed entries 
 * @param  ctxt   event context pointner
 * @param  uap    application cookie (data control block pointer)
 * @param  due    time spec
 * @param  inter  time spec
 * @returns
 *      NONE
 */
static void 
jnx_flow_data_periodic_event_handler(evContext ctxt __unused, void * uap,
                                     struct timespec due __unused,
                                     struct timespec inter __unused)
{
    jnx_flow_data_cb_t         * data_cb = NULL;
 
    /*
     * check the time specs for each flow entry
     */
    data_cb = (typeof(data_cb))uap;

    if (data_cb->cb_conn_session == NULL)  {
        jnx_flow_data_register_with_mgmt(data_cb);
    }

    /*
     * Get the current time stamp
     */
    jnx_flow_data_update_ts(data_cb, NULL);

    return;
}

/**
 * This function initializes the data application control block
 * @param  data_cb    data control block pointer
 * @param  ctxt       event context pointner
 * @returns
 *      EOK     if successful
 *      EFAIL   otherwise
 */
static status_t
jnx_flow_data_init_cb(jnx_flow_data_cb_t * data_cb, evContext ctxt)
{
    int32_t idx = 0;
    msp_shm_params_t shmp;
    msp_objcache_params_t ocp;
    int count = 0, cpu_num;

    bzero(&shmp, sizeof(shmp));
    bzero(&ocp, sizeof(ocp));

    bzero(data_cb, sizeof(*data_cb));

    /*
     * Get the user_cpu
     */
    cpu_num = MSP_NEXT_NONE;
    while ((cpu_num = msp_env_get_next_user_cpu(cpu_num)) != 
           MSP_NEXT_END) {
        
        data_cb->user_cpu[count] = cpu_num;
        count++;
    }

    if (count == 0) {
        jnx_flow_log(LOG_INFO, "no user core configured");
        return EFAIL;
    }

    /* Now bind this thread to one of the threads in the user core*/
    if (msp_process_bind(jnx_flow_data_cb.user_cpu[0]) != EOK) {
        jnx_flow_log(LOG_INFO, "Unable to bind main thread to the user cpu");
        return EFAIL;
    }

    data_cb->cb_ctxt   = ctxt;
    data_cb->cb_status = JNX_FLOW_DATA_STATUS_INIT;

    strncpy(shmp.shm_name, "jnx-flow arena", SHM_NAME_LEN);
    shmp.shm_flags |= MSP_APP_SHM_RESTART;

    /*
     *  allocate & initialze the shared memory
     */
    if (msp_shm_allocator_init(&shmp) != MSP_OK) {
        jnx_flow_log(LOG_INFO, "shared memory allocator initialization failed");
        return EFAIL;
    }

    data_cb->shm_handle = shmp.shm;

    /*
     *  create objcache for the look table
     */
    ocp.oc_shm = shmp.shm;
    strncpy(ocp.oc_name, "flow-table cache", OC_NAME_LEN);

    ocp.oc_size  = sizeof(jnx_flow_data_flow_db_t);
    ocp.oc_align = 0;

    jnx_flow_log(LOG_INFO, "%s:%d:obj-cache create for \"%s\" %d",
                 __func__, __LINE__, ocp.oc_name, ocp.oc_size);
    if (msp_objcache_create(&ocp) != MSP_OK) {
        jnx_flow_log(LOG_INFO, "obj-cache create failed");
        return EFAIL;
    }

    /*
     * store the objcache handler for the flow-table
     */
    data_cb->cb_hash_oc = ocp.oc;

    /*
     *  create objcache for the flow entry
     */
    ocp.oc_shm = shmp.shm;
    strncpy(ocp.oc_name, "flow-entry cache", OC_NAME_LEN);

    ocp.oc_size  = sizeof(jnx_flow_data_flow_entry_t);
    ocp.oc_align = 0;

    jnx_flow_log(LOG_INFO, "%s:%d:obj-cache create for \"%s\" %d",
                 __func__, __LINE__, ocp.oc_name, ocp.oc_size);
    if (msp_objcache_create(&ocp) != MSP_OK) {
        jnx_flow_log(LOG_INFO, "obj-cache create failed");
        return EFAIL;
    }

    /*
     * store the objcache handler for the flow-entry
     */
    data_cb->cb_flow_oc = ocp.oc;

    /*
     *  create objcache for the rules
     */
    ocp.oc_shm = shmp.shm;
    strncpy(ocp.oc_name, "rule-entry cache", OC_NAME_LEN);

    ocp.oc_size  = sizeof(jnx_flow_data_rule_entry_t);
    ocp.oc_align = 0;

    jnx_flow_log(LOG_INFO, "%s:%d:obj-cache create for \"%s\" %d",
                 __func__, __LINE__, ocp.oc_name, ocp.oc_size);

    if (msp_objcache_create(&ocp) != MSP_OK) {
        jnx_flow_log(LOG_INFO, "obj-cache create failed");
        return EFAIL;
    }

    /*
     * store the objcache handler for the flow-table
     */
    data_cb->cb_rule_oc = ocp.oc;

    /*
     *  create objcache for the service set
     */
    ocp.oc_shm = shmp.shm;
    strncpy(ocp.oc_name, "svc-entry cache", OC_NAME_LEN);

    ocp.oc_size  = sizeof(jnx_flow_data_svc_set_t);
    ocp.oc_align = 0;

    jnx_flow_log(LOG_INFO, "%s:%d:obj-cache create for \"%s\" %d",
                 __func__, __LINE__, ocp.oc_name, ocp.oc_size);
    if (msp_objcache_create(&ocp) != MSP_OK) {
        jnx_flow_log(LOG_INFO, "obj-cache create failed");
        return EFAIL;
    }

    /*
     * store the objcache handler for the service_set
     */
    data_cb->cb_svc_oc = ocp.oc;

    /*
     *  create objcache for list entries
     */

    ocp.oc_shm = shmp.shm;
    strncpy(ocp.oc_name, "list-entry cache", OC_NAME_LEN);

    ocp.oc_size  = sizeof(jnx_flow_data_list_entry_t);
    ocp.oc_align = 0;

    jnx_flow_log(LOG_INFO, "%s:%d:obj-cache create for \"%s\" %d",
                 __func__, __LINE__, ocp.oc_name, ocp.oc_size);
    if (msp_objcache_create(&ocp) != MSP_OK) {
        jnx_flow_log(LOG_INFO, "obj-cache create failed");
        return EFAIL;
    }

    /*
     * store the objcache handler for the list-entry
     */
    data_cb->cb_list_oc = ocp.oc;

    /*
     * Initialize the configuration lock
     */
    if (JNX_FLOW_DATA_CONFIG_LOCK_INIT(data_cb)) {
        jnx_flow_log(LOG_INFO, "data agent config read/write lock init failed");
        return EFAIL;
    }

    /*
     * TBD: convert into malloc for packet buffers 
     */

    /*
     *  create objcache for the packet buffers 
     */
    ocp.oc_shm = shmp.shm;
    strncpy(ocp.oc_name, "packet-buffer cache", OC_NAME_LEN);

    ocp.oc_size  = JNX_FLOW_BUF_SIZE;
    ocp.oc_align = 0;

    jnx_flow_log(LOG_INFO, "%s:%d:obj-cache create for \"%s\" %d",
                 __func__, __LINE__, ocp.oc_name, ocp.oc_size);
    if (msp_objcache_create(&ocp) != MSP_OK) {
        jnx_flow_log(LOG_INFO, "obj-cache create failed");
        return EFAIL;
    }

    /*
     * store the objcache handler for packet-buffer 
     */
    data_cb->cb_buf_oc = ocp.oc;

    /*
     * Initialize the service data base, and the rule database
     */
    patricia_root_init(&data_cb->cb_svc_db,  FALSE, 
                       fldsiz(jnx_flow_data_svc_set_t, svc_key),
                       fldoff(jnx_flow_data_svc_set_t, svc_key) -
                       fldoff(jnx_flow_data_svc_set_t, svc_node) -
                       fldsiz(jnx_flow_data_svc_set_t, svc_node));

    patricia_root_init(&data_cb->cb_svc_id_db,  FALSE,
                       fldsiz(jnx_flow_data_svc_set_t, svc_id),
                       fldoff(jnx_flow_data_svc_set_t, svc_id) -
                       fldoff(jnx_flow_data_svc_set_t, svc_id_node) -
                       fldsiz(jnx_flow_data_svc_set_t, svc_id_node));

    patricia_root_init(&data_cb->cb_rule_db,  FALSE, 
                       fldsiz(jnx_flow_data_rule_entry_t, rule_id),
                       fldoff(jnx_flow_data_rule_entry_t, rule_id) -
                       fldoff(jnx_flow_data_rule_entry_t, rule_node) -
                       fldsiz(jnx_flow_data_rule_entry_t, rule_node));

    /*
     * for flow hash data base, allocate a buffer
     */
    if ((data_cb->cb_flow_db = HASH_ALLOC(data_cb->cb_hash_oc, 
                                          jnx_flow_data_cb.user_cpu[0])) == NULL) {
        jnx_flow_log(LOG_INFO, "data agent hash table buffer alloc failed");
        return EFAIL;
    }

    /*
     * Initialize the flow data base
     */
    for (idx = 0; idx < JNX_FLOW_DATA_FLOW_BUCKET_COUNT; idx++) {
        JNX_FLOW_DATA_HASH_CHAIN(data_cb, idx) = NULL;
        JNX_FLOW_DATA_HASH_COUNT(data_cb, idx) = 0;
        JNX_FLOW_DATA_HASH_LAST(data_cb, idx) = NULL;
        if (JNX_FLOW_DATA_HASH_LOCK_INIT(data_cb->cb_flow_db, idx)) {
            jnx_flow_log(LOG_INFO, "data agent flow bucket lock init failed");
            return EFAIL;
        }
    }

    /*
     * for config responses, allocate a buffer.
     */
    if ((data_cb->cb_send_buf = BUF_ALLOC(data_cb->cb_buf_oc,
                                          jnx_flow_data_cb.user_cpu[0])) == NULL) {
        jnx_flow_log(LOG_INFO, "data agent periodic buffer alloc failed");
        return EFAIL;
    }

    jnx_flow_log(LOG_INFO, "data agent control block initialized");
    return EOK;
}


/**
 * This function shutdowns the data application process
 * @param  data_cb    data control block pointer
 * @returns
 *      NONE 
 */
static void
jnx_flow_data_shutdown(jnx_flow_data_cb_t * data_cb)
{
    data_cb->cb_status = JNX_FLOW_DATA_STATUS_SHUTDOWN;

    /*
     * shutdown the connections
     */
    if (data_cb->cb_conn_client) {
        pconn_client_close(data_cb->cb_conn_client); 
    }

    if (data_cb->cb_conn_session) {
        pconn_session_close(data_cb->cb_conn_session); 
    }

    if (data_cb->cb_conn_server) {
        pconn_server_shutdown(data_cb->cb_conn_server); 
    }

    /*
     * destroy the obj cache
     */

    if (data_cb->cb_svc_oc) {
        msp_objcache_destroy(data_cb->cb_svc_oc);
        data_cb->cb_svc_oc = NULL;
    }

    if (data_cb->cb_rule_oc) {
        msp_objcache_destroy(data_cb->cb_rule_oc);
        data_cb->cb_rule_oc = NULL;
    }

    if (data_cb->cb_list_oc) {
        msp_objcache_destroy(data_cb->cb_list_oc);
        data_cb->cb_list_oc = NULL;
    }

    if (data_cb->cb_buf_oc) {
        msp_objcache_destroy(data_cb->cb_buf_oc);
        data_cb->cb_buf_oc = NULL;
    }

    if (data_cb->cb_flow_oc) {
        msp_objcache_destroy(data_cb->cb_flow_oc);
        data_cb->cb_flow_oc = NULL;
    }

    if (data_cb->cb_hash_oc) {
        msp_objcache_destroy(data_cb->cb_hash_oc);
        data_cb->cb_hash_oc = NULL;
    }

    /*
     * destroy the locks
     * (TBD)
     */
    jnx_flow_log(LOG_INFO, "data agent application shutting down");

    msp_exit();
    return;
}

/**
 * This function handles the SIGTERM signal
 * @param  signal singnal number
 */
static void
jnx_flow_data_sigterm_handler(int event __unused)
{
    jnx_flow_data_shutdown(&jnx_flow_data_cb);
}

/**
 * This function creates a libconn server to listen to management connection
 * @param  ctxt   eventLib context
 * @returns
 *      EOK    if succecssful 
 *      EFAIL  otherwise
 */
static status_t
jnx_flow_data_create_pconn_server(jnx_flow_data_cb_t * data_cb)
{
    pconn_server_params_t svr_params;

    memset(&svr_params, 0, sizeof(pconn_server_params_t));

    svr_params.pconn_port            = JNX_FLOW_DATA_PORT;
    svr_params.pconn_max_connections = JNX_FLOW_DATA_MAX_CONN_IDX;
    svr_params.pconn_event_handler   = jnx_flow_data_mgmt_event_handler;


    if (!(data_cb->cb_conn_server =
          pconn_server_create(&svr_params, data_cb->cb_ctxt,
                              jnx_flow_data_mgmt_msg_handler, data_cb))) {
        jnx_flow_log(LOG_INFO, "Management agent server create failed");
        return EFAIL;
    }
    return EOK;
}

/**
 * This function registers with the management agent running on RE
 * @param  ctxt   eventLib context
 * @returns
 *      EOK    if succecssful 
 *      EFAIL  otherwise
 */
static status_t
jnx_flow_data_register_with_mgmt(jnx_flow_data_cb_t * data_cb) 
{
    pconn_client_params_t clnt_params;

    memset(&clnt_params, 0, sizeof(clnt_params));
    clnt_params.pconn_port = JNX_FLOW_MGMT_PORT;
    clnt_params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_RE;

    if (!(data_cb->cb_conn_client = pconn_client_connect(&clnt_params))) {
        jnx_flow_log(LOG_INFO, "register with management agent failed");
        return EFAIL;
    }
    jnx_flow_log(LOG_INFO, "register with management agent success");
    return EOK;
}

/**
 * This function initializes the data application process 
 * @param  ctxt   eventLib context
 * @returns
 *      EOK    if succecssful 
 *      EFAIL  otherwise
 */
static int 
jnx_flow_data_agent_init(evContext ctxt)
{
    msp_dataloop_params_t dloop_args;
    pthread_attr_t  attr;        
    pthread_t       tid;
    /*
     * Initialize the underlying fifo/obj-cache
     * infrastructure
     */
    if (msp_init() != MSP_OK) {
        return EFAIL;
    }

    /*
     * initialize the data module control block
     */
    if (jnx_flow_data_init_cb(&jnx_flow_data_cb, ctxt) != EOK) {
        goto cleanup_agent;
    }

    /*
     * signal handlers
     */
    signal(SIGTERM, jnx_flow_data_sigterm_handler);
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);

    /*
     * set logging level to info, & to syslog only
     */
    logging_set_level(LOG_INFO);
    logging_set_mode(LOGGING_SYSLOG);

    /*
     * create packet processing threads
     * let the control block be the application
     * cookie for the packet processing threads
     */

    bzero(&dloop_args, sizeof(dloop_args));
    dloop_args.app_data = &jnx_flow_data_cb;

    if (msp_data_create_loops((msp_data_loop_t)jnx_flow_data_process_packet,
                              &dloop_args) != MSP_OK) {
        jnx_flow_log(LOG_INFO, "packet processing thread create failed");
        goto cleanup_threads;
    }

    pthread_attr_init(&attr);

    if (pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)) {
        goto cleanup_threads;
    }

    if (pthread_attr_setcpuaffinity_np(&attr, jnx_flow_data_cb.user_cpu[1])) {
        goto cleanup_threads;
    }

    if (pthread_create(&tid, &attr, jnx_flow_data_flow_aging_proc,
                       (void*) &jnx_flow_data_cb)) {
        goto cleanup_threads;
    }
    
    /*
     * start the pconn server for listening to management module
     * agent messages
     */
    if (jnx_flow_data_create_pconn_server(&jnx_flow_data_cb) != EOK) {
        jnx_flow_log(LOG_INFO, "pconn server setup failed");
        goto cleanup_threads;
    }

    /*
     * register self with the management module running on RE
     */
    if (jnx_flow_data_register_with_mgmt(&jnx_flow_data_cb) != EOK) {
        /*
         * do not exit, the periodic timer routine will take care
         * of reconnect
         */
    }

    /*
     * Start a periodic time event function
     */
    if (evSetTimer(jnx_flow_data_cb.cb_ctxt,
                   jnx_flow_data_periodic_event_handler,
                   &jnx_flow_data_cb, evNowTime(),
                   evConsTime((JNX_FLOW_DATA_PERIODIC_SEC), 0), 0) < 0)
    {
        jnx_flow_log(LOG_INFO, "periodic event handler create failed");
        goto cleanup_server;
    }

    /*
     * Do not mark self as up, until we receive the connect
     * from the management on the server connection
     */
    jnx_flow_log(LOG_INFO, "data agent initialization successful");
    return EOK;

cleanup_server:
    pconn_server_shutdown(jnx_flow_data_cb.cb_conn_server);

cleanup_threads:
cleanup_agent:
    jnx_flow_log(LOG_INFO, "data agent initialization failed");

    jnx_flow_data_shutdown(&jnx_flow_data_cb);

    return EFAIL;
}

/**
 * Entry point for jnx-flow-data agent module
 * This function calls msp_app_init API to initialize the ms-pic
 * infrastructure and the data agent state
 * @param  argc   number of proces arguments
 * @param  argv   argument list
 * @returns
 *      0    on exit
 */
int
main(int argc, char ** argv)
{
    mp_sdk_app_ctx_t app_ctx;
    int rc;

    app_ctx = msp_create_app_ctx(argc, argv, "jnx-flow-data");
    msp_set_app_cb_init(app_ctx, jnx_flow_data_agent_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


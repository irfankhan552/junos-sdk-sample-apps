/*
 * $Id: jnx-gateway-data_main.c 397581 2010-09-02 20:03:02Z sunilbasker $
 *
 * jnx-gateway-data_main.c - Defines the Initialization Sequence of the
 *                      JNX-GATEWAY-DATA App
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
 * @file jnx-gateway-data.c
 * @brief This is the main file of the jnx-gateway-data application. This is
 * responsible for the complete initialisation of the data application.
 *
 * This file covers the following stuff:-
 * 1. Initialisation of the TX & Rx Fifos
 * 2. Launching of the data threads.
 * 3. Binding of the thread to a FIFO
 * 4. Processor Affinity
 * 5. Initialzation of the Pconn Server
 * 6. Initialization of the Timer for periodic cleanup of tunnels.
 * 7. Initialization of the State Control Block of the application.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <isc/eventlib.h>
#include <signal.h>
#include <jnx/mpsdk.h>
#include "jnx-gateway-data.h"
#include "jnx-gateway-data_db.h"
#include "jnx-gateway-data_packet.h"
#include "jnx-gateway-data_control.h"

#define min(a, b) (a < b ? a : b)

trace_file_t * jnx_gw_trace_file;

jnx_gw_data_cb_t*  jnx_gw_data = NULL;

/*===========================================================================*
 *                                                                           *
 *                 Local function prototypes                                 *  
 *                                                                           *
 *===========================================================================*/                 

/* Function to initialise the application control state block */
static jnx_gw_data_cb_t* jnx_gw_data_init_cb(void);

/* Function to create a Pconn Server to communicate with MGMT & CTRL */ 
static jnx_gw_data_err_t jnx_gw_data_create_pconn_server(jnx_gw_data_cb_t* app_cb, 
                          evContext  ev_ctxt);

/* Function to initialize the data agent application */
static int jnx_gw_data_agent_init(evContext  ev_ctxt);

void jnx_gw_data_sigterm_handler(int signo);
/*===========================================================================*
 *                                                                           *
 *                 Function Definitions                                      *
 *                                                                           *
 *===========================================================================*/                 

/**
 * The jnx gateway data agent initialization routine
 */
static int
jnx_gw_data_agent_init(evContext  ev_ctxt)
{
    jnx_gw_data_cb_t*    app_cb = NULL;
    msp_dataloop_params_t dloop_params;
    
   

    msp_init();

    /* 
     * This routine will allocate and initialize the "cb" (control block) for the
     * application. This control block will be shared across various data threads
     * (to process the data packets) & the control thread(to process messages 
     * from control pic & RE). This routine is responsible for allocation & 
     * initialization of 
     * a. control block.
     * b. session database.
     * c. Lock initialization on the CB.
     */

    if ((app_cb = jnx_gw_data_init_cb()) == NULL) {
        jnx_gw_log(LOG_INFO, "Data Agent Control block Initialization failed");

        goto cleanup_data_pic_state;
    }
    jnx_gw_data = app_cb;

    /* clean self on sigterm */
    signal(SIGTERM, jnx_gw_data_sigterm_handler);

    /* ignore sighup */
    signal(SIGHUP, SIG_IGN);

    /* ignore sigpipe */
    signal(SIGPIPE, SIG_IGN);

    /* open the trace file */
    jnx_gw_trace_file = trace_file_open(jnx_gw_trace_file,
                                        PATH_JNX_GW_TRACE,
                                        JNX_GW_TRACE_FILE_SIZE,
                                        JNX_GW_TRACE_FILE_COUNT);

    /* trace all */
    trace_flag_set(jnx_gw_trace_file, TRACE_ALL);
    
    logging_set_level(LOG_INFO);
    logging_set_mode(LOGGING_SYSLOG);

    /*
     * Create the threads responsible for data processing.
     * Here, one thread is reserved for processing of control messages
     * from both Control App as well as RE.
     */

    bzero(&dloop_params, sizeof(msp_dataloop_params_t));
    dloop_params.app_data = app_cb;
    if (msp_data_create_loops((msp_data_loop_t )jnx_gw_data_process_packet,
                              &dloop_params) != MSP_OK) {
        jnx_gw_log(LOG_INFO, "Data Agent create packet processing loop create fail");

        goto cleanup_threads;
    }

    if (jnx_gw_data_create_pconn_server(app_cb, ev_ctxt) != 
        JNX_GW_DATA_SUCCESS) {
        jnx_gw_log(LOG_INFO, "Data Agent connection server setup failed");

        goto cleanup_threads;
    }

    jnx_gw_data_connect_mgmt(app_cb, ev_ctxt);

    if (evSetTimer(ev_ctxt, jnx_gw_periodic_cleanup_timer_expiry,
                   (void*)app_cb, 
                   evNowTime(),
                   evConsTime((JNX_GW_DATA_PERIODIC_CLEANUP_TIME_SEC), 0),0)
        < 0) {

        jnx_gw_log(LOG_INFO, "Data Agent Periodic stat timer event setup failed");
        goto cleanup_pconn_server;
    }

    /*
     * Now we are done with all the initialisations, hence mark the state of the 
     * application as READY. Data Threads will process packets only when
     * the application state is SAMPLE_DATA_APP_STATE_READY.
     * 
     */

    /*
     * Do not mark self as up, until we have the connection
     * established with the management module
     */

    /* evMainloop will start in the msp_app_init() */
    /* return here, for successful cases */
    jnx_gw_log(LOG_INFO, "Data Agent Initialization done");
    return EOK;

    /* 
     * Under normal circunstances th control should never come here.
     * It is only during some error during initialization we will end
     * up coming here, just to clean up whatever we have done do far.
     */

cleanup_pconn_server:
    /* Here the pconn_server will be cleared */
    pconn_server_shutdown(app_cb->conn_server);

cleanup_threads:
cleanup_data_pic_state:    
    /* Here we will clean up the strucutures for data pic initialization. */
    jnx_gw_data_shutdown(app_cb);

    jnx_gw_log(LOG_INFO, "Data Agent Initialization failed");

    return EFAIL;
}

/**
 * Create a pconn client which would enable jnx-gateway-mgmt to know about the 
 * PIC and enable communication with the PIC. Soon, this will be replaced with
 * a periodic message sent from jnx-gateway-data to jnx-gateway-mgmt
 *
 * This function creates a pconn server and registers the event-handler, 
 * message-handler, and it's own state block(as a cookie) with the pconn
 * library.
 *
 * @param[in] app_cb    State Block of the JNX-GATEWAY-DATA
 * @param[in] ev_ctxt   Context information associated with the event library
 *
 * @return Result of the operation
 *     @li JNX_GW_DATA_FAILURE  if the server creation failed
 *     @li JNX_GW_DATA_SUCCESS  if the server creation was successful
 */
jnx_gw_data_err_t
jnx_gw_data_connect_mgmt(jnx_gw_data_cb_t* app_cb,
                         evContext        ev_ctxt __unused)
{
    pconn_client_params_t params;

    if (app_cb->conn_client) {
        jnx_gw_log(LOG_INFO, "Closing management client connection");
        pconn_client_close(app_cb->conn_client); 
    }

    memset(&params, 0, sizeof(pconn_client_params_t));

    params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_RE;
    params.pconn_port                    = JNX_GW_MGMT_DATA_PORT;
    params.pconn_num_retries             = PCONN_RETRY_MAX;

    if (!(app_cb->conn_client = pconn_client_connect(&params))) {
        jnx_gw_log(LOG_INFO, "Management client connection setup failed");
        return JNX_GW_DATA_FAILURE;
    }

    jnx_gw_log(LOG_INFO, "Management client connection setup successful");
    return JNX_GW_DATA_SUCCESS;
}

/**
 * Create the Pconn server to enable jnx-gateway-ctrl and jnx-gateway-mgmt to
 * communicate with the jnx-gateway-data.
 *
 * This function creates a pconn server and registers the event-handler, 
 * message-handler, and it's own state block(as a cookie) with the pconn
 * library.
 *
 * @param[in] app_cb    State Block of the JNX-GATEWAY-DATA
 * @param[in] ev_ctxt   Context information associated with the event library
 *
 * @return Result of the operation
 *     @li JNX_GW_DATA_FAILURE  if the server creation failed
 *     @li JNX_GW_DATA_SUCCESS  if the server creation was successful
 */
static jnx_gw_data_err_t
jnx_gw_data_create_pconn_server(jnx_gw_data_cb_t* app_cb,
                                evContext        ev_ctxt)
{
    
    pconn_server_params_t params;
    pconn_server_t*       server;

    memset(&params, 0, sizeof(pconn_server_params_t));
    
    params.pconn_port            = JNX_GW_DATA_PORT;
    params.pconn_event_handler   = jnx_gw_data_pconn_event_handler;
    params.pconn_max_connections = 2; /* One connection for CTRL & one for Mgmt */

    server = pconn_server_create(&params, ev_ctxt,
                                jnx_gw_data_pconn_msg_handler, 
                                (void*)app_cb);

    if (!server) {
        jnx_gw_log(LOG_INFO, "Connection server setup failed");
        return JNX_GW_DATA_FAILURE;
    }

    app_cb->conn_server = server; 

    return JNX_GW_DATA_SUCCESS;
}

/**
 * Create & Initialise the State Control BLock of the JNX-GATEWAY-DATA.
 *
 * This function creates the JNX_GW_DATA_CB_T structure and initialises the
 * hash tables, locks etc used by the application. It also populates the
 * RE name, the name of pic on which control pic resides and it's own PIC
 * name
 * 
 * @param[in] app_params    parameters associated with the application. 
 *
 * @return Result of the operation
 *     @li NULL                 if the state control block couldn't be 
 *                              created.
 *     @li jnx_gw_data_cb_t*    Pointer to the state control block     
 */
static jnx_gw_data_cb_t*
jnx_gw_data_init_cb()
{
    jnx_gw_data_cb_t*    app_cb = NULL;
    int                  i=0;
     
    /* 
     * This API is called instead of malloc to ensure
     * that later on when we want to plug in memory 
     * manager we can do that easily without disturbing 
     * the main code.
     */
    if ((app_cb = (jnx_gw_data_cb_t*)JNX_GW_MALLOC(JNX_GW_DATA_ID,
                 sizeof(jnx_gw_data_cb_t))) == NULL) {
        jnx_gw_log(LOG_INFO, "Malloc for data agent control block failed");
         return NULL;
    }

    memset(app_cb, 0, sizeof(jnx_gw_data_cb_t));

    app_cb->app_state = JNX_GW_DATA_STATE_INIT;   
    
    if (jnx_gw_data_lock_init(&app_cb->app_cb_lock) != EOK) {
        goto free_cb;
    }

    /*
     * Initialise the GRE TUNNEL DB.
     * 1. Initialise the bucket lock
     * 2. Initialise the chain for the bucket.
     * 3. Intialise the counter for the number of entries in the bucket
     */
    for(i = 0; i < JNX_GW_DATA_MAX_GRE_BUCKETS; i++) {

        if(jnx_gw_data_lock_init(&app_cb->gre_db.hash_bucket[i].bucket_lock) 
           != EOK) {
            jnx_gw_log(LOG_INFO, "Mutex setup failed for GRE Hash Bucket(%d)", i);
            goto free_cb;
        }

        app_cb->gre_db.hash_bucket[i].chain = NULL;
        app_cb->gre_db.hash_bucket[i].count = 0;
        
    }
    
    /*
     * Initialise the IPIP SUB TUNNEL DB.
     * 1. Initialise the bucket lock. The number of entries being
     * initialised is equal to the number of GRE tunnels because, the 
     * ip-ip sub tunnel represents the reverse path for the forward direction
     * of the GRE tunnel. Hence, the number would be equal to the number of
     * GRE. 
     */
    for(i = 0; i < JNX_GW_DATA_MAX_GRE_BUCKETS; i++) {

        if(jnx_gw_data_lock_init(&app_cb->ipip_sub_tunnel_db.hash_bucket[i].bucket_lock)
           != EOK) {
            jnx_gw_log(LOG_INFO, "Mutex setup failed for IPIP Hash Bucket(%d)", i);
            goto free_cb;
        }

        app_cb->ipip_sub_tunnel_db.hash_bucket[i].chain = NULL;
        app_cb->ipip_sub_tunnel_db.hash_bucket[i].count = 0;
    }

    /*
     * Initialise the IP-IP STAT DB.
     * 1. Initialise the bucket lock
     * 2. Initialise the chain for the bucket.
     * 3. Intialise the counter for the number of entries in the bucket
     */
    for(i = 0; i < JNX_GW_DATA_MAX_IP_IP_BUCKETS; i++) {

        if(jnx_gw_data_lock_init(&app_cb->ipip_tunnel_db.hash_bucket[i].bucket_lock) 
           != EOK) {
            jnx_gw_log(LOG_INFO, "Mutex setup failed for IPIP STAT Hash Bucket(%d)",
                   i);
            goto free_cb;
        }
        app_cb->ipip_sub_tunnel_db.hash_bucket[i].chain = NULL;
        app_cb->ipip_sub_tunnel_db.hash_bucket[i].count = 0;
    }

    /*
     * Initialise the VRF STAT DB.
     * 1. Initialise the bucket lock
     * 2. Initialise the chain for the bucket.
     * 3. Intialise the counter for the number of entries in the bucket
     */
    for(i = 0; i < JNX_GW_DATA_MAX_VRF_BUCKETS; i++) {

        if(jnx_gw_data_lock_init(&app_cb->vrf_db.hash_bucket[i].bucket_lock) != EOK) {
            jnx_gw_log(LOG_INFO, "Mutex setup failed for VRF STAT Hash Bucket(%d)",
                   i);
            goto free_cb;
        }
    }

    /*
     * Allocate the buffer for sending responses/ periodic messages. Since,
     * there is just thread responsible for all this, we just need one buffer
     * to be pre allocated.
     */
    if((app_cb->buffer = (char*)JNX_GW_MALLOC(JNX_GW_DATA_ID,
                 JNX_GW_DATA_MAX_BUF_SIZE)) == NULL) {
        jnx_gw_log(LOG_INFO, "Malloc failed for Periodic stat send buffer");
        goto free_cb;
    }

    /* Initialise the deleted tunnels structure */
    app_cb->del_tunnels.gre_tunnel  = NULL;
    app_cb->del_tunnels.ipip_tunnel = NULL;
    app_cb->del_tunnels.ipip_sub_tunnel = NULL;

    jnx_gw_log(LOG_INFO, "Data Agent control block initialized");
    return app_cb;

free_cb:
    jnx_gw_log(LOG_INFO, "Data agent control block initialization failed");
    JNX_GW_FREE(JNX_GW_DATA_ID, app_cb);

    return NULL;
}


/**
 * Starting Point fo the JNX-GATEWAY-DATA
 * This routine is responsible for the complete initialisation of the application. 
 * It performs the following functionality
 * a. Get the application specific parameters from the sdk.
 * b. Initialise the State Control Block for the application.
 * c. Create the Tx & Rx FIFOs.
 * d. Create the Threads for processing data
 * e. Bind a thread to Tx & Rx FIFO i.e one Tx & Rx FIFO per thread.
 * f. Setup the context for using the event library.
 * g. Create a Pconn Server to enable communication with the JNX-GATEWAY-CTRL &
 *    JNX-GATEWAY-MGMT.
 * h. Setup a periodic timer to clean up the deleted tunnels.
 */
int
main(int argc, char** argv)
{
    mp_sdk_app_ctx_t app_ctx;
    int rc;

    app_ctx = msp_create_app_ctx(argc, argv, "jnx-gateway-data");
    msp_set_app_cb_init(app_ctx, jnx_gw_data_agent_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


/**
 * This function clears the application
 * does an exit
 */
void 
jnx_gw_data_sigterm_handler(int signo __unused)
{
    jnx_gw_log(LOG_INFO, "Closing Data Agent Application");

    if (jnx_gw_data) {
        if (jnx_gw_data->conn_client) {
            pconn_client_close(jnx_gw_data->conn_client);
            jnx_gw_data->conn_client = NULL;
        }
        if (jnx_gw_data->conn_server) {
            pconn_server_shutdown(jnx_gw_data->conn_server);
            jnx_gw_data->conn_server = NULL;
        }
    }

    msp_exit();
    exit(0);
}

/**
 * Initiate the shutdown of the application. This should happen when the
 * application receives a SIGTERM signal. Signal would be received by the 
 * main thread. 
 * 
 */
void 
jnx_gw_data_shutdown(jnx_gw_data_cb_t* app_cb)
{

    if(app_cb == NULL)
        return;
    jnx_gw_log(LOG_INFO, "Closing Data Agent Application");
    
    /*
     * 1. Set the state of the application as SHUTDOWN, close the raw socket opened
     *      for receving the packets(required for pseudo sdk)
     * 2. All the threads before starting the packet processing will check
     *    the state of the CB.
     * 3. If the state of the CB is SHUTDOWN, then the thread will exit and 
     *    will reduce the value of number of threads running.
     * 4. The main thread will keep on waiting till all the threads exit.
     * 5. Once the counter becomes zero, it will close the control connections.
     * 6. Clean the session DB & free the memory.
     * 7. Drain the FIFOs
     * 8. Free the fifo memory.
     * 9. Clear & free the application cb
     * 10. Clear the data pic state (used for pseudo sdk)
     * 11. exit
     */
     
    msp_exit();
    return;
}

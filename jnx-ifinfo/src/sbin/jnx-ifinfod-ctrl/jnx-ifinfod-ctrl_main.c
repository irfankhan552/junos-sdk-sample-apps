/*
 * $Id: jnx-ifinfod-ctrl_main.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-ifinfod-ctrl_main.c - handles main and signal handlers
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 *
 * @file  jnx-ifinfod_-ctrl_main.c
 * @brief Handles main () and daemon specific initialiation fucntions.
 *
 */

#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <isc/eventlib.h>
#include <jnx/mpsdk.h>
#include <jnx/aux_types.h>
#include <jnx/logging.h>
#include <string.h>
#include <jnx/trace.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <jnx/pconn.h>

#define DNAME_JNX_IFINFOD_CTRL    "jnx-ifinfod-ctrl"

#include <jnx/logging.h>

/*** Constants ***/
#define MGMT_CLIENT_CONNECT_RETRIES    3     /* max number of connect retries */
#define SNMPTRAP_PORT_NUM              7080
#define MGMT_CLIENT_MSG                5

/**
 * Define a logging macro which prefixes a log tag like on the RE
 */
#define LOG(_level, _fmt...)   \
    logging((_level), "JNX_IFINFOD_CTRL: " _fmt)

static pconn_client_t     * mgmt_client;     /* client cnx to management component */
status_t init_connections (evContext ctx);
void close_connections (void);


/**
 * Connection handler for new and dying connections
 *
 * @param[in] session
 *      The session information for the source peer
 *
 * @param[in] event
 *      The event (established, or shutdown are the ones we care about)
 *
 * @param[in] cookie
 *      The cookie we passed in. This is the eventlib context here.
 */
static void
client_connection (pconn_client_t * session __unused,
		   pconn_event_t event,
		   void * cookie __unused)
{
    int rc;
    uint32_t id;

    switch (event) {
    case PCONN_EVENT_ESTABLISHED:
	id = htonl(MGMT_CLIENT_MSG);
	rc = pconn_client_send(mgmt_client, MGMT_CLIENT_MSG, &id, sizeof(id));

	if (rc != PCONN_OK) {
	    LOG(LOG_ERR, "%s: Failed to send MSG_ID to the mgmt component. " 
		"Error: %d", __func__, rc);
	}
	break;
    case PCONN_EVENT_SHUTDOWN:
	mgmt_client = NULL;
	break;
    default:
	printf("default case");
	break;
    }
}

/**
 * Message handler for open connections,
 * nothing much is done here for now.
 *
 * @param[in] session
 *      The session information for the source peer
 *
 * @param[in] msg
 *      The inboud message
 *
 * @param[in] cookie
 *      The cookie we passed in. This is the eventlib context here.
 *
 * @return
 *      SUCCESS if successful.
 */
static status_t
client_message (pconn_client_t * session __unused,
		ipc_msg_t * msg __unused,
		void * cookie __unused)
{
    return SUCCESS;
}

/**
 * Initialize the client connection
 *
 * @param[in] ctx
 *      event context
 *
 * @return
 *      SUCCESSFUL if successful; otherwise EFAIL with an error message.
 */
status_t
init_connections (evContext ctx)
{
    pconn_client_params_t c_params;

    mgmt_client = NULL;

    bzero(&c_params, sizeof(pconn_client_params_t));

    /* setup the client args */
    c_params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_RE;
    c_params.pconn_port                    = SNMPTRAP_PORT_NUM;
    c_params.pconn_num_retries             = MGMT_CLIENT_CONNECT_RETRIES;
    c_params.pconn_event_handler           = client_connection;

    /* connect */
    mgmt_client = pconn_client_connect_async(&c_params, ctx, client_message, NULL);

    if (mgmt_client == NULL) {
	LOG(LOG_ERR, "%s: Failed to initialize the pconn client connection " \
	  "to the management component", __func__);
	return EFAIL;
    }

    return SUCCESS;
}

/**
 * Close existing connections.
 */
void
close_connections (void)
{
    pconn_client_close(mgmt_client);
    mgmt_client = NULL;
}

/**
 * This function quits jnx-ifinfo-ctrl daemon
 */
static void
jnx_ifinfod_ctrl_quit (int signo __unused)
{
    close_connections();
}

/**
 * This fucntion calls daemon specific initialization function calls.
 *
 * @param[in] ctx
 *      Event context
 *
 * @return SUCCESS if successful; EFAIL incase of failure.
 */
static int
jnx_ifinfod_ctrl_init (evContext ctx __unused)
{

    /* Ignore some signals that we may receive */
    signal(SIGTERM, jnx_ifinfod_ctrl_quit); 
    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    
    logging_set_mode(LOGGING_SYSLOG);
    return init_connections(ctx);
}

/**
 * Main function invokes msp_app_init API and initializes ifinfod_ctrl
 * daemon with necessary info.
 *
 * @param[in] argc
 *      Standard argument count
 *
 * @param[in] argv
 *      Standard arguments array
 *
 * @return 0 upon successful exit of the application(shouldn't hapen)
 *         or -1 upon failure
 */
int
main (int32_t argc , char **argv)
{
    mp_sdk_app_ctx_t app_ctx;
    int rc;

    app_ctx = msp_create_app_ctx(argc, argv, DNAME_JNX_IFINFOD_CTRL);
    msp_set_app_cb_init(app_ctx, jnx_ifinfod_ctrl_init);

    rc = msp_app_init(app_ctx);

    /* Should never reach this point */
    msp_destroy_app_ctx(app_ctx);
    return rc;
}


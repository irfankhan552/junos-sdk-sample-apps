/*
 * $Id: equilibrium-data_monitor.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium-data_monitor.c
 * @brief Relating to server monitoring
 * 
 * Relating to server monitoring / probing
 */

#include "equilibrium-data_main.h"
#include <sys/socket.h>
#include <unistd.h>
#include "equilibrium-data_config.h"
#include "equilibrium-data_conn.h"
#include "equilibrium-data_packet.h"
#include "equilibrium-data_monitor.h"


/*** Constants ***/

/**
 * Add a monitor blurb to log msgs
 */
#define LOGM(_level, _fmt...)   \
    LOG((_level), "Server Monitor: " _fmt)


/*** Data structures ***/


/**
 * Forward declaration of struct mon_app_info_s
 */
typedef struct mon_app_info_s mon_app_info_t;


/**
 * An item in a server set
 */
typedef struct mon_server_info_s {
    in_addr_t                server_addr;  ///< server IP address
    uint32_t                 sessions;     ///< number of sessions active
    int                      sock;         ///< fd of socket to this server
    evTimerID                test_timer;   ///< timer id
    evConnID                 conn_id;      ///< the eventlib socket cnx handle
    evFileID                 file_id;      ///< the eventlib socket read handle 
    mon_app_info_t           * app;        ///< the app this server is in
    boolean                  is_up;        ///< server status (knows which set it is in)
    uint16_t                 timeouts;     ///< number of timeouts observed
    TAILQ_ENTRY(mon_server_info_s) entries; ///< next/prev entries
} mon_server_info_t;


/**
 * A set of eq_server_info_t
 */
typedef TAILQ_HEAD(server_info_set_s, mon_server_info_s) server_info_set_t;


/**
 * The key of a mon_app_info_t
 */
typedef struct mon_app_key_s {
    uint16_t  svc_set_id;           ///< service-set id
    in_addr_t app_addr;             ///< application address
    uint16_t  app_port;             ///< application port
} mon_app_key_t;


/**
 * The structure we use to bundle the patricia-tree node with the data 
 * to store for each application
 */
struct mon_app_info_s {
    patnode           node;  ///< Tree node in applications (comes first)
    mon_app_key_t     key;                  ///< key
    msp_spinlock_t    app_lock;             ///< lock for this application
    eq_smon_t         * server_mon_params;  ///< server monitoring parameters
    server_info_set_t * up_servers;         ///< up server list, sorted by sessions
    server_info_set_t * down_servers;       ///< down server list
};


static patroot apps;        ///< patricia tree root of mon_app_info_t's
static boolean doShutdown;  ///< flag to exit from the main event loop
static evContext mon_ctx;   ///< monitoring context
static msp_spinlock_t apps_big_lock; ///< lock for whole apps config (pat tree)

/*** STATIC/INTERNAL Functions ***/


/**
 * Generate the app_entry (static inline) function for internal usage only.
 * (see patricia tree (libjuniper) docs for how this expands)
 */
PATNODE_TO_STRUCT(app_entry, mon_app_info_t, node)


/**
 * Forward declaration of function probe server
 */
static void
probe_server(evContext c, void * u, struct timespec d, struct timespec i);


/**
 * Stop any timers and shutdown any probe socket connections to the server.
 * Should have a lock on the application of the server to use this function.
 * 
 * @param[in] server
 *      The server
 */
static void
stop_server_probes(mon_server_info_t * server)
{
    if(evTestID(server->test_timer)) {
        evClearTimer(mon_ctx, server->test_timer);
        evInitID(&server->test_timer);
    }
    
    if(evTestID(server->file_id)) {
        evDeselectFD(mon_ctx, server->file_id);
        evInitID(&server->file_id);
    }

    if(evTestID(server->conn_id)) {
        evCancelConn(mon_ctx, server->conn_id);
        evInitID(&server->conn_id);
    }

    if(server->sock != -1 && close(server->sock) != 0) {
       LOGM(LOG_ERR, "%s: Failed to close socket to server: %m", __func__);
    }

    server->sock = -1;
}


/**
 * When a server probe timeout occurs or connection fails, set timers 
 * appropriately and move server depending on timeouts reached.
 * Should have a lock on the application of the server to use this function.
 * 
 * @param[in] server
 *      The server
 */
static void
server_probe_failed(mon_server_info_t * server)
{
    struct in_addr addr;
    mon_app_info_t * app = server->app;
    
    stop_server_probes(server);
    
    // check if the timeouts have surpassed the allowed number 
    // and we mark the server down if so
    if(server->is_up && ++(server->timeouts) <= 
            app->server_mon_params->timeouts_allowed) {

        // still ok, so schedule next probe to server with connection_interval
        
        if(evSetTimer(mon_ctx, probe_server, server, evAddTime(evNowTime(),
            evConsTime(app->server_mon_params->connection_interval, 0)),
            evConsTime(0, 0), &server->test_timer)) {

            addr.s_addr = server->server_addr; // for inet_ntoa
            LOGM(LOG_EMERG, "%s: Failed to initialize a probe timer to probe "
                "server %s (Error: %m)", __func__, inet_ntoa(addr));
        }
    } else {
        // server is down, use down_retry_interval
        
        if(evSetTimer(mon_ctx, probe_server, server, evAddTime(evNowTime(),
            evConsTime(app->server_mon_params->down_retry_interval, 0)),
            evConsTime(0, 0), &server->test_timer)) {

            addr.s_addr = server->server_addr; // for inet_ntoa
            LOGM(LOG_EMERG, "%s: Failed to initialize a probe timer to probe "
                "server %s (Error: %m)", __func__, inet_ntoa(addr));
        }
        
        // it is was previously up, take it down
        if(server->is_up) {
            server->is_up = FALSE;
            server->sessions = 0;
            TAILQ_REMOVE(app->up_servers, server, entries);
            TAILQ_INSERT_HEAD(app->down_servers, server, entries);
            
            notify_server_status(app->key.svc_set_id, app->key.app_addr,
                    app->key.app_port, server->server_addr, SERVER_STATUS_DOWN);
            
            clean_sessions_using_server(app->key.svc_set_id,
                    app->key.app_addr, app->key.app_port, server->server_addr);
        }
    }
}


/**
 * Read messages from an HTTP server sending us a response
 * 
 * @param[in] ctx
 *     The event context
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] fd
 *     The absolute time when the event is due (now)
 * 
 * @param[in] evmask
 *     The event mask 
 */
static void
http_read(evContext ctx __unused,
          void * uap,
          int fd __unused,
          int evmask __unused)
{
    const uint8_t BUF_LEN  = 64;
    mon_server_info_t * server = (mon_server_info_t *)uap;
    mon_app_info_t * app;
    struct in_addr addr;
    char buf[BUF_LEN];
    int rc;
    
    INSIST_ERR(server != NULL);
    app = server->app;
    bzero(buf, BUF_LEN);
    
    // Get application lock
    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
    
    if(app->server_mon_params == NULL) {
        addr.s_addr = server->server_addr; // for inet_ntoa
        // Release application lock
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
        LOGM(LOG_ERR, "%s: Request to read HTTP response from %s in an "
                "application with no server monitoring parameters",
                __func__, inet_ntoa(addr));
        return;
    }
    
    addr.s_addr = server->server_addr; // for inet_ntoa

    // Validate that it is an HTTP response
    if((rc = recv(server->sock, buf, BUF_LEN - 1, 0)) < 0) {
        
        LOGM(LOG_WARNING, "%s: Probe to server %s did not get an HTTP "
            "response (Error: %m)", __func__, inet_ntoa(addr));
        
        server_probe_failed(server);
        
        // Release application lock
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
        return;
        
    } else {
        if(rc < 3) {
            LOGM(LOG_WARNING, "%s: Probe to server %s did not get a long enough"
                    "response to tell if it was an HTTP response (Content: %s)",
                    __func__, inet_ntoa(addr), buf);
            
            server_probe_failed(server);
            
            // Release application lock
            INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
            return;
            
        } else if(strnstr(buf, "HTTP", 4) == NULL) {

            LOGM(LOG_WARNING, "%s: Probe to server %s did not get an HTTP "
                "response (Content: %s)", __func__, inet_ntoa(addr), buf);
            
            server_probe_failed(server);
            
            // Release application lock
            INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
            return;
        }
    }
    
    LOGM(LOG_INFO, "%s: Probe to server %s returned an HTTP response",
            __func__, inet_ntoa(addr));
    
    stop_server_probes(server);
    
    // server is ok, so schedule next probe to server with connection_interval
    if(evSetTimer(mon_ctx, probe_server, server, evAddTime(evNowTime(),
            evConsTime(app->server_mon_params->connection_interval, 0)),
            evConsTime(0, 0), &server->test_timer)) {

        addr.s_addr = server->server_addr; // for inet_ntoa
        LOGM(LOG_EMERG, "%s: Failed to initialize a probe timer to probe "
            "server %s (Error: %m)", __func__, inet_ntoa(addr));
        
        // Release application lock
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
        return;
    }
    
    // it is was previously down, take it up
    if(!server->is_up) {
        server->is_up = TRUE;
        server->sessions = 0;
        TAILQ_REMOVE(app->down_servers, server, entries);
        TAILQ_INSERT_HEAD(app->up_servers, server, entries);
        
        notify_server_status(server->app->key.svc_set_id,
                server->app->key.app_addr, server->app->key.app_port,
                server->server_addr, SERVER_STATUS_UP);
    }
    
    server->timeouts = 0; // reset
    
    // Release application lock
    INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
}



/**
 * Callback for when we established a connection to the server 
 * 
 * @param[in] ctx
 *     The event context
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] fd
 *     The absolute time when the event is due (now)
 * 
 * @param[in] la
 *     Local address
 * 
 * @param[in] lalen
 *     Local address length
 * 
 * @param[in] ra
 *     Remote address
 * 
 * @param[in] ralen
 *     Remote address length 
 */
static void
http_connect(evContext ctx __unused, void * uap, int fd,
             const void *la __unused, int lalen __unused,
             const void *ra __unused, int ralen __unused)
{
    const char * GET_REQ = "GET / HTTP/1.1\nHost: %s\n\n";
    const uint8_t BUF_LEN = 255;
    char buf[BUF_LEN];
    mon_server_info_t * server;
    mon_app_info_t * app;
    struct in_addr addr;
    
    server = (mon_server_info_t *)uap;
    INSIST_ERR(server != NULL);
    
    app = server->app;
    
    bzero(buf, BUF_LEN);
    
    // Get application lock
    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);

    addr.s_addr = server->server_addr; // for inet_ntoa
    
    if(app->server_mon_params == NULL) {
        // Release application lock
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
        LOGM(LOG_ERR, "%s: Request to read HTTP response from %s in an "
                "application with no server monitoring parameters",
                __func__, inet_ntoa(addr));
        return;
    }
    
    evInitID(&server->conn_id); // reset this
    
    if(fd == -1) { // error occured and socket has been closed
        server->sock = -1;
        server_probe_failed(server);
        // Release application lock
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
        
        LOGM(LOG_ERR, "%s: Connection to server %s failed (Error %m)",
                __func__, inet_ntoa(addr));
        return;
    }
    
    LOGM(LOG_INFO, "%s: Connected to server %s. Sending HTTP Request...",
            __func__, inet_ntoa(addr));
    
    // setup reading callback
    evInitID(&server->file_id);
    if(evSelectFD(mon_ctx, server->sock, EV_READ, http_read, server,
            &server->file_id)) {
        
        LOGM(LOG_ERR, "%s: evSelectFD failed (Error: %m)", __func__);
        server_probe_failed(server);

    } else {
        // Send HTTP GET Request
        
        sprintf(buf, GET_REQ, inet_ntoa(addr));
        if(send(server->sock, buf, strlen(buf), 0) == -1) {

            LOGM(LOG_ERR, "%s: failed to send HTTP request to %s (Error: %m)",
                    __func__, inet_ntoa(addr));
            server_probe_failed(server);
        } else {
            LOGM(LOG_INFO, "%s: Sent HTTP Request to %s",
                __func__, inet_ntoa(addr));
        }
    }
    
    // Release application lock
    INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
}


/**
 * Timeout for the probe the server is hit
 * 
 * @param[in] ctx
 *     The event context for this application
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] due
 *     The absolute time when the event is due (now)
 * 
 * @param[in] inter
 *     The period; when this will next be called 
 */
static void
probe_timeout(evContext ctx __unused,
            void * uap,
            struct timespec due __unused,
            struct timespec inter __unused)
{
    mon_server_info_t * server = (mon_server_info_t *)uap;
    mon_app_info_t * app;
    struct in_addr addr;
    
    INSIST_ERR(server != NULL);
    app = server->app;
    
    // Get application lock
    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);

    addr.s_addr = server->server_addr; // for inet_ntoa
    
    if(app->server_mon_params == NULL) {
        // Release application lock
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
        LOGM(LOG_ERR, "%s: Probe timeout for server %s in an application with "
            "no server monitoring parameters", __func__, inet_ntoa(addr));
        return;
    }

    LOGM(LOG_INFO, "%s: Probe timeout for server %s",
            __func__, inet_ntoa(addr));
    
    server_probe_failed(server);
    
    // Release application lock
    INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
}


/**
 * Initialize an HTTP connection to the server.
 * Should have a lock on the application of the server to use this function.
 * 
 * @param[in] server
 *      The server info
 * 
 * @return SUCCESS upon successfully connecting
 */
static status_t
init_http_connection(mon_server_info_t * server)
{
    // for socket options
    const int sndbuf = 1024;           // send buffer
    const int on = 1;                  // to turn an option on
    
    struct sockaddr_in server_addr;
    struct in_addr addr;
    
    if(server->sock != -1) {
        LOGM(LOG_ERR, "%s: socket fd for server is already open", __func__);
        return EFAIL;
    }
    
    server->sock = socket(AF_INET, SOCK_STREAM, 0);
    if(server->sock == -1) {
        LOGM(LOG_ERR, "%s: socket() failed: %m", __func__);
        return EFAIL;        
    }

    // Set some socket options

    if(setsockopt(server->sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) {
        LOGM(LOG_ERR, "%s: setsockopt SO_REUSEADDR failed: %m", __func__);
        close(server->sock);
        server->sock = -1;
        return EFAIL;
    }
    if(setsockopt(server->sock, SOL_SOCKET, SO_SNDBUF, &sndbuf,sizeof(sndbuf))){
        LOGM(LOG_ERR, "%s: setsockopt SO_SNDBUF failed: %m", __func__);
        close(server->sock);
        server->sock = -1;
        return EFAIL;
    }

    bzero(&server_addr, sizeof(struct sockaddr_in));
    server_addr.sin_len = sizeof(struct sockaddr_in);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = server->app->key.app_port;     // in nw byte order
    server_addr.sin_addr.s_addr = server->server_addr;    // in nw byte order

    addr.s_addr = server->server_addr; // for inet_ntoa
    LOGM(LOG_INFO,"%s: Connecting to server %s", __func__, inet_ntoa(addr));
    
    // setup reading callback
    evInitID(&server->conn_id);
    if(evConnect(mon_ctx, server->sock, (struct sockaddr *)&server_addr,
        sizeof(struct sockaddr_in), http_connect, server, &server->conn_id)) {
        
        LOGM(LOG_ERR, "%s: Connecting to server %s failed (Error %m)",
                __func__, inet_ntoa(addr));
        close(server->sock);
        server->sock = -1;
        return EFAIL;
    }
    
    // schedule timeout for probe to server
    if(evSetTimer(mon_ctx, probe_timeout, server, evAddTime(evNowTime(),
            evConsTime(server->app->server_mon_params->connection_timeout, 0)),
            evConsTime(0, 0), &server->test_timer)) {
        
        LOGM(LOG_EMERG, "%s: Failed to initialize a probe timeout timer for "
            "probe to server %s (Error: %m)", __func__, inet_ntoa(addr));
    }
    
    return SUCCESS;
}


/**
 * Probe the server
 * 
 * @param[in] ctx
 *     The event context for this application
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] due
 *     The absolute time when the event is due (now)
 * 
 * @param[in] inter
 *     The period; when this will next be called 
 */
static void
probe_server(evContext ctx __unused,
            void * uap,
            struct timespec due __unused,
            struct timespec inter __unused)
{
    mon_server_info_t * server = (mon_server_info_t *)uap;
    mon_app_info_t * app;
    struct in_addr addr;
    
    INSIST_ERR(server != NULL);
    app = server->app;
    
    // Get application lock
    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
    
    if(evTestID(server->test_timer)) {
        evClearTimer(mon_ctx, server->test_timer);
        evInitID(&server->test_timer);
    }
    
    if(app->server_mon_params == NULL) {
        addr.s_addr = server->server_addr; // for inet_ntoa
        // Release application lock
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
        LOGM(LOG_ERR, "%s: Request to probe server %s in an application with no "
            "server monitoring parameters", __func__, inet_ntoa(addr));
        return;
    }
    
    if(init_http_connection(server) != SUCCESS) {
        server_probe_failed(server);
    }
    
    // Release application lock
    INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
}


/**
 * Dummy keep-alive function
 * 
 * @param[in] ctx
 *     The event context for this application
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] due
 *     The absolute time when the event is due (now)
 * 
 * @param[in] inter
 *     The period; when this will next be called 
 */
static void
keep_alive(evContext ctx __unused,
           void * uap __unused,
           struct timespec due __unused,
           struct timespec inter __unused)
{

}


/**
 * Run the server monitor main loop
 * 
 * @param[in] params
 *      parameters passed to pthread create
 * 
 * @return NULL
 */
static void *
start_monitor(void * params __unused)
{
    const int KEEP_ALIVE = 5; //900;
    evEvent event;
    int rc = 0;
    
    INSIST_ERR(evCreate(&mon_ctx) != -1);
    
    // Give the context a dummy event to keep it alive
    if(evSetTimer(mon_ctx, keep_alive, NULL, evConsTime(0, 0),
            evConsTime(KEEP_ALIVE, 0), NULL)) {
        LOGM(LOG_EMERG, "%s: Failed to initialize main loop's keep alive timer",
            __func__);
        return NULL;
    }
    
    // Go to work, wait for events
    // This is exactly like evMainLoop except for doShutdown
    while (!doShutdown && ((rc = evGetNext(mon_ctx, &event, EV_WAIT)) == 0)) {
        if((rc = evDispatch(mon_ctx, event)) < 0) {
            break;
        }
    }
    
    evDestroy(mon_ctx);
    
    if(doShutdown) {
        LOGM(LOG_INFO, "Shutting down");
    } else {
        LOGM(LOG_ERR, "Unexpected exit from main loop: %d", rc);
    }
    
    return NULL;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init the data structures that will store configuration info
 * 
 * @param[in] ctx
 *      event context
 * 
 * @param[in] cpu
 *      cpu to use or MSP_NEXT_END run on current event context and cpu
 *
 * @return 
 *      SUCCESS if successful; otherwise EFAIL with an error message.
 */
status_t
init_monitor(evContext ctx, int cpu)
{
    pthread_attr_t attr;
    pthread_t tid;
    
    msp_spinlock_init(&apps_big_lock);
    
    patricia_root_init(&apps, FALSE, sizeof(mon_app_key_t), 0);
                   // root, is key ptr, key size, key offset
    
    if(cpu == MSP_NEXT_END) {
        // There's no other user CPU to bind to, so use whatever we are
        // currently bound to  
        mon_ctx = ctx; // use the main event context
        return SUCCESS;
    }
    
    // create a new thread for the monitor and bind it to the available user CPU
    
    LOG(LOG_INFO, "%s: Starting monitor on user cpu %d", __func__, cpu);
    
    doShutdown = FALSE;
    
    // Set up pthread attributes
    pthread_attr_init(&attr);
    
    // schedule with respect to all threads in the system, not process (default)
    if(pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM)) {
        LOG(LOG_ERR, "%s: pthread_attr_setscope(PTHREAD_SCOPE_SYSTEM) failed",
                __func__);
        pthread_attr_destroy(&attr);
        return EFAIL;
    }
    
    // bind monitor thread to the user CPU
    if(pthread_attr_setcpuaffinity_np(&attr, cpu)) {
        LOG(LOG_ERR, "%s: pthread_attr_setcpuaffinity_np on CPU %d failed",
                __func__, cpu);
        pthread_attr_destroy(&attr);
        return EFAIL;
    }
    
    // create and start thread
    if(pthread_create(&tid, &attr, start_monitor, NULL)) {
        LOG(LOG_ERR, "%s: pthread_create() on CPU %d failed",
                __func__, cpu);
        pthread_attr_destroy(&attr);
        return EFAIL;
    }
    
    pthread_attr_destroy(&attr);
    
    return SUCCESS;
}


/**
 * Shutdown the monitor
 */
void
shutdown_monitor(void)
{
    mon_app_info_t * app;
    mon_server_info_t * server;
    
    doShutdown = TRUE;
    
    // delete all applications
    
    // Get big lock
    INSIST_ERR(msp_spinlock_lock(&apps_big_lock) == MSP_OK);
   
    while((app = app_entry(patricia_find_next(&apps, NULL))) != NULL) {
        
        // Get application lock
        INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
        
        while((server = TAILQ_FIRST(app->up_servers)) != NULL) {
            stop_server_probes(server);
            TAILQ_REMOVE(app->up_servers, server, entries);
            free(server);
        }
        free(app->up_servers);
        
        while((server = TAILQ_FIRST(app->down_servers)) != NULL) {
            stop_server_probes(server);
            TAILQ_REMOVE(app->up_servers, server, entries);
            free(server);
        }
        free(app->down_servers);
        
        patricia_delete(&apps, &app->node);
        
        free(app);
    }
    
    // Release big lock
    INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
}


/**
 * Add a server to monitor.
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 * 
 * @param[in] server_addr
 *      The server address, of the server within that application
 *
 * @param[in] monitor
 *      The server monitoring parameters (all servers in the same 
 *      application must have the same monitor). If NULL, then server 
 *      is not monitored and assumed UP.
 */
void
monitor_add_server(uint16_t ss_id,
                   in_addr_t app_addr,
                   uint16_t app_port,
                   in_addr_t server_addr,
                   eq_smon_t * monitor)
{
    mon_app_key_t key;
    mon_app_info_t * app;
    mon_server_info_t * server;
    struct in_addr addr;
    
    bzero(&key, sizeof(mon_app_key_t));
    key.svc_set_id = ss_id;
    key.app_addr = app_addr;
    key.app_port = app_port;
    
    // Get big lock
    INSIST_ERR(msp_spinlock_lock(&apps_big_lock) == MSP_OK);
    
    // get the application
    app = app_entry(patricia_get(&apps, sizeof(key), &key));
    
    if(app == NULL) { // new app
        
        app = calloc(1, sizeof(mon_app_info_t));
        INSIST_ERR(app != NULL);
        
        app->key.svc_set_id = ss_id;
        app->key.app_addr = app_addr;
        app->key.app_port = app_port;
        
        // init a bit
        msp_spinlock_init(&app->app_lock);
        // Get application lock
        INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);

        if(!patricia_add(&apps, &app->node)) {
            // Release big lock
            INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
            
            LOG(LOG_ERR, "%s: Failed to add application to monitor config",
                    __func__);
            free(app);
            return;
        }
        
        // Release big lock
        INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
        
        app->server_mon_params = monitor;
        
        app->down_servers = malloc(sizeof(server_info_set_t));
        INSIST_ERR(app->down_servers != NULL);
        TAILQ_INIT(app->down_servers);
        
        app->up_servers = malloc(sizeof(server_info_set_t));
        INSIST_ERR(app->up_servers != NULL);
        TAILQ_INIT(app->up_servers);
        
    } else { // app exists already
        
        // Get application lock
        INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
        
        // Release big lock
        INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
        
        
        // check for the server in one of the sets
        if(app->up_servers) {
            server = TAILQ_FIRST(app->up_servers);
            while(server != NULL) {
                if(server->server_addr == server_addr) {
                    LOG(LOG_ERR, "%s: Server already exists in monitor config",
                            __func__);
                    // Release application lock
                    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
                    return;
                }
                server = TAILQ_NEXT(server, entries);            
            }
        }
        
        // check for the server in one of the sets
        if(app->down_servers) {
            server = TAILQ_FIRST(app->down_servers);
            while(server != NULL) {
                if(server->server_addr == server_addr) {
                    LOG(LOG_ERR, "%s: Server already exists in monitor config",
                            __func__);
                    // Release application lock
                    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
                    return;
                }
                server = TAILQ_NEXT(server, entries);            
            }
        }
        
        // check monitor
        if(monitor != app->server_mon_params) {
            // change_monitor_config should have been used first
            LOG(LOG_ERR, "%s: monitor configuration does not match existing "
                    "monitor configuration for this application", __func__);
            // Release application lock
            INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
            return;
        }
    }
    
    // add the server
    server = calloc(1, sizeof(mon_server_info_t));
    INSIST_ERR(server != NULL);
    server->server_addr = server_addr;
    server->app = app;
    server->sock = -1;
    
    // if monitor is NULL then it is assumed up, otherwise it starts down
    if(monitor != NULL) {
        server->is_up = FALSE;
        TAILQ_INSERT_TAIL(app->down_servers, server, entries);
        
        // schedule probe to server immediately
        if(evSetTimer(mon_ctx, probe_server, server, evConsTime(0, 0),
                evConsTime(0, 0), &server->test_timer)) {
            addr.s_addr = server_addr; // for inet_ntoa
            LOG(LOG_EMERG, "%s: Failed to initialize a probe timer to "
                "probe server %s (Error: %m)", __func__, inet_ntoa(addr));
        }
    } else {
        server->is_up = TRUE;
        // insert at the head since it is sorted by server->sessions
        TAILQ_INSERT_HEAD(app->up_servers, server, entries);
        
        addr.s_addr = server_addr; // for inet_ntoa
        LOG(LOG_INFO, "%s: Added (permanently UP) server %s",
                __func__, inet_ntoa(addr));
        
        // no probe to setup
    }
    
    // Release application lock
    INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
}


/**
 * Remove the server from the monitor's configuration.
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 * 
 * @param[in] server_addr
 *      The server address, of the server within that application
 */
void
monitor_remove_server(uint16_t ss_id,
                      in_addr_t app_addr,
                      uint16_t app_port,
                      in_addr_t server_addr)
{
    boolean found = FALSE;
    mon_app_key_t key;
    mon_app_info_t * app;
    mon_server_info_t * server;
    
    bzero(&key, sizeof(mon_app_key_t));
    key.svc_set_id = ss_id;
    key.app_addr = app_addr;
    key.app_port = app_port;
    
    // Get big lock
    INSIST_ERR(msp_spinlock_lock(&apps_big_lock) == MSP_OK);
    
    // get the application
    app = app_entry(patricia_get(&apps, sizeof(key), &key));
    
    if(app == NULL) {
        // Release big lock
        INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
        
        LOG(LOG_ERR, "%s: Failed to get application in the monitor config",
                __func__);
        return;        
    }
    
    // Get application lock
    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
    
    // Release big lock
    INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
    
    // check for the server in one of the sets
    server = TAILQ_FIRST(app->up_servers);
    while(server != NULL) {
        if(server->server_addr == server_addr) {
            found = TRUE;
            TAILQ_REMOVE(app->up_servers, server, entries);
            break;
        }
        server = TAILQ_NEXT(server, entries);            
    }
    
    if(!found) {
        server = TAILQ_FIRST(app->down_servers);
        while(server != NULL) {
            if(server->server_addr == server_addr) {
                found = TRUE;
                TAILQ_REMOVE(app->down_servers, server, entries);
                break;
            }
            server = TAILQ_NEXT(server, entries);            
        }
        if(!found) { // still not here
            // Release application lock
            INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
            
            LOG(LOG_ERR, "%s: Failed to find the server in the monitor config",
                    __func__);
            return;
        }
    }
    
    stop_server_probes(server);
    
    clean_sessions_using_server(app->key.svc_set_id,
            app->key.app_addr, app->key.app_port, server->server_addr);
    
    free(server);
    
    // check if we have any servers left
    if(TAILQ_EMPTY(app->up_servers) && TAILQ_EMPTY(app->down_servers)) {
        // no servers left, delete app config
        
        // preserve lock ordering chain of locks (big to small/app)...
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
        INSIST_ERR(msp_spinlock_lock(&apps_big_lock) == MSP_OK);
        INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
        
        if(!patricia_delete(&apps, &app->node)) {
            // Release big lock
            INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
            LOG(LOG_ERR, "%s: Failed to remove app from monitor config",
                    __func__);
            return;
        }

        // Release big lock
        INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
        
        free(app->down_servers);
        free(app->up_servers);
        free(app);
    } else {
        // Release application lock
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
    }
}


/**
 * Remove all servers from the monitor's configuration matching this application
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 */
void
monitor_remove_all_servers_in_app(uint16_t ss_id,
                                  in_addr_t app_addr,
                                  uint16_t app_port)
{
    mon_app_key_t key;
    mon_app_info_t * app;
    mon_server_info_t * server;
    
    bzero(&key, sizeof(mon_app_key_t));
    key.svc_set_id = ss_id;
    key.app_addr = app_addr;
    key.app_port = app_port;
    
    // Get big lock
    INSIST_ERR(msp_spinlock_lock(&apps_big_lock) == MSP_OK);
    
    // get the application
    app = app_entry(patricia_get(&apps, sizeof(key), &key));
    
    if(app == NULL) {
        // Release big lock
        INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
        
        LOG(LOG_ERR, "%s: Failed to get application in the monitor config",
                __func__);
        return;        
    }
    
    // Get application lock
    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
    
    // get rid of this app from the monitor's config, first so we can unlock
    if(!patricia_delete(&apps, &app->node)) {
        // Release both locks
        INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
        
        LOG(LOG_ERR, "%s: Failed to remove app from monitor config",
                __func__);
        return;
    }

    // Release big lock
    INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
    
    // empty both sets (up and down) deleting all servers
    while((server = TAILQ_FIRST(app->up_servers)) != NULL) {
        stop_server_probes(server);
        TAILQ_REMOVE(app->up_servers, server, entries);
        free(server);
    }
    while((server = TAILQ_FIRST(app->down_servers)) != NULL) {
        stop_server_probes(server);
        TAILQ_REMOVE(app->down_servers, server, entries);
        free(server);
    }
    
    clean_sessions_with_app(
            app->key.svc_set_id, app->key.app_addr, app->key.app_port);
    
    free(app->down_servers);
    free(app->up_servers);
    free(app);
}


/**
 * Remove all servers from the monitor's configuration matching this service set
 * 
 * @param[in] ss_id
 *      service-set id
 */
void
monitor_remove_all_servers_in_service_set(uint16_t ss_id)
{
    mon_app_info_t * app;
    mon_server_info_t * server;
    
    struct tmp_app_s {
        mon_app_info_t         * app;
        TAILQ_ENTRY(tmp_app_s) entries;
    }  * tmp_app;

    TAILQ_HEAD(, tmp_app_s) app_list = TAILQ_HEAD_INITIALIZER(app_list);
    
    // Get big lock
    INSIST_ERR(msp_spinlock_lock(&apps_big_lock) == MSP_OK);
    
    // go thru all the applications with this service set id
    while((app = app_entry(patricia_subtree_match(
            &apps, sizeof(uint16_t)*8, &ss_id))) != NULL) {

        // Get application lock
        INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
        
        // get rid of this app from the monitor's config
        if(!patricia_delete(&apps, &app->node)) {
            // Release app lock
            INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
            LOG(LOG_ERR, "%s: Failed to remove app from monitor config",
                    __func__);
        }
        
        tmp_app = malloc(sizeof(struct tmp_app_s));
        INSIST_ERR(tmp_app != NULL);
        tmp_app->app = app;
        TAILQ_INSERT_TAIL(&app_list, tmp_app, entries);
    }

    // Release big lock
    INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
    
    if(!TAILQ_EMPTY(&app_list)) {
        clean_sessions_with_service_set(ss_id);
    }
    
    // delete all apps in app_list
    while((tmp_app = TAILQ_FIRST(&app_list)) != NULL) {
        
        app = tmp_app->app;
        
        // empty both sets (up and down) deleting all servers
        while((server = TAILQ_FIRST(app->up_servers)) != NULL) {
            stop_server_probes(server);
            TAILQ_REMOVE(app->up_servers, server, entries);
            free(server);
        }
        while((server = TAILQ_FIRST(app->down_servers)) != NULL) {
            stop_server_probes(server);
            TAILQ_REMOVE(app->down_servers, server, entries);
            free(server);
        }
        
        TAILQ_REMOVE(&app_list, tmp_app, entries);
        free(app->down_servers);
        free(app->up_servers);
        free(app);
        free(tmp_app);
    }
}


/**
 * Change the monitoring parameters for all servers.
 * Does not free the pointer to the old monitor.
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 * 
 * @param[in] monitor
 *      The server monitoring parameters (all servers in the same 
 *      application use the same monitor). If NULL, then server 
 *      is not monitored and assumed UP.
 */
void
change_monitoring_config(uint16_t ss_id,
                         in_addr_t app_addr,
                         uint16_t app_port,
                         eq_smon_t * monitor)
{
    mon_app_key_t key;
    mon_app_info_t * app;
    mon_server_info_t * server, * tmp;
    struct in_addr addr;
    
    bzero(&key, sizeof(mon_app_key_t));
    key.svc_set_id = ss_id;
    key.app_addr = app_addr;
    key.app_port = app_port;
    
    // Get big lock
    INSIST_ERR(msp_spinlock_lock(&apps_big_lock) == MSP_OK);
    
    // get the application
    app = app_entry(patricia_get(&apps, sizeof(key), &key));
    
    if(app == NULL) {
        // Release big lock
        INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
        
        LOG(LOG_ERR, "%s: Failed to get application in the monitor config",
                __func__);
        return;
    }
    
    // Get application lock
    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
    
    // Release big lock
    INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
    
    
    // switch the server monitoring parameters
    // (we don't need to free since the config.c module holds the mem for this)
    
    app->server_mon_params = monitor;
    
    // Go through servers, cancel the existing timers and setup the new monitor
    // Leaving the servers in whatever set they were already in seems fine and
    // causes less disruption
    
    server = TAILQ_FIRST(app->up_servers);
    while(server != NULL) {
        // cancel anything currently going on
        stop_server_probes(server);
        
        if(monitor != NULL) {
            // schedule probe to server immediately
            if(evSetTimer(mon_ctx, probe_server, server, evConsTime(0, 0),
                    evConsTime(0, 0), &server->test_timer)) {
                addr.s_addr = server->server_addr; // for inet_ntoa
                LOG(LOG_EMERG, "%s: Failed to initialize a probe timer to probe "
                    "server %s (Error: %m)", __func__, inet_ntoa(addr));
            }
        }
        server = TAILQ_NEXT(server, entries);            
    }
    
    server = TAILQ_FIRST(app->down_servers);
    while(server != NULL) {
        
        // cancel anything currently going on
        stop_server_probes(server);
        
        if(monitor != NULL) {
            
            // schedule probe to server immediately
            if(evSetTimer(mon_ctx, probe_server, server, evConsTime(0, 0),
                    evConsTime(0, 0), &server->test_timer)) {
                addr.s_addr = server->server_addr; // for inet_ntoa
                LOG(LOG_EMERG, "%s: Failed to initialize a probe timer to probe "
                    "server %s (Error: %m)", __func__, inet_ntoa(addr));
            }
            
            server = TAILQ_NEXT(server, entries);

        } else { // no monitor so assume everything is up
            
            tmp = TAILQ_NEXT(server, entries); // save next
            TAILQ_REMOVE(app->down_servers, server, entries);
            
            // insert at head since sessions should be zero
            // (and up_servers is sorted by sessions)
            server->sessions = 0; // enforce
            server->is_up = TRUE;
            TAILQ_INSERT_HEAD(app->up_servers, server, entries);
            
            addr.s_addr = server->server_addr; // for inet_ntoa
            LOG(LOG_INFO, "%s: Added (permanently UP) server %s",
                    __func__, inet_ntoa(addr));
            
            server = tmp; // copy back next server
        }    
    }
    
    // Release application lock
    INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
}


/**
 * Get the server with the least load for this application
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 * 
 * @return the server address if the application exists and has any up servers.
 *    returns 0 if the application does not exist and,
 *            (in_addr_t)-1 if there are no "up" servers but the app exists
 */
in_addr_t
monitor_get_server_for(uint16_t ss_id,
                       in_addr_t app_addr,
                       uint16_t app_port)
{
    mon_app_key_t key;
    mon_app_info_t * app;
    mon_server_info_t * server, *tmp, *tmp2;
    
    bzero(&key, sizeof(mon_app_key_t));
    key.svc_set_id = ss_id;
    key.app_addr = app_addr;
    key.app_port = app_port;
    
    // Get big lock
    INSIST_ERR(msp_spinlock_lock(&apps_big_lock) == MSP_OK);
    
    // get the application
    app = app_entry(patricia_get(&apps, sizeof(key), &key));
    
    if(app == NULL) {
        // Release big lock
        INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
        return 0;
    }
    
    // Get application lock
    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
    
    // Release big lock
    INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
    
   
    // Get the first server, since it should have the least load
    
    server = TAILQ_FIRST(app->up_servers);
    if(server == NULL) {
        return (in_addr_t)-1;
    }
    ++server->sessions;
    
    // Go through the up servers and preserve the ordering by load 
    
    tmp2 = tmp = TAILQ_NEXT(server, entries);
    while(tmp != NULL) {
        if(tmp->sessions < server->sessions) {
            tmp = TAILQ_NEXT(tmp, entries);
        } else {
            if(tmp != tmp2) {
                // insert server here
                TAILQ_REMOVE(app->up_servers, server, entries);
                TAILQ_INSERT_BEFORE(tmp, server, entries);
            }
            // else don't need to move it
            break;
        }
    }
    // if we went through all of them
    if(tmp == NULL) {
        // insert server at start
        TAILQ_REMOVE(app->up_servers, server, entries);
        TAILQ_INSERT_TAIL(app->up_servers, server, entries);
    }
    
    // Release application lock
    INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
    
    return server->server_addr;
}


/**
 * Find the server in the app, and reduce the load by one
 * 
 * @param[in] ss_id
 *      service-set id
 * 
 * @param[in] app_addr
 *      Application address, used to identify the application
 * 
 * @param[in] app_port
 *      Application port
 * 
 * @param[in] server_addr
 *      Server address
 */
void
monitor_remove_session_for_server(uint16_t ss_id,
                                  in_addr_t app_addr,
                                  uint16_t app_port,
                                  in_addr_t server_addr)
{
    mon_app_key_t key;
    mon_app_info_t * app;
    mon_server_info_t * server, *tmp, *tmp2;
    
    bzero(&key, sizeof(mon_app_key_t));
    key.svc_set_id = ss_id;
    key.app_addr = app_addr;
    key.app_port = app_port;
    
    // Get big lock
    INSIST_ERR(msp_spinlock_lock(&apps_big_lock) == MSP_OK);
    
    // get the application
    app = app_entry(patricia_get(&apps, sizeof(key), &key));
    
    if(app == NULL) {
        // Release big lock
        INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
        return;
    }
    
    // Get application lock
    INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
    
    // Release big lock
    INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
   
    server = TAILQ_FIRST(app->up_servers);

    while(server != NULL) {
        if(server->server_addr == server_addr) {
            --server->sessions;
            
            // Go through the up servers and preserve the ordering by load
            tmp2 = tmp = TAILQ_PREV(server, server_info_set_s, entries);
            while(tmp != NULL) {
                if(tmp->sessions > server->sessions) {
                    tmp = TAILQ_PREV(tmp, server_info_set_s, entries);
                } else {
                    if(tmp != tmp2) {
                        // insert server here
                        TAILQ_REMOVE(app->up_servers, server, entries);
                        TAILQ_INSERT_AFTER(app->up_servers, 
                                tmp, server, entries);
                    }
                    // else don't need to move it
                    break;
                }
            }
            // if we went through all of them
            if(tmp == NULL) {
                // insert server at start
                TAILQ_REMOVE(app->up_servers, server, entries);
                TAILQ_INSERT_HEAD(app->up_servers, server, entries);
            }
            
            break;
        }
        server = TAILQ_NEXT(server, entries);
    }
    
    // Release application lock
    INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
}


/**
 * Send server/application load stats to the mgmt component 
 */
void
monitor_send_stats(void)
{
    mon_app_info_t * app;
    mon_server_info_t * server;
    uint32_t session_count;
    
    // Get big lock
    INSIST_ERR(msp_spinlock_lock(&apps_big_lock) == MSP_OK);
    
    app = app_entry(patricia_find_next(&apps, NULL));
    
    while(app != NULL) { // go thru all apps
        
        // Get application lock
        INSIST_ERR(msp_spinlock_lock(&app->app_lock) == MSP_OK);
        
        session_count = 0;
        
        server = TAILQ_FIRST(app->up_servers);
        while(server != NULL) { // go thru all servers
            
            session_count += server->sessions; // add to total for app
            
            server = TAILQ_NEXT(server, entries);
        }
        
        // report
        notify_application_sessions(app->key.svc_set_id,
                app->key.app_addr, app->key.app_port, session_count);
        
        
        // Release application lock
        INSIST_ERR(msp_spinlock_unlock(&app->app_lock) == MSP_OK);
        
        app = app_entry(patricia_find_next(&apps, &app->node));       
    }
    
    // Release big lock
    INSIST_ERR(msp_spinlock_unlock(&apps_big_lock) == MSP_OK);
}


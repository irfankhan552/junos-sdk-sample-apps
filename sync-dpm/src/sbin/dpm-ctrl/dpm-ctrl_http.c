/*
 * $Id: dpm-ctrl_http.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file dpm-ctrl_http.c
 * @brief Relating to the DPM's HTTP server 
 *
 * 
 * The DPM runs an HTTP server in the dpm-ctrl_http module on a separate thread.
 * The public functions provide a way to start and stop the HTTP server.
 */


#include <dirent.h>
#include <pthread.h>
#include <mihl.h>
#include "dpm-ctrl_main.h"
#include "dpm-ctrl_config.h"
#include "dpm-ctrl_http.h"

/*** Constants ***/

/**
 * The port that the DPM's public HTTP server runs on 
 */
#define DPM_HTTP_PORT 80

/**
 * The maximum number of concurrently accepted connections to the HTTP server 
 */
#define DPM_HTTP_MAX_CONNECTIONS 400

/**
 * The logging level to use with the HTTP server (library) 
 */
#define HTTP_SERVER_LOG_LEVEL \
    (MIHL_LOG_ERROR | MIHL_LOG_WARNING | MIHL_LOG_INFO | MIHL_LOG_INFO_VERBOSE)

/**
 * HTML content type header
 */
#define HTML_CONTENT "Content-type: text/html\r\n"


/*** Data structures: ***/

/**
 * Whether the server is shutdown or (for the server thread) needs shutting down
 */
static volatile uint8_t shutdown_server = 1;
static pthread_t        http_thread;    ///< thread of the HTTP server 
static pthread_mutex_t  suspend_lock;   ///< lock for running server
extern int http_cpu_num;                ///< CPU setup for use of HTTP server

/**
 * Used to control output on the standard webpage
 */
typedef enum {
    LOGGED_OUT = 1,  ///< display login page
    LOGGED_IN,       ///< display logout page
    BAD_PASSWORD,    ///< display login page, with error msg
    LOGIN_SUCCESSFUL ///< display logout page, with success msg
} status_e;

/*** STATIC/INTERNAL Functions ***/


/**
 * Display standard HTML page and write it in the HTTP response using
 * the mihl_add function
 * 
 * @param cnx
 *      Opaque context structure as returned by mihl_init()
 * 
 * @param is_logged_in
 *      True if the user is logged in (will display log out page), and 
 *      False if the user is not logged in (will display login page)
 */
static void
write_page(mihl_cnx_t * cnx, status_e status)
{
    mihl_add(cnx,
        "<!DOCTYPE html PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\" \"http://www.w3.org/TR/html4/loose.dtd\">"
        "<html>"
        "<head>"
        "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=ISO-8859-1\">"
        "<title>SYNC Dynamic Policy Manager - Portal</title>"
        "<style>"
        "body {"
        "    background-color: #666666;"
        "    font-family: Arial, sans-serif;"
        "    font-size: 10pt;"
        "}"
        "#content {"
        "    background-color: #FFFFFF;"
        "    border: 1px outset #000000;"
        "    height: 300px;"
        "    min-width: 700px;"
        "    width: 700px;"
        "    padding: 10px;"
        "}"
        "</style>"
        "</head>"
        "<body>"
        "<div align=\"center\">"
        "  <div id=\"content\" align=\"left\">"
        "    <p align=\"center\"><strong><u>Welcome to the SDK Your Net Corporation (SYNC)" 
        "    Dynamic Policy Manager Example Portal</u></strong></p>"
        "    <p>&nbsp;</p>");
    
    if(status == LOGGED_IN || status == LOGIN_SUCCESSFUL) {
        
        mihl_add(cnx,
            "    <p>Using this site you may repudiate your own traffic policy by logging in.</p>");

        if(status == LOGIN_SUCCESSFUL) {
            mihl_add(cnx,
                    "    <p style=\"color:green\">Login Succesful.</p>");
        }
        
        mihl_add(cnx,
            "    <p><u><b>Subscriber Sign-out</b></u>"
            "        <form name=\"signout\" action=\"/logout.html\" method=\"post\">"
            "        <input value=\" Sign Out \" tabindex=\"1\" type=\"submit\"><br>");
    } else {
        mihl_add(cnx,
            "    <p>Using this site you may authorize your own traffic policy by logging in.</p>");
        
        if(status == BAD_PASSWORD) {
            mihl_add(cnx,
                    "    <p style=\"color:red\">Incorrect username or password.</p>");
        }
        
        mihl_add(cnx,
            "    <p><u><b>Subscriber Sign-in</b></u>"
            "        <form name=\"signin\" action=\"/login.html\" method=\"post\">"
            "        Username:<br><input name=\"username\" id=\"username\" tabindex=\"1\" size=\"21\" type=\"text\"><br><br>"
            "        Password:<br><input name=\"password\" id=\"password\" tabindex=\"2\" size=\"21\" type=\"password\">"
            "        <br><br>"
            "        <input value=\" Sign In \" tabindex=\"3\" type=\"submit\"><br>");
    }

    mihl_add(cnx,
        "        </form>"
        "    </p>"
        "  </div>"
        "</div>"
        "</body>"
        "</html>");
}


/**
 * Send a page with a script to redirect to the DPM's home page
 * Alternatively, we could send an HTTP MOVED, but this library doesn't support
 * that currently.
 * 
 * @param cnx
 *      Opaque context structure as returned by mihl_init()
 * 
 * @param tag
 *      URL
 * 
 * @param host
 *      The connecting host
 * 
 * @param param
 *      The user data registers for the callback 
 * 
 * @return
 *      the result of mihl_send
 */
static int
logout(mihl_cnx_t * cnx,
       char const * tag,
       char const * host,
       int num_vars __unused,
       char ** var_names __unused,
       char ** var_values __unused,
       void * param __unused)
{
    
    struct in_addr client_address;
    int rc;
    
    rc = inet_aton(host, &client_address); // client_address is nw-byte order
    if(rc != 1) {
        LOG(LOG_ERR, "%s: Couldn't parse host IP: %s", __func__, host);
        return 0;
    }
    
    LOG(LOG_INFO, "%s: %s (%s)", __func__, tag, host);
    
    if(user_logged_in(client_address.s_addr)) {
        
        remove_policy(client_address.s_addr);
        
    } else {
        LOG(LOG_WARNING, "%s: Got a logout request for a user that "
                "isn't logged in: %s", __func__, host);
    }
    
    write_page(cnx, LOGGED_OUT);
    
    return mihl_send(cnx, NULL, HTML_CONTENT);
}


/**
 * Send a page with a script to redirect to the DPM's home page
 * Alternatively, we could send an HTTP MOVED, but this library doesn't support
 * that currently.
 * 
 * @param cnx
 *      Opaque context structure as returned by mihl_init()
 * 
 * @param tag
 *      URL
 * 
 * @param host
 *      The connecting host
 * 
 * @param param
 *      The user data registers for the callback
 * 
 * @return
 *      the result of mihl_send
 */
static int
login(mihl_cnx_t * cnx,
      char const * tag,
      char const * host,
      int num_vars,
      char ** var_names,
      char ** var_values,
      void * param __unused)
{
    /********************
    const char * sep = "/?&";
    char url[1024], * token,
    ********************/
    char * username, * password;
    struct in_addr client_address;
    int rc, i;
    
    rc = inet_aton(host, &client_address); // client_address is nw-byte order
    if(rc != 1) {
        LOG(LOG_ERR, "%s: Couldn't parse host IP: %s", __func__, host);
        return 0;
    }
    
    LOG(LOG_INFO, "%s: %s (%s) (numvars: %d)", __func__, tag, host, num_vars);
    
    // parse tag/URL for username/password to validate
    username = password = NULL;
    
    for(i = 0; i < num_vars; ++i) {
        if(strcmp(var_names[i], "username") == 0) {
            username = var_values[i];
        } else if(strcmp(var_names[i], "password") == 0) {
            password = var_values[i];
        }
    }
    
    // validate
    if(username == NULL || password == NULL ||
            !validate_credentials(username, password)) {
        write_page(cnx, BAD_PASSWORD);
    } else {
        // add policy for this user
        apply_policy(username, client_address.s_addr);
        write_page(cnx, LOGIN_SUCCESSFUL);
    }
    
    return mihl_send(cnx, NULL, HTML_CONTENT);
}


/**
 * Send a page with a script to redirect to the DPM's home page
 * Alternatively, we could send an HTTP MOVED, but this library doesn't support
 * that currently.
 * 
 * @param cnx
 *      Opaque context structure as returned by mihl_init()
 * 
 * @param tag
 *      URL
 * 
 * @param host
 *      The connecting host
 * 
 * @param param
 *      The user data registers for the callback
 * 
 * @return
 *      the result of mihl_send
 */
static int
check_access(mihl_cnx_t * cnx,
             char const * tag,
             char const * host,
             void * param __unused)
{
    struct in_addr client_address;
    int rc;
    
    LOG(LOG_INFO, "%s: %s (%s)", __func__, tag, host);
    
    rc = inet_aton(host, &client_address); // client_address is nw-byte order
    if(rc != 1) {
        LOG(LOG_ERR, "%s: Couldn't parse host IP: %s", __func__, host);
        return 0;
    }
    
    write_page(cnx,
            (user_logged_in(client_address.s_addr)) ? LOGGED_IN : LOGGED_OUT);
    
    return mihl_send(cnx, NULL, HTML_CONTENT);
}


/**
 * Run the HTTP server
 * 
 * @param[in] params
 *      parameters passed to pthread create
 * 
 * @return NULL
 */
static void *
run_http_server(void * params __unused)
{
    mihl_ctx_t * ctx;
    sigset_t sig_mask;

    LOG(LOG_INFO, "%s: HTTP server thread alive", __func__);
    
    // Block SIGTERM to this thread/main thread will handle otherwise we inherit  
    // this behaviour in our threads sigmask and the signal might come here
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sig_mask, NULL);
    
    // initial a server and get the server context
    ctx = mihl_init("0.0.0.0", DPM_HTTP_PORT,
            DPM_HTTP_MAX_CONNECTIONS, HTTP_SERVER_LOG_LEVEL);
    
    if (!ctx) {
        LOG(LOG_ERR, "%s: Failed to start HTTP server using library",
            __func__);
        pthread_exit((void *) 0);
        return NULL;
    }

    LOG(LOG_INFO, "%s: Started HTTP server", __func__);
    
    // register default handler
    mihl_handle_get(ctx, NULL, check_access, NULL);
    
    // register other handlers to control access
    mihl_handle_post(ctx, "/login.html", login, NULL);
    mihl_handle_post(ctx, "/logout.html", logout, NULL);

    // MAIN LOOP
    while(!shutdown_server) {
        // don't serve while suspended
        INSIST_ERR(pthread_mutex_lock(&suspend_lock) == 0);
        
        mihl_server(ctx); // serve pages and accept/terminate connections
        
        INSIST_ERR(pthread_mutex_unlock(&suspend_lock) == 0);
    }
    
    mihl_end(ctx);
    
    LOG(LOG_INFO, "%s: HTTP server thread exiting", __func__);

    pthread_exit((void *) 0); // explicitly return to join
    return NULL;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize and start the HTTP server on a separate thread
 */
void
init_http_server(void)
{
    pthread_attr_t attr;
    int rc;
    
    pthread_mutex_init(&suspend_lock, NULL);
    
    if(!shutdown_server) { // if server is running
        shutdown_http_server();
    }

    /* Initialize and set thread detached attribute */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setcpuaffinity_np(&attr, http_cpu_num);

    shutdown_server = 0; // server starting
    
    LOG(LOG_INFO, "%s: Staring DPM's HTTP server", __func__);
    
    rc = pthread_create(&http_thread, NULL, run_http_server, NULL);
    if(rc) {
        LOG(LOG_ERR, "%s: Failed to start http server thread (%d)",
            __func__, rc);
        shutdown_server = 1; // still shutdown
    }
    
    pthread_attr_destroy(&attr);
}


/**
 * Suspend the HTTP server. May block.
 */
void
suspend_http_server(void)
{
    int rc;
    
    rc = pthread_mutex_lock(&suspend_lock);
    
    if(rc != 0) {
        LOG(LOG_ERR, "%s: Failed to suspend http server %d", __func__, rc);
    }
}


/**
 * Resume the HTTP server after reconfiguration.
 * Can only be called if suspended with suspend_http_server.
 */
void
resume_http_server(void)
{
    int rc;
    
    rc = pthread_mutex_unlock(&suspend_lock);
    
    if(rc != 0) {
        LOG(LOG_ERR, "%s: Failed to resume http server %d", __func__, rc);
    }
}


/**
 * Stop and shutdown the HTTP server. It supports being restarted. Server 
 * cannot be suspended before calling this.
 */
void
shutdown_http_server(void)
{
    int rc, status;
    
    if(shutdown_server) { // if server isn't running
        return;
    }
    
    shutdown_server = 1;
    
    LOG(LOG_INFO, "%s: Shutting down DPM's HTTP server", __func__);
    
    // wait for server thread
    rc = pthread_join(http_thread, (void **)&status);
    if(rc) {
        LOG(LOG_WARNING, "%s: Failed to synchronize on http server "
                "thread exit (%d)", __func__, rc);
        return;
    }
    
    pthread_mutex_destroy(&suspend_lock);
}

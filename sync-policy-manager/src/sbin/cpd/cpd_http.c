/*
 * $Id: cpd_http.c 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file cpd_http.c
 * @brief Relating to the CPD's HTTP server 
 *
 * 
 * The CPD runs an HTTP server in the cpd_http module on a separate thread.
 * The functions provide a way to start and stop the HTTP server.
 */


#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <signal.h>
#include <pthread.h>
#include <mihl.h>
#include <jnx/aux_types.h>
#include "cpd_http.h"
#include "cpd_config.h"
#include "cpd_conn.h"
#include "cpd_logging.h"

/*** Constants ***/

/**
 * The port that the CPD's public HTTP server runs on 
 */
#define CPD_HTTP_PORT 80

/**
 * The maximum number of concurrently accepted connections to the HTTP server 
 */
#define CPD_HTTP_MAX_CONNECTIONS 100

/**
 * The logging level to use with the HTTP server (library) 
 */
#define HTTP_SERVER_LOG_LEVEL \
    (MIHL_LOG_ERROR | MIHL_LOG_WARNING | MIHL_LOG_INFO | MIHL_LOG_INFO_VERBOSE)

/**
 * HTML content type header
 */
#define HTML_CONTENT "Content-type: text/html\r\n"

/**
 * The directory in which content to serve via HTTP is found 
 */
#define CPD_CONTENT_DIRECTORY "/var/cpd_content"

/**
 * Number of media types in type_mappings 
 */
#define MEDIA_TYPES 9

/**
 * The of the type is undefined in type_mappings then use this type
 */
#define DEFAULT_MEDIA_TYPE "text/plain"

/**
 * MIME type mapping mapping for (our) supported file extensions in the 
 * directory of content for the HTTP server.
 */
const char * type_mappings[MEDIA_TYPES][2] = {
    {".html", "text/html"},
    {".htm",  "text/html"},
    {".css",  "text/css"},
    {".xml",  "text/xml"},
    {".jpg",  "image/jpeg"},
    {".gif",  "image/gif"},
    {".png",  "image/png"},
    {".js",   "application/javascript"},
    {".swf",  "application/x-shockwave-flash"}
};


/*** Data structures: ***/

/**
 * Whether the server is shutdown or (for the server thread) needs shutting down
 */
static boolean shutdown_server = TRUE;

/**
 * The thread of the HTTP server
 */
static pthread_t http_thread;

/**
 * The readable formatted pfd address
 */
static char * pfd_address = NULL;


/**
 * The readable formatted pfd address
 */
static char * cpd_address = NULL;


/*** STATIC/INTERNAL Functions ***/


/**
 * Add a message to a standard HTML page and write it in the HTTP response using
 * the mihl_add function  
 * 
 * @param cnx
 *      Opaque context structure as returned by mihl_init()
 * 
 * @param message
 *      The message that will be bold and in red on the page
 */
static void
write_page(mihl_cnx_t * cnx, char const * message)
{
    mihl_add(cnx,
        "<html>"
        "<head>"
        "<META HTTP-EQUIV=\"pragma\" CONTENT=\"no-cache\">"
        "<title>SYNC Captive Portal</title>"
        "<style> "
        "body { "
            "background-color: #666666;"
            "font-family: Arial, sans-serif;"
            "font-size: 10pt;"
        "} "
        "#content { "
            "background-color: #FFFFFF;"
            "border: 1px outset #000000;"
            "height: 400px;"
            "min-width: 700px;"
            "width: 700px;"
            "padding: 10px;"
        "} "
        "#message { "
            "color: #CC0000;"
            "font-weight: bold;"
        "} "
        "</style>"
        "</head>"
        "<body><div align=\"center\"><div id=\"content\" align=\"left\">"
        "<p align=\"center\"><strong><u>Welcome to the SDK Your Net Corporation "
        "(SYNC) Policy Manager Captive Portal Example</u></strong></p>"
        "<p>&nbsp;</p>"
        "<p id=\"message\" align=\"center\">%s</p>"
        "<p>&nbsp;</p>"
        "<p align=\"center\"><a href=\"/index.html\">Return Home</a></p>"
        "</div></div></body></html>",

            message);
}

/**
 * Send a page with a script to redirect to the CPD's home page
 * Alternatively, we could send an HTTP MOVED, but this library doesn't support
 * that currently.
 * 
 * @param cnx
 *      Opaque context structure as returned by mihl_init()
 * 
 * @param tag
 *      URL (of the non existent page)
 * 
 * @param host
 *      The connecting host
 * 
 * @param param
 *      The user data registers for the callback (the server's IP) 
 * 
 * @return
 *      the result of mihl_send
 */
static int
page_not_found(mihl_cnx_t * cnx,
               char const * tag,
               char const * host,
               void * param __unused) {
    
    mihl_add(cnx, "<html><body>"
                    "<script>"
                        "window.location = 'http://%s/index.html';"
                    "</script>"
                  "</body></html>", cpd_address);
    
    LOG(LOG_INFO, "%s: sending a redirect script pointing to the CPD for "
        "connection from %s requesting page: %s", __func__, host, tag);
    
    return mihl_send(cnx, NULL, HTML_CONTENT);
}


/**
 * Send a page with a script to redirect to the CPD's home page
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
repudiate_access(mihl_cnx_t * cnx,
               char const * tag,
               char const * host,
               void * param) {
    
    struct in_addr client_address;
    int rc;
    
    if(strcmp(host, pfd_address) == 0) {
        return page_not_found(cnx, tag, host, param);
    }
    
    rc = inet_aton(host, &client_address); // client_address is nw-byte order
    
    if(rc != 1) {
        LOG(LOG_INFO, "%s: Couldn't parse host IP: %s", __func__, host);
        return 0;
    }
    
    LOG(LOG_INFO, "%s: Repudiating access for host: %s", __func__, host);
    
    if(is_auth_user(client_address.s_addr)) {
        delete_auth_user_addr(client_address.s_addr);
        send_repudiated_user(client_address.s_addr);
        write_page(cnx, "Your access has been repudiated");
    } else {
        write_page(cnx, "Your access is already currently unauthorized");
    }
    
    return mihl_send(cnx, NULL, HTML_CONTENT);
}


/**
 * Send a page with a script to redirect to the CPD's home page
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
authorize_access(mihl_cnx_t * cnx,
               char const * tag,
               char const * host,
               void * param) {

    struct in_addr client_address;
    int rc;
    
    if(strcmp(host, pfd_address) == 0) {
        return page_not_found(cnx, tag, host, param);
    }
    
    rc = inet_aton(host, &client_address); // client_address is nw-byte order
    
    if(rc != 1) {
        LOG(LOG_INFO, "%s: Couldn't parse host IP: %s", __func__, host);
        return 0;
    }
    
    LOG(LOG_INFO, "%s: Authorizing access for host %s", __func__, host);
    
    if(!is_auth_user(client_address.s_addr)) {
        add_auth_user_addr(client_address.s_addr);
        send_authorized_user(client_address.s_addr);
        write_page(cnx, "Your access has been authorized");
    } else {
        write_page(cnx, "Your access is already currently authorized");
    }
    
    return mihl_send(cnx, NULL, HTML_CONTENT);
}


/**
 * Send a page with a script to redirect to the CPD's home page
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
             void * param) {
    
    struct in_addr client_address;
    int rc;
    
    if(strcmp(host, pfd_address) == 0) {
        return page_not_found(cnx, tag, host, param);
    }
    
    rc = inet_aton(host, &client_address); // client_address is nw-byte order
    
    if(rc != 1) {
        LOG(LOG_INFO, "%s: Couldn't parse host IP: %s", __func__, host);
        return 0;
    }
    
    LOG(LOG_INFO, "%s: Checking access for host %s", __func__, host);
    
    if(is_auth_user(client_address.s_addr)) {
        write_page(cnx, "Your access is currently authorized");
    } else {
        write_page(cnx, "Your access is currently unauthorized");
    }
    
    return mihl_send(cnx, NULL, HTML_CONTENT);
}


/**
 * Register content with the HTTP server
 * 
 * @param[in] ctx
 *      The mihl HTTP library context/handle for the started server
 */
static void
register_content(mihl_ctx_t * ctx)
{
    struct dirent * direntry;
    DIR * dfd;
    char filename[256], path[256];
    int i;
    boolean found;
    
    errno = 0;
    dfd = opendir(CPD_CONTENT_DIRECTORY);
    
    if(dfd == NULL) {
        LOG(LOG_ERR, "%s: Failed to open directory of content (%s) for the HTTP"
            " server due to error: %m", __func__, CPD_CONTENT_DIRECTORY);
        return;
    }
        
    while((direntry = readdir(dfd))) {

        if(direntry->d_name[0] == '.' || direntry->d_type == DT_DIR) {
            continue; // ignore files with names starting with . and directories
        }
        
        // build filename
        snprintf(filename, 256, "%s/%s", CPD_CONTENT_DIRECTORY, 
            direntry->d_name);

        // Check that we have permission to read this file
        if (access(filename, R_OK ) == 0) {
            
            // build path to file on the server
            snprintf(path, 256, "/%s", direntry->d_name);
            
            // check if it is a known media type
            found = FALSE;
            for(i = 0; i < MEDIA_TYPES; ++i) {
                if(strstr(path, type_mappings[i][0])) {
                    
                    // register handler of path with this file and type
                    mihl_handle_file(ctx, path, filename, 
                        type_mappings[i][1], 0);
                    
                    LOG(LOG_INFO, "%s:  path: %s , file: %s , type: %s",
                        __func__, path, filename, type_mappings[i][1]);
                    
                    found = TRUE;
                    break;
                }
            }
            if(!found) {
                // register handler of path with this file and default type
                mihl_handle_file(ctx, path, filename, DEFAULT_MEDIA_TYPE, 0);
                
                LOG(LOG_INFO, "%s:  path: %s , file: %s , type: %s",
                    __func__, path, filename, DEFAULT_MEDIA_TYPE);
                
            }
        }
        else {
            LOG(LOG_ERR, "%s: Failed to access file %s in the directory of "
                " content for the HTTP server: %m", __func__, filename);
            errno = 0;
        }
    }
    
    if(errno) {
        LOG(LOG_ERR, "%s: Failed to read from the directory of content (%s)"
            "for the HTTP server: %m", __func__, CPD_CONTENT_DIRECTORY);
    }
    // else normal end of directory
    
    closedir(dfd);
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
    struct in_addr tmp;
    sigset_t sig_mask;
    
    LOG(LOG_INFO, "%s: HTTP server thread alive", __func__);
    
    // Block SIGTERM to this thread/main thread will handle otherwise we inherit
    // this behaviour in our threads sigmask and the signal might come here
    sigemptyset(&sig_mask);
    sigaddset(&sig_mask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sig_mask, NULL);
    
    tmp.s_addr = get_cpd_address();
    cpd_address = strdup(inet_ntoa(tmp));
    tmp.s_addr = get_pfd_address();
    pfd_address = strdup(inet_ntoa(tmp));
    
    // initial a server and get the server context
    ctx = mihl_init(cpd_address, CPD_HTTP_PORT,
            CPD_HTTP_MAX_CONNECTIONS, HTTP_SERVER_LOG_LEVEL);
    
    if (!ctx) {
        LOG(LOG_ERR, "%s: Failed to start HTTP server on %s:%d using library",
            __func__, cpd_address, CPD_HTTP_PORT);
        pthread_exit((void *) 0);
        return NULL;
    }

    LOG(LOG_INFO, "%s: Started HTTP server on %s:%d using library",
        __func__, cpd_address, CPD_HTTP_PORT);
    
    // register default handler
    mihl_handle_get(ctx, NULL, page_not_found, NULL);

    // register handler for all pages
    register_content(ctx);
    
    LOG(LOG_INFO, "%s: Registered CPD server URLs with content", __func__);
    
    // register other handlers to control access
    mihl_handle_get(ctx, "/repudiate.html", repudiate_access, NULL);
    mihl_handle_get(ctx, "/authorize.html", authorize_access, NULL);
    mihl_handle_get(ctx, "/check.html",     check_access,     NULL);
    
    LOG(LOG_INFO, "%s: Registered other CPD server URLs", __func__);

    while(!shutdown_server) {
        mihl_server(ctx); // serve pages and accept/terminate connections
    }
    
    LOG(LOG_INFO, "%s: Shutting down the server", __func__);
    
    mihl_end(ctx);
    
    free(pfd_address);
    pfd_address = NULL;
    free(cpd_address);
    cpd_address = NULL;
    
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
    
    if(!shutdown_server) { // if server is running
        shutdown_http_server();
    }

    /* Initialize and set thread detached attribute */
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

    shutdown_server = FALSE; // server started
    
    LOG(LOG_INFO, "%s: Staring CPD's HTTP server", __func__);
    
    rc = pthread_create(&http_thread, NULL, run_http_server, NULL);
    if(rc) {
        LOG(LOG_ERR, "%s: Failed to start http server thread (%d)",
            __func__, rc);
        shutdown_server = TRUE; // still shutdown
    }
    
    pthread_attr_destroy(&attr);
}


/**
 * Stop and shutdown the HTTP server. It supports being restarted.
 */
void
shutdown_http_server(void)
{
    int rc, status;
    
    if(shutdown_server) { // if server isn't running
        return;
    }
    
    shutdown_server = TRUE;
    
    LOG(LOG_INFO, "%s: Shutting down CPD's HTTP server", __func__);
    
    rc = pthread_join(http_thread, (void **)&status); // wait for server thread
    if(rc) {
        LOG(LOG_ERR, "%s: Failed to synchronize on http server thread exit "
            "(%d)", __func__, rc);
    }
}





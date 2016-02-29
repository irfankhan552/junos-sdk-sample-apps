/*
 *$Id: jnx-gateway-data_control.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-gateway-data_control.c - Processing done by the Control Thread 
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
 * @file jnx-gateway-data_control.c
 * @brief All the routines executed in the context of the control thread are
 * preesnt in this file. 
 *
 * This file covers teh following stuff:-
 * 1.  Event handler & the message handler routines registered with the PCONN
 *     library
 * 2.  Routine for periodic cleanup of the tunnels. This routine is registered
 *     with the event library.
 * 3.  Routines for backend support of operational commands like STAT FETCH.
 * 4.  Routines for back end support of configuration commands like Setup &  
 *     Clearing up of Tunnels. 
 */
#include <jnx/mpsdk.h>
#include "jnx-gateway-data.h"
#include <jnx/jnx-gateway_msg.h>
#include "jnx-gateway-data_control.h"
#include "jnx-gateway-data_utils.h"
#include "jnx/pconn.h"
#include <net/if_802.h>
#include <stdlib.h>
#include <string.h>

/*===========================================================================*
 *                                                                           *
 *                 Local function prototypes                                 *
 *                                                                           *
 *===========================================================================*/

/* Function to process the STAT FETCH message from JNX-GW-MGMT (RE) */
static void jnx_gw_data_process_stat_fetch_msg(jnx_gw_data_cb_t* app_cb, 
                                               jnx_gw_msg_t*     msg, 
                                               pconn_session_t*  session);

/* Function to process the GRE STAT FETCH Message */
static void jnx_gw_data_fetch_gre_stat(jnx_gw_data_cb_t*   app_cb, 
                                       jnx_gw_msg_stat_t*  msg_ptr,
                                       jnx_gw_msg_t*       rsp_ptr, 
                                       pconn_session_t*    session);

/* Function to process the IPIP  STAT FETCH Message */
static void jnx_gw_data_fetch_ipip_stat(jnx_gw_data_cb_t*   app_cb, 
                                        jnx_gw_msg_stat_t*  msg_ptr,
                                        jnx_gw_msg_t*       rsp_ptr, 
                                        pconn_session_t*    session);

/* Function to process the VRF SUMMARY STAT FETCH Message */
static void jnx_gw_data_fetch_vrf_summary_stats(jnx_gw_data_cb_t*   app_cb, 
                                                jnx_gw_msg_stat_t*  msg_ptr,
                                                jnx_gw_msg_t*       rsp_ptr,
                                                pconn_session_t*    session);

/* Function to process the GRE Message from the JNX-GW-CTRL (CTRL PIC) */
static void jnx_gw_data_process_gre_msg(jnx_gw_data_cb_t*   app_cb, 
                                        jnx_gw_msg_t*       msg, 
                                        pconn_session_t*    session);

/* Function to process the IPIP Message from the JNX-GW-CTRL (CTRL PIC) */
static void jnx_gw_data_process_ipip_msg(jnx_gw_data_cb_t*  app_cb, 
                                         jnx_gw_msg_t*      msg, 
                                         pconn_session_t* session);

/* Function to process the JNX_GW_ADD_GRE_SESSION message from the JNX-GW_CTRL */ 
static uint32_t jnx_gw_data_add_gre_session(jnx_gw_data_cb_t*  app_cb, 
                                        jnx_gw_msg_gre_t*  msg_ptr,
                                        jnx_gw_msg_gre_t*  rsp_ptr);

/* Function to process the JNX_GW_DEL_GRE_SESSION message from the JNX-GW_CTRL */ 
static uint32_t jnx_gw_data_del_gre_session(jnx_gw_data_cb_t*  app_cb, 
                                        jnx_gw_msg_gre_t*  msg_ptr,
                                        jnx_gw_msg_gre_t*  rsp_ptr);

/* Function to process the JNX_GW_ADD_IP_IP_SESSION message from the JNX-GW_CTRL */ 
static uint32_t jnx_gw_data_add_ipip_tunnel_entry(jnx_gw_data_cb_t*  app_cb, 
                                              jnx_gw_msg_ipip_t*  msg_ptr,
                                              jnx_gw_msg_ipip_t* rsp_ptr);

/* Function to process the JNX_GW_DEL_IP_IP_SESSION message from the JNX-GW_CTRL */ 
static uint32_t jnx_gw_data_del_ipip_tunnel_entry(jnx_gw_data_cb_t*  app_cb, 
                                              jnx_gw_msg_ipip_t*  msg_ptr,
                                              jnx_gw_msg_ipip_t* rsp_ptr);

/* Function to fetch the extensive stats for all the VRFs */
static void jnx_gw_data_fetch_all_vrf_extensive_stats(jnx_gw_data_cb_t*  app_cb, 
                                                      jnx_gw_msg_t* rsp_ptr, 
                                                       pconn_session_t* session);

/* Function to fetch the extensive stats for all a particular VRF   */
static void jnx_gw_data_fetch_vrf_extensive_stats(jnx_gw_data_cb_t*  app_cb, 
                                                  jnx_gw_msg_stat_t *msg_ptr, 
                                                  jnx_gw_msg_t* rsp_ptr, 
                                                  pconn_session_t* session);

/* Function to fill the gre tunnel stats in a buffer */
static void jnx_gw_data_fill_gre_tunnel_stats(jnx_gw_data_gre_tunnel_t* gre_tunnel,
                                       char* buf);

/* Function to fill the ip ip tunnel stats in a buffer */
static void jnx_gw_data_fill_ipip_tunnel_stats(jnx_gw_data_ipip_tunnel_t* ipip_tunnel,
                                        char* buf);

/* Function to fill the vrf summary stats in a buffer */
static void jnx_gw_data_fill_vrf_summary_stats(jnx_gw_data_vrf_stat_t* vrf_entry,
                                        char* buf);

static void jnx_gw_data_fetch_all_vrf_summary_stats(jnx_gw_data_cb_t*   app_cb, 
                                                    jnx_gw_msg_t*   rsp_msg, 
                                                    pconn_session_t*   session);

/*===========================================================================*
 *                                                                           *
 *                 Function Definitions                                      *
 *                                                                           *
 *===========================================================================*/                 

/**
 * 
 * This is the function registered with PCONN library to receive 
 * events related to connection with JNX-GATEWAY-MGMT & JNX-GATEWAY-CTRL
 * 
 *
 * @param[in] session   pconn_server_session, can be from CTRL or MGMT to which 
 *                      event belongs
 * @param[in] event     Eevnt Type
 * @Param[in] cookie    opaque pointer (JNX_GW_DATA_CB_T*) in this case, passed
 *                      to pconn_Server during init time.
 *
 */
void 
jnx_gw_data_pconn_event_handler(pconn_session_t *session, 
                                pconn_event_t   event, 
                                void*           cookie)
{
    
    pconn_peer_info_t peer_info;
    jnx_gw_data_cb_t*   app_cb = (jnx_gw_data_cb_t*)cookie;
    int i = 0;


    jnx_gw_log(LOG_INFO, "Event(%d) received", event);

    if(app_cb == NULL)
        return;

    /* get the peer information */
    if (pconn_session_get_peer_info(session, &peer_info) != PCONN_OK) {
        jnx_gw_log(LOG_INFO, "Peer Info for the event is unknown");
        return;
    }

    switch(event) {

        case PCONN_EVENT_ESTABLISHED:
            
            if (app_cb->session_count < JNX_GW_DATA_MAX_SVR_SESN_COUNT) {
                app_cb->session[app_cb->session_count] = session;
                app_cb->session_count++;

                /*
                 * Mark the data application status as
                 * up only when we have the connection
                 * to the management application agent up
                 */
                if (peer_info.ppi_peer_type == PCONN_PEER_TYPE_RE) {
                    app_cb->app_state = JNX_GW_DATA_STATE_READY;
                    jnx_gw_log(LOG_INFO, "Management Connection is UP");
                } else {
                    jnx_gw_log(LOG_INFO, "Control Connection is UP");
                }
            }

            break;
        case PCONN_EVENT_SHUTDOWN:

            for(i = 0; i < JNX_GW_DATA_MAX_SVR_SESN_COUNT; i++) {

                if (app_cb->session[i] != session) continue;


                app_cb->session_count--;
                /*
                 * Mark the data pic agent state
                 * as init, as we have lost connection
                 * to the management application agent
                 */
                if (peer_info.ppi_peer_type == PCONN_PEER_TYPE_RE) {
                    jnx_gw_log(LOG_INFO, "Management Connection is DOWN");
                    app_cb->app_state = JNX_GW_DATA_STATE_INIT;
                    if (app_cb->conn_client) {
                        pconn_client_close(app_cb->conn_client); 
                        app_cb->conn_client = NULL;
                    }
                } else {
                     jnx_gw_log(LOG_INFO, "Control Connection is DOWN");
                }
                app_cb->session[i] = NULL;
            }
            break;
        default:
            break;
    }
    return;
}
/**
 * 
 * This is the function registered with PCONN library to receive messages
 * directed to the JNX-GATEWAY-DATA. 
 * 
 * This function process the configuration as well as the operational commands 
 * related messages from JNX-GATEWAY-MGMT & JNX-GATEWAY-CTRL
 *
 * @param[in] session   pconn_server_session, can be from CTRL or MGMT
 * @param[in] ipc_msg   Message received by the server
 * @Param[in] cookie    opaque pointer (JNX_GW_DATA_CB_T*) in this case, passed
 *                      to pconn_Server during init time.
 *
 */
status_t
jnx_gw_data_pconn_msg_handler(pconn_session_t*    session, 
                              ipc_msg_t*          ipc_msg,
                              void*               cookie)
{
   jnx_gw_data_cb_t*    app_cb;
   jnx_gw_msg_t*        msg;
   
   jnx_gw_log(LOG_DEBUG, "Message received");

   msg    = (jnx_gw_msg_t*)ipc_msg->data; 
   app_cb = (jnx_gw_data_cb_t*)cookie;

   switch(msg->msg_header.msg_type){

        case JNX_GW_STAT_FETCH_MSG:

            jnx_gw_data_process_stat_fetch_msg(app_cb, msg, session); 
            break;

        case JNX_GW_GRE_SESSION_MSG:

            jnx_gw_data_process_gre_msg(app_cb, msg, session);
            break;

        case JNX_GW_IPIP_TUNNEL_MSG:

            jnx_gw_data_process_ipip_msg(app_cb, msg, session);
            break;

        default:
            return EOK;
    }

    return EOK;
}

/**
 * 
 * This is the function registered with PCONN library to receive the events 
 * related to socket for jnx-gateway-mgmt
 * 
 * @param[in] client    pconn client for jnx-gateway-mgmt
 * @param[in] event     event for the client
 */
void
jnx_gw_data_pconn_client_event_handler(pconn_client_t* client __unused,
                                       pconn_event_t   event) 
{
   jnx_gw_log(LOG_INFO, "Management Event(%d) received", event);
    switch(event) {

        case PCONN_EVENT_ESTABLISHED:
            break;
        case PCONN_EVENT_SHUTDOWN:
            break;
        case PCONN_EVENT_FAILED:
            break;
    }
    return;
}

/**
 * 
 * This is the function registered with PCONN library to receive the events 
 * related to socket for jnx-gateway-mgmt
 * 
 * @param[in] client    pconn client for jnx-gateway-mgmt
 * @param[in] msg       event for the client
 * @param[in] cookie    cookie
 */
void
jnx_gw_data_pconn_client_msg_handler(pconn_client_t*   client __unused,
                                     void*            msg __unused,
                                     void*            cookie __unused) 
{
    jnx_gw_log(LOG_DEBUG, "Management Message received");
    return;
}

/**
 * 
 * This function is used to process the GRE Session related messages i.e.
 * session add & delete.
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg       Pointer to the message received.
 * @param[in] session   pconn_server_session, 
 */
static void 
jnx_gw_data_process_gre_msg(jnx_gw_data_cb_t*    app_cb, 
                            jnx_gw_msg_t*        msg,
                            pconn_session_t*     session) 
{
    jnx_gw_msg_gre_t*   msg_ptr, *rsp_ptr;
    jnx_gw_msg_t*       rsp_buffer = (jnx_gw_msg_t*)(app_cb->buffer);
    int                 count, ret = 0, rsp_len = 0;

    /*
     * Make the message pointer point to the sub header 
     */ 
    msg_ptr = (jnx_gw_msg_gre_t*)((char*)msg + sizeof(jnx_gw_msg_header_t));

    /*
     * Fill the Response Message Header
     */
    rsp_buffer->msg_header.msg_type = msg->msg_header.msg_type;  
    rsp_buffer->msg_header.count    = msg->msg_header.count;
    rsp_buffer->msg_header.msg_id   = msg->msg_header.msg_id;
    

    rsp_ptr = (jnx_gw_msg_gre_t*)((char*)rsp_buffer + 
                                  sizeof(jnx_gw_msg_header_t));
    rsp_len = sizeof(jnx_gw_msg_header_t);
    
    for (count = 0; count < msg->msg_header.count; count++) {

        /*
         * Determine the sub header type and based on that perform
         * the appropriate action.
         */
        rsp_ptr->sub_header.length   = htons(4);
        rsp_ptr->sub_header.sub_type = msg_ptr->sub_header.sub_type;

         switch(msg_ptr->sub_header.sub_type) {
             
             case JNX_GW_ADD_GRE_SESSION:

                 ret = jnx_gw_data_add_gre_session(app_cb, msg_ptr, rsp_ptr);
                 break;

             case JNX_GW_DEL_GRE_SESSION:

                 ret = jnx_gw_data_del_gre_session(app_cb, msg_ptr, rsp_ptr);
                 break;

             default:
                 /*Send Error Response */
                 jnx_gw_log(LOG_DEBUG, "Invalid GRE Message type");
                 break;
         }

        msg_ptr = (jnx_gw_msg_gre_t*)((char*)msg_ptr + 
                                      ntohs(msg_ptr->sub_header.length));
        rsp_ptr = (jnx_gw_msg_gre_t*)((char*)rsp_ptr + ret);
        rsp_len += ret;

    }

    rsp_buffer->msg_header.count = count;
    rsp_buffer->msg_header.msg_len = htons(rsp_len);

    pconn_server_send(session, JNX_GW_GRE_SESSION_MSG, rsp_buffer, rsp_len); 
    return;
}

/**
 * 
 * This function is used to process the GRE ADD Session messages.
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg_ptr   Pointer to the message received.
 * @param[in] rsp_ptr   Pointer to the response buffer
 */
static uint32_t
jnx_gw_data_add_gre_session(jnx_gw_data_cb_t*    app_cb,
                            jnx_gw_msg_gre_t*    msg_ptr,
                            jnx_gw_msg_gre_t*    rsp_ptr)
{
    jnx_gw_gre_key_hash_t                     gre_tunnel_key;
    jnx_gw_data_ipip_sub_tunnel_key_hash_t    ipip_sub_tunnel_key;
    jnx_gw_ipip_tunnel_key_hash_t             ipip_tunnel_key;
    jnx_gw_msg_gre_info_t                     gre_info;
    jnx_gw_msg_ip_ip_info_t                   ip_ip_info;
    jnx_gw_msg_session_info_t                 session_info;
    jnx_gw_msg_tunnel_type_t*                 ing_tunnel_ptr = NULL;
    jnx_gw_msg_tunnel_type_t*                 eg_tunnel_ptr = NULL;
    jnx_gw_msg_ip_ip_info_t*                  ip_ip_msg_ptr = NULL;
    jnx_gw_data_gre_tunnel_t*                 gre_tunnel = NULL;
    jnx_gw_data_ipip_sub_tunnel_t*            ipip_sub_tunnel = NULL;
    jnx_gw_data_ipip_tunnel_t*                ipip_tunnel  = NULL; 
    uint32_t                                 err_code = JNX_GW_MSG_ERR_NO_ERR;
    jnx_gw_data_vrf_stat_t*                   gre_vrf_entry;
    jnx_gw_data_vrf_stat_t*                   eg_vrf_entry;
    uint32_t                                 eg_vrf = 0;

    memset(&gre_tunnel_key, 0, sizeof(jnx_gw_gre_key_hash_t));
    memset(&ipip_sub_tunnel_key, 0, sizeof(jnx_gw_data_ipip_sub_tunnel_key_hash_t));
    memset(&ipip_tunnel_key, 0, sizeof(jnx_gw_ipip_tunnel_key_hash_t));
    memset(&gre_info, 0, sizeof(jnx_gw_msg_gre_info_t));
    memset(&ip_ip_info, 0, sizeof(jnx_gw_msg_ip_ip_info_t));
    memset(&session_info, 0, sizeof(jnx_gw_msg_session_info_t));

    /* Control App has asked to add a GRE Session. It involves the following :-
     *
     * 1. Extract the key for both the ends i.e. GRE & IP-IP/IP.
     * 2. Perform a lookup in the GRE-DB and see if the entry already exists. If
     *    yes, then return an error.
     * 3. Perform a lookup in the IP-IP tunnel to see if the tunnel exists. If
     *    it doesn't then return an error.
     * 4. Create an entry in the GRE_TUNNEL_DB, IP_IP_SUB_TUNNEL DB.
     * 5. Update the appropriate pointers for the IP-IP Tunnel DB.
     * 6. Add the entry in the vrf db for summary stats
     */
    jnx_gw_log(LOG_DEBUG, "GRE Session Add Request");

    /* 
     * Extract the session related info from the message
     */
    session_info.session_id = ntohs(msg_ptr->info.add_session.session_info.
                                    session_id); 
    session_info.proto = msg_ptr->info.add_session.session_info.proto;
    session_info.sip   = ntohl(msg_ptr->info.add_session.session_info.sip);
    session_info.dip   = ntohl(msg_ptr->info.add_session.session_info.dip);
    session_info.sport = ntohs(msg_ptr->info.add_session.session_info.sport);                    
    session_info.dport = ntohs(msg_ptr->info.add_session.session_info.dport);                    

    ing_tunnel_ptr = (jnx_gw_msg_tunnel_type_t*)((char*)msg_ptr + 
                                                 sizeof(jnx_gw_msg_sub_header_t) + 
                                                 sizeof(jnx_gw_msg_session_info_t)); 
    /*
     * Extract the keys.
     */

    /* Get the ingress tunnel type */
    if(ing_tunnel_ptr->tunnel_type == JNX_GW_TUNNEL_TYPE_GRE) {

        gre_info.vrf = ntohl(msg_ptr->info.add_session.
                             ing_tunnel_info.gre_tunnel.vrf);

        gre_info.gateway_ip = ntohl(msg_ptr->info.add_session.
                                    ing_tunnel_info.gre_tunnel.gateway_ip);

        gre_info.self_ip = ntohl(msg_ptr->info.add_session.
                                 ing_tunnel_info.gre_tunnel.self_ip);

        gre_info.gre_key = ntohl(msg_ptr->info.add_session.
                                 ing_tunnel_info.gre_tunnel.gre_key);

        gre_info.gre_seq = ntohl(msg_ptr->info.add_session.
                                 ing_tunnel_info.gre_tunnel.gre_seq);

        gre_tunnel_key.key.vrf     = gre_info.vrf;
        gre_tunnel_key.key.gre_key = gre_info.gre_key;

    }
    else {

        /*
         * All other cases are errors in the current application, hence just
         * send an error response in this case.
         */
        err_code = JNX_GW_MSG_ERR_INVALID_ING_TUNNEL_TYPE;
        jnx_gw_log(LOG_DEBUG, "Invalid Ingress tunnel type (%d)", 
               ing_tunnel_ptr->tunnel_type);
        goto jnx_gw_send_add_rsp_to_control;
    }

    /*
     * Go to the egress tunnel type
     */
    eg_tunnel_ptr = (jnx_gw_msg_tunnel_type_t*)
        ((uint8_t *)ing_tunnel_ptr + ntohs(ing_tunnel_ptr->length));

    if(eg_tunnel_ptr->tunnel_type == JNX_GW_TUNNEL_TYPE_IPIP) {

        ip_ip_msg_ptr  = (jnx_gw_msg_ip_ip_info_t*)((char*)eg_tunnel_ptr + 
                                                    sizeof(jnx_gw_msg_tunnel_type_t));

        ip_ip_info.vrf        = ntohl(ip_ip_msg_ptr->vrf);
        ip_ip_info.gateway_ip = ntohl(ip_ip_msg_ptr->gateway_ip);
        ip_ip_info.self_ip    = ntohl(ip_ip_msg_ptr->self_ip);
        eg_vrf                = ip_ip_info.vrf;

        ipip_tunnel_key.key.gateway_ip = ip_ip_info.gateway_ip;
        ipip_tunnel_key.key.vrf        = ip_ip_info.vrf;


    } else {
        /*
         * All other cases are errors in the current application, hence just
         * send an error response in this case.
         */
        err_code = JNX_GW_MSG_ERR_INVALID_EG_TUNNEL_TYPE;
        jnx_gw_log(LOG_DEBUG, "Invalid Egress tunnel type (%d)", 
                     eg_tunnel_ptr->tunnel_type);
        goto jnx_gw_send_add_rsp_to_control;
    }


    /*
     * Perform a lookup in the gre database to find out if the entry exist.
     */
    if(jnx_gw_data_db_gre_tunnel_lookup_without_lock(app_cb, &gre_tunnel_key) != 
       NULL) {
        /*
         * Session Already exists. Send an error response.
         */
        err_code = JNX_GW_MSG_ERR_GRE_SESS_EXISTS;
        jnx_gw_log(LOG_DEBUG, "GRE Tunnel is present(%d, %d)",
                     gre_info.vrf, gre_info.gre_key);
        goto jnx_gw_send_add_rsp_to_control;
    }


    /*
     * If the egress tunnel is IP-IP then perform a lookup in the ip-ip stat db
     * to find out if the entry exists.
     */
    if((ipip_tunnel = jnx_gw_data_db_ipip_tunnel_lookup_without_lock(app_cb, 
                                                                     &ipip_tunnel_key)) == NULL) {

        /*
         * IP-IP Tunnel doesn't exist send an error response.
         */
        jnx_gw_log(LOG_DEBUG, "IPIP Tunnel is absent (%d, %s)",
               ip_ip_info.vrf, JNX_GW_IP_ADDRA(ip_ip_info.gateway_ip));

        err_code = JNX_GW_MSG_ERR_IPIP_SESS_NOT_EXIST;
        goto jnx_gw_send_add_rsp_to_control;
    }

    /*
     * Now we have got all the information required to process the add session
     * message. Start doing the actual processing now.
     */


    /*
     * Add an entry in the GRE DB 
     */ 
    if((gre_tunnel = jnx_gw_data_db_add_gre_tunnel(app_cb, &gre_tunnel_key)) ==
       NULL) {

        /* Could not add an entry in the GRE data base, send an error response
         */
        jnx_gw_log(LOG_DEBUG, "GRE tunnel entry (%d, %d) add failed",
               gre_info.vrf, gre_info.gre_key);

        err_code = JNX_GW_MSG_ERR_MEM_ALLOC_FAIL;
        goto jnx_gw_send_add_rsp_to_control;
    }

    /*
     * Add an entry in the ip-ip sub tunnel db
     */

    ipip_sub_tunnel_key.key.vrf          = ip_ip_info.vrf;
    ipip_sub_tunnel_key.key.gateway_addr = ip_ip_info.gateway_ip;
    ipip_sub_tunnel_key.key.client_addr  = session_info.sip;
    ipip_sub_tunnel_key.key.client_port  = session_info.sport;

    if((ipip_sub_tunnel = jnx_gw_data_db_add_ipip_sub_tunnel(app_cb, 
                                                             &ipip_sub_tunnel_key)) == NULL) {

        /*
         * Could not add an entry for the reverse path, hence delete the GRE
         * tunnel and send an error response.
         */
        jnx_gw_data_db_del_gre_tunnel(app_cb, gre_tunnel);

        jnx_gw_log(LOG_DEBUG, "IPIP Tunnel mux add failed (%d, %s)"
                   " for (%d, %d)",
                   ip_ip_info.vrf, JNX_GW_IP_ADDRA(ip_ip_info.gateway_ip),
                   gre_info.vrf, gre_info.gre_key);

        err_code = JNX_GW_MSG_ERR_MEM_ALLOC_FAIL;
        goto jnx_gw_send_add_rsp_to_control;
    }

    /*
     * Add an entry in the VRF DB and VRF list (Used to maintain aggregate stats
     * for a particular VRF).
     * We need to add two entries, one for the ingress Vrf and the one for the
     * egress vrf. In this case if the entry exists, the pointer of the existing
     * entry woulf be returned by this routine.
     */ 
    if((gre_vrf_entry = jnx_gw_data_db_add_vrf_entry(app_cb, 
                                                     gre_tunnel_key.key.vrf)) == NULL) {

        /*
         * Could not add an entry in the VRF DB to maintain the aggregate stats 
         * for the vrf associated with the GRE Tunnel. Send back an error
         * response
         */
        jnx_gw_data_db_del_gre_tunnel(app_cb, gre_tunnel);
        jnx_gw_data_db_del_ipip_sub_tunnel(app_cb, ipip_sub_tunnel);

         jnx_gw_log(LOG_DEBUG, "Ingress VRF Stat Entry add failed (%d, %d)",
                gre_tunnel_key.key.vrf, gre_info.gre_key);

        err_code = JNX_GW_MSG_ERR_MEM_ALLOC_FAIL;
        goto jnx_gw_send_add_rsp_to_control;

    }

    if((eg_vrf_entry = jnx_gw_data_db_add_vrf_entry(app_cb, eg_vrf)) == NULL) {

        /*
         * Could not add an entry in the VRF DB to maintain the aggregate stats 
         * for the vrf associated with the GRE Tunnel. Send back an error
         * response
         */
        jnx_gw_data_db_del_gre_tunnel(app_cb, gre_tunnel);
        jnx_gw_data_db_del_ipip_sub_tunnel(app_cb, ipip_sub_tunnel);

        err_code = JNX_GW_MSG_ERR_MEM_ALLOC_FAIL;
        jnx_gw_log(LOG_DEBUG, "Egress VRF(%d) Stat Entry add failed", eg_vrf);
        goto jnx_gw_send_add_rsp_to_control;
    }

    /*
     * Increment the use count of the IP_IP Stat tunnel by two,
     * one for the GRE Tunnel and other for the reverse path i.e
     * IP_IP_TUNNEL
     */ 

    atomic_add_int(2, &ipip_tunnel->use_count);

    /* Initialise the GRE Tunnel entry now */

    /*
     * First acquire lock on the Tunnel
     */
    jnx_gw_data_acquire_lock(&gre_tunnel->lock);

    gre_tunnel->self_ip_addr    = gre_info.self_ip;
    gre_tunnel->egress_vrf      = eg_vrf;
    gre_tunnel->tunnel_type     = JNX_GW_TUNNEL_TYPE_IPIP;
    gre_tunnel->ipip_tunnel     = ipip_tunnel;
    gre_tunnel->ing_vrf_stat    = gre_vrf_entry;
    gre_tunnel->eg_vrf_stat     = eg_vrf_entry;
    gre_tunnel->ipip_sub_tunnel = ipip_sub_tunnel;

    if(gre_tunnel->tunnel_type == JNX_GW_TUNNEL_TYPE_IPIP) {

        gre_tunnel->egress_info.ip_ip.gateway_addr = 
            ip_ip_info.gateway_ip;

        gre_tunnel->egress_info.ip_ip.self_ip_addr = 
            ip_ip_info.self_ip;
    }

    /* Add the tunnels in the VRF-list to enable stats on a per vrf */
    gre_tunnel->next_in_vrf = gre_vrf_entry->next_gre_tunnel;
    gre_vrf_entry->next_gre_tunnel = (jnx_gw_data_gre_tunnel_t*)((char*)gre_tunnel + 
                                                                 offsetof(jnx_gw_data_gre_tunnel_t,
                                                                          next_in_vrf));

    gre_vrf_entry->vrf_stats.total_sessions++;
    gre_vrf_entry->vrf_stats.active_sessions++;
    /* 
     * Populate the encapsulation header associated with the egress IP-IP 
     * tunnel. When the packet arrives just prepend this structure on the packet
     * with minimal modifications(IP len, Sequence id, TTL and checksum)
     */
    gre_tunnel->ip_hdr.ip_v    = IPVERSION;
    gre_tunnel->ip_hdr.ip_hl   = 5;
    gre_tunnel->ip_hdr.ip_tos  = 0;
    gre_tunnel->ip_hdr.ip_len  = 0;
    gre_tunnel->ip_hdr.ip_id   = 0;
    gre_tunnel->ip_hdr.ip_off  = 0;
    gre_tunnel->ip_hdr.ip_ttl  = 0;
    gre_tunnel->ip_hdr.ip_p    = IPPROTO_IPIP;
    gre_tunnel->ip_hdr.ip_sum  = 0;
    gre_tunnel->ip_hdr.ip_src.s_addr  = htonl(ip_ip_info.self_ip);
    gre_tunnel->ip_hdr.ip_dst.s_addr  = htonl(ip_ip_info.gateway_ip);

    gre_tunnel->tunnel_state = JNX_GW_DATA_ENTRY_STATE_READY;
    gre_tunnel->gre_seq      = 0xFFFFFFFF;

    /*Release the lock on the entry now */
    jnx_gw_data_release_lock(&gre_tunnel->lock);

    /* Now, prepare the reverse path entry */

    /* Acquire a lock on the Tunnel */
    jnx_gw_data_acquire_lock(&ipip_sub_tunnel->lock);

    ipip_sub_tunnel->egress_vrf   = gre_tunnel_key.key.vrf;
    ipip_sub_tunnel->gre_tunnel   = gre_tunnel;
    ipip_sub_tunnel->ipip_tunnel  = ipip_tunnel;
    ipip_sub_tunnel->ing_vrf_stat = eg_vrf_entry;
    ipip_sub_tunnel->eg_vrf_stat  = gre_vrf_entry;
    ipip_sub_tunnel->tunnel_state = JNX_GW_DATA_ENTRY_STATE_READY;

    /* 
     * Populate the Encapsulation header associated with the Egress GRE tunnel.
     * When the packet arrives just prepend this structure on the packet
     * with minimal modifications(IP len, Sequence id, TTL and checksum)
     */
    ipip_sub_tunnel->ip_gre_hdr_len = sizeof(struct ip) + GRE_FIELD_WIDTH;

    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_v    = IPVERSION;
    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_hl   = 5;
    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_tos  = 0;
    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_len  = 0;
    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_id   = 0;
    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_off  = 0;
    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_ttl  = 0;
    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_p    = IPPROTO_GRE;
    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_sum  = 0;

    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_src.s_addr  = 
        htonl(gre_info.self_ip);
    ipip_sub_tunnel->ip_gre_hdr.outer_ip_hdr.ip_dst.s_addr  = 
        htonl(gre_info.gateway_ip);

    if (ing_tunnel_ptr->flags & JNX_GW_GRE_CHECKSUM_PRESENT) {
        ipip_sub_tunnel->ip_gre_hdr_cksum_offset = GRE_CKSUM_OFFSET;
        ipip_sub_tunnel->ip_gre_hdr.gre_header.hdr_flags.info.checksum = 1;
        ipip_sub_tunnel->ip_gre_hdr.gre_header.info.checksum_key.checksum   = 0;
        ipip_sub_tunnel->ip_gre_hdr.gre_header.info.checksum_key.rsvd       = 0;
        ipip_sub_tunnel->ip_gre_hdr_len += GRE_FIELD_WIDTH;
    }
    else {
        ipip_sub_tunnel->ip_gre_hdr_cksum_offset = 0;
        ipip_sub_tunnel->ip_gre_hdr.gre_header.hdr_flags.info.checksum = 0;
    }

    ipip_sub_tunnel->ip_gre_hdr.gre_header.hdr_flags.info.rsvd1        = 0;

    if (ing_tunnel_ptr->flags & JNX_GW_GRE_KEY_PRESENT) {
        ipip_sub_tunnel->ip_gre_hdr_key_offset = 
            ipip_sub_tunnel->ip_gre_hdr_len;
        ipip_sub_tunnel->ip_gre_hdr.gre_header.hdr_flags.info.key_present = 1;
        *(uint32_t *)((char *)&ipip_sub_tunnel->ip_gre_hdr + 
                       ipip_sub_tunnel->ip_gre_hdr_key_offset) = 
            htonl(gre_tunnel_key.key.gre_key);

        ipip_sub_tunnel->ip_gre_hdr_len += GRE_FIELD_WIDTH;
    }
    else {
        ipip_sub_tunnel->ip_gre_hdr_key_offset =  0;
        ipip_sub_tunnel->ip_gre_hdr.gre_header.hdr_flags.info.key_present = 0;
    }

    if (ing_tunnel_ptr->flags & JNX_GW_GRE_SEQ_PRESENT) {
        ipip_sub_tunnel->ip_gre_hdr_seq_offset = 
            ipip_sub_tunnel->ip_gre_hdr_len;
        ipip_sub_tunnel->ip_gre_hdr.gre_header.hdr_flags.info.seq_num  = 1;
        ipip_sub_tunnel->gre_seq = 0xFFFFFFFF;
        ipip_sub_tunnel->ip_gre_hdr_len += GRE_FIELD_WIDTH;
    }
    else {
        ipip_sub_tunnel->ip_gre_hdr_seq_offset = 0;
        ipip_sub_tunnel->ip_gre_hdr.gre_header.hdr_flags.info.seq_num  = 0;
    }

    ipip_sub_tunnel->ip_gre_hdr.gre_header.hdr_flags.info.rsvd2        = 0;
    ipip_sub_tunnel->ip_gre_hdr.gre_header.hdr_flags.info.version      = 0;

    ipip_sub_tunnel->ip_gre_hdr.gre_header.protocol_type = ETHERTYPE_IP;

    /* Release the lock on the Tunnel */
    jnx_gw_data_release_lock(&ipip_sub_tunnel->lock);

    jnx_gw_log(LOG_DEBUG, "GRE Session Add Request success (%d, %d)",
               gre_tunnel_key.key.vrf, gre_tunnel_key.key.gre_key);

jnx_gw_send_add_rsp_to_control:

    /* Fill the response pointer */
    rsp_ptr->sub_header.err_code = err_code;
    rsp_ptr->sub_header.length   = htons(sizeof(jnx_gw_msg_gre_t));

    rsp_ptr->info.add_session.ing_tunnel_info.gre_tunnel.vrf =
        msg_ptr->info.add_session.ing_tunnel_info.gre_tunnel.vrf;

    rsp_ptr->info.add_session.ing_tunnel_info.gre_tunnel.gateway_ip =
        msg_ptr->info.add_session.ing_tunnel_info.gre_tunnel.gateway_ip;

    rsp_ptr->info.add_session.ing_tunnel_info.gre_tunnel.self_ip =
        msg_ptr->info.add_session.ing_tunnel_info.gre_tunnel.self_ip;

    rsp_ptr->info.add_session.ing_tunnel_info.gre_tunnel.gre_key =
        msg_ptr->info.add_session.ing_tunnel_info.gre_tunnel.gre_key;

    rsp_ptr->info.add_session.ing_tunnel_info.gre_tunnel.gre_seq =
        msg_ptr->info.add_session.ing_tunnel_info.gre_tunnel.gre_seq;

    return sizeof(jnx_gw_msg_gre_t);
}

/**
 * 
 * This function is used to process the GRE DEL Session messages 
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg_ptr   Pointer to the message received.
 * @param[in] rsp_ptr   Pointer to the response buffer
 */
static uint32_t
jnx_gw_data_del_gre_session(jnx_gw_data_cb_t*    app_cb,
                            jnx_gw_msg_gre_t*    msg_ptr,
                            jnx_gw_msg_gre_t*    rsp_ptr)
{
    jnx_gw_gre_key_hash_t               gre_tunnel_key;
    jnx_gw_data_gre_tunnel_t*           gre_tunnel = NULL;
    jnx_gw_data_ipip_sub_tunnel_t*      ipip_sub_tunnel = NULL;
    uint32_t                           err_code = JNX_GW_MSG_ERR_NO_ERR;
        
    memset(&gre_tunnel_key, 0, sizeof(jnx_gw_gre_key_hash_t));

    /* Control App has asked to delete a GRE Session. It involves the following :-
     *
     * 1. Extract the GRE Key.
     * 2. Perform a lookup in the GRE-DB and see if the entry exists. If
     *    not, then return an error.
     * 3. Acquire a lock on the GRE Tunnel.
     * 4. Mark the state of the tunnel as DEL
     * 5. Remove the entry from the gre db and add it in the deleted list.
     *    Update the timestamp in the entry, 
     * 6. Remove the entry from the VRF list
     * 7. Release the lock.
     * 8. Acquire a lock on the IP-IP Tunnel, using the pointer present in the
     *    gre tunnel
     * 9. Mark the state of the IP-IP Tunnel as DEL
     * 10.Remove the entry from the ip-ip tunnel db and add it in the deleted
     *    list. Update the timestamp of the entry. 
     * 11.Remove the entry from the VRF list
     * 12.Release the lock.
     * 13.Decrement the use_Count of the ip-ip stat by 2.
     */
    
    gre_tunnel_key.key.vrf     = ntohl(msg_ptr->info.del_session.gre_tunnel.vrf);
    gre_tunnel_key.key.gre_key = ntohl(msg_ptr->info.del_session.gre_tunnel.gre_key);

    jnx_gw_log(LOG_DEBUG, "GRE Session delete request (%d, %d)", 
           gre_tunnel_key.key.vrf, gre_tunnel_key.key.gre_key);
    /*
     * Perform a lookup in the gre database to find out if the entry exists.
     */
    if((gre_tunnel = jnx_gw_data_db_gre_tunnel_lookup_without_lock(app_cb, 
                                                &gre_tunnel_key)) == NULL) {
        /*
         * Session doesn't exists. Send an error response.
         */
        err_code = JNX_GW_MSG_ERR_GRE_SESS_NOT_EXIST;
        jnx_gw_log(LOG_DEBUG, "GRE Session entry is absent (%d, %d)",
               gre_tunnel_key.key.vrf, gre_tunnel_key.key.gre_key);
        goto jnx_gw_send_del_rsp_to_control;
    }

    /* Acquire lock on the entry */
    jnx_gw_data_acquire_lock(&gre_tunnel->lock);

    gre_tunnel->tunnel_state = JNX_GW_DATA_ENTRY_STATE_DEL;

    /*Remove the entry from the gre DB */
    jnx_gw_data_db_del_gre_tunnel(app_cb, gre_tunnel);

    /* Remove the entry from the VRF list */
    jnx_gw_data_db_remove_gre_tunnel_from_vrf(app_cb, gre_tunnel);
        
    /*Add the GRE Tunnel in the deleted list */
    gre_tunnel->next_in_bucket     = app_cb->del_tunnels.gre_tunnel;
    app_cb->del_tunnels.gre_tunnel = gre_tunnel;

    /*Get the pointer to the ip-ip tunnel from the gre Tunnel */
    ipip_sub_tunnel = gre_tunnel->ipip_sub_tunnel;
    
    /* Remove the entry from the IP-IP SUB Tunnel DB */
    jnx_gw_data_db_del_ipip_sub_tunnel(app_cb, ipip_sub_tunnel);

    /*Release lock on the entry */
    jnx_gw_data_release_lock(&gre_tunnel->lock);

    /*Acquire lock on the ip_ip Tunnel */
    jnx_gw_data_acquire_lock(&ipip_sub_tunnel->lock);

    ipip_sub_tunnel->tunnel_state = JNX_GW_DATA_ENTRY_STATE_DEL;

    /*Add the IP-IP Tunnel in the deleted list */
    ipip_sub_tunnel->next_in_bucket  = app_cb->del_tunnels.ipip_sub_tunnel;
    app_cb->del_tunnels.ipip_sub_tunnel = ipip_sub_tunnel;
    
    /*Release lock on the entry */
    jnx_gw_data_release_lock(&ipip_sub_tunnel->lock);

    /* Decrement the use count of the ipip-tunnel  entry */
    atomic_sub_int(2, &ipip_sub_tunnel->ipip_tunnel->use_count);

    jnx_gw_log(LOG_DEBUG, "GRE Session Delete Request success (%d, %d)",
               gre_tunnel_key.key.vrf, gre_tunnel_key.key.gre_key);

jnx_gw_send_del_rsp_to_control:

    /* Fill the response pointer */
    rsp_ptr->sub_header.err_code = err_code;
    rsp_ptr->sub_header.length   = htons(sizeof(jnx_gw_msg_gre_t));

    rsp_ptr->info.del_session.gre_tunnel.vrf =
        msg_ptr->info.del_session.gre_tunnel.vrf;

    rsp_ptr->info.del_session.gre_tunnel.gateway_ip =
        msg_ptr->info.del_session.gre_tunnel.gateway_ip;

    rsp_ptr->info.del_session.gre_tunnel.self_ip =
        msg_ptr->info.del_session.gre_tunnel.self_ip;

    rsp_ptr->info.del_session.gre_tunnel.gre_key =
        msg_ptr->info.del_session.gre_tunnel.gre_key;

    return sizeof(jnx_gw_msg_gre_t);
}


/**
 * 
 * This function is used to process the IPIP Tunnel Related messages 
 * i.e. ADD & DEL
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg       Pointer to the message received.
 * @param[in] session   Pointer to the server session on which the message
 *                      arrived.
 */
static void 
jnx_gw_data_process_ipip_msg(jnx_gw_data_cb_t*  app_cb, 
                             jnx_gw_msg_t*      msg,
                             pconn_session_t*   session) 
{
    jnx_gw_msg_ipip_t*   msg_ptr,*rsp_ptr;
    jnx_gw_msg_t*        rsp_buffer = (jnx_gw_msg_t*)(app_cb->buffer);
    int                  count = 0, ret = 0, rsp_len = 0;

    /*
     * Make the message pointer point to the sub header 
     */ 
    msg_ptr = (jnx_gw_msg_ipip_t*)((char*)msg + sizeof(jnx_gw_msg_header_t));

    /*
     * Fill the Response Message Header
     */
    rsp_buffer->msg_header.msg_type = msg->msg_header.msg_type;  
    rsp_buffer->msg_header.count    = msg->msg_header.count;
    rsp_buffer->msg_header.msg_id   = msg->msg_header.msg_id;
    
    rsp_ptr = (jnx_gw_msg_ipip_t*)(((char*)rsp_buffer + 
                                    sizeof(jnx_gw_msg_header_t)));
    rsp_len = sizeof(jnx_gw_msg_header_t);
    
    for (count = 0; count < msg->msg_header.count; count++) {

        /*
         * Determine the sub header type and based on that perform
         * the appropriate action.
         */
        rsp_ptr->sub_header.length = htons(4);
        rsp_ptr->sub_header.sub_type = msg_ptr->sub_header.sub_type;

         switch(msg_ptr->sub_header.sub_type) {
             
             case JNX_GW_ADD_IP_IP_SESSION:

                 ret = jnx_gw_data_add_ipip_tunnel_entry(app_cb, msg_ptr,
                                                         rsp_ptr);
                 break;

             case JNX_GW_DEL_IP_IP_SESSION:

                 ret = jnx_gw_data_del_ipip_tunnel_entry(app_cb, msg_ptr,
                                                         rsp_ptr);
                 break;

             default:
                 jnx_gw_log(LOG_DEBUG, "Invalid IPIP Message type");
                 /* Send an error response */
                 break;
         }

        msg_ptr = (jnx_gw_msg_ipip_t*)((char*)msg_ptr + 
                                       ntohs(msg_ptr->sub_header.length));
        rsp_ptr = (jnx_gw_msg_ipip_t*)((char*)rsp_ptr +  ret);
        rsp_len += ret;

    }

    rsp_buffer->msg_header.msg_len = htons(rsp_len);
    pconn_server_send(session, JNX_GW_IPIP_TUNNEL_MSG, rsp_buffer, rsp_len); 
    return;
}


/**
 * This function is used to process the IPIP Add Tunnel messages 
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg       Pointer to the message received.
 * @param[in] rsp_ptr   Pointer to the response being sent. 
 */
static uint32_t
jnx_gw_data_add_ipip_tunnel_entry(jnx_gw_data_cb_t*   app_cb,
                                  jnx_gw_msg_ipip_t*  msg_ptr,
                                  jnx_gw_msg_ipip_t*  rsp_ptr)
{
    jnx_gw_ipip_tunnel_key_hash_t    ipip_tunnel_key;
    uint32_t                        tunnel_ip;
    jnx_gw_data_ipip_tunnel_t*       ipip_tunnel = NULL;
    int                              err_code = JNX_GW_MSG_ERR_NO_ERR;
    jnx_gw_data_vrf_stat_t*          vrf_entry = NULL;

    memset(&ipip_tunnel_key, 0, sizeof(jnx_gw_ipip_tunnel_key_hash_t));

    /*
     * Control APP has asked us to add an IP-IP Tunnel, It involves the
     * following:-
     * 1. Extract the key from the Message.
     * 2. Perform a lookup on the IP_IP Stat DB to find out if the entry exists.
     * 3. If the entry exists, return an error.
     * 4. Add the entry in the ipip_tunnel db.
     * 5. Initialise the entry
     * 6. Add the entry in the VRF DB to get the summary stats.
     */ 

    if(msg_ptr->info.add_tunnel.tunnel_type.tunnel_type != 
       JNX_GW_TUNNEL_TYPE_IPIP) {

        /*Send Error Response */
        err_code = JNX_GW_MSG_ERR_INVALID_TUNNEL_TYPE;
        goto jnx_gw_send_add_rsp_to_control;
    }

    ipip_tunnel_key.key.gateway_ip = ntohl(msg_ptr->info.add_tunnel.
                                           ipip_tunnel.gateway_ip);
    ipip_tunnel_key.key.vrf        = ntohl(msg_ptr->info.add_tunnel.
                                           ipip_tunnel.vrf);
    tunnel_ip                      = ntohl(msg_ptr->info.add_tunnel.
                                           ipip_tunnel.self_ip);
        
    jnx_gw_log(LOG_INFO, "IPIP Gateway Add (%d, %s)", 
           ipip_tunnel_key.key.vrf,
           JNX_GW_IP_ADDRA(ipip_tunnel_key.key.gateway_ip));

    if((ipip_tunnel = jnx_gw_data_db_ipip_tunnel_lookup_without_lock(app_cb, 
                                                 &ipip_tunnel_key)) != NULL) {

        /* Entry exists already and hence return an error */
        err_code = JNX_GW_MSG_ERR_IPIP_SESS_EXISTS;
        jnx_gw_log(LOG_INFO, "IPIP Gateway (%d, %s) is present", 
               ipip_tunnel_key.key.vrf,
               JNX_GW_IP_ADDRA(ipip_tunnel_key.key.gateway_ip));
        goto jnx_gw_send_add_rsp_to_control;
    }

    /* Add an entry into the ipip_tunnel db. */
    if((ipip_tunnel = jnx_gw_data_db_add_ipip_tunnel(app_cb, 
                                   &ipip_tunnel_key)) == NULL) {

       /* Couldn;t add an entry in the data base, send an error response */
        err_code = JNX_GW_MSG_ERR_MEM_ALLOC_FAIL;
        goto jnx_gw_send_add_rsp_to_control;
    }

    if((vrf_entry = jnx_gw_data_db_add_vrf_entry(app_cb, 
                                          ipip_tunnel_key.key.vrf)) == NULL) {

        /* Could not add an entry for the vrf to maintain the aggregate stats */
        err_code = JNX_GW_MSG_ERR_MEM_ALLOC_FAIL;
        goto jnx_gw_send_add_rsp_to_control;
    }

    /* Acquire a lock on the tunnel */
    jnx_gw_data_acquire_lock(&ipip_tunnel->lock);

    ipip_tunnel->tunnel_state = JNX_GW_DATA_ENTRY_STATE_READY;
    ipip_tunnel->self_ip      = tunnel_ip;
    ipip_tunnel->use_count    = 0;
    ipip_tunnel->ing_vrf      = vrf_entry;

    /* Add this entry in the VRF so as to enable fetching stats on a per 
     * VRF basis 
     */ 
    ipip_tunnel->next_in_vrf    = vrf_entry->next_ipip_tunnel;
    vrf_entry->next_ipip_tunnel = (jnx_gw_data_ipip_tunnel_t*)((char*)ipip_tunnel +
                                   offsetof(jnx_gw_data_ipip_tunnel_t, 
                                           next_in_vrf));
    /* Release the lock on the tunnel */
   jnx_gw_data_release_lock(&ipip_tunnel->lock);

   jnx_gw_log(LOG_INFO, "IPIP Gateway (%d, %s) add successful", 
              ipip_tunnel_key.key.vrf,
              JNX_GW_IP_ADDRA(ipip_tunnel_key.key.gateway_ip));
        
jnx_gw_send_add_rsp_to_control:

    /* Fill the response pointer */
    rsp_ptr->sub_header.err_code = err_code;
    rsp_ptr->sub_header.length   = htons(sizeof(jnx_gw_msg_ipip_t));

    rsp_ptr->info.add_tunnel.ipip_tunnel.gateway_ip =
        msg_ptr->info.add_tunnel.ipip_tunnel.gateway_ip;

    rsp_ptr->info.add_tunnel.ipip_tunnel.vrf =
        msg_ptr->info.add_tunnel.ipip_tunnel.vrf;

    rsp_ptr->info.add_tunnel.ipip_tunnel.self_ip =
        msg_ptr->info.add_tunnel.ipip_tunnel.self_ip;

    return sizeof(jnx_gw_msg_ipip_t);
}

/**
 * This function is used to process the IPIP DEL Tunnel messages 
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg       Pointer to the message received.
 * @param[in] rsp_ptr   Pointer to the response being sent. 
 */
static uint32_t 
jnx_gw_data_del_ipip_tunnel_entry(jnx_gw_data_cb_t*   app_cb,
                                  jnx_gw_msg_ipip_t*  msg_ptr,
                                  jnx_gw_msg_ipip_t*  rsp_ptr)
{
    jnx_gw_ipip_tunnel_key_hash_t    ipip_tunnel_key;
    int                              err_code = JNX_GW_MSG_ERR_NO_ERR;
    jnx_gw_data_ipip_tunnel_t*       ipip_tunnel = NULL;

    memset(&ipip_tunnel_key, 0, sizeof(jnx_gw_ipip_tunnel_key_hash_t));
    /*
     * Control APP has asked us to del an IP-IP Tunnel, It involves the
     * following:-
     * 1. Extract the key from the Message.
     * 2. Perform a lookup on the IP_IP TUNNEL DB to find out if the entry exists.
     * 3. If the entry doesn't exists, return an error.
     * 4. Delete the entry in the ipip_tunnel db.
     * 5. Initialise the entry
     * 6. Add the entry from the VRF list to get the summary stats.
     */ 
    if(msg_ptr->info.del_tunnel.tunnel_type.tunnel_type !=  
       JNX_GW_TUNNEL_TYPE_IPIP) {

        /*Send Error Response */
        err_code = JNX_GW_MSG_ERR_INVALID_TUNNEL_TYPE;
        goto jnx_gw_send_del_rsp_to_control;
    }

    ipip_tunnel_key.key.gateway_ip = ntohl(msg_ptr->info.del_tunnel.ipip_tunnel.gateway_ip);
    ipip_tunnel_key.key.vrf        = ntohl(msg_ptr->info.del_tunnel.ipip_tunnel.vrf);

        jnx_gw_log(LOG_INFO, "IPIP Gateway (%d, %s) delete",
               ipip_tunnel_key.key.vrf,
               JNX_GW_IP_ADDRA(ipip_tunnel_key.key.gateway_ip));
    if((ipip_tunnel = jnx_gw_data_db_ipip_tunnel_lookup_without_lock(app_cb, 
                                                                     &ipip_tunnel_key)) == NULL) {

        /* Entry exists already and hence return an error */
        err_code = JNX_GW_MSG_ERR_IPIP_SESS_NOT_EXIST;
        jnx_gw_log(LOG_INFO, "IPIP Gateway (%d, %s) is absent",
               ipip_tunnel_key.key.vrf,
               JNX_GW_IP_ADDRA(ipip_tunnel_key.key.gateway_ip));
        goto jnx_gw_send_del_rsp_to_control;
    }

    if(ipip_tunnel->use_count > 0) {
        /* Implies the tunnel is being used by various GRE sessions and hence
         * can't be remvoed. 
         */
         jnx_gw_log(LOG_INFO, "IPIP Gateway (%d, %s) with active GRE"
                    " sessions can not be deleted",
                   ipip_tunnel_key.key.vrf,
                   JNX_GW_IP_ADDRA(ipip_tunnel_key.key.gateway_ip));
        err_code = JNX_GW_MSG_ERR_IP_IP_TUNNEL_IN_USE;
        goto jnx_gw_send_del_rsp_to_control;

    }

    /* Delete  an entry from the ipip_tunnel db. */
    jnx_gw_data_db_del_ipip_tunnel(app_cb, ipip_tunnel);

    /* Acquire a lock on the tunnel */
    jnx_gw_data_acquire_lock(&ipip_tunnel->lock);

    /* Remove the entry from the VRF list */
    jnx_gw_data_db_remove_ipip_tunnel_from_vrf(app_cb, ipip_tunnel);

    ipip_tunnel->tunnel_state = JNX_GW_DATA_ENTRY_STATE_DEL;

    /*Add the IP-IP Tunnel in the deleted list */
    ipip_tunnel->next_in_bucket  = app_cb->del_tunnels.ipip_tunnel;
    app_cb->del_tunnels.ipip_tunnel = ipip_tunnel;

    /* Release the lock on the tunnel */
    jnx_gw_data_release_lock(&ipip_tunnel->lock);

    jnx_gw_log(LOG_INFO, "IPIP Gateway (%d, %s) delete successful", 
               ipip_tunnel_key.key.vrf,
               JNX_GW_IP_ADDRA(ipip_tunnel_key.key.gateway_ip));
         
jnx_gw_send_del_rsp_to_control:

    /* Fill the response pointer */
    rsp_ptr->sub_header.err_code = err_code;
    rsp_ptr->sub_header.length   = htons(sizeof(jnx_gw_msg_ipip_t));

    rsp_ptr->info.add_tunnel.ipip_tunnel.gateway_ip =
        msg_ptr->info.add_tunnel.ipip_tunnel.gateway_ip;

    rsp_ptr->info.add_tunnel.ipip_tunnel.vrf =
        msg_ptr->info.add_tunnel.ipip_tunnel.vrf;

    rsp_ptr->info.add_tunnel.ipip_tunnel.self_ip =
        msg_ptr->info.add_tunnel.ipip_tunnel.self_ip;

    return sizeof(jnx_gw_msg_ipip_t);
}


/**
 * This function is used to process the STAT FETCH messages 
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg       Pointer to the message received.
 * @param[in] session   Pointer to the pconn server session. 
 */
static void 
jnx_gw_data_process_stat_fetch_msg(jnx_gw_data_cb_t*    app_cb, 
                                   jnx_gw_msg_t*        msg,
                                   pconn_session_t*     session) 
{
    jnx_gw_msg_stat_t*      msg_ptr = NULL;
    jnx_gw_msg_t*           rsp_buffer = ((jnx_gw_msg_t*)(app_cb->buffer));

    /*
     * Make the message pointer point to the sub header 
     */ 
    msg_ptr = (jnx_gw_msg_stat_t*)((char*)msg + sizeof(jnx_gw_msg_header_t));

    /*
     * Fill the Response Message Header
     */
    rsp_buffer->msg_header.msg_type = msg->msg_header.msg_type;  
    rsp_buffer->msg_header.count    = msg->msg_header.count;
    rsp_buffer->msg_header.msg_id   = msg->msg_header.msg_id;
    
    /*
     * Determine the sub header type and based on that perform
     * the appropriate action.
     */

     switch(msg_ptr->sub_header.sub_type) {
         
         case JNX_GW_FETCH_SUMMARY_STAT:

             jnx_gw_data_fetch_all_vrf_summary_stats(app_cb, rsp_buffer, session);
             break;

         case JNX_GW_FETCH_SUMMARY_VRF_STAT:

             jnx_gw_data_fetch_vrf_summary_stats(app_cb, msg_ptr, rsp_buffer, session);
             break;

         case JNX_GW_FETCH_EXTENSIVE_STAT:

             jnx_gw_data_fetch_all_vrf_extensive_stats(app_cb, rsp_buffer, 
                                                       session);
             break;

         case JNX_GW_FETCH_EXTENSIVE_VRF_STAT:

             jnx_gw_data_fetch_vrf_extensive_stats(app_cb, msg_ptr, rsp_buffer,
                                                   session);
             break;

         case JNX_GW_FETCH_IPIP_STAT:

             jnx_gw_data_fetch_ipip_stat(app_cb, msg_ptr, rsp_buffer, session);
             break;

         case JNX_GW_FETCH_GRE_STAT:

             jnx_gw_data_fetch_gre_stat(app_cb, msg_ptr, rsp_buffer, session);
             break;

         default:
             /*Send Error Response */
             break;
     }

     return;
}


/**
 * This function is used to fetch the stats for a particular GRE TUNNEL
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg       Pointer to the message received.
 * @param[in] rsp_msg   Pointer to the response buffer
 * @param[in] session   Pointer to the pconn server session. 
 */
static void 
jnx_gw_data_fetch_gre_stat(jnx_gw_data_cb_t*       app_cb,
                           jnx_gw_msg_stat_t*      msg_ptr,
                           jnx_gw_msg_t*           rsp_msg,
                           pconn_session_t*        session)
{
    jnx_gw_gre_key_hash_t       gre_tunnel_key;
    uint32_t                   err_code = JNX_GW_MSG_ERR_NO_ERR;
    uint16_t                   msg_len = sizeof(jnx_gw_msg_header_t);
    jnx_gw_data_gre_tunnel_t*   gre_tunnel =NULL;
    jnx_gw_msg_stat_rsp_t*      rsp_ptr;

    rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                       sizeof(jnx_gw_msg_header_t));

    rsp_ptr->info.gre_stat.sub_header.length = 
                 (sizeof(jnx_gw_msg_sub_header_t) +
                  sizeof(jnx_gw_gre_key_hash_t));

    rsp_ptr->info.gre_stat.sub_header.sub_type = msg_ptr->sub_header.sub_type;
    
    memset(&gre_tunnel_key, 0, sizeof(jnx_gw_gre_key_hash_t));

    /* Extract the Key from the Message */
    gre_tunnel_key.key.vrf     = ntohl(msg_ptr->info.gre_stat.vrf);
    gre_tunnel_key.key.gre_key = ntohl(msg_ptr->info.gre_stat.gre_key);

    /* Perform a lookup in the gre DB to get the tunnel entry */
    if((gre_tunnel = jnx_gw_data_db_gre_tunnel_lookup_without_lock(app_cb, 
                                               &gre_tunnel_key)) == NULL) {
        /*
         * Session doesn't exists. Send an error response.
         */
        err_code = JNX_GW_MSG_ERR_GRE_SESS_NOT_EXIST;
        goto jnx_gw_send_stat_rsp_to_control;
    }

    /* Start preparing the response for th GRE Stats */

    /* First fill the Key itself */
    rsp_ptr->info.gre_stat.gre_key.vrf     = msg_ptr->info.gre_stat.vrf;
    rsp_ptr->info.gre_stat.gre_key.gre_key = msg_ptr->info.gre_stat.gre_key;

    /* Fill the stats in the message */
    rsp_ptr->info.gre_stat.stats.packets_in    = 
                                htonl(gre_tunnel->stats.packets_in);
    rsp_ptr->info.gre_stat.stats.packets_out   = 
                                htonl(gre_tunnel->stats.packets_out);
    rsp_ptr->info.gre_stat.stats.bytes_in      = 
                                htonl(gre_tunnel->stats.bytes_in);
    rsp_ptr->info.gre_stat.stats.bytes_out     = 
                                htonl(gre_tunnel->stats.bytes_out);
    rsp_ptr->info.gre_stat.stats.checksum_fail = 
                                htonl(gre_tunnel->stats.checksum_fail);
    rsp_ptr->info.gre_stat.stats.ttl_drop      = 
                                htonl(gre_tunnel->stats.ttl_drop);
    rsp_ptr->info.gre_stat.stats.cong_drop     = 
                                htonl(gre_tunnel->stats.cong_drop);

    rsp_ptr->info.gre_stat.sub_header.length += sizeof(jnx_gw_common_stat_t); 

jnx_gw_send_stat_rsp_to_control:
    msg_len = rsp_ptr->info.gre_stat.sub_header.length + 
                            sizeof(jnx_gw_msg_header_t);

    rsp_msg->msg_header.msg_len = htons(msg_len);
    
    rsp_ptr->info.gre_stat.sub_header.length =
        htons(rsp_ptr->info.gre_stat.sub_header.length);

    rsp_ptr->info.gre_stat.sub_header.err_code = err_code;

    pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg, msg_len); 

    return;
}

/**
 * This function is used to fetch the stats for a particular IP-IP TUNNEL
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg       Pointer to the message received.
 * @param[in] rsp_msg   Pointer to the response buffer
 * @param[in] session   Pointer to the pconn server session. 
 */
static void 
jnx_gw_data_fetch_ipip_stat(jnx_gw_data_cb_t*       app_cb,
                            jnx_gw_msg_stat_t*      msg_ptr,
                            jnx_gw_msg_t*           rsp_msg,
                            pconn_session_t*        session)
{
    jnx_gw_ipip_tunnel_key_hash_t    ipip_tunnel_key;
    uint32_t                        err_code = JNX_GW_MSG_ERR_NO_ERR;
    uint16_t                        msg_len = sizeof(jnx_gw_msg_header_t);
    jnx_gw_data_ipip_tunnel_t*       ipip_tunnel_entry = NULL;
    jnx_gw_msg_stat_rsp_t*           rsp_ptr;

    rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                       sizeof(jnx_gw_msg_header_t));
        
    rsp_ptr->info.ipip_stat.sub_header.length = sizeof(jnx_gw_msg_sub_header_t) +
                                                sizeof(jnx_gw_ipip_tunnel_key_t);
    
    rsp_ptr->info.ipip_stat.sub_header.sub_type = msg_ptr->sub_header.sub_type;
   
    memset(&ipip_tunnel_key, 0, sizeof(jnx_gw_ipip_tunnel_key_t));

    /* Extract the Key from the Message */
    ipip_tunnel_key.key.vrf        = ntohl(msg_ptr->info.ipip_stat.vrf);
    ipip_tunnel_key.key.gateway_ip = ntohl(msg_ptr->info.ipip_stat.gateway_ip);

    /* Perform a lookup in the gre DB to get the tunnel entry */
    if((ipip_tunnel_entry = jnx_gw_data_db_ipip_tunnel_lookup_without_lock(
                                 app_cb, &ipip_tunnel_key)) == NULL) {
        /*
         * Session doesn't exists. Send an error response.
         */
         jnx_gw_log(LOG_DEBUG, "IPIP tunnel (%d, %s) entry is absent",
                    ipip_tunnel_key.key.vrf,
                    JNX_GW_IP_ADDRA(ipip_tunnel_key.key.gateway_ip));
        err_code = JNX_GW_MSG_ERR_IPIP_SESS_NOT_EXIST;
        goto jnx_gw_send_stat_rsp_to_control;
    }

    /* Start preparing the response for the IP-IP Stats */

    /* First fill the Key itself */
    rsp_ptr->info.ipip_stat.ipip_key.vrf        = 
                        (msg_ptr->info.ipip_stat.vrf);
    rsp_ptr->info.ipip_stat.ipip_key.gateway_ip = 
                        (msg_ptr->info.ipip_stat.gateway_ip);

    /* Fill the stats in the message */
    rsp_ptr->info.ipip_stat.stats.packets_in    = 
                        htonl(ipip_tunnel_entry->stats.packets_in);
    rsp_ptr->info.ipip_stat.stats.packets_out   = 
                        htonl(ipip_tunnel_entry->stats.packets_out);
    rsp_ptr->info.ipip_stat.stats.bytes_in      = 
                        htonl(ipip_tunnel_entry->stats.bytes_in);
    rsp_ptr->info.ipip_stat.stats.bytes_out     = 
                        htonl(ipip_tunnel_entry->stats.bytes_out);
    rsp_ptr->info.ipip_stat.stats.checksum_fail = 
                        htonl(ipip_tunnel_entry->stats.checksum_fail);
    rsp_ptr->info.ipip_stat.stats.ttl_drop      = 
                        htonl(ipip_tunnel_entry->stats.ttl_drop);
    rsp_ptr->info.ipip_stat.stats.cong_drop     = 
                        htonl(ipip_tunnel_entry->stats.cong_drop);

    rsp_ptr->info.ipip_stat.sub_header.length += sizeof(jnx_gw_common_stat_t); 

jnx_gw_send_stat_rsp_to_control:
    msg_len += rsp_ptr->info.ipip_stat.sub_header.length;
    
    rsp_msg->msg_header.msg_len = htons(msg_len);
    
    rsp_ptr->info.ipip_stat.sub_header.length =
        htons(rsp_ptr->info.ipip_stat.sub_header.length);
    
    rsp_ptr->info.ipip_stat.sub_header.err_code = err_code;

    pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg, msg_len); 

    return;
}


/**
 * This function is used to fetch the summary stats for a particular VRF
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg       Pointer to the message received.
 * @param[in] rsp_msg   Pointer to the response buffer
 * @param[in] session   Pointer to the pconn server session. 
 */
static void
jnx_gw_data_fetch_vrf_summary_stats(jnx_gw_data_cb_t*       app_cb,
                                    jnx_gw_msg_stat_t*      msg_ptr,
                                    jnx_gw_msg_t*           rsp_msg,
                                    pconn_session_t*        session)
{
    uint32_t                       err_code = JNX_GW_MSG_ERR_NO_ERR;
    uint16_t                       msg_len  = sizeof(jnx_gw_msg_header_t);
    uint32_t                       vrf = 0;
    jnx_gw_data_vrf_stat_t*         vrf_entry = NULL;
    jnx_gw_msg_stat_rsp_t*          rsp_ptr;
        
    rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                       sizeof(jnx_gw_msg_header_t));

    /* Extract the Key from the Message */
    vrf  = ntohl(msg_ptr->info.summary_vrf_stat.vrf);

    rsp_ptr->info.summary_vrf_stat.sub_header.length = 
        sizeof(jnx_gw_msg_sub_header_t) + sizeof(vrf);

    rsp_ptr->info.summary_vrf_stat.sub_header.sub_type = 
                                        JNX_GW_FETCH_SUMMARY_VRF_STAT;

    if((vrf_entry = jnx_gw_data_db_vrf_entry_lookup (app_cb, vrf)) == NULL) {

        jnx_gw_log(LOG_DEBUG, "VRF entry (%d) is absent", vrf);
        err_code = JNX_GW_MSG_ERR_VRF_NOT_EXIST;
        goto jnx_gw_send_stat_rsp_to_control;
    }

    /* Start preparing the response */
    rsp_ptr->info.summary_vrf_stat.vrf = htonl(vrf);

    /*Fill the vrf specific stats */
    rsp_ptr->info.summary_vrf_stat.vrf_stats.tunnel_not_present = 
            htonl(vrf_entry->vrf_stats.tunnel_not_present);
    
    rsp_ptr->info.summary_vrf_stat.vrf_stats.invalid_pkt   =
            htonl(vrf_entry->vrf_stats.invalid_pkt);

    rsp_ptr->info.summary_vrf_stat.vrf_stats.active_sessions = 
            htonl(vrf_entry->vrf_stats.active_sessions);
    
    rsp_ptr->info.summary_vrf_stat.vrf_stats.total_sessions   =
            htonl(vrf_entry->vrf_stats.total_sessions);

    /* Fill the summary stats for the VRF */
    rsp_ptr->info.summary_vrf_stat.summary_stats.packets_in =
            htonl(vrf_entry->stats.packets_in);
    rsp_ptr->info.summary_vrf_stat.summary_stats.packets_out =
            htonl(vrf_entry->stats.packets_out);
    rsp_ptr->info.summary_vrf_stat.summary_stats.bytes_in =
            htonl(vrf_entry->stats.bytes_in);
    rsp_ptr->info.summary_vrf_stat.summary_stats.bytes_out =
            htonl(vrf_entry->stats.bytes_out);
    rsp_ptr->info.summary_vrf_stat.summary_stats.checksum_fail =
            htonl(vrf_entry->stats.checksum_fail);
    rsp_ptr->info.summary_vrf_stat.summary_stats.ttl_drop = 
            htonl(vrf_entry->stats.ttl_drop);
    rsp_ptr->info.summary_vrf_stat.summary_stats.cong_drop = 
            htonl(vrf_entry->stats.cong_drop);

    rsp_ptr->info.summary_vrf_stat.sub_header.length += 
                                            (sizeof(jnx_gw_common_stat_t) +
                                             sizeof(jnx_gw_vrf_stat_t)); 
jnx_gw_send_stat_rsp_to_control:

    msg_len += rsp_ptr->info.summary_vrf_stat.sub_header.length;

    rsp_ptr->info.summary_vrf_stat.sub_header.length =
        htons(rsp_ptr->info.summary_vrf_stat.sub_header.length);
    
    rsp_ptr->info.summary_vrf_stat.sub_header.err_code = err_code;

    rsp_msg->msg_header.msg_len = htons(msg_len);


    pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg, msg_len); 

    return;
}

/**
 * This function is used to fetch the extensive stats for all the tunnels in all
 * the VRFs. In case the response exceeds the max buffer length, function will
 * split the response and send mutiple packets for the same.
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] rsp_msg   Pointer to the response buffer
 * @param[in] session   Pointer to the pconn server session. 
 */
static void
jnx_gw_data_fetch_all_vrf_extensive_stats(jnx_gw_data_cb_t*       app_cb,
                                          jnx_gw_msg_t*           rsp_msg,
                                          pconn_session_t*        session)
{
    uint16_t                       msg_len  = sizeof(jnx_gw_msg_header_t);
    jnx_gw_data_vrf_stat_t*         vrf_entry = NULL;
    char*                           rsp_buf;
    jnx_gw_data_ipip_tunnel_t*      ipip_tunnel = NULL;
    jnx_gw_data_gre_tunnel_t*       gre_tunnel = NULL;
    int                             i = 0;
    int                             count = 0;
    jnx_gw_msg_stat_rsp_t*          rsp_ptr;

    rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                       sizeof(jnx_gw_msg_header_t));
    rsp_buf = (char*)rsp_ptr;
    /*
     * Single response buffer can be of 4K size. So, if the the response is going
     * to cross the 4K limit, then we will split the message and send it in
     * separate messages.
     *
     * We need to traverse the VRF list and for each vrf entry collect the
     * summary and also all the GRE & IP-IP Tunnels in the vrf.
     */

    for (i = 0; i < JNX_GW_DATA_MAX_VRF_BUCKETS; i++) {

        /* 
         * Extensive Summary Stats comprises of :-
         * 1. Summary stats for the VRF
         * 2. Stats for all the GRE Tunnels in the VRF
         * 3. Stats for all the IP-IP Tunnels in the VRF.
         */

        for(vrf_entry = app_cb->vrf_db.hash_bucket[i].chain;
            vrf_entry != NULL;
            vrf_entry = vrf_entry->next_in_bucket) {

            /* Check if there is space for VRF sub header, vrf key and summary
             * stats.
             */
            if((msg_len + sizeof(jnx_gw_msg_sub_header_t) +
                sizeof(jnx_gw_msg_summary_stat_rsp_t)) > JNX_GW_DATA_MAX_BUF_SIZE) {

                /* Send the message to the MGMT and then reuse the buffer to
                 * fill the remaining portion of the data.
                 */
                rsp_msg->msg_header.more = 1;
                rsp_msg->msg_header.msg_len = htons(msg_len);
                rsp_msg->msg_header.count = count;

                if(pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg,
                                     msg_len)) {
                    return;
                }
                
                /* Now reset the fields so that the further response can be
                 * sent
                 */ 
                count = 0;
                rsp_msg->msg_header.more = 0;
                msg_len = sizeof(jnx_gw_msg_header_t);

                rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                                   sizeof(jnx_gw_msg_header_t));
                rsp_buf = (char*)rsp_ptr;
            }

            if(vrf_entry->state != JNX_GW_DATA_ENTRY_STATE_READY) {
                continue;
            }
            count++;

            /* There is space in the current buffer to accomodate the VRF
             * summary.
             */
            ((jnx_gw_msg_sub_header_t*)rsp_buf)->length = 
                                   htons (sizeof(jnx_gw_msg_summary_stat_rsp_t));
            
            ((jnx_gw_msg_sub_header_t*)rsp_buf)->sub_type =
                                        JNX_GW_FETCH_SUMMARY_VRF_STAT; 

            ((jnx_gw_msg_sub_header_t*)rsp_buf)->err_code = 
                                        JNX_GW_MSG_ERR_NO_ERR; 

            rsp_buf = rsp_buf + sizeof(jnx_gw_msg_sub_header_t);

            /* Fill the VRF ID */
            *(uint32_t*)rsp_buf = htonl(vrf_entry->key);

            rsp_buf = rsp_buf + sizeof(uint32_t);

            /* Fill the VRF Stats */
            jnx_gw_data_fill_vrf_summary_stats(vrf_entry, rsp_buf);

            rsp_buf = rsp_buf + sizeof(jnx_gw_vrf_stat_t) +
                                sizeof(jnx_gw_common_stat_t);

            msg_len += sizeof(jnx_gw_msg_summary_stat_rsp_t);

            /* Fill the information regarding all the GRE tunnels in the VRF */
            for(gre_tunnel = vrf_entry->next_gre_tunnel;
                gre_tunnel != NULL;
                gre_tunnel = gre_tunnel->next_in_vrf) {

                gre_tunnel = (jnx_gw_data_gre_tunnel_t*)((char*)gre_tunnel - 
                               offsetof(jnx_gw_data_gre_tunnel_t, next_in_vrf));

                if((msg_len + sizeof(jnx_gw_msg_sub_header_t) + 
                    sizeof(jnx_gw_gre_key_t) +
                    sizeof(jnx_gw_common_stat_t)) > JNX_GW_DATA_MAX_BUF_SIZE) {

                    /* Send the message to the MGMT and then reuse the buffer to
                     * fill the remaining portion of the data.
                     */

                    rsp_msg->msg_header.more = 1;
                    rsp_msg->msg_header.count = count;
                    rsp_msg->msg_header.msg_len = htons(msg_len);

                    if(pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, 
                                         rsp_msg, msg_len)) {
                        return;
                    }
                    
                    /* Now reset the fields so that the further response can be
                     * sent
                     */ 
                    count = 0;
                    rsp_msg->msg_header.more = 0;
                    msg_len = sizeof(jnx_gw_msg_header_t);

                    rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                                        sizeof(jnx_gw_msg_header_t));
                    rsp_buf = (char*)rsp_ptr;
                }

                if(gre_tunnel->tunnel_state != JNX_GW_DATA_ENTRY_STATE_READY)
                    continue;

                count++;

                /* Fill the Sub Header for the GRE TUNNEL STAT */
                ((jnx_gw_msg_sub_header_t*)rsp_buf)->sub_type = 
                                                        JNX_GW_FETCH_GRE_STAT;

                ((jnx_gw_msg_sub_header_t*)rsp_buf)->length = 
                                     htons(sizeof(jnx_gw_msg_sub_header_t) +
                                           sizeof(jnx_gw_gre_key_t) +
                                           sizeof(jnx_gw_common_stat_t));
                
                ((jnx_gw_msg_sub_header_t*)rsp_buf)->err_code = JNX_GW_MSG_ERR_NO_ERR;

                rsp_buf = rsp_buf + sizeof(jnx_gw_msg_sub_header_t);

                /* Fill the KEY for the TUNNEL */
                ((jnx_gw_gre_key_t*)rsp_buf)->vrf = htonl(gre_tunnel->key.vrf); 
                ((jnx_gw_gre_key_t*)rsp_buf)->gre_key = 
                                        htonl(gre_tunnel->key.gre_key); 
                
                rsp_buf = rsp_buf + sizeof(jnx_gw_gre_key_t);

                jnx_gw_data_fill_gre_tunnel_stats(gre_tunnel, rsp_buf);

                rsp_buf = rsp_buf + sizeof(jnx_gw_common_stat_t);

                msg_len += sizeof(jnx_gw_msg_sub_header_t) + 
                           sizeof(jnx_gw_gre_key_t) +
                           sizeof(jnx_gw_common_stat_t);
           }

           /* Fill the information regarding all the IP-IP tunnels */ 
           for(ipip_tunnel = vrf_entry->next_ipip_tunnel;
               ipip_tunnel != NULL;
               ipip_tunnel = ipip_tunnel->next_in_vrf) {

                ipip_tunnel = (jnx_gw_data_ipip_tunnel_t*)((char*)ipip_tunnel - 
                               offsetof(jnx_gw_data_ipip_tunnel_t, next_in_vrf));

                if((msg_len + sizeof(jnx_gw_msg_sub_header_t) + 
                    sizeof(jnx_gw_ipip_tunnel_key_t) +
                    sizeof(jnx_gw_common_stat_t)) > JNX_GW_DATA_MAX_BUF_SIZE) {

                    /* Send the message to the MGMT and then reuse the buffer to
                     * fill the remaining portion of the data.
                     */
                    rsp_msg->msg_header.more = 1;
                    rsp_msg->msg_header.msg_len = htons(msg_len);
                    rsp_msg->msg_header.count = count;

                    if(pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg, 
                                         msg_len)) {
                        return;
                    }
                    
                    /* Now reset the fields so that the further response can be
                     * sent
                     */ 
                    count = 0;
                    rsp_msg->msg_header.more = 0;
                    msg_len = sizeof(jnx_gw_msg_header_t);

                    rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                                  sizeof(jnx_gw_msg_header_t));
                    rsp_buf = (char*)rsp_ptr;
                }

                if(ipip_tunnel->tunnel_state != JNX_GW_DATA_ENTRY_STATE_READY)
                    continue;

                count++;
                /* Fill the Sub Header for the IP-IP TUNNEL STAT */
                ((jnx_gw_msg_sub_header_t*)rsp_buf)->sub_type = 
                                                        JNX_GW_FETCH_IPIP_STAT;

                ((jnx_gw_msg_sub_header_t*)rsp_buf)->length = 
                                        htons(sizeof(jnx_gw_msg_sub_header_t)  +
                                              sizeof(jnx_gw_ipip_tunnel_key_t) +
                                              sizeof(jnx_gw_common_stat_t));

                ((jnx_gw_msg_sub_header_t*)rsp_buf)->err_code = 
                                                JNX_GW_MSG_ERR_NO_ERR;

                rsp_buf = rsp_buf + sizeof(jnx_gw_msg_sub_header_t);

                /* Fill the KEY for the TUNNEL */
                ((jnx_gw_ipip_tunnel_key_t*)rsp_buf)->vrf = 
                                             htonl(ipip_tunnel->key.vrf); 
                ((jnx_gw_ipip_tunnel_key_t*)rsp_buf)->gateway_ip = 
                                             htonl(ipip_tunnel->key.gateway_ip); 
                
                rsp_buf = rsp_buf + sizeof(jnx_gw_ipip_tunnel_key_t);

                jnx_gw_data_fill_ipip_tunnel_stats(ipip_tunnel, rsp_buf);

                rsp_buf = rsp_buf + sizeof(jnx_gw_common_stat_t);

                msg_len += sizeof(jnx_gw_msg_sub_header_t)  + 
                           sizeof(jnx_gw_ipip_tunnel_key_t) +
                           sizeof(jnx_gw_common_stat_t);

           }
        }
    }

    rsp_msg->msg_header.msg_len = htons(msg_len);
    rsp_msg->msg_header.count = count;
    rsp_msg->msg_header.more = 0;

    pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg, msg_len); 
    return;
}

/**
 * This function is used to fetch the extensive stats for all the tunnels in a
 * particular VRF. In case the response lenght exceeds the max response buffer 
 * size, it will split the response and will send multiple repsonses for the
 * same.
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] msg_ptr   msg_ptr
 * @param[in] rsp_msg   Pointer to the response buffer
 * @param[in] session   Pointer to the pconn server session. 
 */
static void
jnx_gw_data_fetch_vrf_extensive_stats(jnx_gw_data_cb_t*       app_cb,
                                      jnx_gw_msg_stat_t*      msg_ptr,
                                      jnx_gw_msg_t*           rsp_msg,
                                      pconn_session_t*        session)
{
    uint32_t                       err_code = JNX_GW_MSG_ERR_NO_ERR;
    uint16_t                       msg_len  = sizeof(jnx_gw_msg_header_t);
    uint32_t                       vrf = 0;
    jnx_gw_data_vrf_stat_t*         vrf_entry   = NULL;
    jnx_gw_data_gre_tunnel_t*       gre_tunnel  = NULL;
    jnx_gw_data_ipip_tunnel_t*      ipip_tunnel = NULL;
    char*                           rsp_buf; 
    jnx_gw_msg_stat_rsp_t*          rsp_ptr = NULL;
    int                             count = 0;

    rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + sizeof(jnx_gw_msg_header_t));
    rsp_buf = (char*)rsp_ptr;

    /*
     * Single response buffer can be of 4K size. So, if the the response is going
     * to cross the 4K limit, then we will split the message and send it in
     * separate messages.
     */

    /*
     * Locate the VRF entry from the VRF DB and prepare the response indicating 
     * the summary stats for that vrf and all the GRE as well as IP-IP tunnels
     * in that VRF
     */

    /* Extract the Key from the Message */
    vrf  = ntohl(msg_ptr->info.extensive_vrf_stat.vrf);

    if((vrf_entry = jnx_gw_data_db_vrf_entry_lookup (app_cb, vrf)) == NULL) {

        err_code = JNX_GW_MSG_ERR_VRF_NOT_EXIST;
        goto jnx_gw_send_stat_rsp_to_control;
    }
    
    if(vrf_entry->state != JNX_GW_DATA_ENTRY_STATE_READY) {

        err_code = JNX_GW_MSG_ERR_VRF_NOT_EXIST;
        goto jnx_gw_send_stat_rsp_to_control;
    }

    count++;

    /* Start preparing the response */
    ((jnx_gw_msg_sub_header_t*)rsp_buf)->length = 
                    htons( sizeof(jnx_gw_msg_summary_stat_rsp_t));

    ((jnx_gw_msg_sub_header_t*)rsp_buf)->sub_type = 
                            JNX_GW_FETCH_SUMMARY_VRF_STAT; 
    ((jnx_gw_msg_sub_header_t*)rsp_buf)->err_code = 
                            JNX_GW_MSG_ERR_NO_ERR; 

    rsp_buf = rsp_buf + sizeof(jnx_gw_msg_sub_header_t);

    /* Fill the VRF ID */
    *(uint32_t*)rsp_buf = htonl(vrf_entry->key);

    rsp_buf = rsp_buf + sizeof(uint32_t);

    /* Fill the VRF Stats */
    jnx_gw_data_fill_vrf_summary_stats(vrf_entry, rsp_buf);

    rsp_buf = rsp_buf + sizeof(jnx_gw_vrf_stat_t) +
              sizeof(jnx_gw_common_stat_t);

    msg_len += sizeof(jnx_gw_msg_summary_stat_rsp_t);

    /* Fill the information regarding all the GRE tunnels in the VRF */
    for(gre_tunnel = (vrf_entry->next_gre_tunnel);
        gre_tunnel != NULL;
        gre_tunnel = gre_tunnel->next_in_vrf) {

        gre_tunnel = (jnx_gw_data_gre_tunnel_t*)((char*)gre_tunnel - 
                      offsetof(jnx_gw_data_gre_tunnel_t, next_in_vrf));

        if((msg_len + sizeof(jnx_gw_msg_sub_header_t) + sizeof(jnx_gw_gre_key_t) +
            sizeof(jnx_gw_common_stat_t)) > JNX_GW_DATA_MAX_BUF_SIZE) {

            /* Send the message to the MGMT and then reuse the buffer to
             * fill the remaining portion of the data.
             */
            rsp_msg->msg_header.more    = 1;
            rsp_msg->msg_header.msg_len = htons(msg_len);
            rsp_msg->msg_header.count   = count;

            if(pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg, msg_len)) {
                return;
            }
            
            /* Now reset the fields so that the further response can be
             * sent
             */ 
            count = 0;
            rsp_msg->msg_header.more = 0;
            msg_len = sizeof(jnx_gw_msg_header_t);

            rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                               sizeof(jnx_gw_msg_header_t));
            rsp_buf = (char*)rsp_ptr;
        }

        if(gre_tunnel->tunnel_state != JNX_GW_DATA_ENTRY_STATE_READY) 
            continue;

        count++;

        /* Fill the Sub Header for the GRE TUNNEL STAT */
        ((jnx_gw_msg_sub_header_t*)rsp_buf)->sub_type = 
                                                JNX_GW_FETCH_GRE_STAT;

        ((jnx_gw_msg_sub_header_t*)rsp_buf)->length = 
                                         htons(sizeof(jnx_gw_msg_sub_header_t) +
                                               sizeof(jnx_gw_gre_key_t) +
                                               sizeof(jnx_gw_common_stat_t));
        
        ((jnx_gw_msg_sub_header_t*)rsp_buf)->err_code  = JNX_GW_MSG_ERR_NO_ERR;

        rsp_buf = rsp_buf + sizeof(jnx_gw_msg_sub_header_t);

        /* Fill the KEY for the TUNNEL */
        ((jnx_gw_gre_key_t*)rsp_buf)->vrf = htonl(gre_tunnel->key.vrf); 
        ((jnx_gw_gre_key_t*)rsp_buf)->gre_key = htonl(gre_tunnel->key.gre_key); 
        
        rsp_buf = rsp_buf + sizeof(jnx_gw_gre_key_t);

        jnx_gw_data_fill_gre_tunnel_stats(gre_tunnel, rsp_buf);

        rsp_buf = rsp_buf + sizeof(jnx_gw_common_stat_t);

        msg_len += sizeof(jnx_gw_msg_sub_header_t) + sizeof(jnx_gw_gre_key_t) +
                   sizeof(jnx_gw_common_stat_t);
   }

   /* Fill the information regarding all the IP-IP tunnels */ 
   for(ipip_tunnel = vrf_entry->next_ipip_tunnel;
       ipip_tunnel != NULL;
       ipip_tunnel = ipip_tunnel->next_in_vrf) {

        ipip_tunnel = (jnx_gw_data_ipip_tunnel_t*)((char*)ipip_tunnel - 
                      offsetof(jnx_gw_data_ipip_tunnel_t, next_in_vrf));

        if((msg_len + sizeof(jnx_gw_msg_sub_header_t) + 
            sizeof(jnx_gw_ipip_tunnel_key_t) +
            sizeof(jnx_gw_common_stat_t)) > JNX_GW_DATA_MAX_BUF_SIZE) {

            /* Send the message to the MGMT and then reuse the buffer to
             * fill the remaining portion of the data.
             */
            rsp_msg->msg_header.more    = 1;
            rsp_msg->msg_header.msg_len = htons(msg_len);
            rsp_msg->msg_header.count   = count;

            if(pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg, 
                                 msg_len)) {
                return;
            }
            
            /* Now reset the fields so that the further response can be
             * sent
             */ 
            rsp_msg->msg_header.more = 0;
            msg_len = sizeof(jnx_gw_msg_header_t);
            count = 0;

            rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                               sizeof(jnx_gw_msg_header_t));
            rsp_buf = (char*)rsp_ptr;
        }

        if(ipip_tunnel->tunnel_state != JNX_GW_DATA_ENTRY_STATE_READY)
            continue;

        count++;

        /* Fill the Sub Header for the IP-IP TUNNEL STAT */
        ((jnx_gw_msg_sub_header_t*)rsp_buf)->sub_type = 
                                                JNX_GW_FETCH_IPIP_STAT;

        ((jnx_gw_msg_sub_header_t*)rsp_buf)->length = 
                                htons(sizeof(jnx_gw_msg_sub_header_t)  +
                                      sizeof(jnx_gw_ipip_tunnel_key_t) +
                                      sizeof(jnx_gw_common_stat_t));

        ((jnx_gw_msg_sub_header_t*)rsp_buf)->err_code  = 
                                                JNX_GW_MSG_ERR_NO_ERR;

        rsp_buf = rsp_buf + sizeof(jnx_gw_msg_sub_header_t);

        /* Fill the KEY for the TUNNEL */
        ((jnx_gw_ipip_tunnel_key_t*)rsp_buf)->vrf = htonl(ipip_tunnel->key.vrf); 
        ((jnx_gw_ipip_tunnel_key_t*)rsp_buf)->gateway_ip = 
                                         htonl(ipip_tunnel->key.gateway_ip);
        
        rsp_buf = rsp_buf + sizeof(jnx_gw_ipip_tunnel_key_t);

        jnx_gw_data_fill_ipip_tunnel_stats(ipip_tunnel, rsp_buf);

        rsp_buf = rsp_buf + sizeof(jnx_gw_common_stat_t);

        msg_len += sizeof(jnx_gw_msg_sub_header_t) + 
                   sizeof(jnx_gw_ipip_tunnel_key_t) +
                   sizeof(jnx_gw_common_stat_t);

   }

jnx_gw_send_stat_rsp_to_control:
    rsp_msg->msg_header.msg_len = htons(msg_len);
    rsp_msg->msg_header.count = count;
    pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg, msg_len); 
   return;
}

/**
 * This function is used to fetch the summary stats for all the VRFs. 
 * In case the response length exceeds the max response buffer 
 * size, it will split the response and will send multiple repsonses for the
 * same.
 *
 * @param[in] app_cb    Application State Control Block
 * @param[in] rsp_msg   Pointer to the response buffer
 * @param[in] session   Pointer to the pconn server session. 
 */

static void 
jnx_gw_data_fetch_all_vrf_summary_stats(jnx_gw_data_cb_t*       app_cb,
                                        jnx_gw_msg_t*           rsp_msg,
                                        pconn_session_t*        session)
{
    uint16_t                       msg_len  = sizeof(jnx_gw_msg_header_t);
    jnx_gw_data_vrf_stat_t*         vrf_entry = NULL;
    char*                           rsp_buf;
    int                             i = 0;
    int                             count = 0;
    jnx_gw_msg_stat_rsp_t*          rsp_ptr;

    rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                       sizeof(jnx_gw_msg_header_t));
    rsp_buf = (char*)rsp_ptr;

    /*
     * Single response buffer can be of 4K size. So, if the the response is going
     * to cross the 4K limit, then we will split the message and send it in
     * separate messages.
     *
     * We need to traverse the VRF list and for each vrf entry collect the
     * summary
     */

    for (i = 0; i < JNX_GW_DATA_MAX_VRF_BUCKETS; i++) {

        for(vrf_entry = app_cb->vrf_db.hash_bucket[i].chain;
            vrf_entry != NULL;
            vrf_entry = vrf_entry->next_in_bucket) {


            /* Check if there is space for VRF sub header, vrf key and summary
             * stats.
             */
            if((msg_len + sizeof(jnx_gw_msg_summary_stat_rsp_t)) > JNX_GW_DATA_MAX_BUF_SIZE) {

                /* Send the message to the MGMT and then reuse the buffer to
                 * fill the remaining portion of the data.
                 */
                rsp_msg->msg_header.more = 1;
                rsp_msg->msg_header.msg_len = htons(msg_len);
                rsp_msg->msg_header.count   = count;

                if(pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg, 
                                     msg_len)) {
                    return;
                }
                
                /* Now reset the fields so that the further response can be
                 * sent
                 */ 
                count = 0;
                rsp_msg->msg_header.count = 0;
                rsp_msg->msg_header.more = 0;
                msg_len = (sizeof(jnx_gw_msg_header_t));

                rsp_ptr = (jnx_gw_msg_stat_rsp_t*)((char*)rsp_msg + 
                                                   sizeof(jnx_gw_msg_header_t));
                rsp_buf = (char*)rsp_ptr;
            }

            if(vrf_entry->state != JNX_GW_DATA_ENTRY_STATE_READY) {
                continue;
            }

            count++;

            /* There is space in the current buffer to accomodate the VRF
             * summary.
             */
            ((jnx_gw_msg_sub_header_t*)rsp_buf)->length = 
                               htons (sizeof(jnx_gw_msg_summary_stat_rsp_t));
            
            ((jnx_gw_msg_sub_header_t*)rsp_buf)->sub_type = 
                                        JNX_GW_FETCH_SUMMARY_VRF_STAT; 

            ((jnx_gw_msg_sub_header_t*)rsp_buf)->err_code = 
                                        JNX_GW_MSG_ERR_NO_ERR; 

            rsp_buf = rsp_buf + sizeof(jnx_gw_msg_sub_header_t);

            /* Fill the VRF ID */
            *(uint32_t*)rsp_buf = htonl(vrf_entry->key);

            rsp_buf = rsp_buf + sizeof(uint32_t);

            /* Fill the VRF Stats */
            jnx_gw_data_fill_vrf_summary_stats(vrf_entry, rsp_buf);

            rsp_buf = rsp_buf + sizeof(jnx_gw_common_stat_t) + 
                                sizeof(jnx_gw_vrf_stat_t);

            msg_len += sizeof(jnx_gw_msg_summary_stat_rsp_t);
        }
    }

    rsp_msg->msg_header.count = count;
    rsp_msg->msg_header.msg_len = htons(msg_len);
    pconn_server_send(session, JNX_GW_STAT_FETCH_MSG, rsp_msg, msg_len); 

    return;
}

/**
 * This is a utility function used to fill the VRF related stats into a 
 * buffer. The function assumes that it has enough space in the buffer
 *
 * @param[in] vrf_entry    Pointer to the buffer entry
 * @param[in] buf          Pointer to the response buffer
 */
static void 
jnx_gw_data_fill_vrf_summary_stats(jnx_gw_data_vrf_stat_t* vrf_entry,
                                   char*                   buf)
{
    /* Fill the VRF specific Summary Stats */
    ((jnx_gw_vrf_stat_t*)buf)->tunnel_not_present  = 
                        htonl(vrf_entry->vrf_stats.tunnel_not_present);

    ((jnx_gw_vrf_stat_t*)buf)->invalid_pkt  = 
                        htonl(vrf_entry->vrf_stats.invalid_pkt);
    
    ((jnx_gw_vrf_stat_t*)buf)->active_sessions  = 
                        htonl(vrf_entry->vrf_stats.active_sessions);

    ((jnx_gw_vrf_stat_t*)buf)->total_sessions  = 
                        htonl(vrf_entry->vrf_stats.total_sessions);

    buf = buf+ sizeof(jnx_gw_vrf_stat_t);

    /* Fill the common stats for the VRF */
    ((jnx_gw_common_stat_t*)buf)->packets_in        = 
                                htonl(vrf_entry->stats.packets_in);
    ((jnx_gw_common_stat_t*)buf)->packets_out       = 
                                htonl(vrf_entry->stats.packets_out);
    ((jnx_gw_common_stat_t*)buf)->bytes_in          = 
                                htonl(vrf_entry->stats.bytes_in);
    ((jnx_gw_common_stat_t*)buf)->bytes_out         = 
                                htonl(vrf_entry->stats.bytes_out);
    ((jnx_gw_common_stat_t*)buf)->checksum_fail     = 
                                htonl(vrf_entry->stats.checksum_fail);
    ((jnx_gw_common_stat_t*)buf)->ttl_drop          = 
                                htonl(vrf_entry->stats.ttl_drop);
    ((jnx_gw_common_stat_t*)buf)->cong_drop         = 
                                htonl(vrf_entry->stats.cong_drop);
    ((jnx_gw_common_stat_t*)buf)->inner_ip_invalid  = 
                                htonl(vrf_entry->stats.inner_ip_invalid);
   return; 
}

/**
 * This is a utility function used to fill the GRE TUNNEL related stats into a 
 * buffer. The function assumes that it has enough space in the buffer
 *
 * @param[in] gre_tunnel   Pointer to the buffer entry
 * @param[in] buf          Pointer to the response buffer
 */
static void 
jnx_gw_data_fill_gre_tunnel_stats(jnx_gw_data_gre_tunnel_t*     gre_tunnel,
                                  char*                         buf)
{
    /* Fill the GRE Tunnel stats in the buffer provided */
    ((jnx_gw_common_stat_t*)buf)->packets_in         = 
                                        htonl(gre_tunnel->stats.packets_in);
    ((jnx_gw_common_stat_t*)buf)->packets_out        = 
                                        htonl(gre_tunnel->stats.packets_out);
    ((jnx_gw_common_stat_t*)buf)->bytes_in           = 
                                        htonl(gre_tunnel->stats.bytes_in);
    ((jnx_gw_common_stat_t*)buf)->bytes_out          = 
                                        htonl(gre_tunnel->stats.bytes_out);
    ((jnx_gw_common_stat_t*)buf)->checksum_fail      = 
                                        htonl(gre_tunnel->stats.checksum_fail);
    ((jnx_gw_common_stat_t*)buf)->ttl_drop           = 
                                        htonl(gre_tunnel->stats.ttl_drop);
    ((jnx_gw_common_stat_t*)buf)->cong_drop          = 
                                        htonl(gre_tunnel->stats.cong_drop);
    ((jnx_gw_common_stat_t*)buf)->inner_ip_invalid   = 
                                      htonl(gre_tunnel->stats.inner_ip_invalid);

    return;
}

/**
 * This is a utility function used to fill the IPIP TUNNEL related stats into a 
 * buffer. The function assumes that it has enough space in the buffer
 *
 * @param[in] ipip_tunnel  Pointer to the buffer entry
 * @param[in] buf          Pointer to the response buffer
 */
static void 
jnx_gw_data_fill_ipip_tunnel_stats(jnx_gw_data_ipip_tunnel_t* ipip_tunnel,
                                   char*                      buf)
{
    /* Fill the IP-IP Tunnel stats in the buffer provided */
    ((jnx_gw_common_stat_t*)buf)->packets_in         = 
                                    htonl(ipip_tunnel->stats.packets_in);
    ((jnx_gw_common_stat_t*)buf)->packets_out        = 
                                    htonl(ipip_tunnel->stats.packets_out);
    ((jnx_gw_common_stat_t*)buf)->bytes_in           = 
                                    htonl(ipip_tunnel->stats.bytes_in);
    ((jnx_gw_common_stat_t*)buf)->bytes_out          = 
                                    htonl(ipip_tunnel->stats.bytes_out);
    ((jnx_gw_common_stat_t*)buf)->checksum_fail      = 
                                    htonl(ipip_tunnel->stats.checksum_fail);
    ((jnx_gw_common_stat_t*)buf)->ttl_drop           = 
                                    htonl(ipip_tunnel->stats.ttl_drop);
    ((jnx_gw_common_stat_t*)buf)->cong_drop          = 
                                    htonl(ipip_tunnel->stats.cong_drop);
    ((jnx_gw_common_stat_t*)buf)->inner_ip_invalid   = 
                                    htonl(ipip_tunnel->stats.inner_ip_invalid);

    return;
}

/**
 * 
 * This is the function registered with the EVENT LIBRARY to invoke in
 * every JNX_GW_DATA_PERIODIC_CLEANUP_TIME_SEC. This function is responsible for
 * freeing the tunnels which have been deleted. This function runs in the
 * context of the control thread.
 *
 * @param[in] context   Event Library Context.
 * @param[in] uap       Opaque pointer passed to event library (JNX_GW_DATA_CB_T*)
 *                      in this case.
 * @Param[in] due       Event Library specific 
 * @Param[in] inter     Event Library specific 
 *
 */
void
jnx_gw_periodic_cleanup_timer_expiry(evContext context, void* uap, 
                                     struct timespec due __unused,
                                     struct timespec inter __unused)
{
    jnx_gw_data_cb_t*               app_cb;
    jnx_gw_data_gre_tunnel_t*       tmp_gre = NULL, *tmp_prev_gre = NULL;
    jnx_gw_data_ipip_tunnel_t*      tmp_ipip = NULL, *tmp_prev_ipip = NULL;
    jnx_gw_data_ipip_sub_tunnel_t*  tmp_ipip_sub = NULL, *tmp_prev_ipip_sub = NULL;
    jnx_gw_data_gre_tunnel_t*       gre_tunnel = NULL;
    jnx_gw_data_ipip_tunnel_t*      ipip_tunnel = NULL;
    jnx_gw_data_ipip_sub_tunnel_t*  ipip_sub_tunnel = NULL;
    time_t                          cur_timestamp =0;
    jnx_gw_msg_header_t            *msg_buffer = NULL;
    jnx_gw_msg_sub_header_t        *sub_hdr = NULL;
    jnx_gw_data_vrf_stat_t*         vrf_entry = NULL;
    jnx_gw_periodic_stat_t          stat, *stat_p = NULL;
    int                             i = 0, msg_len = 0;

    
    app_cb = (jnx_gw_data_cb_t*)uap; 

    /*
     * Examine the deleted lists of tunnels. If there is any tunnel to be
     * deleted free it.
     */

    if(app_cb->del_tunnels.gre_tunnel != NULL) {

        /* There are GRE Tunnels to be cleaned up. */
        tmp_gre  = app_cb->del_tunnels.gre_tunnel;

        while(tmp_gre != NULL) {

            if((cur_timestamp - tmp_gre->time_stamp) >= 
               JNX_GW_DATA_PERIODIC_CLEANUP_TIME_SEC)  {

                if(tmp_prev_gre == NULL) {

                    app_cb->del_tunnels.gre_tunnel = tmp_gre->next_in_bucket;

                }else {

                    tmp_prev_gre->next_in_bucket = tmp_gre->next_in_bucket;
                }

                gre_tunnel = tmp_gre;

                tmp_gre = tmp_gre->next_in_bucket;

                JNX_GW_FREE(JNX_GW_DATA_ID, gre_tunnel);
            }
            else {

                tmp_prev_gre = tmp_gre;
                tmp_gre       = tmp_gre->next_in_bucket;
            }
        }
        
    }

    if(app_cb->del_tunnels.ipip_tunnel != NULL) {
        
        /* There are IPIP Tunnels to be cleaned up. */
        tmp_ipip  = app_cb->del_tunnels.ipip_tunnel;

        while(tmp_ipip != NULL) {

            if((cur_timestamp - tmp_ipip->time_stamp) >= 
               JNX_GW_DATA_PERIODIC_CLEANUP_TIME_SEC)  {

                if(tmp_prev_ipip == NULL) {

                    app_cb->del_tunnels.ipip_tunnel = tmp_ipip->next_in_bucket;
                }else {

                    tmp_prev_ipip->next_in_bucket = tmp_ipip->next_in_bucket;
                }

                ipip_tunnel = tmp_ipip;

                tmp_ipip = tmp_ipip->next_in_bucket;

                JNX_GW_FREE(JNX_GW_DATA_ID, (void*)ipip_tunnel);
            }
            else {

                tmp_prev_ipip = tmp_ipip;
                tmp_ipip       = tmp_ipip->next_in_bucket;
            }
        }
    }

    if(app_cb->del_tunnels.ipip_sub_tunnel != NULL) {
        
        /* There are IPIP SUB Tunnels to be cleaned up. */
        tmp_ipip_sub  = app_cb->del_tunnels.ipip_sub_tunnel;

        while(tmp_ipip_sub != NULL) {

            if((cur_timestamp - tmp_ipip_sub->time_stamp) >= 
               JNX_GW_DATA_PERIODIC_CLEANUP_TIME_SEC)  {

                if (tmp_prev_ipip == NULL) {

                    app_cb->del_tunnels.ipip_sub_tunnel = tmp_ipip_sub->next_in_bucket;

                }else {

                    tmp_prev_ipip_sub->next_in_bucket = tmp_ipip_sub->next_in_bucket;
                }

                ipip_sub_tunnel = tmp_ipip_sub;

                tmp_ipip_sub = tmp_ipip_sub->next_in_bucket;

                JNX_GW_FREE(JNX_GW_DATA_ID, (void*)ipip_sub_tunnel);
            }
            else {

                tmp_prev_ipip_sub = tmp_ipip_sub;
                tmp_ipip_sub      = tmp_ipip_sub->next_in_bucket;
            }
        }
    }

    /*
     * If the management application agent connection
     * is not up,  issue a reconnect
     * do not send the periodic stat messages in
     * between to the management agent
     */

    if (app_cb->app_state != JNX_GW_DATA_STATE_READY) {
        jnx_gw_data_connect_mgmt(app_cb, context);
        return;
    }

    /* send a periodic status message to the Mgmt Agent */
    msg_buffer  = (typeof(msg_buffer))(app_cb->buffer);
    memset(msg_buffer, 0, sizeof(*msg_buffer));

    msg_len              = sizeof(*msg_buffer);
    msg_buffer->msg_type = JNX_GW_STAT_PERIODIC_MSG;

    sub_hdr = (typeof(sub_hdr))((char *)msg_buffer + msg_len);
    memset(sub_hdr, 0, sizeof(*sub_hdr));

    sub_hdr->sub_type = JNX_GW_STAT_PERIODIC_DATA_AGENT;
    sub_hdr->err_code = JNX_GW_MSG_ERR_NO_ERR; 
    sub_hdr->length   = htons(sizeof(*sub_hdr) + sizeof(*stat_p));
    msg_len          += sizeof(*sub_hdr);

    stat_p = (typeof(stat_p))((char *)msg_buffer + msg_len);

    memset(&stat, 0, sizeof(stat));

    for (i = 0; i < JNX_GW_DATA_MAX_VRF_BUCKETS; i++) {

        for(vrf_entry = app_cb->vrf_db.hash_bucket[i].chain;
            vrf_entry != NULL;
            vrf_entry = vrf_entry->next_in_bucket) {


            /* Check if there is space for VRF sub header, vrf key and summary
             * stats.
             */

            if(vrf_entry->state != JNX_GW_DATA_ENTRY_STATE_READY) {
                continue;
            }

            stat.active_sessions    += vrf_entry->vrf_stats.active_sessions;
            stat.total_sessions     += vrf_entry->vrf_stats.total_sessions;
            stat.tunnel_not_present += vrf_entry->vrf_stats.tunnel_not_present;
            stat.invalid_pkt        += vrf_entry->vrf_stats.invalid_pkt;
            stat.packets_in         += vrf_entry->stats.packets_in;
            stat.packets_out        += vrf_entry->stats.packets_out;
            stat.bytes_in           += vrf_entry->stats.bytes_in;
            stat.bytes_out          += vrf_entry->stats.bytes_out;
            stat.checksum_fail      += vrf_entry->stats.checksum_fail;
            stat.ttl_drop           += vrf_entry->stats.ttl_drop;
            stat.cong_drop          += vrf_entry->stats.cong_drop;
            stat.inner_ip_invalid   += vrf_entry->stats.inner_ip_invalid;
        }
    }

    memset(stat_p, 0, sizeof(*stat_p));
    stat_p->active_sessions    = htonl(stat.active_sessions); 
    stat_p->total_sessions     = htonl(stat.total_sessions);
    stat_p->tunnel_not_present = htonl(stat.tunnel_not_present);
    stat_p->invalid_pkt        = htonl(stat.invalid_pkt); 
    stat_p->packets_in         = htonl(stat.packets_in);
    stat_p->packets_out        = htonl(stat.packets_out);
    stat_p->bytes_in           = htonl(stat.bytes_in);
    stat_p->bytes_out          = htonl(stat.bytes_out);
    stat_p->checksum_fail      = htonl(stat.checksum_fail);
    stat_p->ttl_drop           = htonl(stat.ttl_drop);
    stat_p->cong_drop          = htonl(stat.cong_drop); 
    stat_p->inner_ip_invalid   = htonl(stat.inner_ip_invalid);   

    msg_len            += sizeof(*stat_p);
    msg_buffer->msg_len = htons(msg_len);

    /* send the message now */
    pconn_client_send(app_cb->conn_client, JNX_GW_STAT_PERIODIC_MSG,
                      msg_buffer, msg_len); 

    return;
}

/*
 *$Id: jnx-gateway-data_packet.c 397581 2010-09-02 20:03:02Z sunilbasker $
 *
 * jnx-gateway-data-packet.c - Complete packet processing code
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
 * @file jnx-gateway-data-pkt-proc.c
 * @brief All the routines responsible for packet processing are
 * preesnt in this file. 
 *
 * This file covers the following stuff:-
 * 1.  Deque of the Packet from the FIFO. 
 * 2.  Processing of the packet received
 * 3.  Enque the processed packet to the Tx-FIFO
 * 4.  Update the relevant per packet stats.
 * 
 */
#include <unistd.h>
#include <signal.h>
#include <jnx/mpsdk.h>
#include "jnx-gateway-data.h"
#include "jnx-gateway-data_db.h"
#include "jnx-gateway-data_utils.h"
#include "jnx-gateway-data_packet.h"
#include <net/if_802.h>

/*===========================================================================*
 *                                                                           *
 *                 Local function prototypes                                 *
 *                                                                           *
 *===========================================================================*/

/* Function to process the GRE packets received by the JNX-GW-DATA */
static jnx_gw_data_err_t jnx_gw_data_process_gre_packet(
                                       jnx_gw_pkt_proc_ctxt_t* pkt_ctxt);

/* Function to process the IP-IP  packets received by the JNX-GW-DATA */
static jnx_gw_data_err_t jnx_gw_data_process_ipip_packet(
                                      jnx_gw_pkt_proc_ctxt_t* pkt_ctxt);

/* Function to process the IP  packets received by the JNX-GW-DATA */
static jnx_gw_data_err_t jnx_gw_data_process_ip_packet(
                          jnx_gw_pkt_proc_ctxt_t* pkt_ctxt, uint8_t verify_checksum);

/* Function to process the error returned by the IP  packets processing */
static jnx_gw_data_err_t jnx_gw_data_process_ip_error(
                                      jnx_gw_pkt_proc_ctxt_t* pkt_ctxt);

/* Function to process the error returned by the GRE packets processing */
static jnx_gw_data_err_t jnx_gw_data_process_gre_error(
                                      jnx_gw_pkt_proc_ctxt_t*   pkt_ctxt);

/* Function to process the error returned by the IP-IP packets processing */
static jnx_gw_data_err_t jnx_gw_data_process_ipip_error(
                                     jnx_gw_pkt_proc_ctxt_t*   pkt_ctxt);

/* Function to drop the Packet */
static void jnx_gw_data_drop_pkt(jnx_gw_pkt_proc_ctxt_t* pkt_ctxt);

/* Function to process the Ingress GRE Tunnel & VRF Stats */
static jnx_gw_data_err_t jnx_gw_data_process_ing_gre_vrf_stats(
                                     jnx_gw_pkt_proc_ctxt_t* pkt_ctxt);

/* Function to process the Ingress IP-IP Tunnel & VRF Stats */
static jnx_gw_data_err_t jnx_gw_data_process_ing_ipip_vrf_stats(
                                     jnx_gw_pkt_proc_ctxt_t* pkt_ctxt);

/* Function to process the Egress GRE Tunnel & VRF Stats */
static jnx_gw_data_err_t jnx_gw_data_process_eg_gre_vrf_stats(
                                     jnx_gw_pkt_proc_ctxt_t* pkt_ctxt);

/* Function to process the INgress GRE Tunnel & VRF Stats */
static jnx_gw_data_err_t jnx_gw_data_process_eg_ipip_vrf_stats(
                                     jnx_gw_pkt_proc_ctxt_t* pkt_ctxt);

/*===========================================================================*
 *                                                                           *
 *                 Function Definitions                                      *
 *                                                                           *
 *===========================================================================*/                 

/**
 * 
 * This is the top level function of all the data threads. Each thread is
 * responsible for complete processing of the packet i.e. deque the packet
 * from the RX_FIFO, packet validation, packet decap and encap and 
 * sending it out. All the functionality is performed by the this function
 * (by calling various sub-routines). This function runs an infinite loop
 * and never returns.
 *
 * @param[in] args      Arguments passed by the main thread to initiate this
 *                      data thread.
 *
 */
void* 
jnx_gw_data_process_packet(void* loop_args)
{
    jnx_gw_data_cb_t                *app_cb;
    msp_dataloop_args_t             *data_args_p;
    uint32_t                         agent_num, pkt_type;
    register jnx_gw_pkt_proc_ctxt_t* pkt_ctxt; 
    sigset_t                         sigmask;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigmask, NULL);
    
    data_args_p = (typeof(data_args_p))loop_args;
    agent_num   = msp_data_get_cpu_num(data_args_p->dhandle);
    app_cb      = (typeof(app_cb))data_args_p->app_data;

    /*
     * Check if the application has initialized itself, if not then wait
     * till it initializes
     */

    while (app_cb->app_state == JNX_GW_DATA_STATE_INIT)
        sleep(1);

    /* Get the packet processing context for this thread */
    pkt_ctxt = &app_cb->pkt_ctxt[agent_num];

    /*
     * Initialise some fields of the packet processing ctxt 
     */
    pkt_ctxt->app_cb  = app_cb;
    pkt_ctxt->dhandle = data_args_p->dhandle;

    /*
     * Start the packet loop 
     */
    while (1) {

        /*
         * Check the state of the application, if it has been shutdown then
         * simply exit.
         */
        if (pkt_ctxt->app_cb->app_state == JNX_GW_DATA_STATE_SHUTDOWN) {
            exit(0);
        }

        /*
         * Issue a deque request from the rx-fifo.
         */
        if ((pkt_ctxt->pkt_buf = msp_data_recv(pkt_ctxt->dhandle, &pkt_type))
            == NULL) {
            continue;
        }


        /* Reset the Packet Ctxt First */
        pkt_ctxt->ing_vrf_entry = NULL;
        pkt_ctxt->eg_vrf_entry  = NULL;
        pkt_ctxt->stat_type     = JNX_GW_DATA_STAT_TYPE_NONE;


        /* Get the VRF assocaited with the packet */
        pkt_ctxt->ing_vrf = jbuf_getvrf(pkt_ctxt->pkt_buf);

        /* 
         * Use jtod to typecast the data in the j-buf to the IP-Header 
         * strucuture 
         */

        pkt_ctxt->ip_hdr =
            jbuf_to_d(pkt_ctxt->pkt_buf, typeof(pkt_ctxt->ip_hdr));

        /* Do the Initial IP layer processing on the packet */
        if(jnx_gw_data_process_ip_packet(pkt_ctxt, FALSE) ==
           JNX_GW_DATA_DROP_PKT) {
            jnx_gw_data_process_ip_error(pkt_ctxt);
            continue;
        }

        switch(pkt_ctxt->ip_hdr->ip_p) {

            case IPPROTO_GRE: 
                if(jnx_gw_data_process_gre_packet(pkt_ctxt) == 
                   JNX_GW_DATA_DROP_PKT) {

                    jnx_gw_data_process_gre_error(pkt_ctxt);
                    continue;
                }
                break;

            case IPPROTO_IPIP:

                if(jnx_gw_data_process_ipip_packet(pkt_ctxt) ==
                   JNX_GW_DATA_DROP_PKT) {

                    jnx_gw_data_process_ipip_error(pkt_ctxt);
                    continue;
                }
                break;

            default:
                /* Increment the VRF Error Stats & Drop the Packet*/
                jnx_gw_data_drop_pkt(pkt_ctxt);
                break;
        }
    }
}

/**
 * 
 * This is the function used to process the IP Header of the packer received.
 * The function performs various sanity checks on the IP Header of the packet
 * to ensure the integrity of the packet. If the packet is not valid the
 * function will return an error
 *
 * @param[in] pkt_ctxt     Packet Processing context used to process the packet 
 * 
 * @return Result of the operation
 *     @li JNX_GW_DROP_PKT      Invalid Packet and hence drop the packet.
 *                               Error code is marked in pkt_ctxt->stat_type
 *     @li JNX_GW_DATA_SUCCESS  Function was successful
 *                    
 */
static jnx_gw_data_err_t
jnx_gw_data_process_ip_packet(jnx_gw_pkt_proc_ctxt_t* pkt_ctxt, uint8_t verify_checksum)
{
    struct ip* ip_hdr = pkt_ctxt->ip_hdr;
    uint32_t pkt_len;

    pkt_len = jbuf_length(pkt_ctxt->pkt_buf, NULL);

    /* Perform some sanity checks on the Packet received */

    /* Check the version of the IP packet */
    if(ip_hdr->ip_v != IPVERSION) {

        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_INVALID_PKT;
        return JNX_GW_DATA_DROP_PKT;
    }

    /* check the length of the packet */
    if (pkt_len < sizeof(struct ip)) {

        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_INVALID_PKT;
        return JNX_GW_DATA_DROP_PKT;
    }

    /* check the Length of the IP Header */
    if((uint32_t)(ip_hdr->ip_hl << 2) < sizeof(struct ip)) {

        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_INVALID_PKT;
        return JNX_GW_DATA_DROP_PKT;
    }

    /* Check the length of IP Packet w.r.t Header Length */
    if((ip_hdr->ip_hl << 2) > ntohs(ip_hdr->ip_len)) {

        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_INVALID_PKT;
        return JNX_GW_DATA_DROP_PKT;
    }

    /* 
     * We are not doing the IP header Checksum Validation because that will be
     * done by the XLR itself.
     */
    if (verify_checksum){

        uint32_t   checksum;

        checksum = jnx_gw_data_compute_checksum(pkt_ctxt->pkt_buf, 0, 
                                           sizeof(struct ip));

        if (checksum & 0x0000ffff) {

            pkt_ctxt->stat_type = JNX_GW_DATA_ERR_INVALID_PKT;
            return JNX_GW_DATA_DROP_PKT;
        }
    }

    return JNX_GW_DATA_SUCCESS;
}


/**
 * 
 * This function is used to process the GRE packet received by the application.
 *
 * @param[in] pkt_ctxt     Packet Processing context used to process the packet 
 * 
 * @return Result of the operation
 *     @li JNX_GW_DROP_PKT      Some Error occurred and hence drop the packet
 *                              Error code is marked in pkt_ctxt->stat_type
 *     @li JNX_GW_DATA_SUCCESS  Function was succesful
 *
 */
static jnx_gw_data_err_t
jnx_gw_data_process_gre_packet(jnx_gw_pkt_proc_ctxt_t*    pkt_ctxt)
{
    int                         decap_len = 0;
    uint32_t                    checksum  = 0;
    int                         pkt_len   = 0;
    uint8_t                     ttl = 0;
    jnx_gw_gre_encap_header_t*  ip_gre_hdr = NULL;

    /* Typecast the data portion in the jbuf to a structure */

#ifdef GW_DATA_DEBUG
    printf("%s:%d\n", __func__, __LINE__);
#endif

    pkt_ctxt->ip_gre_hdr = ip_gre_hdr =
        jbuf_to_d(pkt_ctxt->pkt_buf, typeof(ip_gre_hdr));

    ip_gre_hdr->gre_header.hdr_flags.flags =
                ntohs(ip_gre_hdr->gre_header.hdr_flags.flags);

    if(ip_gre_hdr->gre_header.hdr_flags.info.key_present == 0) {

        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_GRE_PKT_WITHOUT_KEY;
        return JNX_GW_DATA_DROP_PKT;
    }

    if(ip_gre_hdr->gre_header.hdr_flags.info.seq_num == 1) {

        /* Increment the VRF Error Stats & Drop the Packet*/
        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_GRE_PKT_WITH_SEQ;
        return JNX_GW_DATA_DROP_PKT;
    }

    if(ntohs(ip_gre_hdr->gre_header.protocol_type) != ETHERTYPE_IP) {

        /* Increment the VRF Error Stats & Drop the Packet*/
        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_GRE_PKT_INVALID_PROTO;
        return JNX_GW_DATA_DROP_PKT;
    }


    if(ip_gre_hdr->gre_header.hdr_flags.info.version != 0) {

        /* Increment the VRF Error Stats & Drop the Packet*/
        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_GRE_PKT_INVALID_PROTO;
        return JNX_GW_DATA_DROP_PKT;
    }

    decap_len = (pkt_ctxt->ip_hdr->ip_hl << 2) +  GRE_FIELD_WIDTH; 


    if (ip_gre_hdr->gre_header.hdr_flags.info.checksum) {

        checksum = jnx_gw_data_compute_checksum(pkt_ctxt->pkt_buf, 
                                           (pkt_ctxt->ip_hdr->ip_hl << 2),
                                           (pkt_ctxt->ip_hdr->ip_len -
                                            (pkt_ctxt->ip_hdr->ip_hl << 2)));
        if (checksum & 0x0000FFFF) {

            /* Increment the VRF Error Stats & drop the packet */
            pkt_ctxt->stat_type = JNX_GW_DATA_ERR_GRE_CHECKSUM;
            return JNX_GW_DATA_DROP_PKT;
        }
        decap_len += GRE_FIELD_WIDTH;
    }

    if (ip_gre_hdr->gre_header.hdr_flags.info.key_present) {
        pkt_ctxt->gre_key_info.key.gre_key = 
            ntohl(*(uint32_t *)
                  (jbuf_to_d(pkt_ctxt->pkt_buf, char *) + decap_len));
        decap_len += GRE_FIELD_WIDTH;
    } else {
        /* no GRE Key */
        pkt_ctxt->gre_key_info.key.gre_key =  0;
    }

    if (ip_gre_hdr->gre_header.hdr_flags.info.seq_num) {
        decap_len += GRE_FIELD_WIDTH;
    }

    /* Perform a lookup in the GRE DB to find out the tunnel */
    pkt_ctxt->gre_key_info.key.vrf = pkt_ctxt->ing_vrf; 

    if ((pkt_ctxt->gre_tunnel =
         jnx_gw_data_db_gre_tunnel_lookup_with_lock(pkt_ctxt->app_cb, 
                                                    &pkt_ctxt->gre_key_info))
        == NULL) {

        /* Increment the VRF Error Stats & drop the packet */
        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_GRE_TUNNEL_NOT_PRESENT;
        return JNX_GW_DATA_DROP_PKT;
    }


    if (pkt_ctxt->gre_tunnel->tunnel_state != JNX_GW_DATA_ENTRY_STATE_READY) {

        /* Drop the packet */
        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_GRE_TUNNEL_NOT_READY;
        return JNX_GW_DATA_DROP_PKT;
    }
    
    pkt_ctxt->ing_vrf_entry = pkt_ctxt->gre_tunnel->ing_vrf_stat;

    /*Now that we have found the tunnel increment the stats for the tunnel */
    jnx_gw_data_process_ing_gre_vrf_stats(pkt_ctxt);

    /*Remove the Outer IP Header & GRE Header from the packet */


    jbuf_adj(pkt_ctxt->pkt_buf,decap_len); 

    /* 
     * Get the information about the Egress VRF and how to encapsulate the rcvd
     * packet
     */
    pkt_ctxt->eg_vrf_entry = pkt_ctxt->gre_tunnel->eg_vrf_stat;

    /* Get the pointer to the inner IP Header */

    pkt_ctxt->ip_hdr =
        jbuf_to_d(pkt_ctxt->pkt_buf, typeof(pkt_ctxt->ip_hdr));

    /* Do the processing for IP Layer */
    if (jnx_gw_data_process_ip_packet(pkt_ctxt, TRUE) == JNX_GW_DATA_DROP_PKT) {

        /* Increment the error Counter */
        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_IPIP_TUNNEL_INVALID_INNER_PKT;
        return JNX_GW_DATA_DROP_PKT;
    }

    /* Get the TTL Value of the inner IP packet */
    if (pkt_ctxt->ip_hdr->ip_ttl <= 1) {

        pkt_ctxt->stat_type = JNX_GW_DATA_GRE_INNER_IP_TTL;
        return JNX_GW_DATA_DROP_PKT;
    }

    pkt_len = ntohs(pkt_ctxt->ip_hdr->ip_len);

    /* Compute the checksum of the inner IP packet, Use the incremental
     * checksum procedure */
    jnx_gw_data_update_ttl_compute_inc_checksum(pkt_ctxt->ip_hdr);

    ttl = pkt_ctxt->ip_hdr->ip_ttl;
        
    if (pkt_ctxt->gre_tunnel->tunnel_type == JNX_GW_TUNNEL_TYPE_IPIP) {
        
        /* We have a pre computed ip Header in the IP-IP Tunnel
         * to be appeneded to the packet
         */
        pkt_ctxt->pkt_buf = jbuf_prepend(pkt_ctxt->pkt_buf, 
                                      sizeof(struct ip));

        /* copy the outer ip header to the packet start */
        jbuf_copy_from_buf(pkt_ctxt->pkt_buf, 0,
                     sizeof(struct ip), (char*)&pkt_ctxt->gre_tunnel->ip_hdr);

        
        /* Now modify the fields required to for this packet */
        pkt_ctxt->ip_hdr =
            jbuf_to_d(pkt_ctxt->pkt_buf, typeof(pkt_ctxt->ip_hdr));

        pkt_ctxt->ip_hdr->ip_len = htons(pkt_len + sizeof(struct ip));
        pkt_ctxt->ip_hdr->ip_ttl = ttl;

        /* Set the IP-ID in the IP header */
        pkt_ctxt->ip_hdr->ip_id = atomic_add_uint(1,
                                                 &pkt_ctxt->app_cb->ip_id);

         pkt_ctxt->ip_hdr->ip_id = htons(pkt_ctxt->ip_hdr->ip_id);
         pkt_ctxt->ip_hdr->ip_sum = htons(0);

        /* Compute the IP Header Checksum */
        pkt_ctxt->ip_hdr->ip_sum = 
            (jnx_gw_data_compute_checksum(pkt_ctxt->pkt_buf,
                                          0, sizeof(struct ip)));

    }
    else {
        
        /* The packet has to go out plain ip packet */
    }

#ifdef GW_DATA_DEBUG
    {
        uint32_t idx;
        printf("%s:%d:%d:\n", __func__, __LINE__, pkt_len);
        for (idx = 0; idx < 16; idx++) {
            printf("\t%08X", *(uint32_t *)((uint32_t *)pkt_ctxt->ip_hdr + idx));
            if (!((idx + 1) % 4)) printf("\n");
        }
        printf("\n");
    }
#endif

    /* Set the outgoing VRF in the PKT BUFF */
    jbuf_setvrf(pkt_ctxt->pkt_buf, pkt_ctxt->gre_tunnel->egress_vrf);

    /* Send the packet out */
    if (msp_data_send(pkt_ctxt->dhandle, pkt_ctxt->pkt_buf,
                      MSP_MSG_TYPE_PACKET) != MSP_OK) {

        pkt_ctxt->stat_type = JNX_GW_DATA_CONG_DROP;
        return JNX_GW_DATA_DROP_PKT;
    }

    /* Increment the stats for the tunnel */
    jnx_gw_data_process_eg_ipip_vrf_stats(pkt_ctxt);

    return JNX_GW_DATA_SUCCESS;
}

/**
 * 
 * This function is used to process the IPIP packet received by the application.
 *
 * @param[in] pkt_ctxt     Packet Processing context used to process the packet 
 * 
 * @return Result of the operation
 *     @li JNX_GW_DROP_PKT      Some Error occurred and hence drop the packet
 *                              Error code is marked in pkt_ctxt->stat_type
 *     @li JNX_GW_DATA_SUCCESS  Function was succesful
 *
 */
static jnx_gw_data_err_t
jnx_gw_data_process_ipip_packet(jnx_gw_pkt_proc_ctxt_t*   pkt_ctxt) 
{
    int                         pkt_len = 0;
    uint8_t                     ttl = 0;
    jnx_gw_ipip_encap_header_t* ipip_hdr;
    struct ip*                  ip_hdr;
    jnx_gw_gre_encap_header_t*  ip_gre_hdr;
    
#ifdef GW_DATA_DEBUG
    printf("%s:%d\n", __func__, __LINE__);
#endif

    /* Typecast the data portion in the jbuf to a structure */
    pkt_ctxt->ipip_hdr = ipip_hdr =
        jbuf_to_d(pkt_ctxt->pkt_buf, typeof(ipip_hdr));

    /* Perform a lookup in the IPIP SUB TUNNEL DB to find out the tunnel */
    pkt_ctxt->ipip_sub_key_info.key.vrf          = pkt_ctxt->ing_vrf; 
    pkt_ctxt->ipip_sub_key_info.key.gateway_addr = 
                        ntohl(ipip_hdr->outer_ip_hdr.ip_src.s_addr);
    pkt_ctxt->ipip_sub_key_info.key.client_addr  = 
                        ntohl(ipip_hdr->inner_ip_hdr.ip_dst.s_addr);
    pkt_ctxt->ipip_sub_key_info.key.client_port  = 
                        ntohs(ipip_hdr->tcp_hdr.th_dport);

    if((pkt_ctxt->ipip_sub_tunnel = 
        jnx_gw_data_db_ipip_sub_tunnel_lookup_with_lock(pkt_ctxt->app_cb, 
                                       &pkt_ctxt->ipip_sub_key_info)) == NULL) {

        /* Increment the VRF Error Stats & drop the packet */
        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_IPIP_TUNNEL_NOT_PRESENT;
        return JNX_GW_DATA_DROP_PKT;
    }

    if (pkt_ctxt->ipip_sub_tunnel->tunnel_state
        != JNX_GW_DATA_ENTRY_STATE_READY) {

        /* Drop the packet */
        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_IPIP_TUNNEL_NOT_READY;
        return JNX_GW_DATA_DROP_PKT;
    }
    
    pkt_ctxt->ing_vrf_entry  = pkt_ctxt->ipip_sub_tunnel->ing_vrf_stat;

    /* Increment the stats for the ingress i.e IP-IP tunnel */
    jnx_gw_data_process_ing_ipip_vrf_stats(pkt_ctxt);

    /*Remove the Outer IP Header from the packet */
    jbuf_adj(pkt_ctxt->pkt_buf, sizeof(struct ip)); 

    /* 
     * Get the information about the Egress VRF and how to encapsulate the rcvd
     * packet
     */
    pkt_ctxt->eg_vrf_entry = pkt_ctxt->ipip_sub_tunnel->eg_vrf_stat;

    /* Get the pointer to the inner IP Header */
    pkt_ctxt->ip_hdr = ip_hdr =
        jbuf_to_d(pkt_ctxt->pkt_buf, typeof(ip_hdr));

    /* Do the processing for IP Layer */
    if (jnx_gw_data_process_ip_packet(pkt_ctxt, TRUE) == JNX_GW_DATA_DROP_PKT) {

         pkt_ctxt->stat_type = JNX_GW_DATA_ERR_IPIP_TUNNEL_INVALID_INNER_PKT;
         return JNX_GW_DATA_DROP_PKT;
    }

    /* Get the TTL Value of the inner IP packet */
    if (ip_hdr->ip_ttl <= 1) {

        pkt_ctxt->stat_type = JNX_GW_DATA_ERR_IPIP_INNER_IP_TTL;
        return JNX_GW_DATA_DROP_PKT;
    }

    /* Decrement the TTL of the Inner IP packet by 1*/
    ttl = ip_hdr->ip_ttl;

    pkt_len = ip_hdr->ip_len;

    /* Compute the checksum of the inner IP packet, Use the incremental
     * checksum procedure */
    jnx_gw_data_update_ttl_compute_inc_checksum(pkt_ctxt->ip_hdr);
     
    /* 
     * We have a pre computed IP & GRE Header in the GRE Tunnel to be 
     * appeneded to the packet 
     */
    pkt_ctxt->pkt_buf = jbuf_prepend(pkt_ctxt->pkt_buf, 
                                     pkt_ctxt->ipip_sub_tunnel->ip_gre_hdr_len);

    /* set the packet length */
    pkt_len += pkt_ctxt->ipip_sub_tunnel->ip_gre_hdr_len;

    /* copy ip gre header to the start of the packet */
    jbuf_copy_from_buf(pkt_ctxt->pkt_buf, 0,
                  pkt_ctxt->ipip_sub_tunnel->ip_gre_hdr_len,
                  (char*)&(pkt_ctxt->ipip_sub_tunnel->ip_gre_hdr));

    /* get the pointer to the top of the packet data */
    pkt_ctxt->ip_gre_hdr = ip_gre_hdr =
        jbuf_to_d(pkt_ctxt->pkt_buf, typeof(ip_gre_hdr));

    /* Now modify the fields required for this packet */

    /* set sequence number if flagged */
    if (pkt_ctxt->ipip_sub_tunnel->ip_gre_hdr_seq_offset) {
        *(uint32_t *)((uint8_t *)ip_gre_hdr +
                       pkt_ctxt->ipip_sub_tunnel->ip_gre_hdr_seq_offset) =
            htonl(atomic_add_uint(1, &pkt_ctxt->ipip_sub_tunnel->gre_seq));
    }

    /* compute the GRE checksum,  if flagged */
    if (pkt_ctxt->ipip_sub_tunnel->ip_gre_hdr_cksum_offset) {
        *(uint16_t *)((char *)ip_gre_hdr +
                       pkt_ctxt->ipip_sub_tunnel->ip_gre_hdr_cksum_offset) =
            (jnx_gw_data_compute_checksum(pkt_ctxt->pkt_buf, sizeof(struct ip),
                                          pkt_len - sizeof(struct ip)));
    }

    /* set the header length */
    ip_gre_hdr->outer_ip_hdr.ip_len = htons(pkt_len);

    /* set the ttl */
    ip_gre_hdr->outer_ip_hdr.ip_ttl = ttl;

    /* set the ip frag id */
    ip_gre_hdr->outer_ip_hdr.ip_id  = 
        htons(atomic_add_uint(1, &pkt_ctxt->app_cb->ip_id));

    /* Compute the IP Header Checksum */
    ip_gre_hdr->outer_ip_hdr.ip_sum =
        (jnx_gw_data_compute_checksum(pkt_ctxt->pkt_buf,
                                       0, sizeof(struct ip)));

#ifdef GW_DATA_DEBUG
    {
        uint32_t idx;
        printf("%s:%d:%d:%d\n", __func__, __LINE__, 
               pkt_len, pkt_ctxt->ipip_sub_tunnel->ip_gre_hdr_len);
        for (idx = 0; idx < 16; idx++) {
            printf("\t%08X", *(uint32_t *)((uint32_t *)pkt_ctxt->ip_hdr + idx));
            if (!((idx + 1) % 4)) printf("\n");
        }
        printf("\n");
    }
#endif

    /* Set the outgoing VRF in the PKT BUFF */
   jbuf_setvrf(pkt_ctxt->pkt_buf, pkt_ctxt->ipip_sub_tunnel->egress_vrf);

    if (msp_data_send(pkt_ctxt->dhandle, pkt_ctxt->pkt_buf,
                     MSP_MSG_TYPE_PACKET) != MSP_OK) {

        pkt_ctxt->stat_type = JNX_GW_DATA_CONG_DROP;
        return JNX_GW_DATA_DROP_PKT;
    }

    /* Increment the stats for the egress i.e GREtunnel */
    jnx_gw_data_process_eg_gre_vrf_stats(pkt_ctxt);

    return JNX_GW_DATA_SUCCESS;
}

/**
 * 
 * This function is used to process the Error in the IP Packets
 *
 * @param[in] pkt_ctxt     Packet Processing context used to process the packet 
 * 
 * @return Result of the operation
 *     @li JNX_GW_DROP_PKT      Some Error occurred and hence drop the packet
 *     @li JNX_GW_DATA_SUCCESS  Function was succesful
 *
 */
static jnx_gw_data_err_t
jnx_gw_data_process_ip_error(jnx_gw_pkt_proc_ctxt_t*   pkt_ctxt)
{
    uint32_t pkt_len;

    pkt_len = jbuf_length(pkt_ctxt->pkt_buf, NULL);

    /* These stats need to be incremented in the VRF Stats */
    if (pkt_ctxt->ing_vrf_entry == NULL) {

        pkt_ctxt->ing_vrf_entry = 
            jnx_gw_data_db_vrf_entry_lookup(pkt_ctxt->app_cb, 
                                            pkt_ctxt->ing_vrf);

        if (pkt_ctxt->ing_vrf_entry == NULL) {
            jnx_gw_data_drop_pkt(pkt_ctxt);
            return JNX_GW_DATA_DROP_PKT;
        }
    }

    /* 
     * To increment the stats we should be using the atomic APIs of the sdk,
     * which will not require any lock. However, pseudo-sdk requires locks for
     * the correct operations. Hence, we are passing lock as a parameter to the
     * Pseudo SDK APIs, just to ensure atomicity.
     */
    switch(pkt_ctxt->stat_type) {

        case JNX_GW_DATA_ERR_INVALID_PKT:
            atomic_add_uint(1, 
                            &pkt_ctxt->ing_vrf_entry->vrf_stats.invalid_pkt);
            break;
        default:
            break;
    }

    /* Increment the "Packets In" for the VRF */
    atomic_add_uint(1,&pkt_ctxt->ing_vrf_entry->stats.packets_in);

    /* Increment the "Bytes In" for the VRF */
    atomic_add_uint(pkt_len,
                    &pkt_ctxt->ing_vrf_entry->stats.packets_in);

    jnx_gw_data_drop_pkt(pkt_ctxt);

    return JNX_GW_DATA_SUCCESS;
}

/**
 * 
 * This function is used to process the Error in the IP-IP Packets
 *
 * @param[in] pkt_ctxt     Packet Processing context used to process the packet 
 * 
 * @return Result of the operation
 *     @li JNX_GW_DROP_PKT      Some Error occurred and hence drop the packet
 *     @li JNX_GW_DATA_SUCCESS  Function was succesful
 *
 */
static jnx_gw_data_err_t
jnx_gw_data_process_ipip_error(jnx_gw_pkt_proc_ctxt_t*   pkt_ctxt)
{
    uint32_t pkt_len;

    pkt_len = jbuf_length(pkt_ctxt->pkt_buf, NULL);

    /* These stats need to be incremented in the VRF Stats */
    if (pkt_ctxt->ing_vrf_entry == NULL) {

        pkt_ctxt->ing_vrf_entry =
            jnx_gw_data_db_vrf_entry_lookup(pkt_ctxt->app_cb, 
                                            pkt_ctxt->ing_vrf);

        if (pkt_ctxt->ing_vrf_entry == NULL) {
            jnx_gw_data_drop_pkt(pkt_ctxt);
            return JNX_GW_DATA_SUCCESS;
        }
    }

    switch(pkt_ctxt->stat_type) {

        case JNX_GW_DATA_ERR_IPIP_TUNNEL_NOT_PRESENT:
            /* Increment the VRF Stats */
            atomic_add_uint(1, 
                            &pkt_ctxt->ing_vrf_entry->
                            vrf_stats.tunnel_not_present);
            break;

        case JNX_GW_DATA_ERR_IPIP_TUNNEL_NOT_READY:
            /* Increment the VRF Stats */
            atomic_add_uint(1, 
                            &pkt_ctxt->ing_vrf_entry->
                            vrf_stats.tunnel_not_present);
            break;

        case JNX_GW_DATA_ERR_IPIP_TUNNEL_INVALID_INNER_PKT: 
            /* Increment the IPIP Tunnel inner ip error stats*/
            atomic_add_uint(1, 
                            &pkt_ctxt->ipip_sub_tunnel->
                            ipip_tunnel->stats.inner_ip_invalid);
            break;

        case JNX_GW_DATA_ERR_IPIP_INNER_IP_TTL:    
            /* Increment the IPIP Tunnel ttl Drop stats*/
            atomic_add_uint(1, &pkt_ctxt->ipip_sub_tunnel->ipip_tunnel->
                            stats.ttl_drop);
            break;
            
        case JNX_GW_DATA_CONG_DROP:
            /* Increment the Drop due to congestion, increment this stat in the
             * GRE Tunnel */
            atomic_add_uint(1, &pkt_ctxt->ipip_sub_tunnel->
                            ipip_tunnel->stats.cong_drop);
            break;
        default:
            break;
    }

    /* Increment the Packet In & Bytes In for the ingress VRF */
    atomic_add_uint(1,
                    &pkt_ctxt->ing_vrf_entry->stats.packets_in);

    /* Increment the "Bytes In" for the VRF */
    atomic_add_uint(pkt_len,
                    &pkt_ctxt->ing_vrf_entry->stats.packets_in);

    jnx_gw_data_drop_pkt(pkt_ctxt);
    return JNX_GW_DATA_SUCCESS;
}

/**
 * 
 * This function is used to process the Error in the GRE Packets
 *
 * @param[in] pkt_ctxt     Packet Processing context used to process the packet 
 * 
 * @return Result of the operation
 *     @li JNX_GW_DROP_PKT      Some Error occurred and hence drop the packet
 *     @li JNX_GW_DATA_SUCCESS  Function was succesful
 *
 */
static jnx_gw_data_err_t
jnx_gw_data_process_gre_error(jnx_gw_pkt_proc_ctxt_t*   pkt_ctxt)
{
    /* These stats need to be incremented in the VRF Stats */
    if(pkt_ctxt->ing_vrf_entry == NULL) {

        pkt_ctxt->ing_vrf_entry = jnx_gw_data_db_vrf_entry_lookup(
                                                        pkt_ctxt->app_cb, 
                                                        pkt_ctxt->ing_vrf);

        if (pkt_ctxt->ing_vrf_entry == NULL) {
            jnx_gw_data_drop_pkt(pkt_ctxt);
            return JNX_GW_DATA_SUCCESS;
        }
    }

    switch(pkt_ctxt->stat_type) {

        case JNX_GW_DATA_ERR_GRE_PKT_WITHOUT_KEY:
        case JNX_GW_DATA_ERR_GRE_PKT_WITH_SEQ:
        case JNX_GW_DATA_ERR_GRE_PKT_INVALID_PROTO:

            atomic_add_uint(1, &pkt_ctxt->ing_vrf_entry->
                            vrf_stats.invalid_pkt);
            break;

        case JNX_GW_DATA_ERR_GRE_TUNNEL_NOT_PRESENT:
        case JNX_GW_DATA_ERR_GRE_TUNNEL_NOT_READY:

            atomic_add_uint(1, &pkt_ctxt->ing_vrf_entry->
                            vrf_stats.tunnel_not_present);
            break;

        case JNX_GW_DATA_GRE_INNER_IP_TTL:    
            atomic_add_uint(1, &pkt_ctxt->gre_tunnel->stats.ttl_drop);
            break;

        case JNX_GW_DATA_CONG_DROP:
            /* Increment the Drop due to congestion, increment
             * this stat in the GRE Tunnel
             */
            atomic_add_uint(1, &pkt_ctxt->gre_tunnel->ipip_tunnel->
                            stats.cong_drop);
            break;
        default:
            break;
    }

    jnx_gw_data_drop_pkt(pkt_ctxt);
    return JNX_GW_DATA_SUCCESS;
}

/**
 * 
 * This function is used to stats in the Ingress VRF AND IP-IP Tunnel
 *
 * @param[in] pkt_ctxt     Packet Processing context used to process the packet 
 * 
 * @return Result of the operation
 *     @li JNX_GW_DATA_SUCCESS  Function was succesful
 */
jnx_gw_data_err_t
jnx_gw_data_process_ing_ipip_vrf_stats(jnx_gw_pkt_proc_ctxt_t*  pkt_ctxt)
{
    uint32_t pkt_len;

    pkt_len = jbuf_length(pkt_ctxt->pkt_buf, NULL);

    /* Increment the Packets in for the IP-IP Tunnel */
    atomic_add_uint(1, 
                    &(pkt_ctxt->ipip_sub_tunnel->
                      ipip_tunnel->stats.packets_in));

    /* Increment the Bytes in for the IP-IP Tunnel */
    atomic_add_uint(pkt_len,
                    &(pkt_ctxt->ipip_sub_tunnel->ipip_tunnel->
                      stats.bytes_in));

    /* Increment the Packets in for the Ingress VRF */
    atomic_add_uint(1, 
                    &pkt_ctxt->ing_vrf_entry->stats.packets_in);

    /* Increment the "Bytes In" for the Ingress VRF */
    atomic_add_uint(pkt_len,
                    &pkt_ctxt->ing_vrf_entry->stats.bytes_in);

    return JNX_GW_DATA_SUCCESS;
}

/**
 * 
 * This function is used to stats in the Egress VRF AND GRE Tunnel
 *
 * @param[in] pkt_ctxt     Packet Processing context used to process the packet 
 * 
 * @return Result of the operation
 *     @li JNX_GW_DATA_SUCCESS  Function was succesful
 */
static jnx_gw_data_err_t 
jnx_gw_data_process_eg_gre_vrf_stats(jnx_gw_pkt_proc_ctxt_t*    pkt_ctxt)
{
    uint32_t pkt_len;

    pkt_len = jbuf_length(pkt_ctxt->pkt_buf, NULL);

    /* Increment the Packets out for the GRE Tunnel */
    atomic_add_uint(1,&pkt_ctxt->ipip_sub_tunnel->
                    gre_tunnel->stats.packets_out); 

    /* Increment the Bytes Out for the GRE Tunnel */
    atomic_add_uint(pkt_len,
                    &pkt_ctxt->ipip_sub_tunnel->gre_tunnel->
                    stats.bytes_out);
    
    /* Increment the Packets Out for the Egress VRF */
    atomic_add_uint(1, &pkt_ctxt->ing_vrf_entry->stats.packets_out);

    /* Increment the Bytes Out for the Egress VRF */
    atomic_add_uint(pkt_len,
                    &pkt_ctxt->ing_vrf_entry->stats.bytes_out);

    return JNX_GW_DATA_SUCCESS;
}

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
static jnx_gw_data_err_t
jnx_gw_data_process_eg_ipip_vrf_stats(jnx_gw_pkt_proc_ctxt_t*   pkt_ctxt)
{
    uint32_t pkt_len;

    pkt_len = jbuf_length(pkt_ctxt->pkt_buf, NULL);

    /* Increment the Packets out for the IPIP Tunnel */
    atomic_add_uint(1,
                    &pkt_ctxt->gre_tunnel->ipip_tunnel->stats.packets_out); 

    /* Increment the Bytes Out for the IP-IP Tunnel */
    atomic_add_uint(pkt_len,
                    &pkt_ctxt->gre_tunnel->ipip_tunnel->stats. bytes_out);
    
    /* Increment the Packets Out for the Egress VRF */
    atomic_add_uint(1, &pkt_ctxt->eg_vrf_entry->stats.packets_out);

    /* Increment the Bytes Out for the Egress VRF */
    atomic_add_uint(pkt_len,
                    &pkt_ctxt->eg_vrf_entry->stats.bytes_out);

    return JNX_GW_DATA_SUCCESS;
}

/**
 * 
 * This function is used to stats in the Ingress VRF AND GRE Tunnel
 *
 * @param[in] pkt_ctxt     Packet Processing context used to process the packet 
 * 
 * @return Result of the operation
 *     @li JNX_GW_DATA_SUCCESS  Function was succesful
 */
static jnx_gw_data_err_t 
jnx_gw_data_process_ing_gre_vrf_stats(jnx_gw_pkt_proc_ctxt_t*   pkt_ctxt)
{
    uint32_t pkt_len;

    pkt_len = jbuf_length(pkt_ctxt->pkt_buf, NULL);

    /* Increment the Packets in for the GRE Tunnel */
    atomic_add_uint(1, &pkt_ctxt->gre_tunnel->stats.packets_in);

    /* Increment the Bytes in for the GRE Tunnel */
    atomic_add_uint(pkt_len,
                    &pkt_ctxt->gre_tunnel->stats.bytes_in);
    
    /* Increment the Packets in for the Ingress VRF */
    atomic_add_uint(1, &pkt_ctxt->ing_vrf_entry->stats.packets_in);

    /* Increment the "Bytes In" for the Ingress VRF */
    atomic_add_uint(pkt_len,
                    &pkt_ctxt->ing_vrf_entry->stats.bytes_in);

    return JNX_GW_DATA_SUCCESS;
}
/**
 * 
 * This function is used to drop the packet.
 *
 * @param[in] pkt_ctxt     Packet Processing context used to process the packet 
 * 
 */

static void
jnx_gw_data_drop_pkt(jnx_gw_pkt_proc_ctxt_t*  pkt_ctxt)
{

    /* We need to inform the POT that this packet is being dropped and the
     * consider the sequence number corresponding to this packet as being
     * received
     */

    /*
     * Now we can drop the packet 
     */ 

    jbuf_free(pkt_ctxt->pkt_buf);

    return;
}


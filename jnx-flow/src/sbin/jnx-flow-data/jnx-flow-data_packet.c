/*
 * $Id: jnx-flow-data_packet.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-flow-data_packet.c -  Packet processing thread routines
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
 * @file jnx-flow-data_packet.c 
 * @brief
 * This file contains the packet processing thread functions
 * The flow of a packet when received is processed as follows
 *   create the service key entry for the packet
 *   create the flow key entry for the packet
 *   find or create the flow entry for the packet flow attribute
 *   apply the flow action on the packet
 *   update statistics
 *   invariably queue the packet to the tx fifo
 *   with free flag set, if the packet needs to be dropped
 */

#include "jnx-flow-data.h"

/**
 * This function queues the packet to the tx fifo for
 * the packet processing thread
 * sets the free flag, incase, the packet need to be dropped
 *
 * TBD(XXX) send failure retry logic 
 *
 * @param  pkt_ctxt   packet processing thread context pointer
 * @param  action     whether packet needs to be dropped or not
 * @returns
 *      NONE
 */
static void
jnx_flow_data_send_packet(jnx_flow_data_pkt_ctxt_t *pkt_ctxt,
                          jnx_flow_rule_action_type_t action)
{
    if (action == JNX_FLOW_RULE_ACTION_DROP) {
        jbuf_free(pkt_ctxt->pkt_buf);
        return;
    }
    msp_data_send(pkt_ctxt->thread_handle, pkt_ctxt->pkt_buf,
                  MSP_MSG_TYPE_PACKET);
    return;
}

/**
 * This function tries to find a service set for the received
 * packet, with the service attributes in the packet processing
 * thread context.
 * matches the rules in the service set to extract the action
 * information needed to be applied on this flow
 * @param  pkt_ctxt   packet processing thread context pointer
 * @returns 
 *    pservice_set    if a service set is found, & a rule match
 *                    is successfully applied for the packet
 *    NULL            otherwise
 */
static jnx_flow_data_svc_set_t *
jnx_flow_data_match_svc_set(jnx_flow_data_pkt_ctxt_t * pkt_ctxt)
{
    jnx_flow_data_svc_set_t    *psvc_set = NULL;
    jnx_flow_data_list_entry_t *pentry = NULL;
    jnx_flow_data_rule_entry_t *prule = NULL;

    /*
     * acquire configuration read lock
     */
    JNX_FLOW_DATA_ACQUIRE_CONFIG_READ_LOCK(pkt_ctxt->data_cb);

    /*
     * see whether we have the service set configured,
     * with the packet jbuf header service information attributes
     */
    if ((psvc_set = jnx_flow_data_svc_set_lookup(pkt_ctxt->data_cb,
                                                 &pkt_ctxt->svc_key))
        == NULL) {
        /*
         * could not find the service set, release configuration read lock
         * return NULL
         */
        JNX_FLOW_DATA_RELEASE_CONFIG_READ_LOCK(pkt_ctxt->data_cb);
        return NULL;
    }

    /*
     * scan the rule list the service set in serial fashion for matching
     * with the packet flow attributes
     */
    for (pentry = psvc_set->svc_rule_set.list_head; (pentry);
         pentry = pentry->next) {

        prule = pentry->ptr;

        /*
         * direction does not match, continue
         */
        if ((pkt_ctxt->flow_direction != JNX_FLOW_RULE_DIRECTION_INVALID) &&
            (prule->rule_direction != pkt_ctxt->flow_direction)) {
            continue;
        }

        /*
         * match source address, prefix match
         */
        if ((pkt_ctxt->flow_key.session.src_addr &
             prule->rule_session.src_mask) !=
            (prule->rule_session.session.src_addr &
             prule->rule_session.src_mask)) {
            break;
        }

        /*
         * match destination address, prefix match
         */
        if ((pkt_ctxt->flow_key.session.dst_addr &
             prule->rule_session.dst_mask) !=
            (prule->rule_session.session.dst_addr &
             prule->rule_session.dst_mask)) {
            break;
        }

        /*
         * match protocol, equal match
         * 0 is wild card
         */
        if ((prule->rule_session.session.proto) &&
            (pkt_ctxt->flow_key.session.proto !=
             prule->rule_session.session.proto)) {
            break;
        }

        /*
         * match source port, equal match
         * 0 is wild card
         */
        if ((prule->rule_session.session.src_port) &&
            (pkt_ctxt->flow_key.session.src_port !=
             prule->rule_session.session.src_port)) {
            break;
        }

        /*
         * match destination port, equal match
         * 0 is wild card
         */
        if ((prule->rule_session.session.dst_port) &&
            (pkt_ctxt->flow_key.session.dst_port !=
             prule->rule_session.session.dst_port)) {
            break;
        }

        /*
         * this rule matched for the received packet
         * set the rule entry in the packet context structure
         * increment the rule applied count
         */
        pkt_ctxt->rule_entry = prule;
        pkt_ctxt->rule_count++;

        /*
         * if the rule action is drop, break
         */
        if (prule->rule_action == JNX_FLOW_RULE_ACTION_DROP) {
            break;
        }
    }

    /*
     * release configuration read lock
     */
    JNX_FLOW_DATA_RELEASE_CONFIG_READ_LOCK(pkt_ctxt->data_cb);

    /* 
     * matching rule entry in the service set found
     */
    if (pkt_ctxt->rule_entry)  {
        return psvc_set;
    }

    /*
     * otherwise, return NULL
     */
    return NULL;
}

/**
 * This function tries to  find a matching flow entry for the
 * received packet in the packet processing thread context
 * If a matching flow entry could not be found, tries to
 * create the flow entries, for both forward and reverse direction
 * by doing a rule match for the service set attribute of the 
 * received packet
 * @param  pkt_ctxt   packet processing thread context pointer
 * @returns 
 *    flow_entry      if a matching flow entry is found, or successfully
                      created in both forward and reverse direction
 *    NULL            otherwise
 * 
 */
static jnx_flow_data_flow_entry_t *
jnx_flow_data_find_add_flow_entry(jnx_flow_data_pkt_ctxt_t * pkt_ctxt)
{
    uint32_t                              hash, rhash;
    jnx_flow_session_key_t                flow_key;
    register jnx_flow_data_flow_entry_t  *flow_entry = NULL,
                                         *rflow_entry = NULL;
    register jnx_flow_data_svc_set_t     *psvc = NULL;
    jnx_flow_data_flow_entry_t*           tmp_entry = NULL;

    /*
     * calculate the hash index
     */
    hash = jnx_flow_data_get_flow_hash(&pkt_ctxt->flow_key);
    pkt_ctxt->flow_hash = hash;

    /*
     * acquire the bucket lock
     */

    JNX_FLOW_DATA_HASH_LOCK(pkt_ctxt->data_cb->cb_flow_db, hash);

    /*
     * find the flow entry
     */

    flow_entry = JNX_FLOW_DATA_HASH_CHAIN(pkt_ctxt->data_cb, hash);

    for (;flow_entry; flow_entry = flow_entry->flow_next) {

        /*
         * compare the flow entry key
         */
        if (bcmp(&flow_entry->flow_key, &pkt_ctxt->flow_key,
                 sizeof(flow_key))) {
            continue;
        }

        /*
         * flow entry found 
         * release the bucket lock
         */

        JNX_FLOW_DATA_HASH_UNLOCK(pkt_ctxt->data_cb->cb_flow_db, hash);

        /*
         * the flow entry is not up,
         * return NULL
         */
        if (flow_entry->flow_status != JNX_FLOW_DATA_STATUS_UP) {

            return NULL;
        }

        /*
         * return flow entry
         */
        return flow_entry;
    }

    JNX_FLOW_DATA_HASH_UNLOCK(pkt_ctxt->data_cb->cb_flow_db, hash);

    /*
     * the flow entry is not present
     * find the service set, for a new flow creation
     */

    if (!(psvc = jnx_flow_data_match_svc_set(pkt_ctxt))) {
        /*
         * could not find a matching service set/rule entry
         * release the bucket lock
         * return NULL
         */
        return NULL;
    }

    /*
     * create the forward flow entry 
     * allocate memory for the forward flow entry
     */
    if ((flow_entry = FLOW_ALLOC(pkt_ctxt->data_cb->cb_flow_oc,
                                         pkt_ctxt->thread_idx)) == NULL) {
        /*
         * could not allocate memory, release the bucket lock
         * return NULL
         */
        return NULL;
    }

    memset(flow_entry, 0, sizeof(*flow_entry));

    JNX_FLOW_DATA_FLOW_LOCK_INIT(flow_entry);
    
    /*
     * set the flow entry attributes
     */
    flow_entry->flow_status               = JNX_FLOW_DATA_STATUS_INIT;
    flow_entry->flow_dir                  = pkt_ctxt->flow_direction;
    flow_entry->flow_key.svc_key.svc_type = pkt_ctxt->flow_key.svc_key.svc_type;
    flow_entry->flow_key.svc_key.svc_id   = pkt_ctxt->flow_key.svc_key.svc_id;
    flow_entry->flow_key.session.src_addr = pkt_ctxt->flow_key.session.src_addr;
    flow_entry->flow_key.session.dst_addr = pkt_ctxt->flow_key.session.dst_addr;
    flow_entry->flow_key.session.src_port = pkt_ctxt->flow_key.session.src_port;
    flow_entry->flow_key.session.dst_port = pkt_ctxt->flow_key.session.dst_port;
    flow_entry->flow_key.session.proto    = pkt_ctxt->flow_key.session.proto;

    /*
     * set the service set & action attributes
     */
    flow_entry->flow_svc_id    = psvc->svc_id;
    flow_entry->flow_rule_id   = pkt_ctxt->rule_entry->rule_id;
    flow_entry->flow_action    = pkt_ctxt->rule_entry->rule_action;
    flow_entry->flow_ts        = pkt_ctxt->pkt_ts;

    /*
     * for nexthop style service flow entries
     * gather the egress interface value
     */
    if (psvc->svc_type == JNX_FLOW_SVC_TYPE_NEXTHOP) {
        flow_entry->flow_key.svc_key.svc_iif  = psvc->svc_iif;
        flow_entry->flow_oif                  = psvc->svc_oif;
    } else {
        flow_entry->flow_key.svc_key.svc_id = psvc->svc_id;
    }

    JNX_FLOW_DATA_HASH_LOCK(pkt_ctxt->data_cb->cb_flow_db, hash);

    /* 
     * Check if there is an entry already added by some other thread
     * In that case, just free the flow entry created, and check it's state.
     * IF the state is not UP then return NULL
     */
    tmp_entry = JNX_FLOW_DATA_HASH_CHAIN(pkt_ctxt->data_cb, hash);

    for (;tmp_entry; tmp_entry = tmp_entry->flow_next) {

        /*
         * compare the flow entry key
         */
        if (bcmp(&tmp_entry->flow_key, &pkt_ctxt->flow_key,
                 sizeof(flow_key))) {
            continue;
        }

        JNX_FLOW_DATA_HASH_UNLOCK(pkt_ctxt->data_cb->cb_flow_db, hash);

        if (tmp_entry->flow_status == JNX_FLOW_DATA_STATUS_UP) {
            
            FLOW_FREE(pkt_ctxt->data_cb->cb_flow_oc, flow_entry, 
                      pkt_ctxt->thread_idx);
            return tmp_entry;
        } else {

            FLOW_FREE(pkt_ctxt->data_cb->cb_flow_oc, flow_entry, 
                      pkt_ctxt->thread_idx);
            return NULL;
        }
    }

    /*
     * add to the hash bucket
     */
    if (JNX_FLOW_DATA_HASH_CHAIN(pkt_ctxt->data_cb, hash) == NULL) {
        JNX_FLOW_DATA_HASH_LAST(pkt_ctxt->data_cb, hash) = flow_entry;
    }
    flow_entry->flow_next =
        JNX_FLOW_DATA_HASH_CHAIN(pkt_ctxt->data_cb, hash);
    JNX_FLOW_DATA_HASH_CHAIN(pkt_ctxt->data_cb, hash) = flow_entry;

    /*
     * release the bucket lock
     */
    JNX_FLOW_DATA_HASH_UNLOCK(pkt_ctxt->data_cb->cb_flow_db, hash);

    /*
     * create the reverse flow entry
     * frame the reverse flow key first
     */

    memset(&flow_key, 0, sizeof(flow_key));

    flow_key.svc_key.svc_type = flow_entry->flow_key.svc_key.svc_type;
    flow_key.session.src_addr = flow_entry->flow_key.session.dst_addr;
    flow_key.session.dst_addr = flow_entry->flow_key.session.src_addr;
    flow_key.session.src_port = flow_entry->flow_key.session.dst_port;
    flow_key.session.dst_port = flow_entry->flow_key.session.src_port;
    flow_key.session.proto    = flow_entry->flow_key.session.proto;

    if (psvc->svc_type == JNX_FLOW_SVC_TYPE_NEXTHOP)
        flow_key.svc_key.svc_iif  = flow_entry->flow_oif;
    else 
        flow_key.svc_key.svc_id   = flow_entry->flow_svc_id;


    /*
     * calculate the hash index for the reverse flow
     */
    rhash = jnx_flow_data_get_flow_hash(&flow_key);

    /*
     * acquire the bucket lock for reverse flow creation
     */
    JNX_FLOW_DATA_HASH_LOCK(pkt_ctxt->data_cb->cb_flow_db, rhash);

    /*
     * check whether the reverse flow entry is already present
     */
    rflow_entry = JNX_FLOW_DATA_HASH_CHAIN(pkt_ctxt->data_cb, rhash);

    for (;rflow_entry; rflow_entry = rflow_entry->flow_next) {

        /*
         * compare the flow entry key
         */
        if (bcmp(&rflow_entry->flow_key, &flow_key, sizeof(flow_key))) {
            continue;
        }

        /* 
         * reverse flow entry found, release the bucket lock 
         * mark the newly created forward flow entry as deleted
         * this entry will be deleted in the periodic cleanup function
         */
        JNX_FLOW_DATA_HASH_UNLOCK(pkt_ctxt->data_cb->cb_flow_db, rhash);
        
        JNX_FLOW_DATA_FLOW_LOCK(flow_entry);

        flow_entry->flow_status = JNX_FLOW_DATA_STATUS_DELETE;

        JNX_FLOW_DATA_FLOW_UNLOCK(flow_entry);

        JNX_FLOW_DATA_FLOW_LOCK(rflow_entry);

        rflow_entry->flow_status = JNX_FLOW_DATA_STATUS_DOWN;

        JNX_FLOW_DATA_FLOW_UNLOCK(rflow_entry);

        return NULL;
    }
    
    JNX_FLOW_DATA_HASH_UNLOCK(pkt_ctxt->data_cb->cb_flow_db, rhash);

    /*
     * reverse flow entry is not present
     * allocate memory for the flow entry
     */
    if ((rflow_entry = FLOW_ALLOC(pkt_ctxt->data_cb->cb_flow_oc,
                                          pkt_ctxt->thread_idx)) == NULL) {
        /*
         * could not allocate memory for the reverse flow entry
         * release the hash bucket lock for the reverse flow entry
         * mark the newly created forward flow entry as deleted
         * this entry will be deleted in the periodic cleanup function
         * return NULL
         */
        JNX_FLOW_DATA_FLOW_LOCK(flow_entry);
        flow_entry->flow_status = JNX_FLOW_DATA_STATUS_DELETE;
        JNX_FLOW_DATA_FLOW_UNLOCK(flow_entry);
        return NULL;
    }

    memset(rflow_entry, 0, sizeof(*rflow_entry));
    /*
     * set the reverse flow entry attributes
     */
    rflow_entry->flow_status               = JNX_FLOW_DATA_STATUS_INIT;
    rflow_entry->flow_key.svc_key.svc_type = flow_key.svc_key.svc_type;
    rflow_entry->flow_key.session.src_addr = flow_key.session.src_addr;
    rflow_entry->flow_key.session.dst_addr = flow_key.session.dst_addr;
    rflow_entry->flow_key.session.src_port = flow_key.session.src_port;
    rflow_entry->flow_key.session.dst_port = flow_key.session.dst_port;
    rflow_entry->flow_key.session.proto    = flow_key.session.proto;

    /*
     * set the service set & action attributes
     */
    rflow_entry->flow_svc_id   = flow_entry->flow_svc_id;
    rflow_entry->flow_rule_id  = flow_entry->flow_rule_id;
    rflow_entry->flow_action   = flow_entry->flow_action;
    rflow_entry->flow_ts       = pkt_ctxt->pkt_ts;

    /*
     * Reverse the flow direction
     */

    rflow_entry->flow_dir = flow_entry->flow_dir;
    if (rflow_entry->flow_dir != JNX_FLOW_RULE_DIRECTION_INVALID) {
        if (rflow_entry->flow_dir == JNX_FLOW_RULE_DIRECTION_OUTPUT) {
            rflow_entry->flow_dir = JNX_FLOW_RULE_DIRECTION_INPUT;
        }
        else {
            rflow_entry->flow_dir = JNX_FLOW_RULE_DIRECTION_OUTPUT;
        }
    }

    /*
     * for nexthop style service flow entries
     * gather the egress interface value
     */

    if (psvc->svc_type == JNX_FLOW_SVC_TYPE_NEXTHOP) {
        rflow_entry->flow_key.svc_key.svc_iif  = psvc->svc_oif;
        rflow_entry->flow_oif                  = psvc->svc_iif;
    } else {
        rflow_entry->flow_key.svc_key.svc_id   = psvc->svc_id;
    }

    /*
     * set the reverse pointers for both the flows
     */

    rflow_entry->flow_entry  = flow_entry;
    rflow_entry->flow_status = JNX_FLOW_DATA_STATUS_UP;

    JNX_FLOW_DATA_HASH_LOCK(pkt_ctxt->data_cb->cb_flow_db, rhash);

    /*
     * add to the bucket
     */
    if (JNX_FLOW_DATA_HASH_CHAIN(pkt_ctxt->data_cb, rhash) == NULL) {
        JNX_FLOW_DATA_HASH_LAST(pkt_ctxt->data_cb, rhash) = rflow_entry;
    }

    rflow_entry->flow_next = JNX_FLOW_DATA_HASH_CHAIN(pkt_ctxt->data_cb, rhash);
    JNX_FLOW_DATA_HASH_CHAIN(pkt_ctxt->data_cb, rhash) = rflow_entry;

    JNX_FLOW_DATA_HASH_UNLOCK(pkt_ctxt->data_cb->cb_flow_db, rhash);

    /*
     * set the flow status as UP for both the flows
     */
    JNX_FLOW_DATA_FLOW_LOCK(flow_entry);

    flow_entry->flow_status = JNX_FLOW_DATA_STATUS_UP;
    flow_entry->flow_entry  = rflow_entry;
    
    JNX_FLOW_DATA_FLOW_UNLOCK(flow_entry);

    /*
     * update the rule statistics
     */
    atomic_add_uint(1, &pkt_ctxt->rule_entry->rule_stats.rule_applied_count);

    /* 
     * increment by 2, one forward and one reverse flow 
     */
    atomic_add_uint(2, &pkt_ctxt->rule_entry->rule_stats.rule_total_flow_count);
    atomic_add_uint(2, &pkt_ctxt->rule_entry->rule_stats.rule_active_flow_count);
    /*
     * Update the service set statistics 
     */
    atomic_add_uint(2, &psvc->svc_stats.total_flow_count);
    atomic_add_uint(2, &psvc->svc_stats.active_flow_count);

    /*
     * This is for the number of rules applied
     */
    atomic_add_uint(pkt_ctxt->rule_count,
                    &psvc->svc_stats.applied_rule_count);
    atomic_add_uint(pkt_ctxt->rule_count,
                    &pkt_ctxt->data_cb->cb_stats.applied_rule_count);

    /* 
     * For both service set and the application control block
     * if the flow action is drop, update the drop flow count
     * otherwise update the allow flow count
     */
    if (flow_entry->flow_action == JNX_FLOW_RULE_ACTION_DROP) {
        atomic_add_uint(2, &psvc->svc_stats.total_drop_flow_count);
        atomic_add_uint(2, &psvc->svc_stats.active_drop_flow_count);
        atomic_add_uint(2, &pkt_ctxt->data_cb->
                        cb_stats.total_drop_flow_count);
        atomic_add_uint(2, &pkt_ctxt->data_cb->
                        cb_stats.active_drop_flow_count);
    } else {
        atomic_add_uint(2, &psvc->svc_stats.total_allow_flow_count);
        atomic_add_uint(2, &psvc->svc_stats.active_allow_flow_count);
        atomic_add_uint(2, &pkt_ctxt->data_cb->
                        cb_stats.total_allow_flow_count);
        atomic_add_uint(2, &pkt_ctxt->data_cb->
                        cb_stats.active_allow_flow_count);
    }

    /*
     * application control block statistics
     */
    atomic_add_uint(2, &pkt_ctxt->data_cb->cb_stats.total_flow_count);
    atomic_add_uint(2, &pkt_ctxt->data_cb->cb_stats.active_flow_count);

    /*
     * return the forward flow entry
     */
    return flow_entry;
}

/**
 * This is the main entry point for the packet processing thread running
 * on a data cpu. Here the input argument is parsed to get the 
 * cpu context, fifo processing handle, & other attributes
 * Waits in a forever while loop to dequeue packets from the rx fifo
 * attached with the data cpu, & applies the flow actions on the 
 * packet
 * @param  loop_args   packet processing thread parameter structure pointer
 */
void *
jnx_flow_data_process_packet(void * loop_args)
{
    uint32_t                            cpu_num, pkt_type;
    jnx_flow_data_cb_t                  *data_cb;
    msp_dataloop_args_t                 *data_args_p;
    register jnx_flow_data_pkt_ctxt_t   *pkt_ctxt;
    register jnx_flow_data_flow_entry_t *flow_entry;
    jbuf_svc_set_info_t                  svc_set_info;

    /*
     * extract the CPU number and the application control block pointers
     */
    data_args_p = (typeof(data_args_p))loop_args;
    cpu_num     = msp_data_get_cpu_num(data_args_p->dhandle);
    data_cb     = (typeof(data_cb))data_args_p->app_data;


    /*
     * get the packet process context
     */
    pkt_ctxt = &data_cb->cb_pkt_ctxt[cpu_num];

    /*
     * initialize the packet context to some extent
     */
    pkt_ctxt->data_cb        = data_cb;
    pkt_ctxt->thread_idx     = cpu_num;
    pkt_ctxt->thread_handle  = data_args_p->dhandle;

    /*
     * Initialize the list here for send failure retry logic
     * TBD(XXX)
     */

    /*
     * packet loop
     */
    while (1) {

        /*
         * application is closing down, break from the packet loop
         */
        if (data_cb->cb_status == JNX_FLOW_DATA_STATUS_SHUTDOWN) {
            break;
        }

        /*
         * initialize the flow key, these may not be set,
         * so set them to wild card values
         */
        pkt_ctxt->flow_key.session.src_port = 0;
        pkt_ctxt->flow_key.session.dst_port = 0;

        /* 
         * Reset the rule attributes, needed while
         * matching rules for new flow creation
         */
        pkt_ctxt->rule_entry = NULL;
        pkt_ctxt->rule_count = 0;

        /*
         * dequeue packets from the fifo for the packet processing thread
         */
        if ((pkt_ctxt->pkt_buf = msp_data_recv(pkt_ctxt->thread_handle,
                                               &pkt_type))
            == NULL) {
            continue;
        }

        /*
         * application is still not initialized, wait
         */
        if (data_cb->cb_status == JNX_FLOW_DATA_STATUS_INIT) {
            jnx_flow_data_send_packet(pkt_ctxt, JNX_FLOW_RULE_ACTION_DROP);
            continue;
        }

        jbuf_get_svc_set_info(pkt_ctxt->pkt_buf, &svc_set_info);

        /*
         * no need to do ip layer validation, this should already
         * been performed in the pfe engine
         *
         * get the service type from the packet buffer
         */

        if (svc_set_info.svc_type == JBUF_SVC_TYPE_INTERFACE) {
            pkt_ctxt->svc_key.svc_type = pkt_ctxt->flow_key.svc_key.svc_type = 
                JNX_FLOW_SVC_TYPE_INTERFACE;
        } else {
            pkt_ctxt->svc_key.svc_type = pkt_ctxt->flow_key.svc_key.svc_type = 
                JNX_FLOW_SVC_TYPE_NEXTHOP;
        }

        /*
         * Get the service application direction
         */
        pkt_ctxt->flow_direction = svc_set_info.pkt_dir;

        /*
         * if the packet service type is nexthop-style
         * extract the ingress interface from the packet buffer, 
         * otherwise, extrace the svc-id from the packet buffer
         */
        if (pkt_ctxt->svc_key.svc_type == JNX_FLOW_SVC_TYPE_NEXTHOP) {

            pkt_ctxt->svc_key.svc_iif =
                pkt_ctxt->flow_key.svc_key.svc_iif = 
                svc_set_info.info.nexthop_type.rcv_subunit;

        } else {

            pkt_ctxt->svc_key.svc_id =
                pkt_ctxt->flow_key.svc_key.svc_id = 
                svc_set_info.info.intf_type.svc_set_id;
        }

        /*
         * extract the flow information for the packet
         */
        pkt_ctxt->ip_hdr =
            jbuf_to_d(pkt_ctxt->pkt_buf, typeof(pkt_ctxt->ip_hdr));

        /*
         * Extract the ip layer information, 
         * source ip, destination ip, & the ip protocol
         */
        pkt_ctxt->flow_key.session.src_addr =
            ntohl(pkt_ctxt->ip_hdr->ip_src.s_addr);
        pkt_ctxt->flow_key.session.dst_addr =
            ntohl(pkt_ctxt->ip_hdr->ip_dst.s_addr);
        pkt_ctxt->flow_key.session.proto    = pkt_ctxt->ip_hdr->ip_p;

        /*
         * Extract the packet length
         */
        pkt_ctxt->pkt_len = ntohs(pkt_ctxt->ip_hdr->ip_len);


        /*
         * Extract the ip protocol layer information
         */
        if ((pkt_ctxt->flow_key.session.proto == IPPROTO_TCP) ||
            (pkt_ctxt->flow_key.session.proto == IPPROTO_UDP)) {

            uint32_t ip_hlen = pkt_ctxt->ip_hdr->ip_hl <<2 , offset; 
            struct jbuf * jb;

            /*
             * get the source and destination application port values
             */

            if ((jb = jbuf_getptr(pkt_ctxt->pkt_buf, ip_hlen, &offset))) {
                pkt_ctxt->tcp_hdr = (typeof(pkt_ctxt->tcp_hdr))
                    (jbuf_to_d(jb, char *) + offset);

                pkt_ctxt->flow_key.session.src_port =
                    ntohs(pkt_ctxt->tcp_hdr->th_sport);

                pkt_ctxt->flow_key.session.dst_port =
                    ntohs(pkt_ctxt->tcp_hdr->th_dport);
            }
        }

        /*
         * Get the time stamp for the packet
         */
        jnx_flow_data_update_ts(pkt_ctxt->data_cb, &pkt_ctxt->pkt_ts);

        /*
         * Find the flow entry for the received packet 
         * if not found, create one
         * no need to hold the lock after this
         */
        flow_entry = jnx_flow_data_find_add_flow_entry(pkt_ctxt);

        /*
         * flow entry found, update the stats, apply the action
         */

        if (flow_entry) {

            /*
             * set the time stamp in the flow entry
             */
            flow_entry->flow_ts = pkt_ctxt->pkt_ts;

            /*
             * flow entry ingress stats
             */
            atomic_add_uint(1, &flow_entry->flow_stats.pkts_in);

            atomic_add_uint(pkt_ctxt->pkt_len, &flow_entry->flow_stats.bytes_in);

            /*
             * flow entry action stats
             */
            if (flow_entry->flow_action == JNX_FLOW_RULE_ACTION_ALLOW) {

                atomic_add_uint(1, &flow_entry->flow_stats.pkts_out);
                atomic_add_uint(pkt_ctxt->pkt_len,
                                &flow_entry->flow_stats.bytes_out);
                /*
                 * for next hop style svc,
                 * set the egress interface subunit in the jbuf
                 */
                if (flow_entry->flow_key.svc_key.svc_type ==
                    JNX_FLOW_SVC_TYPE_NEXTHOP) {
                    jbuf_set_ointf(pkt_ctxt->pkt_buf, flow_entry->flow_oif);
                }
            } else {
                /*
                 * increment the drop statistics for the flow
                 */
                atomic_add_uint(1, &flow_entry->flow_stats.pkts_dropped);
                atomic_add_uint(pkt_ctxt->pkt_len,
                                &flow_entry->flow_stats.bytes_dropped);
            }

            /*
             * Now send the packet out, with the action applied
             */
            jnx_flow_data_send_packet(pkt_ctxt, flow_entry->flow_action);

            continue;
        }

        /*
         * could not create/find the flow entry, allow (?)
         */
        jnx_flow_data_send_packet(pkt_ctxt, JNX_FLOW_RULE_ACTION_DROP);

    }
    return NULL;
}

/*
 * $Id: jnx-flow-data_utils.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-flow-data_utils.c -  Various utility functions
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
 * @file jnx-flow-data_utils.c 
 * @brief
 * This file contains various utils routines, used by
 * the jnx-flow-data module
 */

#include "jnx-flow-data.h"
/**
 * This function updates the time stamp for the active flows
 * and the periodic time stamp
 * @params data_cb   pointer to the data agent control block
 * @params ts        pointer to the timestamp entry
 * @returns
 *  EOK             
 */
status_t
jnx_flow_data_update_ts(jnx_flow_data_cb_t * data_cb, uint64_t * ts)
{
    if (ts) {
        *ts = data_cb->cb_periodic_ts;
    } else {
        data_cb->cb_periodic_ts += JNX_FLOW_DATA_PERIODIC_SEC;
    }
    return EOK;
}

/**
 * This function computes the hash for a flow entry
 * @params flow_key    pointer to the flow key structure
 * @returns
 *  hash        hash key for the flow entry             
 */
uint32_t 
jnx_flow_data_get_flow_hash(jnx_flow_session_key_t * flow_key)
{
    uint32_t *key;
    uint32_t hash;

    key = (typeof(key))flow_key;

    hash = JNX_FLOW_HASH_MAGIC_NUMBER;
    hash = ((hash << 5) + hash) ^ key[0];
    hash = ((hash << 5) + hash) ^ key[1];
    hash = ((hash << 5) + hash) ^ key[2];
    hash = ((hash << 5) + hash) ^ key[3];
    hash = ((hash << 5) + hash) ^ key[4];

    return (hash & JNX_FLOW_DATA_FLOW_HASH_MASK);
}

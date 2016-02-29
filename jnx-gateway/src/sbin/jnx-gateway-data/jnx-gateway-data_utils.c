/*
 *$Id: jnx-gateway-data_utils.c 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-gateway-data_utils.c - Common routines used by the control thread &
 *                            data threads of JNX-GATEWAY_DATA application.
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
 * @file jnx-gateway-data-utils.c
 * @brief This file contains the utility routines used by the control &
 *        data threads.
 * 
 * This file covers the following stuff:-
 * 1. Look up, addition & deltion routines fron the various tunnel databases
 * 2. Routines for computation of hash values for the various tunnel keys
 */
#include "jnx-gateway-data_utils.h"
#include "string.h"

jnx_gw_data_gre_tunnel_t*
jnx_gw_data_db_gre_tunnel_lookup_with_lock(jnx_gw_data_cb_t*       app_cb,
                                           jnx_gw_gre_key_hash_t*  gre_key)
{
    jnx_gw_data_gre_tunnel_t*   tmp = NULL;
    uint32_t                    hash_val = 0;

   /* compute the Hash Value from the GRE KEY */
    hash_val = jnx_gw_data_compute_gre_hash(gre_key);

    /*Acquire a Lock on the bucket corresponding to Hash Value */
    jnx_gw_data_acquire_lock(&app_cb->gre_db.hash_bucket[hash_val].bucket_lock);

    /*
     * Perform a linear search in the bucket to see if there is
     * an entry present with the key passed.
     */
    for(tmp = app_cb->gre_db.hash_bucket[hash_val].chain;
        tmp != NULL;
        tmp = tmp->next_in_bucket)
    {
        if((memcmp(&gre_key->key, &tmp->key, sizeof(jnx_gw_gre_key_t))) == 0) {

            break;
        }
    }

    /* Release the lock */
    jnx_gw_data_release_lock(&app_cb->gre_db.hash_bucket[hash_val].bucket_lock);

    return tmp;
}


jnx_gw_data_gre_tunnel_t*
jnx_gw_data_db_gre_tunnel_lookup_without_lock(jnx_gw_data_cb_t*       app_cb,
                                              jnx_gw_gre_key_hash_t*  gre_key)
{
    jnx_gw_data_gre_tunnel_t*   tmp = NULL;
    uint32_t                    hash_val = 0;

    /* compute the Hash Value from the GRE KEY */
    hash_val = jnx_gw_data_compute_gre_hash(gre_key);

    /*
     * Perform a linear search in the bucket to see if there is
     * an entry present with the key passed.
     */
    for(tmp = app_cb->gre_db.hash_bucket[hash_val].chain;
        tmp != NULL;
        tmp = tmp->next_in_bucket)
    {
        if((memcmp(&gre_key->key, &tmp->key, sizeof(jnx_gw_gre_key_t))) == 0) {

            break;
        }
    }

    return tmp;
}

jnx_gw_data_ipip_tunnel_t*
jnx_gw_data_db_ipip_tunnel_lookup_with_lock(jnx_gw_data_cb_t*               app_cb,
                                            jnx_gw_ipip_tunnel_key_hash_t*  key)
{
    jnx_gw_data_ipip_tunnel_t*   tmp = NULL;
    uint32_t                     hash_val = 0;

    /* Compute the hash value from the IP IP Stat Key */
    hash_val = jnx_gw_data_compute_ipip_tunnel_hash(key);

    /*Acquire a Lock on the bucket corresponding to Hash Value */
    jnx_gw_data_acquire_lock(&app_cb->ipip_tunnel_db.hash_bucket[hash_val].bucket_lock);

    for(tmp = app_cb->ipip_tunnel_db.hash_bucket[hash_val].chain;
        tmp != NULL;
        tmp = tmp->next_in_bucket)
    {
        if((memcmp(&key->key, &tmp->key, sizeof(jnx_gw_ipip_tunnel_key_t))) == 0) {

            break;
        }
    }
    
    /* Release the lock */
    jnx_gw_data_release_lock(&app_cb->ipip_tunnel_db.hash_bucket[hash_val].bucket_lock);

    return tmp;
}

jnx_gw_data_ipip_tunnel_t*
jnx_gw_data_db_ipip_tunnel_lookup_without_lock(jnx_gw_data_cb_t*               app_cb,
                                               jnx_gw_ipip_tunnel_key_hash_t*  key)  
{
    jnx_gw_data_ipip_tunnel_t*   tmp = NULL;
    uint32_t                     hash_val = 0;

    /* Compute the hash value from the IP IP Stat Key */
    hash_val = jnx_gw_data_compute_ipip_tunnel_hash(key);

    for(tmp = app_cb->ipip_tunnel_db.hash_bucket[hash_val].chain;
        tmp != NULL;
        tmp = tmp->next_in_bucket)
    {
        if((memcmp(&key->key, &tmp->key, sizeof(jnx_gw_ipip_tunnel_key_t))) == 0) {

            break;
        }
    }
    
    return tmp;
}

jnx_gw_data_gre_tunnel_t*
jnx_gw_data_db_add_gre_tunnel(jnx_gw_data_cb_t*         app_cb,
                              jnx_gw_gre_key_hash_t*    gre_key)
{
    jnx_gw_data_gre_tunnel_t * gre_tunnel = NULL;
    uint32_t                   hash_val = 0;

    /* compute the Hash Value from the GRE KEY */
    hash_val = jnx_gw_data_compute_gre_hash(gre_key);

    /* Allocate an entry for the gre_tunnel */
    if((gre_tunnel = JNX_GW_MALLOC(JNX_GW_DATA_ID,
                 sizeof(jnx_gw_data_gre_tunnel_t))) == NULL) {
         return NULL;
    }

    memset(gre_tunnel, 0, sizeof(jnx_gw_data_gre_tunnel_t)); 

    /*Copy the Key in the allocated entry */
    gre_tunnel->key = gre_key->key;

    /*
     * Mark the state as INIT, the entry should not be used till
     * the entry gets fully populated. Hence, state of the entry
     * will be marked as INIT so that data packets if at all arrive 
     * will not use it.
     */
    gre_tunnel->tunnel_state = JNX_GW_DATA_ENTRY_STATE_INIT;
    gre_tunnel->tunnel_type  = JNX_GW_TUNNEL_TYPE_GRE;

    /* Init the lock for this entry */
    if(jnx_gw_data_lock_init(&gre_tunnel->lock) != EOK) {

        JNX_GW_FREE(JNX_GW_DATA_ID, gre_tunnel);
        return NULL;
    }

    /* Now add this entry in the GRE DB */

    /* Acquire a lock on the bucket */
    jnx_gw_data_acquire_lock(&app_cb->gre_db.hash_bucket[hash_val].bucket_lock);

    gre_tunnel->next_in_bucket = 
                app_cb->gre_db.hash_bucket[hash_val].chain;

    app_cb->gre_db.hash_bucket[hash_val].count++;

    app_cb->gre_db.hash_bucket[hash_val].chain = gre_tunnel;

    /* Release a lock on the bucket */
    jnx_gw_data_release_lock(&app_cb->gre_db.hash_bucket[hash_val].bucket_lock);

    return gre_tunnel;
}

jnx_gw_data_ipip_sub_tunnel_t*
jnx_gw_data_db_add_ipip_sub_tunnel(jnx_gw_data_cb_t*                        app_cb,
                                   jnx_gw_data_ipip_sub_tunnel_key_hash_t*  key)
{
    jnx_gw_data_ipip_sub_tunnel_t*  ipip_sub_tunnel = NULL;
    uint32_t                        hash_val = 0;

    /* Compute the Hash Value from the key */
    hash_val = jnx_gw_data_compute_ipip_sub_tunnel_hash(key);

    /*Allocate an entry for the IP-IP Tunnel */
    if((ipip_sub_tunnel = JNX_GW_MALLOC(JNX_GW_DATA_ID,
                          sizeof(jnx_gw_data_ipip_sub_tunnel_t))) == NULL) {
       return NULL;
    }

    memset(ipip_sub_tunnel, 0, sizeof(jnx_gw_data_ipip_sub_tunnel_t));

    /*Copy the key in the allocated entry */
    ipip_sub_tunnel->key = key->key;

    /* Init the lock for this entry */
    if(jnx_gw_data_lock_init(&ipip_sub_tunnel->lock) != EOK) {

        JNX_GW_FREE(JNX_GW_DATA_ID, ipip_sub_tunnel);
        return NULL;
    }

    /*
     * Mark the state as INIT, the entry should not be used till
     * the entry gets fully populated. Hence, state of the entry
     * will be marked as INIT so that data packets if at all arrives 
     * will not use it.
     */
    ipip_sub_tunnel->tunnel_state = JNX_GW_DATA_ENTRY_STATE_INIT;

    /* Now add this entry in the IP-IP Tunnel DB */

    /* Acquire a lock on the bucket */
    jnx_gw_data_acquire_lock( &app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].bucket_lock);

    ipip_sub_tunnel->next_in_bucket = 
            app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].chain;

    app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].chain = ipip_sub_tunnel;

    app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].count++;

    /* Release the lock */
    jnx_gw_data_release_lock(&app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].bucket_lock);

    return ipip_sub_tunnel;
}

int
jnx_gw_data_db_del_gre_tunnel(jnx_gw_data_cb_t*           app_cb,
                              jnx_gw_data_gre_tunnel_t*   gre_tunnel)
{
    uint32_t                   hash_val = 0;
    jnx_gw_data_gre_tunnel_t*  tmp_prev = NULL;
    jnx_gw_data_gre_tunnel_t*  tmp = NULL;

    /* Compute the hash value from the GRE Tunnel key */
    hash_val = jnx_gw_data_compute_gre_hash((jnx_gw_gre_key_hash_t*)&gre_tunnel->key);

    /* Acquire the bucket lock */
    jnx_gw_data_acquire_lock(&app_cb->gre_db.hash_bucket[hash_val].bucket_lock);

    for(tmp = app_cb->gre_db.hash_bucket[hash_val].chain;
        tmp != gre_tunnel;
        tmp = tmp->next_in_bucket) {

        tmp_prev = tmp;
    }

    if(tmp_prev == NULL) {
        
        app_cb->gre_db.hash_bucket[hash_val].chain = tmp->next_in_bucket;

    }else {

        tmp_prev->next_in_bucket = tmp->next_in_bucket;
    }

    app_cb->gre_db.hash_bucket[hash_val].count-- ;
    
    /* Release the bucket lock */
    jnx_gw_data_release_lock(&app_cb->gre_db.hash_bucket[hash_val].bucket_lock);

    return 0;
}

int
jnx_gw_data_db_del_ipip_tunnel(jnx_gw_data_cb_t*            app_cb,
                              jnx_gw_data_ipip_tunnel_t*    ipip_tunnel)
{
    uint32_t                    hash_val = 0;
    jnx_gw_data_ipip_tunnel_t*  tmp_prev = NULL;
    jnx_gw_data_ipip_tunnel_t*  tmp = NULL;

    /* Compute the hash value from the IPIP Tunnel key */
    hash_val = jnx_gw_data_compute_ipip_tunnel_hash(
                            (jnx_gw_ipip_tunnel_key_hash_t*)&ipip_tunnel->key);

    for(tmp = app_cb->ipip_tunnel_db.hash_bucket[hash_val].chain;
        tmp != ipip_tunnel;
        tmp = tmp->next_in_bucket) {

        tmp_prev = tmp;
    }

    /* Acquire the bucket lock */
    jnx_gw_data_acquire_lock(&app_cb->ipip_tunnel_db.hash_bucket[hash_val].bucket_lock);

    if(tmp_prev == NULL) {
        
        app_cb->ipip_tunnel_db.hash_bucket[hash_val].chain = tmp->next_in_bucket;

    }else {

        tmp_prev->next_in_bucket = tmp->next_in_bucket;
    }

    app_cb->ipip_tunnel_db.hash_bucket[hash_val].count--;
    
    /* Release the bucket lock */
    jnx_gw_data_release_lock(&app_cb->ipip_tunnel_db.hash_bucket[hash_val].bucket_lock);

    return 0;
}

jnx_gw_data_vrf_stat_t*  
jnx_gw_data_db_add_vrf_entry(jnx_gw_data_cb_t*      app_cb,
                             uint32_t               vrf)
{
    uint32_t                 hash_val = 0;
    jnx_gw_data_vrf_stat_t*  vrf_entry = NULL;

    /* Compute the hash Value from the VRF */
    hash_val = jnx_gw_data_compute_vrf_hash(vrf);

    /* Acquire a lock on the VRF DB */
    jnx_gw_data_acquire_lock(&app_cb->vrf_db.hash_bucket[hash_val].bucket_lock);

    /* Check if the entry already exists in the DB */
    for(vrf_entry = app_cb->vrf_db.hash_bucket[hash_val].chain;
        vrf_entry != NULL;
        vrf_entry = vrf_entry->next_in_bucket) {

        if(memcmp(&vrf_entry->key, &vrf, sizeof(uint32_t)) == 0)
            break;
    }

    if(vrf_entry == NULL) {

        /*We need to add the vrf entry into the db now */
        
        /* Allocate an entry now */
        if((vrf_entry = JNX_GW_MALLOC(JNX_GW_DATA_ID,
                              sizeof(jnx_gw_data_vrf_stat_t))) == NULL) {
            
           jnx_gw_data_release_lock(&app_cb->vrf_db.hash_bucket[hash_val].bucket_lock); 
           return NULL;
        }

        memset(vrf_entry, 0, sizeof(jnx_gw_data_vrf_stat_t));

        vrf_entry->key = vrf;

        vrf_entry->state = JNX_GW_DATA_ENTRY_STATE_READY;

        /* Add the entry in the DB */
        vrf_entry->next_in_bucket = 
                app_cb->vrf_db.hash_bucket[hash_val].chain;

        app_cb->vrf_db.hash_bucket[hash_val].chain = vrf_entry;

        app_cb->vrf_db.hash_bucket[hash_val].count++;
    }
    
    jnx_gw_data_release_lock(&app_cb->vrf_db.hash_bucket[hash_val].bucket_lock); 
    
    return vrf_entry;
}

int
jnx_gw_data_db_del_ipip_sub_tunnel(jnx_gw_data_cb_t*               app_cb,
                                   jnx_gw_data_ipip_sub_tunnel_t*  ipip_sub_tunnel)
{
    uint32_t                        hash_val = 0;
    jnx_gw_data_ipip_sub_tunnel_t*  tmp_prev = NULL;
    jnx_gw_data_ipip_sub_tunnel_t*  tmp = NULL;

    /* Compute the hash value from the IP-IP Sub Tunnel key */
    hash_val = jnx_gw_data_compute_ipip_sub_tunnel_hash(
                (jnx_gw_data_ipip_sub_tunnel_key_hash_t*)&ipip_sub_tunnel->key);

    for(tmp = app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].chain;
        tmp != ipip_sub_tunnel;
        tmp = tmp->next_in_bucket) {

        tmp_prev = tmp;
    }

    /* Acquire the bucket lock */
    jnx_gw_data_acquire_lock(&app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].
                        bucket_lock);

    if(tmp_prev == NULL) {
        
        app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].chain = 
                                               tmp->next_in_bucket;

    }else {

        tmp_prev->next_in_bucket = tmp->next_in_bucket;
    }

    app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].count-- ;
    
    /* Release the bucket lock */
    jnx_gw_data_release_lock(&app_cb->ipip_sub_tunnel_db.hash_bucket[
                        hash_val].bucket_lock);

    return 0;
}

uint32_t
jnx_gw_data_compute_gre_hash(jnx_gw_gre_key_hash_t*  key)
{
    uint32_t   hash_val = 0;

    hash_val = JNX_GW_DATA_HASH_MAGIC_NUMBER;
    hash_val = ((hash_val << 5) + hash_val) ^ key->val[0];
    hash_val = ((hash_val << 5) + hash_val) ^ key->val[1];

    hash_val = hash_val & JNX_GW_DATA_GRE_TUNNEL_HASH_MASK;

    return hash_val;
}

uint32_t
jnx_gw_data_compute_ipip_tunnel_hash(jnx_gw_ipip_tunnel_key_hash_t* key)
{
    uint32_t   hash_val = 0;

    hash_val = JNX_GW_DATA_HASH_MAGIC_NUMBER;
    hash_val = ((hash_val << 5) + hash_val) ^ key->val[0];
    hash_val = ((hash_val << 5) + hash_val) ^ key->val[1];

    hash_val = hash_val & JNX_GW_DATA_IPIP_TUNNEL_HASH_MASK;

    return hash_val;
}

uint32_t
jnx_gw_data_compute_ipip_sub_tunnel_hash(jnx_gw_data_ipip_sub_tunnel_key_hash_t* key)
{
    uint32_t   hash_val = 0;

    hash_val = JNX_GW_DATA_HASH_MAGIC_NUMBER;
    hash_val = ((hash_val << 5) + hash_val) ^ key->val[0];
    hash_val = ((hash_val << 5) + hash_val) ^ key->val[1];
    hash_val = ((hash_val << 5) + hash_val) ^ key->val[2];
    hash_val = ((hash_val << 5) + hash_val) ^ key->val[3];

    hash_val = hash_val & JNX_GW_DATA_GRE_TUNNEL_HASH_MASK;

    return hash_val;
}

uint32_t
jnx_gw_data_compute_vrf_hash(jnx_gw_vrf_key_t vrf)
{
    uint32_t   hash_val = 0;

    hash_val = JNX_GW_DATA_HASH_MAGIC_NUMBER;
    hash_val = ((hash_val << 5) + hash_val) ^ vrf;

    hash_val = hash_val & JNX_GW_DATA_VRF_HASH_MASK;

    return hash_val;
}

int 
jnx_gw_data_db_remove_gre_tunnel_from_vrf(jnx_gw_data_cb_t*          app_cb,
                                          jnx_gw_data_gre_tunnel_t*  gre_tunnel) 
{
    jnx_gw_data_vrf_stat_t*     vrf_entry;
    jnx_gw_data_gre_tunnel_t*   tmp = NULL;
    jnx_gw_data_gre_tunnel_t*   tmp_prev = NULL;

    vrf_entry = gre_tunnel->ing_vrf_stat;

    if(app_cb == NULL)
        return -1;

    /* Search the GRE Tunnel in this VRF */
    tmp = (jnx_gw_data_gre_tunnel_t*)((char*)(vrf_entry->next_gre_tunnel) - 
                           offsetof(jnx_gw_data_gre_tunnel_t, next_in_vrf));

    while (tmp && (tmp != gre_tunnel)) {

        tmp_prev = tmp;

        tmp = tmp->next_in_vrf;

        tmp = (jnx_gw_data_gre_tunnel_t*)((char*)tmp  - 
                            offsetof(jnx_gw_data_gre_tunnel_t, next_in_vrf));
    }

    if(tmp_prev == NULL) {

        vrf_entry->next_gre_tunnel = gre_tunnel->next_in_vrf;
    }
    else {

        tmp_prev->next_in_vrf = gre_tunnel->next_in_vrf;
    }

    vrf_entry->vrf_stats.active_sessions--;

    return 0;
}

int 
jnx_gw_data_db_remove_ipip_tunnel_from_vrf(jnx_gw_data_cb_t*           app_cb,
                                           jnx_gw_data_ipip_tunnel_t*  ipip_tunnel) 
{
    jnx_gw_data_vrf_stat_t*     vrf_entry;
    jnx_gw_data_ipip_tunnel_t*   tmp = NULL;
    jnx_gw_data_ipip_tunnel_t*   tmp_prev = NULL;

    if(app_cb == NULL)
        return -1;

    vrf_entry = ipip_tunnel->ing_vrf;

    tmp = (jnx_gw_data_ipip_tunnel_t*)((char*)(vrf_entry->next_ipip_tunnel) - 
                           offsetof(jnx_gw_data_ipip_tunnel_t, next_in_vrf));

    /* Search the IP-IP Tunnel in this VRF */
    while(tmp && (tmp != ipip_tunnel)) {

        tmp_prev = tmp;

        tmp = tmp->next_in_vrf;

        tmp = (jnx_gw_data_ipip_tunnel_t*)((char*)tmp - 
                           offsetof(jnx_gw_data_ipip_tunnel_t, next_in_vrf));
        
    }

    if(tmp_prev == NULL) {

        vrf_entry->next_ipip_tunnel = ipip_tunnel->next_in_vrf;
    }
    else {

        tmp_prev->next_in_vrf = ipip_tunnel->next_in_vrf;
    }

    return 0;
}

jnx_gw_data_ipip_tunnel_t*  
jnx_gw_data_db_add_ipip_tunnel(jnx_gw_data_cb_t*                  app_cb,
                               jnx_gw_ipip_tunnel_key_hash_t*     key)
{
    jnx_gw_data_ipip_tunnel_t*  ipip_tunnel = NULL;
    uint32_t                    hash_val = 0;

    /* Compute the Hash Value from the key */
    hash_val = jnx_gw_data_compute_ipip_tunnel_hash(key);

    /*Allocate an entry for the IP-IP Tunnel */
    if((ipip_tunnel = JNX_GW_MALLOC(JNX_GW_DATA_ID,
                      sizeof(jnx_gw_data_ipip_tunnel_t))) == NULL) {
       return NULL;
    }

    memset(ipip_tunnel, 0, sizeof(jnx_gw_data_ipip_tunnel_t));

    /*Copy the key in the allocated entry */
    ipip_tunnel->key = key->key;

    /* Init the lock for this entry */
    if(jnx_gw_data_lock_init(&ipip_tunnel->lock) != EOK) {

        JNX_GW_FREE(JNX_GW_DATA_ID, ipip_tunnel);
        return NULL;
    }

    /*
     * Mark the state as INIT, the entry should not be used till
     * the entry gets fully populated. Hence, state of the entry
     * will be marked as INIT so that data packets if at all arrives 
     * will not use it.
     */
    ipip_tunnel->tunnel_state = JNX_GW_DATA_ENTRY_STATE_INIT;

    /* Now add this entry in the IP-IP Tunnel DB */

    /* Acquire a lock on the bucket */
    jnx_gw_data_acquire_lock( &app_cb->ipip_tunnel_db.hash_bucket[hash_val].bucket_lock);

    ipip_tunnel->next_in_bucket = 
            app_cb->ipip_tunnel_db.hash_bucket[hash_val].chain;

    app_cb->ipip_tunnel_db.hash_bucket[hash_val].chain = ipip_tunnel;

    app_cb->ipip_tunnel_db.hash_bucket[hash_val].count++;

    /* Release the lock */
    jnx_gw_data_release_lock(&app_cb->ipip_tunnel_db.hash_bucket[hash_val].bucket_lock);

    return ipip_tunnel;

}

jnx_gw_data_vrf_stat_t*  
jnx_gw_data_db_vrf_entry_lookup(jnx_gw_data_cb_t*    app_cb,
                                jnx_gw_vrf_key_t     vrf)
{
    
    jnx_gw_data_vrf_stat_t*   tmp = NULL;
    uint32_t                  hash_val = 0;

    /* compute the Hash Value from the GRE KEY */
    hash_val = jnx_gw_data_compute_vrf_hash(vrf);

    /*
     * Perform a linear search in the bucket to see if there is
     * an entry present with the key passed.
     */
    for(tmp = app_cb->vrf_db.hash_bucket[hash_val].chain;
        tmp != NULL;
        tmp = tmp->next_in_bucket)
    {
        if((memcmp(&vrf, &tmp->key, sizeof(jnx_gw_vrf_key_t))) == 0) {

            break;
        }
    }

    return tmp;
}

jnx_gw_data_ipip_sub_tunnel_t*  
jnx_gw_data_db_ipip_sub_tunnel_lookup_without_lock(jnx_gw_data_cb_t*  app_cb,
                             jnx_gw_data_ipip_sub_tunnel_key_hash_t* key)
{
    jnx_gw_data_ipip_sub_tunnel_t* tmp = NULL;
    uint32_t                       hash_val = 0;


    /* Compute the hash value from the key */
    hash_val = jnx_gw_data_compute_ipip_sub_tunnel_hash(key);

    for(tmp = app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].chain;
        tmp != NULL;
        tmp = tmp->next_in_bucket)
    {
        if((memcmp(key, &tmp->key, 
                   sizeof(jnx_gw_data_ipip_sub_tunnel_key_t))) == 0) {

            break;
        }
    }

    return tmp;
}

jnx_gw_data_ipip_sub_tunnel_t*  
jnx_gw_data_db_ipip_sub_tunnel_lookup_with_lock(jnx_gw_data_cb_t*  app_cb,
                          jnx_gw_data_ipip_sub_tunnel_key_hash_t* key)
{
    jnx_gw_data_ipip_sub_tunnel_t* tmp = NULL;
    uint32_t                       hash_val = 0;


    /* Compute the hash value from the key */
    hash_val = jnx_gw_data_compute_ipip_sub_tunnel_hash(key);

    /* Acquire a lock on the bucket */
    jnx_gw_data_acquire_lock( &app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].bucket_lock);

    for(tmp = app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].chain;
        tmp != NULL;
        tmp = tmp->next_in_bucket)
    {
        if((memcmp(key, &tmp->key, sizeof(jnx_gw_data_ipip_sub_tunnel_key_t))) == 0) {

            break;
        }
    }
    jnx_gw_data_release_lock(&app_cb->ipip_sub_tunnel_db.hash_bucket[hash_val].bucket_lock);

    return tmp;
}


static int32_t
jnx_gw_data_compute_jbuf_checksum(void *val, void * buf, uint32_t len)
{
    char * sbuf = buf;
    uint32_t sum = *(uint32_t *)val;

    len = (len & 1) + (len >> 1);

    while (len > 0) {
        sum  += *((uint16_t *)sbuf);
        sbuf += sizeof(uint16_t);
        len--;
    }

    *(uint32_t *)val = sum;
    return EOK;
}

uint32_t
jnx_gw_data_compute_checksum(struct jbuf * jb, uint32_t offset,
                        uint32_t len)
{
    uint32_t sum = 0;

    jbuf_apply(jb, offset, len, jnx_gw_data_compute_jbuf_checksum, &sum);

    while (sum >> 16) {
        sum  = (sum & 0xFFFF) + (sum >> 16);
    }

    return (~sum);
}

void
jnx_gw_data_update_ttl_compute_inc_checksum(struct ip * ip_hdr)
{
    uint32_t sum = 0;
    uint16_t old_val = 0;

    old_val = ntohs(*(uint16_t *)&ip_hdr->ip_ttl);
    ip_hdr->ip_ttl--;

    sum = (old_val + (~ntohs(*(uint16_t *)&ip_hdr->ip_ttl) & 0xFFFF));

    sum += ntohs(ip_hdr->ip_sum);

    sum = ((sum & 0xFFFF) + (sum >> 16));

    ip_hdr->ip_sum = htons((sum & 0xFFFF) + (sum >> 16));

    return;
}

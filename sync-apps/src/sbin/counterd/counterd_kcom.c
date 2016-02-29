/*
 * $Id: counterd_kcom.c 366969 2010-03-09 15:30:13Z taoliu $
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
 * @file counterd_kcom.c
 * @brief Routines related to KCOM
 * 
 * 
 * Functions to initialize KCOM and to do GENCFG
 */
 
#include <string.h>
#include <isc/eventlib.h>
#include <jnx/bits.h>
#include <jnx/aux_types.h>
#include <jnx/provider_info.h>
#include <jnx/junos_kcom.h>
#include "counterd_kcom.h"
#include "counterd_logging.h"


/*** Constants ***/


#define COUNTERD_GENCFG_KEY 0xc01dface   ///< A key we pick unique to this app
#define COUNTERD_MINOR_NUM           2   ///< A minor number we picked


/*** Data Structures ***/


/*** STATIC/INTERNAL Functions ***/


/**
 * Bare minimum setup for a GENCFG callback function. It is required!
 *
 * @param[in] jk_gencfg
 *      gencfg data structure which needs to be free'd like after a GET
 * 
 * @return
 *      KCOM_OK (potential errors are ignored)
 */

static int
counterd_gencfg_async(junos_kcom_gencfg_t * jk_gencfg)
{
    ERRMSG(COUNTERD, TRACE_LOG_DEBUG,
        "%s - Called and ignored.", __func__);

    jk_gencfg->opcode = JUNOS_KCOM_GENCFG_OPCODE_GET_FREE;
    jk_gencfg->get_p = NULL;
    junos_kcom_gencfg(jk_gencfg);
    
    junos_kcom_msg_free(jk_gencfg);
    
    return KCOM_OK;
}


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Init KCOM library so we can use it
 *
 * @param[in] ctx
 *      event context for app
 * 
 * @return
 *      KCOM_OK (0) on success, or a KCOM error on failure
 */
int
counterd_kcom_init(evContext ctx)
{
    int rc;
    junos_kcom_gencfg_t jk_gencfg;
    provider_origin_id_t origin_id;

    rc = provider_info_get_origin_id(&origin_id);
    
    if (rc) {
        ERRMSG(COUNTERD, TRACE_LOG_ERR,
            "%s: Retrieving origin ID failed: %m", __func__);
        return rc;
    }
    
    /*
     * Some KCOM required initialization
     */
    rc = junos_kcom_init(origin_id, ctx);
    
    if(rc) {
        ERRMSG(COUNTERD, TRACE_LOG_ERR,
            "%s - Failed to initialize KCOM", __func__);
        return rc;
    }

    /*
     * Some GENCFG initialization
     */
    bzero(&jk_gencfg, sizeof(junos_kcom_gencfg_t));
    jk_gencfg.opcode        = JUNOS_KCOM_GENCFG_OPCODE_INIT;
    jk_gencfg.user_info_p   = NULL;
    jk_gencfg.user_handler  = counterd_gencfg_async;

    rc = junos_kcom_gencfg(&jk_gencfg);
    
    if(rc) {
        ERRMSG(COUNTERD, TRACE_LOG_ERR,
            "%s - Failed to initialize KCOM GENCFG", __func__);
        return rc;
    }

    /*
     * Add a filter to only be notified about our blobs we add
     * (cut down the notifications)
     */

    bzero(&jk_gencfg, sizeof(junos_kcom_gencfg_t));
    jk_gencfg.opcode        = JUNOS_KCOM_GENCFG_OPCODE_FILTER_ADD;
    jk_gencfg.blob_id       = COUNTERD_MINOR_NUM;
    
    rc = junos_kcom_gencfg(&jk_gencfg);
    
    if(rc) {
        ERRMSG(COUNTERD, TRACE_LOG_ERR,
            "%s - Failed to add KCOM GENCFG filter", __func__);
        return rc;    
    }

    return KCOM_OK;
}


/**
 * Add or replace some data in KCOM GENCFG
 *
 * @param[in] data
 *      data to add
 * 
 * @param[in] data_len
 *      length in bytes of data
 * 
 * @return
 *      KCOM_OK (0) on success, or a KCOM error on failure
 */
int
counterd_add_data(void * data, uint32_t data_len)
{
    junos_kcom_gencfg_t jk_gencfg;
    uint32_t user_key = COUNTERD_GENCFG_KEY; // key for data
    int rc;

    INSIST(data != NULL);
    INSIST(data_len > 0);
    
    /*
     * Get a blob of data
     */
    bzero(&jk_gencfg, sizeof(junos_kcom_gencfg_t));
    jk_gencfg.opcode        = JUNOS_KCOM_GENCFG_OPCODE_BLOB_GET;
    jk_gencfg.blob_id       = COUNTERD_MINOR_NUM;
    jk_gencfg.key.size      = sizeof(uint32_t);
    jk_gencfg.key.data_p    = (void *) &user_key;
    
    rc = junos_kcom_gencfg(&jk_gencfg);
    
    if(!rc) {

        jk_gencfg.opcode = JUNOS_KCOM_GENCFG_OPCODE_GET_FREE;
        rc = junos_kcom_gencfg(&jk_gencfg);
        
        if(rc) {
            ERRMSG(COUNTERD, TRACE_LOG_ERR,
                "%s: Failed to free data from KCOM GENCFG blob get",
                __func__);
            return rc;
        }

        /*
         * It already exists so delete it first
         */
        bzero(&jk_gencfg, sizeof(junos_kcom_gencfg_t));
        jk_gencfg.opcode        = JUNOS_KCOM_GENCFG_OPCODE_BLOB_DEL;
        jk_gencfg.blob_id       = COUNTERD_MINOR_NUM;
        jk_gencfg.key.size      = sizeof(uint32_t);
        jk_gencfg.key.data_p    = (void *) &user_key;
        
        rc = junos_kcom_gencfg(&jk_gencfg);
        
        if(rc) {
            ERRMSG(COUNTERD, TRACE_LOG_ERR,
                "%s: Failed to delete existing blob while replacing data",
                __func__);
            return rc;
        }
    }

    /*
     * Add a blob with data
     */
    bzero(&jk_gencfg, sizeof(junos_kcom_gencfg_t));
    jk_gencfg.opcode        = JUNOS_KCOM_GENCFG_OPCODE_BLOB_ADD;
    jk_gencfg.blob_id       = COUNTERD_MINOR_NUM;
    jk_gencfg.key.size      = sizeof(uint32_t);
    jk_gencfg.key.data_p    = (void *) &user_key;
    jk_gencfg.blob.size     = data_len;
    jk_gencfg.blob.data_p   = data;
    
    rc = junos_kcom_gencfg(&jk_gencfg);
    
    if(rc) {
        ERRMSG(COUNTERD, TRACE_LOG_ERR,
            "%s: Failed to add new blob", __func__);
    }
    
    return rc;
}


/**
 * Get some data from KCOM GENCFG
 *
 * @param[in] data
 *      pointer of where to copy the data
 * 
 * @param[in] data_len
 *      max number of bytes to copy to into data
 * 
 * @return
 *      number of bytes of data read or -1 on error
 */
int
counterd_get_data(void * data, uint32_t data_len)
{

    junos_kcom_gencfg_t jk_gencfg;
    uint32_t user_key = COUNTERD_GENCFG_KEY;
    int rc;

    INSIST(data != NULL);
    INSIST(data_len > 0);
    
    /*
     * Get a blob of data
     */
    bzero(&jk_gencfg, sizeof(junos_kcom_gencfg_t));
    jk_gencfg.opcode        = JUNOS_KCOM_GENCFG_OPCODE_BLOB_GET;
    jk_gencfg.blob_id       = COUNTERD_MINOR_NUM;
    jk_gencfg.key.size      = sizeof(uint32_t);
    jk_gencfg.key.data_p    = (void *) &user_key;
    
    rc = junos_kcom_gencfg(&jk_gencfg);
    
    if (rc) {
        ERRMSG(COUNTERD, TRACE_LOG_DEBUG,
            "%s: Call to get data from KCOM returned nothing", __func__);
        return -1;
    } else {
        jk_gencfg.opcode = JUNOS_KCOM_GENCFG_OPCODE_GET_FREE;
        rc = junos_kcom_gencfg(&jk_gencfg);
        
        if(rc) {
            ERRMSG(COUNTERD, TRACE_LOG_ERR,
                "%s: Failed to free data from KCOM GENCFG blob get",
                __func__);
            return rc;
        }
    }
    
    /*
     * Copy retrieved data
     */
    if(jk_gencfg.blob.size > data_len) {
        memcpy(data, jk_gencfg.blob.data_p, data_len);
        return data_len;
    }
    
    if(jk_gencfg.blob.size > 0) {
        memcpy(data, jk_gencfg.blob.data_p, jk_gencfg.blob.size);
        return jk_gencfg.blob.size;
    }
    
    return 0;
}


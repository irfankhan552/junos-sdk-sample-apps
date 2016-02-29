/*
 * $Id: equilibrium2_svc.h 418048 2010-12-30 18:58:42Z builder $
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
 * @file equilibrium2_svc.h
 * 
 * @brief Contains function
 * 
 */

#ifndef __EQUILIBRIUM2_SVC_H__
#define __EQUILIBRIUM2_SVC_H__

#include <sys/types.h>
#include <sys/queue.h>
#include <jnx/trace.h>

/** The event class name of EQ2 classify service. */
#define EV_CLASS_CLASSIFY "ev-class-classify"

#define EV_CLASSIFY_FIRST_PACKET    1  /**< event of first packet */

#define STATUS_UPDATE_INTERVAL      2  /**< service status update interval */

/** Define an INSIST/assert with logging. */
#ifdef INSIST_ERR
#undef INSIST_ERR
#endif

#define INSIST_ERR(c) if (!(c)) \
    msp_log(LOG_EMERG, "%s:%d: insist '%s' ERROR: %m", \
        __FILE__, __LINE__, #c); else (void)NULL

#define EQ2_TRACE(_msg_type, _fmt...) \
    msp_log(LOG_INFO, _fmt)

/** Manager connect state. */
typedef enum connect_state_e {
    CONNECT_NA,                  /**< no connection */
    CONNECT_OK,                  /**< connected */
    CONNECT_INPROGRESS           /**< connect in progress */
} connect_state_t;

/**
 * The data structure of service-set in service plugin.
 */
typedef struct sp_svc_set_s {
    LIST_ENTRY(sp_svc_set_s) entry;        /**< list entry*/
    blob_svc_set_t           *ss_policy;   /**< pointer to the policy */
    int                      ss_ssn_count;
                                /**< number of sessions of this service-set */
    char                     ss_active;    /**< state of the service-set */
} sp_svc_set_t;

typedef LIST_HEAD(sp_svc_set_head_s, sp_svc_set_s) sp_svc_set_head_t;

/* Dummy number for demo purpose. */
#define EQ2_LUCKY_NUM    666

/** Public data of classify service. */
enum classify_public_data_id_e {
    EQ2_CLASSIFY_PUB_DATA_LUCKY_NUM,
    EQ2_CLASSIFY_PUB_DATA_LUCKY_STR
} classify_public_data_id_t;

#endif /* __EQUILIBRIUM2_SVC_H__ */


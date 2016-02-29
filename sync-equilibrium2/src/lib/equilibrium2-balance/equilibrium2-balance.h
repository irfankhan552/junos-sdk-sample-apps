/*
 * $Id: equilibrium2-balance.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file equilibrium2-balance.h
 * @brief Related to the Equilibrium II balance service plugin.
 *
 */

#ifndef __EQUILIBRIUM2_BALANCE_H__
#define __EQUILIBRIUM2_BALANCE_H__

#include <stdlib.h>
#include <stdbool.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/queue.h>
#include <isc/eventlib.h>
#include <jnx/aux_types.h>
#include <jnx/atomic.h>
#include <jnx/msp_locks.h>
#include <jnx/msp_policy_db.h>
#include <jnx/multi-svcs/msvcs_events.h>

/** List item of session action for forward path. */
typedef struct ssn_f_action_s {
    LIST_ENTRY(ssn_f_action_s) entry;      /**< list entry */
    svr_addr_t                 *svr_addr;  /**< pointer to the address */
} ssn_f_action_t;

/** List item of session action for reverse path. */
typedef struct ssn_r_action_s {
    LIST_ENTRY(ssn_r_action_s) entry;  /**< list entry */
    in_addr_t                  addr;   /**< address */
} ssn_r_action_t;

int                     balance_pid;      /**< plugin ID */
msvcs_control_context_t *ctrl_ctx;        /**< global copy of control context */
sp_svc_set_head_t       svc_set_head;     /**< head of service-set list */
msp_spinlock_t          svc_set_lock;     /**< lock for service-set list */
svr_group_head_t        *svr_group_head;  /**< head of server group list */
msp_spinlock_t          svr_group_lock;   /**< lock for server group list */
uint16_t                svr_group_count;  /**< number of server groups */
msvcs_event_class_t     classify_ev_class;
                        /**< event class of classify service */

/**
 * @brief
 * Data event handler for the plugins.
 *
 * @param[in] ctx
 *     Pointer to data context.
 *
 * @param[in] ev
 *     Data event.
 *
 * @return Data handler status code
 */
int equilibrium2_balance_data_hdlr(msvcs_data_context_t *ctx,
        msvcs_data_event_t ev);

/**
 * @brief
 * Control event handler.
 *
 * @param[in] ctx
 *     Pointer to control context.
 *
 * @param[in] ev
 *     control event.
 *
 * @return Control handler status code
 */
int equilibrium2_balance_ctrl_hdlr(msvcs_control_context_t *ctx,
        msvcs_control_event_t ev);

/**
 * @brief
 * Get service-set by service-set ID.
 *
 * There could be maximumly two service-sets with the same ID,
 * one is to be deleted, another is active.
 *
 * @param[in] id
 *      Service-set ID
 *
 * @param[in] active
 *      Activation flag, 'true' is active, 'false' is inactive
 *
 * @return
 *      Pointer to the service-set on success, NULL on failure
 */
sp_svc_set_t *get_svc_set(uint16_t id, bool active);

/**
 * @brief
 * Delete a service-set by pointer.
 *
 * @param[in] ss
 *      Pointer to service-set
 */
void del_svc_set(sp_svc_set_t *ss);

#endif /* __EQUILIBRIUM2_BALANCE_H__ */


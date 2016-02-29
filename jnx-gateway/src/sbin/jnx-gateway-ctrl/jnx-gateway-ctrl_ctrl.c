/*
 * $Id: jnx-gateway-ctrl_ctrl.c 437848 2011-04-21 07:35:27Z amudha $
 *
 * jnx-gateway-ctrl_ctrl.c - configuration db management functions
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
 * @file jnx-gateway-ctrl_ctrl.c
 * @brief
 * This file contains the configurationdatabase manaement
 * routines, also, it contains the routines for gre session
 * creation and deletion routines
 * The configurations items are maintained as follows,
 *   1. control policy - maintained per vrf
 *   2. user policy - maintained globally
 *   3. data policy - maintained globally
 *
 * Following are the major data structures,
 * The main control block, for the control agent 
 * contains
 *   1. the data pic table &
 *   2. vrf table,
 *
 * The data pic table entry contains,
 *   1. the interfaces table attached with it.
 *   2. the session management fields for the data pic
 *
 * The vrf table entry contains,
 *  1. the gre gateway table
 *  2. the ipip gateway table
 *  3. the control policy table
 *  4. the interfaces in the vrf
 *
 * The control policy table entry contains,
 *  1. the list of interfaces using this 
 *     control policy
 *
 * The user profile table entry contains,
 *  1. the ipip gateway entry
 *  2. gre sessions table using this user profile
 *
 * The gre gateway table entry contains,
 *  1. gre sessions table for this gre gateway
 *
 * The ipip gateway table entry contains,
 *  1. gre sessions table using this ipip gateway
 *
 */

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include <jnx/pconn.h>
#include <jnx/atomic.h>

/* include the comman header files for
   the sample gateway application */
#include <jnx/jnx-gateway.h>
#include <jnx/jnx-gateway_msg.h>

/* include control agent header file */
#include "jnx-gateway-ctrl.h"

/***********************************************************
 *                                                         *
 *        LOCAL FUNCTION PROTOTYPE DEFINITIONS             *
 *                                                         *
 ***********************************************************/
jnx_gw_ctrl_data_pic_t *
jnx_gw_ctrl_get_next_data_pic(jnx_gw_ctrl_data_pic_t
                              * pdata_pic);

void
jnx_gw_ctrl_clear_vrf(jnx_gw_ctrl_vrf_t * pvrf);

jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_get_first_vrf(void);

void
jnx_gw_ctrl_clear_user(jnx_gw_ctrl_vrf_t * pvrf,
                       jnx_gw_ctrl_user_t * puser);

jnx_gw_ctrl_user_t *
jnx_gw_ctrl_get_first_user(void);

void 
jnx_gw_ctrl_clear_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                       jnx_gw_ctrl_policy_t * pctrl);

jnx_gw_ctrl_policy_t *
jnx_gw_ctrl_get_next_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                          jnx_gw_ctrl_policy_t * pctrl);

jnx_gw_ctrl_policy_t *
jnx_gw_ctrl_get_first_ctrl(jnx_gw_ctrl_vrf_t * pvrf);

void 
jnx_gw_ctrl_clear_data_pic(jnx_gw_ctrl_data_pic_t * pdata_pic);

jnx_gw_ctrl_data_pic_t *
jnx_gw_ctrl_get_first_data_pic(void);

jnx_gw_ctrl_intf_t *
jnx_gw_ctrl_get_first_intf(jnx_gw_ctrl_vrf_t * pvrf,
                           jnx_gw_ctrl_data_pic_t * pdata_pic,
                           jnx_gw_ctrl_policy_t * pctrl);

jnx_gw_ctrl_ipip_gw_t * 
jnx_gw_ctrl_get_first_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf);

void
jnx_gw_ctrl_clear_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf,
                          jnx_gw_ctrl_ipip_gw_t * pipip_gw);

void 
jnx_gw_ctrl_clear_intf(jnx_gw_ctrl_vrf_t * pvrf,
                       jnx_gw_ctrl_data_pic_t * pdata_pic,
                       jnx_gw_ctrl_intf_t * pintf);

jnx_gw_ctrl_gre_gw_t * 
jnx_gw_ctrl_get_first_gre_gw(jnx_gw_ctrl_vrf_t * pvrf);

void
jnx_gw_ctrl_clear_gre_gw(jnx_gw_ctrl_vrf_t * pvrf,
                         jnx_gw_ctrl_gre_gw_t * pgre_gw);

jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_get_first_gre_session(jnx_gw_ctrl_vrf_t * pvrf,
                               jnx_gw_ctrl_gre_gw_t * pgre_gw,
                               jnx_gw_ctrl_ipip_gw_t * pipip_gw,
                               jnx_gw_ctrl_user_t   * puser,
                               uint32_t lock);

void
jnx_gw_ctrl_clear_gre_session(jnx_gw_ctrl_gre_session_t * pgre_session);

status_t
jnx_gw_ctrl_add_nexthop(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_intf_t * pintf);

status_t
jnx_gw_ctrl_delete_nexthop(jnx_gw_ctrl_vrf_t * pvrf __unused,
                           jnx_gw_ctrl_intf_t * pintf);

jnx_gw_ctrl_route_t * 
jnx_gw_ctrl_route_get_first(jnx_gw_ctrl_vrf_t * pvrf __unused,
                            jnx_gw_ctrl_intf_t * pintf);

jnx_gw_ctrl_route_t * 
jnx_gw_ctrl_route_get_next(jnx_gw_ctrl_vrf_t * pvrf __unused,
                            jnx_gw_ctrl_intf_t * pintf,
                            jnx_gw_ctrl_route_t * proute);
jnx_gw_ctrl_route_t * 
jnx_gw_ctrl_route_lookup(jnx_gw_ctrl_vrf_t * pvrf __unused,
                         jnx_gw_ctrl_intf_t * pintf,
                         uint32_t key);
jnx_gw_ctrl_route_t *
jnx_gw_ctrl_add_route(jnx_gw_ctrl_vrf_t * pvrf,
                      jnx_gw_ctrl_intf_t * pintf,
                      jnx_gw_ctrl_route_t *proute,
                      uint32_t dest_ip, uint32_t rt_flags);
status_t
jnx_gw_ctrl_delete_route(jnx_gw_ctrl_vrf_t * pvrf __unused,
                         jnx_gw_ctrl_intf_t * pintf, 
                         jnx_gw_ctrl_route_t * proute);

static void 
jnx_gw_ctrl_get_vrf_rt_table_id(jnx_gw_ctrl_vrf_t * pvrf);

/* patnode to base pointer conversion routines for
   the data structures in tables */

/* GRE session structure */

PATNODE_TO_STRUCT(jnx_gw_ctrl_gre_session_entry,
                  jnx_gw_ctrl_gre_session_t, gre_sesn_node)

PATNODE_TO_STRUCT(jnx_gw_ctrl_gre_vrf_sesn_entry,
                  jnx_gw_ctrl_gre_session_t, gre_vrf_sesn_node)

PATNODE_TO_STRUCT(jnx_gw_ctrl_gre_ipip_sesn_entry,
                  jnx_gw_ctrl_gre_session_t, gre_ipip_sesn_node)

PATNODE_TO_STRUCT(jnx_gw_ctrl_gre_user_sesn_entry,
                  jnx_gw_ctrl_gre_session_t, gre_user_sesn_node)

/* GRE gateway structure */
PATNODE_TO_STRUCT(jnx_gw_ctrl_gre_gw_entry,
                  jnx_gw_ctrl_gre_gw_t, gre_gw_node)

/* IPIP gateway structure */
PATNODE_TO_STRUCT(jnx_gw_ctrl_ipip_gw_entry,
                  jnx_gw_ctrl_ipip_gw_t, ipip_gw_node)

/* User structure */
PATNODE_TO_STRUCT(jnx_gw_ctrl_user_entry,
                  jnx_gw_ctrl_user_t, user_node)

/* Control Policy structure */
PATNODE_TO_STRUCT(jnx_gw_ctrl_ctrl_entry,
                  jnx_gw_ctrl_policy_t, ctrl_node)

PATNODE_TO_STRUCT(jnx_gw_ctrl_vrf_ctrl_entry,
                  jnx_gw_ctrl_policy_t, ctrl_vrf_node)

/* Data PIC Interface Structure */
PATNODE_TO_STRUCT(jnx_gw_ctrl_vrf_intf_entry,
                  jnx_gw_ctrl_intf_t, intf_vrf_node) 

PATNODE_TO_STRUCT(jnx_gw_ctrl_pol_intf_entry,
                  jnx_gw_ctrl_intf_t, intf_ctrl_node) 

PATNODE_TO_STRUCT(jnx_gw_ctrl_intf_entry,
                  jnx_gw_ctrl_intf_t, intf_node) 

/* Data PIC structure */
PATNODE_TO_STRUCT(jnx_gw_ctrl_pic_entry,
                  jnx_gw_ctrl_data_pic_t, pic_node)

/* Route structure */
PATNODE_TO_STRUCT(jnx_gw_ctrl_route_entry,
                  jnx_gw_ctrl_route_t, route_node)

/* VRF structure */
PATNODE_TO_STRUCT(jnx_gw_ctrl_vrf_entry,
                  jnx_gw_ctrl_vrf_t, vrf_node)

PATNODE_TO_STRUCT(jnx_gw_ctrl_thread_vrf_entry,
                  jnx_gw_ctrl_vrf_t, vrf_tnode)


/***********************************************************
 *                                                         *
 *             GRE SESSION MANAGEMENT ROUTINES             *
 *                                                         *
 ***********************************************************/

/***********************************************************
 *                                                         *
 *             GRE SESSION LOOKUP ROUTINES                 *
 *                                                         *
 ***********************************************************/

/**
 * This function finds the gre session inside a vrf with
 * the gre key for a gateway client session
 * @param    pvrf     vrf structure pointer
 * @param    pgre_gw  gre gateway structure pointer
 * @param    gre_key  gre key 
 * @returns
 *    pgre_session    gre session pointer, with the same gre key
 *    NULL         otherwise
 */
jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_lookup_gre_session(jnx_gw_ctrl_vrf_t * pvrf,
                               jnx_gw_ctrl_gre_gw_t * pgre_gw __unused,
                               uint32_t gre_key, int lock)
{
    patnode *pnode;

    if (lock) {
        JNX_GW_CTRL_SESN_DB_READ_LOCK(pvrf);
    }
    if ((pnode = patricia_get(&pvrf->gre_sesn_db,
                              sizeof(gre_key), &gre_key))) {
        if (lock) {
            JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pvrf);
        }
        return jnx_gw_ctrl_gre_vrf_sesn_entry(pnode);
    }
    if (lock) {
        JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pvrf);
    }
    return NULL;
}

/**
 * This function finds the first gre session inside the vrf for the 
 * gre gateway or the ipip gateway, or the user
 * only one of the parameters is not null
 * @param    pvrf      vrf structure pointer
 * @param    pgre_gw   gre gateway structure pointer
 * @param    pipip_gw  ipip gateway structure pointer
 * @param    puser     user policy structure pointer
 * @returns
 *    pgre_session    gre session pointer
 *    NULL         otherwise
 */
jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_get_first_gre_session(jnx_gw_ctrl_vrf_t * pvrf,
                                  jnx_gw_ctrl_gre_gw_t * pgre_gw,
                                  jnx_gw_ctrl_ipip_gw_t * pipip_gw,
                                  jnx_gw_ctrl_user_t   * puser,
                                  uint32_t lock)
{
    patnode * p_node = NULL;

    if (pgre_gw) {

        JNX_GW_CTRL_SESN_DB_READ_LOCK(pgre_gw);

        if ((p_node = patricia_find_next(&pgre_gw->gre_sesn_db, NULL))) {

            JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pgre_gw);
            return jnx_gw_ctrl_gre_session_entry(p_node);
        }

        JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pgre_gw);

    }  else if (pipip_gw) {

        JNX_GW_CTRL_SESN_DB_READ_LOCK(pipip_gw);

        if ((p_node = patricia_find_next(&pipip_gw->gre_sesn_db, NULL))) {

            JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pipip_gw);

            return jnx_gw_ctrl_gre_ipip_sesn_entry(p_node);
        }

        JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pipip_gw);

    } else if (pvrf) {

        if (lock) {
           
            JNX_GW_CTRL_SESN_DB_READ_LOCK(pvrf);

            if ((p_node = patricia_find_next(&pvrf->gre_sesn_db, NULL))) {

                JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pvrf);

                return jnx_gw_ctrl_gre_vrf_sesn_entry(p_node);
            }

            JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pvrf);

        } else if ((p_node = patricia_find_next(&pvrf->gre_sesn_db, NULL))) {

            return jnx_gw_ctrl_gre_vrf_sesn_entry(p_node);
        }
    } else if (puser) {
        JNX_GW_CTRL_SESN_DB_READ_LOCK(puser);

        if ((p_node = patricia_find_next(&puser->gre_sesn_db, NULL))) {

            JNX_GW_CTRL_SESN_DB_READ_UNLOCK(puser);

            return jnx_gw_ctrl_gre_user_sesn_entry(p_node);
        }

        JNX_GW_CTRL_SESN_DB_READ_UNLOCK(puser);
    }
    return NULL;
}

/**
 * This function finds next gre session inside the vrf for the 
 * gre gateway or the ipip gateway, or the user, for the
 * current gre session entry
 * @param    pvrf      vrf structure pointer
 * @param    pgre_gw   gre gateway structure pointer
 * @param    pipip_gw  ipip gateway structure pointer
 * @param    puser     user policy structure pointer
 * @param    pgre_session gre session structure pointer
 * @returns
 *    pgre_session    gre session pointer
 *    NULL         otherwise
 */
jnx_gw_ctrl_gre_session_t *
jnx_gw_ctrl_get_next_gre_session(jnx_gw_ctrl_vrf_t * pvrf,
                                 jnx_gw_ctrl_gre_gw_t * pgre_gw,
                                 jnx_gw_ctrl_ipip_gw_t * pipip_gw,
                                 jnx_gw_ctrl_user_t   * puser,
                                 jnx_gw_ctrl_gre_session_t * pgre_session,
                                 uint32_t lock)
{
    patnode *pnode = NULL;

    if (pgre_session == NULL) {
        return jnx_gw_ctrl_get_first_gre_session(pvrf, pgre_gw,
                                                 pipip_gw, puser, lock);
    }

    if (pgre_gw)  {
        JNX_GW_CTRL_SESN_DB_READ_LOCK(pgre_gw);

        if ((pnode = patricia_find_next(&pgre_gw->gre_sesn_db,
                                        &pgre_session->gre_sesn_node))) {

            JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pgre_gw);

            return jnx_gw_ctrl_gre_session_entry(pnode);
        }

        JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pgre_gw);
    } else  if (pipip_gw) {

        JNX_GW_CTRL_SESN_DB_READ_LOCK(pipip_gw);

        if ((pnode = 
             patricia_find_next(&puser->gre_sesn_db,
                                &pgre_session->gre_ipip_sesn_node))) {

            JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pipip_gw);

            return jnx_gw_ctrl_gre_ipip_sesn_entry(pnode);
        }

        JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pipip_gw);

    } else if (pvrf) {

        if (lock) {

            JNX_GW_CTRL_SESN_DB_READ_LOCK(pvrf);

            if ((pnode = 
                 patricia_find_next(&pvrf->gre_sesn_db,
                                    &pgre_session->gre_vrf_sesn_node))) {

                JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pvrf);

                return jnx_gw_ctrl_gre_vrf_sesn_entry(pnode);
            }

            JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pvrf);

        } else if ((pnode =
                    patricia_find_next(&pvrf->gre_sesn_db,
                                       &pgre_session->gre_vrf_sesn_node))) {
            return jnx_gw_ctrl_gre_vrf_sesn_entry(pnode);
        }
    } else if (puser) {

        JNX_GW_CTRL_SESN_DB_READ_LOCK(puser);

        if ((pnode = 
             patricia_find_next(&puser->gre_sesn_db,
                                &pgre_session->gre_user_sesn_node))) {

            JNX_GW_CTRL_SESN_DB_READ_UNLOCK(puser);

            return jnx_gw_ctrl_gre_user_sesn_entry(pnode);
        }

        JNX_GW_CTRL_SESN_DB_READ_UNLOCK(puser);
    }
    return NULL;
}

/**
 * This function finds the gre session inside the gateway
 * for the session five tuple
 * @param    pvrf      vrf structure pointer
 * @param    pgre_gw   gre gateway structure pointer
 * @param    psesn     session 5 tuple structure pointer
 * @returns
 *    pgre_session    gre session pointer
 *    NULL            otherwise
 */
jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_get_gw_gre_session(jnx_gw_ctrl_vrf_t * pvrf __unused,
                               jnx_gw_ctrl_gre_gw_t * pgre_gw,
                               jnx_gw_ctrl_session_info_t * psesn)
{
    patnode * pnode = NULL;

    JNX_GW_CTRL_SESN_DB_READ_LOCK(pgre_gw);

    if ((pnode = patricia_get(&pgre_gw->gre_sesn_db,
                              sizeof(*psesn), psesn))) {

        JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pgre_gw);

        return jnx_gw_ctrl_gre_session_entry(pnode);
    }

    JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pgre_gw);
    return NULL;
}

/**
 * This function finds the gre session inside the ipip gateway
 * for the vrf and gre key
 * @param    pipip_gw   ipip gateway structure pointer
 * @param    psesn      gre session 5 tuple structure pointer
 * @returns
 *    pgre_session    gre session pointer
 *    NULL            otherwise
 */
jnx_gw_ctrl_gre_session_t *
jnx_gw_ctrl_get_ipip_gre_session(jnx_gw_ctrl_ipip_gw_t * pipip_gw,
                                 jnx_gw_ctrl_session_info_t * psesn)
{
    patnode * pnode = NULL;

    JNX_GW_CTRL_SESN_DB_READ_LOCK(pipip_gw);

    if ((pnode = patricia_get(&pipip_gw->gre_sesn_db,
                              sizeof(*psesn), psesn))) {

        JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pipip_gw);
        return jnx_gw_ctrl_gre_ipip_sesn_entry(pnode);
    }

    JNX_GW_CTRL_SESN_DB_READ_UNLOCK(pipip_gw);

    return NULL;
}


/**
 * This function finds the gre session inside a user policy with
 * the five tuple session entry
 * @param    pvrf     vrf structure pointer
 * @param    pgre_gw  gre gateway structure pointer
 * @param    gre_key  gre key 
 * @returns
 *    pgre_session    gre session pointer, with the same gre key
 *    NULL         otherwise
 */
jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_lookup_user_gre_session(jnx_gw_ctrl_user_t * puser,
                                    jnx_gw_ctrl_session_info_t * psesn)
{
    patnode *pnode;

    JNX_GW_CTRL_SESN_DB_READ_LOCK(puser);

    if ((pnode = patricia_get(&puser->gre_sesn_db,
                              sizeof(*psesn), psesn))) {

        JNX_GW_CTRL_SESN_DB_READ_UNLOCK(puser);

        return jnx_gw_ctrl_gre_user_sesn_entry(pnode);

    }
    JNX_GW_CTRL_SESN_DB_READ_UNLOCK(puser);
    return NULL;
}

/***********************************************************
 *                                                         *
 *             GRE SESSION ADD/DELETE ROUTINES             *
 *                                                         *
 ***********************************************************/
/**
 * This function add a gre session entry inside the vrf for the 
 * gre gateway with the current client session five tuple
 * current gre session entry
 * The pgre_config entry contains the resolved matching user 
 * profile, data pic, egress vrf, ipip gateway (if any), and
 * the self interface ip addresses to be used as local tunnel
 * end points on the data pic adds the gre session entry to
 * the gre gateway, ipip gateway, user profile session db.
 * @param    pvrf        vrf structure pointer
 * @param    pgre_gw     gre gateway structure pointer
 * @param    pgre_config gre session structure pointer
 * @returns
 *    pgre_session    gre session pointer
 *    NULL         otherwise
 */
jnx_gw_ctrl_gre_session_t * 
jnx_gw_ctrl_add_gre_session(jnx_gw_ctrl_vrf_t * pvrf __unused,
                            jnx_gw_ctrl_gre_gw_t * pgre_gw,
                            jnx_gw_ctrl_gre_session_t * pgre_msg)
{
    jnx_gw_ctrl_gre_session_t  *pgre_session;


    if ((pgre_session = JNX_GW_MALLOC(JNX_GW_CTRL_ID,
                                      sizeof(*pgre_session))) == NULL) {
        jnx_gw_log(LOG_ERR, "GRE Session setup malloc failed");
        goto gw_gre_session_cleanup;
    }

    /* fill ingress info */
    pgre_session->ingress_gre_key = pgre_msg->ingress_gre_key;
    pgre_session->ingress_vrf_id  = pgre_msg->ingress_vrf_id;
    pgre_session->ingress_intf_id = pgre_msg->ingress_intf_id;
    pgre_session->ingress_gw_ip   = pgre_msg->ingress_gw_ip;
    pgre_session->ingress_self_ip = pgre_msg->ingress_self_ip;

    /* fill egress info */
    pgre_session->egress_vrf_id   = pgre_msg->egress_vrf_id;
    pgre_session->egress_intf_id  = pgre_msg->egress_intf_id;
    pgre_session->egress_gw_ip    = pgre_msg->egress_gw_ip;
    pgre_session->egress_self_ip  = pgre_msg->egress_self_ip;

    /* fill session info */
    pgre_session->sesn_msgid      = pgre_msg->sesn_msgid;
    pgre_session->sesn_status     = JNX_GW_CTRL_STATUS_INIT;
    pgre_session->sesn_flags      = pgre_msg->sesn_flags;

    /* fill session info */
    pgre_session->sesn_proto      = pgre_msg->sesn_proto;
    pgre_session->sesn_client_ip  = pgre_msg->sesn_client_ip;
    pgre_session->sesn_server_ip  = pgre_msg->sesn_server_ip;
    pgre_session->sesn_sport      = pgre_msg->sesn_sport;
    pgre_session->sesn_dport      = pgre_msg->sesn_dport;
    pgre_session->sesn_id         = pgre_msg->sesn_id;

    /* fill data pic info */
    pgre_session->pdata_pic       = pgre_msg->pdata_pic;
    pgre_session->pingress_intf   = pgre_msg->pingress_intf;
    pgre_session->pegress_intf    = pgre_msg->pegress_intf;
    pgre_session->pgre_gw         = pgre_msg->pgre_gw;
    pgre_session->pipip_gw        = pgre_msg->pipip_gw;
    pgre_session->pingress_vrf    = pgre_msg->pingress_vrf;
    pgre_session->pegress_vrf     = pgre_msg->pegress_vrf;

    /* fill user & route info */
    pgre_session->puser           = pgre_msg->puser;
    pgre_session->pingress_route  = pgre_msg->pingress_route;
    pgre_session->pegress_route   = pgre_msg->pegress_route;

    /* set the patricia node key length */
    patricia_node_init_length(&pgre_session->gre_vrf_sesn_node,
                              sizeof(pgre_session->ingress_gre_key));

    patricia_node_init_length(&pgre_session->gre_sesn_node,
                              sizeof(jnx_gw_ctrl_session_info_t));

    patricia_node_init_length(&pgre_session->gre_ipip_sesn_node,
                              sizeof(jnx_gw_ctrl_session_info_t));

    patricia_node_init_length(&pgre_session->gre_user_sesn_node,
                              sizeof(jnx_gw_ctrl_session_info_t));

    /* add to vrf */
    JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_session->pingress_vrf);

    if (!patricia_add(&pgre_session->pingress_vrf->gre_sesn_db,
                      &pgre_session->gre_vrf_sesn_node)) {


        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->pingress_vrf);

        jnx_gw_log(LOG_ERR, "GRE Session setup patricia add failed(0)%d",
                   pgre_session->ingress_gre_key);

        /* generate an error message */
        goto gw_gre_session_cleanup3;
    }

    JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->pingress_vrf);

    /* add to gre gateway */

    JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_gw);

    if (!patricia_add(&pgre_gw->gre_sesn_db, &pgre_session->gre_sesn_node)) {

        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_gw);

        jnx_gw_log(LOG_ERR, "GRE Session setup patricia add failed(1)%d",
                   pgre_session->ingress_gre_key);

        /* generate an error message */
        goto gw_gre_session_cleanup2;
    }

    JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_gw);

    /* add to ipip gateway */
    if (pgre_session->pipip_gw) {

        JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_session->pipip_gw);

        if (!patricia_add(&pgre_session->pipip_gw->gre_sesn_db,
                          &pgre_session->gre_ipip_sesn_node)) {

            JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->pipip_gw);

            jnx_gw_log(LOG_ERR, "GRE Session setup patricia add failed(2)%d",
                   pgre_session->ingress_gre_key);
            goto gw_gre_session_cleanup1;
        }

        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->pipip_gw);
    }

    /* add to user structure */
    if (pgre_session->puser) {

        JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_session->puser);

        if (!patricia_add(&pgre_session->puser->gre_sesn_db,
                          &pgre_session->gre_user_sesn_node)) {

            JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->puser);

            jnx_gw_log(LOG_ERR, "GRE Session setup patricia add failed(3)%d",
                   pgre_session->ingress_gre_key);

            goto gw_gre_session_cleanup0;
        }

        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->puser);
    }

    /* update the statistics */

    if (pgre_session->puser) {
        atomic_add_uint(1, &pgre_session->puser->gre_sesn_count);
        atomic_add_uint(1, &pgre_session->puser->gre_active_sesn_count);
    }

    atomic_add_uint(1, &pgre_session->pgre_gw->gre_sesn_count);
    atomic_add_uint(1, &pgre_session->pgre_gw->gre_active_sesn_count);

    if (pgre_session->pipip_gw) {
        atomic_add_uint(1, &pgre_session->pipip_gw->gre_sesn_count);
        atomic_add_uint(1, &pgre_session->pipip_gw->gre_active_sesn_count);
    }

    atomic_add_uint(1, &pgre_session->pdata_pic->gre_sesn_count);
    atomic_add_uint(1, &pgre_session->pdata_pic->gre_active_sesn_count);

    atomic_add_uint(1, &pgre_session->pingress_vrf->gre_sesn_count);
    atomic_add_uint(1, &pgre_session->pingress_vrf->gre_active_sesn_count);

    atomic_add_uint(1, &pgre_session->pingress_intf->gre_sesn_count);
    atomic_add_uint(1, &pgre_session->pingress_intf->gre_active_sesn_count);

    if (pgre_session->pingress_vrf != pgre_session->pegress_vrf) {
        atomic_add_uint(1, &pgre_session->pegress_vrf->gre_sesn_count);
        atomic_add_uint(1, &pgre_session->pegress_vrf->gre_active_sesn_count);
    }

    if (pgre_session->pingress_intf != pgre_session->pegress_intf) {
        atomic_add_uint(1, &pgre_session->pegress_intf->gre_sesn_count);
        atomic_add_uint(1, &pgre_session->pegress_intf->gre_active_sesn_count);
    }

    atomic_add_uint(1, &jnx_gw_ctrl.gre_sesn_count);
    atomic_add_uint(1, &jnx_gw_ctrl.gre_active_sesn_count);

    jnx_gw_log(LOG_DEBUG, "GRE session setup done %d!", 
               pgre_session->ingress_gre_key);
    return pgre_session;

gw_gre_session_cleanup0:
    if (pgre_session->pipip_gw) {

        JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_session->pipip_gw);

        patricia_delete(&pgre_session->pipip_gw->gre_sesn_db,
                        &pgre_session->gre_ipip_sesn_node);

        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->pipip_gw);
    }

gw_gre_session_cleanup1:
    if (pgre_session->pgre_gw) {

        JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_session->pgre_gw);

        patricia_delete(&pgre_session->pgre_gw->gre_sesn_db,
                        &pgre_session->gre_sesn_node);

        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->pgre_gw);
    }

gw_gre_session_cleanup2:
    if (pgre_session->pingress_vrf) {

        JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_session->pingress_vrf);

        patricia_delete(&pgre_session->pingress_vrf->gre_sesn_db,
                        &pgre_session->gre_vrf_sesn_node);

        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->pingress_vrf);
    }

gw_gre_session_cleanup3:
    if (pgre_session) {

        JNX_GW_FREE(JNX_GW_CTRL_ID, pgre_session);
    }

gw_gre_session_cleanup:
    jnx_gw_log(LOG_ERR, "GRE session setup failed!");
    return NULL;
}

/**
 * This function updates the gre session stats across
 * the dependency chain, frees the gre session entry
 * @param    pgre_session   gre session structure pointer
 */
void
jnx_gw_ctrl_clear_gre_session(jnx_gw_ctrl_gre_session_t * pgre_session)
{
    if (pgre_session->pingress_vrf) { 

        JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_session->pingress_vrf);

        if (!patricia_delete(&pgre_session->pingress_vrf->gre_sesn_db,
                             &pgre_session->gre_vrf_sesn_node)) {

            jnx_gw_log(LOG_ERR, "GRE session clear patricia delete failed!");
        }
        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->pingress_vrf);
    }

    if (pgre_session->pgre_gw) { 

        JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_session->pgre_gw);

        if (!patricia_delete(&pgre_session->pgre_gw->gre_sesn_db,
                             &pgre_session->gre_sesn_node)) {
            jnx_gw_log(LOG_ERR, "GRE session clear patricia delete failed!");
        }

        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->pgre_gw);
    }

    if (pgre_session->pipip_gw) {

        JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_session->pgre_gw);

        if (!patricia_delete(&pgre_session->pipip_gw->gre_sesn_db,
                             &pgre_session->gre_ipip_sesn_node)) {
            jnx_gw_log(LOG_ERR, "GRE session clear patricia delete failed!");
        }

        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->pgre_gw);
    }

    if (pgre_session->puser) {

        JNX_GW_CTRL_SESN_DB_WRITE_LOCK(pgre_session->puser);

        if (!patricia_delete(&pgre_session->puser->gre_sesn_db,
                             &pgre_session->gre_user_sesn_node)) {
            jnx_gw_log(LOG_ERR, "GRE session clear patricia delete failed!");
        }

        JNX_GW_CTRL_SESN_DB_WRITE_UNLOCK(pgre_session->puser);
    }

    /* update status */

    atomic_sub_int(1, &pgre_session->pgre_gw->gre_active_sesn_count);

    if (pgre_session->puser) {
        atomic_sub_int(1, &pgre_session->puser->gre_active_sesn_count);
    }

    if (pgre_session->pipip_gw) {
        atomic_sub_int(1, &pgre_session->pipip_gw->gre_active_sesn_count);
    }

    atomic_sub_int(1, &pgre_session->pdata_pic->gre_active_sesn_count);

    atomic_sub_int(1, &pgre_session->pingress_vrf->gre_active_sesn_count);

    atomic_sub_int(1, &pgre_session->pingress_intf->gre_active_sesn_count);

    if (pgre_session->pingress_vrf != pgre_session->pegress_vrf) {
        atomic_sub_int(1, &pgre_session->pegress_vrf->gre_active_sesn_count);
        atomic_sub_int(1, &pgre_session->pegress_intf->gre_active_sesn_count);
    }

    /* the reverse route setup was a client route */
    if ((pgre_session->pegress_route) &&
        (pgre_session->pegress_route->flags == JNX_GW_CTRL_CLIENT_RT)) {

        atomic_sub_int(1, &pgre_session->pegress_route->ref_count);

        /* delete the route if the ref count reached zero */
        if (pgre_session->pegress_route->ref_count == 0) {
            jnx_gw_ctrl_delete_route(pgre_session->pegress_vrf,
                                     pgre_session->pegress_intf,
                                     pgre_session->pegress_route);

        }
    }
    atomic_sub_int(1, &jnx_gw_ctrl.gre_active_sesn_count);

    JNX_GW_FREE(JNX_GW_CTRL_ID, pgre_session);
    return;
}

/**
 * This function deletes the gre session from the
 * session dbs, & calls clear function
 * @param    pvrf        vrf structure pointer
 * @param    pgre_gw     gre gateway structure pointer
 * @param    pgre_session   gre session structure pointer
 */
status_t 
jnx_gw_ctrl_delete_gre_session(jnx_gw_ctrl_vrf_t * pvrf __unused,
                            jnx_gw_ctrl_gre_gw_t * pgre_gw __unused,
                            jnx_gw_ctrl_gre_session_t * pgre_session)
{
    uint8_t msg_type = JNX_GW_CTRL_GRE_SESN_DONE;

    jnx_gw_log(LOG_DEBUG, "GRE Session %d delete in %s",
           pgre_session->ingress_gre_key, pvrf->vrf_name);

    if (pgre_session->sesn_status == JNX_GW_CTRL_STATUS_FAIL) {
        msg_type = JNX_GW_CTRL_GRE_SESN_ERR;
    }

    if (pgre_session->pgre_gw &&  
        pgre_session->pgre_gw->gre_gw_status == JNX_GW_CTRL_STATUS_UP ) {
        jnx_gw_ctrl_fill_gw_gre_msg(msg_type, pvrf,
                                    pgre_session->pgre_gw, pgre_session);
    }

    if ((pgre_session->pdata_pic &&
         pgre_session->pdata_pic->pic_status == JNX_GW_CTRL_STATUS_UP) &&
        (pgre_session->sesn_status != JNX_GW_CTRL_STATUS_DOWN)) {
        jnx_gw_ctrl_fill_data_pic_msg(msg_type, JNX_GW_DEL_GRE_SESSION,
                                      pgre_session->pdata_pic, pgre_session);
    }
    jnx_gw_ctrl_clear_gre_session(pgre_session);
    return EOK;
}

/***********************************************************
 *                                                         *
 *             GRE GATEWAY MANAGEMENT ROUTINES             *
 *                                                         *
 ***********************************************************/

/***********************************************************
 *                                                         *
 *             GRE GATEWAY LOOKUP ROUTINES                 *
 *                                                         *
 ***********************************************************/

/**
 * This function returns the gre gateway for a gateway ip
 * in a vrf
 * @params  pvrf    vrf structure pointer
 * @params  gw_ip   gateway ip address
 * @returns pgre_gw if found
 *          NULL    otherwise
 */
jnx_gw_ctrl_gre_gw_t * 
jnx_gw_ctrl_lookup_gre_gw(jnx_gw_ctrl_vrf_t * pvrf, uint32_t gre_gw_ip)
{
    patnode * pnode;
    if ((pnode = patricia_get(&pvrf->gre_gw_db, sizeof(gre_gw_ip),
                              &gre_gw_ip))) {
        return jnx_gw_ctrl_gre_gw_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the first gre gateway for a vrf
 * @params  pvrf    vrf structure pointer
 * @returns pgre_gw if found
 *          NULL    otherwise
 */
jnx_gw_ctrl_gre_gw_t * 
jnx_gw_ctrl_get_first_gre_gw(jnx_gw_ctrl_vrf_t * pvrf)
{
    patnode * pnode;
    if ((pnode = patricia_find_next(&pvrf->gre_gw_db, NULL))) {
        return jnx_gw_ctrl_gre_gw_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next gre gateway for a 
 * gre gateway inside a vrf
 * @params  pvrf    vrf structure pointer
 * @params  pgre_gw current gre gateway pointer
 * @returns
 *    pgre_gw if next found
 *    NULL    otherwise
 */
jnx_gw_ctrl_gre_gw_t * 
jnx_gw_ctrl_get_next_gre_gw(jnx_gw_ctrl_vrf_t * pvrf,
                            jnx_gw_ctrl_gre_gw_t * pgre_gw)
{
    patnode * pnode;

    if (pgre_gw == NULL) {
        return jnx_gw_ctrl_get_first_gre_gw(pvrf);
    }

    if ((pnode =  patricia_find_next(&pvrf->gre_gw_db,
                                     &pgre_gw->gre_gw_node))) {
        return jnx_gw_ctrl_gre_gw_entry(pnode);
    }
    return NULL;
}

/***********************************************************
 *                                                         *
 *             GRE GATEWAY ADD/DELETE ROUTINES             *
 *                                                         *
 ***********************************************************/

/**
 * This function add a gre gateway entry inside the vrf 
 * adds the entry to the vrf structure gre gateway db
 * @param    pvrf        vrf structure pointer
 * @param    gre_gw_ip   gre gateway ip address
 * @param    gre_gw_port gre gateway source port
 * @returns
 *    pgre_gw     gre gateway pointer
 *    NULL        otherwise
 */
jnx_gw_ctrl_gre_gw_t * 
jnx_gw_ctrl_add_gre_gw(jnx_gw_ctrl_vrf_t * pvrf, uint32_t gre_gw_ip,
                       uint16_t gre_gw_port)
{
    jnx_gw_ctrl_gre_gw_t * pgre_gw;

    jnx_gw_log(LOG_INFO, "GRE Gateway add %s in %s",
           JNX_GW_IP_ADDRA(gre_gw_ip), pvrf->vrf_name);

    if ((pgre_gw = JNX_GW_MALLOC(JNX_GW_CTRL_ID, sizeof(*pgre_gw))) == NULL) {
        jnx_gw_log(LOG_ERR,
                   "GRE Gateway %s in %s add malloc failed!",
                   JNX_GW_IP_ADDRA(gre_gw_ip), pvrf->vrf_name);
        goto gre_gw_cleanup;
    }

    pgre_gw->gre_gw_ip     = gre_gw_ip;
    pgre_gw->gre_gw_port   = gre_gw_port;
    pgre_gw->gre_vrf_id    = pvrf->vrf_id;
    pgre_gw->pvrf          = pvrf;
    pgre_gw->gre_gw_status = JNX_GW_CTRL_STATUS_INIT;

    patricia_root_init(&pgre_gw->gre_sesn_db, FALSE,
                       sizeof(jnx_gw_ctrl_session_info_t),
                       fldoff(jnx_gw_ctrl_gre_session_t, sesn_proto) -
                       fldoff(jnx_gw_ctrl_gre_session_t, gre_sesn_node) -
                       fldsiz(jnx_gw_ctrl_gre_session_t, gre_sesn_node));

    if (JNX_GW_CTRL_SESN_DB_RW_LOCK_INIT(pgre_gw)) {
        jnx_gw_log(LOG_ERR, "GRE Gateway %s in %s lock init failed",
                   JNX_GW_IP_ADDRA(gre_gw_ip), pvrf->vrf_name);
        goto gre_gw_cleanup;
    }

    patricia_node_init_length(&pgre_gw->gre_gw_node,
                              sizeof(pgre_gw->gre_gw_ip)); 

    if (!patricia_add(&pvrf->gre_gw_db, &pgre_gw->gre_gw_node)) {
        jnx_gw_log(LOG_ERR, "GRE Gateway %s in %s add patricia add failed!",
                   JNX_GW_IP_ADDRA(gre_gw_ip), pvrf->vrf_name);
        goto gre_gw_cleanup;
    }

    pgre_gw->gre_gw_status = JNX_GW_CTRL_STATUS_UP;
    pvrf->gre_gw_count++;

    jnx_gw_log(LOG_INFO, "GRE Gateway %s in %s add done",
               JNX_GW_IP_ADDRA(gre_gw_ip), pvrf->vrf_name);
    return pgre_gw;

gre_gw_cleanup:

    /* generate an error message */
    jnx_gw_log(LOG_ERR, "GRE Gateway %s in %s add failed",
               JNX_GW_IP_ADDRA(gre_gw_ip), pvrf->vrf_name);

    if (pgre_gw) {
        JNX_GW_FREE(JNX_GW_CTRL_ID, pgre_gw);
    }
    return NULL;
}

/**
 * This function deletes all the gre sessions currently
 * active for the gre gateway, frees the gre gateway
 * structure
 * @param    pvrf      vrf structure pointer
 * @param    pgre_gw   gre gateway structure pointer
 */
void
jnx_gw_ctrl_clear_gre_gw(jnx_gw_ctrl_vrf_t * pvrf,
                         jnx_gw_ctrl_gre_gw_t * pgre_gw)
{
    jnx_gw_ctrl_gre_session_t * pgre_session;

    jnx_gw_log(LOG_INFO, "GRE Gateway %s in %s delete",
           pvrf->vrf_name, JNX_GW_IP_ADDRA(pgre_gw->gre_gw_ip));

    pgre_gw->gre_gw_status = JNX_GW_CTRL_STATUS_DOWN;

    while ((pgre_session = jnx_gw_ctrl_get_first_gre_session(NULL, pgre_gw,
                                                       NULL, NULL, TRUE))) {
        /* set the session status to fail */
        pgre_session->sesn_status = JNX_GW_CTRL_STATUS_FAIL;
        jnx_gw_ctrl_delete_gre_session(pvrf, pgre_gw, pgre_session);
    }

    jnx_gw_ctrl_send_gw_gre_msgs();

    jnx_gw_ctrl_send_data_pic_msgs();

    /* delete from the data base */
    if (!patricia_delete(&pvrf->gre_gw_db, &pgre_gw->gre_gw_node)) {
        jnx_gw_log(LOG_ERR, "GRE Gateway %s in %s patricia delete failed",
                   JNX_GW_IP_ADDRA(pgre_gw->gre_gw_ip), pvrf->vrf_name);
    }

    if (JNX_GW_CTRL_SESN_DB_RW_LOCK_DELETE(pgre_gw)) {
        jnx_gw_log(LOG_ERR, "GRE Gateway %s in %s lock delete failed",
                   JNX_GW_IP_ADDRA(pgre_gw->gre_gw_ip), pvrf->vrf_name);
    }

    JNX_GW_FREE(JNX_GW_CTRL_ID, pgre_gw);
    return;
}

/**
 * This function removes the gre gateway entry from 
 * the vrf structure, calls clear the gateway entry
 * @param    pvrf      vrf structure pointer
 * @param    pgre_gw   gre gateway structure pointer
 */
status_t
jnx_gw_ctrl_delete_gre_gw(jnx_gw_ctrl_vrf_t * pvrf,
                          jnx_gw_ctrl_gre_gw_t * pgre_gw)
{
    jnx_gw_ctrl_clear_gre_gw(pvrf, pgre_gw);
    pvrf->gre_gw_count--;
    return EOK;
}

/***********************************************************
 *                                                         *
 *             IPIP GATEWAY MANAGEMENT ROUTINES            *
 *                                                         *
 ***********************************************************/

/***********************************************************
 *                                                         *
 *             IPIP GATEWAY LOOKUP ROUTINES                *
 *                                                         *
 ***********************************************************/

/**
 * This function returns the ipip gateway for a gateway ip
 * in a vrf
 * @params  pvrf    vrf structure pointer
 * @params  gw_ip   gateway ip address
 * @returns
 *     pipip_gw  if found
 *     NULL      otherwise
 */
jnx_gw_ctrl_ipip_gw_t *
jnx_gw_ctrl_lookup_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf, uint32_t gateway_ip)
{
    patnode * pnode;
    if ((pnode = patricia_get(&pvrf->ipip_gw_db, sizeof(gateway_ip),
                              &gateway_ip))) {
        return jnx_gw_ctrl_ipip_gw_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the first ipip gateway for a vrf
 * @params  pvrf    vrf structure pointer
 * @returns pipip_gw if found
 *          NULL     otherwise
 */
jnx_gw_ctrl_ipip_gw_t * 
jnx_gw_ctrl_get_first_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf)
{
    patnode * pnode;
    if ((pnode = patricia_find_next(&pvrf->ipip_gw_db, NULL))) {
        return jnx_gw_ctrl_ipip_gw_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next ipip gateway for a 
 * ipip gateway inside a vrf
 * @params  pvrf     vrf structure pointer
 * @params  pipip_gw current ipip gateway pointer
 * @returns
 *    pipip_gw if next found
 *    NULL     otherwise
 */

jnx_gw_ctrl_ipip_gw_t * 
jnx_gw_ctrl_get_next_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf,
                             jnx_gw_ctrl_ipip_gw_t *pipip_gw)
{
    patnode * pnode;
    if (pipip_gw == NULL) {
        return jnx_gw_ctrl_get_first_ipip_gw(pvrf);
    }

    if ((pnode = patricia_find_next(&pvrf->ipip_gw_db,
                                    &pipip_gw->ipip_gw_node))) {
        return jnx_gw_ctrl_ipip_gw_entry(pnode);
    }
    return NULL;
}

/***********************************************************
 *                                                         *
 *             IPIP GATEWAY ADD/DELETE ROUTINES            *
 *                                                         *
 ***********************************************************/

/**
 * This function adds ipip gateway entry inside the vrf 
 * adds the entry to the vrf structure ipip gateway db
 * @param    pvrf        vrf structure pointer
 * @param    ipip_gw_ip  ipip gateway ip address
 * @returns
 *    pipip_gw     gre gateway pointer
 *    NULL         otherwise
 */
jnx_gw_ctrl_ipip_gw_t *
jnx_gw_ctrl_add_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf, uint32_t ipip_gw_ip)
{
    jnx_gw_ctrl_ipip_gw_t * pipip_gw;


    jnx_gw_log(LOG_INFO, "IPIP Gateway %s in %s add",
           JNX_GW_IP_ADDRA(ipip_gw_ip), pvrf->vrf_name);

    if ((pipip_gw = JNX_GW_MALLOC(JNX_GW_CTRL_ID, sizeof(*pipip_gw))) == NULL) {
        jnx_gw_log(LOG_ERR,
                   "IPIP Gateway %s in %s add alloc failed",
                   JNX_GW_IP_ADDRA(ipip_gw_ip), pvrf->vrf_name);
        return NULL;
    }

    pipip_gw->ipip_gw_ip     = ipip_gw_ip;
    pipip_gw->ipip_vrf_id    = pvrf->vrf_id;
    pipip_gw->ipip_gw_status = JNX_GW_CTRL_STATUS_INIT;
    pipip_gw->pvrf           = pvrf;

    patricia_root_init(&pipip_gw->gre_sesn_db, FALSE,
                       sizeof(jnx_gw_ctrl_session_info_t),
                       fldoff(jnx_gw_ctrl_gre_session_t, sesn_proto) -
                       fldoff(jnx_gw_ctrl_gre_session_t, gre_ipip_sesn_node) -
                       fldsiz(jnx_gw_ctrl_gre_session_t, gre_ipip_sesn_node));

    if (JNX_GW_CTRL_SESN_DB_RW_LOCK_INIT(pipip_gw)) {
        jnx_gw_log(LOG_ERR,
                   "IPIP Gateway %s in %s lock init failed",
                   JNX_GW_IP_ADDRA(pipip_gw->ipip_gw_ip), pvrf->vrf_name);
        goto ipip_gw_cleanup;
    }

    patricia_node_init_length(&pipip_gw->ipip_gw_node,
                              sizeof(pipip_gw->ipip_gw_ip)); 

    if (!patricia_add(&pvrf->ipip_gw_db, &pipip_gw->ipip_gw_node)) {
        jnx_gw_log(LOG_ERR,
                   "IPIP Gateway %s in %s add patricia add failed",
               JNX_GW_IP_ADDRA(pipip_gw->ipip_gw_ip), pvrf->vrf_name);
        goto ipip_gw_cleanup;
    }

    /* fill the ipip tunnel add message for the data pic agents */
    jnx_gw_ctrl_fill_data_pic_msgs(JNX_GW_IPIP_TUNNEL_MSG,
                                   JNX_GW_ADD_IP_IP_SESSION,
                                   pipip_gw);

    /* push the mssages to the data pics */
    jnx_gw_ctrl_send_data_pic_msgs();
    
    pvrf->ipip_gw_count++;
    jnx_gw_ctrl.ipip_tunnel_count++;
    jnx_gw_log(LOG_INFO, "IPIP Gateway %s in %s add done",
               JNX_GW_IP_ADDRA(ipip_gw_ip), pvrf->vrf_name);

    return pipip_gw;

ipip_gw_cleanup:
    /* generate an error message */
    jnx_gw_log(LOG_ERR, "IPIP Gateway %s in %s add failed",
               JNX_GW_IP_ADDRA(ipip_gw_ip), pvrf->vrf_name);
    JNX_GW_FREE(JNX_GW_CTRL_ID, pipip_gw);
    return NULL;
}

/**
 * This function deletes all the gre sessions currently
 * active for the ipip gateway, frees the ipip gateway
 * structure
 * @param    pvrf      vrf structure pointer
 * @param    pipip_gw  ipip gateway structure pointer
 */
void
jnx_gw_ctrl_clear_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf,
                          jnx_gw_ctrl_ipip_gw_t * pipip_gw)
{
    jnx_gw_ctrl_gre_gw_t * pgre_gw;
    jnx_gw_ctrl_gre_session_t * pgre_session;

    jnx_gw_log(LOG_INFO, "IPIP Gateway %s in %s delete",
               JNX_GW_IP_ADDRA(pipip_gw->ipip_gw_ip), pvrf->vrf_name);

    pipip_gw->ipip_gw_status = JNX_GW_CTRL_STATUS_DOWN;

    while ((pgre_session = jnx_gw_ctrl_get_first_gre_session(NULL, NULL,
                                                       pipip_gw, NULL, TRUE))) {
        /* set the session status to fail */
        pgre_session->sesn_status = JNX_GW_CTRL_STATUS_FAIL;
        pgre_gw                   = pgre_session->pgre_gw;
        jnx_gw_ctrl_delete_gre_session(pvrf, pgre_gw, pgre_session);
        jnx_gw_ctrl_fill_data_pic_msg(JNX_GW_GRE_SESSION_MSG,
                                      JNX_GW_DEL_GRE_SESSION,
                                      pgre_session->pdata_pic, pgre_session);
    }

    /* send the gre messages to the gre gateways */
    jnx_gw_ctrl_send_gw_gre_msgs();

    /* fill the ipip tunnel delete messages to the data pic agents */
    jnx_gw_ctrl_fill_data_pic_msgs(JNX_GW_IPIP_TUNNEL_MSG, 
                                   JNX_GW_DEL_IP_IP_SESSION,
                                   pipip_gw);

    /* push the messages out */
    jnx_gw_ctrl_send_data_pic_msgs();

    if (JNX_GW_CTRL_SESN_DB_RW_LOCK_DELETE(pipip_gw)) {
        jnx_gw_log(LOG_ERR,
                   "IPIP Gateway %s in %s lock delete failed",
                   JNX_GW_IP_ADDRA(pipip_gw->ipip_gw_ip), pvrf->vrf_name);
    }
    /* remove from the database */
    if (!patricia_delete(&pvrf->ipip_gw_db, &pipip_gw->ipip_gw_node)) {
        jnx_gw_log(LOG_ERR, 
                   "IPIP Gateway %s in %s delete patricia delete failed",
               JNX_GW_IP_ADDRA(pipip_gw->ipip_gw_ip), pvrf->vrf_name);
    }
    JNX_GW_FREE(JNX_GW_CTRL_ID, pipip_gw);
    return;
}

/**
 * This function removes the ipip gateway entry from 
 * the vrf structure, calls clear the gateway entry
 * @param    pvrf      vrf structure pointer
 * @param    pipip_gw  ipip gateway structure pointer
 */
status_t
jnx_gw_ctrl_delete_ipip_gw(jnx_gw_ctrl_vrf_t * pvrf,
                           jnx_gw_ctrl_ipip_gw_t  * pipip_gw)
{
    jnx_gw_ctrl_clear_ipip_gw(pvrf, pipip_gw);
    pvrf->ipip_gw_count--;
    jnx_gw_ctrl.ipip_tunnel_count--;
    return EOK;
}

/***********************************************************
 *                                                         *
 *             DATA-PIC INTF MANAGEMENT ROUTINES           *
 *                                                         *
 ***********************************************************/

/***********************************************************
 *                                                         *
 *             DATA-PIC INTF LOOKUP ROUTINES               *
 *                                                         *
 ***********************************************************/

/**
 * This function returns the interface 
 * in a vrf/control policy/ data pic
 * @params  pvrf        vrf structure pointer
 * @params  pctrl       control policy structure pointer
 * @params  pdata_pic   data pic structure pointer
 * @params  intf_id     interface index
 * @returns
 *     pintf   if found
 *     NULL    otherwise
 */
jnx_gw_ctrl_intf_t *
jnx_gw_ctrl_lookup_intf(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_data_pic_t * pdata_pic,
                        jnx_gw_ctrl_policy_t * pctrl, uint16_t intf_id)
{
    patnode * pnode;
    if (pvrf) {
        if ((pnode = patricia_get(&pvrf->vrf_intf_db,
                                  sizeof(intf_id), &intf_id))) {
            return jnx_gw_ctrl_vrf_intf_entry(pnode);
        }
    } else if (pdata_pic) {
        if ((pnode = patricia_get(&pdata_pic->pic_intf_db,
                                  sizeof(intf_id), &intf_id))) {
            return jnx_gw_ctrl_intf_entry(pnode);
        }
    } else if (pctrl) {
        if ((pnode = patricia_get(&pctrl->ctrl_intf_db,
                                  sizeof(intf_id), &intf_id))) {
            return jnx_gw_ctrl_pol_intf_entry(pnode);
        }
    }
    return NULL;
}

/**
 * This function returns the first interface
 * in a vrf/control policy/ data pic
 * @params  pvrf        vrf structure pointer
 * @params  pdata_pic   data pic structure pointer
 * @params  pctrl       control policy structure pointer
 * @returns
 *     pintf   if found
 *     NULL         otherwise
 */

jnx_gw_ctrl_intf_t *
jnx_gw_ctrl_get_first_intf(jnx_gw_ctrl_vrf_t * pvrf,
                           jnx_gw_ctrl_data_pic_t * pdata_pic,
                           jnx_gw_ctrl_policy_t * pctrl)
{
    patnode * pnode;
    if (pvrf) {
        if ((pnode = patricia_find_next(&pvrf->vrf_intf_db, NULL))) {
            return jnx_gw_ctrl_vrf_intf_entry(pnode);
        }
    } else if (pdata_pic) {
        if ((pnode = patricia_find_next(&pdata_pic->pic_intf_db,
                                        NULL))) {
            return jnx_gw_ctrl_intf_entry(pnode);
        }
    } else if (pctrl) {
        if ((pnode = patricia_find_next(&pctrl->ctrl_intf_db, NULL))) {
            return jnx_gw_ctrl_pol_intf_entry(pnode);
        }
    }
    return NULL;
}

/**
 * This function returns the next interface
 * in a vrf/control policy/ data pic
 * @params  pvrf        vrf structure pointer
 * @params  pdata_pic   data pic structure pointer
 * @params  pctrl       control policy structure pointer
 * @params  pintf  data interface structure pointer
 * @returns
 *     pintf   if next found
 *     NULL         otherwise
 */
jnx_gw_ctrl_intf_t *
jnx_gw_ctrl_get_next_intf(jnx_gw_ctrl_vrf_t * pvrf,
                          jnx_gw_ctrl_data_pic_t * pdata_pic,
                          jnx_gw_ctrl_policy_t * pctrl,
                          jnx_gw_ctrl_intf_t * pintf)
{
    patnode * pnode;

    if (pintf == NULL) {
        return jnx_gw_ctrl_get_first_intf(pvrf, pdata_pic, pctrl);
    }

    if (pvrf) {
        if ((pnode = patricia_find_next(&pvrf->vrf_intf_db,
                                        &pintf->intf_vrf_node))) {
            return jnx_gw_ctrl_vrf_intf_entry(pnode);
        }
    } 

    if (pdata_pic) {
        if ((pnode = patricia_find_next(&pdata_pic->pic_intf_db,
                                        &pintf->intf_node))) { 
            return jnx_gw_ctrl_intf_entry(pnode);
        }
    } 

    if (pctrl) {
        if  ((pnode = patricia_find_next(&pctrl->ctrl_intf_db,
                                         &pintf->intf_ctrl_node))) {
            return jnx_gw_ctrl_pol_intf_entry(pnode);
        }
    }

    return NULL;
}

/***********************************************************
 *                                                         *
 *             DATA-PIC INTF ADD/DELETE ROUTINES           *
 *                                                         *
 ***********************************************************/

/**
 * This function adds the interface to the data pic
 * for a vrf
 * @params  pvrf        vrf structure pointer
 * @params  pdata_pic   data pic structure pointer
 * @params  pctrl       control policy structure pointer
 * @params  intf_name   data interface name string
 * @params  intf_id     data interface index 
 * @params  intf_ip     floating interface ip
 * @returns
 *     pintf   intf structure pointer
 *     NULL    otherwise
 */
jnx_gw_ctrl_intf_t *
jnx_gw_ctrl_add_intf(jnx_gw_ctrl_vrf_t * pvrf,
                     jnx_gw_ctrl_data_pic_t * pdata_pic,
                     jnx_gw_ctrl_intf_t * pintf,
                     jnx_gw_ctrl_policy_t * pctrl,
                     uint8_t *intf_name, uint16_t intf_id,
                     uint32_t intf_subunit, uint32_t intf_ip)
{
    /* retry setting up the nexthop */
    if (pintf) {
        if (pintf->pdata_pic->pic_status == JNX_GW_CTRL_STATUS_UP) {
            jnx_gw_ctrl_add_nexthop(pvrf, pintf);
        }
        return pintf;
    }

    jnx_gw_log(LOG_INFO, "Interface \"%s.%d\" add",
               intf_name, intf_subunit);

    if ((pintf = JNX_GW_MALLOC(JNX_GW_CTRL_ID, sizeof(*pintf))) == NULL) {
        jnx_gw_log(LOG_ERR, "Interface \"%s.%d\" add malloc failed",
                   intf_name, intf_subunit);
        goto intf_cleanup;
    }

    strncpy(pintf->intf_name, intf_name, sizeof(pintf->intf_name));

    pintf->intf_id     = intf_id;
    pintf->intf_vrf_id = pvrf->vrf_id;
    pintf->intf_status = JNX_GW_CTRL_STATUS_INIT;
    pintf->intf_subunit = intf_subunit;

    /* back pointers */
    pintf->pdata_pic   = pdata_pic;
    pintf->intf_ip     = intf_ip;
    pintf->pvrf        = pvrf;
    pintf->pctrl       = pctrl;

    /*
     * initialize thr route table for the routes attached to this
     * interface
     */
    patricia_root_init(&pintf->intf_route_db, FALSE,
                       fldsiz(jnx_gw_ctrl_route_t, key),
                       fldoff(jnx_gw_ctrl_route_t, key) -
                       fldoff(jnx_gw_ctrl_route_t, route_node) -
                       fldsiz(jnx_gw_ctrl_route_t, route_node));

    patricia_node_init_length(&pintf->intf_node, 
                              sizeof(pintf->intf_id));

    patricia_node_init_length(&pintf->intf_ctrl_node, 
                              sizeof(pintf->intf_id));

    patricia_node_init_length(&pintf->intf_vrf_node, 
                              sizeof(pintf->intf_id));

    if (!patricia_add(&pvrf->vrf_intf_db, &pintf->intf_vrf_node)) {
        jnx_gw_log(LOG_ERR, "Interface \"%s.%d\" add patricia add failed", 
                   intf_name, intf_subunit);
        goto intf_cleanup;
    }

    if (!patricia_add(&pdata_pic->pic_intf_db, &pintf->intf_node)) {
        jnx_gw_log(LOG_ERR, "Interface \"%s.%d\" add patricia add failed",
                   intf_name, intf_subunit);
        goto intf_cleanup0;
    }

    /* add to the control policy */
    if ((pctrl) &&
        !patricia_add(&pctrl->ctrl_intf_db, &pintf->intf_ctrl_node)) {
        jnx_gw_log(LOG_ERR, "Interface \"%s.%d\" add patricia add failed",
                   intf_name, intf_subunit);
        
        goto intf_cleanup1;
    }

    /* now try to setup a nexthop for this ifl */
    if (pdata_pic->pic_status == JNX_GW_CTRL_STATUS_UP) {
        jnx_gw_ctrl_add_nexthop(pvrf, pintf);
    }

    jnx_gw_ctrl_send_ipip_msg_to_data_pic(pvrf, pdata_pic, pintf,
                                          JNX_GW_ADD_IP_IP_SESSION);

    pdata_pic->intf_count++;
    jnx_gw_log(LOG_INFO, "Interface \"%s.%d\" add done",
               intf_name, intf_subunit);
    return pintf;

intf_cleanup1:
    patricia_delete(&pdata_pic->pic_intf_db, &pintf->intf_node);

intf_cleanup0:
    patricia_delete(&pvrf->vrf_intf_db, &pintf->intf_vrf_node);

intf_cleanup:
    if (pintf) {
        JNX_GW_FREE(JNX_GW_CTRL_ID, pintf);
    }
    jnx_gw_log(LOG_ERR, "Interface \"%s.%d\" add failed",
               intf_name, intf_subunit);
    return NULL;
}

/**
 * This function clears the interface related
 * resources
 * @params  pvrf        vrf structure pointer
 * @params  pdata_pic   data pic structure pointer
 * @params  pintf       intf structure pointer
 */
void 
jnx_gw_ctrl_clear_intf(jnx_gw_ctrl_vrf_t * pvrf,
                       jnx_gw_ctrl_data_pic_t * pdata_pic,
                       jnx_gw_ctrl_intf_t * pintf)
{
    jnx_gw_ctrl_policy_t   * pctrl = NULL;
    jnx_gw_ctrl_gre_gw_t   * pgre_gw = NULL;
    jnx_gw_ctrl_ipip_gw_t  * pipip_gw = NULL;
    jnx_gw_ctrl_gre_session_t * pgre_session = NULL, *prev = NULL;
    jnx_gw_ctrl_route_t * proute;

    pctrl = pintf->pctrl;
    pvrf  = pintf->pvrf;


    jnx_gw_log(LOG_INFO, "Interface \"%s.%d\" delete",
               pintf->intf_name, pintf->intf_subunit);

    /* clear up all the gre sessions on this interface */

    /* get the gre gateways, & clear the matching gre sessions */
    while ((pgre_gw = jnx_gw_ctrl_get_next_gre_gw(pvrf, pgre_gw))) {

        pgre_session = prev = NULL;
        while ((pgre_session 
                = jnx_gw_ctrl_get_next_gre_session(NULL, pgre_gw, NULL,
                                                   NULL, prev,
                                                   TRUE))) {
            /* if this gre session is through this interface */
            if ((pgre_session->pingress_intf != pintf) &&
                (pgre_session->pegress_intf != pintf)) {
                prev = pgre_session;
                continue;
            }
            jnx_gw_ctrl_delete_gre_session(pvrf, pgre_gw, pgre_session);
        }
    }

    jnx_gw_ctrl_send_gw_gre_msgs();
    jnx_gw_ctrl_send_data_pic_msgs();

    /* clear the gre sessions for the ipip gateways
     * on this interface
     */
    while ((pipip_gw = jnx_gw_ctrl_get_next_ipip_gw(pvrf, pipip_gw))) {

        pgre_session = prev = NULL;

        while ((pgre_session =
                jnx_gw_ctrl_get_next_gre_session(NULL, NULL, pipip_gw,
                                                 NULL, prev,
                                                 TRUE))) {

            /* if this gre session is on this interface  */
            if ((pgre_session->pingress_intf != pintf) &&
                (pgre_session->pegress_intf != pintf)) {
                prev = pgre_session;
                continue;
            }

            pgre_gw = pgre_session->pgre_gw;
            jnx_gw_ctrl_delete_gre_session(pvrf, pgre_gw, pgre_session);
        }
    }

    jnx_gw_ctrl_send_gw_gre_msgs();
    jnx_gw_ctrl_send_data_pic_msgs();

    /* clear all the routes with this interface as nexthop */
    if (pintf->intf_status == JNX_GW_CTRL_STATUS_UP) {

        if ((pctrl) && (pintf->intf_rt)) {
            jnx_gw_ctrl_delete_route(pvrf, pintf, pintf->intf_rt);
        }

        while ((proute = jnx_gw_ctrl_route_get_first(pvrf, pintf))) {
            jnx_gw_ctrl_delete_route(pvrf, pintf, proute);
        }

    }

    /* delete the nexthop set for this interface */
    jnx_gw_ctrl_delete_nexthop(pvrf, pintf);

    /* delete from vrf & data pic structure dbs */
    patricia_delete(&pvrf->vrf_intf_db, &pintf->intf_vrf_node);

    patricia_delete(&pdata_pic->pic_intf_db, &pintf->intf_node);

    /* delete from the control policy */
    if ((pctrl) &&
        patricia_delete(&pctrl->ctrl_intf_db, &pintf->intf_ctrl_node)) {
        pintf->pctrl->ctrl_intf_count--;
    }

    /*
     * incase we have not scheduled a nh add request.
     * free interface memory.
     */
    if ((pintf->intf_status != JNX_GW_CTRL_NH_ADD_PENDING) &&
        (pintf->intf_status != JNX_GW_CTRL_NH_DELETE_PENDING)) {
        JNX_GW_FREE(JNX_GW_CTRL_ID, pintf);
    }
}

/**
 * This function deletes the interface from the data pic
 * for a vrf
 * @params  pvrf        vrf structure pointer
 * @params  pdata_pic   data pic structure pointer
 * @params  pintf       intf structure pointer
 */
status_t
jnx_gw_ctrl_delete_intf(jnx_gw_ctrl_vrf_t * pvrf,
                             jnx_gw_ctrl_data_pic_t * pdata_pic,
                             jnx_gw_ctrl_intf_t * pintf)
{
    jnx_gw_ctrl_clear_intf(pvrf, pdata_pic, pintf);
    pdata_pic->intf_count--;
    return EOK;
}

/***********************************************************
 *                                                         *
 *             DATA-PIC MANAGEMENT ROUTINES                *
 *                                                         *
 ***********************************************************/

/***********************************************************
 *                                                         *
 *             DATA-PIC LOOKUP ROUTINES                    *
 *                                                         *
 ***********************************************************/


/**
 * This function returns the data pic for a ifd name
 * or the pconn client connection pointer
 * @params  pic_name    pic ifd name
 * @params  pclient     pconn client connection pointer
 * @returns
 *     pdata_pic   if found
 *     NULL         otherwise
 */
jnx_gw_ctrl_data_pic_t *
jnx_gw_ctrl_lookup_data_pic(uint8_t * pic_name, pconn_client_t * pclient)
{
    patnode * pnode;
    jnx_gw_ctrl_data_pic_t * pdata_pic = NULL;


    /* if pic name is provided, do a lookup */
    if (pic_name) {
        if ((pnode = patricia_get(&jnx_gw_ctrl.pic_db, strlen(pic_name) + 1,
                                  pic_name))) {
            return jnx_gw_ctrl_pic_entry(pnode);
        }
        return NULL;
    }

    if (pclient == NULL) return NULL;

    while ((pdata_pic = jnx_gw_ctrl_get_next_data_pic(pdata_pic))) {

        if (pdata_pic->pic_data_conn == pclient)
            return pdata_pic;
    }
    return NULL;
}

/**
 * This function returns the first data pic
 * @returns
 *     pdata_pic   if found
 *     NULL         otherwise
 */
jnx_gw_ctrl_data_pic_t *
jnx_gw_ctrl_get_first_data_pic(void)
{
    patnode * pnode;
    if ((pnode = patricia_find_next(&jnx_gw_ctrl.pic_db, NULL))) {
        return jnx_gw_ctrl_pic_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next data pic for a data pic
 * @param    pdata_pic   current data pic structure pointer
 * @returns
 *     pdata_pic   if found
 *     NULL        otherwise
 */
jnx_gw_ctrl_data_pic_t *
jnx_gw_ctrl_get_next_data_pic(jnx_gw_ctrl_data_pic_t * pdata_pic)
{
    patnode * pnode = NULL;
    if (pdata_pic == NULL) {
        return jnx_gw_ctrl_get_first_data_pic();
    }

    if ((pnode = patricia_find_next(&jnx_gw_ctrl.pic_db,
                                     &pdata_pic->pic_node))){
        return jnx_gw_ctrl_pic_entry(pnode);
    }
    return NULL;
}

/***********************************************************
 *                                                         *
 *             DATA-PIC USE ROUTINES                       *
 *                                                         *
 ***********************************************************/

/**
 * This function selects a data pic for a new gre session 
 * if tries to find out the interfaces on the data pic
 * for the ingress and egress vrf for the session
 * if found, gets the interface ip address & sets them
 * as self ip for the gre session
 * @param    pgre_session   gre session pointer
 */
status_t 
jnx_gw_ctrl_select_data_pic(jnx_gw_ctrl_gre_session_t * pgre_session)
{
    jnx_gw_ctrl_data_pic_t  * pdata_pic = NULL;
    jnx_gw_ctrl_intf_t * pintf = NULL;
    jnx_gw_ctrl_intf_t * pin_intf = NULL,
                       * pout_intf = NULL;
    jnx_gw_ctrl_route_t *proute;

    pgre_session->pdata_pic = NULL;

    while ((pdata_pic = jnx_gw_ctrl_get_next_data_pic(pdata_pic))) {

        pintf     = NULL;
        pin_intf  = NULL;
        pout_intf = NULL;

        /* this data pic does not have any interfaces up */

        if (pdata_pic->pic_status != JNX_GW_CTRL_STATUS_UP) {
            continue;
        }

        while ((pintf = jnx_gw_ctrl_get_next_intf(NULL, pdata_pic,
                                                  NULL, pintf))) {

            /* this interface is not ready */
            if ((pintf->intf_status != JNX_GW_CTRL_STATUS_UP) || 
                !(pintf->intf_rt) ||
                (pintf->intf_rt->status != JNX_GW_CTRL_STATUS_UP)) {
                continue;
            }

            /* ingress & egress vrf may be same, fall through*/

            if (pintf->pvrf == pgre_session->pingress_vrf) {
                pin_intf = pintf;
            }

            if (pintf->pvrf == pgre_session->pegress_vrf) {
                pout_intf = pintf;
            }

            /* if not found both the interfaces */
            if ((pin_intf == NULL) || (pout_intf == NULL)) {
                continue;
            }

            /* later on for load balancing,
             * we may need to consider the 
             * data traffic/session load on the
             * data pic (TBD)
             */
            pgre_session->pdata_pic       = pdata_pic;

            pgre_session->pegress_intf    = pout_intf;
            pgre_session->pingress_intf   = pin_intf;

            pgre_session->egress_intf_id  = pout_intf->intf_id;
            pgre_session->ingress_intf_id = pin_intf->intf_id;

            pgre_session->egress_self_ip  = pout_intf->intf_ip;
            pgre_session->ingress_self_ip = pin_intf->intf_ip;

            jnx_gw_log(LOG_DEBUG, 
                       "Data agent \"%s\" selected for GRE session %d",
                       pdata_pic->pic_name, pgre_session->ingress_gre_key);

            pgre_session->pingress_route  = pin_intf->intf_rt;

            if (pgre_session->puser->pipip_gw) {
                pgre_session->pegress_route  = pout_intf->intf_rt;
                return (EOK);
            }

            /*
             * if the user configuration has the egress information
             * as native ip packet, try to set a route for the 
             * client ip on the egress-interface 
             */

            /*
             * if the ingress and egress vrf are the same,
             * and client gateway and client are the same,
             * do not set the route on the reverse path
             * for native format reverse traffic.
             * It may create a block-hole, with the traffic
             * meant for the client gateway getting
             * routed back to the ms-pic
             */

            if ((pgre_session->pingress_vrf == pgre_session->pegress_vrf) &&
                (pgre_session->sesn_client_ip == pgre_session->ingress_gw_ip)){
                return (EOK);
            }

            /* see if the route is already present */
            if ((proute =
                 jnx_gw_ctrl_route_lookup(pgre_session->pegress_vrf, pout_intf,
                                          pgre_session->sesn_client_ip))) {
                pgre_session->pegress_route = proute;
                proute->ref_count++;
                return EOK;
            }

            /* otherwise, set the client route */
            if ((proute =
                 jnx_gw_ctrl_add_route(pgre_session->pegress_vrf, pout_intf,
                                       NULL, pgre_session->sesn_client_ip,
                                       JNX_GW_CTRL_CLIENT_RT))) {
                pgre_session->pegress_route = proute;
                proute->ref_count++;
            }
            return EOK;
        }
    }
    jnx_gw_log(LOG_ERR, "Could not get a data agent for GRE session %d",
               pgre_session->ingress_gre_key);
    return EFAIL;
}

/***********************************************************
 *                                                         *
 *             DATA-PIC ADD/DELETE ROUTINES                *
 *                                                         *
 ***********************************************************/

/**
 * This function add a data pic on receipt of 
 * management agent data pic add message 
 * starts the pconn client connect with the
 * data pic agent
 * @param    pic_name   pic ifd name
 * @returns  
 *   pdata_pic  data pic structure pointer
 *   NULL       otherwise
 */
jnx_gw_ctrl_data_pic_t *
jnx_gw_ctrl_add_data_pic(jnx_gw_msg_ctrl_config_pic_t * pmsg)
{
    pconn_client_params_t   params;
    jnx_gw_ctrl_data_pic_t *pdata_pic;

    jnx_gw_log(LOG_INFO, "Data agent \"%s\" add", pmsg->pic_name);

    if ((pdata_pic = JNX_GW_MALLOC(JNX_GW_CTRL_ID, sizeof(*pdata_pic)))
        == NULL) {
        jnx_gw_log(LOG_ERR, "Data agent \"%s\" add malloc failed",
                   pmsg->pic_name);
        goto gw_datapic_cleanup;
    }

    pdata_pic->peer_type  = ntohl(pmsg->pic_peer_type);
    pdata_pic->ifd_id     = ntohl(pmsg->pic_ifd_id);
    pdata_pic->fpc_id     = ntohl(pmsg->pic_fpc_id);
    pdata_pic->pic_id     = ntohl(pmsg->pic_pic_id);
    pdata_pic->pic_status = JNX_GW_CTRL_STATUS_INIT;
    strncpy(pdata_pic->pic_name, pmsg->pic_name, sizeof(pdata_pic->pic_name));

    patricia_node_init_length(&pdata_pic->pic_node,
                              strlen(pmsg->pic_name) + 1);

    if (!patricia_add(&jnx_gw_ctrl.pic_db, &pdata_pic->pic_node)) {
        jnx_gw_log(LOG_ERR, "Data agent \"%s\" add patricia add failed",
                   pmsg->pic_name);
        goto gw_datapic_cleanup;
    }

    /* establish async client connect with the data pic agent */
    memset(&params, 0, sizeof(pconn_client_params_t));
    params.pconn_port  = JNX_GW_DATA_PORT;
    params.pconn_peer_info.ppi_peer_type = pdata_pic->peer_type;
    params.pconn_peer_info.ppi_fpc_slot  = pdata_pic->fpc_id;
    params.pconn_peer_info.ppi_pic_slot  = pdata_pic->pic_id;
    params.pconn_event_handler = jnx_gw_ctrl_data_event_handler;

    if (!(pdata_pic->pic_data_conn = 
        pconn_client_connect_async(&params, jnx_gw_ctrl.ctxt,
                                   jnx_gw_ctrl_data_msg_handler,
                                   (void *)pdata_pic))) {

        jnx_gw_log(LOG_ERR, "Data agent \"%s\" connect failed",
                   pdata_pic->pic_name);
        patricia_delete(&jnx_gw_ctrl.pic_db, &pdata_pic->pic_node);
        goto gw_datapic_cleanup;
    }

    /* initialize the interface db */
    patricia_root_init(&pdata_pic->pic_intf_db, FALSE, 
                       fldsiz(jnx_gw_ctrl_intf_t, intf_id),
                       fldoff(jnx_gw_ctrl_intf_t, intf_id) -
                       fldoff(jnx_gw_ctrl_intf_t, intf_node) -
                       fldsiz(jnx_gw_ctrl_intf_t, intf_node));

    /* initialize the send lock for the data pic */
    pthread_mutex_init(&pdata_pic->pic_send_lock, 0);

    jnx_gw_log(LOG_ERR, "Data agent \"%s\" add done",
               pdata_pic->pic_name);
    jnx_gw_ctrl.pic_count++;

    return pdata_pic;

gw_datapic_cleanup:
    jnx_gw_log(LOG_ERR, "Data agent \"%s\" add failed",
               pdata_pic->pic_name);
    if (pdata_pic) {
        /* print error message */
        JNX_GW_FREE(JNX_GW_CTRL_ID, pdata_pic);
    }
    return NULL;
}

/**
 * This function clears the data pic structure, 
 * frees the resources,
 * initiates the gre session closure procedure
 * deletes the data pic entry from the main
 * control block data pic db
 * deletes the interfaces attached to this
 * data pic
 * closes the data pic pconn client connection
 * frees the structure memory
 * @param   pdata_pic  data pic structure pointer
 */
void 
jnx_gw_ctrl_clear_data_pic(jnx_gw_ctrl_data_pic_t * pdata_pic)
{
    jnx_gw_ctrl_vrf_t       * pvrf;
    jnx_gw_ctrl_intf_t * pintf;

    jnx_gw_log(LOG_INFO, "Data agent \"%s\" delete",
               pdata_pic->pic_name);

    pdata_pic->pic_status = JNX_GW_CTRL_STATUS_DOWN;

    if (pdata_pic->pic_data_conn) {
        pconn_client_close(pdata_pic->pic_data_conn);
        pdata_pic->pic_data_conn = NULL;
    }

    /* clear all the interfaces on this data pic */
    while ((pintf = jnx_gw_ctrl_get_first_intf(NULL, pdata_pic, NULL))) {
        pvrf = pintf->pvrf;
        jnx_gw_ctrl_delete_intf(pvrf, pdata_pic, pintf);
    }

    /* delete from the data interface db */
    if (!patricia_delete(&jnx_gw_ctrl.pic_db, &pdata_pic->pic_node)) {
        jnx_gw_log(LOG_ERR, "Data agent \"%s\" delete patricia delete failed",
               pdata_pic->pic_name);
    }

    /* free memory */
    JNX_GW_FREE(JNX_GW_CTRL_ID, pdata_pic);
}

/**
 * This function calls clear data pic function
 * & updates the status 
 * @param   pdata_pic  data pic structure pointer
 */
status_t
jnx_gw_ctrl_delete_data_pic(jnx_gw_ctrl_data_pic_t * pdata_pic)
{
    jnx_gw_log(LOG_INFO, "Data Agent %s delete",
               pdata_pic->pic_name);
    jnx_gw_ctrl_clear_data_pic(pdata_pic);
    jnx_gw_ctrl.pic_count--;
    return EOK;
}

/**
 * This function clears all the data pics
 */
status_t
jnx_gw_ctrl_clear_data_pics(void)
{
    jnx_gw_ctrl_data_pic_t * pdata_pic = NULL;

    while ((pdata_pic = jnx_gw_ctrl_get_first_data_pic())) {
        jnx_gw_ctrl_delete_data_pic(pdata_pic);
    }

    return EOK;
}

/***********************************************************
 *                                                         *
 *             CONTROL POLICY MANAGEMENT ROUTINES          *
 *                                                         *
 ***********************************************************/

/***********************************************************
 *                                                         *
 *             CONTROL POLICY LOOKUP ROUTINES              *
 *                                                         *
 ***********************************************************/

/**
 * This function returns the control policy for a address range
 * in a vrf
 * @params  pvrf    vrf structure pointer
 * @params  addr    addr
 * @params  mask    mask
 * @returns
 *     pctrl     if found
 *     NULL      otherwise
 */
jnx_gw_ctrl_policy_t * 
jnx_gw_ctrl_lookup_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                        uint32_t addr, uint32_t mask)
{
    patnode * pnode;
    uint32_t key = (addr & mask);

    if ((pnode = patricia_get(&pvrf->ctrl_policy_db, sizeof(key), &key))) {
        return jnx_gw_ctrl_vrf_ctrl_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the first control policy for a vrf
 * @params  pvrf    vrf structure pointer
 * @returns pctrl   if found
 *          NULL     otherwise
 */
jnx_gw_ctrl_policy_t *
jnx_gw_ctrl_get_first_ctrl(jnx_gw_ctrl_vrf_t * pvrf)
{
    patnode * pnode;

    if ((pnode = patricia_find_next(&pvrf->ctrl_policy_db, NULL))) {
        return (jnx_gw_ctrl_policy_t *) jnx_gw_ctrl_vrf_ctrl_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next control policy for a 
 * control policy in a vrf
 * @params  pvrf    vrf structure pointer
 * @params  pctrl   control policy structure pointer
 * @returns pctrl   if next found
 *          NULL    otherwise
 */
jnx_gw_ctrl_policy_t *
jnx_gw_ctrl_get_next_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                          jnx_gw_ctrl_policy_t * pctrl)
{
    patnode * pnode = NULL;
    if (pctrl == NULL) {
        return jnx_gw_ctrl_get_first_ctrl(pvrf);
    }
    if ((pnode = patricia_find_next(&pvrf->ctrl_policy_db,
                                    &pctrl->ctrl_vrf_node))) {
        return jnx_gw_ctrl_vrf_ctrl_entry(pnode);
    }
    return NULL;
}

/***********************************************************
 *                                                         *
 *             CONTROL POLICY USE ROUTINES                 *
 *                                                         *
 ***********************************************************/

/**
 * This function  assign an ip address to the data pic
 * interface, source the ip address from the vrf control
 * policy
 * @params  pvrf
 * @params  pdata_pic
 * @params  intf_ip
 */
jnx_gw_ctrl_policy_t *
jnx_gw_ctrl_add_intf_to_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                             jnx_gw_ctrl_data_pic_t * pdata_pic __unused,
                             uint32_t * intf_ip)
{
    uint32_t cur_addr;
    jnx_gw_ctrl_policy_t * pctrl = NULL;
    jnx_gw_ctrl_intf_t * pintf = NULL;


    while ((pctrl = jnx_gw_ctrl_get_next_ctrl(pvrf, pctrl))) {

        cur_addr = pctrl->ctrl_cur_addr;

        while ((pintf = jnx_gw_ctrl_get_next_intf(pvrf, NULL, pctrl, pintf))) {
            /* whole address set is in use, jump to the
               next control policy */
            if (pintf->intf_ip == cur_addr)
                break;
        }

        /* affect the jump here */
        if (pintf) {
            continue;
        }

        /* assign the intf with the control struct */
        *intf_ip = cur_addr;

        if (pctrl->ctrl_cur_addr == pctrl->ctrl_end_addr)
            pctrl->ctrl_cur_addr = pctrl->ctrl_start_addr;
        else
            pctrl->ctrl_cur_addr++;

        jnx_gw_log(LOG_INFO, "Using control policy \"%s\" address %s",
                   pvrf->vrf_name, JNX_GW_IP_ADDRA(cur_addr));
        return pctrl;
    }

    jnx_gw_log(LOG_ERR, "Could not find a control routing instance policy "
               " \"%s\" for a new interface", pvrf->vrf_name);

    return NULL;
}

/***********************************************************
 *                                                         *
 *             CONTROL POLICY ADD/DELETE ROUTINES          *
 *                                                         *
 ***********************************************************/

/**
 * This function adds a control policy entry inside the vrf 
 * adds the entry to the vrf structure control policy db
 * initializes the data pic interfaces db
 * @param    pvrf        vrf structure pointer
 * @param    config     ipip gateway ip address
 * @returns
 *    pctrl     control policy pointer
 *    NULL      otherwise
 */
jnx_gw_ctrl_policy_t *
jnx_gw_ctrl_add_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                     jnx_gw_ctrl_policy_t *pconfig_msg)
{
    jnx_gw_ctrl_policy_t * pctrl;

    jnx_gw_log(LOG_INFO, "Control policy \"%s\" (%d) add request",
               pvrf->vrf_name, pvrf->vrf_id);

    if ((pctrl = JNX_GW_MALLOC(JNX_GW_CTRL_ID, sizeof(*pctrl))) == NULL) {
        jnx_gw_log(LOG_ERR, "Control policy \"%s\" (%d) add malloc failed",
                   pvrf->vrf_name, pvrf->vrf_id);
        goto ctrl_pol_cleanup;
    }

    pctrl->ctrl_addr       = pconfig_msg->ctrl_addr;
    pctrl->ctrl_mask       = pconfig_msg->ctrl_mask;
    pctrl->ctrl_vrf_id     = pconfig_msg->ctrl_vrf_id;
    pctrl->ctrl_key        = (pctrl->ctrl_addr &
                              pctrl->ctrl_mask);
    pctrl->pvrf            = pvrf;
    pctrl->ctrl_status     = JNX_GW_CTRL_STATUS_INIT;

    pctrl->ctrl_start_addr = pctrl->ctrl_key;
    pctrl->ctrl_end_addr   = (pctrl->ctrl_key |
                              (~pctrl->ctrl_mask));

    /* remove net addr & bcase addr, for non-host routes */
    if (pctrl->ctrl_start_addr > pctrl->ctrl_end_addr) {
        pctrl->ctrl_start_addr++;
        pctrl->ctrl_end_addr--;
    }

    pctrl->ctrl_cur_addr   = pctrl->ctrl_key + 1;

    patricia_node_init_length(&pctrl->ctrl_vrf_node, sizeof(pctrl->ctrl_key));

    if (!patricia_add(&pvrf->ctrl_policy_db, &pctrl->ctrl_vrf_node)) {
        jnx_gw_log(LOG_ERR, "Control policy \"%s\" add patricia add failed",
                   pvrf->vrf_name);
        goto ctrl_pol_cleanup;
    }

    patricia_root_init(&pctrl->ctrl_intf_db, FALSE,
                       fldsiz(jnx_gw_ctrl_intf_t, intf_id),
                       fldoff(jnx_gw_ctrl_intf_t, intf_id) -
                       fldoff(jnx_gw_ctrl_intf_t, intf_ctrl_node) -
                       fldsiz(jnx_gw_ctrl_intf_t, intf_ctrl_node));

    pvrf->ctrl_policy_count++;
    jnx_gw_log(LOG_INFO, "Control policy \"%s\" (%d) add done",
               pvrf->vrf_name, pvrf->vrf_id);
    return pctrl;

ctrl_pol_cleanup:
    jnx_gw_log(LOG_ERR, "Control policy \"%s\" (%d) add failed",
               pvrf->vrf_name, pvrf->vrf_id);
    if (pctrl) {
        JNX_GW_FREE(JNX_GW_CTRL_ID, pctrl);
    }
    return NULL;
}

/**
 * This function deletes all the interfaces currently
 * active for the control policy , frees the control policy
 * structure
 * @param    pvrf      vrf structure pointer
 * @param    pctrl     control policy structure pointer
 */
void 
jnx_gw_ctrl_clear_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                       jnx_gw_ctrl_policy_t * pctrl)
{
    jnx_gw_ctrl_data_pic_t * pdata_pic;
    jnx_gw_ctrl_intf_t * pintf;

    jnx_gw_log(LOG_INFO, "Control policy \"%s\" (%d) delete",
               pvrf->vrf_name, pvrf->vrf_id);

    /* clear all the interfaces associated,
     * with this control policy
     */
    while ((pintf = jnx_gw_ctrl_get_first_intf(NULL, NULL, pctrl))) {
        pdata_pic = pintf->pdata_pic;
        jnx_gw_ctrl_delete_intf(pvrf, pdata_pic, pintf);
    }
    patricia_delete(&pvrf->ctrl_policy_db, &pctrl->ctrl_vrf_node);
    JNX_GW_FREE(JNX_GW_CTRL_ID, pctrl);
}

/**
 * This function clears the control policy, deletes the 
 * entry from the vrf control policy db
 * @param    pvrf      vrf structure pointer
 * @param    pctrl     control policy structure pointer
 */
status_t
jnx_gw_ctrl_delete_ctrl(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_policy_t * pctrl)
{
    jnx_gw_ctrl_clear_ctrl(pvrf, pctrl);
    pvrf->ctrl_policy_count--;
    return EOK;
}

/**
 * This function modifies the control policy, 
 * @param    pvrf      vrf structure pointer
 * @param    pctrl     control policy structure pointer
 * @param    pconfig   control policy config structure pointer
 * @returns
 *    pctrl   control policy structure pointer
 */
jnx_gw_ctrl_policy_t *
jnx_gw_ctrl_modify_ctrl(jnx_gw_ctrl_vrf_t * pvrf __unused,
                        jnx_gw_ctrl_policy_t * pctrl __unused,
                        jnx_gw_ctrl_policy_t * pconfig_msg __unused)
{
    return pctrl;
}

/***********************************************************
 *                                                         *
 *             USER POLICY MANAGEMENT ROUTINES             *
 *                                                         *
 ***********************************************************/

/***********************************************************
 *                                                         *
 *             USER POLICY LOOKUP ROUTINES                 *
 *                                                         *
 ***********************************************************/

/**
 * This function returns the first user policy 
 * @returns
 *     puser    if found
 *     NULL     otherwise
 */
jnx_gw_ctrl_user_t *
jnx_gw_ctrl_get_first_user(void)
{
    patnode * pnode;
    if ((pnode = patricia_find_next(&jnx_gw_ctrl.user_db, NULL))) {
        return jnx_gw_ctrl_user_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next user policy for a given
 * user profile policy
 * @params    puser    current user profile pointer
 * @returns
 *     puser    if found
 *     NULL     otherwise
 */
jnx_gw_ctrl_user_t *
jnx_gw_ctrl_get_next_user(jnx_gw_ctrl_user_t * puser)
{
    patnode * pnode;
    if (puser == NULL) {
        return jnx_gw_ctrl_get_first_user();
    }
    if ((pnode = patricia_find_next(&jnx_gw_ctrl.user_db,
                                    &puser->user_node))) {
        return jnx_gw_ctrl_user_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the user policy for a user name
 * @params  user_name   user policy name
 * @returns
 *     puser    if found
 *     NULL     otherwise
 */
jnx_gw_ctrl_user_t *
jnx_gw_ctrl_lookup_user(uint8_t * user_name)
{
    patnode * pnode;

    if (!user_name) {
        return NULL;
    }

    if ((pnode = patricia_get(&jnx_gw_ctrl.user_db,
                              strlen(user_name), user_name))) {
        return jnx_gw_ctrl_user_entry(pnode);
    }

    return NULL;
}

/***********************************************************
 *                                                         *
 *             USER POLICY USE ROUTINES                    *
 *                                                         *
 ***********************************************************/

/**
 * This function tries to find a matching user policy
 * for a new gre session
 * extracts the egress vrf, ipip gateway information
 * if successful
 * @params    pgre_session   gre session pointer
 * @returns
 *     EOK    if found
 *     EFAIL  otherwise
 */
status_t 
jnx_gw_ctrl_match_user(jnx_gw_ctrl_gre_session_t * pgre_session)
{
    jnx_gw_ctrl_user_t * puser = NULL;
    jnx_gw_ctrl_ipip_gw_t * pipip_gw = NULL;
    uint32_t sesn_subnet = 0;


    while ((puser = jnx_gw_ctrl_get_next_user(puser))) {

        sesn_subnet = (pgre_session->sesn_client_ip & puser->user_mask);

        if (puser->user_key != sesn_subnet) {
            jnx_gw_log(LOG_DEBUG, "User \"%s\" does not match %x:%x",
                       puser->user_name, puser->user_key, sesn_subnet);
            continue;
        }

        /*
         * did find a valid 5 tuple session entry with this user
         * policy, could not use this policy for the current
         * session
         */
        if (jnx_gw_ctrl_lookup_user_gre_session(puser,
                                                (jnx_gw_ctrl_session_info_t *)
                                                &pgre_session->sesn_proto)) {
            jnx_gw_log(LOG_ERR, "User \"%s\" has "
                       " an existing session", puser->user_name);
            continue;
        }

        /*
         * IPIP Gatway has a valid session entry with the 
         * same five tuple, skip this one
         */

        if (!puser->pipip_gw) {
            continue;
        }

         if (jnx_gw_ctrl_get_ipip_gre_session(puser->pipip_gw,
                                              (jnx_gw_ctrl_session_info_t *)
                                              &pgre_session->sesn_proto)) {
             jnx_gw_log(LOG_ERR, "IPIP Gateway \"%s\" %s has"
                        " an existing session", puser->user_name,
                        JNX_GW_IP_ADDRA(puser->pipip_gw->ipip_gw_ip));
             continue;
         }

        /*
         * Now zeroed on a user policy
         */

        pipip_gw = puser->pipip_gw;

        pgre_session->puser          = puser;
        pgre_session->pegress_vrf    = puser->pvrf;
        pgre_session->pipip_gw       = puser->pipip_gw;
        pgre_session->egress_gw_ip   = puser->user_ipip_gw_ip;
        pgre_session->egress_vrf_id  = puser->user_evrf_id;

        jnx_gw_log(LOG_DEBUG, "User policy \"%s\" selected"
                   " for a new GRE session", puser->user_name);
        return EOK;
    }

    jnx_gw_log(LOG_ERR, "Could not find a user policy for GRE session");
    return EFAIL;
}

/***********************************************************
 *                                                         *
 *             USER POLICY ADD/DELETE ROUTINES             *
 *                                                         *
 ***********************************************************/
/**
 * This function adds a user policy on receipt of 
 * user policy add message from the management module
 * @params    pvrf         vrf structure pointer
 * @params    pipip_gw     ipip gateway structure pointer
 * @params    user config  user policy configuration
 * @returns
 *     puser  if successful
 *     NULL   otherwise
 */
jnx_gw_ctrl_user_t * 
jnx_gw_ctrl_add_user(jnx_gw_ctrl_vrf_t * pvrf,
                     jnx_gw_ctrl_ipip_gw_t * pipip_gw,
                     jnx_gw_ctrl_user_t * pconfig_msg)
{
    jnx_gw_ctrl_user_t * puser;
    uint32_t prefix_len;


    jnx_gw_log(LOG_INFO, "User policy \"%s\" add", pconfig_msg->user_name);

    if ((puser = JNX_GW_MALLOC(JNX_GW_CTRL_ID, sizeof(*puser))) == NULL) {
        jnx_gw_log(LOG_ERR, "User policy \"%s\" add malloc failed",
               pconfig_msg->user_name);
        goto gw_user_cleanup;
    }

    strncpy(puser->user_name, pconfig_msg->user_name,
            sizeof(puser->user_name));

    puser->user_status     = JNX_GW_CTRL_STATUS_INIT;
    puser->user_addr       = pconfig_msg->user_addr;
    puser->user_mask       = pconfig_msg->user_mask;
    puser->user_key        = (pconfig_msg->user_addr & pconfig_msg->user_mask);
    puser->user_evrf_id    = pconfig_msg->user_evrf_id;
    puser->user_ipip_gw_ip = pconfig_msg->user_ipip_gw_ip;

    puser->pvrf            = pvrf;
    puser->pipip_gw        = pipip_gw;

    patricia_node_init_length(&puser->user_node, strlen(puser->user_name));

    if (!patricia_add(&jnx_gw_ctrl.user_db, &puser->user_node)) {
        jnx_gw_log(LOG_ERR, "User policy \"%s\" add patricia add failed",
                   pconfig_msg->user_name);
        goto gw_user_cleanup;
    }

    patricia_root_init(&puser->gre_sesn_db, FALSE, 
                       sizeof(jnx_gw_ctrl_session_info_t),
                       fldoff(jnx_gw_ctrl_gre_session_t, sesn_proto) -
                       fldoff(jnx_gw_ctrl_gre_session_t, gre_user_sesn_node) -
                       fldsiz(jnx_gw_ctrl_gre_session_t, gre_user_sesn_node));

    jnx_gw_ctrl.user_count++;

    if (pipip_gw) {
        pipip_gw->user_policy_count++;
    }
    
    prefix_len = jnx_gw_prefix_len(puser->user_mask);

    if (prefix_len < jnx_gw_ctrl.min_prefix_len) {
        jnx_gw_ctrl.min_prefix_len = prefix_len;
    }

    jnx_gw_log(LOG_INFO, "User policy \"%s\" %s/%d add done",
               pconfig_msg->user_name, JNX_GW_IP_ADDRA(puser->user_addr),
               prefix_len);

    return puser;

gw_user_cleanup:
    jnx_gw_log(LOG_ERR, "User policy \"%s\" add failed",
               pconfig_msg->user_name);

    if (puser) {
        JNX_GW_FREE(JNX_GW_CTRL_ID, puser);
    }

    return NULL;
}

/**
 * This function clears all the gre sessions cactive
 * for the user policy
 * deletes the user policy from the user profile db
 * frees the memory
 * @params    pvrf      vrf structure pointer
 * @params    puser     user profile structure pointer
 */
void
jnx_gw_ctrl_clear_user(jnx_gw_ctrl_vrf_t * pvrf, jnx_gw_ctrl_user_t * puser)
{
    jnx_gw_ctrl_gre_gw_t * pgre_gw;
    jnx_gw_ctrl_gre_session_t * pgre_session;

    jnx_gw_log(LOG_INFO, "User policy \"%s\" delete", puser->user_name);

    /* clear sessions if any */
    while ((pgre_session = jnx_gw_ctrl_get_first_gre_session(NULL, NULL, NULL,
                                                       puser, TRUE))) {
        /* set the session status to fail */
        pgre_session->sesn_status = JNX_GW_CTRL_STATUS_FAIL;
        pgre_gw                = pgre_session->pgre_gw;
        jnx_gw_ctrl_delete_gre_session(pvrf, pgre_gw, pgre_session);
    }

    /*
     * handle the attach the ipip gateway, if the usage count
     * reaches zero, delete it 
     */
    if (puser->pipip_gw) {
        puser->pipip_gw->user_policy_count--;
        if (!puser->pipip_gw->user_policy_count) {
            jnx_gw_ctrl_delete_ipip_gw(puser->pvrf, puser->pipip_gw);
        }
    }
    /* delete from the data base */
    if (!patricia_delete(&jnx_gw_ctrl.user_db, &puser->user_node)) {
        jnx_gw_log(LOG_ERR, "User policy \"%s\" patricia delete failed",
                   puser->user_name);
    }
    JNX_GW_FREE(JNX_GW_CTRL_ID, puser);
}

/**
 * This function clears the user policy
 * updates the statistics
 * @params    pvrf      vrf structure pointer
 * @params    puser     user profile structure pointer
 */
status_t
jnx_gw_ctrl_delete_user(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_user_t * puser)
{
    jnx_gw_ctrl_clear_user(pvrf, puser);
    jnx_gw_ctrl_send_gw_gre_msgs();
    jnx_gw_ctrl_send_data_pic_msgs();
    jnx_gw_ctrl.user_count--;
    return EOK;
}

/**
 * This function modifies a user policy
 * @params    pvrf        vrf structure pointer
 * @params    puser       user profile structure pointer
 * @params    user_config new user profile structure pointer
 */
status_t
jnx_gw_ctrl_modify_user(jnx_gw_ctrl_vrf_t * pvrf __unused,
                        jnx_gw_ctrl_user_t * puser __unused,
                        jnx_gw_ctrl_user_t * pconfig_msg __unused)
{
    return EOK;
}

/* VRF data pic/client route addition/deletions */
/***********************************************************
 *                                                         *
 *             ROUTE MANAGEMENT ROUTINES                   *
 *                                                         *
 ***********************************************************/

/* ssd event/message handler routines */

/**
 * This function handles the connect event for ssd module
 * @params  
 *     fd     ssd fd
 */
int
jnx_gw_ctrl_ssd_connect_handler(int fd)
{
    ssd_get_client_id(fd, 0);
    return EOK;
}

/**
 * This function handles the close event for ssd module
 * @params  
 *     fd     ssd fd
 *     cause  cause
 */
void
jnx_gw_ctrl_ssd_close_handler(int fd __unused, int cause __unused)
{
    /* mark the status as DOWN for the control agent */
    jnx_gw_ctrl.rt_status = JNX_GW_CTRL_STATUS_DOWN;
    return;
}


/**
 * This function handles the ssd module messages
 * @params  
 *     fd     ssd fd
 *     pmsg   ssd ipc message
 */
void
jnx_gw_ctrl_ssd_msg_handler(int fd __unused, struct ssd_ipc_msg * pmsg)
{
    uint32_t cookie;
    jnx_gw_ctrl_vrf_t   *pvrf;
    jnx_gw_ctrl_intf_t  *pintf;
    jnx_gw_ctrl_route_t *proute;

    switch(pmsg->ssd_ipc_cmd) {

        case SSD_SESSION_ID_REPLY:

            /* session setup failed */
            if (!SSD_SESSION_ID_REPLY_CHECK_STATUS(pmsg)) {

                jnx_gw_ctrl.rt_status = JNX_GW_CTRL_STATUS_DOWN;
                jnx_gw_log(LOG_ERR, "SSD Client Session ID request fail");
                break;
            }
            /* store the session id */
            jnx_gw_ctrl.ssd_id = SSD_SESSION_ID_REPLY_GET_CLIENT_ID(pmsg);

            jnx_gw_log(LOG_DEBUG, "SSD Client Session ID (%d) reply success",
                       jnx_gw_ctrl.ssd_id);

            /* set up route service */
            ssd_setup_route_service(jnx_gw_ctrl.ssd_fd);
            break;

        case SSD_ROUTE_SERVICE_REPLY:

            /* the route service setup failed */
            if (!SSD_ROUTE_SERVICE_REPLY_CHECK_STATUS(pmsg)) {

                jnx_gw_ctrl.rt_status = JNX_GW_CTRL_STATUS_DOWN;
                jnx_gw_log(LOG_ERR, "SSD Route Service request fail");
                break;
            }

            /* mark the status as UP for the control agent */
            jnx_gw_ctrl.rt_status = JNX_GW_CTRL_STATUS_UP;

            /* request for vrf route table ids */
            for (pvrf = jnx_gw_ctrl_get_first_vrf(); (pvrf);
                 pvrf = jnx_gw_ctrl_get_next_vrf(pvrf)) {
                jnx_gw_ctrl_add_vrf(pvrf->vrf_id, pvrf->vrf_name);
            }

            jnx_gw_log(LOG_DEBUG, "SSD Route Service reply success");
            break;

        case SSD_ROUTE_SERVICE_CLEAN_UP_REPLY:

            /* route service cleanup failed */

            if (!SSD_ROUTE_SERVICE_CLEAN_UP_REPLY_CHECK_STATUS(pmsg)) {
                jnx_gw_log(LOG_ERR, "SSD Route Service cleanup request fail");
                break;
            }

            jnx_gw_ctrl.rt_status = JNX_GW_CTRL_STATUS_DOWN;
            jnx_gw_log(LOG_DEBUG, "SSD Route Service cleanup reply success");
            break;

        case SSD_ROUTE_TABLE_LOOKUP_REPLY:
            /* get the vrf pointer */
            pvrf =
                (typeof(pvrf))SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_CLIENT_CTX(pmsg);

            if (!SSD_ROUTE_TABLE_LOOKUP_REPLY_CHECK_STATUS(pmsg)) {

                pvrf->vrf_rt_status = JNX_GW_CTRL_STATUS_FAIL;

                jnx_gw_log(LOG_ERR,
                           "SSD Route Table lookup \"%s\" request fail",
                           pvrf->vrf_name);
                break;
            }

            pvrf->vrf_rt_status = JNX_GW_CTRL_STATUS_UP;
            pvrf->vrf_rtid      = SSD_ROUTE_TABLE_LOOKUP_REPLY_GET_RTTID(pmsg);

            jnx_gw_log(LOG_DEBUG, "SSD Route Table lookup for \"%s\""
                       " successful %d",
                       pvrf->vrf_name, pvrf->vrf_rtid);

            for (pintf = jnx_gw_ctrl_get_first_intf(pvrf, NULL, NULL); (pintf);
                pintf = jnx_gw_ctrl_get_next_intf(pvrf, NULL, NULL, pintf)) {
                if (pintf->pdata_pic->pic_status == JNX_GW_CTRL_STATUS_UP)
                    jnx_gw_ctrl_add_nexthop(pvrf, pintf);
            }

            break;

        case SSD_NH_ADD_REPLY:

            /* get the interface pointer */
            pintf = (typeof(pintf)) SSD_NEXTHOP_ADD_REPLY_GET_CLIENT_CTX(pmsg);

            /* next hop add failed */
            if (!SSD_NEXTHOP_ADD_REPLY_CHECK_STATUS(pmsg)) {

                if (pintf->intf_status == JNX_GW_CTRL_NH_DELETE_PENDING) {

                    JNX_GW_FREE(JNX_GW_CTRL_ID, pintf);
                    break;
                }

                jnx_gw_log(LOG_ERR, "SSD Nexthop add request %s fail",
                           pintf->intf_name);

                pintf->intf_status = JNX_GW_CTRL_STATUS_FAIL;
                break;
            }

            /* set the status as UP, & store the nexthop index */
            pintf->intf_nhid   = *((typeof(&pintf->intf_nhid))&
                                   SSD_NEXTHOP_ADD_REPLY_GET_NH_ID(pmsg));

            /*
             * handle the interface down event, if it has got
             * deleted in between an request event, & response
             */

            if (pintf->intf_status == JNX_GW_CTRL_NH_DELETE_PENDING) {

                pintf->intf_status = JNX_GW_CTRL_STATUS_UP;

                jnx_gw_ctrl_delete_nexthop(NULL, pintf);

                JNX_GW_FREE(JNX_GW_CTRL_ID, pintf);

                break;
            }

            pintf->intf_status = JNX_GW_CTRL_STATUS_UP;

            pvrf = pintf->pvrf;

            jnx_gw_log(LOG_DEBUG, "SSD Nexthop add \"%s.%d\" (%d) "
                       "on \"%s\" successful",
                       pintf->intf_name, pintf->intf_subunit,
                       *(uint32_t*)&pintf->intf_nhid, pvrf->vrf_name);

            /* add the interface route */
            pintf->intf_rt = jnx_gw_ctrl_add_route(pvrf, pintf, pintf->intf_rt, 
                                                   pintf->intf_ip,
                                                   JNX_GW_CTRL_INTF_RT);

            /*
             * now try adding routes for the interface
             * any existing routes
             */
            for (proute = jnx_gw_ctrl_route_get_first(pvrf, pintf); (proute);
                 proute = jnx_gw_ctrl_route_get_next(pvrf, pintf, proute)) {

                if (proute == pintf->intf_rt) {
                    continue;
                }
                jnx_gw_ctrl_add_route(pvrf, pintf, proute, 0, 0);
            }

            break;

        case SSD_NH_DELETE_REPLY:

            cookie = SSD_NEXTHOP_DELETE_REPLY_GET_CLIENT_CTX(pmsg);

            /* next hop delete failed */
            if (!SSD_NEXTHOP_DELETE_REPLY_CHECK_STATUS(pmsg)) {
                jnx_gw_log(LOG_ERR, "SSD Nexthop delete %d request fail",
                           cookie);
                break;
            }

            jnx_gw_log(LOG_DEBUG, "SSD Nexthop delete %d successful", cookie);
            break;

        case SSD_ROUTE_ADD_REPLY:

            proute = (typeof(proute))SSD_ROUTE_ADD_REPLY_GET_CLIENT_CTX(pmsg);

            /* route add failed */
            if (!SSD_ROUTE_ADD_REPLY_CHECK_STATUS(pmsg)) {

                /* scheduled for delete, free the route memory */
                if (proute->status == JNX_GW_CTRL_RT_DELETE_PENDING) {
                    JNX_GW_FREE(JNX_GW_CTRL_ID, proute->rtp.rta_dest);
                    JNX_GW_FREE(JNX_GW_CTRL_ID, proute);
                    break;
                }

                jnx_gw_log(LOG_ERR, "SSD Route add %s request fail",
                           JNX_GW_IP_ADDRA(proute->addr));

                proute->status = JNX_GW_CTRL_STATUS_FAIL;
                jnx_gw_ctrl_delete_route(NULL, proute->pintf, proute);
                break;
            }

            jnx_gw_log(LOG_DEBUG, "SSD Route add %s successful",
                         JNX_GW_IP_ADDRA(proute->addr));

            /*
             * delete has been scheduled, try sending the delete
             * message to ssd
             */
            if (proute->status == JNX_GW_CTRL_RT_DELETE_PENDING) {
                proute->status = JNX_GW_CTRL_STATUS_UP;
                /* has already been cleared from the interface database */
                jnx_gw_ctrl_delete_route(NULL, NULL, proute);
            } else {
                proute->status = JNX_GW_CTRL_STATUS_UP;
            }
            break;

        case SSD_ROUTE_DELETE_REPLY:
            cookie = SSD_ROUTE_DELETE_REPLY_GET_CLIENT_CTX(pmsg);
            if (!SSD_ROUTE_DELETE_REPLY_CHECK_STATUS(pmsg)) {
                jnx_gw_log(LOG_ERR, "SSD Route delete %s failed",
                           JNX_GW_IP_ADDRA(cookie));
                break;
            }
            jnx_gw_log(LOG_DEBUG, "SSD Route delete %s successful",
                       JNX_GW_IP_ADDRA(cookie));

            break;

        default: /* do not know what is this message */
            jnx_gw_log(LOG_ERR, "SSD Unknown message");
            break;
    }
}

/**
 * This function adds a next hop for an interface
 * @params  
 *     pvrf    vrf structure pointer
 *     pintf   interface structure pointer
 * @returns  status
 */
status_t
jnx_gw_ctrl_add_nexthop(jnx_gw_ctrl_vrf_t * pvrf,
                        jnx_gw_ctrl_intf_t * pintf)
{
    struct ssd_nh_add_parms nh_params;

    if ((pintf->intf_status == JNX_GW_CTRL_STATUS_UP) ||
        (pintf->intf_status == JNX_GW_CTRL_NH_ADD_PENDING)) {
        return (EOK);
    }

    /*
     * ssd route service is still not up, or, the vrf
     * has still not resolved its route table id,
     * return
     */

    if ((jnx_gw_ctrl.rt_status != JNX_GW_CTRL_STATUS_UP) ||
        (pvrf->vrf_rt_status != JNX_GW_CTRL_STATUS_UP)) {
        return (EOK);
    }

    pintf->intf_status = JNX_GW_CTRL_NH_ADD_PENDING;
    /* send the interface index for creating the service
     * next hop as the service pic ifl 
     */

    jnx_gw_log(LOG_INFO, "Nexthop \"%s.%d\" add request",
               pintf->intf_name, pintf->intf_subunit);

    ifl_idx_t_setval(nh_params.ifl, pintf->intf_id);
    nh_params.opq_data.len  = 0;
    nh_params.opq_data.data = NULL;
    nh_params.pkt_dist_type = SSD_NEXTHOP_PKT_DIST_RR;
    nh_params.af = SSD_GF_INET;

    ssd_request_nexthop_add(jnx_gw_ctrl.ssd_fd, pintf->intf_id,
                            &nh_params, (uint32_t)pintf);
    return (EOK);
}

/**
 * This function deletes the next hop for an interface
 * @params  
 *     pvrf    vrf structure pointer
 *     pintf   interface structure pointer
 * @returns  status
 */
status_t
jnx_gw_ctrl_delete_nexthop(jnx_gw_ctrl_vrf_t * pvrf __unused,
                           jnx_gw_ctrl_intf_t * pintf)
{
    /* schedule delete, when we receive the add response */
    if (pintf->intf_status == JNX_GW_CTRL_NH_ADD_PENDING) {
        pintf->intf_status = JNX_GW_CTRL_NH_DELETE_PENDING;
        return (EOK);
    }

    /* next hop was really been added */
    if  (pintf->intf_status == JNX_GW_CTRL_STATUS_UP) {

        /* delete the next hop for the service pic ifl */
        ssd_request_nexthop_delete(jnx_gw_ctrl.ssd_fd, pintf->intf_id,
                                   pintf->intf_nhid, pintf->intf_id);
    }
    return (EOK);
}

/**
 * This function gets the route for a prefix on an interface
 * @params  
 *     pvrf    vrf structure pointer
 *     pintf   interface structure pointer
 *     key     ip address prefix
 * @returns  proute   route structure pointer
 */
jnx_gw_ctrl_route_t * 
jnx_gw_ctrl_route_lookup(jnx_gw_ctrl_vrf_t * pvrf __unused,
                         jnx_gw_ctrl_intf_t * pintf,
                         uint32_t key)
{
    patnode * pnode;
    if ((pnode = patricia_get(&pintf->intf_route_db, sizeof(key),
                              &key))) {
        return jnx_gw_ctrl_route_entry(pnode);
    }
    return NULL;
}

/**
 * This function gets the first route configured on the interface
 * @params  
 *     pvrf     vrf structure pointer
 *     pintf    interface structure pointer
 * @returns  proute   route structure pointer
 */
jnx_gw_ctrl_route_t * 
jnx_gw_ctrl_route_get_first(jnx_gw_ctrl_vrf_t * pvrf __unused,
                            jnx_gw_ctrl_intf_t * pintf)
{
    patnode * pnode;

    if ((pnode = patricia_find_next(&pintf->intf_route_db, NULL))) {
        return jnx_gw_ctrl_route_entry(pnode);
    }
    return NULL;
}

/**
 * This function get a next route for a given route
 * @params 
 *     pvrf     vrf structure pointer
 *     pintf    interface structure pointer
 *     proute   route structure pointer   
 * @returns  proute   route structure pointer
 */
jnx_gw_ctrl_route_t * 
jnx_gw_ctrl_route_get_next(jnx_gw_ctrl_vrf_t * pvrf __unused,
                            jnx_gw_ctrl_intf_t * pintf,
                            jnx_gw_ctrl_route_t * proute)
{
    patnode * pnode;

    if (proute == NULL) {
        return jnx_gw_ctrl_route_get_first(pvrf, pintf);
    }

    if ((pnode = patricia_find_next(&pintf->intf_route_db,
                                    &proute->route_node))) {
        return jnx_gw_ctrl_route_entry(pnode);
    }
    return NULL;
}

/**
 * This function add a route for an ifl/client
 * @params
 *     pvrf     vrf structure pointer
 *     pintf    interface structure pointer
 *     proute   route structure pointer   
 *     dest_ip  ip prefix
 *     rt_flags ip route flags,
 *              (whether its an ifl route, or a client route)
 * @returns  proute   route structure pointer
 */
jnx_gw_ctrl_route_t *
jnx_gw_ctrl_add_route(jnx_gw_ctrl_vrf_t * pvrf,
                      jnx_gw_ctrl_intf_t * pintf,
                      jnx_gw_ctrl_route_t *proute,
                      uint32_t dest_ip, uint32_t rt_flags)
{
    char intf_name[JNX_GW_STR_SIZE];
    ssd_sockaddr_un * dest = NULL;
    ifl_idx_t ifl;

    /*
     * schedule route add
     */
    if (proute != NULL) {

        /*
         * these are transient status for the route, do not
         * do a route add on these states
         */
        if ((jnx_gw_ctrl.rt_status != JNX_GW_CTRL_STATUS_UP) ||
            (pvrf->vrf_rt_status != JNX_GW_CTRL_STATUS_UP) ||
            (pintf->intf_status != JNX_GW_CTRL_STATUS_UP)) {
            return proute;
        }

        if ((proute->status == JNX_GW_CTRL_STATUS_UP) ||
            (proute->status == JNX_GW_CTRL_RT_ADD_PENDING) ||
            (proute->status == JNX_GW_CTRL_RT_DELETE_PENDING))  {
            return proute;
        }

        /*
         * if the route is not scheduled for delete, 
         * and the route status is not UP,
         * schedule a route add request
         */

        proute->status = JNX_GW_CTRL_RT_ADD_PENDING;
        ssd_request_route_add(jnx_gw_ctrl.ssd_fd, &proute->rtp,
                              (uint32_t)proute);
        return proute;
    }

    jnx_gw_log(LOG_INFO, "Route %s add request on \"%s.%d\"(%d)",
               JNX_GW_IP_ADDRA(dest_ip),
               pintf->intf_name, pintf->intf_subunit, 
               *(int*)&pintf->intf_nhid);

    if ((dest = JNX_GW_MALLOC(JNX_GW_CTRL_ID, sizeof(*dest))) == NULL) {
        jnx_gw_log(LOG_ERR, "Route %s add destination malloc failed",
                   JNX_GW_IP_ADDRA(dest_ip));
        return NULL;
    }

    if ((proute = JNX_GW_MALLOC(JNX_GW_CTRL_ID, sizeof(*proute))) == NULL) {
        /*
         * free destination structure
         */
        JNX_GW_FREE(JNX_GW_CTRL_ID, dest);
        jnx_gw_log(LOG_ERR, "Route %s add route malloc failed",
                   JNX_GW_IP_ADDRA(dest_ip));
        return NULL;
    }

    /*
     * set the ifl index value
     */
    ifl_idx_t_setval(ifl, pintf->intf_id);

    /*
     * set up the route structure values
     */
    proute->key      = dest_ip;
    proute->addr     = dest_ip;
    proute->mask     = JNX_GW_HOST_MASK;
    proute->intf_id  = pintf->intf_id;
    proute->vrf_id   = pvrf->vrf_id;
    proute->pintf    = pintf;
    proute->flags    = rt_flags;
    proute->status   = JNX_GW_CTRL_STATUS_INIT;


    /*
     * do not do htonx for anything but destination ip 
     * set the destination ip
     */
    ssd_setsocktype(dest, SSD_GF_INET);
    dest->in.gin_addr = ntohl(dest_ip);
    dest->in.gin_len  = sizeof(*dest);

    /*
     * set the route structure
     */
    proute->rtp.rta_dest        = dest;
    proute->rtp.rta_prefixlen   = JNX_GW_DFLT_PREFIX_LEN;
    proute->rtp.rta_state       = 0;
    proute->rtp.rta_gw_handle   = 0;

    /*
     * set the metrics for the route, ssd requires atleast
     * one set of preferences set
     */
    proute->rtp.rta_preferences.rtm_type[0] = SSD_RTM_VALUE_PRESENT;
    proute->rtp.rta_preferences.rtm_val[0] = 1;

    /*
     * set the vrf-table id for the route
     */
    proute->rtp.rta_rtt         = pvrf->vrf_rtid;
    proute->rtp.rta_flags       = 0;

    /*
     * only one service nexthop gateway 
     * set the next hop as service nexthop
     */
    proute->rtp.rta_n_gw        = JNX_GW_DFLT_NUM_GATEWAYS;
    proute->rtp.rta_nhtype      = SSD_RNH_UNICAST;

    sprintf(intf_name,"%s.%d", pintf->intf_name, pintf->intf_subunit);

    /*
     * now populate the interface name, indices into the
     * nexthop gateway structure
     */
    proute->rtp.rta_gateway[0].rtg_gwaddr  = NULL;
    proute->rtp.rta_gateway[0].rtg_ifl     = ifl;
    proute->rtp.rta_gateway[0].rtg_iflname = intf_name;
    proute->rtp.rta_gateway[0].rtg_lcladdr = NULL;
    proute->rtp.rta_gateway[0].rtg_nhindex = pintf->intf_nhid;

    /*
     * add the route to the interface route database
     */
    patricia_node_init_length(&proute->route_node, sizeof(proute->key));

    if (!patricia_add(&pintf->intf_route_db, &proute->route_node)) {
        jnx_gw_log(LOG_ERR, "Route %s add request patricia add failed",
                   JNX_GW_IP_ADDRA(proute->addr));
        goto gw_route_cleanup;
    }

    if ((jnx_gw_ctrl.rt_status != JNX_GW_CTRL_STATUS_UP) ||
        (pvrf->vrf_rt_status != JNX_GW_CTRL_STATUS_UP) ||
        (pintf->intf_status != JNX_GW_CTRL_STATUS_UP)) {
        jnx_gw_log(LOG_ERR, "Route Service is not up ctrl %d,"
                   " vrf %d, intf %d",
                   jnx_gw_ctrl.rt_status, pvrf->vrf_rt_status,
                   pintf->intf_status);
        return proute;
    }


    /*
     * now send this message across to the ssd server module, for
     * setting the route for dirverting intended traffic to the
     * data pic
     */

    if (rt_flags == JNX_GW_CTRL_CLIENT_RT) {
        jnx_gw_log(LOG_INFO, "Client Route %s add request on \"%s.%d\" %d",
                   JNX_GW_IP_ADDRA(proute->addr),
                   pintf->intf_name, pintf->intf_subunit,
                   *(uint32_t*)&pintf->intf_nhid);
    } else {
        jnx_gw_log(LOG_INFO, "Route %s add request for \"%s.%d\" %d",
                   JNX_GW_IP_ADDRA(proute->addr),
                   pintf->intf_name, pintf->intf_subunit,
                   *(uint32_t*)&pintf->intf_nhid);
    }

    proute->status   = JNX_GW_CTRL_STATUS_INIT;

    if (ssd_request_route_add(jnx_gw_ctrl.ssd_fd, &proute->rtp,
                             (uint32_t)proute) < 0) {
        patricia_delete(&pintf->intf_route_db, &proute->route_node);
        goto gw_route_cleanup;
    }

    return (proute);

gw_route_cleanup:
    /*
     * free route structure
     */
    if (dest)  {
        JNX_GW_FREE(JNX_GW_CTRL_ID, dest);
    }

    if (proute) {
        JNX_GW_FREE(JNX_GW_CTRL_ID, proute);
    }
    jnx_gw_log(LOG_INFO, "Route %s add request failed",
               JNX_GW_IP_ADDRA(dest_ip));
    return NULL;
}

/**
 * This function deletes a route for an ifl/client
 * @params  
 *     pvrf    vrf structure pointer
 *     pintf   interface structure pointer
 *     proute  route structure pointer   
 * @returns status
 */
status_t
jnx_gw_ctrl_delete_route(jnx_gw_ctrl_vrf_t * pvrf __unused,
                         jnx_gw_ctrl_intf_t * pintf, 
                         jnx_gw_ctrl_route_t * proute)
{
    struct ssd_rt_delete_parms rtd;

    jnx_gw_log(LOG_INFO, "Route %s delete request",
               JNX_GW_IP_ADDRA(proute->addr));

    /* remove this route entry from the interface context */

    if ((pintf) && (pintf->intf_rt == proute)) {
        pintf->intf_rt = NULL;
    }

    /* add request has been posted, schedule delete, & return */
    if (proute->status == JNX_GW_CTRL_RT_ADD_PENDING)  {

        jnx_gw_log(LOG_INFO, "Route %s add/delete request pending",
                   JNX_GW_IP_ADDRA(proute->addr));

        proute->status = JNX_GW_CTRL_RT_DELETE_PENDING;
        /*
         * neverth the less remove this route from the 
         * interface database
         */
        if (pintf) {
            patricia_delete(&pintf->intf_route_db, &proute->route_node);
        }
        return (EOK);
    }

    /*
     * now post a delete request for the route, only
     * if the route add was succcessful
     */

    if (proute->status == JNX_GW_CTRL_STATUS_UP)  {

        proute->status = JNX_GW_CTRL_STATUS_DELETE;
        memset(&rtd, 0, sizeof(rtd));
        rtd.rtd_dest      = proute->rtp.rta_dest;
        rtd.rtd_prefixlen = proute->rtp.rta_prefixlen;
        rtd.rtd_rtt       = proute->rtp.rta_rtt;
        rtd.rtd_gw_handle = 0;

        /* send the delete request to the ssd module */
        ssd_request_route_delete(jnx_gw_ctrl.ssd_fd, &rtd, proute->addr);
    }

    if (pintf) {
        patricia_delete(&pintf->intf_route_db, &proute->route_node);
    }

    JNX_GW_FREE(JNX_GW_CTRL_ID, proute->rtp.rta_dest);
    JNX_GW_FREE(JNX_GW_CTRL_ID, proute);
    return (EOK);
}

/* VRF additions/deletions are implicit */

/***********************************************************
 *                                                         *
 *             VRF MANAGEMENT ROUTINES                     *
 *                                                         *
 ***********************************************************/

/***********************************************************
 *                                                         *
 *             VRF LOOKUP ROUTINES                         *
 *                                                         *
 ***********************************************************/

/**
 * This function returns the vrf for a vrf index
 * @params  vrf_id   vrf index
 * @returns
 *     pvrf    if found
 *     NULL    otherwise
 */
jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_lookup_vrf(uint32_t vrf_id)
{
    patnode * pnode;

    if ((pnode = patricia_get(&jnx_gw_ctrl.vrf_db, sizeof(vrf_id), &vrf_id))) {
        return jnx_gw_ctrl_vrf_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the first vrf
 * @returns
 *     pvrf    if found
 *     NULL    otherwise
 */
jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_get_first_vrf(void)
{
    patnode * pnode;

    if ((pnode = patricia_find_next(&jnx_gw_ctrl.vrf_db, NULL))) {
        return jnx_gw_ctrl_vrf_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next vrf for a given
 * vrf policy
 * @params    pvrf    current vrf pointer
 * @returns
 *     pvrf    if found
 *     NULL    otherwise
 */
jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_get_next_vrf(jnx_gw_ctrl_vrf_t * pvrf)
{
    patnode * pnode;
    if (pvrf == NULL) {
        return jnx_gw_ctrl_get_first_vrf();
    }
    if ((pnode = patricia_find_next(&jnx_gw_ctrl.vrf_db,
                                    &pvrf->vrf_node))) {
        return jnx_gw_ctrl_vrf_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the first vrf
 * @returns
 *     pvrf    if found
 *     NULL    otherwise
 */
jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_thread_get_first_vrf(jnx_gw_ctrl_sock_list_t * psocklist)
{
    patnode * pnode;
    if ((pnode = patricia_find_next(&psocklist->recv_vrf_db, NULL))) {
        return jnx_gw_ctrl_thread_vrf_entry(pnode);
    }
    return NULL;
}

/**
 * This function returns the next vrf for a given
 * vrf
 * @params    pvrf    current vrf pointer
 * @returns
 *     pvrf    if found
 *     NULL    otherwise
 */
jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_thread_get_next_vrf(jnx_gw_ctrl_sock_list_t * psocklist,
                                jnx_gw_ctrl_vrf_t * pvrf)
{
    patnode * pnode;

    if (pvrf == NULL) {
        return jnx_gw_ctrl_thread_get_first_vrf(psocklist);
    }

    if ((pnode = patricia_find_next(&psocklist->recv_vrf_db,
                                    &pvrf->vrf_tnode))) {
        return jnx_gw_ctrl_thread_vrf_entry(pnode);
    }
    return NULL;
}

/***********************************************************
 *                                                         *
 *             VRF ADD/DELETE ROUTINES                     *
 *                                                         *
 ***********************************************************/
/**
 * This function requests for the route table index
 * for inet.0 table for a route instance
 * @params pvrf   pointer to the vrf structure
 */
static void 
jnx_gw_ctrl_get_vrf_rt_table_id(jnx_gw_ctrl_vrf_t * pvrf)
{
    uint8_t vrf_tname[JNX_GW_STR_SIZE];
    uint8_t *rtt_name;

    if (!strlen(pvrf->vrf_name)) {
        return;
    }

    if ((jnx_gw_ctrl.rt_status != JNX_GW_CTRL_STATUS_UP) ||
        (pvrf->vrf_rt_status == JNX_GW_CTRL_STATUS_UP) ||
        (pvrf->vrf_rt_status == JNX_GW_CTRL_RT_TABLE_LOOKUP_PENDING)) {
        return;
    }

    if (!strcmp(pvrf->vrf_name, "default")) {
        strncpy(vrf_tname, "inet.0", sizeof(vrf_tname));
    }
    else  {
        strncpy(vrf_tname, pvrf->vrf_name, sizeof(vrf_tname));
        strcat(vrf_tname, ".inet.0");
    }

    if (!(rtt_name = strdup(vrf_tname))) {
        jnx_gw_log(LOG_ERR, 
                   "Route Table Id %s request malloc failed", vrf_tname);
        return;
    }

    pvrf->vrf_rt_status = JNX_GW_CTRL_RT_TABLE_LOOKUP_PENDING;

    /* send request to ssd for vrf inet.0 table id */
    jnx_gw_log(LOG_INFO, "Route Table Id request for %s", rtt_name);
    ssd_request_route_table_lookup(jnx_gw_ctrl.ssd_fd, rtt_name,
                                   (int32_t)pvrf);
}

/**
 * This function adds a vrf entry to the vrf db
 * creates a thread for handling the gre
 * session management functions from the
 * gre gateways on this vrf
 * @params    vrf_id    vrf index
 * @returns
 *     pvrf   if successful
 *     NULL   otherwise
 */
jnx_gw_ctrl_vrf_t *
jnx_gw_ctrl_add_vrf(uint32_t vrf_id, uint8_t * vrf_name)
{
    jnx_gw_ctrl_vrf_t * pvrf = NULL;

    /* retry getting the ssd route table id for this vrf */

    if ((pvrf = jnx_gw_ctrl_lookup_vrf(vrf_id)) != NULL) {
        if (!strlen(pvrf->vrf_name) && (vrf_name) && strlen(vrf_name)) {
            strncpy(pvrf->vrf_name, vrf_name, sizeof(pvrf->vrf_name));
        } 
        jnx_gw_ctrl_get_vrf_rt_table_id(pvrf);
        /* return */
        return pvrf;
    }

    jnx_gw_log(LOG_INFO, "Routing instance \"%s\" %d add", vrf_name, vrf_id);

    if ((pvrf = JNX_GW_MALLOC(JNX_GW_CTRL_ID, sizeof(*pvrf))) == NULL) {
        jnx_gw_log(LOG_ERR, "Routing instance <%s, %d> add malloc failed!",
                     vrf_name, vrf_id);
        goto gw_vrf_cleanup;
    }

    pvrf->vrf_id            = vrf_id;
    pvrf->vrf_status        = JNX_GW_CTRL_STATUS_INIT;
    pvrf->vrf_sig_status    = JNX_GW_CTRL_STATUS_INIT;
    pvrf->vrf_rt_status     = JNX_GW_CTRL_STATUS_INIT;

    pvrf->vrf_gre_key_start = JNX_GW_CTRL_GRE_KEY_START;
    pvrf->vrf_gre_key_end   = JNX_GW_CTRL_GRE_KEY_END;
    pvrf->vrf_gre_key_cur   = JNX_GW_CTRL_GRE_KEY_START;
    pvrf->vrf_max_gre_sesn  = JNX_GW_CTRL_GRE_KEY_END;

    /* set the name of vrf */
    if (vrf_name && strlen(vrf_name)) {
        strncpy(pvrf->vrf_name, vrf_name, sizeof(pvrf->vrf_name));
        /* get the route table index for inet.0 table in the vrf */
        jnx_gw_ctrl_get_vrf_rt_table_id(pvrf);
    }

    patricia_node_init_length(&pvrf->vrf_node, sizeof(pvrf->vrf_id));

    patricia_node_init_length(&pvrf->vrf_tnode, sizeof(pvrf->vrf_id));

    if (!patricia_add(&jnx_gw_ctrl.vrf_db, &pvrf->vrf_node)) {
        jnx_gw_log(LOG_ERR, "Routing instance \"%s\" (%d) add"
                   " patricia add failed",
                   vrf_name, vrf_id);
        goto gw_vrf_cleanup;
    }

    patricia_root_init(&pvrf->gre_sesn_db, FALSE,
                       fldsiz(jnx_gw_ctrl_gre_session_t, ingress_gre_key),
                       fldoff(jnx_gw_ctrl_gre_session_t, ingress_gre_key) -
                       fldoff(jnx_gw_ctrl_gre_session_t, gre_vrf_sesn_node) -
                       fldsiz(jnx_gw_ctrl_gre_session_t, gre_vrf_sesn_node));

    patricia_root_init(&pvrf->gre_gw_db, FALSE,
                       fldsiz(jnx_gw_ctrl_gre_gw_t, gre_gw_ip),
                       fldoff(jnx_gw_ctrl_gre_gw_t, gre_gw_ip) -
                       fldoff(jnx_gw_ctrl_gre_gw_t, gre_gw_node) -
                       fldsiz(jnx_gw_ctrl_gre_gw_t, gre_gw_node));

    patricia_root_init(&pvrf->ipip_gw_db, FALSE,
                       fldsiz(jnx_gw_ctrl_ipip_gw_t, ipip_gw_ip),
                       fldoff(jnx_gw_ctrl_ipip_gw_t, ipip_gw_ip) -
                       fldoff(jnx_gw_ctrl_ipip_gw_t, ipip_gw_node) -
                       fldsiz(jnx_gw_ctrl_ipip_gw_t, ipip_gw_node));

    patricia_root_init(&pvrf->vrf_intf_db, FALSE,
                       fldsiz(jnx_gw_ctrl_intf_t, intf_id),
                       fldoff(jnx_gw_ctrl_intf_t, intf_id) -
                       fldoff(jnx_gw_ctrl_intf_t, intf_vrf_node) -
                       fldsiz(jnx_gw_ctrl_intf_t, intf_vrf_node));

    patricia_root_init(&pvrf->ctrl_policy_db, FALSE,
                       fldsiz(jnx_gw_ctrl_policy_t, ctrl_key),
                       fldoff(jnx_gw_ctrl_policy_t, ctrl_key) -
                       fldoff(jnx_gw_ctrl_policy_t, ctrl_vrf_node) -
                       fldsiz(jnx_gw_ctrl_policy_t, ctrl_vrf_node));

    pthread_mutex_init(&pvrf->vrf_send_lock, 0);

    jnx_gw_log(LOG_INFO, "Routing instance \"%s\" (%d) add done",
               vrf_name, vrf_id);
    jnx_gw_ctrl.vrf_count++;
    return pvrf;

gw_vrf_cleanup:
    jnx_gw_log(LOG_ERR, "Routing instance \"%s\" (%d) add failed",
               vrf_name, vrf_id);
    if (pvrf) {
        patricia_delete(&jnx_gw_ctrl.vrf_db, &pvrf->vrf_node);
        JNX_GW_FREE(JNX_GW_CTRL_ID, pvrf);
    }
    /* generate an error message */
    return NULL;
}

/**
 * This function clears all the gre sessions active
 * for the vrf
 * deletes the vrf from the vrf db
 * frees the memory
 * @params    pvrf      vrf structure pointer
 */
void
jnx_gw_ctrl_clear_vrf(jnx_gw_ctrl_vrf_t * pvrf)
{
    jnx_gw_ctrl_user_t      * puser = NULL;
    jnx_gw_ctrl_intf_t      * pintf = NULL;
    jnx_gw_ctrl_gre_gw_t    * pgre_gw = NULL;
    jnx_gw_ctrl_ipip_gw_t   * pipip_gw = NULL;
    jnx_gw_ctrl_data_pic_t  * pdata_pic = NULL;
    jnx_gw_ctrl_gre_session_t * pgre_session = NULL;

    /* clear all the gateways on this vrf */

    jnx_gw_log(LOG_INFO, "Routing instance <%s, %d> delete",
               pvrf->vrf_name, pvrf->vrf_id);

    /* clear the gre sessions */
    while ((pgre_session = jnx_gw_ctrl_get_first_gre_session(pvrf, NULL,
                                                             NULL, NULL,
                                                             TRUE))) {
        /* set the session status to fail */
        pgre_session->sesn_status = JNX_GW_CTRL_STATUS_FAIL;
        jnx_gw_ctrl_delete_gre_session(pvrf, pgre_gw, pgre_session);
    }

    /* gre gateways */
    while ((pgre_gw = jnx_gw_ctrl_get_first_gre_gw(pvrf))) {
        jnx_gw_ctrl_delete_gre_gw(pvrf, pgre_gw);
    }

    while ((puser = jnx_gw_ctrl_get_next_user(puser))) {
        if (puser->pvrf != pvrf) continue;
        jnx_gw_ctrl_delete_user(pvrf, puser);
    }

    /* ipip gateways */
    while ((pipip_gw = jnx_gw_ctrl_get_first_ipip_gw(pvrf))) {
        jnx_gw_ctrl_delete_ipip_gw(pvrf, pipip_gw);
    }

    /* clear all the data interfaces on this vrf */

    while ((pintf = jnx_gw_ctrl_get_first_intf(pvrf, NULL, NULL))) {
        pdata_pic = pintf->pdata_pic;
        jnx_gw_ctrl_delete_intf(pvrf, pdata_pic, pintf);
    }

    /* delete from the db */
    patricia_delete(&jnx_gw_ctrl.vrf_db, &pvrf->vrf_node);
    JNX_GW_FREE(JNX_GW_CTRL_ID, pvrf);
}

/**
 * This function clears the vrf
 * updates the statistics
 * @params    pvrf      vrf structure pointer
 */
status_t
jnx_gw_ctrl_delete_vrf(jnx_gw_ctrl_vrf_t * pvrf)
{
    jnx_gw_ctrl_clear_vrf(pvrf);
    jnx_gw_ctrl.vrf_count--;
    return EOK;
}

/**
 * This function clears all vrfs
 * resources
 */
status_t
jnx_gw_ctrl_clear_vrfs(void)
{
    jnx_gw_ctrl_vrf_t * pvrf = NULL;

    while ((pvrf = jnx_gw_ctrl_get_first_vrf()))  {
        jnx_gw_ctrl_delete_vrf(pvrf);
    }
    return EOK;
}

/*
 * $Id: jnx-gateway-mgmt_kcom.c 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-gateway-mgmt_kcom.c - kcom interface ifd event handler routines
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

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <ctype.h>
#include <unistd.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <ddl/dax.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>
#include <jnx/trace.h>
#include <jnx/junos_trace.h>
#include <jnx/pconn.h>
#include <jnx/junos_kcom.h>
#include <jnx/vrf_util_pub.h>
#include <jnx/parse_ip.h>
#include <jnx/interface_info_pub.h>
#include <isc/eventlib.h>
#include <jnx/provider_info.h>
#include JNX_GATEWAY_MGMT_OUT_H

#include <jnx/jnx-gateway.h>
#include <jnx/jnx-gateway_msg.h>

#include "jnx-gateway-mgmt.h"
#include "jnx-gateway-mgmt_config.h"


/**
 * This file contains the kcom inteface routines
 * for handling interface ifd events
 */

/* local function prototype definitions */
static uint32_t jnx_gw_mgmt_ifl_get_vrfid(kcom_ifl_t * iflm);
static char msp_prefix[JNX_GW_STR_SIZE];

/* local macro definitions */
#define JNX_GW_IFL_VRF_IDX(iflm) jnx_gw_mgmt_ifl_get_vrfid(iflm)

/**
 * Function : jnx_gw_mgmt_get_ifl
 * Puspose  : get the loopback & services interfaces
 * 
 * @params iflm  ifl pointer
 * @return EOK   successful
 *         EFAIL fail
 */

int 
jnx_gw_mgmt_get_ifl (kcom_ifl_t * iflm, void * arg __unused)
{
    uint8_t * ifl_name;
    int32_t vrf_id, ifl_index;
    jnx_gw_mgmt_intf_t * pintf;

    ifl_name = iflm->ifl_name;

    /* check only for the services pic interfaces */
    if (strncmp(ifl_name, MSP_PREFIX, strlen(MSP_PREFIX))) { 
        junos_kcom_msg_free(iflm);
        return (EOK);
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_INFO, "IFL \"%s.%d\" %s event", 
                    iflm->ifl_name, iflm->ifl_subunit, "add");

    JNX_GW_IFL_IFL_IDX(ifl_index, iflm);

    if ((vrf_id = JNX_GW_IFL_VRF_IDX(iflm)) == JNX_GW_INVALID_VRFID) {
        return EOK;
    }

    if ((pintf = jnx_gw_mgmt_intf_lookup(ifl_index)) == NULL) {
        if ((pintf = jnx_gw_mgmt_intf_add(ifl_name, ifl_index, vrf_id,
                                          iflm->ifl_subunit))
            == NULL) {
            junos_kcom_msg_free(iflm);
            return EOK;
        }
    }

    pintf->intf_status = JNX_GW_MGMT_STATUS_UP;
    jnx_gw_mgmt_send_ctrl_intf_config(NULL, pintf, JNX_GW_CONFIG_ADD);
    junos_kcom_msg_free(iflm);
    return (EOK);
}

/**
 * Function : jnx_gw_mgmt_ifd_event_handler
 * Puspose  : handles the ifd state messages, deletes control/data sessions
 * ifd delete events
 * 
 * Return   : 0: success , -1: error
 */

static int 
jnx_gw_mgmt_ifd_event_handler(kcom_ifdev_t *ifdm, void * arg __unused)
{
    uint8_t ifd_name[JNX_GW_STR_SIZE];
    jnx_gw_mgmt_ctrl_session_t * pctrl_pic;
    jnx_gw_mgmt_data_session_t * pdata_pic;

    strncpy(ifd_name, ifdm->ifdev_name, sizeof(ifd_name));

    /* check only for the multi-services pic interfaces */
    if (strncmp(ifd_name, msp_prefix, strlen(msp_prefix))) { 
        junos_kcom_msg_free(ifdm);
        return (0);
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_INFO, "IFD \"%s\" %s event", ifdm->ifdev_name,
                    (ifdm->ifdev_op == KCOM_DELETE) ? "delete" : 
                    (ifdm->ifdev_op == KCOM_ADD) ?  "add" :
                    (ifdm->ifdev_op == KCOM_CHANGE) ?  "change" : "unknown");

    switch (ifdm->ifdev_op) {
        case KCOM_DELETE:
            /* clear up the services (if any) are active on this
               ifd, if the ifd is going down */
            if ((pctrl_pic = jnx_gw_mgmt_ctrl_sesn_lookup(ifd_name))) {
                jnx_gw_mgmt_destroy_ctrl_conn(pctrl_pic, FALSE);
            } 
            if ((pdata_pic = jnx_gw_mgmt_data_sesn_lookup(ifd_name))) {
                jnx_gw_mgmt_destroy_data_conn(pdata_pic);
            }
            break;

        case KCOM_ADD:
            /* the add will happen on receipt of the register messages
             * from the control and data pics
             */
            break;

        case KCOM_CHANGE:
            break;
        case KCOM_GET:
            break;
        default:
            break;
    }

    junos_kcom_msg_free(ifdm);
    return (EOK);
}

/**
 * Function : jnx_gw_mgmt_ifl_get_vrfid
 * Puspose  : resovles the ifl to the attached vrf 
 * 
 * Return   : 0: success , -1: error
 */

static uint32_t
jnx_gw_mgmt_ifl_get_vrfid(kcom_ifl_t * iflm)
{
    uint8_t ifl_name[JNX_GW_STR_SIZE];
    sprintf(ifl_name, "%s.%d", iflm->ifl_name, iflm->ifl_subunit);
    return vrf_getindexbyifname(ifl_name, AF_INET);
}


/**
 * Function : jnx_gw_mgmt_ifl_event_handler
 * Puspose  : handles the ifd state messages, deletes control/data sessions
 * ifd delete events
 * 
 * Return   : 0: success , -1: error
 */

static int 
jnx_gw_mgmt_ifl_event_handler(kcom_ifl_t *iflm, void * arg __unused)
{
    uint8_t ifl_name[JNX_GW_STR_SIZE];
    int32_t vrf_id, ifl_index;
    jnx_gw_mgmt_intf_t * pintf = NULL;
    jnx_gw_mgmt_ctrl_session_t * pctrl_pic = NULL;

    strncpy( ifl_name, iflm->ifl_name, sizeof(ifl_name));

    /* check only for the services pic interfaces */
    if (strncmp(ifl_name, msp_prefix, sizeof(msp_prefix))) {
        junos_kcom_msg_free(iflm);
        return (EOK);
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_INFO, "IFL \"%s.%d\" %s event", iflm->ifl_name,
                    iflm->ifl_subunit,
                    (iflm->ifl_op == KCOM_DELETE) ? "delete" : 
                    (iflm->ifl_op == KCOM_ADD) ?  "add" :
                    (iflm->ifl_op == KCOM_CHANGE) ? "change": "unknown");

    JNX_GW_IFL_IFL_IDX(ifl_index, iflm);

    if ((vrf_id = JNX_GW_IFL_VRF_IDX(iflm)) == JNX_GW_INVALID_VRFID) {
        junos_kcom_msg_free(iflm);
        return EOK;
    }

    pintf     = jnx_gw_mgmt_intf_lookup(ifl_index);
    pctrl_pic = jnx_gw_mgmt_ctrl_sesn_lookup(ifl_name);


    /* ifl events, post to the control agents,
     * do not worry about whether they are responsible
     * for the data agents running on them or, not
     */

    switch (iflm->ifl_op) {

        /* ifl delete event */
        case KCOM_DELETE:
            if (pintf == NULL) {
                break;
            }
            pintf->intf_status = JNX_GW_MGMT_STATUS_DELETE;
            jnx_gw_mgmt_send_ctrl_intf_config(pctrl_pic, pintf,
                                              JNX_GW_CONFIG_DELETE);
            jnx_gw_mgmt_intf_delete(pintf);
            break;

            /* ifl add event */
        case KCOM_ADD:
            if ((pintf == NULL) &&
                ((pintf = jnx_gw_mgmt_intf_add(ifl_name, ifl_index, vrf_id,
                                               iflm->ifl_subunit))
                 == NULL)) {
                break;
            }
            /* set the vrf id, we are intrested in */
            pintf->intf_vrf_id = vrf_id;
            pintf->intf_status = JNX_GW_MGMT_STATUS_UP;
            jnx_gw_mgmt_send_ctrl_intf_config(pctrl_pic, pintf,
                                              JNX_GW_CONFIG_ADD);
            break;

        case KCOM_CHANGE:
            /* ifl change event */
            /* fall through */
        case KCOM_GET:
            /* fall through */
        default:
            break;
    }
    junos_kcom_msg_free(iflm);
    return (EOK);
}

/**
 * Function : jnx_gw_mgmt_get_rtb
 * Puspose  : explicit scan functions for the routing table entries
 *            sends the messages to the control agents
 * @params  rtb  kcom rtb information structure
 * @params  args not used
 * @return  0: success , -1: error
 */
int
jnx_gw_mgmt_get_rtb (kcom_rtb_t * rtb, void * arg __unused)
{
    uint8_t add_flag = JNX_GW_CONFIG_ADD;

    /* internal vrfs, do not handle */
    if ((rtb->rtb_index == 1) || (rtb->rtb_index == 2)) {
        junos_kcom_msg_free(rtb);
        return (EOK);
    }

    if (junos_kcom_rtb_deleting(rtb)) {
        add_flag = JNX_GW_CONFIG_DELETE;
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_INFO, "VRF \"%s\" %s event", rtb->rtb_name,
                    (add_flag == JNX_GW_CONFIG_ADD) ? "add": "delete");

    /* send the vrf information to control agents */
    jnx_gw_mgmt_ctrl_send_vrf_msg(NULL, rtb->rtb_index, add_flag,
                                  JNX_GW_CTRL_SIGNALING_NOOP, FALSE);
    junos_kcom_msg_free(rtb);
    return (EOK);
}

/**
 * Function : jnx_gw_mgmt_rtb_event_handler
 * Puspose  : handles the route table state messages
 * 
 * Return   : 0: success , -1: error
 */

static int 
jnx_gw_mgmt_rtb_event_handler(kcom_rtb_t *rtb, void * arg __unused)
{

    /* internal vrfs, do not handle */
    if ((rtb->rtb_index == 1) || (rtb->rtb_index == 2)) {
        junos_kcom_msg_free(rtb);
        return (EOK);
    }

    jnx_gw_mgmt_log(JNX_GATEWAY_EVENT, JNX_GATEWAY_TRACEFLAG_EVENT,
                    LOG_INFO, "VRF \"%s\" %s event", rtb->rtb_name,
                    (rtb->rtb_op == KCOM_DELETE) ? "delete" : 
                    (rtb->rtb_op == KCOM_ADD) ?  "add" : 
                    (rtb->rtb_op == KCOM_CHANGE) ?  "change" : "unknown");

    /* rtb events, post to the control agents, */

    switch (rtb->rtb_op) {

        /* rtb delete event */
        case KCOM_DELETE:
            jnx_gw_mgmt_ctrl_send_vrf_msg(NULL, rtb->rtb_index, 
                                          JNX_GW_CONFIG_DELETE,
                                          JNX_GW_CTRL_SIGNALING_NOOP, FALSE);
            break;

            /* rtb add event */
        case KCOM_ADD:
            /* fall through */
            jnx_gw_mgmt_ctrl_send_vrf_msg(NULL, rtb->rtb_index, 
                                          JNX_GW_CONFIG_ADD,
                                          JNX_GW_CTRL_SIGNALING_NOOP, FALSE);
            break;

            /* rtb change event */
        case KCOM_CHANGE:
            /* set the vrf id, we are intrested in */
            jnx_gw_mgmt_ctrl_send_vrf_msg(NULL, rtb->rtb_index, 
                                          JNX_GW_CONFIG_MODIFY,
                                          JNX_GW_CTRL_SIGNALING_NOOP, FALSE);
            break;

        case KCOM_GET:
            /* fall through */
        default:
            break;
    }

    junos_kcom_msg_free(rtb);
    return (EOK);
}

/**
 * Function : jnx_gw_mgmt_kcom_init
 * Puspose  : Init KCOM library and register for asynchronous event/messages
 * Return   : 0: success , -1: error
 */
status_t
jnx_gw_mgmt_kcom_init(evContext ctxt)
{
    provider_origin_id_t        origin_id;

    if (provider_info_get_origin_id(&origin_id)) {
        jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_INIT, LOG_ERR,
                "%s: Retrieving origin ID failed: %m", __func__);
        return EFAIL;
    }
    
    if (junos_kcom_init(origin_id, ctxt) != KCOM_OK) {

        jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_INIT,
                        LOG_EMERG, "Kcom Module Initialization failed");
        return EFAIL;
    }
    
    strncpy(msp_prefix, MSP_PREFIX, sizeof(msp_prefix));


    /* register for ifd/ifl/vrf events */

    if (junos_kcom_register_rtb_handler(NULL, jnx_gw_mgmt_rtb_event_handler)
        != KCOM_OK) {
        jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_ERR, LOG_EMERG,
                        "Kcom RTB Event Handler Setup failed");
        goto kcom_cleanup;
    }

    if (junos_kcom_register_ifd_handler(NULL, jnx_gw_mgmt_ifd_event_handler)
        != KCOM_OK) { 
        jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_ERR, LOG_EMERG,
                        "Kcom IFD Event Handler Setup failed");
        goto kcom_cleanup;
    }

    if (junos_kcom_register_ifl_handler(NULL, jnx_gw_mgmt_ifl_event_handler) 
        != KCOM_OK) {
        jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_ERR, LOG_EMERG,
                        "Kcom IFL Event Handler Setup failed");
        goto kcom_cleanup;
    }

    /*
     * now get the interfaces & vrfs currently active on the system
     */
    junos_kcom_rtb_get_all(jnx_gw_mgmt_get_rtb, AF_INET, NULL);

    junos_kcom_ifl_get_all(jnx_gw_mgmt_get_ifl, msp_prefix, NULL);

    jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_INIT, LOG_INFO,
                    "Kcom Module Initialization done");
    return EOK;

kcom_cleanup:
    /*
     * shutdown the kcom module
     */
    junos_kcom_shutdown();

    jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_ERR, LOG_ERR,
           "Kcom Module Initialization failed");
    return EFAIL;
}

/**
 * Function : jnx_gw_mgmt_kcom_init
 * Puspose  : clean up kcom register events, states
 */

int
jnx_gw_mgmt_kcom_cleanup(evContext ctxt __unused)
{
    junos_kcom_shutdown();

    jnx_gw_mgmt_log(JNX_GATEWAY_INIT, JNX_GATEWAY_TRACEFLAG_INIT, LOG_INFO,
           "Kcom Module shutdown done");
    return EOK;
}

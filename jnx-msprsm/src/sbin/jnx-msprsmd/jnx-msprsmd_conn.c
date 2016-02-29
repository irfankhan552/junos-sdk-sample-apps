/*
 * $Id: jnx-msprsmd_conn.c 420796 2011-01-19 18:16:39Z emil $
 *
 * jnx-msprsmd_conn.c - conn init and message handler functions
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */


/**
 *
 * @file msprsmd_conn.c
 * @brief Handles pconn client functionality
 *
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>

#include <arpa/inet.h>
#include <jnx/trace.h>
#include <ddl/dtypes.h>

#include <jnx/junos_kcom.h>
#include <jnx/junos_trace.h>
#include <jnx/pconn.h>
#include <jnx/ssd_ipc_msg.h>
#include <jnx/name_len_shared.h>
#include <jnx/ipc_msp_pub.h>

#include JNX_MSPRSM_OUT_H

#include "jnx-msprsmd.h"
#include "jnx-msprsmd_config.h"
#include "jnx-msprsmd_kcom.h"
#include "jnx-msprsmd_conn.h"

/*
 * Global variables
 */
extern const char *msprsmd_if_str[IF_MAX];


/*
 * Static variables
 */
static struct in_addr floating_ip;


/*
 * Static functions
 */
static void
msprsmd_conn_ip2prefix (uint32_t ipaddr,
                        uint8_t prefix[KCOM_MAX_PREFIX_LEN])
{

    prefix[3] = (ipaddr & 0xff000000) >> 24;
    prefix[2] = (ipaddr & 0x00ff0000) >> 16;
    prefix[1] = (ipaddr & 0x0000ff00) >> 8;
    prefix[0] = (ipaddr & 0x000000ff);
}


/*
 * Exported functions
 */
int
msprsmd_conn_add_ifa (ifl_idx_t ifl_index)
{
    kcom_ifl_t kcom_ifl;
    kcom_ifdev_t kcom_ifd;
    pconn_client_t *pconn;
    pconn_client_params_t params;
    ipc_msp_req_t  req;
    ipc_msg_t *ipc_msg = NULL;
    int error = EINVAL;

    /*
     * Check the parameters
     */
    error = junos_kcom_ifl_get_by_index(ifl_index, &kcom_ifl);
    if (error != KCOM_OK) {

        syslog(LOG_ERR, "%s: junos_kcom_ifl_get_by_index() returned %d",
               __func__, error);
        goto failure;
    }

    /*
     * Fill pconn_client_params
     */
    memset(&params, 0, sizeof(pconn_client_params_t));
    params.pconn_peer_info.ppi_peer_type = PCONN_PEER_TYPE_PIC;
    junos_kcom_ifd_get_by_index(kcom_ifl.ifl_devindex,
                                &kcom_ifd);

    params.pconn_peer_info.ppi_fpc_slot = kcom_ifd.ifdev_media_nic;
    params.pconn_peer_info.ppi_pic_slot = kcom_ifd.ifdev_media_pic;
    params.pconn_port = IPC_MSP_TCP_PORT;
    params.pconn_num_retries = PCONN_RETRY_MAX;

    /*
     * Connect to the PIC
     */
    pconn = pconn_client_connect(&params);
    if (!pconn) {
        error = errno;
        syslog(LOG_INFO, "%s: "
               "pconn_client_connect() returned %s",
               __func__, strerror(error));

        goto failure;
    }

    /*
     * Fill in the command request
     */
    bzero(&req, sizeof(ipc_msp_ifl_addr_req_t));
    req.type = IPC_MSP_TYPE_IFL_ADDR;

    req.u.ifl_addr_req.opcode = IPC_MSP_IFL_ADDR_ADD;
    req.u.ifl_addr_req.ifl_index = ifl_idx_t_getval(ifl_index);
    req.u.ifl_addr_req.prefix_len = sizeof(uint32_t) * NBBY;
    req.u.ifl_addr_req.proto = PROTO_IPV4;
    msprsmd_conn_ip2prefix(floating_ip.s_addr,
                           req.u.ifl_addr_req.prefix);

    /*
     * Host -> Network
     */
    ipc_msp_req_hton(&req);

    /* Send the command request to the PIC */
    error = pconn_client_send(pconn, 0, &req, sizeof(ipc_msp_req_t));
    if (error != PCONN_OK) {

        syslog(LOG_ERR, "%s: "
               "pconn_client_send() returned %s",
               __func__, strerror(error));

        goto cleanup;
    }

    /* Get the command reply from the pic */
    if ((ipc_msg=pconn_client_recv(pconn)) == NULL) {

        error = errno;
        syslog(LOG_ERR, "%s: "
               "pconn_client_recv() returned %s",
               __func__, strerror(error));

        goto cleanup;
    }

    /* Parse the reply */
    ipc_msp_res_ntoh((ipc_msp_res_t *)ipc_msg->data);
    error = ((ipc_msp_res_t *)ipc_msg->data)->u.ifl_addr_res.status;

    syslog(LOG_INFO, "%s: "
           "Adding address to the PIC with ifl index %d"
           "ip address is %s, result is %s",
            __func__,
            ifl_idx_t_getval(ifl_index),
            addr2ascii(AF_INET, &floating_ip, sizeof(floating_ip), NULL),
            error?strerror(error):"OK");

    if (error == EEXIST) {
        error = 0;
    }

cleanup:
    /* Close the connection to the pic */
    pconn_client_close(pconn);

failure:
    return error;
}


/**
 * @brief
 *     Initializes pconn subsystem
 *
 * It reads the given config, and creates local data structures
 * for both interfaces
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_conn_init (msprsmd_config_t *msprsmd_config)
{
    junos_trace(JNX_MSPRSMD_TRACEFLAG_PCONN, "%s: "
                "initializing pconn parameters, floating ip is %s",
                __func__,
                addr2ascii(AF_INET, &msprsmd_config->floating_ip,
                           sizeof (msprsmd_config->floating_ip), NULL));

    floating_ip = msprsmd_config->floating_ip;
    return 0;
}

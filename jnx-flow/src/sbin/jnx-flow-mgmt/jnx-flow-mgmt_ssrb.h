/*
 * $Id: jnx-flow-mgmt_ssrb.h 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * jnx-flow-mgmt_ssrb.h - ssrb configuration data structure
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
 * @file : jnx-flow-mgmt_ssrb.h
 * @brief
 * This file contains the jnx-flow-mgmt ssrb entry
 * configuration data structures
 */
#ifndef __JNX_FLOW_MGMT_SSRB_H_
#define __JNX_FLOW_MGMT_SSRB_H_



typedef struct jnx_flow_ssrb_node_s {

    patnode     node;
    char        svc_set_name[JNX_FLOW_STR_SIZE];
    uint16_t   svc_set_id;
    nh_idx_t    in_nh_idx;
    nh_idx_t    out_nh_idx;

}jnx_flow_ssrb_node_t;

/***********************************************************************

         Function prototypes for Service Set resoltion Mapping

***********************************************************************/     

/* Function to allocate the SSRB Node */
extern jnx_flow_ssrb_node_t*  jnx_flow_mgmt_alloc_ssrb_node(void);

/* Function to free the SSRB Node */
extern void jnx_flow_mgmt_free_ssrb_node(jnx_flow_ssrb_node_t* ssrb);
#endif

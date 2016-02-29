/*
 * $Id: jnx-flow-mgmt_kcom.h 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * jnx-flow-mgmt_config.h - config data structures.
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
 * @file : jnx-flow-mgmt_kcom.h
 * @brief
 * This file contains the jnx-flow-mgmt kernel communication
 * function declarations
 */

#ifndef __JNX_FLOW_MGMT_KCOM_H__
#define __JNX_FLOW_MGMT_KCOM_H__

extern int jnx_flow_mgmt_kcom_init(evContext ctxt);
extern int jnx_flow_mgmt_kcom_cleanup(evContext ctxt __unused);

#endif

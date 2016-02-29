/*
 * $Id: jnx-flow-mgmt_ssrb.c 346460 2009-11-14 05:06:47Z ssiano $ 
 *
 * jnx-flow-mgmt_ssrb.c - SSRB related routines
 *
 * Vivek Gupta, Aug 2007
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyrignt (c) 2005-2008, Juniper Networks, Inc.
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

#include <jnx/trace.h>
#include <jnx/aux_types.h>
#include <jnx/bits.h>
#include <jnx/patricia.h>

#include <jnx/jnx-flow.h>
#include "jnx-flow-mgmt.h"
#include "jnx-flow-mgmt_ssrb.h"

/**
 * @file : jnx-flow-mgmt_rule.c
 * @brief
 * This file contains the jnx-flow-mgmt ssrb management routines
 */
/**
 * alloc a ssrb node entry
 */
jnx_flow_ssrb_node_t*  
jnx_flow_mgmt_alloc_ssrb_node(void)
{
    jnx_flow_ssrb_node_t* node = NULL;

    node =  calloc(1, sizeof(jnx_flow_ssrb_node_t));

    return node;
}

/**
 * free the ssrb node entry
 */
void 
jnx_flow_mgmt_free_ssrb_node(jnx_flow_ssrb_node_t* ssrb)
{
    free(ssrb);
}

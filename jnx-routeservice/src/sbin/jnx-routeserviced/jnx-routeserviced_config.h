/*
 * $Id: jnx-routeserviced_config.h 346460 2009-11-14 05:06:47Z ssiano $ 
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


#ifndef __JNX_ROUTESERVICED_CONFIG_H__
#define __JNX_ROUTESERVICED_CONFIG_H__

#define RT_DATA_STR_SIZE 127

#define RT_ADD_REQ_STAT_PENDING 0
#define RT_ADD_REQ_STAT_SUCCESS 1
#define RT_ADD_REQ_STAT_FAILURE 2

typedef struct jnx_routeserviced_data_s {
    patnode            routeserviced_patnode;
    char              *dest;
    char              *nhop;
    char              *ifname;
    int                req_status;
    unsigned int       req_ctx;
    unsigned int       prefixlen;
} jnx_routeserviced_data_t;


int jnx_routeserviced_config_read(int check);
jnx_routeserviced_data_t *rt_data_first(void);
jnx_routeserviced_data_t *rt_data_next(jnx_routeserviced_data_t *rt_data);

void rt_add_data_update(unsigned int cli_ctx, int result);
void rt_add_data_send_pending(void);
void rt_add_data_mark_pending(void);
void rt_data_delete_entry_by_ctx(unsigned int ctx);
void rt_data_init(void);

#endif /* __JNX_ROUTESERVICED_CONFIG_H__ */


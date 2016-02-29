/*
 * $Id: jnx-exampled_config.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-exampled_config.h - declarations for global config functions
 *
 * Copyright (c) 2005-2007, Juniper Networks, Inc.
 * All rights reserved.
 */


#ifndef __JNX_EXAMPLED_CONFIG_H__
#define __JNX_EXAMPLED_CONFIG_H__

#define EX_DATA_STR_SIZE 127
typedef struct jnx_exampled_data_s {
    patnode    exd_node;
    char      *exd_index;
    char      *exd_descr;
    u_int32_t  exd_type;
    char      *exd_value;
} jnx_exampled_data_t;


int jnx_exampled_config_read (int check);
void ex_data_init (void);
jnx_exampled_data_t *ex_data_first (void);
jnx_exampled_data_t *ex_data_next (jnx_exampled_data_t *ex_data);
jnx_exampled_data_t *ex_data_lookup (const char *ex_index);

#endif /* __JNX_EXAMPLED_CONFIG_H__ */


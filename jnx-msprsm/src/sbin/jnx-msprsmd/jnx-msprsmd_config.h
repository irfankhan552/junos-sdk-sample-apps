/*
 * $Id: jnx-msprsmd_config.h 418048 2010-12-30 18:58:42Z builder $
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __JNX_MSPRSMD_CONFIG_H__
#define __JNX_MSPRSMD_CONFIG_H__

typedef struct msprsmd_config_s {
    char ifname[IF_MAX][KCOM_IFNAMELEN];
    struct in_addr  floating_ip;
} msprsmd_config_t;


int msprsmd_config_read(int check);


#endif /* __JNX_MSPRSMD_CONFIG_H__ */

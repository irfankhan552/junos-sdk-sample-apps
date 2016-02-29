/* $Id: jnx-msprsmd_conn.h 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-msprsmd_conn.h : jnx-msprsmd libpconn client
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */


#ifndef __JNX_MSPRSMD_CONN_H__
#define __JNX_MSPRSMD_CONN_H__


/**
 * @brief
 *     Sends address addition request to the given interface
 *
 * Connects to the mspmand on the interface given by the index
 * and sends the ifa_add request there
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_conn_add_ifa (ifl_idx_t ifl_index);


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
msprsmd_conn_init (msprsmd_config_t *msprsmd_config);

#endif /* end of __JNX_MSPRSMD_CONN_H__ */

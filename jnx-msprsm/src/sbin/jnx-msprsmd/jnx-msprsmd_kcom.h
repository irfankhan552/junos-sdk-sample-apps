/* $Id: jnx-msprsmd_kcom.h 418048 2010-12-30 18:58:42Z builder $
 *
 * jnx-msprsmd_kcom.h : jnx-msprsmd kernel communication functions
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */


#ifndef __JNX_MSPRSMD_KCOM_H__
#define __JNX_MSPRSMD_KCOM_H__


/**
 * @brief
 *     Gets intreface's chassis geometry
 *     by msprsmd interface index
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_kcom_pic_by_idx (msprsmd_if_t msprsmd_if,
                         uint32_t *fpc_slot, uint32_t *pic_slot);

/**
 * @brief
 *     Gets ifl index by msprsmd interface index
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_kcom_ifl_by_idx (msprsmd_if_t msprsmd_if, ifl_idx_t *ifl_idx);


/**
 * @brief
 *     Initializes kcom subsystem
 *
 * It reads the given config, closes old kcom session
 * if one was in place, and opens a new one.
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_kcom_init (evContext ctx,
                   msprsmd_config_t *msprsmd_config);


/**
 * @brief
 *      Initializes kcom switchover handler
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_kcom_start_switchover (void);


/**
 * @brief
 *     Closes the kcom subsystem
 *
 *  Shuts kcom down - this will close all sessions
 *  with kcom message handlers assosiated with them.
 *
 */
void
msprsmd_kcom_shutdown (void);

#endif /* end of __JNX_MSPRSMD_KCOM_H__ */

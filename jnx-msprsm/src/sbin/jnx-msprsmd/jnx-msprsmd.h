/*
 * $Id: jnx-msprsmd.h 418048 2010-12-30 18:58:42Z builder $
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __JNX_MSPRSMD_H__
#define __JNX_MSPRSMD_H__

/*
 * Commonly used interface index enum
 */
typedef enum {
    IF_PRI=0,
    IF_SEC,
    IF_MAX
} msprsmd_if_t;

/*
 * Macro for cathcing and dumping child function errors
 */
#define CHECK(__msg, __level, __func, __func_str)           \
do {                                                        \
    int __err = __func;                                     \
    if (__err) {                                            \
        ERRMSG(__msg, __level, "%s: %s() returned %s",      \
               __func__, __func_str, strerror(__err));  \
        return __err;                                       \
    }                                                       \
} while (0)

#endif /* __JNX_MSPRSMD_H__ */

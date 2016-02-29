/*
 * $Id: jnx-routeserviced_gencfg.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __JNX_ROUTESERVICED_GENCFG_H___
#define __JNX_ROUTESERVICED_GENCFG_H___

/*
 * Gencfg Blog key for client-id 
 */
#define JNX_ROUTESERVICED_CLIENT_ID_BLOB_KEY  0x00000001

/*
 * Gencfg Blog ID for client-id 
 */
#define JNX_ROUTESERVICED_CLIENT_ID_BLOB_ID   0x00000001

int jnx_routeserviced_gencfg_init(void);
void jnx_routeserviced_gencfg_store_client_id(int id);
int jnx_routeserviced_gencfg_get_client_id(int *id);
void jnx_routeserviced_gencfg_delete_client_id(int id __unused);

#endif /* !__JNX_ROUTESERVICED_GENCFG_H__ */

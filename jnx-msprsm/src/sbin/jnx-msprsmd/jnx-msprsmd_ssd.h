/*
 * $Id: jnx-msprsmd_ssd.h 418048 2010-12-30 18:58:42Z builder $
 *
 * Copyright (c) 2008, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __JNX_MSPRSMD_SSD_H__
#define __JNX_MSPRSMD_SSD_H__


/**
 * @brief
 *     Adds the service style nexthop
 *
 *  Sends nexthop add request for the given service interface.
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_ssd_add_nexthop (msprsmd_if_t msprsmd_if);


/**
 * @brief
 *     Removes the service style nexthop
 *
 *  Sends nexthop delete request for the given service interface.
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_ssd_del_nexthop (msprsmd_if_t msprsmd_if);

/**
 * @brief
 *     Adds the route to the service PIC
 *
 *  Sends route add request for the given service interface
 *
 * @return 0 on success or error on failure
 */
int
msprsmd_ssd_add_route (msprsmd_if_t msprsmd_if);

/**
 * @brief
 *     Deletes the route to the service PIC
 *
 *  Sends route delete request for the given service interface
 *
 * @return 0 on success or error on failure
 */

int
msprsmd_ssd_del_route (msprsmd_if_t msprsmd_if);

/**
 * @brief
 *     Connects to the SDK Service Daemon (SSD)
 *
 *  It reads the given config, closes oldef SSD session
 *  if one was in place, populates the necessary
 *  socket address of the server, gets the routing table id,
 *  connects to the SDK Service Daemon and adds nexthops
 *  for the configured interfaces
 *
 * @return 0 on success or error on failure
 */

int
msprsmd_ssd_init (evContext ctx, msprsmd_config_t *msprsmd_config);


/**
 * @brief
 *     Disconnects from the SDK Service Daemon
 *
 *  Closes the session with SDK Service Daemon,
 *  wiping off all routing objects assosiated
 *  with this session
 *
 * @return 0 on success or error on failure
 */
void
msprsmd_ssd_shutdown (void);

#endif /* ! __JNX_MSPRSMD_SSD_H__ */

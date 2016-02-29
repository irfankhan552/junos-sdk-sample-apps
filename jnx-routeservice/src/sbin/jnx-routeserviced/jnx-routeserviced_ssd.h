/*
 * $Id: jnx-routeserviced_ssd.h 346460 2009-11-14 05:06:47Z ssiano $
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

#ifndef __JNX_ROUTESERVICED_SSD_H__
#define __JNX_ROUTESERVICED_SSD_H__

typedef enum jnx_routeserviced_state_e {
    JNX_RS_STATE_RESTARTING,
    JNX_RS_STATE_SSD_DISCONNECTED,
    JNX_RS_STATE_SSD_CLIENT_ID_RESTORED
} jnx_routeserviced_state_t;

struct jnx_routeserviced_route_add_req {
    struct ssd_route_parms rtp;  /* Route parameters */
    ssd_sockaddr_un route_addr;  /* Destination address */
    ssd_sockaddr_un route_nh;    /* Next hop address for destination */
    ssd_sockaddr_un route_local; /* Local address associated with interface */
    unsigned int    ctx;         /* Context for the request */
    TAILQ_ENTRY(jnx_routeserviced_route_add_req) entries;
};

struct jnx_routeserviced_route_del_req {
    struct ssd_rt_delete_parms rtp;  /* Route parameters */
    ssd_sockaddr_un route_addr;      /* Destination address */
    unsigned int    ctx;             /* Context for the request */
    TAILQ_ENTRY(jnx_routeserviced_route_del_req) entries;
};

int  jnx_routeserviced_ssd_init (evContext ev_ctx);
void jnx_routeserviced_ssd_add_route (int fd, char *destination, 
                                      char *nhop, char *family,
                                      unsigned int prefixlen,
                                      unsigned int req_ctx);
void jnx_routeserviced_ssd_del_route (int fd, char *destination, 
                                      char *nhop, char *family,
                                      unsigned int prefixlen,
                                      unsigned int req_ctx);
void jnx_routeserviced_ssd_reconnect(evContext ctx __unused, 
                                     void *uap __unused,
                                     struct timespec due __unused,
                                     struct timespec inter __unused);

#endif /* ! __JNX_ROUTESERVICED_SSD_H__ */

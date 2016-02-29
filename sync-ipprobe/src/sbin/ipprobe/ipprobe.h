/*
 * $Id: ipprobe.h 346460 2009-11-14 05:06:47Z ssiano $
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

/**
 * @file ipprobe.h
 * @brief The header file for probe initiator (client)
 *
 * This header file defines macros and data structures for
 * the probe initiator (client).
 */

#ifndef __IPPROBE_H__
#define __IPPROBE_H__

/** Default values of probe packet fields */
#define DEFAULT_PACKET_TOS          IPTOS_RELIABILITY
#define DEFAULT_PACKET_SIZE         100
#define DEFAULT_PACKET_COUNT        10
#define DEFAULT_PACKET_INTERVAL     20
#define DEFAULT_PACKET_TTL          128
#define DEFAULT_PROBE_SRC_PORT      2000
#define DEFAULT_PROBE_DST_PORT      2100

#define PRINT_ERROR(fmt, ...) \
            do { \
                printf("ERROR: " fmt, ##__VA_ARGS__); \
            } while (0)
#define PRINT_ERRORNO(fmt, ...) \
            do { \
                printf("ERROR %d: " fmt, errno, ##__VA_ARGS__); \
            } while (0)

/**
 * Probe parameter data structure
 */
typedef struct probe_params_s {
    char    name[MAX_STR_LEN];  /**< Probe name in configuration */
    u_char  protocol;           /**< IP protocol */
    u_char  tos;                /**< TOS in IP header */
    u_short src_port;           /**< Protocol source port */
    u_short dst_port;           /**< Protocol destination port */
    u_short packet_size;        /**< Size of probe packet, in byte */
    u_short packet_count;       /**< The number of probe packets */
    u_short packet_interval;    /**< Interval between each two probe packets,
                                     in ms */
} probe_params_t;

/**
 * Probe statistics data structure
 */
typedef struct packet_stats_s {
    float sd_delay;     /**< Source-to-destination delay */
    float ds_delay;     /**< Destination-to-source delay */
    float rrt;          /**< Round-trip-time */
    float sd_jitter;    /**< Source-to-destination jitter */
    float ds_jitter;    /**< Destination-to-source jitter */
    float rr_jitter;    /**< Round-trip jitter */
} packet_stats_t;

/**
 * Initialize probe parameters to default value and
 * read probe configuration.
 *
 * @param[in] probe_name
 *      Pointer to the probe name.
 *
 * @return 0 on success, -1 on failure
 */
int config_read(int check);

#endif

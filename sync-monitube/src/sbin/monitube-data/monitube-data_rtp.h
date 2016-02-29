/*
 * $Id: monitube-data_rtp.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-data_rtp.h
 * @brief Related to RTP
 * 
 *  Related to the Real-time Transport Protocol (RTP)
 *  and maintaining RTP state and statistics
 */
 
#ifndef __MONITUBE_DATA_RTP_H__
#define __MONITUBE_DATA_RTP_H__

/*** Constants ***/

#define RTP_VERSION        2  ///< current protocol version
#define RTP_SEQ_MOD  (1<<16)  ///< used in update_seq

#define MIN_SEQUENTIAL    2   ///< minimum number of packets to be seen before a source is valid

/*** Data structures ***/

/**
 * RTP data header
 */
typedef struct rtphdr {
#if BYTE_ORDER == LITTLE_ENDIAN
    uint8_t cc:4,        ///< CSRC count
            x:1,         ///< header extension flag
            p:1,         ///< padding flag
            version:2;   ///< protocol version
            
    uint8_t pt:7,        ///< payload type
            m:1;         ///< marker bit
#endif
#if BYTE_ORDER == BIG_ENDIAN
    uint8_t version:2,   ///< protocol version
            p:1,         ///< padding flag
            x:1,         ///< header extension flag
            cc:4;        ///< CSRC count
            
    uint8_t m:1,         ///< marker bit
            pt:7;        ///< payload type
#endif
    
    uint16_t seq;         ///< sequence number
    uint32_t ts;          ///< timestamp
    uint32_t ssrc;        ///< synchronization source
    uint32_t csrc[0];     ///< optional contributing source (CSRC) list
} rtp_hdr_t;


/**
 * Per-source state information
 */
typedef struct source_s {
    uint16_t max_seq;        ///< highest seq. number seen
    uint32_t cycles;         ///< shifted count of seq. number cycles
    uint32_t base_seq;       ///< base seq number
    uint32_t bad_seq;        ///< last 'bad' seq number + 1
    uint32_t probation;      ///< sequ. packets till source is valid
    uint32_t received;       ///< packets received
    uint32_t expected_prior; ///< packet expected at last interval
    uint32_t received_prior; ///< packet received at last interval
} source_t;


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Init state for RTP statistics
 * 
 * @param[in] s
 *      Source state
 * 
 * @param[in] seq
 *      Current sequence number
 * 
 * @return 1 upon finding a valid packet part of a new state; 0 otherwise
 */
int
update_seq(source_t * s, uint16_t seq);

#endif


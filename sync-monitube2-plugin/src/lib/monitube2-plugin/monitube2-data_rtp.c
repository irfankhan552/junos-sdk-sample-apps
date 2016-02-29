/*
 * $Id: monitube2-data_rtp.c 347265 2009-11-19 13:55:39Z kdickman $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 * 
 * Copyright (c) 2009, Juniper Networks, Inc.
 * All rights reserved.
 */

/**
 * @file monitube2-data_rtp.c
 * @brief Related to RTP
 * 
 *  Related to the Real-time Transport Protocol (RTP)
 *  and maintaining RTP state and statistics
 */

#include "monitube2-data_main.h"
#include "monitube2-data_rtp.h"

/*** Constants ***/


/*** Data structures ***/


/*** STATIC/INTERNAL Functions ***/


/**
 * Init state for RTP statistics
 * 
 * @param[in] s
 *      Source state
 * 
 * @param[in] seq
 *      Current sequence number
 */
static void
init_seq(source_t * s, uint16_t seq)
{
    s->base_seq = seq;
    s->max_seq = seq;
    s->bad_seq = RTP_SEQ_MOD + 1;
    s->cycles = 0;
    s->received = 0;
    s->received_prior = 0;
    s->expected_prior = 0;
}

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
update_seq(source_t * s, uint16_t seq)
{
    const int MAX_DROPOUT    = 3000;
    const int MAX_MISORDER   =  100;
    
    uint16_t udelta = seq - s->max_seq;

    /*
     * Source is not valid until MIN_SEQUENTIAL packets with
     * sequential sequence numbers have been received.
     */
    
    if (s->probation) {
        /* packet is in sequence */
        if (seq == s->max_seq + 1) {
            s->probation--;
            s->max_seq = seq;
            if (s->probation == 0) {
                init_seq(s, seq);
                s->received++;
                return 1;
            }
        } else {
            s->probation = MIN_SEQUENTIAL - 1;
            s->max_seq = seq;
        }
        return 0;
        
    } else if (udelta < MAX_DROPOUT) {
        /* in order, with permissible gap */
        if (seq < s->max_seq) {
            /*
             * Sequence number wrapped - count another 64K cycle.
             */
            s->cycles += RTP_SEQ_MOD;
        }
        s->max_seq = seq;
    } else if (udelta <= RTP_SEQ_MOD - MAX_MISORDER) {
        /* the sequence number made a very large jump */
        
        if (seq == s->bad_seq) {
            /*
             * Two sequential packets -- assume that the other side
             * restarted without telling us so just re-sync
             * (i.e., pretend this was the first packet).
             */
            init_seq(s, seq);
        } else {
            s->bad_seq = (seq + 1) & (RTP_SEQ_MOD-1);
            return 0;
        }
    } else {
        /* duplicate or reordered packet */
    }
    s->received++;
    return 1;
}

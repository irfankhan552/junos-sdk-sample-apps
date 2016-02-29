/*
 * $Id: ped_schedule.c 366969 2010-03-09 15:30:13Z taoliu $
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
 
/**
 * @file ped_schedule.c
 * @brief Init and play with timers
 * 
 * 
 * Initialize timers
 */
#include <sync/common.h>
#include "ped_schedule.h"
#include "ped_services.h"
#include "ped_logging.h"

#include PE_OUT_H

/*** Constants ***/


/*** Data Structures ***/

static evTimerID  timer_id;    ///< timer ID used by interface description
static boolean    timer_set;   ///< is the timer set
static uint32_t   period;      ///< periodic update time (seconds)

extern evContext  ped_ctx;  ///< Event context for ped.


/*** STATIC/INTERNAL Functions ***/

/**
 * On the "stop" timer expiry event we we restart the scheduler.
 * 
 * @param[in] ctx
 *     The event context for this application
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] due
 *     The absolute time when the event is due (now)
 * 
 * @param[in] inter
 *     The period; when this will next be called 
 */
static void
ped_restart(evContext ctx __unused,
            void * uap  __unused,
            struct timespec due __unused,
            struct timespec inter __unused)
{
    junos_trace(PED_TRACEFLAG_NORMAL, "%s", __func__);

    ped_schedule_reset();
}

/**
 * On timer expiry we process periodic events.
 * If this gets called more than MAX_RETRY_ATTEMPTS in less than 
 * SCHEDULE_STOP_RETRY seconds, then it does not process events and
 * it will cancel these regular periodic events for 
 * SCHEDULE_RESTART_TIME seconds. After that ped_restart is called.
 * 
 * Periodic events include:
 *   We update the interfaces via the PSD.
 * 
 * 
 * @param[in] ctx
 *     The event context for this application
 * 
 * @param[in] uap
 *     The user data for this callback
 * 
 * @param[in] due
 *     The absolute time when the event is due (now)
 * 
 * @param[in] inter
 *     The period; when this will next be called 
 */
static void
ped_periodic(evContext ctx __unused,
             void * uap  __unused,
             struct timespec due __unused,
             struct timespec inter __unused)
{
    static struct timespec first_call_time;
    static int attempts = 0;

    struct timespec time_difference;

    junos_trace(PED_TRACEFLAG_HB, "%s", __func__);
    
    // check if we've been processing these a lot _recently_
    // by a lot we mean more than normal due to errors and retries
    
    if(++attempts > MAX_RETRY_ATTEMPTS) {
        
        time_difference = evSubTime(evNowTime(), first_call_time);
        
        if(evCmpTime(time_difference, evConsTime(SCHEDULE_STOP_RETRY, 0)) < 0) {
            
            // we've had more than MAX_RETRY_ATTEMPTS in less than
            // SCHEDULE_STOP_RETRY seconds... STOP retrying!
            attempts = 0;

            // clear any periodic events associated with this timer
            if(timer_set && evClearTimer(ped_ctx, timer_id)) {
                ERRMSG(PED, TRACE_LOG_ERR,
                        "%s: evClearTimer() FAILED!", __func__);
            }

            // restart trying later (a one-shot event/non-periodic)
            // Wait for SCHEDULE_RESTART_TIME seconds !
            if(evSetTimer(ped_ctx, ped_restart, NULL,
                  evAddTime(evNowTime(), evConsTime(SCHEDULE_RESTART_TIME, 0)),
                  evConsTime(0, 0), &timer_id)) {
        
                ERRMSG(PED, TRACE_LOG_ERR,
                        "%s: evSetTimer() FAILED! Cannot continue "
                        "retries or periodic events", __func__);

                timer_set = FALSE;
            } else {
                timer_set = TRUE;
            }
            return;
        }

        // hasn't been _recently_ so just proceed...
        attempts = 0;
    }

    if(attempts == 1)
        first_call_time = evNowTime(); // record this "first" event

    // Process periodic events:
    if(!check_psd_hb()) {
        ped_schedule_reset(); // upon failure re-try immediately 
    }
}

/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Initialize the internals necessary for the periodic timer/scheduler.
 * Does not set timers. That's done with ped_schedule_reset.
 * This must be called before using ped_schedule_reset.
 * 
 * @return 0 on success, and -1 on error
 */
int
ped_schedule_init(void)
{
    junos_trace(PED_TRACEFLAG_NORMAL, "%s", __func__);

    timer_set = FALSE;
    period = SCHEDULE_PERIOD;
    
    return 0;
}


/**
 * Clear existing timer if it exists, and set the timer fresh again.
 * Starts a periodic timer every SCHEDULE_PERIOD seconds, and a one-shot timer
 * that goes off in SCHEDULE_IMMEDIATE_DELAY seconds.
 * Timer callback is ped_periodic.
 * 
 * If we're resetting, then something went wrong most likely (or it's 
 * a configuration load/reload) so we have a small delay timer before
 * retrying. If all goes well, then the periodic timer will continue
 * checks after that. 
 */
void
ped_schedule_reset(void)
{
    junos_trace(PED_TRACEFLAG_NORMAL, "%s", __func__);

    // unset it then set it again
    if(timer_set && evClearTimer(ped_ctx, timer_id)) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: evClearTimer() FAILED!", __func__);
    }

    /*
     * Set the timer:
     * immediate one-shot event at time (now + SCHEDULE_IMMEDIATE_DELAY), and
     * periodocally afterward every period seconds
     */
    if(evSetTimer(ped_ctx, ped_periodic, NULL,
            evAddTime(evNowTime(), evConsTime(SCHEDULE_IMMEDIATE_DELAY, 0)),
            evConsTime(period, 0), &timer_id)) {

        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: evSetTimer() FAILED! Cannot continue "
                "retries or periodic events", __func__);

        timer_set = FALSE;
    } else {
        timer_set = TRUE;
    }
}


/**
 * Clear existing schedule for shutdown
 */
void
ped_schedule_stop(void)
{
    // unset it
    if(timer_set && evClearTimer(ped_ctx, timer_id)) {
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: evClearTimer() FAILED!", __func__);
    }
}


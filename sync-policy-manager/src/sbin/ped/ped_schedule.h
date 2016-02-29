/*
 * $Id: ped_schedule.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file ped_schedule.h
 * 
 * @brief Routines related to timers
 * 
 * Functions to initialize timer infrastruture and register periodic updates
 */
#ifndef __PED_SCHEDULE_H__
#define __PED_SCHEDULE_H__

/*** Constants ***/

// We'll put these here so they're publicly changeable:
#define SCHEDULE_IMMEDIATE_DELAY  1  ///< seconds before immediately due timeout
#define SCHEDULE_PERIOD       10     ///< seconds before next due timeout
#define SCHEDULE_RESTART_TIME 120    ///< seconds before a retry after a stop

/**
 * @brief if we have more than MAX_RETRY_ATTEMPTS within this
 * (SCHEDULE_STOP_RETRY) many seconds, then we stop for SCHEDULE_RESTART_TIME
 */
#define SCHEDULE_STOP_RETRY      10

/**
 * @brief number of retries associated with SCHEDULE_STOP_RETRY
 */
#define MAX_RETRY_ATTEMPTS        3

/*** Data structures ***/

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Initialize the internals necessary for the periodic timer/scheduler.
 * Does not set timers. That's done with ped_schedule_reset.
 * This must be called before using ped_schedule_reset.
 * 
 * @return 0 on success, and -1 on error
 */
int ped_schedule_init(void);

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
void ped_schedule_reset(void);


/**
 * Clear existing schedule for shutdown
 */
void ped_schedule_stop(void);

#endif

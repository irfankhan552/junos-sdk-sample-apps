/*
 * $Id: counterd_message.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file counterd_message.h
 * @brief Contains message manipulation functions
 * 
 * Contains the message manipulation functions
 * 
 */
 
#ifndef __COUNTERD_MESSAGE_H__
#define __COUNTERD_MESSAGE_H__


/*** Constants ***/


/*** Data structures: ***/


/*** STATIC/INTERNAL Functions ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Reset the message stored in persistent storage. This function will compare
 * the current contents if what is in storage to the new message, and if they
 * are the same, then it takes sets the local message-viewed counter to the
 * one in storage and continues counting from there. Otherwise, the message is
 * different and we reset the local counter too. 
 * 
 * @param[in] new_message
 *      The new configured message
 * 
 * @return
 *      TRUE if new_message was different than what was found in storage;
 *      FALSE if there exists an identical message already in storage
 */
boolean
reset_message(char * new_message);


/**
 * Get Message (for viewing) and increment the message-viewed counter.
 * 
 * @return message or NULL if it doesn't exist
 */
char * get_message(void);


/**
 * Get the number of times the message has been viewed
 * 
 * @return the number of times the message has been viewed
 */
uint32_t get_times_viewed(void);

#endif


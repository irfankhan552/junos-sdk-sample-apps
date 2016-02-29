/*
 * $Id: counterd_message.c 366969 2010-03-09 15:30:13Z taoliu $
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
 * @file counterd_message.c
 * @brief Contains message manipulation functions
 * 
 * Contains the message manipulation functions
 * 
 */

#include "counterd_config.h"
#include "counterd_message.h"
#include "counterd_kcom.h"
#include "counterd_logging.h"


/*** Constants ***/


/*** Data structures: ***/


/**
 * The data structure we store in GENCFG quasi-persistent storage 
 */
typedef struct msg_data_s {
    uint32_t times_viewed;          ///< the message-viewed counter
    char message[MESSAGE_STR_SIZE];  ///< the message itself
} msg_data_t;


/**
 * Number of times the message has been viewed
 */
static uint32_t times_viewed = 0;


/*** STATIC/INTERNAL Functions ***/


/*** GLOBAL/EXTERNAL Functions ***/


/**
 * Reset the message stored in persistent storage. This function will compare
 * the current contents of what is in storage to the new message, and if they
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
reset_message(char * new_message)
{
    msg_data_t data;
    
    INSIST(strlen(new_message) < MESSAGE_STR_SIZE);
    
    if(counterd_get_data(&data, sizeof(data)) > 0) {
        
        // There's something in GENCFG
        
        if(strcmp(data.message, new_message) == 0) {
        
            // message in GENCFG is the same as new_message
            times_viewed = data.times_viewed; // save the times_viewed locally
            
            ERRMSG(COUNTERD, TRACE_LOG_INFO,
                "%s: Got a value of %d from GENCFG.",
                __func__, times_viewed);
            
            return FALSE;
        }
    }
    
    // set new values to go into storage and add it
    
    times_viewed = data.times_viewed = 0;
    strcpy(data.message, new_message);
    
    counterd_add_data(&data, sizeof(data));
    
    ERRMSG(COUNTERD, TRACE_LOG_INFO,
        "%s: Did not get any value from GENCFG. Defaulting it to zero.",
        __func__);
    
    return TRUE;
}


/**
 * Get Message (for viewing) and increment the message-viewed counter.
 * 
 * @return message or NULL if it doesn't exist
 */
char *
get_message(void)
{
    counter_data_t * data;
    msg_data_t msg_data;

    if((data = first_message()) != NULL) {

        // increment message-viewed counter
        msg_data.times_viewed = ++times_viewed;
        strcpy(msg_data.message, data->message);
        
        // replace values in storage
        counterd_add_data(&msg_data, sizeof(msg_data));
        
        return data->message;
    }
    
    return NULL;
}


/**
 * Get the number of times the message has been viewed
 * 
 * @return the number of times the message has been viewed
 */
uint32_t
get_times_viewed(void)
{
    return times_viewed;
}



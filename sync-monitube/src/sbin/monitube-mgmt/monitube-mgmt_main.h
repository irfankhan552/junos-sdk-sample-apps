/*
 * $Id: monitube-mgmt_main.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file monitube-mgmt_main.h
 * 
 * @brief Contains shutdown function
 * 
 * Contains shutdown function
 */


#ifndef __MONITUBE_MGMT_MAIN_H__
#define __MONITUBE_MGMT_MAIN_H__

/*** Constants ***/


/*** Data structures ***/


/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Shutdown app and opitonally exit with status 1
 * 
 * @param[in] do_exit
 *      if true, log a shutdown message and call exit(1)
 */
void
monitube_shutdown(boolean do_exit);

#endif

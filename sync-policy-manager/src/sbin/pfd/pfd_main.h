/*
 * $Id: pfd_main.h 346460 2009-11-14 05:06:47Z ssiano $
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
 * @file pfd_main.h
 * @brief Functions for shutting down the PFD
 * 
 * Functions for shutting down the PFD
 */
 
#ifndef __PFD_MAIN_H__
#define __PFD_MAIN_H__

/*** Constants ***/

/*** Data structures ***/

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Shutdown PFD. Calls exit(1) after cleanning up
 */
void pfd_shutdown(void);

#endif


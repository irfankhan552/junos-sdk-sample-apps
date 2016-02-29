/*
 * $Id: jnx-exampled_kcom.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * jnx-exampled_kcom.h - declarations for global KCOM functions
 *
 * Copyright (c) 2006, Juniper Networks, Inc.
 * All rights reserved.
 */

#ifndef __JNX_EXAMPLED_KCOM_H__
#define __JNX_EXAMPLED_KCOM_H__


/* interval of periodic timer, in seconds */
#define JNX_EXAMPLED_KCOM_TIMER_PERIOD      60

int jnx_exampled_kcom_init (evContext ctxt);

#endif /* __JNX_EXAMPLED_KCOM_H__ */



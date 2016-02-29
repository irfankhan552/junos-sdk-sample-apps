#
# $Id: Makefile 346460 2009-11-14 05:06:47Z ssiano $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

LIB = ht

# don't link against shared libraries
NOSHARED = yes

SRCS = \
	hashtable.c \
	hashtable_itr.c
	
HDRS = \
	hashtable.h \
	hashtable_itr.h

SRC_LIBS += \
	${LIBM}

WARNING_SET = HIGH

.include <bsd.lib.mk>

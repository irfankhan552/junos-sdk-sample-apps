#
# $Id: Makefile 346460 2009-11-14 05:06:47Z ssiano $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

# Mini HTTP Library from http://mihl.sourceforge.net/

LIB = mihl

# don't link against shared libraries
NO_SHARED = yes

.if ${MACHINE} != "xlr"
# make this one a shared lib on i386
SHLIB_MAJOR = 1
SHLIB_MINOR = 0
.endif

SRCS = \
	b64.c \
	mihl.c \
	tcp_utils.c

HDRS = \
	mihl.h

WARNING_SET = HIGH

.include <bsd.lib.mk>

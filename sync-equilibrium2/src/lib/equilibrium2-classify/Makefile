#
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
# 
# Copyright (c) 2008, Juniper Networks, Inc.
# All rights reserved.
#

LIB = equilibrium2-classify

SRCS = \
	equilibrium2-classify_ctrl.c \
	equilibrium2-classify_data.c \
	equilibrium2-classify_entry.c

SHLIB_MAJOR = 1
SHLIB_MINOR = 0

# Use this flag for Services SDK API access
NEED_JUNOS_MP_SDK = yes

SRC_LIBS += \
	${LIBMP-SDK} \
	${LIBMSP-SVCS} \
	${LIBISC} \
	${LIBJUNOS-SDK} \
	${LIBPROVIDER} \
	${LIBEVENT} \
	${LIBJIPC} \
	${LIBCONN} \
	${LIBJUNIPER}

CFLAGS += -I${.CURDIR}/../../shared/h

LDFLAGS += -export-dynamic

.include <bsd.lib.mk>


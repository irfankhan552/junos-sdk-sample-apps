#
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
# 
# Copyright (c) 2008, Juniper Networks, Inc.
# All rights reserved.
#

LIB = equilibrium2-balance

SRCS = \
	equilibrium2-balance_ctrl.c \
	equilibrium2-balance_data.c \
	equilibrium2-balance_entry.c

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

#LDFLAGS += -export-dynamic

.include <bsd.lib.mk>


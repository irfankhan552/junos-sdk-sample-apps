#
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
# 
# Copyright (c) 2009, Juniper Networks, Inc.
# All rights reserved.
#

LIB = passthru-plugin

SRCS = \
	passthru_main.c

SHLIB_MAJOR = 1
SHLIB_MINOR = 0

WARNINGS_SET = HIGH
W_cast-align_mips=

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

LDFLAGS += -export-dynamic

.include <bsd.lib.mk>

# 
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
#
# Copyright (c) 2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = dpm-ctrl

SRCS = \
	dpm-ctrl_config.c \
	dpm-ctrl_conn.c \
	dpm-ctrl_dfw.c \
	dpm-ctrl_http.c \
	dpm-ctrl_main.c

# added shared/h to include path:
CFLAGS += -I${.CURDIR}/../../shared/h

# Compile with Big TLB support
BTLB_BINARY = YES

WARNINGS_SET = HIGH

# Use this flag for Services SDK API access
NEED_JUNOS_MP_SDK = yes

# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =

#This one only for MP apps
# skip over "warning: cast increases required alignment of target type"
W_cast-align_mips =

DPLIBS += \
	${LIBMIHL} \
	${LIBISC} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBDFWD} \
	${LIBDFW} \
	${LIBNM} \
	${LIBMSVCS-PMON} \
	${LIBMSP-SVCS} \
	${LIBMP-SDK} \
	${LIBVRFUTIL} \
	${LIBRTSOCK} \
	${LIBJUNIPER} \
	${LIBTHR} \
	${LIBPROVIDER}

.include <version.mk>
.include <bsd.prog.mk>

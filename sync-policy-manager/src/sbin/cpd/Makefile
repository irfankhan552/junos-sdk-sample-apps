#
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = cpd

SRCS = \
	cpd_config.c \
	cpd_conn.c \
	cpd_kcom.c \
	cpd_http.c \
	cpd_main.c

# added shared/h to include path:
CFLAGS += -I${.CURDIR}/../../shared/h

WARNINGS_SET = HIGH

# Use this flag for Services SDK API access
NEED_JUNOS_MP_SDK = yes

# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =

#This one only for MP apps
W_cast-align_mips =

DPLIBS += \
	${LIBMIHL} \
	${LIBMP-SDK} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBISC} \
	${LIBJUNOS-SDK} \
	${LIBJUNIPER} \
	${LIBRTSOCK} \
	${LIBPROVIDER} \
	${LIBTHR}

.include <version.mk>
.include <bsd.prog.mk>

# 
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = pfd

SRCS = \
	pfd_config.c \
	pfd_conn.c \
	pfd_kcom.c \
	pfd_main.c \
	pfd_nat.c \
	pfd_packet.c

# added shared/h to include path:
CFLAGS += -I${.CURDIR}/../../shared/h

WARNINGS_SET = HIGH

# Use this flag for Services SDK API access
NEED_JUNOS_MP_SDK = yes

# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =

# This one only for Services apps
W_cast-align_mips =

DPLIBS += \
	${LIBMP-SDK} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBISC} \
	${LIBVRFUTIL} \
	${LIBJUNOS-SDK} \
	${LIBJUNIPER} \
	${LIBRTSOCK} \
	${LIBPROVIDER} \
	${LIBTHR} \
	${LIBHT} \
	${LIBM}

.include <version.mk>
.include <bsd.prog.mk>

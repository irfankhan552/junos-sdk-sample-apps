#
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
#
# SDK gateway example application - dataplane daemon
#
# Copyright (c) 2006-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = jnx-flow-data

SRCS = \
	jnx-flow-data_control.c \
	jnx-flow-data_main.c \
	jnx-flow-data_packet.c \
	jnx-flow-data_utils.c

WARNINGS_SET = HIGH
# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align_mips =

DPLIBS += \
	${LIBISC} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBMP-SDK} \
	${LIBJUNIPER} \
	${LIBTHR}

# Use this flag for Services SDK API access
NEED_JUNOS_MP_SDK = yes
NO_SHARED = yes

.include <version.mk>
.include <bsd.prog.mk>

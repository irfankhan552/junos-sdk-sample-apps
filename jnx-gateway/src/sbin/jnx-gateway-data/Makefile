 
# $Id: Makefile 418048 2010-12-30 18:58:42Z builder $
#
# SDK gateway example application - dataplane daemon
#
#
# Copyright (c) 2006-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = jnx-gateway-data

SRCS = \
	jnx-gateway-data_control.c \
	jnx-gateway-data_main.c \
	jnx-gateway-data_packet.c \
	jnx-gateway-data_utils.c

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

.include <version.mk>
.include <bsd.prog.mk>

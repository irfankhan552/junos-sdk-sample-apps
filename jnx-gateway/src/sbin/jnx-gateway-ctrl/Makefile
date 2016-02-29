#
# $Id: Makefile 418048 2010-12-30 18:58:42Z builder $
#
# SDK gateway example application - dataplane control daemon
#
#
# Copyright (c) 2006-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = jnx-gateway-ctrl

SRCS = \
	jnx-gateway-ctrl_ctrl.c \
	jnx-gateway-ctrl_data.c \
	jnx-gateway-ctrl_gre.c \
	jnx-gateway-ctrl_main.c \
	jnx-gateway-ctrl_mgmt.c

WARNINGS_SET = HIGH

# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align_mips =

DPLIBS += \
	${LIBISC} \
	${LIBCONN} \
	${LIBMP-SDK} \
	${LIBJIPC} \
	${LIBSSD} \
	${LIBUTIL} \
	${LIBJUNIPER} \
	${LIBTHR}

LDFLAGS += -export-dynamic

# Use this flag for Services SDK API access
NEED_JUNOS_MP_SDK = yes

.include <version.mk>
.include <bsd.prog.mk>

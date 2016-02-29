#
# $Id: Makefile 418048 2010-12-30 18:58:42Z builder $
#
# SDK gateway example application - re daemon
#
#
# Copyright (c) 2006-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = jnx-gateway-mgmt

SRCS = \
	jnx-gateway-mgmt_config.c \
	jnx-gateway-mgmt_conn.c \
	jnx-gateway-mgmt_kcom.c \
	jnx-gateway-mgmt_main.c \
	jnx-gateway-mgmt_ui.c

CFLAGS += -I${.CURDIR}/../common

WARNINGS_SET = HIGH
# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align =

DPLIBS += \
	${LIBKVM} \
	${LIBDDL-ACCESS} \
	${LIBMEMORY} \
	${LIBDDL-HELPERS} \
	${LIBISC} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBUI-ODL} \
	${LIBRTSOCK} \
	${LIBJUNOS-SDK} \
	${LIBEVENT} \
	${LIBVRFUTIL} \
	${LIBXMLUTIL} \
	${LIBDAEMON-CONTROL} \
	${LIBDDL-HELPERS} \
	${LIBUTIL} \
	${LIBM} \
	${LIBJNX-GATEWAY-ODL} \
	${LIBJUNOSCRIPT} \
	${LIBRTSFILTER} \
	${LIBJUNIPER} \
	${LIBDDL-ACCESS} \
	${LIBPROVIDER}

LDFLAGS += -export-dynamic

CFLAGS += -DJNX_GATEWAY_MGMT_ODL_H="<xmltags/${PROVIDER_PREFIX}-gateway-mgmt_odl.h>"
CFLAGS += -DJNX_GATEWAY_MGMT_OUT_H="<feature/${PROVIDER_PREFIX}-gateway-mgmt_out.h>"
CFLAGS += -DJNX_GATEWAY_MGMT_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-gateway-mgmt_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

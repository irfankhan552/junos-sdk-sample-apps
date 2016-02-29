#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# SDK gateway example application - re daemon
#
# Copyright (c) 2006-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = jnx-flow-mgmt

SRCS = \
	jnx-flow-mgmt_main.c \
	jnx-flow-mgmt_conn.c \
	jnx-flow-mgmt_kcom.c \
	jnx-flow-mgmt_config.c \
	jnx-flow-mgmt_ui.c \
	jnx-flow-mgmt_rule.c \
	jnx-flow-mgmt_svc_set.c \
	jnx-flow-mgmt_ssrb.c

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
	${LIBJNX-FLOW-ODL} \
	${LIBJUNOSCRIPT} \
	${LIBRTSFILTER} \
	${LIBJUNIPER} \
	${LIBJUNIPER} \
	${LIBDDL-ACCESS} \
	${LIBPROVIDER}

LDFLAGS += -export-dynamic

CFLAGS += -DJNX_FLOW_MGMT_ODL_H="<xmltags/${PROVIDER_PREFIX}-flow-mgmt_odl.h>"
CFLAGS += -DJNX_FLOW_MGMT_OUT_H="<feature/${PROVIDER_PREFIX}-flow-mgmt_out.h>"
CFLAGS += -DJNX_FLOW_MGMT_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-flow-mgmt_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

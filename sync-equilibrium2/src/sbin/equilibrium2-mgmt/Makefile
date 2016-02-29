#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = equilibrium2-mgmt

SRCS = \
	equilibrium2-mgmt_config.c \
	equilibrium2-mgmt_kcom.c \
	equilibrium2-mgmt_main.c \
	equilibrium2-mgmt_server.c \
	equilibrium2-mgmt_ui.c

# added shared/h to include path:
CFLAGS += -I${.CURDIR}/../../shared/h

WARNINGS_SET = HIGH
# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align =

DPLIBS += \
	${LIBRTSOCK} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBPMON} \
	${LIBJUNOS-SDK} \
	${LIBISC} \
	${LIBVRFUTIL} \
	${LIBDDL-ACCESS} \
	${LIBJUNOSCRIPT} \
	${LIBMEMORY} \
	${LIBEQUILIBRIUM2-ODL} \
	${LIBUI-ODL} \
	${LIBJUNIPER} \
	${LIBNET-SNMP} \
	${LIBKVM} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBPROVIDER}

LDFLAGS += -export-dynamic

CFLAGS += \
        -DEQUILIBRIUM2_ODL_H="<xmltags/${PROVIDER_PREFIX}-equilibrium2_odl.h>" \
        -DEQUILIBRIUM2_OUT_H="<feature/${PROVIDER_PREFIX}-equilibrium2_out.h>" \
        -DEQUILIBRIUM2_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-equilibrium2_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

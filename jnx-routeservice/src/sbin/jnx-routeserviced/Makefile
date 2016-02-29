#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = jnx-routeserviced

SRCS = \
	jnx-routeserviced_config.c \
	jnx-routeserviced_gencfg.c \
	jnx-routeserviced_main.c \
	jnx-routeserviced_ssd.c \
	jnx-routeserviced_ui.c \
	jnx-routeserviced_util.c

WARNINGS_SET = HIGH
# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align =

DPLIBS += \
	${LIBRTSOCK} \
	${LIBJUNOS-SDK} \
	${LIBISC} \
	${LIBDDL-ACCESS} \
	${LIBJUNOSCRIPT} \
	${LIBMEMORY} \
	${LIBJNX-ROUTESERVICE-ODL} \
	${LIBUI-ODL} \
	${LIBJUNIPER} \
	${LIBNET-SNMP} \
	${LIBKVM} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBSSD} \
	${LIBPROVIDER}		 

LDFLAGS += -export-dynamic

CFLAGS += -DJNX_ROUTESERVICED_ODL_H="<xmltags/${PROVIDER_PREFIX}-routeservice_odl.h>"
CFLAGS += -DJNX_ROUTESERVICED_OUT_H="<feature/${PROVIDER_PREFIX}-routeservice_out.h>"
CFLAGS += -DJNX_ROUTESERVICED_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-routeservice_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

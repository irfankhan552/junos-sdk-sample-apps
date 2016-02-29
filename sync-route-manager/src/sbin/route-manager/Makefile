#
# $Id$
#
# Copyright (c) 2010, Juniper Networks, Inc.
# All rights reserved.
#

PROG = route-manager

SRCS = \
	${PROG}_config.c \
	${PROG}_kcom.c \
	${PROG}_main.c \
	${PROG}_ssd.c

WARNINGS_SET = HIGH
# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align_mips =

DPLIBS += \
	${LIBJUNOS-SDK} \
	${LIBSSD} \
	${LIBRTSOCK} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBISC} \
	${LIBVRFUTIL} \
	${LIBDDL-ACCESS} \
	${LIBJUNOSCRIPT} \
	${LIBMEMORY} \
	${LIBUI-ODL} \
	${LIBJUNIPER} \
	${LIBKCOM} \
	${LIBNET-SNMP} \
	${LIBKVM} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBPROVIDER}

LDFLAGS += -export-dynamic

CFLAGS += \
	-DOUT_H="<feature/${PROVIDER_PREFIX}-${PROG}_out.h>" \
	-DSEQUENCE_H="<feature/${PROVIDER_PREFIX}-${PROG}_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>


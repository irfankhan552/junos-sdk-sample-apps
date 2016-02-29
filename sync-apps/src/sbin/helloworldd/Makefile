#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = helloworldd

SRCS = \
	helloworldd_config.c \
	helloworldd_main.c \
	helloworldd_ui.c

WARNINGS_SET = HIGH
# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =

DPLIBS += \
	${LIBRTSOCK} \
	${LIBJUNOS-SDK} \
	${LIBISC} \
	${LIBDDL-ACCESS} \
	${LIBJUNOSCRIPT} \
	${LIBMEMORY} \
	${LIBHELLOWORLD-ODL} \
	${LIBUI-ODL} \
	${LIBJUNIPER} \
	${LIBNET-SNMP} \
	${LIBKVM} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBJIPC} \
	${LIBPROVIDER}

LDFLAGS += -export-dynamic

CFLAGS += \
        -DHELLOWORLD_ODL_H="<xmltags/${PROVIDER_PREFIX}-helloworld_odl.h>" \
        -DHELLOWORLD_OUT_H="<feature/${PROVIDER_PREFIX}-helloworld_out.h>" \
        -DHELLOWORLD_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-helloworld_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

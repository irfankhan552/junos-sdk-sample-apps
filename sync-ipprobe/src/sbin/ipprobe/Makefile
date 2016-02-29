#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = ipprobe

SRCS = \
	ipprobe.c \
	ipprobe_config.c

WARNINGS_SET = HIGH

# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align =

DPLIBS += \
	${LIBJIPC} \
	${LIBRTSOCK} \
	${LIBJUNOS-SDK} \
	${LIBISC} \
	${LIBDDL-ACCESS} \
	${LIBJUNOSCRIPT} \
	${LIBMEMORY} \
	${LIBUI-ODL} \
	${LIBKVM} \
	${LIBJUNOS-STAT} \
	${LIBIFSTAT} \
	${LIBPMON} \
	${LIBJUNOS-AAA} \
	${LIBJUNIPER} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBPROVIDER}

LDFLAGS += -export-dynamic

CFLAGS += \
        -DIPPROBE_OUT_H="<feature/${PROVIDER_PREFIX}-ipprobe_out.h>" \
        -DIPPROBE_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-ipprobe_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2009, Juniper Networks, Inc.
# All rights reserved.
#

PROG = ipprobe-mt

SRCS = \
	ipprobe-mt_config.c \
	ipprobe-mt_main.c \
	ipprobe-mt_mngr.c \
	ipprobe-mt_rspd.c \
	ipprobe-mt_thrd.c \
	ipprobe-mt_ui.c

WARNINGS_SET = HIGH

# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align =

DPLIBS += \
	${LIBTHR} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBJUNOS-SDK} \
	${LIBISC} \
	${LIBDDL-ACCESS} \
	${LIBJUNOSCRIPT} \
	${LIBMEMORY} \
	${LIBUI-ODL} \
	${LIBIPPROBE-MT-ODL} \
	${LIBKVM} \
	${LIBRTSOCK} \
	${LIBJUNIPER} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBPROVIDER}

LDFLAGS += -export-dynamic

CFLAGS += \
	-DIPPROBE_MT_ODL_H="<xmltags/${PROVIDER_PREFIX}-ipprobe-mt_odl.h>" \
	-DIPPROBE_MT_OUT_H="<feature/${PROVIDER_PREFIX}-ipprobe-mt_out.h>" \
	-DIPPROBE_MT_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-ipprobe-mt_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>


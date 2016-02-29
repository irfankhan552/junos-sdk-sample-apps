#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = counterd
  
SRCS = \
	counterd_config.c \
	counterd_kcom.c \
	counterd_main.c \
	counterd_message.c \
	counterd_ui.c

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
	${LIBCOUNTER-ODL} \
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
	-DCOUNTERD_ODL_H="<xmltags/${PROVIDER_PREFIX}-counter_odl.h>" \
	-DCOUNTERD_OUT_H="<feature/${PROVIDER_PREFIX}-counter_out.h>" \
	-DCOUNTERD_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-counter_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#
  
PROG = hellopics-mgmt
  
SRCS = \
	hellopics-mgmt_config.c \
	hellopics-mgmt_conn.c \
	hellopics-mgmt_ha.c \
	hellopics-mgmt_main.c \
	hellopics-mgmt_ui.c
  
# added shared/h to include path:
CFLAGS += -I${.CURDIR}/../../shared/h

WARNINGS_SET = HIGH
# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align =

DPLIBS += \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBRTSOCK} \
	${LIBJUNOS-SDK} \
	${LIBJUNOS-SYNC} \
	${LIBISC} \
	${LIBDDL-ACCESS} \
	${LIBJUNOSCRIPT} \
	${LIBMEMORY} \
	${LIBHELLOPICS-ODL} \
	${LIBUI-ODL} \
	${LIBNET-SNMP} \
	${LIBKVM} \
	${LIBJUNIPER} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBPROVIDER}


LDFLAGS += -export-dynamic

CFLAGS += \
        -DHELLOPICS_ODL_H="<xmltags/${PROVIDER_PREFIX}-hellopics_odl.h>" \
        -DHELLOPICS_OUT_H="<feature/${PROVIDER_PREFIX}-hellopics-mgmt_out.h>" \
        -DHELLOPICS_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-hellopics-mgmt_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

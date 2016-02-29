#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = monitube-mgmt

SRCS = \
	monitube-mgmt_config.c \
	monitube-mgmt_conn.c \
	monitube-mgmt_kcom.c \
	monitube-mgmt_main.c \
	monitube-mgmt_ui.c

# to turn on debugging with symbols:
#DEBUG_FLAGS = -g

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
	${LIBMONITUBE-ODL} \
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
        -DMONITUBE_ODL_H="<xmltags/${PROVIDER_PREFIX}-monitube_odl.h>" \
        -DMONITUBE_OUT_H="<feature/${PROVIDER_PREFIX}-monitube-mgmt_out.h>" \
        -DMONITUBE_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-monitube-mgmt_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

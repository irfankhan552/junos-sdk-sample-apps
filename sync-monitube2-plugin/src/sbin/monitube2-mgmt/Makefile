#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2009, Juniper Networks, Inc.
# All rights reserved.
#

PROG = monitube2-mgmt

SRCS = \
	monitube2-mgmt_config.c \
	monitube2-mgmt_conn.c \
	monitube2-mgmt_kcom.c \
	monitube2-mgmt_main.c \
	monitube2-mgmt_ui.c

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
	${LIBMONITUBE2-ODL} \
	${LIBUI-ODL} \
	${LIBJUNIPER} \
	${LIBNET-SNMP} \
	${LIBKVM} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBPROVIDER} \
	${LIBSVCS-MGMT}

LDFLAGS += -export-dynamic

CFLAGS += \
        -DMONITUBE2_ODL_H="<xmltags/${PROVIDER_PREFIX}-monitube2_odl.h>" \
        -DMONITUBE2_OUT_H="<feature/${PROVIDER_PREFIX}-monitube2-mgmt_out.h>" \
        -DMONITUBE2_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-monitube2-mgmt_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

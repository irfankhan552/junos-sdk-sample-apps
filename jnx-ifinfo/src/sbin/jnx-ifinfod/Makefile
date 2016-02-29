#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Makefile - Build rules for ifinfod
#
# Copyright (c) 2006-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = jnx-ifinfod

SRCS = \
	jnx-ifinfod_config.c \
	jnx-ifinfod_conn.c \
	jnx-ifinfod_kcom.c \
	jnx-ifinfod_main.c \
	jnx-ifinfod_mthd.c \
	jnx-ifinfod_snmp.c \
	jnx-ifinfod_ui.c \
	jnx-ifinfod_util.c

WARNINGS_SET = HIGH
# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align =

DPLIBS += \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBRTSOCK} \
	${LIBJUNOS-SDK} \
	${LIBJUNOS-STAT} \
	${LIBIFSTAT} \
	${LIBISC} \
	${LIBDDL-ACCESS} \
	${LIBJUNOSCRIPT} \
	${LIBMEMORY} \
	${LIBJNX-IFINFO-ODL} \
	${LIBUI-ODL} \
	${LIBKVM} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBNET-SNMP} \
	${LIBJNET-SNMP} \
	${LIBJSNMP} \
	${LIBJUNIPER} \
	${LIBVERIFY} \
	${LIBPROVIDER} 

LDFLAGS += -export-dynamic

CFLAGS += -DJNX_IFINFOD_ODL_H="<xmltags/${PROVIDER_PREFIX}-ifinfo_odl.h>"
CFLAGS += -DJNX_IFINFOD_OUT_H="<feature/${PROVIDER_PREFIX}-ifinfo_out.h>"
CFLAGS += -DJNX_IFINFOD_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-ifinfo_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

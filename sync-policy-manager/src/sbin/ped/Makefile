#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = ped

SRCS = \
	ped_config.c \
	ped_conn.c \
	ped_filter.c \
	ped_kcom.c \
	ped_main.c \
	ped_policy_table.c \
	ped_schedule.c \
	ped_script.c \
	ped_service_route.c \
	ped_services.c \
	ped_snmp.c \
	ped_ssd.c \
	ped_ui.c

#Used for the sync application:
CFLAGS += -I${.CURDIR}/../../shared/h

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
	${LIBPE-ODL} \
	${LIBUI-ODL} \
	${LIBSSD} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBDFWD} \
	${LIBDFW} \
	${LIBNM} \
	${LIBNET-SNMP} \
	${LIBJSNMP} \
	${LIBJUNIPER} \
	${LIBVERIFY} \
	${LIBKVM} \
	${LIBVRFUTIL} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBHT} \
	${LIBM} \
	${LIBPROVIDER} \
	${LIBJNET-SNMP}

LDFLAGS += -export-dynamic

CFLAGS += \
        -DPE_ODL_H="<xmltags/${PROVIDER_PREFIX}-pe_odl.h>" \
        -DPE_OUT_H="<feature/${PROVIDER_PREFIX}-pe_out.h>" \
        -DPE_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-pe_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

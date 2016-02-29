#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#
  
PROG = equilibrium-mgmt
  
SRCS = \
	equilibrium-mgmt_config.c \
	equilibrium-mgmt_conn.c \
	equilibrium-mgmt_kcom.c \
	equilibrium-mgmt_main.c \
	equilibrium-mgmt_ui.c
  
  
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
	${LIBEQUILIBRIUM-ODL} \
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
        -DEQUILIBRIUM_ODL_H="<xmltags/${PROVIDER_PREFIX}-equilibrium_odl.h>" \
        -DEQUILIBRIUM_OUT_H="<feature/${PROVIDER_PREFIX}-equilibrium-mgmt_out.h>" \
        -DEQUILIBRIUM_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-equilibrium-mgmt_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

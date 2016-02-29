#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = jnx-routeserviced

SRCS = \
	jnx-routeserviced_config.cc \
	jnx-routeserviced_main.cc \
	jnx-routeserviced_ssd.cc \
	jnx-routeserviced_ui.cc 

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
	${LIBJNX-CC-ROUTESERVICE-ODL} \
	${LIBUI-ODL} \
	${LIBJUNIPER} \
	${LIBNET-SNMP} \
	${LIBKVM} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBSSD} \
	${LIBPROVIDER} \
	${LIBSTDCPLUSPLUS} \
	${LIBM} 

LDFLAGS += -export-dynamic

CFLAGS += -DJNX_CC_ROUTESERVICED_ODL_H="<xmltags/${PROVIDER_PREFIX}-cc-routeservice_odl.h>"
CFLAGS += -DJNX_CC_ROUTESERVICED_OUT_H="<feature/${PROVIDER_PREFIX}-cc-routeservice_out.h>"
CFLAGS += -DJNX_CC_ROUTESERVICED_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-cc-routeservice_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>


#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Makefile - Build rules for jnx-exampled.
#
# Copyright (c) 2005-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = jnx-exampled

SRCS = \
      jnx-exampled_config.c \
      jnx-exampled_main.c \
      jnx-exampled_kcom.c \
      jnx-exampled_ui.c \
      jnx-exampled_snmp.c \
      jnx-exampled_trap.c \
      jnx-exampled_mthd.c

WARNINGS_SET = HIGH
# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =

DPLIBS += \
	${LIBJUNOS-SDK} \
	${LIBRTSOCK} \
	${LIBISC} \
	${LIBDDL-ACCESS} \
	${LIBJUNOSCRIPT} \
	${LIBMEMORY} \
	${LIBDDL-JUNOS} \
	${LIBJNX-EXAMPLE-ODL} \
	${LIBUI-ODL} \
	${LIBNET-SNMP} \
	${LIBJNET-SNMP} \
	${LIBKVM} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBPROVIDER} \
	${LIBJSNMP} \
	${LIBJUNIPER} \
	${LIBVERIFY}

# This list is not necessary for JUNOS SDK applications:
DPADD += \
	${LIBJNX-EXAMPLE-DD}

LDFLAGS += -export-dynamic

CFLAGS += -DJNX_EXAMPLED_ODL_H="<xmltags/${PROVIDER_PREFIX:Ujnx}-example_odl.h>"
CFLAGS += -DJNX_EXAMPLED_OUT_H="<feature/${PROVIDER_PREFIX:Ujnx}-example_out.h>"
CFLAGS += -DJNX_EXAMPLED_SEQUENCE_H="<feature/${PROVIDER_PREFIX:Ujnx}-example_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

#
# To turn optimization off, mk DEBUG_FLAGS=-O0. warnings.mk will make sure
# -Wuninitialized is removed (conflicts with this option).
# 

#
# $Id: Makefile 418048 2010-12-30 18:58:42Z builder $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = jnx-msprsmd

SRCS = \
	jnx-msprsmd_config.c \
	jnx-msprsmd_conn.c \
	jnx-msprsmd_kcom.c \
	jnx-msprsmd_ssd.c \
	jnx-msprsmd_main.c \
	jnx-msprsmd_ui.c

WARNINGS_SET = HIGH
# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =
W_cast-align =

DPLIBS += \
	${LIBRTSOCK} \
	${LIBJUNOS-SDK} \
	${LIBVRFUTIL} \
	${LIBISC} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBDDL-ACCESS} \
	${LIBJUNOSCRIPT} \
	${LIBMEMORY} \
	${LIBJNX-MSPRSM-ODL} \
	${LIBUI-ODL} \
	${LIBJUNIPER} \
	${LIBNET-SNMP} \
	${LIBKVM} \
	${LIBDAEMON-CONTROL} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBSSD} \
	${LIBPROVIDER}

LDFLAGS += -export-dynamic

CFLAGS += -DJNX_MSPRSM_OUT_H="<feature/${PROVIDER_PREFIX}-msprsm_out.h>"
CFLAGS += -DJNX_MSPRSM_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-msprsm_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

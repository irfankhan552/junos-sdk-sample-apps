#
# $Id: Makefile 407537 2010-10-29 03:06:16Z angelat $
#
# Copyright (c) 2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = dpm-mgmt

SRCS = \
	dpm-mgmt_config.c \
	dpm-mgmt_conn.c \
	dpm-mgmt_kcom.c \
	dpm-mgmt_main.c \
	dpm-mgmt_ui.c

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
	${LIBDPM-ODL} \
	${LIBUI-ODL} \
	${LIBJUNIPER} \
	${LIBEVENT} \
	${LIBDDL-HELPERS} \
	${LIBRTSFILTER} \
	${LIBPROVIDER}

LDFLAGS += -export-dynamic

CFLAGS += \
        -DDPM_ODL_H="<xmltags/${PROVIDER_PREFIX}-dpm_odl.h>" \
        -DDPM_OUT_H="<feature/${PROVIDER_PREFIX}-dpm-mgmt_out.h>" \
        -DDPM_SEQUENCE_H="<feature/${PROVIDER_PREFIX}-dpm-mgmt_sequence.h>"

.include <version.mk>
.include <bsd.prog.mk>

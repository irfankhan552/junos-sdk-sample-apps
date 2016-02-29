# 
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
#
# Copyright (c) 2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = monitube-data

SRCS = \
	monitube-data_config.c \
	monitube-data_conn.c \
	monitube-data_ha.c \
	monitube-data_main.c \
	monitube-data_packet.c \
	monitube-data_rtp.c
	
# added shared/h to include path:
CFLAGS += -I${.CURDIR}/../../shared/h

WARNINGS_SET = HIGH

# Use this flag for Services SDK API access
NEED_JUNOS_MP_SDK = yes

# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =

# This one only for Services SDK apps
W_cast-align_mips =

DPLIBS += \
	${LIBISC} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBMP-SDK} \
	${LIBJUNIPER} \
	${LIBMSP-SVCS} \
	${LIBPROVIDER} \
	${LIBTHR} \
	${LIBJUNOS-SYNC}

.include <version.mk>
.include <bsd.prog.mk>


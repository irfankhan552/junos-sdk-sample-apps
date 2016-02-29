# 
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
#
# Copyright (c) 2009, Juniper Networks, Inc.
# All rights reserved.
#

LIB = monitube2-plugin

SRCS = \
	monitube2-data_config.c \
	monitube2-data_conn.c \
	monitube2-data_main.c \
	monitube2-data_packet.c \
	monitube2-data_rtp.c
	
SHLIB_MAJOR = 1
SHLIB_MINOR = 0
	
# added shared/h to include path:
CFLAGS += -I${.CURDIR}/../../shared/h

WARNINGS_SET = HIGH

# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =

#This one only for MP apps
W_cast-align_mips =

# Use this flag for Services SDK API access
NEED_JUNOS_MP_SDK = yes

SRC_LIBS += \
	${LIBPTHREAD} \
	${LIBISC} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBMP-SDK} \
	${LIBJUNIPER} \
	${LIBMSP-SVCS} \
	${LIBJUNOS-SDK} \
	${LIBPROVIDER}
	
LDFLAGS += -export-dynamic

.include <bsd.lib.mk>


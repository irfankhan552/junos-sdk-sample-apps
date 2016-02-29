# 
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

PROG = ipsnooper

SRCS = \
	ipsnooper_main.c \
	ipsnooper_packet.c \
	ipsnooper_server.c

WARNINGS_SET = HIGH

# Use this flag for Services SDK API access
NEED_JUNOS_MP_SDK = yes

# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =

#This one only for MP apps
W_cast-align_mips =

DPLIBS += \
	${LIBMP-SDK} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBISC} \
	${LIBJUNIPER} \
	${LIBTHR} 

.include <version.mk>
.include <bsd.prog.mk>

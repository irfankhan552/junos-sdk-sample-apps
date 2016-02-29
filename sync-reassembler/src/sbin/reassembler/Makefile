# 
# $Id: Makefile 365138 2010-02-27 10:16:06Z builder $
#
# Copyright (c) 2009, Juniper Networks, Inc.
# All rights reserved.
#

PROG = reassembler

SRCS = \
	reassembler_main.c \
	reassembler_packet.c
	
WARNINGS_SET = HIGH

# Use this flag for Services SDK API access
NEED_JUNOS_MP_SDK = yes

# Leave out -Waggregate-return as some eventlib calls return structs.
W_aggregate-return =

#This one only for MP apps
W_cast-align_mips =

DPLIBS += \
	${LIBM} \
	${LIBISC} \
	${LIBMP-SDK} \
	${LIBMSP-SVCS} \
	${LIBCONN} \
	${LIBJIPC} \
	${LIBJUNIPER} \
	${LIBTHR}

.include <version.mk>
.include <bsd.prog.mk>

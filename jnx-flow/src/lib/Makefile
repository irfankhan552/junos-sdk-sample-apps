#
# $Id: Makefile 359317 2010-01-22 21:15:05Z ranjini $
#
# Copyright (c) 2006-2008, Juniper Networks, Inc.
# All rights reserved.
#

# define the subdirs
.if make(hostuilibs)
SUBDIR ?= ddl odl
.else
# define this subdir for SYNC
SUBDIR = libht

.if ${MACHINE} == "xlr"

SUBDIR += \
	libmihl \
	libjnx-mspexampled-ctrl \
	equilibrium2-classify \
	equilibrium2-balance \
	monitube2-plugin \
	passthru-plugin

.else

SUBDIR += \
	  odl \
	  ddl \
	  rrdl

.endif
.endif

hostuilibs: _SUBDIR

.include <bsd.subdir.mk>


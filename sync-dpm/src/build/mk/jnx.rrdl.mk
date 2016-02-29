#
# $Id: jnx.rrdl.mk,v 1.2.10.2 2009-08-27 02:10:54 ssiano Exp $
#
# Copyright (c) 2009, Juniper Networks, Inc.
# All rights reserved.
#

.if defined(PROVIDER_POLICIES)
.for p in ${PROVIDER_POLICIES}
CRRDL_PATH += ${LIBRRDL:H:H:S,${OBJTOP_JUNOS},${SRCTOP},}/input/policy/${p}
.endfor
CRRDL_PATH += ${LIBRRDL:H:H:S,${OBJTOP_JUNOS},${HOST_OBJTOP_JUNOS},}/input
.endif

.include "${SB_BACKING_SB}/src/build/mk/jnx.rrdl.mk"


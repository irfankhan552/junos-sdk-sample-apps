#
# $Id: bsd.libnames.mk,v 1.6.78.1.4.1 2009-08-13 05:06:06 kdickman Exp $
#
# Copyright (c) 2006-2007, Juniper Networks, Inc.
# All rights reserved.
#

# Pick up the Juniper libnames
.include "${SB_BACKING_SB}/src/build/mk/jnx.libnames-pub.mk"
.include "${SB_BACKING_SB}/src/build/mk/bsd.libnames.mk"

# include the definitions of the libraries created by SDK user
.include "libnames.mk"

# avoid duplicate definitions
.ifndef __sdk_libnames__
__sdk_libnames__ = 1

# this is where to find our own ddl files, and also where to 
# find Juniper-provided ones
INCLUDES_ddl = -I${RELOBJTOP}/lib/ddl -I${RELSRCTOP_JUNOS}/lib/ddl
INCLUDES_odl = -I${RELOBJTOP}/lib/odl -I${RELSRCTOP_JUNOS}/lib/odl

.endif

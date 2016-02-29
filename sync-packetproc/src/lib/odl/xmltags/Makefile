#
# $Id: Makefile 346460 2009-11-14 05:06:47Z ssiano $
#
# Copyright (c) 2006-2007, Juniper Networks, Inc.
# All rights reserved.
#

.include "../Makefile.inc"

# we take our input from here and normal junos
# Read the original JUNOS input.mk first and then all our *.input.mk
# That way any makefile targets and variables in the
# original JUNOS odl Makefiles can be overwritten if required.

INPUTS != ls -f ${SDK_ODL_INC}/input/public-input.mk ${.CURDIR:H}/input/*.input.mk 2>/dev/null || echo

srcdir := ${SDK_ODL_INC}/xmltags
.PATH: ${srcdir} 

# Do the work.
.include "${srcdir}/Makefile"


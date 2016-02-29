#
# $Id: Makefile 378525 2010-05-07 13:11:44Z thejesh $
#
# Copyright (c) 2006-2007, Juniper Networks, Inc.
# All rights reserved.
#

.include "../Makefile.inc"

# Read the public JUNOS public-input.mk first and then all our *.input.mk
# That way any makefile targets and variables in the
# original JUNOS ddl Makefiles can be overwritten if required.

INPUTS != ls -f ${SDK_DDL_INC}/input/public-input.mk ${.CURDIR:H}/input/*.input.mk 2>/dev/null || echo 

srcdir := ${SDK_DDL_INC}/feature
.PATH: ${srcdir} 

# Do the work.
.include "${srcdir}/Makefile"

.if ${.OBJDIR} != ${.CURDIR}
# Include generated *.dd files in cleanup.
CLEANFILES += *.dd
.endif

# Before compiling the ddl files, generate sdk-common-include.dd, which
# can be included in *.cmd.dd & *.cnf.dd  files.
.for f in ${FEATURES}
$f_out.o: sdk-common-include.dd
.endfor

# Define the install directory path in sdk-common-include.dd.
sdk-common-include.dd:
	echo "#define JUNOS_SDK_INSTALLDIR \"${JUNOS_SDK_INSTALLDIR}\"" > ${.TARGET}


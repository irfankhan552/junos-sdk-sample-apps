#
# $Id: Makefile 418086 2010-12-30 21:26:07Z jvanhorne $
#
# Copyright (c) 2006-2007, Juniper Networks, Inc.
# All rights reserved.
#

# This is the top-level makefile for the juniper sdk examples that are
# shipped with the junos-sdk.  It is a straightforward sub-directory
# makefile which builds the following subdirectories:
#
# sbin -- contains the example application(s)
# lib  -- contains the ddl and odl libraries for the example app(s)
# etc  -- builds the certificate chain for the example package(s)
.include <SDK-VERSION>

# define the subdirs
.if make(hostuilibs)
SUBDIR ?= lib
.else
SUBDIR?= etc lib sbin 
.endif

# include the system subdirectory makefile which does the work
.include <bsd.subdir.mk>

# set the default rules for build_arch.${MACHINE} and for release
BUILD_ARCH_USE: .USE
	@echo "[Building targets for MACHINE=${.TARGET:E} ...]"
	(cd ${_CURDIR} && MACHINE=${.TARGET:E} MACHINE_ARCH=${MACHINE_ARCH.${.TARGET:E}} ${.MAKE})
	
SDK_MACHINE_LIST = i386 xlr octeon
${SDK_MACHINE_LIST:%=build_arch.%}: BUILD_ARCH_USE

.PHONY: sdk-version
sdk-version:
	@echo "SDK version: ${JUNOS_SDK_VERSION_MAJOR}.${JUNOS_SDK_VERSION_MINOR}"

# 'mk release' will build the package for installation on a JUNOS router
release: .PHONY ${SDK_MACHINE_LIST:%=build_arch.%}
	(cd ${_CURDIR}/release && ${.MAKE})

# 'mk hosttools' will build just the buildhost-targeted tools.  
HOSTTOOLS?= etc/certs

hosttools:
	cd ${.CURDIR} && ${HOSTTOOL_ENV} ${.MAKE} HOSTPROG=yes SUBDIR="${HOSTTOOLS}"

# 'mk hostuilibs' will build just the hostprog version of DDL/ODL libs for UI simulator 
hostuilibs: _SUBDIR

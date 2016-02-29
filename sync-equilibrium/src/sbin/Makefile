#
# $Id: Makefile 366965 2010-03-09 15:13:36Z taoliu $
#
# Copyright (c) 2006-2008, Juniper Networks, Inc.
# All rights reserved.
#

# define the subdirectories to be built

.if ${MACHINE} == "xlr"

SUBDIR = \
	jnx-gateway-ctrl \
	jnx-gateway-data \
	jnx-flow-data \
	hellopics-ctrl \
	hellopics-data \
	ipsnooper \
	packetproc-data \
	pfd \
	cpd \
	equilibrium-data \
	monitube-data \
	dpm-ctrl \
	jnx-mspexampled-ctrl \
	jnx-ifinfod-ctrl \
	reassembler

.else

SUBDIR = \
	jnx-exampled \
	jnx-gateway-mgmt \
	jnx-routeserviced \
	jnx-ifinfod \
	jnx-cc-routeserviced \
	jnx-flow-mgmt \
	counterd \
	helloworldd \
	ped \
	psd \
	equilibrium-mgmt \
	equilibrium2-mgmt \
	monitube-mgmt \
	monitube2-mgmt \
	dpm-mgmt \
	ipprobe \
	ipprobe-manager \
	ipprobe-mt \
	hellopics-mgmt \
	route-manager \
	jnx-msprsmd

.endif

.include <bsd.subdir.mk>

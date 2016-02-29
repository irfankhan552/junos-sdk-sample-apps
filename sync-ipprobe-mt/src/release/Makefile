#
# $Id: Makefile 366965 2010-03-09 15:13:36Z taoliu $
#
# Copyright (c) 2006-2008, Juniper Networks, Inc.
# All rights reserved.
#
#
# This makefile's joy in life is to take the necessary executables,
# libraries, and other stuff and create a package that can be
# installed on a router running JUNOS.
#
# For a package ${PKG}, the makefile will use ${PKG}.manifest
# to create a signed package at $SB/ship/${PKG}-${RELEASE}.tgz.
#
# There are four types of packages:
#
# - RE package. 
#      Just a regular JUNOS package, behaves similar to jkernel/jroute. 
#      Is installed on the RE.
#      RE package targets depend on SDK_PKG_RE.
# - PIC package. 
#      Just a regular JUNOS package, behaves similar to jkernel/jroute. 
#      Is installed on the PIC.
#      PIC package targets depend on SDK_PKG_PIC.
# - Container package. 
#      Contains PIC packages. 
#      Is installed on the RE.
#      Removes old and copies new PIC packages to the predefined RE location.
#      These packages are later picked up and installed on the PIC.
#      CNTR package targets depend on SDK_PKG_CNTR
#      and on the enclosed package targets 
# - Bundle package. 
#      Contains RE and/or Container packages. 
#      Is installed on the RE, and is not uninstallable.
#      Removes old and installs new enclosed packages on the RE.
#      CNTR package targets depend on SDK_PKG_CNTR 
#      and on the enclosed package targets 


#
# List of the packages to be built by default, add yours here
#
SDK_PKG_MAIN = \
	jnx-example \
	jnx-gateway-bundle \
	route-manager \
	sync-apps \
	sync-ipprobe \
	sync-ipprobe-mt \
	sync-ipsnooper \
	sync-hellopics-apps-bundle \
	sync-policy-manager-bundle \
	sync-packetproc-data \
	sync-equilibrium-bundle \
	sync-equilibrium2-bundle \
	sync-dpm-bundle \
	sync-monitube-bundle \
	sync-monitube2-bundle \
	sync-reassembler \
	sync-passthru-plugin \
	jnx-routeservice \
	jnx-cc-routeservice \
	jnx-ifinfo-bundle \
	jnx-flow-bundle \
	jnx-mspexampled-bundle \
	jnx-msprsm

#
# Package targets
#

# jnx-example
jnx-example:                     SDK_PKG_RE

# jnx-gateway
jnx-gateway-bundle:              SDK_PKG_BUNDLE
jnx-gateway-ctrl:                SDK_PKG_CNTR
jnx-gateway-data:                SDK_PKG_CNTR
jnx-gateway-mgmt:                SDK_PKG_RE
jnx-gateway-ctrl-pic:            SDK_PKG_PIC
jnx-gateway-data-pic:            SDK_PKG_PIC
jnx-gateway-ctrl:                jnx-gateway-ctrl-pic
jnx-gateway-data:                jnx-gateway-data-pic
jnx-gateway-bundle:              jnx-gateway-mgmt jnx-gateway-ctrl jnx-gateway-data

# route-manager
route-manager:                   SDK_PKG_RE

# sync-apps
sync-apps:                       SDK_PKG_RE

# sync-ipprobe
sync-ipprobe:                    SDK_PKG_RE

# sync-ipprobe-mt
sync-ipprobe-mt:                 SDK_PKG_RE

# sync-ipsnooper
sync-ipsnooper:                  SDK_PKG_CNTR
sync-ipsnooper-pic:              SDK_PKG_PIC
sync-ipsnooper:                  sync-ipsnooper-pic

# sync-hellopics-apps
sync-hellopics-apps-bundle:      SDK_PKG_BUNDLE
sync-hellopics-apps-ctrl:        SDK_PKG_CNTR
sync-hellopics-apps-data:        SDK_PKG_CNTR
sync-hellopics-apps-mgmt:        SDK_PKG_RE
sync-hellopics-apps-ctrl-pic:    SDK_PKG_PIC
sync-hellopics-apps-data-pic:    SDK_PKG_PIC
sync-hellopics-apps-ctrl:        sync-hellopics-apps-ctrl-pic
sync-hellopics-apps-data:        sync-hellopics-apps-data-pic
sync-hellopics-apps-bundle:      sync-hellopics-apps-mgmt sync-hellopics-apps-ctrl sync-hellopics-apps-data

# sync-policy-manager
sync-policy-manager-bundle:      SDK_PKG_BUNDLE
sync-policy-manager-ctrl:        SDK_PKG_CNTR
sync-policy-manager-data:        SDK_PKG_CNTR
sync-policy-manager-mgmt:        SDK_PKG_RE
sync-policy-manager-ctrl-pic:    SDK_PKG_PIC
sync-policy-manager-data-pic:    SDK_PKG_PIC
sync-policy-manager-ctrl:        sync-policy-manager-ctrl-pic
sync-policy-manager-data:        sync-policy-manager-data-pic
sync-policy-manager-bundle:      sync-policy-manager-mgmt sync-policy-manager-ctrl sync-policy-manager-data

# sync-packetproc
sync-packetproc-data:            SDK_PKG_CNTR
sync-packetproc-data-pic:        SDK_PKG_PIC
sync-packetproc-data:            sync-packetproc-data-pic

# sync-reassembler
sync-reassembler:                SDK_PKG_CNTR
sync-reassembler-pic:            SDK_PKG_PIC
sync-reassembler:                sync-reassembler-pic

# sync-passthru-plugin
sync-passthru-plugin:            SDK_PKG_CNTR
sync-passthru-plugin-pic:        SDK_PKG_PIC
sync-passthru-plugin:            sync-passthru-plugin-pic

# sync-equilibrium
sync-equilibrium-bundle:         SDK_PKG_BUNDLE
sync-equilibrium-data:           SDK_PKG_CNTR
sync-equilibrium-data-pic:       SDK_PKG_PIC
sync-equilibrium-mgmt:           SDK_PKG_RE
sync-equilibrium-data:           sync-equilibrium-data-pic
sync-equilibrium-bundle:         sync-equilibrium-mgmt sync-equilibrium-data

# sync-equilibrium2
sync-equilibrium2-bundle:        SDK_PKG_BUNDLE
sync-equilibrium2-services:      SDK_PKG_CNTR
sync-equilibrium2-balance:       SDK_PKG_PIC
sync-equilibrium2-classify:      SDK_PKG_PIC
sync-equilibrium2-mgmt:          SDK_PKG_RE
sync-equilibrium2-services:      sync-equilibrium2-balance \
                                 sync-equilibrium2-classify
sync-equilibrium2-bundle:        sync-equilibrium2-mgmt \
                                 sync-equilibrium2-services

# sync-monitube (process version)
sync-monitube-bundle:         SDK_PKG_BUNDLE
sync-monitube-data:           SDK_PKG_CNTR
sync-monitube-data-pic:       SDK_PKG_PIC
sync-monitube-mgmt:           SDK_PKG_RE
sync-monitube-data:           sync-monitube-data-pic
sync-monitube-bundle:         sync-monitube-mgmt sync-monitube-data

# sync-monitube2 (plug-in version)
sync-monitube2-bundle:         SDK_PKG_BUNDLE
sync-monitube2-data:           SDK_PKG_CNTR
sync-monitube2-data-pic:       SDK_PKG_PIC
sync-monitube2-mgmt:           SDK_PKG_RE
sync-monitube2-data:           sync-monitube2-data-pic
sync-monitube2-bundle:         sync-monitube2-mgmt sync-monitube2-data

# sync-dpm
sync-dpm-bundle:         SDK_PKG_BUNDLE
sync-dpm-ctrl:           SDK_PKG_CNTR
sync-dpm-ctrl-pic:       SDK_PKG_PIC
sync-dpm-mgmt:           SDK_PKG_RE
sync-dpm-ctrl:           sync-dpm-ctrl-pic
sync-dpm-bundle:         sync-dpm-mgmt sync-dpm-ctrl

# jnx-routeservice
jnx-routeservice:                SDK_PKG_RE

# jnx-cc-routeservice
jnx-cc-routeservice:             SDK_PKG_RE

# jnx-ifinfo
jnx-ifinfo-bundle:               SDK_PKG_BUNDLE
jnx-ifinfo:                      SDK_PKG_RE
jnx-ifinfo-ctrl:                 SDK_PKG_CNTR
jnx-ifinfo-ctrl-pic:             SDK_PKG_PIC
jnx-ifinfo-ctrl:                 jnx-ifinfo-ctrl-pic
jnx-ifinfo-bundle:               jnx-ifinfo jnx-ifinfo-ctrl

#jnx-flow
jnx-flow-bundle:                 SDK_PKG_BUNDLE
jnx-flow-mgmt:                   SDK_PKG_RE
jnx-flow-data:                   SDK_PKG_CNTR
jnx-flow-data-pic:               SDK_PKG_PIC
jnx-flow-data:                   jnx-flow-data-pic
jnx-flow-bundle:                 jnx-flow-mgmt jnx-flow-data

# jnx-mspexampled
jnx-mspexampled-bundle:          SDK_PKG_BUNDLE
jnx-mspexampled-ctrl:            SDK_PKG_CNTR
jnx-mspexampled-ctrl-pic:        SDK_PKG_PIC
jnx-mspexampled-ctrl:            jnx-mspexampled-ctrl-pic
jnx-mspexampled-bundle:          jnx-mspexampled-ctrl

# jnx-msprsm
jnx-msprsm:                      SDK_PKG_RE

#
# Add your package targets here:
#


#
# The magic:
#
MAKE_SDK_PKG= sdk
.include <pkg/sdk-pkg.mk>

#
# $Id: libnames.mk,v 1.19.114.1 2009-09-19 18:51:23 dtang Exp $
#
# Add local definitions of libraries in this file
#

# ODL compiler-generated libraries:

.include <provider-prefix.mk>

LIBJNX-EXAMPLE-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-example-odl.a

LIBJNX-ROUTESERVICE-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-routeservice-odl.a

LIBJNX-CC-ROUTESERVICE-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-cc-routeservice-odl.a

LIBJNX-IFINFO-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-ifinfo-odl.a

LIBHELLOWORLD-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-helloworld-odl.a

LIBCOUNTER-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-counter-odl.a

LIBEQUILIBRIUM-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-equilibrium-odl.a

LIBEQUILIBRIUM2-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-equilibrium2-odl.a

LIBIPPROBE-MT-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-ipprobe-mt-odl.a

LIBMONITUBE-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-monitube-odl.a

LIBMONITUBE2-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-monitube2-odl.a

LIBDPM-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-dpm-odl.a

LIBPE-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-pe-odl.a

LIBHELLOPICS-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-hellopics-odl.a

LIBJNX-FLOW-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-flow-mgmt-odl.a

LIBJNX-GATEWAY-ODL = ${OBJTOP}/lib/odl/xmltags/lib${PROVIDER_PREFIX}-gateway-mgmt-odl.a


# Used in the jnx-exampled Makefile's DPADD list.
# This list is not necessary for JUNOS SDK applications and defining 
# DDL lib variables is not necessary for SDK applications.

LIBJNX-EXAMPLE-DD=  ${OBJTOP}/lib/ddl/feature/lib${PROVIDER_PREFIX}-example-dd.so.1

LIBJNX-IFINFO-DD = ${OBJTOP}/lib/ddl/feature/lib${PROVIDER_PREFIX}-ifinfo-dd.so.1

LIBJNX-ROUTESERVICE-DD= ${OBJTOP}/lib/ddl/feature/lib${PROVIDER_PREFIX}-routeservice-dd.so.1

LIBJNX-CC-ROUTESERVICE-DD= ${OBJTOP}/lib/ddl/feature/lib${PROVIDER_PREFIX}-cc-routeservice-dd.so.1

LIBJNX-FLOW-DD= ${OBJTOP}/lib/ddl/feature/lib${PROVIDER_PREFIX}-flow-mgmt-dd.so.1

LIBJNX-GATEWAY-DD= ${OBJTOP}/lib/ddl/feature/lib${PROVIDER_PREFIX}-gateway-mgmt-dd.so.1

# If you are adding your own library 'libfoo', you can add a line here like
#
# LIBFOO = ${OBJTOP}/lib/libfoo/libfoo.a
#
# and the build will assume that the sources for libfoo are present in
#
# If the library has no sources because it is precompiled, then use
#
# LIBFOO = ${RELSRCTOP}/lib/libfoo/libfoo.a
#
# and the libfoo.a file would be in $SB/src/lib/libfoo
#
# Any public headers for libfoo should be present in
#
# ${SB}/src/lib/libfoo/h/
#
# In the Makefile of the application where you want to use libfoo APIs
# you just need to have a line like
#
# DPLIBS += ${LIBFOO}
#
# and all the CFLAGS, LDFLAGS, etc. will get taken care of by the make
# infrastructure
#
# NOTE: This file is included in $SB/src/build/mk/bsd.libnames.mk before 
# $SB_BACKING_SB/src/build/mk/jnx.libnames.mk and
# $SB_BACKING_SB/src/build/mk/bsd.libnames.mk
#
# If you define a library that is already defined in one of those files, then
# please add your LIBFOO definition to the end of 
# $SB/src/build/mk/bsd.libnames.mk instead of here below in order to override 
# definitions present in those files from the backing sandbox
#
#
# Examples:
# Additional libraries used by SDK Your Net Corporation (SYNC) SDK examples:
#

# for xml (precompiled)
# TODO swap for another lib that is not in the BSB (PR 404499)
# libxml was recently added into the BSB
#
# LIBXML2 = ${RELSRCTOP}/lib/libxml2/libxml2.a

# lib for hashtables (compiled in build)
LIBHT = ${OBJTOP}/lib/libht/libht.a

# lib for HTTP server (compiled in build)
LIBMIHL = ${OBJTOP}/lib/libmihl/libmihl.a

# lib for JNX mspexample (compiled in build)
LIBJNX-MSPEXAMPLED-CTRL = \
    ${OBJTOP}/lib/libjnx-mspexampled-ctrl/libjnx-mspexampled-ctrl.a

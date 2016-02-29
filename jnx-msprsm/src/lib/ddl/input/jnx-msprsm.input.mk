#
# $Id: jnx-msprsm.input.mk,v 1.1.280.1 2009-08-13 05:06:09 kdickman Exp $
#
# Copyright (c) 2008-2009, Juniper Networks, Inc.
# All rights reserved.
#

# File used to define the daemon to mgd for system startup and control
DAEMON_XML+= jnx-msprsm.xml

# The name of the feature that the ddl library will be created for
INPUT_FEATURES+= ${PROVIDER_PREFIX}-msprsm

# for each feature, enumerate the files that comprise the definitions
# for configuration and control
${PROVIDER_PREFIX}-msprsm_INPUT_FILES += \
    jnx-msprsm.cnf.dd \

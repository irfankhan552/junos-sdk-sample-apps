#
# $Id: sync-monitube2.input.mk,v 1.2.4.2 2009-09-05 03:20:46 ssiano Exp $
#
# Copyright (c) 2009, Juniper Networks, Inc.
# All rights reserved.
#

# File used to define the daemon to mgd for system startup and
# control

DAEMON_XML += monitube2_desc.xml

# The name of the feature that the ddl library will be created for
INPUT_FEATURES += \
	${PROVIDER_PREFIX}-monitube2-mgmt

# for each feature, enumerate the files that comprise the definitions
# for configuration and control

${PROVIDER_PREFIX}-monitube2-mgmt_INPUT_FILES += \
	monitube2-mgmt.cnf.dd \
	monitube2-mgmt.cmd.dd


#
# $Id: sync-hellopics.input.mk,v 1.1.92.2.4.1 2009-08-13 05:06:09 kdickman Exp $
#
# Copyright (c) 2009, Juniper Networks, Inc.
# All rights reserved.
#

# File used to define the daemon to mgd for system startup and
# control

DAEMON_XML += hellopics_desc.xml

# The name of the feature that the ddl library will be created for
INPUT_FEATURES += \
	${PROVIDER_PREFIX}-hellopics-mgmt

# for each feature, enumerate the files that comprise the definitions
# for configuration and control

${PROVIDER_PREFIX}-hellopics-mgmt_INPUT_FILES += \
	hellopics-mgmt.cnf.dd \
	hellopics-mgmt.cmd.dd


#
# $Id: ipprobe-mt.input.mk,v 1.2.4.2 2009-09-05 03:20:46 ssiano Exp $
#
# Copyright (c) 2009, Juniper Networks, Inc.
# All rights reserved.
#

# File used to define the daemon to mgd for system startup and
# control

DAEMON_XML += \
	ipprobe-mt.xml

# The name of the feature that the ddl library will be created for
INPUT_FEATURES += \
	${PROVIDER_PREFIX}-ipprobe-mt

# for each feature, enumerate the files that comprise the definitions
# for configuration and control

${PROVIDER_PREFIX}-ipprobe-mt_INPUT_FILES += \
	ipprobe-mt.cnf.dd \
	ipprobe-mt.cmd.dd


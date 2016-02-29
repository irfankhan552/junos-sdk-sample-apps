#
# $Id: sync.input.mk,v 1.11.124.1.4.1 2009-08-13 05:06:09 kdickman Exp $
#
# Copyright (c) 2007-2008, Juniper Networks, Inc.
# All rights reserved.
#

# File used to define the daemon to mgd for system startup and
# control

DAEMON_XML += daemon_desc.xml

# The name of the feature that the ddl library will be created for
INPUT_FEATURES += \
	${PROVIDER_PREFIX}-helloworld \
	${PROVIDER_PREFIX}-counter

# for each feature, enumerate the files that comprise the definitions
# for configuration and control

${PROVIDER_PREFIX}-helloworld_INPUT_FILES += \
	helloworld.cnf.dd \
	helloworld.cmd.dd

${PROVIDER_PREFIX}-counter_INPUT_FILES += \
	counter.cnf.dd \
	counter.cmd.dd


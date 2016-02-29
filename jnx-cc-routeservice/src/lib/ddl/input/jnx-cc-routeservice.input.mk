#
# $Id: jnx-cc-routeservice.input.mk,v 1.2.260.1 2009-08-13 05:06:09 kdickman Exp $
#
# Copyright (c) 2007-2009, Juniper Networks, Inc.
# All rights reserved.
#

# File used to define the daemon to mgd for system startup and control
DAEMON_XML += jnx-cc-routeservice.xml

# The name of the feature that the ddl library will be created for
INPUT_FEATURES += ${PROVIDER_PREFIX}-cc-routeservice

# for each feature, enumerate the files that comprise the definitions
# for configuration and control
${PROVIDER_PREFIX}-cc-routeservice_INPUT_FILES += \
	jnx-cc-routeservice.cnf.dd \
	jnx-cc-routeservice.cmd.dd

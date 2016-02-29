#
# $Id: jnx-cc-routeservice.input.mk,v 1.2.260.1 2009-08-13 05:06:17 kdickman Exp $
#
# Copyright (c) 2007-2009, Juniper Networks, Inc.
# All rights reserved.
#

# the name assigned to the desired features
INPUT_FEATURES += ${PROVIDER_PREFIX}-cc-routeservice

# the definitions for each feature
${PROVIDER_PREFIX}-cc-routeservice_INPUT_FILES = \
	jnx-cc-routeservice.odl

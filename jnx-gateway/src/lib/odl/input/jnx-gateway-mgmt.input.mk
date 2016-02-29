#
# $Id: jnx-gateway-mgmt.input.mk,v 1.1.304.1 2009-08-13 05:06:17 kdickman Exp $
#
# Copyright (c) 2006-2009, Juniper Networks, Inc.
# All rights reserved.
#

# the name assigned to the desired features
INPUT_FEATURES += ${PROVIDER_PREFIX}-gateway-mgmt

# the definitions for each feature
${PROVIDER_PREFIX}-gateway-mgmt_INPUT_FILES = \
jnx-gateway-mgmt.odl

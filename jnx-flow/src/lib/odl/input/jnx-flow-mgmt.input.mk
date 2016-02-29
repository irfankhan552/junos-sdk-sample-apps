#
# $Id: jnx-flow-mgmt.input.mk,v 1.1.280.1 2009-08-13 05:06:17 kdickman Exp $
#
# Copyright (c) 2006-2009, Juniper Networks, Inc.
# All rights reserved.
#

# the name assigned to the desired features
INPUT_FEATURES += ${PROVIDER_PREFIX}-flow-mgmt

# the definitions for each feature
${PROVIDER_PREFIX}-flow-mgmt_INPUT_FILES = \
jnx-flow-mgmt.odl

#
# $Id: jnx-example.input.mk,v 1.3.304.1 2009-08-13 05:06:17 kdickman Exp $
#
# Copyright (c) 2006-2009, Juniper Networks, Inc.
# All rights reserved.
#

# the name assigned to the desired features
INPUT_FEATURES += ${PROVIDER_PREFIX}-example

# the definitions for each feature
${PROVIDER_PREFIX}-example_INPUT_FILES = \
jnx-example.odl

#
# $Id$
#
# Copyright (c) 2010, Juniper Networks, Inc.
# All rights reserved.
#

# File containing daemon table(s) and require table(s)
DAEMON_XML += route-manager.xml

# The name of the feature that the DDL library will be created for
INPUT_FEATURES += ${PROVIDER_PREFIX}-route-manager

# for each feature, enumerate the files that comprise the definitions for
# configuration and command
${PROVIDER_PREFIX}-route-manager_INPUT_FILES += \
	route-manager.cnf.dd


#
# $Id: jnx-example.input.mk,v 1.3.256.1.4.1 2009-08-13 05:06:09 kdickman Exp $
#
# Copyright (c) 2006-2009, Juniper Networks, Inc.
# All rights reserved.
#

# File used to define the daemon to mgd for system startup and control
DAEMON_XML += jnx-example.xml

# The name of the feature that the ddl library will be created for
INPUT_FEATURES += ${PROVIDER_PREFIX}-example

# for each feature, enumerate the files that comprise the definitions
# for configuration and control
${PROVIDER_PREFIX}-example_INPUT_FILES += \
    jnx-example.cnf.dd \
    jnx-example.cmd.dd

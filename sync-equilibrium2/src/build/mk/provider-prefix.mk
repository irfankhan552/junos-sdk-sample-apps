#
# $Id: provider-prefix.mk,v 1.3.128.2 2009-08-13 05:06:06 kdickman Exp $
#
# Copyright (c) 2009, Juniper Networks, Inc.
# All rights reserved.
#

.ifndef __provider_prefix_mk__
__provider_prefix_mk__ = 1

PROVIDER_PREFIX != ${EXTRACT_PROV_PREFIX} ${SIGCERT}

.endif


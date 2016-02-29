#
# $Id: Makefile.inc 346460 2009-11-14 05:06:47Z ssiano $
#
# Copyright (c) 2006, Juniper Networks, Inc.
# All rights reserved.
#

# protect from multiple inclusion
.if !defined(_sdk_example_ddl_inc_)
_sdk_example_ddl_inc_=1
.include <bsd.init.mk>

# include the Juniper ddl include
.include "${SDK_DDL_INC}/Makefile.inc"

.endif

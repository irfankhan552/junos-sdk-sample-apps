#
# $Id: Makefile.inc 346460 2009-11-14 05:06:47Z ssiano $
#
# Copyright (c) 2006, Juniper Networks, Inc.
# All rights reserved.
#

# protect from multiple inclusions
.if !defined(_sdk_example_odl_inc_)
_sdk_example_odl_inc_=1
.include <bsd.init.mk>

# odl relies on some definitions in ddl so pick up the file that
# provides them
.include "../ddl/Makefile.inc"

.include "${SDK_ODL_INC}/Makefile.inc"

.endif

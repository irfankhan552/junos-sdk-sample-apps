#
# $Id: makefile,v 1.1 2006-08-30 22:32:28 nuke Exp $
#
# Copyright (c) 2001, 2006, Juniper Networks, Inc.
# All rights reserved.
#
# makefile for hosttools
#
# It just so happens that make(1) favours makefile over Makefile
# which provides us the opportunity to do some environment frobbing
# before invoking make again with the real Makefile.
# This is a simple way of making hosttools use a separate obj tree
# from that used for the target-host stuff.
# 

# this include will bring in the right environment and invoke make
# with Makefile
.include <jnx.hosttool.mk>

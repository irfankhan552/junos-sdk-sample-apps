#
# $Id: Makefile 346460 2009-11-14 05:06:47Z ssiano $
#
# Copyright (c) 2006, 2008, Juniper Networks, Inc.
# All rights reserved.
#

.include "${SYS_MK_DIR}/pkg/jnx.setup.mk"

.PATH: ${HOST_OBJTOP_BSD}/etc/certs
all: certs.pem
certs.pem: ${SIGCERT} sdk-certs.pem
	echo SIGCERT is ${SIGCERT}
	cat ${.ALLSRC:M*.pem} > $@

clean:
	rm -f certs.pem

cleandepend:


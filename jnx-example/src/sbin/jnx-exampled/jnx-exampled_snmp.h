/*
 * $Id: jnx-exampled_snmp.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * Copyright (c) 2006, Juniper Networks, Inc.
 * All rights reserved.
 *
 */

#if (!defined(_JNX_EXAMPLED_SNMP_H_INC))
#define _JNX_EXAMPLED_SNMP_H_INC

void
netsnmp_subagent_init (evContext ctx);

/* function declarations */
void
init_jnxExampleMIB (void);
void
init_jnxExampleNetSnmpVersion (void);

/* column number definitions for table jnxExampleDataTable */
#define COLUMN_JNX_EXAMPLE_DATANAME		1
#define COLUMN_JNX_EXAMPLE_DATADESCRIPTION	2
#define COLUMN_JNX_EXAMPLE_DATATYPE		3
#define COLUMN_JNX_EXAMPLE_DATAVALUE		4

#endif /* _JNX_EXAMPLED_SNMP_H_INC */

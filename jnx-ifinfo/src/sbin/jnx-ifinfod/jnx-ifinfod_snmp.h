/*$Id: jnx-ifinfod_snmp.h 346460 2009-11-14 05:06:47Z ssiano $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2006-2008, Juniper Networks, Inc.
 * All rights reserved.
 * jnx-ififnod_snmp.h
 */

#if (!defined(_JNX_IFINFOD_SNMP_H_INC))
#define _JNX_IFINFOD_SNMP_H_INC

void netsnmp_subagent_init (evContext ctx);

/* function declarations */
void init_jnxIfinfoMIB (void);
void init_jnxIfinfoNetSnmpVersion (void);
void init_jnxIfinfoInterfacesTable(void);

/* column number definitions for table jnxIfinfoTable */
#define COLUMN_JNX_IFINFO_INDEX   		1
#define COLUMN_JNX_IFINFO_ALIAS          	2
#define COLUMN_JNX_IFINFO_NAME           	3

/* column number definitions for table jnxIfinfoInterfaceTable */
#define COLUMN_JNX_IFINFO_IFD_SNMP_ID        	1
#define COLUMN_JNX_IFINFO_INTERFACE      	2

#endif /* _JNX_IFINFOD_SNMP_H_INC */

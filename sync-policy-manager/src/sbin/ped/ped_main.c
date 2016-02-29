/*
 * $Id: ped_main.c 366969 2010-03-09 15:30:13Z taoliu $
 *
 * This code is provided as is by Juniper Networks SDK Developer Support.
 * It is provided with no warranties or guarantees, and Juniper Networks
 * will not provide support or maintenance of this code in any fashion.
 * The code is provided only to help a developer better understand how
 * the SDK can be used.
 *
 * Copyright (c) 2007-2008, Juniper Networks, Inc.
 * All rights reserved.
 */
 
/** 
 * @file ped_main.c
 * @brief Contains main entry point
 * 
 * Contains the main entry point and registers the application as a JunOS daemon 
 */ 

/* The Application and This Daemon's Documentation: */

/** 

\mainpage

\section intro_sec Introduction

The Policy Manager sample application composed of two daemons on the routing 
engine (RE), the Policy Server Daemon (PSD) and the Policy Enforcement Daemon 
(PED). Both daemons will be added to the sync-policy-manager-mgmt package, which
for the fictitious company SDK Your Net Corp. (SYNC), demonstrates how to use 
the correct naming conventions in developing a package to hold RE-SDK daemons. 
As of the 8.5 package release, the sample application also contains two daemons 
running on the MS-PIC or even separate PICs optionally. There is the Packet 
Filtering Daemon (PFD) running as a data application and a Captive Portal Daemon
(CPD), which is a simple HTTP server running as a control application. The whole
application containing all four daemons is contained in the 
sync-policy-manager-bundle package.

The goal of this application is to demonstrate the use of some JUNOS SDK APIs. 
It covers the use of DDL and ODL, respectively, to manage configuration and 
commands, and to control the output of operational commands. It uses event 
control from the eventlib API in libisc2 (a library provided by the Internet 
Software Consortium) to exemplify its use with sockets to provide asynchronous 
communication services. It demonstrates the use of libjunos-sdk in several ways:
Kernel Communication (KCOM) is used to listen for protocol family changes on 
interfaces, and tracing and logging happens using the APIs exposed in the 
junos_trace module. This application also demonstrates the use of libjipc for 
inter-process communication to and from the PSD. Libssd, a major SDK library 
that communicates with the SDK Service Daemon (SSD), is used to manage routes 
associated with policies and install service routes to MS-PICs. Lastly, the 
application demonstrates writing control and data applications for the Services
SDK using libconn and libmp-sdk, performing PIC-PIC and RE-PIC communications.

\section PED_sec Policy Enforcement Daemon (PED)

The PED, a client of the PSD, starts off by requesting policies for all 
interfaces under its management, and then registers with the PSD to receive 
notifications when the PSD's policies change. The PED also sends regular 
TCP-based echo requests to the PSD called heartbeat messages. These are simply 
echoed back to the PED, and both sides know the connection is still good. All 
communication between the PSD and the PED is asynchronous.

The PED will be responsible for enforcing policies on inet interfaces. It must 
be told which interfaces (IFFs) to attempt to manage (request policies for). The
IFFs, consisting of the family and the IFL-structured interface names, which 
match the PED's pattern matching expressions, will be attempted. Specifically, 
the PED is configured with one or more conditions, which combine an address 
family with a pattern matching expression in each one.

Lastly, the PED also acts as the management application in the three-piece 
Services-SDK application (PED, CPD, and PFD). As such, its role is to pass on 
configuration to the CPD (the control piece) and the PFD (the data piece). These
daemons running on the MS-PIC have no other access to the configuration present 
on the RE.

\subsection init_sec Initialization

The PED starts the connection to the PSD and the SSD, registers KCOM message 
handlers, and initializes its scheduler to send and check heartbeats. Most of 
the other action takes place after reading the configuration. Initialization 
relating to the PFD and CPD happens after the PED has received connections from
both of them; however, the server listening for these connections is started 
during the PED initialization.

\subsection conf_sec Configuration

The PED configuration is a set of conditions to decide for which interfaces it 
will send policy requests. A condition consists of a condition name, interface 
name pattern and address family. When the configuration is changed and read, 
the PED will request all policies from the PSD.

The PED configuration also contains an address for the PFD and the CPD. The PED 
waits for connections from both the CPD and the PFD. Once both have connected, 
it knows the interface name for each. For example, for the PFD an MS-PIC 
interface name would be something such as ms-x/y/0, where x and y correspond to 
the FPC- and PIC-slot numbers for the location of the PIC. The PED learns that 
IFLs 100 and 101 are reserved and configured for the PFD through its 
configuration. It then creates a virtual-router routing instance named 
pfd_forwarding with ms-x/y/0.100 installed into it. Accordingly, ms-x/y/0.101 
stays in the master routing instance.

Using the logical unit numbers 100 and 101, the PED creates next-hop IDs (with 
libssd) for both data interfaces. Following that, it installs a service route 
for ms-x/y/0.100 with the SSD to direct all packets through pfd_forwarding to 
this one of the PFD's interfaces. For the PFD's other interface remaining in the
master routing instance, the PED creates a service route for all traffic 
matching only the PFD address.

The routing instance created contains the following configuration:

\verbatim
pfd_forwarding {
        instance-type virtual-router;
        interface ms-x/y/0.100;
}
\endverbatim

Lastly, the PED creates an inet firewall filter named pfd_filter with the 
following content:

\verbatim
filter pfd_filter {
        term any {
                then routing-instance pfd_forwarding;
        }
}
\endverbatim

This filter directs all traffic into the pfd_forwarding routing instance. It is 
applied on all interfaces under the PED's management as described in the 
following sections.

The filter and routing instance above are only created if they do not yet exist 
of course, which should only be the first time the PED starts.

If in the PED's configuration, the PFD's or CPD's address changes, then only the
service routes, the CPD, and the PFD are re-configured.

\subsection hb_sec  Scheduler and Heartbeat

The PED scheduler checks heartbeat replies as received, and also sends heartbeat
messages every 10 seconds. If the PED didn't get a heartbeat reply, it assumes
either the connection to the PSD or the PSD itself is down. In such a case the
PED will close the current connection and try to setup a new connection to the
PSD. Assuming a connection to the SSD is established, when the PED gets the
heartbeat for the first time, it will compare the patterns in the conditions to
all the actual interface names from KCOM. It then queries the PSD for all 
IFL-formatted interface names that match a condition by sending a policy request 
to the PSD for each matching interface. Note the address family must match as 
well and is also sent to the PSD.

\subsection kcom_msg_sec KCOM Message Handler

The PED registers KCOM message handlers in initialization. It also uses KCOM 
functions to query interface information when going through all interfaces.

When the PED starts or gets a MSG_POLICY_UPDATE message from the PSD, it queries
all interfaces by KCOM APIs and updates policies.

When an interface is added, deleted, or changed for KCOM purposes (enabled or 
disabled), the KCOM message handler will update the policy for that interface. 
This is described below.

\subsection msg_proc_sec The PSD Message Processing

When connecting to the PSD, the PED sets up a message handler to process the 
asynchronous messages from the PSD. The messages it receives are as follows:

\li MSG_HB <br>
When receiving the MSG_HB message the PED sets a flag indicating the connection 
to the PSD is still alive. This message contains no data.

\li MSG_FILTER <br>
In the case of receiving a MSG_FILTER reply message from the PSD, the message 
contains not only the interface name and address family (from the request), but 
primarily the names of an input and output filter. The PED will invoke an 
operation (op) script to apply the filter to the interface in the candidate 
configuration and then commit the change. Because this is a synchronized 
operation, the PED waits until the op script completes and returns. The filter 
is not added into the policy table if running op script returns an error.

\li MSG_ROUTE <br>
In the case of receiving a MSG_ROUTE reply message from the PSD, the message 
contains not only the interface name and address family (from the request), but 
primarily a route's data. The PED may receive more than one of these messages 
with the same interface name and address family, and it will thus add the route 
to the policy for that interface. Each route goes into the policy table in the 
PED, and is requested for addition into the routing table through the SSD*.

\li MSG_POLICY_NA <br>
The PSD doesn't have a policy for this interface and address family, so the PED 
removes the associated policy from the policy table if one exists. In so doing,
it removes filters from the interface's configuration (again, not implemented) 
and routes through the SSD. The interface (IFF) is no longer said to be managed.

\li MSG_POLICY_UPDATE <br>
The configuration in the PSD was updated, or the PSD was restarted. Upon 
receiving this message from the PSD, the PED potentially needs to update all 
interfaces. It does this using the same process it goes through upon start up, 
but of course a policy table already exists potentially, so there may be managed
interfaces. For all managed interfaces we may receive different routes and 
filters than what is currently applied (and recorded in the policy table); thus,
after going through all interfaces (using KCOM) we send the PSD a 
MSG_UPDATE_DONE message (see below), and the PSD echoes it back. Upon receiving 
it, all MSG_ROUTE and MSG_FILTER messages must be received by the PED for all 
requests made during the update. The PED then scans (cleans) the policy table. 
It looks for route or filter entries that it had managed before receiving the 
MSG_POLICY_UPDATE message, but that the PSD did not report on. This would 
indicate that the route or filter is no longer part of the policy, so the PED 
removes it. For a route it deletes it with the SSD, and for a filter, again, 
this is not implemented. If a policy is left empty after this process the 
interface is taken out of the policy table, and is no longer said to be managed.

\li MSG_UPDATE_DONE <br>
The PED sends this message to the PSD and gets it echoed back. Upon receiving 
it, the PED assumes all MSG_ROUTE and MSG_FILTER messages have been received 
as part of the update process started by receiving a MSG_POLICY_UPDATE message. 
The PED will start to clean the policy table as described above.

* When a MSG_FILTER or MSG_ROUTE is received a filter or route will only be 
added to the policy table for that managed interface if the associated policy is
found to be okay. This may not be the case if a policy is marked as broken. This
would happen if, for example, a previous MSG_ROUTE was received and the 
route-add request through the SSD came back (asynchronously) as failed. Upon 
receiving the failed response from the SSD, the policy would be marked as 
broken. Broken policies get removed from the table along with their contents 
during the clean process described above. This mechanism ensures that the 
application of a policy is all-or-nothing style. Note that the clean process 
also initially waits for all responses from the SSD regarding route-add 
requests.

\subsection update_sec Updating an Interface's Policy

To update an interface's policy, the PED compares the interface name received 
from KCOM (from an asynchronous notification) with the patterns in the PED's 
configured conditions. The address family must also match the address family 
specified in the same condition that is used for the interface pattern match 
(the "any" keyword can be configured in a condition to match any address family,
although currently we support only inet in many places throughout this 
application). The PED will send a policy request to the PSD only if a match is 
found; otherwise, the PED removes this interface and the associated policy from
the policy table by acting similarly to if it had received a MSG_POLICY_NA 
regarding this interface.

When a policy is requested for an interface and successfully applied, regardless
of the filters included in the policy, the PED attaches pfd_filter as the input 
filter of every managed interface. If ever an interface becomes unmanaged (the 
policy is removed), then this input filter is also removed.

\subsection table_sec Policy Table

The policy table stores the managed interface policies indexed by interface name
and address family. Each entry may include an interface name, an address family,
an input filter, an output filter, and a list of routes. At least one filter or
route must exist in a policy, and an interface name and an address family must 
always exist.

\subsection snmp_sec SNMP

The PED includes a SNMP sub-agency to support SNMP queries and traps.
The RE-SDK SNMP library doesn't support the SNMP set operation.

The PED SNMP MIB contains:
  Total number of interfaces
  Number of managed interfaces
  Interface table
    Interface name
    Interface address family
    Number of routes
    Input filter name
    Output filter name
  PSD connection up time
  SNMP version

Traps:
  PED-PSD connection up/down

For further details see ped.mib

\subsection pers_conf_mani Persistent Configuration Manipulation

Modifying the interface, filter, and routing instance configuration is achieved 
through operation (op) scripts.

For every interface configuration modification, the PED acquires an exclusive 
lock on the configuration, performs a load-configuration command, commits the 
configuration and releases the configuration lock. 

If the candidate configuration was changed but not committed before the PED 
invokes an op script to apply a filter, the op script will not change the 
candidate configuration and returns an error. However, the filters added by the 
previous op script operations will stay in the configuration.

\subsection com_w_cpdpfd Communication with PFD and CPD

The PED originally must act as the intermediary between the PFD and the CPD. 
When it discovers the connections for both the PFD and the CPD, it sends the 
CPD's internal server interface information to the PFD. The PFD can then connect
directly to the CPD and receive CPD information through that channel.

Lastly, the PED sends both the PFD and the CPD, the configured PFD and CPD 
addresses. If ever one of these configured addresses change, the PED re-sends 
these addresses over the same channel to both the PFD and CPD.


\section prereq_conf PFD and CPD Installation and Prerequisite Configuration

To install the PFD and the CPD some configuration is required on the router. 
Ideally, this should be performed before configuring the PED (which enables it).

If installation of both applications on one MS-PIC (ms-x/y/0) is desired, then 
the following configuration is required:

\verbatim
chassis {
   fpc x {
       pic y {
           adaptive-services {
               service-package extension-providers {
                    control-cores 2;
                    data-cores 6;
                    package sync-policy-manager-ctrl;
                    package sync-policy-manager-data;
               }
           }
       }
   }
}
\endverbatim

If installation of the applications is desired on two separate MS-PICs (ms-w/z/0
and ms-x/y/0), then the following configuration is required:

\verbatim
chassis {
   fpc x {
       pic y {
           adaptive-services {
               service-package extension-providers {
                    data-cores 7;
                    control-cores 1;
                    package sync-policy-manager-data;     
               }
           }
       }
   }

   fpc w {
       pic z {
           adaptive-services {
               service-package extension-providers {
                    control-cores 8;
                    package sync-policy-manager-ctrl;     
               }
           }
       }
   }
}
\endverbatim

Herein we assume the notation from the second situation, where the applications 
are installed on two MS-PICs. The CPD and the PFD are installed on ms-w/z/0 and 
ms-x/y/0 respectively.

Moreover, the configuration of the interfaces must be performed. We assume 
interface logical units of 100 and 101 for the PFD and 102 for the CPD.

\verbatim
interfaces {
    ms-x/y/0 {
       unit 100 {
          family inet;
       }
       unit 101 {
          family inet;
       }
    }

    ms-w/z/0 {
       unit 102 {
          family inet;
             address 3.3.3.3/32;
       }
    }
}
\endverbatim

Note that we do not configure an address for the data interfaces of the PFD.

Lastly, the op scripts packaged with the PED must be allowed to run on the 
router. To achieve this configure the following op script files as demonstrated
below.

\verbatim
system {
    scripts {
        op {
            file ped_init_filter.xsl;
            file ped_update_interface_filter.xsl;
        }
    }
}
\endverbatim


\section example_sec Example

Typical sequence of events upon the PED receiving an interface addition 
notification:

    -# Interface fe-0/0/1.0 is configured with inet (IPv4)
    -# The PED receives notification of a new IFF interface
        -# If the interface name and address family does not match any of the 
        PED's conditions, then any applied routes and filters on this interface 
        will be removed, and the table of managed interfaces will be updated. 
        Then it is done.
        -# Otherwise, it is an interface that the PED will enforce a policy on. 
        It proceeds to step 3.
    -# The PED sends a MSG_POLICY_REQ message to the PSD containing the name of 
    the interface (fe-0/0/1.0) and the address family (inet) for which it wants 
    a policy.
        -# If the PSD finds no match for this interface name in any of its 
        policies, it sends a MSG_POLICY_NA message and proceeds to step 4.
        -# Otherwise, the PSD has a policy for this interface. Using the first 
        matching policy:
            -#   If there are filters in the policy, it sends a MSG_FILTER 
            message containing the name of the filter to attach the input side 
            of the interface and the name of the filter to attach to the output 
            side of the interface (or at least one of such names if both do not 
            exist).
            -#  If there are routes in the policy, it sends the routes one by 
            one in MSG_ROUTE messages.
           It then proceeds to step 5.
    -# The PED receives the MSG_POLICY_NA message, and any applied filters and 
    routes on this interface will be removed. The table of managed interfaces 
    will be updated (policy is entirely removed from table). Then it is done.
    -# The PED adds the filters and routes received in any MSG_FILTER or 
    MSG_ROUTE messages to the table of managed interfaces.
        -# With MSG_FILTER messages, the PED associates the interface name and 
        address family with the filters in its table of managed interfaces as 
        long as the op script invoked to apply the filter is successful. If it 
        is not successful, then the policy is marked as broken  (for deletion 
        upon a table cleanup).
        -# With MSG_ROUTE messages, the PED associates the interface name and 
        address family with the route in its table of managed interfaces as 
        long as the SSD successfully adds the route. The route stays in a 
        pending state until the response from the SSD is received, whereupon the
        route add was successful or was not, in which case the policy is marked 
        as broken (for deletion upon a table cleanup).
        -# If the PED has discovered both the PFD and the CPD, it attaches 
        pfd_filter for this interface if it received at least one MSG_FILTER or
        MSG_ROUTE. All traffic coming in the interface is now sent through the
        PFD.
    -# The PED is done with handling this event.

In the example above when the PED applies or removes filters to an interface, it
would do so using op scripts. These configuration-style operations of course 
need to be committed, and the result of that commit will determine whether or 
not the tuple--interface name, protocol, and policy--gets added or updated in 
the table of managed interfaces. When a filter-add modification fails, the 
associated policy is marked as broken.

Also in the example above when the PED applies or removes routes to an interface
it does so using libssd which communicates with the SSD. The result of adding or
removing routes (when attempted) will determine whether or not the tuple--
interface name, protocol, and policy--gets added or updated in the table of 
managed interfaces. When a route-add request fails the associated policy is 
marked as broken.

In the event that filters applied or removed by the PED conflict with manual 
configurations performed by a user, the last configuration will always take 
precedence.

The sequence of events at start-up (after loading the configuration) is very 
similar to the one above in the example. The only difference is that the PED 
walks through all of the interfaces (IFFs) currently present instead of simply 
listening for them as in step 1. It also goes through them all, upon receiving 
notification (MSG_POLICY_UPDATE) from the PSD that its configuration has 
changed.

Also upon the start-up of the PED, it starts a scheduler to schedule regular 
heartbeat messages.  These messages just maintain that the connection to the PSD
is good and vice-versa. The scheduler times are all changeable, but work as 
follows by default. After the configuration has been loaded the scheduler clears
any existing schedule, and starts a new schedule. Of course, there would be no 
existing schedule if the daemon is just starting. The new schedule involves the 
PED creating an event in 2 seconds and every 10 seconds thereafter. If ever the 
event fails, it informs the scheduler to reschedule (try again) in 2 more 
seconds. Of course if something is really wrong, that will not be fixed by 
retrying; hence, this would just keep repeating. For this reason, before the 
scheduler dispatches the event job, it checks to see if it has processed more 
than 3 events within 10 seconds. If so, it will not process the current event. 
Instead, it will reschedule itself to reset after 2 minutes. Upon the 2 minutes 
elapsing, it resumes scheduling as normal and schedules an event again in 2 
seconds and every 10 seconds thereafter.

In the case of an interface going down because it was deleted, the PED simply 
removes its record about this interface and policy from its table of managed 
interfaces. Since it is gone, so are the filters that have been configured via 
op scripts. The pfd_filter is also gone. We attempt to remove the associated 
routes using libssd.


\section psd_ped_sec Message content between the PSD and the PED

The PED and the PSD use libjipc over TCP sockets to exchange these messages.

\li MSG_HB <br>
The heartbeat message originates from the PED to check the state of connection 
to the PSD. There is no data included in this message. The PSD echoes this 
message back after receiving it.

\li MSG_POLICY_REQ <br>
The policy request message is sent by the PED to request a policy. The data 
included consists of an IFL-formatted interface name and an address family.

\li MSG_FILTER <br>
The filter message is sent by the PSD to reply to the request from the PED. The 
data included consists of an IFL-formatted interface name and an address family, 
which are from the original request, and the names of one input and one output 
filter.

\li MSG_ROUTE <br>
The route message is sent by the PSD to reply to the request from the PED. The 
data included consists of an IFL-formatted interface name and an address family, 
which are from the original request, and route data. The route data consists of 
a route address and prefix, the next-hop address and type, and the route metric 
and preference.

\li MSG_POLICY_NA <br>
The policy-not-available message is sent by the PSD to reply to the request from 
the PED. This message is used when no policy is configured on the server for the 
requested interface name and address family. No data is included in this message 
other than an IFL-formatted interface name and an address family, which are both 
from the original request.

\li MSG_POLICY_UPDATE <br>
The update message is sent by the PSD to notify all connected clients (the PED) 
that the server configuration was updated. From the clients' points of view, 
they may want to update their policies or check for changes if they want to 
remain synchronized with the policy server. This message contains no data.

\li MSG_UPDATE_DONE <br>
The update-done message originates from the PED. It marks the end of a set of 
policy requests. The PSD echoes this message back after receiving it, and no 
data is included in the message. Because communication is asynchronous, this 
helps the PED know that no more responses to policy requests are coming when it 
receives the echo back, since the PSD serves received messages in FIFO order. In 
this case the PED sends this message last and it should get the echo response 
back last as well.


\section figures_sec Figures

\subsection f1_sec Internal Communication Channels between the Daemons (and SSD)

\verbatim

                 +---------+
                 |         |
                 |   PSD   |
                 |         |
                 +----x----+
                      |
                      | IRI2 (libjipc)
                      | 
                      |            ,--> (libssd)
                 +----x----+      /   +---------+
                 |         |   IRI2   |         |
                 |   PED   x----------x   SSD   |
                 |         x\         |         |
                 +----x----+ \        +---------+
                      |       `-------.
                 IRI2 |    all         \
                      |   (libconn)     \ IRI2
                      |                  \
                 +----x----+          +---x-----+
                 |         |   IRI2   |         |
                 |   CPD   x----------x   PFD   |
                 |         |          |         |
                 +-------x-+          +---------+

\endverbatim

\subsection f2_sec An Authorized Subscriber's Traffic Flow

\verbatim

auth'd: ,,,,,,,,,,,,,,                               ,,,,,,,,,,,,,,,,,,,,, 
 O      ..........    ,                             ,    .................
/X\              :     ,                            ,    :                
/ \          #===================#              #=======================#
             | managed interface |              |   outbound interface  |
             #===================#              #=======================#
                 :       ;,,,,,,,,,,,,,,,,,,,      ,  .                 
                 :                           ,    ,  .                   
            ___________________________     _____________________________ 
           /     :                     \   /   ,,  .                     \
           |   ped_forwarding.inet.0   |   |      .   inet.0             |
           \___________________________/   \_____________________________/
                 : |                           /.          |              
                 : |                          /.           |              
                 : |                         /.            |              
            +------x---------+              /.   +---------x---------+    
            |     100        |             /.    |        102        |    
            |            101 x------------'.     |                   |    
            |     PFD        |.............      |       CPD         |    
            |   (NAT/PAT)    |                   |   (HTTP Server)   |    
            |                |                   |                   |    
            +----------------+                   +-------------------+    


Legend:

...... = From user to some non-CPD destination (filtered via PFD)
,,,,,, = From destination to user (directed as normal)

\endverbatim

\subsection f3_sec An Unauthorized Subscriber's Traffic Flow

\verbatim

                                                     +-------------+
            ###############                          |   unauth'd  |
unauth'd:   ************* #                          |   original  |
user    ,,,,,,,,,,,,,,  * #                          | destination |
 O      ..........   ,  * #                          +------x------+
/X\              :   ,  * #                                 |
/ \          #===================#              #=======================#   
             | managed interface |              |   outbound interface  |   
             #===================#              #=======================#   
                 :   ,  *  #                                          
                 :   ,  *  ######################################     
                 :   ,  *                                       #
                 :   ,,,,,,,,,,,,,,,,,,,,,,,,,,,,               #
            ___________________________     _____________________________   
           /     :      *              \   /  ,                          \  
           |   ped_forwarding.inet.0   |   | , " ' *  inet.0    #        |  
           \___________________________/   \,____________________________/  
                 : |    *                  , " /' *     * '|"   #           
                 : |    *                 , " /' *      * '|"   #          
                 : |    *                , " /' *       * '|"   #           
            +------x---------+,,,,,,,,,,, " /' * +---------x---------+      
            |     100        |"""""""""""" /' *  |        102        |      
            |            101 x------------/' *   |                   |      
            |     PFD        |''''''''''''' *    |       CPD         |      
            |   (NAT/PAT)    |**************     |   (HTTP Server)   |      
            |                |                   |                   |      
            +----------------+                   +-------------------+   


Legend:

...... = From user to destination (redirected at PFD to CPD into '''')
'''''' = From PFD to CPD (NAT'd traffic from user from ....)
"""""" = From CPD to PFD (CPD sends an HTTP redirect, goes into ,,,,)
,,,,,, = From destination to user (re-sourced at PFD from """")
****** = From user to CPD (after CPD sent the PFD nat'd req a HTTP redirect)
###### = From CPD to user (HTTP response reaches the user directly as normal)

\endverbatim


\section sample_conf_sec Sample Configuration

The sample configuration below shows an example of how to configure the daemon. 
The configuration for the PED is under "sync policy-enforcement." For example, 
the condition "a-cond" flags all Fast Ethernet (names start with "fe") 
interfaces of the inet family (i.e. IPv4) as interesting and under its 
management.

\verbatim
sync {
    policy-enforcement {
        captive-portal-address 3.3.3.3; // must match IFL 102's address
        packet-filter-address 4.4.4.4;
        packet-filter-interface ms-x/y/0.100;
        packet-filter-nat-interface ms-x/y/0.101;

        condition a-cond {  
            family inet;
            interface-name fe*;
        }
        condition b-cond {
            family inet6;
            interface-name fe-0*;
        }
        condition c-cond {
            family any;
            interface-name ge*;
        }
        traceoptions {
            file ped.trace;
            flag all;
        }
    }
}
\endverbatim

*/


#include <sync/common.h>
#include "ped_config.h"
#include "ped_conn.h"
#include "ped_daemon_name.h"
#include "ped_kcom.h"
#include "ped_schedule.h"
#include "ped_policy_table.h"
#include "ped_ssd.h"
#include "ped_snmp.h"
#include "ped_service_route.h"
#include "ped_filter.h"

#include PE_SEQUENCE_H

/*** Constants ***/


/*** Data Structures ***/

/**
 * CMD Mode menu tree of commands:
 * (This is defined at the end of ped_ui.c)
 */
extern const parse_menu_t master_menu[];

/**
 * Path to PED config
 */
extern const char *ped_top_config[];

evContext ped_ctx;  ///< Event context for ped (global)

/*** STATIC/INTERNAL Functions ***/

/**
 * Callback for the first initialization of the RE-SDK Application
 * 
 * @param[in] ctx
 *     Newly created event context 
 * 
 * @return
 *      SUCCESS if successful, or EFAIL if failed.
 */
static int
ped_init(evContext ctx)
{
    ped_ctx = ctx;
    init_config(); // init configuration storage
    init_table();  // init table of managed interfaces
    ped_ssd_init(); // connect to SSD and init our SSD module
    init_dfw(ctx);
    service_route_init(ctx); // connect to SSD and init our service route module

    if(ped_kcom_init()) { // init KCOM
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Initialize kcom FAILED!", __func__);
    } else if(ped_schedule_init()) {  // init SCHEDULER
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Initialize scheduler FAILED!", __func__);
    } else if(init_server(ctx)) {  // init peer connection (CPD/PFD) server    
        ERRMSG(PED, TRACE_LOG_ERR,
                "%s: Initialize peer connection server FAILED!", __func__);        
    } else {
        netsnmp_subagent_init();
        ped_schedule_reset(); // start SCHEDULER
        return SUCCESS;
    }
    return EFAIL;
}

/*** GLOBAL/EXTERNAL Functions ***/

/**
 * Intializes ped's environment
 * 
 * @param[in] argc
 *     Number of command line arguments
 * 
 * @param[in] argv
 *     String array of command line arguments
 * 
 * @return 0 upon successful exit of the application or non-zero upon failure
 */
int
main (int argc, char **argv)
{
    int ret = 0;
    junos_sdk_app_ctx_t ctx;

    /* Create an application context */
    ctx = junos_create_app_ctx(argc, argv, DNAME_PED,
                               master_menu, PACKAGE_NAME, SEQUENCE_NUMBER);
    if (ctx == NULL) {
        return -1;
    }

    /* Set config read call back */
    if ((ret = junos_set_app_cb_config_read(ctx, ped_config_read))) {
        goto cleanup;
    }

    /* Set init call back */
    if ((ret = junos_set_app_cb_init(ctx, ped_init))) {
        goto cleanup;
    }
    
    /* set trace options DDL path */
    if ((ret = junos_set_app_cfg_trace_path(ctx, ped_top_config)) < 0) {
        goto cleanup;
    }

    /* Calling junos_app_init */
    ret = junos_app_init(ctx);

cleanup:
    /* Destroying context if daemon init/exit failed */
    junos_destroy_app_ctx(ctx);
    return ret;

}

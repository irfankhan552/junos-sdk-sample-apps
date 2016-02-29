# junos-sdk-samples-apps
Junos SDK open source sample apps also available from the Juniper Developer Network. Require the proprietary (must be Juniper partner) backing sandbox and toolchain to build however.

_I cannot take credit for all of this code, but I did write most of it as the lead developer evangelist and TME for the Junos SDK circa 2006-2010. Most notably I wrote helloworld, hellopics, monitube, equilibrium, and many others._


**Title: Route Manipulation Basics (C++ edition)**

Formal name in UI: jnx-cc-routeservice  

Description:  This sample
application is one simple daemon that reads in custom route provisioning rules
configured through its UI extensions. In turn, it uses libssd APIs to add,
delete, and manipulate routes programmatically. It’s a simple baseline showing
developers how they can manipulate routing, though more realistic use cases
would change routes more dynamically rather than based on simple provisioning
rules. This is the C++ code edition of this sample app. There is also a C code
edition.  

Control plane programs: 1  

Service plane programs: 0  
  

**Title: Basic Example Junos App**

Formal name in UI: jnx-example  

Description:  This is the
traditional jnx-example sample app. It provides an uncomplicated code example,
showing developers how to do rudimentary UI extension, SNMP extension, and
getting network interface information and notification. It doesn’t do anything
too useful, to keep it very simple. It reads in the configuration that it makes
possible through configuration UI extensions, and adds new operational commands
to simply show what it loaded, but in different views (list and table
formatting on the CLI).  

Control plane programs: 1  

Service plane programs: 0  
  

**Title: Flow Monitoring and Statistics**

Formal name in UI: jnx-flow  

Description:  This sample
application will remind you of JFlow in case you’re familiar with it. It deals
with monitored traffic, and it aggregates flow statistics for the flows that it
sees, allowing them to be exported to an external server. This provides a
simple baseline of code for developers looking to start implementing an
application with flow monitoring.  

Control plane programs: 1  

Service plane programs: 1  
  

**Title: IP-IP to GRE Gateway**  

Formal name in UI: jnx-gateway  

Description:  This sample
application performs tunneling translation from IP-IP encapsulation to GRE
encapsulation. It shows the basics of dealing with packet processing in the
service plane, focusing on most of the usual steps JBUFs, packet queuing,
packet receiving, packet sending, real-time threading, locks. It is especially
good sample code for developers seeking to do some encapsulation/decapsulation
work of their own in the service plane.  

Control plane programs: 1  

Service plane programs: 2

 

**Title: Interface Information Gathering**

Formal name in UI: jnx-ifinfo  

Description:  This sample app and
code shows the developer how to work with the Junos OS network interfaces. It
retrieves information from them, gets status change notifications, and exposes
this information in a different way through UI and SNMP extensions.  

Control plane programs: 1  

Service plane programs: 1 (optional)

 


 

**Title: Service Plane Server Dynamic Addressing**  

Formal name in UI: jnx-msprsm  

Description:  This sample application
uses libssd to programmatically add an IP addresses to an ms interface. As such
traffic can be sent and terminated at the ms interface using that address, just
as if there was a statically provisioned IP address for the ms interface. Such
addresses are used for the server style traffic termination and sending, not
the typically service plane’s inline style for processing. Using this
programmatic style of adding the addresses, addresses can be floated between ms
interfaces for various purposes like high availability.  

Control plane programs: 1  

Service plane programs: 0

 

**Title: Route Manipulation Basics (C edition)**  

Formal name in UI: jnx-routeservice  

Description:  This sample
application is one simple daemon that reads in custom route provisioning rules
configured through its UI extensions. In turn, it uses libssd APIs to add,
delete, and manipulate routes programmatically. It’s a simple baseline showing
developers how they can manipulate routing, though more realistic use cases
would change routes more dynamically rather than based on simple provisioning
rules. This is the C code edition of this sample app. There is also a C++ code
edition.  

Control plane programs: 1  

Service plane programs: 0

 

**Title: Helloworld (Junos SDK)**

Formal name in UI: sync-apps  

Description:  This sample
application code actually contains two programs. One is the simple helloworld
program. This program simply shows the minimal application with Junos UI
extensions, so that a message like “helloworld” can be configured, and another
operational mode command can be used to output the message. The second program
in this package, called counter, uses helloworld as a baseline, but adds a
counter to how many times the message has been output. It saves this counter in
HA-enabled memory (using KCOM’s GENCFG APIs). If the program restarts, it is
seen that counter resumes where it last left off. The same functionality is
supported upon Routing Engine switchover while GRES is enabled.  

Control plane programs: 2  

Service plane programs: 0

 

**Title: The Dynamic Policy Manager**

Formal name in UI: sync-dpm  

Description:  This sample
application runs a light HTTP server on a service engine and allows users to
login and logout. The credentials database can be configured. When logged out
the users have their traffic rate limited to a global default configured level.
When logged in their traffic is rate limited to the levels associated with
their user class. This is accomplished using libdfwd APIs to programmatically
manipulate stateless firewall filters on the user-facing interface. Firewall
filters and policers are setup at start-up or re-configuration and they
are  manipulated as users log in and out
using the basic web interface provided by the HTTP server.  

Control plane programs: 1  

Service plane programs: 1

**Title: Equilibrium traffic load balancing (standalone program edition)**

Formal name in UI: sync-equilibrium  

Description:  This sample
application consists of the development of a basic HTTP load-balancing system
designed to operate quickly in the network data path in the services plane. The
system function will be comparable to that of a reverse-proxy web-application
load balancer designed to be deployed adjacent to the web server cluster for
added performance and high availability. It demos things like HTTP server probing
with sockets and fast address rewriting inline in the service plane. This
application is configured with the service set configuration construct,
although the service plane component is implemented as a program not a plug-in.
A plug-in edition of the equilibrium load balancer application also exists with
slightly different but comparable functionality.  

Control plane programs: 1  

Service plane programs: 1

**Title: Equilibrium 2 traffic load balancing (service
chaining plug-in edition)**

Formal name in UI: sync-equilibrium2  

Description:  This sample
application consists of the development of a basic load-balancing system
designed to operate quickly in the network data path in the services plane. The
system function will be comparable to that of an application server load
balancer designed to be deployed adjacent to the server cluster for added
performance and high availability. The Equilibrium 2 application contains a
management component running on the control plane and two service plane
plug-ins, the classify service and the balance service. This application is
configured with the service set configuration construct, where the classify and
the balance services can be chained together and optionally with other Junos OS
services. A standalone program edition of the equilibrium load balancer
application also exists with slightly different but comparable functionality.  

Control plane programs: 1  

Service plane plug-ins: 2

**Title: Helloworld for the Service Slane: Hellopics**  

Formal name in UI: sync-hellopics  

Description:  The hellopics sample
application is a very basic sample application that comprises of some service
plane components that don’t actually do any inline packet processing. This
sample code is good at demonstrating IPC between the control and service plane
and application packaging basics for bundling several packages of programs
together.  

Control plane programs: 1  

Service plane programs: 2

**Title: Traffic Probing (single threaded edition)**

Formal name in UI: sync-ipprobe  

Description:  This application
contains two programs for the Routing Engine. The probe client runs on one end
to initiate and send probes as well as receive probe packets back. The probe
manager runs on another Routing Engine of another device. It accepts probe
requests and sends replies. This sample code demonstrates how to use raw sockets
for ICMP/UDP/TCP traffic. There is also a multithreaded edition of this sample
application.  

Control plane programs: 2  

Service plane programs: 0

**Title: Traffic Probing (multithreaded edition)**  

Formal name in UI: sync-ipprobe-mt  

Description:  This is the
multithreaded edition of the Traffic Probing (ipprobe) application. Both client
and manager are integrated into a single daemon in this edition. In addition to
the APIs demonstrated in the single threaded edition of this sample
application, this one demonstrates using multiple threads for a control plane
application which is unconventional.  

Control plane programs: 1  

Service plane programs: 0

**Title: Traffic Snooping**  

Formal name in UI: sync-ipsnooper  

Description:  This application
contains both control and data threads running on a service engine. The data
threads capture packets inline and pass packet information to the control
threads. The control component implements a simple TCP server to which remote clients
may connect to get packet information sent to them. Among other things, the
APIs demonstrates here show basic packet processing, a TCP server running in
the same process on the control plane and control thread flow affinity. To
simplify this sample code, there is no control component to manage this
application. Its behavior is not configurable, it is hard coded.  

Control plane programs: 0  

Service plane programs: 1

**Title: MoniTube IPTV Traffic Monitor (standalone program edition)**

Formal name in UI: sync-monitube  

Description:  This sample
application consist of the development of a basic IPTV monitoring application
designed to operate quickly in the network data path in the service plane. The
app functionality includes monitoring selected RTP streams for calculation of
their media delivery index (MDI) (RFC 4445). The application is designed to be
deployed on any nodes that pass IPTV broadcast service or video on demand (VoD)
streams which are respectively delivered over IP multicast and unicast. The APIs
demonstrated in the code show IPC, packet processing, monitoring, manipulation
and dropping, using object cache, using the forwarding database, using
spinlocks, and much more. This is the program edition of the implementation of
this app. There is also a similar plug-in edition.  

Control plane programs: 1  

Service plane programs: 1

**Title: MoniTube 2 IPTV Traffic Monitor (service chaining plug-in edition)**

Formal name in UI: sync-monitube2-plugin  

Description:  This sample
application consist of the development of a basic IPTV monitoring application
designed to operate quickly in the network data path in the service plane. The
app functionality includes monitoring selected RTP streams for calculation of
their media delivery index (MDI) (RFC 4445). The application is designed to be
deployed on any nodes that pass IPTV broadcast service or video on demand (VoD)
streams which are respectively delivered over IP multicast and unicast. The
APIs demonstrated in the code show IPC, packet processing, monitoring, manipulation
and dropping, using object cache, using the forwarding database, using
spinlocks, and much more. This is the chainable plug-in edition of the
implementation of this app. There is also a similar standalone program edition.  

Control plane programs: 1  

Service plane plug-ins: 1

**Title: Basic Inline Packet Processor**

Formal name in UI: sync-packetproc  

Description:  This sample
application shows very basic inline packet processing with data threads each
with a primitive packet loop. The code can be conditionally compiled with 1 of
3 implementations showing how to start up the data threads’ packet loops. This
would be the equivalent of a helloworld for packet processing. To simplify this
sample code, there is no control component to manage this application. Its
behavior is not configurable, it is hard coded.  

Control plane programs: 0  

Service plane programs: 1

**Title: Packet Passthru Plug-in**

Formal name in UI: sync-passthru  

Description:  This sample
application is the “helloworld” of a packet processing implementation in a
plug-in component form. The application merely passes packets through it. It
does maintain session state, to demonstrate using the session context APIs, but
the session state is only a basic string describing each direction of the flow.
To simplify this sample code, there is no control component to manage this
application. Its behavior is not configurable, it is hard coded.  

Control plane programs: 0  

Service plane plug-ins: 1

 

**Title: Subscriber Policy Manager**

Formal name in UI: sync-policy-manager  

Description:  This sample
application shows some basic subscriber policy management. This is a fairly
unnecessarily elaborate design of the functionality because it has evolved
overtime to do more and more and demonstrates many APIs in various places. It
shows lower-level IPC with TCP sockets (rather than the abstracted IPC APIs)
between the 2 control plane programs, one of which is a policy server (PS) and
the other the enforcer (PE). The policy enforcer also manipulates routing to
the service plane with special service routes and service next hops as per the
app’s configuration. On the service plane a captive portal program (CP) and the
packet filter (PF) run. The packet filter re-writes destination addresses of
unauthorized subscriber traffic to the address of the captive portal web server
where the user can authenticate. To do this transparently, the packet filter
maintains a NAT-like address translation table. In the sense that the packet
filter and captive portal work together to terminate TCP traffic that wasn’t
originally destined to the device/server itself, this application in some
respects closely parallels a transparent TCP proxy implementation. Lastly and
of frequent interest, the control plane policy enforcement app also
demonstrates running a Junos operational script to make a configuration change.  

Control plane programs: 2  

Service plane programs: 2

 

**Title: Packet Fragment Reassembler**

Formal name in UI: sync-reassembler  

Description:  This sample
application consists of the development of a basic IP fragment reassembler that
can receive IP fragment packets and reassemble them into one whole IP packet.
This code is particularly good at demonstrating the JBUF packet allocation and
manipulation APIs. To simplify this sample code, there is no control component
to manage this application. Its behavior is not configurable, it is hard coded.  

Control plane programs: 0  

Service plane programs: 1

 

**Title: Route Manager**

Formal name in UI: sync-route-manager  

Description:  This sample
application is one simple daemon that reads in custom route provisioning rules
configured through its UI extensions. In turn, it uses libssd APIs to add,
delete, and manipulate routes programmatically. It’s a simple baseline showing
developers how they can manipulate routing, though more realistic use cases
would change routes more dynamically rather than based on simple provisioning
rules. This application is similar to the Route Manipulation Basics (jnx-routeservice)
but allows for provisioning additional complex options and hence demonstrates
additional APIs to manipulate the routes it manages.  

Control plane programs: 1  

Service plane programs: 0

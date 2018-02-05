# ubus (OpenWrt micro bus architecture) #

To provide communication between various daemons and applications in OpenWrt a project called ubus has been developed. It consists of several parts including daemon, library and some extra helpers.

The heart of this project is the ubusd daemon. It provides an interface for other daemons to register themselves as well as sending messages. For those curious, this interface is implemented using Unix sockets and it uses TLV (type-length-value) messages.

To simplify development of software using ubus (connecting to it) a library called libubus has been created.

Every daemon registers a set of paths under a specific namespace. Every path can provide multiple procedures with any number of arguments. Procedures can reply with a message.

The code is published under LGPL 2.1 and can be found via git at [http://git.openwrt.org/project/ubus.git](http://git.openwrt.org/project/ubus.git). It's included in OpenWrt since [r28499](https://dev.openwrt.org/changeset/28499).

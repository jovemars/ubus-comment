/*
 * usock - socket helper functions
 *
 * Copyright (C) 2010 Steven Barth <steven@midlink.org>
 * Copyright (C) 2011-2012 Felix Fietkau <nbd@openwrt.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include "usock.h"

static void usock_set_flags(int sock, unsigned int type)
{
    // #include <unistd.h>
    // #include <fcntl.h>
    // int fcntl(int fd, int cmd, ... /* arg */ );
    // Description: performs one operation determined by cmd on fd.
    //              Whether or not optional third argument required depends on cmd.
    //              Some operations below are not supported in old kernel versions, which can test errno EINVAL.
    //              Duplicating a file descriptor:
    //                  F_DUPFD - Duplicate fd using the lowest available file descriptor greater than or equal to arg.
    //                  F_DUPFD_CLOEXEC - As for F_DUPFD, duplicate file descriptor with the close-on-exec flag set.
    //              File descriptor flags(currently only FD_CLOEXEC):
    //                  F_GETFD - Return the file descriptor flags, arg is ignored.
    //                  F_SETFD - Set the file descriptor flags to the value specified by arg.
    //                            NOTE: call fcntl(fd, F_SETFD, FD_CLOEXEC) and ford() plus execve() at the same time
    //                                  may unintentionally leak the file descriptor to child process.
    //              File status flags:
    //                  F_GETFL - Return the file access mode and the file status flags; arg is ignored.
    //                  F_SETFL - Set the file status flags to the value specified by arg.
    //                            NOTE: only O_APPEND, O_ASYNC, O_DIRECT, O_NOATIME, O_NONBLOCK flags can be set on Linux.
    //                                  File access mode (O_RDONLY, O_WRONLY, O_RDWR) and file creation flags (O_CREAT, O_EXCL,
    //                                  O_NOCTTY, O_TRUNC) in arg are ignored.
    //              Advisory record locking:
    //                  NOTE: The third argument points to a flock structure that has at least the following fields:
    //                            struct flock {
    //                                ...
    //                                short l_type;    /* Type of lock: F_RDLCK, F_WRLCK, F_UNLCK */
    //                                short l_whence;  /* How to interpret l_start: SEEK_SET, SEEK_CUR, SEEK_END */
    //                                off_t l_start;   /* Starting offset for lock */
    //                                off_t l_len;     /* Number of bytes to lock */
    //                                pid_t l_pid;     /* PID of process blocking our lock
    //                                                    (set by F_GETLK and F_OFD_GETLK) */
    //                                ...
    //                            };
    //                        Record locks are not inherited by a child created via fork(), but are preserved across an execve().
    //                        Record locks are associated with the process, this has some unfortunate consequences:
    //                            - close() a file descriptor will cause all of a process's locks on the file lost, regardless which file descriptor.
    //                            - Threads in a process share locks, can not use record lock to ensure that threads don't simultaneously access the same region of a file.
    //                  F_SETLK - Acquire a lock (when l_type is F_RDLCK or F_WRLCK) or release a lock (when l_type is F_UNLCK)
    //                            on the bytes specified by the l_whence, l_start, and l_len fields of lock. If a conflicting lock
    //                            is held on the file, this call returns -1 unblock.
    //                  F_SETLKW - As for F_SETLK, but if a conflicting lock is held on the file, then wait for that lock to be released.
    //                  F_GETLK - Test if a lock pointed by flock can placed on the file. 
    //                            If this lock could be placed, return F_UNLCK in the l_type instead of actually placing.
    //                            If placing the lock was precluded, returns details about one of incompatible locks in flock.
    //                            NOTE: In order to place a read lock, fd must be open for reading.
    //                                  In ordor to place a write lock, fd must be open for writing.
    //                                  For a particular byte range of a file opened by a process:
    //                                      - Other processes may place any lock, when this range holds F_UNLCK lock;
    //                                      - Other processes may place F_RDLCK lock, when this range has held F_RDLCK lock;
    //                                      - Other processes can not place F_WRLCK lock, when this range has held F_RDLCK lock;
    //                                      - Other processes can not place any lock, when this range has held F_WRLCK lock;
    //                                      - New lock can be placed in one same process, but will replace old one;
    //              Open file description locks(Linux-specific):
    //                  NOTE: Open file description locks is byte-range lock also, but defferent of record lock:
    //                        - open file description locks are associated with the open file descriptor on which they are acquired;
    //                        - open file description locks are inherited across fork() (and clone() with CLONE_FILES);
    //                        - open file description locks are released on the last close of the open file descriptor.
    //                        - open file description locks placed on an already locked region via the same open file descriptor,
    //                          or via a duplicate of the file descriptor(fork(), dup(), fcntl() F_DUPFD), the existing lock is converted to the new lock type.
    //                        - open file description locks may conflict with each other when they are acquired via different open file descriptions(open()).
    //                  F_OFD_SETLK - Acquire(F_RDLCK or F_WRLCK) or release(F_UNLCK) an open file description lock
    //                  F_OFD_SETLKW - As for F_OFD_SETLK, but wait for conflicting lock to be released
    //                  F_OFD_GETLK - Test if we can place a open file description lock on the file.
    //              Managing signals:
    //                  F_GETOWN - Return the process ID(positive) or process group ID(negative) currently receiving SIGIO and SIGURG signals for events on fd.
    //                  F_SETOWN - Set the process ID or process group ID that will receive SIGIO and SIGURG signals for events on fd. ID is specified in arg.
	if (!(type & USOCK_NOCLOEXEC))
		fcntl(sock, F_SETFD, fcntl(sock, F_GETFD) | FD_CLOEXEC);

	if (type & USOCK_NONBLOCK)
		fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);
}

static int usock_connect(struct sockaddr *sa, int sa_len, int family, int socktype, bool server)
{
	int sock;

    // #include <sys/types.h>
    // #include <sys/socket.h>
    // int socket(int domain, int type, int protocol); 
    // Description: creates an endpoint for communication and returns a descriptor.
    // Input: domain - selects the protocol family which will be used for communication. These families
    //               are defined in <sys/socket.h>, including:
    //                  AF_UNIX, AF_LOCAL       Local communication
    //                  AF_INET                 IPv4 Internet protocols
    //                  AF_INET6                IPv6 Internet protocols
    //                  AF_IPX                  IPX - Novell protocols
    //                  AF_NETLINK              Kernel user interface device
    //                  AF_X25                  ITU-T X.25 / ISO-8208 protocol
    //                  AF_AX25                 Amateur radio AX.25 protocol
    //                  AF_ATMPVC               Access to raw ATM PVCs
    //                  AF_APPLETALK            Appletalk
    //                  AF_PACKET               Low level packet interface
    //        type - specifies the communication semantics, including:
    //                  SOCK_STREAM     Provides sequenced, reliable, two-way, connection-based byte streams.
    //                                  before sent or receive data, the stream socket must be in connected state by calling connect().
    //                                  SIGPIPE signal is raised if a process sends or receives on a broken stream
    //                  SOCK_DGRAM      Supports datagrams (connectionless, unreliable messages of a fixed maximum length). 
    //                  SOCK_SEQPACKET  Provides a sequenced, reliable, two-way connection-based data transmission path
    //                                  for datagrams of fixed maximum length; a consumer is required to read an entire
    //                                  packet with each input system call. NOT implemented for AF_INET.
    //                  SOCK_RAW        Provides raw network protocol access. 
    //                  SOCK_RDM        Provides a reliable datagram layer that does not guarantee ordering. 
    //               the type argument may include the bitwise OR of any of the following values(Linux-specific),
    //               to modify the behavior of socket():
    //                  SOCK_NONBLOCK    Set the O_NONBLOCK file status flag on the new file descriptor.
    //                  SOCK_CLOEXEC     Set the close-on-exec (FD_CLOEXEC) flag on the new file descriptor.
    //        protocol - specifies a particular protocol to be used with the socket. Normally its value is 0,
    //                   because only one protocol exists to support a particular socket type within a given protocol family.
    // ----------------------------------------------------------------------
    // |                            I/O events                              |
    // ----------------------------------------------------------------------
    // |Event      | Poll flag | Occurrence                                 |
    // ----------------------------------------------------------------------
    // |Read       | POLLIN    | New data arrived.                          |
    // ----------------------------------------------------------------------
    // |Read       | POLLIN    | A connection setup has been completed (for |
    // |           |           | connection-oriented sockets)               |
    // ----------------------------------------------------------------------
    // |Read       | POLLHUP   | A disconnection request has been initiated |
    // |           |           | by the other end.                          |
    // ----------------------------------------------------------------------
    // |Read       | POLLHUP   | A connection is broken (only for           |
    // |           |           | connection-oriented protocols).  When the  |
    // |           |           | socket is written SIGPIPE is also sent.    |
    // ----------------------------------------------------------------------
    // |Write      | POLLOUT   | Socket has enough send buffer space for    |
    // |           |           | writing new data.                          |
    // ----------------------------------------------------------------------
    // |Read/Write | POLLIN |  | An outgoing connect(2) finished.           |
    // |           | POLLOUT   |                                            |
    // ----------------------------------------------------------------------
    // |Read/Write | POLLERR   | An asynchronous error occurred.            |
    // ----------------------------------------------------------------------
    // |Read/Write | POLLHUP   | The other end has shut down one direction. |
    // ----------------------------------------------------------------------
    // |Exception  | POLLPRI   | Urgent data arrived.  SIGURG is sent then. |
    // ----------------------------------------------------------------------

	sock = socket(family, socktype, 0);
	if (sock < 0)
		return -1;

	if (server) {
		const int one = 1;

        // #include <sys/types.h>
        // #include <sys/socket.h>
        // int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
        // Description: Set options for the socket referred to by the file descriptor sockfd.
        // Input: sockfd - the file descriptor which to be set.
        //        level - at which the option resides. <sys/socket.h> header defines the following macro
        //                for use as the level argument:
        //                SOL_SOCKET    Options to be accessed at socket level, not protocol level.
        //                IPPROTO_IP    Options level for IP
        //        optname - the name of the specified option.
        //                  For socket API level:
        //                      SO_ACCEPTCONN       Socket is accepting connections. 
        //                      SO_BROADCAST        Transmission of broadcast messages is supported. 
        //                      SO_DEBUG            Debugging information is being recorded. 
        //                      SO_DONTROUTE        bypass normal routing 
        //                      SO_ERROR            Socket error status. 
        //                      SO_KEEPALIVE        Connections are kept alive with periodic messages. 
        //                      SO_LINGER           Socket lingers on close. 
        //                      SO_OOBINLINE        Out-of-band data is transmitted in line. 
        //                      SO_RCVBUF           Receive buffer size. 
        //                      SO_RCVLOWAT         receive "low water mark" 
        //                      SO_RCVTIMEO         receive timeout 
        //                      SO_REUSEADDR        Reuse of local addresses is supported. 
        //                      SO_SNDBUF           Send buffer size. 
        //                      SO_SNDLOWAT         send "low water mark" 
        //                      SO_SNDTIMEO         send timeout 
        //                      SO_TYPE             Socket type. 
        //                  For IP level:
        //                      IP_ADD_MEMBERSHIP   Join a multicast group.  Argument is an ip_mreqn structure.
        //                                          struct ip_mreqn {
        //                                              struct in_addr imr_multiaddr; /* IP multicast group address */
        //                                              struct in_addr imr_address;   /* IP address of local interface */
        //                                              int            imr_ifindex;   /* interface index */
        //                                          };
        //                      IP_ADD_SOURCE_MEMBERSHIP   Join a multicast group and allow receiving data only from a
        //                                                 specified source.  Argument is an ip_mreq_source structure.
        //                                                 struct ip_mreq_source {
        //                                                      struct in_addr imr_multiaddr;  /* IP multicast group address */
        //                                                      struct in_addr imr_interface;  /* IP address of local interface */
        //                                                      struct in_addr imr_sourceaddr; /* IP address of multicast source */
        //                                                  };
        //                      IP_BIND_ADDRESS_NO_PORT     Inform the kernel to not reserve an ephemeral port when using bind() with
        //                                                  a port number of 0. The port will later be automatically chosen at connect() time.
        //                      IP_BLOCK_SOURCE     Stop receiving multicast data from a specific source in a given group.
        //                                          only valid after using either IP_ADD_MEMBERSHIP or IP_ADD_SOURCE_MEMBERSHIP.
        //                      IP_DROP_MEMBERSHIP  Leave a multicast group.  Argument is an ip_mreqn or ip_mreq structure.
        //                      IP_DROP_SOURCE_MEMBERSHIP   Leave a source-specific group, stop receiving data from a given multicast
        //                                                  group that come from a given source.
        //                      IP_FREEBIND    If enabled, binding to an IP address that is nonlocal or does not (yet) exist is allowed.
        //                      IP_HDRINCL     If enabled, the user supplies an IP header in front of the user data. Valid only for SOCK_RAW sockets
        //                                     When enabled, the values set by IP_OPTIONS, IP_TTL, and IP_TOS are ignored.
        //                      IP_MSFILTER    This option provides access to the advanced full-state filtering API.
        //                                     Argument is an ip_msfilter structure.
        //                                      struct ip_msfilter {
        //                                          struct in_addr imsf_multiaddr; /* IP multicast group address */
        //                                          struct in_addr imsf_interface; /* IP address of local interface */
        //                                          uint32_t       imsf_fmode;     /* Filter-mode: MCAST_INCLUDE or MCAST_EXCLUDE */
        //                                          uint32_t       imsf_numsrc;    /* Number of sources in the following array */
        //                                          struct in_addr imsf_slist[1];  /* Array of source addresses */
        //                                      };
        //                      IP_MTU         Retrieve the current known path MTU of the current socket.
        //                                     only valid for getsockopt().
        //                      IP_MTU_DISCOVER     Set or receive the Path MTU Discovery setting for a socket.
        //                      IP_MULTICAST_ALL    modify the delivery policy of multicast messages to sockets bound to the wildcard INADDR_ANY address
        //                      IP_MULTICAST_IF     Set the local device for a multicast socket.
        //                      IP_MULTICAST_LOOP   Set or read a boolean integer argument that determines whether sent multicast packets
        //                                          should be looped back to the local sockets.
        //                      IP_MULTICAST_TTL    Set or read the time-to-live value of outgoing multicast packets for this socket.
        //                      IP_NODEFRAG         If enabled (nonzero), the reassembly of outgoing packets is disabled in the netfilter layer.
        //                                          valid only for SOCK_RAW sockets
        //                      IP_OPTIONS          Set or get the IP options to be sent with every packet from this socket.
        //                      IP_PKTINFO          Pass an IP_PKTINFO ancillary message that contains a pktinfo structure that supplies some information
        //                                          about the incoming packet.
        //                                           struct in_pktinfo {
        //                                               unsigned int   ipi_ifindex;  /* Interface index */
        //                                               struct in_addr ipi_spec_dst; /* Local address */
        //                                               struct in_addr ipi_addr;     /* Header Destination address */
        //                                           };
        //                      IP_RECVOPTS         Pass all incoming IP options to the user in a IP_OPTIONS control message.
        //                      IP_RECVORIGDSTADDR  This boolean option enables the IP_ORIGDSTADDR ancillary message in recvmsg(),
        //                                          in which the kernel returns the original destination address of the datagram being received.
        //                      IP_RECVTOS          If enabled, the IP_TOS ancillary message is passed with incoming packets.
        //                      IP_RECVTTL          When this flag is set, pass a IP_TTL control message with the
        //                                          time-to-live field of the received packet as a byte.
        //                      IP_RETOPTS          Identical to IP_RECVOPTS, but returns raw unprocessed options with timestamp
        //                                          and route record options not filled in for this hop.
        //                      IP_ROUTER_ALERT     Pass all to-be forwarded packets with the IP Router Alert option set to this socket.
        //                      IP_TOS              Set or receive the Type-Of-Service (TOS) field that is sent with every IP packet originating from this socket.
        //                      IP_TRANSPARENT      Setting this boolean option enables transparent proxying on this socket.
        //                      IP_TTL              Set or retrieve the current time-to-live field that is used in every packet sent from this socket.
        //                      IP_UNBLOCK_SOURCE   Unblock previously blocked multicast source. Returns EADDRNOTAVAIL when given source is not being blocked.
        //        optval - the value of the specified option.
        //        optlen - value length of the specified option.
		setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        // #include <sys/types.h>
        // #include <sys/socket.h>
        // int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
        // Description: bind a name to a socket. bind() assigns the address specified by addr to
        //              the socket referred to by the file descriptor sockfd. addrlen specifies the
        //              size, in bytes, of the address structure pointed to by addr.
        //              a SOCK_STREAM socket may receive connections after binding a local address.
        //              The actual structure passed for the addr argument depends on the address family.
        //              The sockaddr structure is defined as something like:
        //                  struct sockaddr {
        //                      sa_family_t sa_family;
        //                      char        sa_data[14];
        //                  }
        //              for AF_INET, actual addr pointer to sockaddr_in structure.
        //                  struct sockaddr_in {
        //                      sa_family_t    sin_family; /* address family: AF_INET */
        //                      in_port_t      sin_port;   /* port in network byte order */
        //                      struct in_addr {
        //                          uint32_t       s_addr;     /* address in network byte order */
        //                      } sin_addr;   /* internet address */
        //                  };
        //              for AF_INET6, actual addr pointer to sockaddr_in6 structure.
        //                  struct sockaddr_in6 {
        //                      sa_family_t     sin6_family;   /* AF_INET6 */
        //                      in_port_t       sin6_port;     /* port number */
        //                      uint32_t        sin6_flowinfo; /* IPv6 flow information */
        //                      struct in6_addr {
        //                          unsigned char   s6_addr[16];   /* IPv6 address */
        //                      } sin6_addr;     /* IPv6 address */
        //                      uint32_t        sin6_scope_id; /* Scope ID (new in 2.4) */
        //                  };
        //              for AF_UNIX, actual addr pointer to sockaddr_un structure.
        //                  struct sockaddr_un {
        //                      sa_family_t sun_family;               /* AF_UNIX */
        //                      char        sun_path[108];            /* pathname */
        //                  };
        //              for AF_X25, actual addr pointer to sockaddr_x25 structure.
        //              for AF_NETLINK, actual addr pointer to sockaddr_nl structure.
		if (!bind(sock, sa, sa_len) &&

            // #include <sys/types.h>
            // #include <sys/socket.h>
            // int listen(int sockfd, int backlog);
            // Description: listen() marks the socket referred to by sockfd as a passive socket,
            //              that is, as a socket that will be used to accept incoming connection
            //              requests using accept().
            //              The sockfd argument is a file descriptor that refers to a socket of
            //              type SOCK_STREAM or SOCK_SEQPACKET.
            //              The backlog argument defines the maximum queue length for completely
            //              established sockets waiting to be accepted. If a connection request arrives
            //              when the queue is full, the client may raise ECONNREFUSED error or reattempt.
		    (socktype != SOCK_STREAM || !listen(sock, SOMAXCONN)))
			return sock;
	} else {

        // #include <sys/types.h>
        // #include <sys/socket.h>
        // int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
        // Description: Connects sockfd to the address specified by addr.
        //              The format of addr is determined by the address family of sockfd.
        //              If the socket is of type SOCK_DGRAM, addr is the only address datagrams
        //              are sent to or received from, then system calls send() and recv() may be used instead of sendto() and recvfrom().
        //              If the socket is of type SOCK_STREAM or SOCK_SEQPACKET, this call attempts to make
        //              a connection to the socket that is bound to the address specified by addr.
		if (!connect(sock, sa, sa_len) || errno == EINPROGRESS)
			return sock;
	}

    // #include <unistd.h>
    // int close(int fd);
    // Description: Closes a file descriptor, so that it no longer refers to any file and may be reused.
    //              Any record locks held on the file owned by the process are removed.
    //              If fd is the last reference to a file which has been removed using unlink(), the file
    //              will be deleted.
    //              close() does not guarantee that the data has been saved to disk。
    //              The close-on-exec file descriptor flag can be used to ensure that a file descriptor is
    //              automatically closed upon a successful execve().
    //              fsync() before close() can be used to diagnose I/O errors.
	close(sock);
	return -1;
}

static int usock_unix(const char *host, int socktype, bool server)
{
    // #include <sys/socket.h>
    // #include <sys/un.h>
    // #define UNIX_PATH_MAX    108
    // struct sockaddr_un {
    //    sa_family_t sun_family; /* AF_UNIX */
    //    char sun_path[UNIX_PATH_MAX]; /* pathname */
    // };
    // Description: sun_family always contains AF_UNIX.
    //              Three types of address are distinguished in this structure:
    //                  pathname: a UNIX domain socket can be bound to a null-terminated file system
    //                            pathname using bind(). When the address of the socket is returned by
    //                            getsockname(), getpeername(), and accept(), its length is
    //                            offsetof(struct sockaddr_un, sun_path) + strlen(sun_path) + 1, and
    //                            sun_path contains the null-terminated pathname. 
    //                  unnamed:  A stream socket that has not been bound to a pathname using bind()
    //                            has no name. Two sockets created by socketpair() are unnamed. When the
    //                            address of an unnamed socket is returned by getsockname(), getpeername(),
    //                            and accept(), its length is sizeof(sa_family_t), and sun_path should not be inspected.
    //                  abstract: an abstract socket address is distinguished by the fact that sun_path[0]
    //                            is a null byte ('\0') followed by specified length of bytes giving the namespace.
    //                            The name has no connection with file system pathnames. When the address
    //                            of an abstract socket is returned by getsockname(), getpeername(), and accept(),
    //                            the returned addrlen is greater than sizeof(sa_family_t), and the name of the socket
    //                            is contained in the first (addrlen - sizeof(sa_family_t)) bytes of sun_path.
    //                            The abstract socket namespace is a nonportable Linux extension.
	struct sockaddr_un sun = {.sun_family = AF_UNIX};

	if (strlen(host) >= sizeof(sun.sun_path)) {
		errno = EINVAL;
		return -1;
	}
	strcpy(sun.sun_path, host);

    // #include <sys/socket.h>
    // #include <sys/un.h>
    // unix_socket = socket(AF_UNIX, type, 0);
    // error = socketpair(AF_UNIX, type, 0, int *sv); 
    // Description: The AF_UNIX (also known as AF_LOCAL) socket family is used to communicate between
    //              processes on the same machine efficiently.
    //              UNIX domain sockets can be either unnamed, or bound to a file system pathname.
    //              Valid types are:
    //                  SOCK_STREAM, for a stream-oriented socket;
    //                  SOCK_DGRAM, for a datagram-oriented socket(reliable and don't reorder datagrams)
    //                  SOCK_SEQPACKET, for a connection-oriented socket
	return usock_connect((struct sockaddr*)&sun, sizeof(sun), AF_UNIX, socktype, server);
}

static int usock_inet(int type, const char *host, const char *service, int socktype, bool server)
{
	struct addrinfo *result, *rp;
	struct addrinfo hints = {
		.ai_family = (type & USOCK_IPV6ONLY) ? AF_INET6 :
			(type & USOCK_IPV4ONLY) ? AF_INET : AF_UNSPEC,
		.ai_socktype = socktype,
		.ai_flags = AI_ADDRCONFIG
			| ((type & USOCK_SERVER) ? AI_PASSIVE : 0)
			| ((type & USOCK_NUMERIC) ? AI_NUMERICHOST : 0),
	};
	int sock = -1, ret;

    // #include <sys/types.h>
    // #include <sys/socket.h>
    // #include <netdb.h>
    // int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
    // Description: Given node and service, getaddrinfo() returns one or more addrinfo structures:
    //               struct addrinfo {
    //                   int              ai_flags;        /* additional options, Multiple flags by bitwise OR */
    //                   int              ai_family;       /* desired address family: AF_INET/AF_INET6/AF_UNSPEC */
    //                   int              ai_socktype;     /* preferred socket type: SOCK_STREAM/SOCK_DGRAM/0 */
    //                   int              ai_protocol;     /* protocol for the returned socket, 0 for any */
    //                   socklen_t        ai_addrlen;
    //                   struct sockaddr *ai_addr;
    //                   char            *ai_canonname;
    //                   struct addrinfo *ai_next;
    //               };
    //              node - specifies either a numerical network address, or a network hostname.
    //                     for IPv4, numbers-and-dots notation as supported by inet_aton();
    //                     for IPv6, hexadecimal string format as supported by inet_pton().
    //              service - sets the port in each returned address structure.
    //                        This argument can be a service name or decimal number.
    //                        If service is NULL, then the port number of the returned addresses will be uninitialized.
    //                        If hints.ai_flags & AI_NUMERICSERV != 0 && service != NULL, then service must point to a numeric port number string.
    //              Either node or service, but not both, may be NULL.
    //              hints - specifies the format of returned socket address list by argument res.
    //                      If hints == NULL, equals to
    //                          hints = {
    //                              .ai_socktype = 0, 
    //                              .ai_protocol = 0, 
    //                              .ai_family   = AF_UNSPEC, 
    //                              .ai_flags    = (AI_V4MAPPED | AI_ADDRCONFIG),
    //                          };
    //                      If hints.ai_flags & AI_ADDRCONFIG != 0, then the returned addresses are IPv4 only or IPv6 only as local system configured.
    //                      If hints.ai_flags & AI_NUMERICHOST != 0, then node must be a numerical network address.
    //                      If hints.ai_flags & AI_PASSIVE != 0 && node == NULL, then the returned addresses will contain INADDR_ANY or IN6ADDR_ANY_INIT for bind()..
    //                      If hints.ai_flags & AI_PASSIVE != 0 && node != NULL, then AI_PASSIVE is ignored.
    //                      If hints.ai_flags & AI_PASSIVE == 0, then the returned addresses will be suitable for use with connect(), sendto(), recvfrom(). If node == NULL, then the returned addresses will be set to INADDR_LOOPBACK or IN6ADDR_LOOPBACK_INIT.
    //                      If hints.ai_flags & AI_V4MAPPED != 0 && hints.ai_family == AF_INET6, and no matching IPv6 addresses could be found, then return IPv4-mapped IPv6 addresses;
    //                      If hints.ai_flags & (AI_V4MAPPED | AI_ALL) != 0, then return both IPv6 and IPv4-mapped IPv6 addresses
    //                      If hints.ai_flags & AI_V4MAPPED ==0, then AI_ALL is ignored.
    //              res - a pointer to the start of a linked list of addrinfo structures. 
    //                    The addresses should be used in the order as getaddrinfo() returned.
    //                    If hints.ai_flags & AI_CANONNAME != 0, then the ai_canonname field of the first of the addrinfo structures in the returned list is set to point to the official name of the host.
    //                    The remaining fields of each returned addrinfo structure are initialized as follows:
    //                        The ai_family, ai_socktype, and ai_protocol fields return the socket creation parameters which can be used in socket();
    //                        The ai_addr field points to the socket address and the ai_addrlen field place the length of the socket address, in bytes.
    //                    The memory allocated for res can be freed by freeaddrinfo().
    if ((ret = getaddrinfo(host, service, &hints, &result)) != 0)
    {
        fprintf(stderr, "getaddrinfo() failed: %s\n", gai_strerror(ret));
		return -1;
    }

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sock = usock_connect(rp->ai_addr, rp->ai_addrlen, rp->ai_family, socktype, server);
		if (sock >= 0)
			break;
	}

    // #include <sys/types.h>
    // #include <sys/socket.h>
    // #include <netdb.h>
    // void freeaddrinfo(struct addrinfo *res);
    // Description: The freeaddrinfo() function frees the memory that was allocated for
    //              the dynamically allocated linked list res.
	freeaddrinfo(result);
	return sock;
}

int usock(int type, const char *host, const char *service) {
	int socktype = ((type & 0xff) == USOCK_TCP) ? SOCK_STREAM : SOCK_DGRAM;
	bool server = !!(type & USOCK_SERVER);
	int sock;

     // TODO: should check whether host is valid for usock_inet()

    // create server socket
    // AF_INET域socket通信过程: 发送方、接收方依赖IP-Port来标识.
    //     发送方通过系统调用send()将原始数据发送到操作系统内核缓冲区中,内核缓冲区从上到下依
    //     次经过TCP层、IP层、链路层的编码，分别添加对应的头部信息，经过网卡将一个数据包发送到网络中。
    //     经过网络路由到接收方的网卡,网卡通过系统中断将数据包通知到接收方的操作系统，再沿着发送方编
    //     码的反方向进行解码，即依次经过链路层、IP层、TCP层去除头部、检查校验等，最终将原始数据上报到接收方进程。
    // AF_UNIX域socket通信过程: 典型的本地IPC，类似于管道，依赖路径名标识发送方和接收方。
    //     发送数据时，指定接收方绑定的路径名，操作系统根据该路径名可以直接找到对应的接收方，
    //     并将原始数据直接拷贝到接收方的内核缓冲区中，并上报给接收方进程进行处理。
    //     接收方可以从收到的数据包中获取到发送方的路径名，并通过此路径名向其发送数据。
    // 相同点: 操作系统提供的接口socket(),bind(),connect(),accept(),send(),recv(),select(),poll(),epoll()相同.
    //     收发数据的过程中，上层应用感知不到底层的差别。
    // 不同点: 建立socket传递的地址域，及bind()的地址结构区别：
    //         socket() 分别传递不同的域AF_INET和AF_UNIX
    //         bind()的地址结构分别为sockaddr_in（制定IP端口）和sockaddr_un（指定路径名）
    //     AF_INET需经过多个协议层的编解码，消耗系统cpu，并且数据传输需要经过网卡，受到网卡带宽的限制; 
    //     AF_UNIX数据到达内核缓冲区后，由内核根据指定路径名找到接收方socket对应的内核缓冲区，直接将数据
    //     拷贝过去，不经过协议层编解码，节省系统cpu，并且不经过网卡，不受网卡带宽的限制。
    //     AF_UNIX的传输速率远远大于AF_INET
    //     AF_INET不仅可以用作本机的跨进程通信，也可以用于不同机器之间的通信; AF_UNIX只能用于本机进程间通信。
	if (type & USOCK_UNIX)
        // use unix domain socket
		sock = usock_unix(host, socktype, server);
	else
        // use linux network stack
		sock = usock_inet(type, host, service, socktype, server);

	if (sock < 0)
		return -1;

	usock_set_flags(sock, type);
	return sock;
}

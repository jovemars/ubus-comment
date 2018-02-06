/*
 * Copyright (C) 2011-2014 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#ifdef FreeBSD
#include <sys/param.h>
#endif
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#include <libubox/blob.h>
#include <libubox/uloop.h>
#include <libubox/usock.h>
#include <libubox/list.h>

#include "ubusd.h"

static struct ubus_msg_buf *ubus_msg_ref(struct ubus_msg_buf *ub)
{
    if (ub->refcount == ~0)
        return ubus_msg_new(ub->data, ub->len, false);

    ub->refcount++;
    return ub;
}

/**
 * ubus_msg_new: fetch a message from blob_buf
 */
struct ubus_msg_buf *ubus_msg_new(void *data, int len, bool shared)
{
    struct ubus_msg_buf *ub;
    int buflen = sizeof(*ub);

    if (!shared)
        // false, create a copy of b.head
        // so we need len more bytes
        buflen += len;

    // #include <stdlib.h>
    // void *calloc(size_t nmemb, size_t size);
    // Desc: allocates memory for an array of nmemb elements of size bytes each and//
    //    returns a pointer to the allocated memory. The memory is set to zero.
    //    If nmemb or size is 0, then calloc() returns NULL.
    ub = calloc(1, buflen);
    if (!ub)
        return NULL;

    // set remote fd later
    ub->fd = -1;

    // fetch the message
    if (shared) {
        // true, use external data buffer
        ub->refcount = ~0;
        ub->data = data;
    } else {
        // false, use the len more bytes calloced earlier
        ub->refcount = 1;
        ub->data = (void *) (ub + 1);
        if (data)
            memcpy(ub + 1, data, len);
    }

    ub->len = len;
    return ub;
}

void ubus_msg_free(struct ubus_msg_buf *ub)
{
    switch (ub->refcount) {
    case 1:
    case ~0:
        if (ub->fd >= 0)
            close(ub->fd);

        free(ub);
        break;
    default:
        ub->refcount--;
        break;
    }
}

static int ubus_msg_writev(int fd, struct ubus_msg_buf *ub, int offset)
{
    static struct iovec iov[2];
    static struct {
        struct cmsghdr h;
        int fd;
    } fd_buf = {
        .h = {
            .cmsg_len = sizeof(fd_buf),
            .cmsg_level = SOL_SOCKET,
            .cmsg_type = SCM_RIGHTS,
        },
    };
    struct msghdr msghdr = {
        .msg_iov = iov,
        .msg_iovlen = ARRAY_SIZE(iov),
        .msg_control = &fd_buf,
        .msg_controllen = sizeof(fd_buf),
    };

    fd_buf.fd = ub->fd;
    if (ub->fd < 0) {
        msghdr.msg_control = NULL;
        msghdr.msg_controllen = 0;
    }

    if (offset < sizeof(ub->hdr)) {
        iov[0].iov_base = ((char *) &ub->hdr) + offset;
        iov[0].iov_len = sizeof(ub->hdr) - offset;
        iov[1].iov_base = (char *) ub->data;
        iov[1].iov_len = ub->len;

        return sendmsg(fd, &msghdr, 0);
    } else {
        offset -= sizeof(ub->hdr);
        return write(fd, ((char *) ub->data) + offset, ub->len - offset);
    }
}

static void ubus_msg_enqueue(struct ubus_client *cl, struct ubus_msg_buf *ub)
{
    if (cl->tx_queue[cl->txq_tail])
        return;

    cl->tx_queue[cl->txq_tail] = ubus_msg_ref(ub);
    cl->txq_tail = (cl->txq_tail + 1) % ARRAY_SIZE(cl->tx_queue);
}

/* takes the msgbuf reference */
void ubus_msg_send(struct ubus_client *cl, struct ubus_msg_buf *ub, bool free)
{
    int written;

    if (!cl->tx_queue[cl->txq_cur]) {
        // no message waiting in transmition queue
        written = ubus_msg_writev(cl->sock.fd, ub, 0);
        if (written >= ub->len + sizeof(ub->hdr))
            goto out;

        if (written < 0)
            written = 0;

        cl->txq_ofs = written;

        /* get an event once we can write to the socket again */
        uloop_fd_add(&cl->sock, ULOOP_READ | ULOOP_WRITE | ULOOP_EDGE_TRIGGER);
    }
    ubus_msg_enqueue(cl, ub);

out:
    if (free)
        ubus_msg_free(ub);
}

static struct ubus_msg_buf *ubus_msg_head(struct ubus_client *cl)
{
    return cl->tx_queue[cl->txq_cur];
}

static void ubus_msg_dequeue(struct ubus_client *cl)
{
    struct ubus_msg_buf *ub = ubus_msg_head(cl);

    if (!ub)
        return;

    ubus_msg_free(ub);
    cl->txq_ofs = 0;
    cl->tx_queue[cl->txq_cur] = NULL;
    cl->txq_cur = (cl->txq_cur + 1) % ARRAY_SIZE(cl->tx_queue);
}

static void handle_client_disconnect(struct ubus_client *cl)
{
    while (ubus_msg_head(cl))
        ubus_msg_dequeue(cl);

    ubusd_proto_free_client(cl);
    if (cl->pending_msg_fd >= 0)
        close(cl->pending_msg_fd);
    uloop_fd_delete(&cl->sock);
    close(cl->sock.fd);
    free(cl);
}

static void client_cb(struct uloop_fd *sock, unsigned int events)
{
    struct ubus_client *cl = container_of(sock, struct ubus_client, sock);
    struct ubus_msg_buf *ub;
    static struct iovec iov;
    static struct {
        struct cmsghdr h;
        int fd;
    } fd_buf = {
        .h = {
            .cmsg_type = SCM_RIGHTS,
            .cmsg_level = SOL_SOCKET,
            .cmsg_len = sizeof(fd_buf),
        }
    };
    struct msghdr msghdr = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
    };

    /* first try to tx more pending data */
    while ((ub = ubus_msg_head(cl))) {
        int written;

        written = ubus_msg_writev(sock->fd, ub, cl->txq_ofs);
        if (written < 0) {
            switch(errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                goto disconnect;
            }
            break;
        }

        cl->txq_ofs += written;
        if (cl->txq_ofs < ub->len + sizeof(ub->hdr))
            break;

        ubus_msg_dequeue(cl);
    }

    /* prevent further ULOOP_WRITE events if we don't have data
     * to send anymore */
    if (!ubus_msg_head(cl) && (events & ULOOP_WRITE))
        uloop_fd_add(sock, ULOOP_READ | ULOOP_EDGE_TRIGGER);

retry:
    if (!sock->eof && cl->pending_msg_offset < sizeof(cl->hdrbuf)) {
        int offset = cl->pending_msg_offset;
        int bytes;

        fd_buf.fd = -1;

        iov.iov_base = &cl->hdrbuf + offset;
        iov.iov_len = sizeof(cl->hdrbuf) - offset;

        if (cl->pending_msg_fd < 0) {
            msghdr.msg_control = &fd_buf;
            msghdr.msg_controllen = sizeof(fd_buf);
        } else {
            msghdr.msg_control = NULL;
            msghdr.msg_controllen = 0;
        }

        bytes = recvmsg(sock->fd, &msghdr, 0);
        if (bytes < 0)
            goto out;

        if (fd_buf.fd >= 0)
            cl->pending_msg_fd = fd_buf.fd;

        cl->pending_msg_offset += bytes;
        if (cl->pending_msg_offset < sizeof(cl->hdrbuf))
            goto out;

        if (blob_pad_len(&cl->hdrbuf.data) > UBUS_MAX_MSGLEN)
            goto disconnect;

        cl->pending_msg = ubus_msg_new(NULL, blob_raw_len(&cl->hdrbuf.data), false);
        if (!cl->pending_msg)
            goto disconnect;

        memcpy(&cl->pending_msg->hdr, &cl->hdrbuf.hdr, sizeof(cl->hdrbuf.hdr));
        memcpy(cl->pending_msg->data, &cl->hdrbuf.data, sizeof(cl->hdrbuf.data));
    }

    ub = cl->pending_msg;
    if (ub) {
        int offset = cl->pending_msg_offset - sizeof(ub->hdr);
        int len = blob_raw_len(ub->data) - offset;
        int bytes = 0;

        if (len > 0) {
            bytes = read(sock->fd, (char *) ub->data + offset, len);
            if (bytes <= 0)
                goto out;
        }

        if (bytes < len) {
            cl->pending_msg_offset += bytes;
            goto out;
        }

        /* accept message */
        ub->fd = cl->pending_msg_fd;
        cl->pending_msg_fd = -1;
        cl->pending_msg_offset = 0;
        cl->pending_msg = NULL;
        ubusd_proto_receive_message(cl, ub);
        goto retry;
    }

out:
    if (!sock->eof || ubus_msg_head(cl))
        return;

disconnect:
    handle_client_disconnect(cl);
}

static bool get_next_connection(int fd)
{
    struct ubus_client *cl;
    int client_fd;

    // #include <sys/types.h>
    // #include <sys/socket.h>
    // int accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
    // Desc: Used with connection-based socket types(SOCK_STREAM, SOCK_SEQPACKET) to extracts
    //       the first connection request on the pending connection queue for the listening socket,
    //       creates a new connected socket, and returns a new file descriptor referring to that socket.
    //       The created socket is not in the listening state, and the listening socket is unaffected.
    //       sockfd  - created by socket(), bound by bind() and listening for connections after listen().
    //                 If no pending connections are present on the queue:
    //                     - accept() blocks until a connection is present, when sockfd is NOT nonblocking;
    //                     - accept() fails with EAGAIN or EWOULDBLOCK, when sockfd is nonblocking.
    //       addr    - a pointer to a sockaddr structure which will be filled in with the address of the
    //                 peer socket. The exact format of addr is determined by the socket's address family.
    //                 When addr is NULL, nothing is filled in; in this case, addrlen should also be NULL.
    //       addrlen - initialized to the size of addr; contains the actual size on return.
    //                 If the buffer size is too small, a greater value than provided will be return.
    //       flags   - bitwise ORed with following values:
    //                 SOCK_NONBLOCK - Set the O_NONBLOCK file status flag on the new open fd.
    //                 SOCK_CLOEXEC  - Set the close-on-exec (FD_CLOEXEC) flag on the new fd.
    //       A readable event will be delivered when a new connection is attempted when using select(), poll(), or epoll().
    client_fd = accept(fd, NULL, 0);
    if (client_fd < 0) {
        switch (errno) {
        case ECONNABORTED:
        case EINTR:
            return true;
        default:
            return false;
        }
    }

    cl = ubusd_proto_new_client(client_fd, client_cb);
    if (cl)
        uloop_fd_add(&cl->sock, ULOOP_READ | ULOOP_EDGE_TRIGGER);
    else
        close(client_fd);

    return true;
}

static void server_cb(struct uloop_fd *fd, unsigned int events)
{
    bool next;

    do {
        // accpet a connection request, and create client socket
        next = get_next_connection(fd->fd);
    } while (next);
}

static struct uloop_fd server_fd = {
    .cb = server_cb,
};

static int usage(const char *progname)
{
    fprintf(stderr, "Usage: %s [<options>]\n"
        "Options: \n"
        "  -s <socket>:        Set the unix domain socket to listen on\n"
        "\n", progname);
    return 1;
}

/**
 * Before main(), 5 avl trees have been constructed:
 *    client    - maintains clients id
 *    obj_types - 
 *    objects   - 
 *    path      - 
 *    patterns  - 
 *    
 */
int main(int argc, char **argv)
{
    const char *ubus_socket = UBUS_UNIX_SOCKET;
    int ret = 0;
    int ch;

    // If all file descriptors referring to the read end of a pipe have been closed, 
    // then a write() will cause a SIGPIPE signal to be generated for the calling process.
    // If the calling process is ignoring this signal, then write() fails with the error EPIPE.
    // #include <signal.h>
    // typedef void (*sighandler_t)(int);
    // sighandler_t signal(int signum, sighandler_t handler);
    // Desc: Sets the behavior of signum to handler, which is either SIG_IGN, SIG_DFL, or
    //       the address of an user defined function. Returns the previous handler or SIG_ERR.
    //           SIG_IGN - Ignore the signal
    //           SIG_DFL - Call the default action.
    //       NOTE: Use sigaction() instead to avoid the version prolem of signal().
    signal(SIGPIPE, SIG_IGN);

    // call epoll_create() to create epoll instance with close-on-exec flag.
    uloop_init();

    // #include <unistd.h>
    // extern char *optarg;
    // extern int optind, opterr, optopt;
    // int getopt(int argc, char * const argv[], const char *optstring);
    // Description: parses the command-line arguments. An element of argv that starts with '-' is an option.
    //              The variable optind is the index of the next element to be processed in argv.
    //              The variable optarg pointers to the argument required by current option.
    //              The variable opterr controls whether print error msgs to STDERR. set to 0 disable print. 
    //              The variable optopt places the erroneous option character. 
    // Input: argc and argv are the argument count and array as passed to the main().
    //        optstring is a string containing the legitimate option characters. an option followed by
    //        a colon requires an argument, two colons takes an optional arg without any blank space.
    //        If the first character of optstring is '+' or the environment variable POSIXLY_CORRECT is set,
    //        then option  processing  stops as soon as a nonoption argument is encountered.
    //        If the first character of optstring is '-', then each nonoption argv-element is handled as if
    //        it were the argument of an option with character code 1.
    // Return: If getopt() is called repeatedly, it returns successively each of the option characters
    //         from each of the option elements. If there are no more option characters, getopt() returns -1.
    //         If there is an option not in optstring was detected or an option followed by a colon took no argument,
    //         getopt() returns '?' by default, and the variable optopt was set to the option character.
    while ((ch = getopt(argc, argv, "s:")) != -1) {
        switch (ch) {
        case 's':
            // A Unix domain socket or IPC socket (inter-process communication socket) is a data
            // communications endpoint for exchanging data between processes executing on the same
            // host operating system. Unix domain sockets support:
            // 1. reliable transmission of stream of bytes (SOCK_STREAM, compare to TCP),
            // 2. unordered and unreliable transmission of datagrams (SOCK_DGRAM, compare to UDP),
            // 3. ordered and reliable transmission of datagrams (SOCK_SEQPACKET, compare to SCTP).
            ubus_socket = optarg;
            break;
        default:
            return usage(argv[0]);
        }
    }

    // #include <unistd.h>
    // int unlink(const char *pathname);
    // Description: deletes a name from the file system. 
    //              If that name was the last link to a file and no processes have the file open, the
    //                file is deleted and the space it was using is made available for reuse;
    //              If the name was the last link to a file but any processes still have the file open,
    //                the file will remain in existence until the last file descriptor referring to it is closed.
    //              If the name referred to a symbolic link, the link is removed.
    //              If the name referred to a socket, fifo or device, the name for it is removed but
    //                processes which have the object open may continue to use it.
    //              If all file descriptor pointing to the file referred to by the name has bees closed,
    //                then unlink() will delete the file.
    unlink(ubus_socket);

    // #include <sys/types.h>
    // #include <sys/stat.h>
    // mode_t umask(mode_t mask);
    // Description: sets the calling process's file creation mask to argument mask & 0777
    //              and returns the previous value of the mask.
    //              umask() is used to modify the permissions placed on newly created files or directories.
    //              Permissions in the umask are turned off from the mode argument to file creating system call,
    //              such as open(), mkdir().
    //              If the parent directory has a default ACL, the default ACL is inherited, and the permission
    //                  bits will be: (the inherited ACL mask & mask argument)
    //              Typical default value for the process umask is 022:
    //                      S_IWGRP | S_IWOTH
    //              Usualiy the mode argument to open() is 0666:
    //                      S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH
    //              Then the permissions on created new file will be 0644:
    //                      S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
    umask(0177);

    // create unix domain socket server in nonblock mode
    // binding ubus_socket for server, or connecting to ubus_socket for client
    server_fd.fd = usock(USOCK_UNIX | USOCK_SERVER | USOCK_NONBLOCK, ubus_socket, NULL);
    if (server_fd.fd < 0) {
        perror("usock");
        ret = -1;
        goto out;
    }

    // add uloop file descriptor to uloop
    uloop_fd_add(&server_fd, ULOOP_READ | ULOOP_EDGE_TRIGGER);

    // start main loop listenning to server_fd
    uloop_run();

    // delete the file ubus_socket linking to
    unlink(ubus_socket);

out:
    uloop_done();
    return ret;
}

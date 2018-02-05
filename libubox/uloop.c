/*
 * uloop - event loop implementation
 *
 * Copyright (C) 2010-2013 Felix Fietkau <nbd@openwrt.org>
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
#include <sys/time.h>
#include <sys/types.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <fcntl.h>
#include <stdbool.h>

#include "uloop.h"
#include "utils.h"

#ifdef USE_KQUEUE
#include <sys/event.h>
#endif
#ifdef USE_EPOLL
#include <sys/epoll.h>
#endif
#include <sys/wait.h>

struct uloop_fd_event {
	struct uloop_fd *fd; // record fd information
	unsigned int events; // event occurs on this fd
};

struct uloop_fd_stack {
	struct uloop_fd_stack *next;
	struct uloop_fd *fd;
	unsigned int events;
};

static struct uloop_fd_stack *fd_stack = NULL;

#define ULOOP_MAX_EVENTS 10

static struct list_head timeouts = LIST_HEAD_INIT(timeouts);
static struct list_head processes = LIST_HEAD_INIT(processes);

static int poll_fd = -1;
bool uloop_cancelled = false;
bool uloop_handle_sigchld = true;
static bool do_sigchld = false;

static struct uloop_fd_event cur_fds[ULOOP_MAX_EVENTS]; // list of current fired fd
static int cur_fd;   // current fired fd
static int cur_nfds; // number of current fired fds


#ifdef USE_KQUEUE

int uloop_init(void)
{
	struct timespec timeout = { 0, 0 };
	struct kevent ev = {};

	if (poll_fd >= 0)
		return 0;

	poll_fd = kqueue();
	if (poll_fd < 0)
		return -1;

	EV_SET(&ev, SIGCHLD, EVFILT_SIGNAL, EV_ADD, 0, 0, 0);
	kevent(poll_fd, &ev, 1, NULL, 0, &timeout);

	return 0;
}


static uint16_t get_flags(unsigned int flags, unsigned int mask)
{
	uint16_t kflags = 0;

	if (!(flags & mask))
		return EV_DELETE;

	kflags = EV_ADD;
	if (flags & ULOOP_EDGE_TRIGGER)
		kflags |= EV_CLEAR;

	return kflags;
}

static struct kevent events[ULOOP_MAX_EVENTS];

static int register_kevent(struct uloop_fd *fd, unsigned int flags)
{
	struct timespec timeout = { 0, 0 };
	struct kevent ev[2];
	int nev = 0;
	unsigned int fl = 0;
	unsigned int changed;
	uint16_t kflags;

	if (flags & ULOOP_EDGE_DEFER)
		flags &= ~ULOOP_EDGE_TRIGGER;

	changed = flags ^ fd->flags;
	if (changed & ULOOP_EDGE_TRIGGER)
		changed |= flags;

	if (changed & ULOOP_READ) {
		kflags = get_flags(flags, ULOOP_READ);
		EV_SET(&ev[nev++], fd->fd, EVFILT_READ, kflags, 0, 0, fd);
	}

	if (changed & ULOOP_WRITE) {
		kflags = get_flags(flags, ULOOP_WRITE);
		EV_SET(&ev[nev++], fd->fd, EVFILT_WRITE, kflags, 0, 0, fd);
	}

	if (!flags)
		fl |= EV_DELETE;

	fd->flags = flags;
	if (kevent(poll_fd, ev, nev, NULL, fl, &timeout) == -1)
		return -1;

	return 0;
}

static int register_poll(struct uloop_fd *fd, unsigned int flags)
{
	if (flags & ULOOP_EDGE_TRIGGER)
		flags |= ULOOP_EDGE_DEFER;
	else
		flags &= ~ULOOP_EDGE_DEFER;

	return register_kevent(fd, flags);
}

static int __uloop_fd_delete(struct uloop_fd *fd)
{
	return register_poll(fd, 0);
}

static int uloop_fetch_events(int timeout)
{
	struct timespec ts;
	int nfds, n;

	if (timeout >= 0) {
		ts.tv_sec = timeout / 1000;
		ts.tv_nsec = (timeout % 1000) * 1000000;
	}

	nfds = kevent(poll_fd, NULL, 0, events, ARRAY_SIZE(events), timeout >= 0 ? &ts : NULL);
	for (n = 0; n < nfds; n++) {
		struct uloop_fd_event *cur = &cur_fds[n];
		struct uloop_fd *u = events[n].udata;
		unsigned int ev = 0;

		cur->fd = u;
		if (!u)
			continue;

		if (events[n].flags & EV_ERROR) {
			u->error = true;
			if (!(u->flags & ULOOP_ERROR_CB))
				uloop_fd_delete(u);
		}

		if(events[n].filter == EVFILT_READ)
			ev |= ULOOP_READ;
		else if (events[n].filter == EVFILT_WRITE)
			ev |= ULOOP_WRITE;

		if (events[n].flags & EV_EOF)
			u->eof = true;
		else if (!ev)
			cur->fd = NULL;

		cur->events = ev;
		if (u->flags & ULOOP_EDGE_DEFER) {
			u->flags &= ~ULOOP_EDGE_DEFER;
			u->flags |= ULOOP_EDGE_TRIGGER;
			register_kevent(u, u->flags);
		}
	}
	return nfds;
}

#endif

#ifdef USE_EPOLL

/**
 * FIXME: uClibc < 0.9.30.3 does not define EPOLLRDHUP for Linux >= 2.6.17
 */
#ifndef EPOLLRDHUP
#define EPOLLRDHUP 0x2000
#endif

int uloop_init(void)
{
	if (poll_fd >= 0)
		return 0;

    // #include <sys/epoll.h> 
    // int epoll_create(int size);
    // Description: creates a new epoll instance.
    // Input: the size argument has been ignored since Linux 2.6.8,
    //        kernel dynamically sizes the required data structures,
    //        but the value must be greater than zero.
    // Return: On success, return a nonnegative file descriptor.
    //         On error, return -1, and errno is set to indicate the error.
    poll_fd = epoll_create(32);
	if (poll_fd < 0)
		return -1;

    // #include <unistd.h>
    // #include <fcntl.h>
    // int fcntl(int fd, int cmd, ... /* arg */ );
    // Description: performs one of the operations determined by cmd described below
    //              on the open file descriptor fd
    // Input:       F_DUPFD: Duplicate the file descriptor fd using the lowest-numbered
    //                       available file descriptor greater than or equal to arg.
    //              F_DUPFD_CLOEXEC: As for F_DUPFD, but additionally set the close-on-exec flag
    //                       for the duplicate file descriptor.(since Linux 2.6.24)
    //              F_GETFD: Return the file descriptor flags; arg is ignored.
    //                       Currently, only FD_CLOEXEC flag is defined.
    //                       If the FD_CLOEXEC bit is set, fd will automatically be closed during a successful execve.
    //              F_SETFD: Set the file descriptor flags to the value specified by arg.
    //              F_GETFL: Return the file access mode and the file status flags; arg is ignored.
    //              F_SETFL: Set the file status flags to the value specified by arg.
    //                       this command can change only the O_APPEND, O_ASYNC, O_DIRECT, O_NOATIME, and O_NONBLOCK flags.
    //              F_SETLK: Acquire a lock or release a lock. arg pointers to a structure has at least the following fields:
    //                       struct flock {
    //                           ...
    //                           short l_type;    /* Type of lock: F_RDLCK,
    //                                               F_WRLCK, F_UNLCK */
    //                           short l_whence;  /* How to interpret l_start:
    //                                               SEEK_SET, SEEK_CUR, SEEK_END */
    //                           off_t l_start;   /* Starting offset for lock */
    //                           off_t l_len;     /* Number of bytes to lock */
    //                           pid_t l_pid;     /* PID of process blocking our lock
    //                                               (set by F_GETLK and F_OFD_GETLK) */
    //                           ...
    //                       };
    //                       If a conflicting lock is held by another process, this call returns -1.
    //              F_SETLKW: As for F_SETLK, but if a conflicting lock is held on the file, then wait for that lock to be released.
    //              F_GETLK: arg describes a lock we would like to place on the file.
    //                       If the lock could be placed, returns F_UNLCK in the l_type field;
    //                       If this lock is being placed, returns details about one of those locks
    //              F_GETOWN: Return the process ID or process group currently receiving SIGIO and SIGURG signals
    //                       for events on file descriptor fd.
    //              F_SETOWN: Set the process ID or process group ID that will receive SIGIO and SIGURG signals
    //                       for events on the file descriptor fd.
    //              F_GETSIG: Return the signal sent when input or output becomes possible.
    //              F_SETSIG: Set the signal sent when input or output becomes possible to the value given in arg.
	fcntl(poll_fd, F_SETFD, fcntl(poll_fd, F_GETFD) | FD_CLOEXEC);

    return 0;
}

static int register_poll(struct uloop_fd *fd, unsigned int flags)
{
	struct epoll_event ev;
	int op = fd->registered ? EPOLL_CTL_MOD : EPOLL_CTL_ADD; // if fd has been registered, modify the event, or add a new one 

	memset(&ev, 0, sizeof(struct epoll_event));

	if (flags & ULOOP_READ)
		ev.events |= EPOLLIN | EPOLLRDHUP;

	if (flags & ULOOP_WRITE)
		ev.events |= EPOLLOUT;

	if (flags & ULOOP_EDGE_TRIGGER)
		ev.events |= EPOLLET;

	ev.data.fd = fd->fd;
	ev.data.ptr = fd; // user defined data pointer
	fd->flags = flags;

    // #include <sys/epoll.h>
    // int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
    // Description: Performs control operations op for the target file descriptor fd on the epoll
    //              instance referred to by epoll file descriptor epfd.
    //              Valid values for the op argument are:
    //                  EPOLL_CTL_ADD - Register fd on epfd and associate event with fd.
    //                  EPOLL_CTL_MOD - Change the event associated with fd;
    //                  EPOLL_CTL_DEL - Remove fd from epfd, argument event is ignored and can be NULL.
    //              The event argument point to a epoll_event structure:
    //                  typedef union epoll_data {
    //                      void        *ptr;
    //                      int          fd;
    //                      uint32_t     u32;
    //                      uint64_t     u64;
    //                  } epoll_data_t;
    //                  struct epoll_event {
    //                      uint32_t     events;      /* Epoll events */
    //                      epoll_data_t data;        /* User data variable */
    //                  };
    //              The events member bitwise-OR with following types:
    //                  EPOLLIN  - available for read()
    //                  EPOLLOUT - available for write()
    //                  EPOLLRDHUP - Stream socket peer closed connection, or shut down writing
    //                               half of connection. Can be used to detect peer shutdown when 
    //                               using Edge Triggered monitoring.
    //                  EPOLLERR - Error condition happened on the associated file descriptor.
    //                             Also reported for write end of a pipe when read end has been closed.
    //                             epoll_wait() will always report for this event.
    //                  EPOLLHUP - Hang up happened on the associated file descriptor.
    //                             epoll_wait() will always wait for this event. When reading from a channel
    //                             (a pipe or a stream socket), this event indicates the peer has closed its
    //                             end of the channel, and read() will return 0(EOF) only after all outstanding
    //                             data in the channel has been consumed.
    //                  EPOLLET  - The default behavior for epoll is Level Triggered(EPOLLLT), this event sets the 
    //                             Edge Triggered behavior for the associated file descriptor.
    //                             Level Triggered: epoll_wait() will always return fd as a ready file descriptor,
    //                                              if there is available data in buffer. LT is simply a faster poll.
    //                             Edge Triggered:  epoll_wait() will return fd as a ready file descriptor for once,
    //                                              if there is available data in buffer, which will cause epoll_wait() block.
    //                             The suggested way to use epoll as an edge-triggered (EPOLLET) interface is as follows:
    //                                 i   with nonblocking file descriptors; and
    //                                 ii  by waiting for an event only after read() or write() return EAGAIN.
    //                  EPOLLONESHOT - Sets the one-shot behavior for the associated file descriptor.
    //                                 Once an event pulled out by epoll_wait(), no more events will be reported,
    //                                 unless epoll_ctl() with EPOLL_CTL_MOD is called to rearm the
    //                                 file descriptor with a new event mask.
	return epoll_ctl(poll_fd, op, fd->fd, &ev);
}

static struct epoll_event events[ULOOP_MAX_EVENTS];

static int __uloop_fd_delete(struct uloop_fd *sock)
{
	sock->flags = 0;
	return epoll_ctl(poll_fd, EPOLL_CTL_DEL, sock->fd, 0);
}

static int uloop_fetch_events(int timeout)
{
	int n, nfds;

    // #include <sys/epoll.h>
    // int epoll_wait(int epfd, struct epoll_event *events, int maxevents, int timeout);
    // Desc: waits for events on the epoll instance epfd.
    //       events contains the events available for the caller.
    //       Up to maxevents(greater than zero and less then the size in epoll_create()) are returned by epoll_wait().
    //       timeout, measured against the CLOCK_MONOTONIC clock, specifies the number of milliseconds that epoll_wait() will block.
    //           timeout = -1, block indefinitely,
    //           timeout =  0, return immediately.
    //       The call will block until either:
    //           *  a file descriptor delivers an event;
    //           *  the call is interrupted by a signal handler; or
    //           *  the timeout expires.
    //       The struct epoll_event is defined as:
    //           typedef union epoll_data {
    //               void    *ptr;
    //               int      fd;
    //               uint32_t u32;
    //               uint64_t u64;
    //           } epoll_data_t;
    //           struct epoll_event {
    //               uint32_t     events;    /* Epoll events */
    //               epoll_data_t data;      /* User data variable */
    //           };
    //       The data field of returned events is same as the most recent epoll_ctl() set on the fd.
    //       On success, epoll_wait() returns the number of fds ready for the requested I/O,
    //       or zero if no fd became ready during the requested timeout milliseconds.
    //       On failed, return -1.
	nfds = epoll_wait(poll_fd, events, ARRAY_SIZE(events), timeout);
	for (n = 0; n < nfds; ++n) {
		struct uloop_fd_event *cur = &cur_fds[n];
		struct uloop_fd *u = events[n].data.ptr;
		unsigned int ev = 0;

        // record all fired fd to array cur_fds
        // cur_fds[n].fd --> events[n].data.ptr
		cur->fd = u;
		if (!u)
			continue;

        // epoll_wait() always reports EPOLLERR and EPOLLHUP events
        // peer has closed its end
		if (events[n].events & (EPOLLERR|EPOLLHUP)) {
			u->error = true;
			if (!(u->flags & ULOOP_ERROR_CB))
				uloop_fd_delete(u);
		}

        // continue, if not an expected event
		if(!(events[n].events & (EPOLLRDHUP|EPOLLIN|EPOLLOUT|EPOLLERR|EPOLLHUP))) {
			cur->fd = NULL;
			continue;
		}

        // Stream socket peer closed its write end of the connection
		if(events[n].events & EPOLLRDHUP)
			u->eof = true;

        // readable
		if(events[n].events & EPOLLIN)
			ev |= ULOOP_READ;

        // writable
		if(events[n].events & EPOLLOUT)
			ev |= ULOOP_WRITE;

		cur->events = ev;
	}

	return nfds;
}

#endif

static bool uloop_fd_stack_event(struct uloop_fd *fd, int events)
{
	struct uloop_fd_stack *cur;

	/*
	 * Do not buffer events for level-triggered fds, they will keep firing.
	 * Caller needs to take care of recursion issues.
	 */
	if (!(fd->flags & ULOOP_EDGE_TRIGGER))
        // return for level-triggered fds
		return false;

    // for edge-triggered fds, 
    //     if arg events < 0, delete this fd, or
    //     add ULOOP_EVENT_BUFFERED mask
    for (cur = fd_stack; cur; cur = cur->next) {
		if (cur->fd != fd)
			continue;

		if (events < 0)
			cur->fd = NULL;
		else
			cur->events |= events | ULOOP_EVENT_BUFFERED;

		return true;
	}

	return false;
}

static void uloop_run_events(int timeout)
{
	struct uloop_fd_event *cur;
	struct uloop_fd *fd;

    // currently, no fired fd has not been delt.
    // call epoll_wait() until timeout.
	if (!cur_nfds) {

        // no fired fd
		cur_fd = 0;

        // call epoll_wait() to monitor fds for the first time
		cur_nfds = uloop_fetch_events(timeout);
		if (cur_nfds < 0)
			cur_nfds = 0;
	}

	while (cur_nfds > 0) {
		struct uloop_fd_stack stack_cur;
		unsigned int events;

		cur = &cur_fds[cur_fd++];
		cur_nfds--;

        // fd     --> cur_fds[cur_fd++].fd     --> events[cur_fd++].data.ptr
		fd = cur->fd;
		events = cur->events;
		if (!fd)
			continue;

		if (!fd->cb)
			continue;

        // if fd has been put in buffer stack
        // for edge-triggered monitoring, add ULOOP_EVENT_BUFFERED flag to event
		if (uloop_fd_stack_event(fd, cur->events))
			continue;

        // stack in buffer
		stack_cur.next = fd_stack;
		stack_cur.fd = fd;
		fd_stack = &stack_cur;
		do {
			stack_cur.events = 0;
			fd->cb(fd, events);
			events = stack_cur.events & ULOOP_EVENT_MASK;
		} while (stack_cur.fd && events);
		fd_stack = stack_cur.next;

		return;
	}
}

int uloop_fd_add(struct uloop_fd *sock, unsigned int flags)
{
	unsigned int fl;
	int ret;

    // if not a readable or writable event, then delete from registed event table
    if (!(flags & (ULOOP_READ | ULOOP_WRITE)))
		return uloop_fd_delete(sock);

    // If not set ULOOP_BLOCKING apperently, change the file status flag to nonblocking
    if (!sock->registered && !(flags & ULOOP_BLOCKING)) {
		fl = fcntl(sock->fd, F_GETFL, 0);
		fl |= O_NONBLOCK;
		fcntl(sock->fd, F_SETFL, fl);
	}

    // TODO: if flags & ULOOP_BLOCKING != 0, make sure epoll_wait() can not block in edge triggered mode

	ret = register_poll(sock, flags);
	if (ret < 0)
		goto out;

	sock->registered = true;
	sock->eof = false;

out:
	return ret;
}

int uloop_fd_delete(struct uloop_fd *fd)
{
	int i;

    // find the file descriptor reffered by fd from current fired fds array.
    // and remove it.
	for (i = 0; i < cur_nfds; i++) {
		if (cur_fds[cur_fd + i].fd != fd)
			continue;

		cur_fds[cur_fd + i].fd = NULL;
	}

	if (!fd->registered)
		return 0;

	fd->registered = false;

    // clear the buffer stack assigned to fd
	uloop_fd_stack_event(fd, -1);

    // delete(unregister) from epoll instance
    // call epoll_ctl() with EPOLL_CTL_DEL
	return __uloop_fd_delete(fd);
}

static int tv_diff(struct timeval *t1, struct timeval *t2)
{
	return
		(t1->tv_sec - t2->tv_sec) * 1000 +
		(t1->tv_usec - t2->tv_usec) / 1000;
}

int uloop_timeout_add(struct uloop_timeout *timeout)
{
	struct uloop_timeout *tmp;
	struct list_head *h = &timeouts;

	if (timeout->pending)
		return -1;

	list_for_each_entry(tmp, &timeouts, list) {
		if (tv_diff(&tmp->time, &timeout->time) > 0) {
			h = &tmp->list;
			break;
		}
	}

	list_add_tail(&timeout->list, h);
	timeout->pending = true;

	return 0;
}

static void uloop_gettime(struct timeval *tv)
{
	struct timespec ts;

    // #include <time.h>
    // int clock_gettime(clockid_t clk_id, struct timespec *tp);
    // Desc: Retrieve the time of the specified clock clk_id.
    //       clk_id - Identifies to a particular clock:
    //            CLOCK_REALTIME - Represents seconds and nanoseconds since 1970-01-01 00:00:00 UTC.
    //                             Affected by system time, adjtime() or NTP.
    //            CLOCK_REALTIME_COARSE - Linux-specific, a faster but less precise version of CLOCK_REALTIME.
    //            CLOCK_MONOTONIC - Represents monotonic time since some unspecified starting point.
    //                              Affected by adjtime() or NTP. Stop increasing when system suspends.
    //            CLOCK_MONOTONIC_COARSE - Linux-specific. A faster but less precise version of CLOCK_MONOTONIC.
    //            CLOCK_MONOTONIC_RAW - Linux-specific. Similar to CLOCK_MONOTONIC, but not subject to NTP or adjtime().
    //            CLOCK_BOOTTIME - Linux-specific. Identical to CLOCK_MONOTONIC, continuous time from system up including suspend.
    //            CLOCK_PROCESS_CPUTIME_ID - Per-process CPU-time clock, measures CPU time consumed by all threads in the process.
    //            CLOCK_THREAD_CPUTIME_ID - Thread-specific CPU-time clock.
    //       tp - A pointer to timespec, 
    //                struct timespec {
    //                    time_t   tv_sec;        /* seconds */
    //                    long     tv_nsec;       /* nanoseconds */
    //                };
    clock_gettime(CLOCK_MONOTONIC, &ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = ts.tv_nsec / 1000;
}

int uloop_timeout_set(struct uloop_timeout *timeout, int msecs)
{
	struct timeval *time = &timeout->time;

	if (timeout->pending)
		uloop_timeout_cancel(timeout);

	uloop_gettime(&timeout->time);

	time->tv_sec += msecs / 1000;
	time->tv_usec += (msecs % 1000) * 1000;

	if (time->tv_usec > 1000000) {
		time->tv_sec++;
		time->tv_usec %= 1000000;
	}

	return uloop_timeout_add(timeout);
}

int uloop_timeout_cancel(struct uloop_timeout *timeout)
{
	if (!timeout->pending)
		return -1;

	list_del(&timeout->list);
	timeout->pending = false;

	return 0;
}

int uloop_timeout_remaining(struct uloop_timeout *timeout)
{
	struct timeval now;

	if (!timeout->pending)
		return -1;

	uloop_gettime(&now);

	return tv_diff(&timeout->time, &now);
}

int uloop_process_add(struct uloop_process *p)
{
	struct uloop_process *tmp;
	struct list_head *h = &processes;

	if (p->pending)
		return -1;

	list_for_each_entry(tmp, &processes, list) {
		if (tmp->pid > p->pid) {
			h = &tmp->list;
			break;
		}
	}

	list_add_tail(&p->list, h);
	p->pending = true;

	return 0;
}

int uloop_process_delete(struct uloop_process *p)
{
	if (!p->pending)
		return -1;

	list_del(&p->list);
	p->pending = false;

	return 0;
}

static void uloop_handle_processes(void)
{
	struct uloop_process *p, *tmp;
	pid_t pid;
	int ret;

	do_sigchld = false;

	while (1) {

        // #include <sys/types.h>
        // #include <sys/wait.h>
        // pid_t waitpid(pid_t pid, int *wstatus, int options);
        // Desc: Wait for state changes in a child of the calling process, and obtain information
        //       about the child whose state has changed. A state change is considered to be:
        //           the child terminated;
        //           the child was stopped by a signal;
        //           the child was resumed by a signal.
        //       This call blocks until either child pid changes state or a signal handler interrupts the call.
        //       pid can be:
        //           < -1    meaning wait for any child process whose process group ID is equal to the absolute value of pid.
        //             -1    meaning wait for any child process.
        //              0    meaning wait for any child process whose process group ID is equal to that of the calling process.
        //           >  0    meaning wait for the child whose process ID is equal to the value of pid.
        //       options is an OR of zero or more of the following constants:
        //           WNOHANG     return immediately if no child has exited.
        //           WUNTRACED   also return if a child has stopped(but not traced via ptrace())
        //           WCONTINUED  also return if a stopped child has been resumed by delivery of SIGCONT.
        //       wstatus stores status information. This integer can be inspected with the following macros:
        //           WIFEXITED      returns true if the child terminated normally(by calling exit())
        //           WEXITSTATUS    returns the exit status of the child, if WIFEXITED returned true.
        //           WIFSIGNALED    returns true if the child process was terminated by a signal.
        //           WTERMSIG       returns signum caused the child process to terminate, if WIFSIGNALED returned true.
        //           WCOREDUMP      returns true if the child produced a core dump, if WIFSIGNALED returned true.
        //           WIFSTOPPED     returns true if the child process was stopped by a signal
        //           WSTOPSIG       returns signum caused the child to stop, if WIFSTOPPED returned true.
        //           WIFCONTINUED   returns true if the child process was resumed by SIGCONT.
        //       On success, returns the process ID of the child whose state has changed; if WNOHANG was set
        //       and one or more child(ren) specified by pid exist, but have not yet changed state, then 0 is returned.
        //       On error, -1 is returned.
		pid = waitpid(-1, &ret, WNOHANG);
		if (pid <= 0)
			return;

		list_for_each_entry_safe(p, tmp, &processes, list) {
			if (p->pid < pid)
				continue;

			if (p->pid > pid)
				break;

			uloop_process_delete(p);
			p->cb(p, ret);
		}
	}

}

static void uloop_handle_sigint(int signo)
{
	uloop_cancelled = true;
}

static void uloop_sigchld(int signo)
{
	do_sigchld = true;
}

static void uloop_setup_signals(bool add)
{
	static struct sigaction old_sigint, old_sigchld;
	struct sigaction s;

	memset(&s, 0, sizeof(struct sigaction));

	if (add) {
        // cancel the main loop when received SIGINT
		s.sa_handler = uloop_handle_sigint;
		s.sa_flags = 0;
	} else {
		s = old_sigint;
	}

    // #include <signal.h>
    // int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact);
    // Description: change the action taken by a process on receipt of a specific signal.
    //              signum - specifies the signal and can be any valid signal except
    //                       SIGKILL and SIGSTOP.
    //                           Signal     Value     Action   Comment
    //                           SIGHUP        1       Term    Hangup detected on controlling terminal
    //                                                         or death of controlling process
    //                           SIGINT        2       Term    Interrupt from keyboard, Ctrl + C
    //                           SIGQUIT       3       Core    Quit from keyboard, Ctrl + \
    //                           SIGILL        4       Core    Illegal Instruction
    //                           SIGABRT       6       Core    Abort signal from abort()
    //                           SIGFPE        8       Core    Floating-point exception
    //                           SIGKILL       9       Term    Kill signal
    //                           SIGSEGV      11       Core    Invalid memory reference
    //                           SIGPIPE      13       Term    Broken pipe: write to pipe with no readers
    //                           SIGALRM      14       Term    Timer signal from alarm()
    //                           SIGTERM      15       Term    Termination signal
    //                           SIGUSR1   30,10,16    Term    User-defined signal 1
    //                           SIGUSR2   31,12,17    Term    User-defined signal 2
    //                           SIGCHLD   20,17,18    Ign     Child stopped or terminated
    //                           SIGCONT   19,18,25    Cont    Continue if stopped
    //                           SIGSTOP   17,19,23    Stop    Stop process
    //                           SIGTSTP   18,20,24    Stop    Stop typed at terminal
    //                           SIGTTIN   21,21,26    Stop    Terminal input for background process
    //                           SIGTTOU   22,22,27    Stop    Terminal output for background process
    //                       SIGKILL and SIGSTOP cannot be caught, blocked, or ignored.
    //              act    - If not NULL, new action from act for signal signum is installed.
    //              oldact - If not NULL, save the previous action.
    //              The sigaction structure is defined as below:
    //                  struct sigaction {
    //                      void     (*sa_handler)(int);
    //                      void     (*sa_sigaction)(int, siginfo_t *, void *);
    //                      sigset_t   sa_mask;
    //                      int        sa_flags;
    //                      void     (*sa_restorer)(void);
    //                  };
    //                  NOTE: Do not assign to both sa_handler and sa_sigaction.
    //              sa_handler specifies the action to be associated with signum and may be
    //                  SIG_DFL for the default action, or
    //                  SIG_IGN to ignore this signal, or
    //                  a pointer to a signal handling function.
    //              sa_mask specifies a mask of signals which should be blocked.
    //              sa_flags is used to modify the behavior of the signal, which is a set of
    //              flags formed by the bitwise OR of zero or more of the following:
    //                  SA_NOCLDSTOP - If signum is SIGCHLD, do not receive notification when
    //                                 child processes stop.
    //                  SA_NOCLDWAIT - If signum is SIGCHLD, do not transform children into zombies
    //                                 when they terminate.
    //                  SA_NODEFER   - Do not prevent the signal from being received from within its
    //                                 own signal handler.
    //                  SA_ONSTACK   - Call the signal handler on an alternate signal stack provided
    //                                 by sigaltstack(). If no available stack, use the default stack.
    //                  SA_RESETHAND - Restore the signal action to the default upon entry to the signal handler
    //                  SA_RESTART   - Provide behavior by making certain system calls restartable.
    //                  SA_RESTORER  - Not for application use.
    //                  SA_SIGINFO   - Set sa_sigaction with 3 arguments, instead of sa_handler:
    //                                     sig  - The number of the signal.
    //                                     info - A pointer to a siginfo_t containing further information about the signal
    //                                                siginfo_t {
    //                                                    int      si_signo;     /* Signal number */
    //                                                    int      si_errno;     /* An errno value */
    //                                                    int      si_code;      /* Signal code */
    //                                                    int      si_trapno;    /* Trap number that caused
    //                                                                              hardware-generated signal
    //                                                                              (unused on most architectures) */
    //                                                    pid_t    si_pid;       /* Sending process ID */
    //                                                    uid_t    si_uid;       /* Real user ID of sending process */
    //                                                    int      si_status;    /* Exit value or signal */
    //                                                    clock_t  si_utime;     /* User time consumed */
    //                                                    clock_t  si_stime;     /* System time consumed */
    //                                                    sigval_t si_value;     /* Signal value */
    //                                                    int      si_int;       /* POSIX.1b signal */
    //                                                    void    *si_ptr;       /* POSIX.1b signal */
    //                                                    int      si_overrun;   /* Timer overrun count;
    //                                                                              POSIX.1b timers */
    //                                                    int      si_timerid;   /* Timer ID; POSIX.1b timers */
    //                                                    void    *si_addr;      /* Memory location which caused fault */
    //                                                    long     si_band;      /* Band event (was int in
    //                                                                              glibc 2.3.2 and earlier) */
    //                                                    int      si_fd;        /* File descriptor */
    //                                                    short    si_addr_lsb;  /* Least significant bit of address
    //                                                                              (since Linux 2.6.32) */
    //                                                    void    *si_lower;     /* Lower bound when address violation
    //                                                                              occurred (since Linux 3.19) */
    //                                                    void    *si_upper;     /* Upper bound when address violation
    //                                                                              occurred (since Linux 3.19) */
    //                                                    int      si_pkey;      /* Protection key on PTE that caused
    //                                                                              fault (since Linux 4.6) */
    //                                                    void    *si_call_addr; /* Address of system call instruction
    //                                                                              (since Linux 3.5) */
    //                                                    int      si_syscall;   /* Number of attempted system call
    //                                                                              (since Linux 3.5) */
    //                                                    unsigned int si_arch;  /* Architecture of attempted system call
    //                                                                              (since Linux 3.5) */
    //                                                }
    //                                            ucontext - A pointer to ucontext_t, cast to void *, contains signal context
    //                                                       information that was saved on the user-space stack by the kernel.
    //
    sigaction(SIGINT, &s, &old_sigint);

	if (!uloop_handle_sigchld)
		return;

	if (add)
        // 
		s.sa_handler = uloop_sigchld;
	else
		s = old_sigchld;

	sigaction(SIGCHLD, &s, &old_sigchld);
}

static int uloop_get_next_timeout(struct timeval *tv)
{
	struct uloop_timeout *timeout;
	int diff;

	if (list_empty(&timeouts))
        // make epoll_wait() block indefinitely
		return -1;

    // fetch the first entry of timeout list
	timeout = list_first_entry(&timeouts, struct uloop_timeout, list);

    // calculate the diff value between first timeout entry and current time
	diff = tv_diff(&timeout->time, tv);
	if (diff < 0)
		return 0;

	return diff; // unit: second
}

static void uloop_process_timeouts(struct timeval *tv)
{
	struct uloop_timeout *t;

	while (!list_empty(&timeouts)) {
		t = list_first_entry(&timeouts, struct uloop_timeout, list);

		if (tv_diff(&t->time, tv) > 0)
			break;

		uloop_timeout_cancel(t);
		if (t->cb)
			t->cb(t);
	}
}

static void uloop_clear_timeouts(void)
{
	struct uloop_timeout *t, *tmp;

	list_for_each_entry_safe(t, tmp, &timeouts, list)
		uloop_timeout_cancel(t);
}

static void uloop_clear_processes(void)
{
	struct uloop_process *p, *tmp;

	list_for_each_entry_safe(p, tmp, &processes, list)
		uloop_process_delete(p);
}

void uloop_run(void)
{
	static int recursive_calls = 0;

    // #include <sys/time.h>
    // struct timeval {
    //            time_t      tv_sec;     /* seconds */
    //            suseconds_t tv_usec;    /* microseconds */
    //        };
	struct timeval tv;

	/*
	 * Handlers are only updated for the first call to uloop_run() (and restored
	 * when this call is done).
	 */
	if (!recursive_calls++)
		uloop_setup_signals(true);

	while(!uloop_cancelled)
	{
	    // Get current time
		uloop_gettime(&tv);

        // process timeout event
		uloop_process_timeouts(&tv);
		if (uloop_cancelled)
			break;

		if (do_sigchld)

            // deal with child process terminating or stoping event
			uloop_handle_processes();

        // Get current time again, because do_sigchld calling wait_pid() may be blocked
		uloop_gettime(&tv);

        // from timeout list, fetch the next fire time in timeout list
        //     -1 means block indefinitely until some events occurs, or 
        //     wait until next fire time
		uloop_run_events(uloop_get_next_timeout(&tv));
	}

	if (!--recursive_calls)
		uloop_setup_signals(false);
}

void uloop_done(void)
{
	if (poll_fd < 0)
		return;

	close(poll_fd);
	poll_fd = -1;

	uloop_clear_timeouts();
	uloop_clear_processes();
}

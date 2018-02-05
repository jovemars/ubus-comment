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
#ifndef USOCK_H_
#define USOCK_H_

#define USOCK_TCP 0                   // SOCK_STREAM
#define USOCK_UDP 1                   // SOCK_DGRAM

#define USOCK_SERVER		0x0100    // ubus server demo
#define USOCK_NOCLOEXEC	    0x0200    // !SOCK_CLOEXEC, not set the FD_CLOEXEC close-on-exec flag
#define USOCK_NONBLOCK		0x0400    // SOCK_NONBLOCK, set the O_NONBLOCK file status flag
#define USOCK_NUMERIC		0x0800    // set AI_NUMERICHOST flag in struct addrinfo hints.ai_flags
#define USOCK_IPV6ONLY		0x2000    // AF_INET6
#define USOCK_IPV4ONLY		0x4000    // AF_INET
#define USOCK_UNIX			0x8000    // AF_UNIX

int usock(int type, const char *host, const char *service);

#endif /* USOCK_H_ */

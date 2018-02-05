/*
 * Copyright (C) 2011 Felix Fietkau <nbd@openwrt.org>
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <libubox/avl-cmp.h>

#include "ubusmsg.h"
#include "ubusd_id.h"

static int random_fd = -1;

static int ubus_cmp_id(const void *k1, const void *k2, void *ptr)
{
	const uint32_t *id1 = k1, *id2 = k2;

	if (*id1 < *id2)
		return -1;
	else
		return *id1 > *id2;
}

void ubus_init_string_tree(struct avl_tree *tree, bool dup)
{
	avl_init(tree, avl_strcmp, dup, NULL);
}

void ubus_init_id_tree(struct avl_tree *tree)
{
	if (random_fd < 0) {

        // #include <linux/random.h>
        // int ioctl(fd, RNDrequest, param);
        // Desc:
        //    The character special files /dev/random and /dev/urandom provide an interface
        //    to the kernel's random number generator.
        //    The file /dev/random has major device number 1 and minor device number 8.
        //    The file /dev/urandom has major device number 1 and minor device number 9.
        //    When read, the /dev/urandom device returns random bytes using a pseudorandom
        //    number generator seeded from the entropy pool.
        // Note:
        //    /dev/urandom is preferred and sufficient in all use cases, with the exception of
        //    applications which require randomness during early boot time;for these applications,
        //    /dev/urandom is not trusted because it will return datas before entropy pool initialized.
        //    In that case, /dev/random or getrandom() must be used instead, because it will block
        //    until the entropy pool is initialized.
        // /proc interfaces
        //    The files /proc/sys/kernel/random provide additional information about the /dev/random
        //    entropy_avail(read-only)
        //        gives the available entropy, in bits, from 0 to 4096.
        //    poolsize(read-only)
        //        gives the size of the entropy pool in bits, the the value is 4096.
        //    read_wakeup_threshold
        //        contains the number of bits of entropy required for waking up processes that
        //        sleep waiting for entropy from /dev/random. default is 64.
        //    write_wakeup_threshold
        //        contains the number of bits of entropy below which we wake up processes that
        //        do a select() or poll() for write access to /dev/random.
        //    uuid and boot_id(read-only)
        //        The former is generated afresh for each read, the latter was generated once.
        // ioctl(2) interface
        //    The following ioctl() requests are defined on file descriptors connected to either
        //    /dev/random or /dev/urandom. The CAP_SYS_ADMIN capability is required for all
        //    requests except RNDGETENTCNT.
        //        RNDGETENTCNT   - Retrieve the entropy count of the input pool, same as 
        //                         /proc/sys/kernel/random/entropy_avail
        //        RNDADDTOENTCNT - Increment or decrement the entropy count of the input pool
        //        RNDADDENTROPY  - Add some additional entropy to the input pool, incrementing
        //                         the entropy count. write() to /dev/random or /dev/urandom only
        //                         adds some data but does not increment the entropy count.
        //                         The following structure is used:
        //                             struct rand_pool_info {
        //                                 int    entropy_count; // value add to or sub from count
        //                                 int    buf_size; // buffer size
        //                                 __u32  buf[0]; // buffer added to the pool
        //                                                // buf是数组名，没有元素，不占用结构体空间
        //                                                // 该数组的实际地址紧跟结构体之后，
        //                                                // 大小由buf_size决定，以实现C语言的数组扩展
        //                             };
        //        RNDZAPENTCNT, RNDCLEARPOOL - Zero the entropy count of all pools and add some
        //                         system data (such as wall clock) to the pools.
		random_fd = open("/dev/urandom", O_RDONLY);
		if (random_fd < 0) {
			perror("open");
			exit(1);
		}
	}

    // initialize an AVL tree to maintenance client id
    // AVL tree is height-balanced binary search tree:
    //     - A height-balanced binary tree is at any given node, the difference in the heights of
    //       its two subtrees is at most 1.
    //     - A binary search tree is a binary tree T such that:
    //         - Key of the left child <= key of parent node
    //         - Key of the right child >= key of parent node
    // AVL tree takes O(log n) time in both the average and worst cases. 
    // For lookup-intensive applications, AVL trees are faster than red–black trees because they are more strictly balanced.
    //
    // ** NOTE **
    // 1. 一个节点如果是其父节点的左子节点，则所有位于其左右子树上的节点值均小于其父节点值
    // 2. 一个节点如果是其父节点的右子节点，则所有位于其左右子树上的节点值均大于其父节点值
	avl_init(tree, ubus_cmp_id, false, NULL);
}

/**
 * allocated an id for new client, and insert this id to avl tree and linked list
 */
bool ubus_alloc_id(struct avl_tree *tree, struct ubus_id *id, uint32_t val)
{
    // point the key to id
	id->avl.key = &id->id;

    // set id to a fixed value
	if (val) {
		id->id = val;
		return avl_insert(tree, &id->avl) == 0;
	}

    // generate a random id seeding /dev/urandom
	do {
        // read 32 bits(an unsigned int number) from /dev/urandom
        // untill this number less than 1024
		if (read(random_fd, &id->id, sizeof(id->id)) != sizeof(id->id))
			return false;

		if (id->id < UBUS_SYSTEM_OBJECT_MAX)
			continue;
	} while (avl_insert(tree, &id->avl) != 0);

	return true;
}


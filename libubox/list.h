/*-
 * Copyright (c) 2011 Felix Fietkau <nbd@openwrt.org>
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef _LINUX_LIST_H_
#define _LINUX_LIST_H_

#include <stddef.h>
#include <stdbool.h>

#define    prefetch(x)

#ifndef container_of
// a member type pointer __mptr pointed to an actual address
// (actual member addr - actual type struct addr) == offsetof(type, member)
// 以0为起始地址的结构体，其起始位置到成员变量的偏移量与
// 以某个实际同类型结构体起始位置到相同成员变量的偏移量相同
// 那么用该成员变量的实际地址减去到0的偏移量，即为该结构体的实际位置
// return the actual address of a variable containing of ptr
#define container_of(ptr, type, member)                    \
    ({                                \
        const typeof(((type *) NULL)->member) *__mptr = (ptr);    \
        (type *) ((char *) __mptr - offsetof(type, member));    \
    })
#endif

struct list_head {
    struct list_head *next;
    struct list_head *prev;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#undef LIST_HEAD
#define LIST_HEAD(name)    struct list_head name = LIST_HEAD_INIT(name)

static inline void
INIT_LIST_HEAD(struct list_head *list)
{
    list->next = list->prev = list;
}

static inline bool
list_empty(const struct list_head *head)
{
    return (head->next == head);
}

static inline bool
list_is_first(const struct list_head *list,
          const struct list_head *head)
{
    return list->prev == head;
}

static inline bool
list_is_last(const struct list_head *list,
         const struct list_head *head)
{
    return list->next == head;
}

static inline void
_list_del(struct list_head *entry)
{
    entry->next->prev = entry->prev;
    entry->prev->next = entry->next;
}

static inline void
list_del(struct list_head *entry)
{
    _list_del(entry);
    entry->next = entry->prev = NULL;
}

static inline void
_list_add(struct list_head *_new, struct list_head *prev,
    struct list_head *next)
{
    //  ______   next   _____                   prev  ______  next
    // | prev |------> |next |                |----->| head |------|
    // |______|<------ |_____|                |------|______|<-----|
    //           prev
    //
    // **NOTE**
    // 1. first->prev == last
    // 2. last->next  == first
    next->prev = _new;
    // 
    //  ______   next   ______                  ______   prev  ______   next
    // | prev |------> | next |                | _new |<------| head |------|
    // |______|        |______|                |______|       |______|<-----|
    //                     | prev
    //          ______     |
    //         | _new |<----
    //         |______|
    //
    _new->next = next;
    //
    //  ______   next   ______                  ______   prev  ______   next
    // | prev |------> | next |                | _new |<------| head |------|
    // |______|        |______|                |______|------>|______|<-----|
    //                    | ^                            next
    //               prev | |
    //          ______    | | next
    //         | _new |<--- |
    //         |______|------
    //
    _new->prev = prev;
    //
    //  ______   next   ______                  ______   prev  ______   next
    // | prev |------> | next |                | _new |<------| head |------|
    // |______|        |______|                |______|------>|______|<-----|
    //    ^               | ^                      |     next     ^
    //    | prev     prev | |                      |              |
    //    |     ______    | | next                 |--------------|
    //    |----| _new |<--- |                               prev
    //         |______|------
    //
    prev->next = _new;
    //
    //   ______          ______
    //  | prev |        | next |                           next
    //  |______|        |______|                      |<-------------|
    //     | ^             | ^                     ___|__   prev  ___|__
    //     | | prev   prev | |                    | _new |<------| head |
    //     | |    ______   | | next               |______|------>|______|
    // next| |---| _new |<-- |                        |     next     ^
    //     |---->|______|-----                        |--------------|
    //                                                      prev
}

static inline void
list_del_init(struct list_head *entry)
{
    _list_del(entry);
    INIT_LIST_HEAD(entry);
}

#define    list_entry(ptr, type, field)    container_of(ptr, type, field)
#define    list_first_entry(ptr, type, field)    list_entry((ptr)->next, type, field)
#define    list_last_entry(ptr, type, field)    list_entry((ptr)->prev, type, field)

#define    list_for_each(p, head)                        \
    for (p = (head)->next; p != (head); p = p->next)

#define    list_for_each_safe(p, n, head)                    \
    for (p = (head)->next, n = p->next; p != (head); p = n, n = p->next)

#define list_for_each_entry(p, h, field)                \
    for (p = list_first_entry(h, typeof(*p), field); &p->field != (h); \
        p = list_entry(p->field.next, typeof(*p), field))

#define list_for_each_entry_safe(p, n, h, field)            \
    for (p = list_first_entry(h, typeof(*p), field),        \
        n = list_entry(p->field.next, typeof(*p), field); &p->field != (h);\
        p = n, n = list_entry(n->field.next, typeof(*n), field))

#define    list_for_each_entry_reverse(p, h, field)            \
    for (p = list_last_entry(h, typeof(*p), field); &p->field != (h); \
        p = list_entry(p->field.prev, typeof(*p), field))

#define    list_for_each_prev(p, h) for (p = (h)->prev; p != (h); p = p->prev)
#define    list_for_each_prev_safe(p, n, h) for (p = (h)->prev, n = p->prev; p != (h); p = n, n = p->prev)

static inline void
list_add(struct list_head *_new, struct list_head *head)
{
    // insert _new behind head
    _list_add(_new, head, head->next);
}

static inline void
list_add_tail(struct list_head *_new, struct list_head *head)
{
    // insert _new before head
    _list_add(_new, head->prev, head);
}

static inline void
list_move(struct list_head *list, struct list_head *head)
{
    _list_del(list);
    list_add(list, head);
}

static inline void
list_move_tail(struct list_head *entry, struct list_head *head)
{
    _list_del(entry);
    list_add_tail(entry, head);
}

static inline void
_list_splice(const struct list_head *list, struct list_head *prev,
    struct list_head *next)
{
    struct list_head *first;
    struct list_head *last;

    if (list_empty(list))
        return;

    first = list->next;
    last = list->prev;
    first->prev = prev;
    prev->next = first;
    last->next = next;
    next->prev = last;
}

static inline void
list_splice(const struct list_head *list, struct list_head *head)
{
    _list_splice(list, head, head->next);
}

static inline void
list_splice_tail(struct list_head *list, struct list_head *head)
{
    _list_splice(list, head->prev, head);
}

static inline void
list_splice_init(struct list_head *list, struct list_head *head)
{
    _list_splice(list, head, head->next);
    INIT_LIST_HEAD(list);
}

static inline void
list_splice_tail_init(struct list_head *list, struct list_head *head)
{
    _list_splice(list, head->prev, head);
    INIT_LIST_HEAD(list);
}

#endif /* _LINUX_LIST_H_ */

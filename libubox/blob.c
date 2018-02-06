/*
 * blob - library for generating/parsing tagged binary data
 *
 * Copyright (C) 2010 Felix Fietkau <nbd@openwrt.org>
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

#include "blob.h"

/**
 * blob_buffer_grow: callback for blob_buf to extend size of buf->buf
 */
static bool
blob_buffer_grow(struct blob_buf *buf, int minlen)
{
    // alligned with 256, everytime memory is not enough, realloc at least 256 byte for once
    int delta = ((minlen / 256) + 1) * 256;

    // buflen 
    buf->buflen += delta;

    // #include <stdlib.h>
    // void *realloc(void *ptr, size_t size);
    // Desc: changes the size of the memory block pointed to by ptr to size bytes.
    //    The contents will be unchanged in the range from the start of the region
    //    up to the minimum of the old and new sizes.
    //    If the new size is larger than the old size, the added memory will not be initialized.
    //    If ptr is NULL, then the call is equivalent to malloc(size), for all values of size;
    //    If size is equal to zero, and ptr is not NULL, then the call is equivalent to free(ptr).
    //    Unless ptr is NULL, it must be returned by earlier call malloc(), calloc(), or realloc().
    //    On success, returns a pointer to the newly allocated memory(possibly moved), the original
    //    pointer ptr is invalidated and any access to it is undefined behavior.
    //    On fail, returns NULL, and the original memory referenced to by ptr shall not be changed.
    buf->buf = realloc(buf->buf, buf->buflen);
    if (buf->buf)
        // #include <string.h>
        // void *memset(void *s, int c, size_t n);
        // Desc: fills the first n bytes of the memory area pointed to by s with the constant byte c.
        //    returns a pointer to the memory area s. No errors are defined.
        memset(buf->buf + buf->buflen - delta, 0, delta); // Set new memory to 0
    return !!buf->buf;
}

/**
 * blob_init: initialize the attr->id_len according to id and len
 */
static void
blob_init(struct blob_attr *attr, int id, unsigned int len)
{
    // keep last 24 bits of len
    len &= BLOB_ATTR_LEN_MASK;

    // left shift 24 bit, and keep higher 7 bits(lower 7 bits of original id)
    // leave the highest bit unused currently
    len |= (id << BLOB_ATTR_ID_SHIFT) & BLOB_ATTR_ID_MASK;

    /**
     *  ___ _______ _________________
     * |   |  id   |       len       |
     * |___|_______|_________________|
     * |<1>|<  7  >|<       24      >|
     *
     */

    // covert to big endian
    attr->id_len = cpu_to_be32(len);
}

/**
 * offset_to_attr: fetch the address adding offset from buf->buf.
 */
static inline struct blob_attr *
offset_to_attr(struct blob_buf *buf, int offset)
{
    void *ptr = (char *)buf->buf + offset - BLOB_COOKIE;
    return ptr;
}

/**
 * attr_to_offset: fetch the offset from buf->buf to the address pointed to by attr.
 */
static inline int
attr_to_offset(struct blob_buf *buf, struct blob_attr *attr)
{
    return (char *)attr - (char *) buf->buf + BLOB_COOKIE;
}

/**
 * blob_buf_grow: call buf->grow() to extand the memory size of buf->buf
 */
void
blob_buf_grow(struct blob_buf *buf, int required)
{
    int offset_head = attr_to_offset(buf, buf->head); // (char *)buf->head - (char *)buf->buf + BLOB_COOKIE

    // blob_buffer_grow(buf, required)
    // return new location of buf->buf
    if (!buf->grow || !buf->grow(buf, required))
        return;

    // since the location of buf->buf may be moved, keep the buf->head repoint to the position before realloc
    buf->head = offset_to_attr(buf, offset_head); // (char *)buf->buf + offset_head - BLOB_COOKIE
}

static struct blob_attr *
blob_add(struct blob_buf *buf, struct blob_attr *pos, int id, int payload)
{
    // obain the offset from the address of buf to the address of pos
    int offset = attr_to_offset(buf, pos); // (char *)pos - (char *)buf->buf + BLOB_COOKIE

    // payload represents the length of buf->head->data, it should equals to buf->head->id_len
    // buf->buflen represents the total size of buf->buf
    int required = (offset - BLOB_COOKIE + sizeof(struct blob_attr) + payload) - buf->buflen;
    struct blob_attr *attr;

    // obtain the position referenced by pos
    if (required > 0) {
        // no enough space for new message, call buf->grow() to realloc more.
        blob_buf_grow(buf, required);

        // retrieve the positon where u want to insert to in new buf
        attr = offset_to_attr(buf, offset); // (char *)buf->buf + offset - BLOB_COOKIE
    } else {
        // current memory is enough
        attr = pos;
    }

    // initialize atrr->id_len
    blob_init(attr, id, payload + sizeof(struct blob_attr));

    // zero clear the payload
    blob_fill_pad(attr);
    return attr;
}

int
blob_buf_init(struct blob_buf *buf, int id)
{
    // called when no enough buffer spaces to allocate new space.
    if (!buf->grow)
        buf->grow = blob_buffer_grow;

    // set buf->head and buf->buf point to a same address at initialization
    buf->head = buf->buf;

    // add a blob_attr space at the address pointed to by buf->buf
    // with payload length = 0
    if (blob_add(buf, buf->buf, id, 0) == NULL)
        return -ENOMEM;

    return 0;
}

void
blob_buf_free(struct blob_buf *buf)
{
    free(buf->buf);
    buf->buf = NULL;
    buf->buflen = 0;
}

/**
 * blob_fill_pad: zero clear the data segment of blob_attr pointer attr
 */
void
blob_fill_pad(struct blob_attr *attr)
{
    char *buf = (char *) attr;

    // fetch the total length of stucture attr and its data
    int len = blob_pad_len(attr);

    // caculate the data length
    int delta = len - blob_raw_len(attr);

    if (delta > 0)
        memset(buf + len - delta, 0, delta);
}

void
blob_set_raw_len(struct blob_attr *attr, unsigned int len)
{
    len &= BLOB_ATTR_LEN_MASK;
    attr->id_len &= ~cpu_to_be32(BLOB_ATTR_LEN_MASK);
    attr->id_len |= cpu_to_be32(len);
}

struct blob_attr *
blob_new(struct blob_buf *buf, int id, int payload)
{
    struct blob_attr *attr;

    attr = blob_add(buf, blob_next(buf->head), id, payload);
    if (!attr)
        return NULL;

    blob_set_raw_len(buf->head, blob_pad_len(buf->head) + blob_pad_len(attr));
    return attr;
}

struct blob_attr *
blob_put_raw(struct blob_buf *buf, const void *ptr, int len)
{
    struct blob_attr *attr;

    if (len < sizeof(struct blob_attr) || !ptr)
        return NULL;

    attr = blob_add(buf, blob_next(buf->head), 0, len - sizeof(struct blob_attr));
    blob_set_raw_len(buf->head, blob_pad_len(buf->head) + len);
    memcpy(attr, ptr, len);
    return attr;
}

struct blob_attr *
blob_put(struct blob_buf *buf, int id, const void *ptr, int len)
{
    struct blob_attr *attr;

    attr = blob_new(buf, id, len);
    if (!attr)
        return NULL;

    if (ptr)
        memcpy(blob_data(attr), ptr, len);
    return attr;
}

void *
blob_nest_start(struct blob_buf *buf, int id)
{
    unsigned long offset = attr_to_offset(buf, buf->head);
    buf->head = blob_new(buf, id, 0);
    return (void *) offset;
}

void
blob_nest_end(struct blob_buf *buf, void *cookie)
{
    struct blob_attr *attr = offset_to_attr(buf, (unsigned long) cookie);
    blob_set_raw_len(attr, blob_pad_len(attr) + blob_len(buf->head));
    buf->head = attr;
}

static const int blob_type_minlen[BLOB_ATTR_LAST] = {
    [BLOB_ATTR_STRING] = 1,
    [BLOB_ATTR_INT8] = sizeof(uint8_t),
    [BLOB_ATTR_INT16] = sizeof(uint16_t),
    [BLOB_ATTR_INT32] = sizeof(uint32_t),
    [BLOB_ATTR_INT64] = sizeof(uint64_t),
};

bool
blob_check_type(const void *ptr, int len, int type)
{
    const char *data = ptr;

    if (type >= BLOB_ATTR_LAST)
        return false;

    if (type >= BLOB_ATTR_INT8 && type <= BLOB_ATTR_INT64) {
        if (len != blob_type_minlen[type])
            return false;
    } else {
        if (len < blob_type_minlen[type])
            return false;
    }

    if (type == BLOB_ATTR_STRING && data[len - 1] != 0)
        return false;

    return true;
}

int
blob_parse(struct blob_attr *attr, struct blob_attr **data, const struct blob_attr_info *info, int max)
{
    struct blob_attr *pos;
    int found = 0;
    int rem;

    memset(data, 0, sizeof(struct blob_attr *) * max);
    blob_for_each_attr(pos, attr, rem) {
        int id = blob_id(pos);
        int len = blob_len(pos);

        if (id >= max)
            continue;

        if (info) {
            int type = info[id].type;

            if (type < BLOB_ATTR_LAST) {
                if (!blob_check_type(blob_data(pos), len, type))
                    continue;
            }

            if (info[id].minlen && len < info[id].minlen)
                continue;

            if (info[id].maxlen && len > info[id].maxlen)
                continue;

            if (info[id].validate && !info[id].validate(&info[id], attr))
                continue;
        }

        if (!data[id])
            found++;

        data[id] = pos;
    }
    return found;
}

bool
blob_attr_equal(const struct blob_attr *a1, const struct blob_attr *a2)
{
    if (!a1 && !a2)
        return true;

    if (!a1 || !a2)
        return false;

    if (blob_pad_len(a1) != blob_pad_len(a2))
        return false;

    return !memcmp(a1, a2, blob_pad_len(a1));
}

struct blob_attr *
blob_memdup(struct blob_attr *attr)
{
    struct blob_attr *ret;
    int size = blob_pad_len(attr);

    ret = malloc(size);
    if (!ret)
        return NULL;

    memcpy(ret, attr, size);
    return ret;
}

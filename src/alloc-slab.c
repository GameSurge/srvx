/* alloc-slab.c - Slab debugging allocator
 * Copyright 2005 srvx Development Team
 *
 * This file is part of srvx.
 *
 * srvx is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "common.h"
#include "log.h"

#if defined(HAVE_SYS_MMAN_H)
# include <sys/mman.h>
#endif

#if !defined(HAVE_MMAP)
# error The slab allocator requires that your system have the mmap() system call.
#endif

struct slab {
    struct slabset *parent;
    struct slab *prev;
    struct slab *next;
    void *base;
    void **free;
    unsigned int used;
};

struct slabset {
    struct slabset *next;
    struct slab *child;
    size_t size;
    size_t items_per_slab;
};

#define SLAB_MIN     (2 * sizeof(void*))
#define SLAB_GRAIN   sizeof(void*)
#define SLAB_ALIGN   SLAB_GRAIN
#define SMALL_CUTOFF 512
/* Element size < SMALL_CUTOFF -> use small slabs.
 * Larger elements are allocated directly using mmap().  The largest
 * regularly allocated struct in srvx 1.x is smaller than
 * SMALL_CUTOFF, so there is not much point in coding support for
 * larger slabs.
 */

static struct slabset *little_slabs[SMALL_CUTOFF / SLAB_GRAIN];
static struct slabset slabset_slabs;
unsigned long alloc_count;
unsigned long alloc_size;

#if defined(MAP_ANON)
#elif defined(MAP_ANONYMOUS)
# define MAP_ANON MAP_ANONYMOUS
#else
# define MAP_ANON 0
#endif

static size_t
slab_pagesize(void)
{
    static size_t pagesize;
    if (pagesize
#if defined(HAVE_GETPAGESIZE)
        || (pagesize  = getpagesize())
#endif
#if defined(HAVE_SYSCONF) && defined(_SC_PAGESIZE)
        || (pagesize = sysconf(_SC_PAGESIZE))
#endif
#if defined(HAVE_SYSCONF) && defined(_SC_PAGE_SIZE)
        || (pagesize = sysconf(_SC_PAGE_SIZE))
#endif
        ) return pagesize;
    assert(0 && "unable to find system page size");
    return pagesize = 4096;
}

static size_t
slab_round_up(size_t size)
{
    return (size + slab_pagesize() - 1) & ~(slab_pagesize() - 1);
}

static void *
slab_map(size_t length)
{
    static int mmap_fd = -1;
    void *res;

#if ! MAP_ANON
    if (mmap_fd < 0) {
        mmap_fd = open("/dev/zero", 0);
        if (mmap_fd < 0)
            log_module(MAIN_LOG, LOG_FATAL, "Unable to open /dev/zero for mmap: %s", strerror(errno()));
    }
#endif
    res = mmap(0, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, mmap_fd, 0);
    if (res == MAP_FAILED)
        log_module(MAIN_LOG, LOG_FATAL, "Unable to mmap %lu bytes (%s).", (unsigned long)length, strerror(errno));
    return res;
}

static void *slab_alloc(struct slabset *sset);
static void slab_unalloc(void *ptr, size_t size);

static struct slabset *
slabset_create(size_t size)
{
    unsigned int idx;

    size = (size < SLAB_MIN) ? SLAB_MIN : (size + SLAB_GRAIN - 1) & ~(SLAB_GRAIN - 1);
    idx = size / SLAB_GRAIN;
    assert(idx < ArrayLength(little_slabs));
    if (!little_slabs[idx]) {
        if (!slabset_slabs.size) {
            unsigned int idx2 = (sizeof(struct slabset) + SLAB_GRAIN - 1) / SLAB_GRAIN;
            slabset_slabs.size = sizeof(struct slabset);
            slabset_slabs.items_per_slab = (slab_pagesize() - sizeof(struct slab)) / ((sizeof(struct slabset) + SLAB_ALIGN - 1) & ~(SLAB_ALIGN - 1));
            little_slabs[idx2] = &slabset_slabs;
            if (idx == idx2)
                return &slabset_slabs;
        }
        little_slabs[idx] = slab_alloc(&slabset_slabs);
        little_slabs[idx]->size = size;
        little_slabs[idx]->items_per_slab = (slab_pagesize() - sizeof(struct slab)) / ((size + SLAB_ALIGN - 1) & ~(SLAB_ALIGN - 1));
    }
    return little_slabs[idx];
}

static void *
slab_alloc(struct slabset *sset)
{
    struct slab *slab;
    void **item;

    if (!sset->child || !sset->child->free) {
        unsigned int ii, step;

        /* Allocate new slab. */
        item = slab_map(slab_pagesize());
        slab = (struct slab*)((char*)item + slab_pagesize() - sizeof(*slab));
        slab->base = item;

        /* Populate free list. */
        step = (sset->size + SLAB_ALIGN - 1) & ~(SLAB_ALIGN - 1);
        for (ii = 1, item = slab->free = slab->base;
             ii < sset->items_per_slab;
             ++ii, item = (*item = (char*)item + step));

        /* Link to parent slabset. */
        slab->parent = sset;
        if ((slab->prev = sset->child)) {
            slab->next = slab->prev->next;
            slab->prev->next = slab;
            if (slab->next)
                slab->next->prev = slab;
        }
        sset->child = slab;
    }

    slab = sset->child;
    item = slab->free;
    slab->free = *item;
    if (slab->used++ == sset->items_per_slab) {
        if (sset->child != slab) {
            /* Unlink slab and reinsert before sset->child. */
            if (slab->prev)
                slab->prev->next = slab->next;
            if (slab->next)
                slab->next->prev = slab->prev;
            if ((slab->prev = sset->child->prev))
                slab->prev->next = slab;
            if ((slab->next = sset->child))
                slab->next->prev = slab;
        } else if (slab->next) {
            /* Advance sset->child to next pointer. */
            sset->child = slab->next;
        }
    }
    memset(item, 0, sset->size);
    return item;
}

static void
slab_unalloc(void *ptr, size_t size)
{
    void **item;
    struct slab *slab, *new_next;

    item = ptr;
    assert(size < SMALL_CUTOFF);
    slab = (struct slab*)((((unsigned long)ptr | (slab_pagesize() - 1)) + 1) - sizeof(*slab));
    *item = slab->free;
    slab->free = item;

    if (slab->used-- == slab->parent->items_per_slab
        && slab->parent->child != slab) {
        new_next = slab->parent->child;
        slab->parent->child = slab;
    } else if (!slab->used) {
        for (new_next = slab;
             new_next->next && new_next->next->used;
             new_next = new_next->next) ;
        new_next = new_next->next;
    } else
        new_next = NULL;

    if (new_next) {
        if (slab->prev)
            slab->prev->next = slab->next;
        if (slab->next)
            slab->next->prev = slab->prev;
        if ((slab->prev = new_next->prev))
            slab->prev->next = slab;
        if ((slab->next = new_next->next))
            slab->next->prev = slab;
    }
}

void *
slab_malloc(UNUSED_ARG(const char *file), UNUSED_ARG(unsigned int line), size_t size)
{
    size_t real, *res;

    real = size + sizeof(size_t);
    if (real < SMALL_CUTOFF)
        res = slab_alloc(slabset_create(real));
    else
        res = slab_map(slab_round_up(real));
    *res = size;
    return res + 1;
}

void *
slab_realloc(const char *file, unsigned int line, void *ptr, size_t size)
{
    size_t orig, *newblock;

    if (!ptr)
        return slab_malloc(file, line, size);

    verify(ptr);
    orig = ((size_t*)ptr)[-1];
    if (orig >= size)
        return ptr;
    newblock = slab_malloc(file, line, size);
    memcpy(newblock, ptr, orig);
    return newblock;
}

char *
slab_strdup(const char *file, unsigned int line, const char *src)
{
    char *target;
    size_t len;

    len = strlen(src) + 1;
    target = slab_malloc(file, line, len);
    memcpy(target, src, len);
    return target;
}

void
slab_free(UNUSED_ARG(const char *file), UNUSED_ARG(unsigned int line), void *ptr)
{
    size_t real, *size;

    if (!ptr)
        return;
    verify(ptr);
    size = (size_t*)ptr - 1;
    real = *size + sizeof(size_t);
    if (real < SMALL_CUTOFF)
        slab_unalloc(size, real);
    else
        munmap(size, slab_round_up(real));
}

void
verify(const void *ptr)
{
    size_t size;

    if (!ptr)
        return;
    else if ((size = ((size_t*)ptr)[-1] + sizeof(size_t)) >= SMALL_CUTOFF)
        assert(((unsigned long)ptr & (slab_pagesize() - 1)) == sizeof(size_t));
    else {
        struct slab *slab;
        size_t expected;

        expected = (size + SLAB_GRAIN - 1) & ~(SLAB_GRAIN - 1);
        slab = (struct slab*)((((unsigned long)ptr | (slab_pagesize() - 1)) + 1) - sizeof(*slab));
        assert(slab->parent->size == expected);
    }
}

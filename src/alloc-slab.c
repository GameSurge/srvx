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

#define SLAB_DEBUG_HEADER 1
#define SLAB_DEBUG_LOG    2
#define SLAB_DEBUG_PERMS  4

#if !defined(SLAB_DEBUG)
# define SLAB_DEBUG 0
#endif

#if !defined(SLAB_RESERVE)
# define SLAB_RESERVE 0
#endif

#if !defined(MAX_SLAB_FREE)
# define MAX_SLAB_FREE 1024
#endif

#if SLAB_DEBUG & SLAB_DEBUG_HEADER

#define ALLOC_MAGIC 0x1a
#define FREE_MAGIC  0xcf

struct alloc_header {
    unsigned int size : 24;
    unsigned int magic : 8;
    unsigned int file_id : 8;
    unsigned int line : 16;
};

static const char *file_ids[256];
static struct file_id_entry {
    const char *name;
    unsigned int id : 8;
} file_id_map[256];
unsigned int file_ids_used;

static int
file_id_cmp(const void *a_, const void *b_)
{
    return strcmp(*(const char**)a_, *(const char**)b_);
}

static unsigned int
get_file_id(const char *fname)
{
    struct file_id_entry *entry;

    entry = bsearch(&fname, file_id_map, file_ids_used, sizeof(file_id_map[0]), file_id_cmp);
    if (entry)
        return entry->id;
    entry = file_id_map + file_ids_used;
    file_ids[file_ids_used] = fname;
    entry->name = fname;
    entry->id = file_ids_used;
    qsort(file_id_map, ++file_ids_used, sizeof(file_id_map[0]), file_id_cmp);
    return file_ids_used - 1;
}

typedef struct alloc_header alloc_header_t;

#else

typedef size_t alloc_header_t;

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
    struct slab *child;
    size_t nslabs;
    size_t nallocs;
    size_t size;
    size_t items_per_slab;
};

#define SLAB_MIN     (2 * sizeof(void*))
#define SLAB_GRAIN   sizeof(void*)
#define SLAB_ALIGN   SLAB_GRAIN
#define SMALL_CUTOFF 576
/* Element size < SMALL_CUTOFF -> use small slabs.
 * Larger elements are allocated directly using mmap().  The largest
 * regularly allocated struct in srvx 1.x is smaller than
 * SMALL_CUTOFF, so there is not much point in coding support for
 * larger slabs.
 */

static struct slabset little_slabs[SMALL_CUTOFF / SLAB_GRAIN];
static struct slab *free_slab_head;
static struct slab *free_slab_tail;
unsigned long free_slab_count;
unsigned long big_alloc_count;
unsigned long big_alloc_size;
unsigned long slab_count;
unsigned long slab_alloc_count;
unsigned long slab_alloc_size;

#if defined(MAP_ANON)
#elif defined(MAP_ANONYMOUS)
# define MAP_ANON MAP_ANONYMOUS
#else
# define MAP_ANON 0
#endif

#if SLAB_DEBUG & SLAB_DEBUG_LOG

FILE *slab_log;

struct slab_log_entry
{
    struct timeval tv;
    void *slab;
    ssize_t size;
};

static void
close_slab_log(void)
{
    fclose(slab_log);
}

static void
slab_log_alloc(void *slab, size_t size)
{
    struct slab_log_entry sle;

    gettimeofday(&sle.tv, NULL);
    sle.slab = slab;
    sle.size = (ssize_t)size;

    if (!slab_log)
    {
        const char *fname;
        fname = getenv("SLAB_LOG_FILE");
        if (!fname)
            fname = "slab.log";
        slab_log = fopen(fname, "w");
        atexit(close_slab_log);
    }

    fwrite(&sle, sizeof(sle), 1, slab_log);
}

static void
slab_log_free(void *slab, size_t size)
{
    struct slab_log_entry sle;

    gettimeofday(&sle.tv, NULL);
    sle.slab = slab;
    sle.size = -(ssize_t)size;
    fwrite(&sle, sizeof(sle), 1, slab_log);
}

static void
slab_log_unmap(void *slab)
{
    struct slab_log_entry sle;

    gettimeofday(&sle.tv, NULL);
    sle.slab = slab;
    sle.size = 0;
    fwrite(&sle, sizeof(sle), 1, slab_log);
}

#else
# define slab_log_alloc(SLAB, SIZE)
# define slab_log_free(SLAB, SIZE)
# define slab_log_unmap(SLAB)
#endif

#if (SLAB_DEBUG & SLAB_DEBUG_PERMS) && defined(HAVE_MPROTECT)

static void
slab_protect(struct slab *slab)
{
    mprotect(slab, (char*)(slab + 1) - (char*)slab->base, PROT_NONE);
}

static void
slab_unprotect(struct slab *slab)
{
    mprotect(slab, (char*)(slab + 1) - (char*)slab->base, PROT_READ | PROT_WRITE);
}

#else
# define slab_protect(SLAB) (void)(SLAB)
# define slab_unprotect(SLAB) (void)(SLAB)
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
    if (!little_slabs[idx].size) {
        little_slabs[idx].size = size;
        little_slabs[idx].items_per_slab = (slab_pagesize() - sizeof(struct slab)) / ((size + SLAB_ALIGN - 1) & ~(SLAB_ALIGN - 1));
    }
    return &little_slabs[idx];
}

static void *
slab_alloc(struct slabset *sset)
{
    struct slab *slab;
    void **item;

    if (!sset->child || !sset->child->free) {
        unsigned int ii, step;

        /* Allocate new slab. */
        if (free_slab_head) {
            slab = free_slab_head;
            slab_unprotect(slab);
            if (!(free_slab_head = slab->next))
                free_slab_tail = NULL;
        } else {
            item = slab_map(slab_pagesize());
            slab = (struct slab*)((char*)item + slab_pagesize() - sizeof(*slab));
            slab->base = item;
            slab_count++;
        }
        slab_log_alloc(slab, sset->size);

        /* Populate free list. */
        step = (sset->size + SLAB_ALIGN - 1) & ~(SLAB_ALIGN - 1);
        for (ii = 1, item = slab->free = slab->base;
             ii < sset->items_per_slab;
             ++ii, item = (*item = (char*)item + step));
        *item = NULL;

        /* Link to parent slabset. */
        slab->parent = sset;
        slab->prev = sset->child;
        if (slab->prev) {
            slab->next = slab->prev->next;
            slab->prev->next = slab;
            if (slab->next)
                slab->next->prev = slab;
        } else
            slab->next = NULL;
        assert(!slab->next || slab == slab->next->prev);
        assert(!slab->prev || slab == slab->prev->next);
        sset->child = slab;
        sset->nslabs++;
    }

    slab = sset->child;
    item = slab->free;
    assert(((unsigned long)item & (slab_pagesize() - 1))
           <= (slab_pagesize() - sizeof(*slab) - sset->size));
    slab->free = *item;
    if (++slab->used == sset->items_per_slab) {
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
            assert(!slab->next || slab == slab->next->prev);
            assert(!slab->prev || slab == slab->prev->next);
        } else if (slab->next) {
            /* Advance sset->child to next pointer. */
            sset->child = slab->next;
        }
    }
    sset->nallocs++;
    memset(item, 0, sset->size);
    return item;
}

static void
slab_unalloc(void *ptr, size_t size)
{
    struct slab *slab, *new_next;

    assert(size < SMALL_CUTOFF);
    slab = (struct slab*)((((unsigned long)ptr | (slab_pagesize() - 1)) + 1) - sizeof(*slab));
    *(void**)ptr = slab->free;
    slab->free = ptr;
    slab->parent->nallocs--;

    if (slab->used-- == slab->parent->items_per_slab
        && slab->parent->child != slab) {
        /* Unlink from current position, relink as parent's first child. */
        new_next = slab->parent->child;
        assert(new_next != NULL);
        if (slab->prev)
            slab->prev->next = slab->next;
        if (slab->next)
            slab->next->prev = slab->prev;
        if ((slab->prev = new_next->prev))
            slab->prev->next = slab;
        slab->next = new_next;
        new_next->prev = slab;
        slab->parent->child = slab;
        assert(!slab->next || slab == slab->next->prev);
        assert(!slab->prev || slab == slab->prev->next);
    } else if (!slab->used) {
        slab_log_free(slab, size);

        /* Unlink slab from its parent. */
        slab->parent->nslabs--;
        if (slab->prev)
            slab->prev->next = slab->next;
        if (slab->next)
            slab->next->prev = slab->prev;
        new_next = slab->next ? slab->next : slab->prev;
        if (slab == slab->parent->child)
            slab->parent->child = new_next;
        if (new_next) {
            assert(!new_next->next || new_next == new_next->next->prev);
            assert(!new_next->prev || new_next == new_next->prev->next);
        }

#if SLAB_RESERVE
        /* Make sure we have enough free slab pages. */
        while (free_slab_count < SLAB_RESERVE) {
            struct slab *tslab;
            void *item;

            item = slab_map(slab_pagesize());
            tslab = (struct slab*)((char*)item + slab_pagesize() - sizeof(*slab));
            tslab->base = item;
            tslab->prev = free_slab_tail;
            free_slab_tail = tslab;
            if (!free_slab_head)
                free_slab_head = tslab;
            else {
                slab_unprotect(tslab->prev);
                tslab->prev->next = tslab;
                slab_protect(tslab->prev);
            }
            free_slab_count++;
            slab_count++;
        }
#endif

        /* Link to list of free slabs. */
        slab->parent = NULL;
        slab->next = NULL;
        slab->prev = free_slab_tail;
        if (slab->prev) {
            slab_unprotect(slab->prev);
            slab->prev->next = slab;
            slab_protect(slab->prev);
        } else
            free_slab_head = slab;
        slab_protect(slab);
        free_slab_tail = slab;
        free_slab_count++;

#if MAX_SLAB_FREE >= 0
        /* Unlink and unmap old slabs, so accesses to stale-enough
         * pointers will fault. */
        while (free_slab_count > MAX_SLAB_FREE) {
            struct slab *tslab;

            tslab = free_slab_tail;
            slab_unprotect(tslab);
            free_slab_tail = tslab->prev;
            if (tslab->prev) {
                slab_unprotect(tslab->prev);
                tslab->prev->next = NULL;
                slab_protect(tslab->prev);
            } else
                free_slab_head = NULL;
            free_slab_count--;
            slab_count--;
            slab_log_unmap(slab);
            munmap(slab->base, slab_pagesize());
        }
#endif
    }
    (void)size;
}

void *
slab_malloc(const char *file, unsigned int line, size_t size)
{
    alloc_header_t *res;
    size_t real;

    assert(size < 1 << 24);
    real = (size + sizeof(*res) + SLAB_GRAIN - 1) & ~(SLAB_GRAIN - 1);
    if (real < SMALL_CUTOFF) {
        res = slab_alloc(slabset_create(real));
        slab_alloc_count++;
        slab_alloc_size += size;
    } else {
        res = slab_map(slab_round_up(real));
        big_alloc_count++;
        big_alloc_size += size;
        slab_log_alloc(res, size);
    }
#if SLAB_DEBUG & SLAB_DEBUG_HEADER
    res->file_id = get_file_id(file);
    res->size = size;
    res->line = line;
    res->magic = ALLOC_MAGIC;
#else
    *res = size;
    (void)file; (void)line;
#endif
    return res + 1;
}

void *
slab_realloc(const char *file, unsigned int line, void *ptr, size_t size)
{
    alloc_header_t *orig;
    void *newblock;
    size_t osize;

    if (!ptr)
        return slab_malloc(file, line, size);

    verify(ptr);
    orig = (alloc_header_t*)ptr - 1;
#if SLAB_DEBUG & SLAB_DEBUG_HEADER
    osize = orig->size;
#else
    osize = *orig;
#endif
    if (osize >= size)
        return ptr;
    newblock = slab_malloc(file, line, size);
    memcpy(newblock, ptr, osize);
    slab_free(file, line, ptr);
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
slab_free(const char *file, unsigned int line, void *ptr)
{
    alloc_header_t *hdr;
    size_t user, real;

    if (!ptr)
        return;
    verify(ptr);
    hdr = (alloc_header_t*)ptr - 1;
#if SLAB_DEBUG & SLAB_DEBUG_HEADER
    hdr->file_id = get_file_id(file);
    hdr->line = line;
    hdr->magic = FREE_MAGIC;
    user = hdr->size;
#else
    user = *hdr;
    (void)file; (void)line;
#endif
    real = (user + sizeof(*hdr) + SLAB_GRAIN - 1) & ~(SLAB_GRAIN - 1);
    if (real < SMALL_CUTOFF) {
        memset(hdr + 1, 0xde, real - sizeof(*hdr));
        slab_unalloc(hdr, real);
        slab_alloc_count--;
        slab_alloc_size -= user;
    } else {
        munmap(hdr, slab_round_up(real));
        big_alloc_count--;
        big_alloc_size -= user;
        slab_log_unmap(hdr);
    }
}

/* Undefine the verify macro in case we're not debugging. */
#undef verify
void
verify(const void *ptr)
{
    alloc_header_t *hdr;
    size_t real;

    if (!ptr)
        return;

    hdr = (alloc_header_t*)ptr - 1;
#if SLAB_DEBUG & SLAB_DEBUG_HEADER
    real = hdr->size + sizeof(*hdr);
    assert(hdr->file_id < file_ids_used);
    assert(hdr->magic == ALLOC_MAGIC);
#else
    real = *hdr + sizeof(*hdr);
#endif
    real = (real + SLAB_GRAIN - 1) & ~(SLAB_GRAIN - 1);

    if (real >= SMALL_CUTOFF)
        assert(((unsigned long)ptr & (slab_pagesize() - 1)) == sizeof(*hdr));
    else {
        struct slab *slab;
        size_t expected;

        expected = (real + SLAB_GRAIN - 1) & ~(SLAB_GRAIN - 1);
        slab = (struct slab*)((((unsigned long)ptr | (slab_pagesize() - 1)) + 1) - sizeof(*slab));
        assert(slab->parent->size == expected);
    }
}

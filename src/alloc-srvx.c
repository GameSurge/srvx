/* alloc-srvx.c - Debug allocation wrapper
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

#undef malloc
#undef free

#define ALLOC_MAGIC 0x1acf
#define FREE_MAGIC  0xfc1d
const char redzone[] = { '\x03', '\x47', '\x76', '\xc7' };

struct alloc_header {
    unsigned int file_id : 8;
    unsigned int size : 24;
    unsigned int line : 16;
    unsigned int magic : 16;
};

static char file_id_map[256][32];
static unsigned int file_ids_used;
unsigned long alloc_count, alloc_size;

static int
file_id_cmp(const void *a_, const void *b_)
{
    return strcmp(a_, b_);
}

static unsigned int
get_file_id(const char *fname)
{
    void *entry;

    entry = bsearch(fname, file_id_map, file_ids_used, sizeof(file_id_map[0]), file_id_cmp);
    if (entry)
        return ((char*)entry - file_id_map[0]) / sizeof(file_id_map[0]);
    strcpy(file_id_map[file_ids_used++], fname);
    qsort(file_id_map, file_ids_used, sizeof(file_id_map[0]), file_id_cmp);
    return file_ids_used - 1;
}

void *
srvx_malloc(const char *file, unsigned int line, size_t size)
{
    struct alloc_header *block;

    block = malloc(sizeof(*block) + size + sizeof(redzone));
    assert(block != NULL);
    memset(block, 0, sizeof(*block) + size);
    memcpy((char*)(block + 1) + size, redzone, sizeof(redzone));
    block->file_id = get_file_id(file);
    block->line = line;
    block->size = size;
    block->magic = ALLOC_MAGIC;
    alloc_count++;
    alloc_size += size;
    return block + 1;
}

void *
srvx_realloc(const char *file, unsigned int line, void *ptr, size_t size)
{
    struct alloc_header *block = NULL, *newblock;

    if (ptr) {
        block = (struct alloc_header *)ptr - 1;
        assert(block->magic == ALLOC_MAGIC);
        assert(0 == memcmp((char*)(block + 1) + block->size, redzone, sizeof(redzone)));
        if (block->size >= size)
            return block + 1;
    }

    newblock = malloc(sizeof(*newblock) + size + sizeof(redzone));
    assert(newblock != NULL);
    memset(newblock, 0, sizeof(*newblock) + size + sizeof(redzone));
    memcpy((char*)(newblock + 1) + size, redzone, sizeof(redzone));
    newblock->file_id = get_file_id(file);
    newblock->line = line;
    newblock->size = size;
    newblock->magic = ALLOC_MAGIC;
    alloc_count++;
    alloc_size += size;

    if (ptr) {
        memcpy(newblock + 1, block + 1, block->size);
        size = block->size + sizeof(*block);
        memset(block, 0, size);
        block->magic = FREE_MAGIC;
        free(block);
        alloc_count--;
        alloc_size -= size - sizeof(*block);
    }

    return newblock + 1;
}

char *
srvx_strdup(const char *file, unsigned int line, const char *src)
{
    char *target;
    size_t len;

    len = strlen(src) + 1;
    target = srvx_malloc(file, line, len);
    memcpy(target, src, len);
    return target;
}

void
srvx_free(const char *file, unsigned int line, void *ptr)
{
    struct alloc_header *block;
    size_t size;

    if (!ptr)
        return;
    block = (struct alloc_header *)ptr - 1;
    assert(block->magic == ALLOC_MAGIC);
    assert(0 == memcmp((char*)(block + 1) + block->size, redzone, sizeof(redzone)));
    size = block->size;
    memset(block + 1, 0xde, size);
    block->magic = FREE_MAGIC;
    free(block);
    alloc_count--;
    alloc_size -= size;
    (void)file; (void)line;
}
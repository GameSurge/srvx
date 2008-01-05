/* timeq.c - time-based event queue
 * Copyright 2000-2004 srvx Development Team
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
 *
 * You should have received a copy of the GNU General Public License
 * along with srvx; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include "common.h"
#include "heap.h"
#include "timeq.h"

heap_t timeq;

struct timeq_entry {
    timeq_func func;
    void *data;
};

static void
timeq_cleanup(void)
{
    timeq_del(0, 0, 0, TIMEQ_IGNORE_WHEN|TIMEQ_IGNORE_FUNC|TIMEQ_IGNORE_DATA);
    heap_delete(timeq);
    timeq = NULL;
}

static void
timeq_init(void)
{
    timeq = heap_new(ulong_comparator);
    reg_exit_func(timeq_cleanup);
}

unsigned long
timeq_next(void)
{
    void *time;
    if (!timeq)
        return ~0;
    heap_peek(timeq, &time, 0);
    return (unsigned long)time;
}

void
timeq_add(unsigned long when, timeq_func func, void *data)
{
    struct timeq_entry *ent;
    void *w;
    ent = malloc(sizeof(struct timeq_entry));
    ent->func = func;
    ent->data = data;
    w = (void*)when;
    if (!timeq)
        timeq_init();
    heap_insert(timeq, w, ent);
}

struct timeq_extra {
    unsigned long when;
    timeq_func func;
    void *data;
    int mask;
};

static int
timeq_del_matching(void *key, void *data, void *extra)
{
    struct timeq_entry *a = data;
    struct timeq_extra *b = extra;
    if (((b->mask & TIMEQ_IGNORE_WHEN) || ((unsigned long)key == b->when))
        && ((b->mask & TIMEQ_IGNORE_FUNC) || (a->func == b->func))
        && ((b->mask & TIMEQ_IGNORE_DATA) || (a->data == b->data))) {
        free(data);
        return 1;
    } else {
        return 0;
    }
}

void
timeq_del(unsigned long when, timeq_func func, void *data, int mask)
{
    struct timeq_extra extra;
    extra.when = when;
    extra.func = func;
    extra.data = data;
    extra.mask = mask;
    if (timeq)
        heap_remove_pred(timeq, timeq_del_matching, &extra);
}

unsigned int
timeq_size(void)
{
    return heap_size(timeq);
}

void
timeq_run(void)
{
    void *k, *d;
    struct timeq_entry *ent;
    while (heap_size(timeq) > 0) {
        heap_peek(timeq, &k, &d);
        if ((unsigned long)k > now)
            break;
        ent = d;
        heap_pop(timeq);
        ent->func(ent->data);
        free(ent);
    }
}

/* heap.c - Abstract heap type
 * Copyright 2000-2002 srvx Development Team
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

/* Possible optimizations:
 *
 * Use another type of heap (rather than binary) if our heaps are big enough.
 *
 * Coalesce multiple entries with the same key into the same chunk, and have
 * a new API function to return all of the entries at the top of the heap.
 */

struct heap {
    comparator_f comparator;
    void **data;
    unsigned int data_used, data_alloc;
};

/*
 *  Allocate a new heap.
 */
heap_t
heap_new(comparator_f comparator)
{
    heap_t heap = malloc(sizeof(struct heap));
    heap->comparator = comparator;
    heap->data_used = 0;
    heap->data_alloc = 8;
    heap->data = malloc(2*heap->data_alloc*sizeof(void*));
    return heap;
}

/*
 *  Move the element at "index" in the heap as far up the heap as is
 *  proper (i.e., as long as its parent node is less than or equal to
 *  its value).
 */
static void
heap_heapify_up(heap_t heap, unsigned int index)
{
    int res;
    unsigned int parent;
    void *last_key, *last_data;
    last_key = heap->data[index*2];
    last_data = heap->data[index*2+1];
    while (index > 0) {
	parent = (index - 1) >> 1;
	res = heap->comparator(last_key, heap->data[parent*2]);
	if (res > 0) break;
	heap->data[index*2] = heap->data[parent*2];
	heap->data[index*2+1] = heap->data[parent*2+1];
	index = parent;
    }
    heap->data[index*2] = last_key;
    heap->data[index*2+1] = last_data;
}

/*
 *  Insert a key/data pair into the heap.
 */
void
heap_insert(heap_t heap, void *key, void *data)
{
    if (heap->data_used == heap->data_alloc) {
	heap->data_alloc *= 2;
	heap->data = realloc(heap->data, 2*heap->data_alloc*sizeof(void*));
    }
    heap->data[heap->data_used*2] = key;
    heap->data[heap->data_used*2+1] = data;
    heap_heapify_up(heap, heap->data_used++);
}

/*
 *  Return what's on top of the heap.
 *  If the heap is empty, put NULL into *key and *data.
 *  (Either key or data may be NULL, in which case the relevant
 *  data will not be returned to the caller.)
 */
void
heap_peek(heap_t heap, void **key, void **data)
{
    if (key) *key = heap->data_used ? heap->data[0] : NULL;
    if (data) *data = heap->data_used ? heap->data[1] : NULL;
}

/*
 * Push the element at "pos" down the heap as far as it will go.
 */
static void
heap_heapify_down(heap_t heap, int pos)
{
    int res;
    unsigned int child;
    void *last_key, *last_data;
    last_key = heap->data[pos*2];
    last_data = heap->data[pos*2+1];
    /* start at left child */
    while ((child=pos*2+1) < heap->data_used) {
	/* use right child if it exists and is smaller */
	if (child+1 < heap->data_used) {
	    res = heap->comparator(heap->data[(child+1)*2], heap->data[child*2]);
	    if (res < 0) child = child+1;
	}
	res = heap->comparator(last_key, heap->data[child*2]);
	if (res <= 0) break;
	heap->data[pos*2] = heap->data[child*2];
	heap->data[pos*2+1] = heap->data[child*2+1];
	pos = child;
    }
    heap->data[pos*2] = last_key;
    heap->data[pos*2+1] = last_data;
}

/*
 * Remove the element at "index" from the heap (preserving the heap ordering).
 */
static void
heap_remove(heap_t heap, unsigned int index)
{
    /* sanity check */
    if (heap->data_used <= index) return;
    /* swap index with last element */
    heap->data_used--;
    heap->data[index*2] = heap->data[heap->data_used*2];
    heap->data[index*2+1] = heap->data[heap->data_used*2+1];
    /* heapify down if index has children */
    if (heap->data_used >= 2*index+1) heap_heapify_down(heap, index);
    if ((index > 0) && (index < heap->data_used)) heap_heapify_up(heap, index);
}

/*
 *  Pop the topmost element from the heap (preserving the heap ordering).
 */
void
heap_pop(heap_t heap)
{
    heap_remove(heap, 0);
}

/*
 *  Remove all elements from the heap if pred(key, data, extra) returns
 *  non-zero on the element's key/data pair.  Can be abused to iterate
 *  over the entire heap, by always returning 0 from pred.
 *
 *  Returns non-zero if the predicate causes the top of the heap to be
 *  removed.
 */
int
heap_remove_pred(heap_t heap, int (*pred)(void *key, void *data, void *extra), void *extra)
{
    unsigned int pos, rem_first;

    if (heap->data_used == 0) return 0;
    if (pred(heap->data[0], heap->data[1], extra)) {
        heap_remove(heap, 0);
        rem_first = 1;
        pos = 0;
    } else {
        rem_first = 0;
        pos = 1;
    }
    while (pos < heap->data_used) {
	if (pred(heap->data[pos*2], heap->data[pos*2+1], extra)) {
            heap_remove(heap, pos);
            pos = 0;
        } else {
            pos++;
        }
    }
    return rem_first;
}

/*
 *  Remove all entries from a heap.
 */
void
heap_delete(heap_t heap)
{
    free(heap->data);
    free(heap);
}

/*
 *  Return number of entries in the heap.
 */
unsigned int
heap_size(heap_t heap)
{
    return heap->data_used;
}

/* prepackaged comparators */
int
int_comparator(const void *a, const void *b)
{
    return (time_t)a-(time_t)b;
}

/* dict.h - Abstract dictionary type
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

#if !defined(DICT_H)
#define DICT_H

/* helper types */
typedef void (*free_f)(void*);
typedef int (*dict_iterator_f)(const char *key, void *data, void *extra);

/* exposed ONLY for the iteration macros; if you use these, DIE */
struct dict_node {
    const char *key;
    void *data;
    struct dict_node *l, *r, *prev, *next;
};

struct dict {
    free_f free_keys, free_data;
    struct dict_node *root, *first, *last;
    unsigned int count;
};

/* "published" API */
typedef struct dict *dict_t;
typedef struct dict_node *dict_iterator_t;

#define dict_first(DICT) ((DICT) ? (DICT)->first : NULL)
#define iter_key(ITER) ((ITER)->key)
#define iter_data(ITER) ((ITER)->data)
#define iter_next(ITER) ((ITER)->next)

dict_t dict_new(void);
/* dict_foreach returns key of node causing halt (non-zero return from
 * iterator function) */
const char* dict_foreach(dict_t dict, dict_iterator_f it, void *extra);
void dict_insert(dict_t dict, const char *key, void *data);
void dict_set_free_keys(dict_t dict, free_f free_keys);
void dict_set_free_data(dict_t dict, free_f free_data);
unsigned int dict_size(dict_t dict);
/* if present!=NULL, then *present=1 iff node was found (if node is
 * not found, return value is NULL, which may be a valid datum) */
void* dict_find(dict_t dict, const char *key, int *present);
int dict_remove2(dict_t dict, const char *key, int no_dispose);
#define dict_remove(DICT, KEY) dict_remove2(DICT, KEY, 0)
char *dict_sanity_check(dict_t dict);
void dict_delete(dict_t dict);

#endif /* !defined(DICT_H) */

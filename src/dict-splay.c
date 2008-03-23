/* dict-splay.c - Abstract dictionary type
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
 */

#include "common.h"
#include "dict.h"

/*
 *    Create new dictionary.
 */
dict_t
dict_new(void)
{
    dict_t dict = calloc(1, sizeof(*dict));
    return dict;
}

/*
 *    Return number of entries in the dictionary.
 */
unsigned int
dict_size(dict_t dict)
{
    return dict->count;
}

/*
 *    Set the function to be called when freeing a key structure.
 *    If the function is NULL, just forget about the pointer.
 */
void
dict_set_free_keys(dict_t dict, free_f free_keys)
{
    dict->free_keys = free_keys;
}

/*
 *    Set the function to free data.
 * If the function is NULL, just forget about the pointer.
 */
void
dict_set_free_data(dict_t dict, free_f free_data)
{
    dict->free_data = free_data;
}

const char *
dict_foreach(dict_t dict, dict_iterator_f it_f, void *extra)
{
    dict_iterator_t it;

    for (it=dict_first(dict); it; it=iter_next(it)) {
        if (it_f(iter_key(it), iter_data(it), extra)) return iter_key(it);
    }
    return NULL;
}

/*
 *   This function finds a node and pulls it to the top of the tree.
 *   This helps balance the tree and auto-cache things you search for.
 */
static struct dict_node*
dict_splay(struct dict_node *node, const char *key)
{
    struct dict_node N, *l, *r, *y;
    int res;

    if (!node) return NULL;
    N.l = N.r = NULL;
    l = r = &N;

    while (1) {
        verify(node);
        res = irccasecmp(key, node->key);
        if (!res) break;
        if (res < 0) {
            if (!node->l) break;
            res = irccasecmp(key, node->l->key);
            if (res < 0) {
                y = node->l;
                node->l = y->r;
                y->r = node;
                node = y;
                if (!node->l) break;
            }
            r->l = node;
            r = node;
            node = node->l;
        } else { /* res > 0 */
            if (!node->r) break;
            res = irccasecmp(key, node->r->key);
            if (res > 0) {
                y = node->r;
                node->r = y->l;
                y->l = node;
                node = y;
                if (!node->r) break;
            }
            l->r = node;
            l = node;
            node = node->r;
        }
    }
    l->r = node->l;
    r->l = node->r;
    node->l = N.r;
    node->r = N.l;
    return node;
}

/*
 *    Free node.  Free data/key using free_f functions.
 */
static void
dict_dispose_node(struct dict_node *node, free_f free_keys, free_f free_data)
{
    if (free_keys && node->key) {
        if (free_keys == free)
            free((void*)node->key);
        else
            free_keys((void*)node->key);
    }
    if (free_data && node->data) {
        if (free_data == free)
            free(node->data);
        else
            free_data(node->data);
    }
    free(node);
}

/*
 *    Insert an entry into the dictionary.
 *    Key ordering (and uniqueness) is determined by case-insensitive
 *    string comparison.
 */
void
dict_insert(dict_t dict, const char *key, void *data)
{
    struct dict_node *new_node;
    if (!key)
        return;
    verify(dict);
    new_node = malloc(sizeof(struct dict_node));
    new_node->key = key;
    new_node->data = data;
    if (dict->root) {
        int res;
        dict->root = dict_splay(dict->root, key);
        res = irccasecmp(key, dict->root->key);
        if (res < 0) {
            /* insert just "before" current root */
            new_node->l = dict->root->l;
            new_node->r = dict->root;
            dict->root->l = NULL;
            if (dict->root->prev) {
                dict->root->prev->next = new_node;
            } else {
                dict->first = new_node;
            }
            new_node->prev = dict->root->prev;
            new_node->next = dict->root;
            dict->root->prev = new_node;
            dict->root = new_node;
        } else if (res > 0) {
            /* insert just "after" current root */
            new_node->r = dict->root->r;
            new_node->l = dict->root;
            dict->root->r = NULL;
            if (dict->root->next) {
                dict->root->next->prev = new_node;
            } else {
                dict->last = new_node;
            }
            new_node->next = dict->root->next;
            new_node->prev = dict->root;
            dict->root->next = new_node;
            dict->root = new_node;
        } else {
            /* maybe we don't want to overwrite it .. oh well */
            if (dict->free_data) {
                if (dict->free_data == free)
                    free(dict->root->data);
                else
                    dict->free_data(dict->root->data);
            }
            if (dict->free_keys) {
                if (dict->free_keys == free)
                    free((void*)dict->root->key);
                else
                    dict->free_keys((void*)dict->root->key);
            }
            free(new_node);
            dict->root->key = key;
            dict->root->data = data;
            /* decrement the count since we dropped the node */
            dict->count--;
        }
    } else {
        new_node->l = new_node->r = NULL;
        new_node->next = new_node->prev = NULL;
        dict->root = dict->first = dict->last = new_node;
    }
    dict->count++;
}

/*
 *    Remove an entry from the dictionary.
 *    Return non-zero if it was found, or zero if the key was not in the
 *    dictionary.
 */
int
dict_remove2(dict_t dict, const char *key, int no_dispose)
{
    struct dict_node *new_root, *old_root;

    if (!dict->root)
        return 0;
    verify(dict);
    dict->root = dict_splay(dict->root, key);
    if (irccasecmp(key, dict->root->key))
        return 0;

    if (!dict->root->l) {
        new_root = dict->root->r;
    } else {
        new_root = dict_splay(dict->root->l, key);
        new_root->r = dict->root->r;
    }
    if (dict->root->prev) dict->root->prev->next = dict->root->next;
    if (dict->first == dict->root) dict->first = dict->first->next;
    if (dict->root->next) dict->root->next->prev = dict->root->prev;
    if (dict->last == dict->root) dict->last = dict->last->prev;
    old_root = dict->root;
    dict->root = new_root;
    dict->count--;
    if (no_dispose) {
        free(old_root);
    } else {
        dict_dispose_node(old_root, dict->free_keys, dict->free_data);
    }
    return 1;
}

/*
 *    Find an entry in the dictionary.
 *    If "found" is non-NULL, set it to non-zero if the key was found.
 *    Return the data associated with the key (or NULL if the key was
 *    not found).
 */
void*
dict_find(dict_t dict, const char *key, int *found)
{
    int was_found;
    if (!dict || !dict->root || !key) {
        if (found)
            *found = 0;
        return NULL;
    }
    verify(dict);
    dict->root = dict_splay(dict->root, key);
    was_found = !irccasecmp(key, dict->root->key);
    if (found)
        *found = was_found;
    return was_found ? dict->root->data : NULL;
}

/*
 *    Delete an entire dictionary.
 */
void
dict_delete(dict_t dict)
{
    dict_iterator_t it, next;
    if (!dict)
        return;
    verify(dict);
    for (it=dict_first(dict); it; it=next) {
        next = iter_next(it);
        dict_dispose_node(it, dict->free_keys, dict->free_data);
    }
    free(dict);
}

struct dict_sanity_struct {
    unsigned int node_count;
    struct dict_node *bad_node;
    char error[128];
};

static int
dict_sanity_check_node(struct dict_node *node, struct dict_sanity_struct *dss)
{
    verify(node);
    if (!node->key) {
        snprintf(dss->error, sizeof(dss->error), "Node %p had null key", (void*)node);
        return 1;
    }
    if (node->l) {
        if (dict_sanity_check_node(node->l, dss)) return 1;
        if (irccasecmp(node->l->key, node->key) >= 0) {
            snprintf(dss->error, sizeof(dss->error), "Node %p's left child's key '%s' >= its key '%s'", (void*)node, node->l->key, node->key);
            return 1;
        }
    }
    if (node->r) {
        if (dict_sanity_check_node(node->r, dss)) return 1;
        if (irccasecmp(node->key, node->r->key) >= 0) {
            snprintf(dss->error, sizeof(dss->error), "Node %p's right child's key '%s' <= its key '%s'", (void*)node, node->r->key, node->key);
            return 1;
        }
    }
    dss->node_count++;
    return 0;
}

/*
 *    Perform sanity checks on the dict's internal structure.
 */
char *
dict_sanity_check(dict_t dict)
{
    struct dict_sanity_struct dss;
    dss.node_count = 0;
    dss.bad_node = 0;
    dss.error[0] = 0;
    verify(dict);
    if (dict->root && dict_sanity_check_node(dict->root, &dss)) {
        return strdup(dss.error);
    } else if (dss.node_count != dict->count) {
        snprintf(dss.error, sizeof(dss.error), "Counted %d nodes but expected %d.", dss.node_count, dict->count);
        return strdup(dss.error);
    } else {
        return 0;
    }
}

/* recdb.h - recursive/record database implementation
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

#ifndef _recdb_h
#define _recdb_h

#include "common.h"
#include "dict.h"

enum recdb_type {
    RECDB_INVALID,
    RECDB_QSTRING,
    RECDB_OBJECT,
    RECDB_STRING_LIST
};

struct record_data {
    enum recdb_type type;
    union {
        char *qstring;
        dict_t object;
        struct string_list *slist;
        void *whatever;
    } d;
};

#define SET_RECORD_QSTRING(rec, qs) do { const char *s = (qs); (rec)->type = RECDB_QSTRING; (rec)->d.qstring = (s) ? strdup(s) : NULL; } while (0)
#define GET_RECORD_QSTRING(rec) (((rec)->type == RECDB_QSTRING) ? (rec)->d.qstring : 0)
#define SET_RECORD_OBJECT(rec, obj) do { (rec)->type = RECDB_OBJECT; (rec)->d.object = (obj); } while (0)
#define GET_RECORD_OBJECT(rec) (((rec)->type == RECDB_OBJECT) ? (rec)->d.object : 0)
#define SET_RECORD_STRING_LIST(rec, sl) do { (rec)->type = RECDB_STRING_LIST; (rec)->d.slist = (sl); } while (0)
#define GET_RECORD_STRING_LIST(rec) (((rec)->type == RECDB_STRING_LIST) ? (rec)->d.slist : 0)

struct string_list {
    unsigned int used, size;
    char **list;
};
void string_list_append(struct string_list *slist, char *string);
struct string_list *string_list_copy(struct string_list *orig);
void string_list_sort(struct string_list *slist);
#define string_list_delete(slist, n) (free((slist)->list[n]), (slist)->list[n] = (slist)->list[--(slist)->used])

/* allocation functions */
struct string_list *alloc_string_list(int size);
struct record_data *alloc_record_data_qstring(const char *string);
struct record_data *alloc_record_data_object(dict_t obj);
struct record_data *alloc_record_data_string_list(struct string_list *slist);
dict_t alloc_database(void);
#define alloc_object() alloc_database()

/* misc operations */
/* note: once you give a string list a string, it frees it automatically */
struct record_data *database_get_path(dict_t db, const char *path);
void *database_get_data(dict_t db, const char *path, enum recdb_type type);

/* freeing data */
void free_string_list(struct string_list *slist);
void free_record_data(void *rdata);
#define free_object(obj) dict_delete(obj)
#define free_database(db) dict_delete(db)

/* parsing stuff from disk */
const char *parse_record(const char *text, char **pname, struct record_data **prd);
dict_t parse_database(const char *filename);

#endif

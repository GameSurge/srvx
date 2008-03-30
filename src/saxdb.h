/* saxdb.h - srvx database manager
 * Copyright 2002-2004 srvx Development Team
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

#if !defined(DBMGR_H)
#define DBMGR_H

#include "recdb.h"

DECLARE_LIST(int_list, int);

struct saxdb;
struct saxdb_context;

#define SAXDB_READER(NAME) int NAME(struct dict *db)
typedef SAXDB_READER(saxdb_reader_func_t);

#define SAXDB_WRITER(NAME) int NAME(struct saxdb_context *ctx)
typedef SAXDB_WRITER(saxdb_writer_func_t);

void saxdb_init(void);
void saxdb_finalize(void);
struct saxdb *saxdb_register(const char *name, saxdb_reader_func_t *reader, saxdb_writer_func_t *writer);
void saxdb_write(const char *db_name);
void saxdb_write_all(void);
int write_database(FILE *out, struct dict *db);

/* Callbacks for SAXDB_WRITERs */
void saxdb_start_record(struct saxdb_context *dest, const char *name, int complex);
void saxdb_end_record(struct saxdb_context *dest);
void saxdb_write_string_list(struct saxdb_context *dest, const char *name, struct string_list *list);
void saxdb_write_string(struct saxdb_context *dest, const char *name, const char *value);
void saxdb_write_int(struct saxdb_context *dest, const char *name, unsigned long value);
void saxdb_write_sint(struct saxdb_context *dest, const char *name, long value);

/* For doing db writing by hand */
struct saxdb_context *saxdb_open_context(FILE *f);
jmp_buf *saxdb_jmp_buf(struct saxdb_context *ctx);
void saxdb_close_context(struct saxdb_context *ctx, int close_file);

#endif /* !defined(DBMGR_H) */

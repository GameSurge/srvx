/* saxdb.c - srvx database manager
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

#include "conf.h"
#include "hash.h"
#include "modcmd.h"
#include "saxdb.h"
#include "timeq.h"

DEFINE_LIST(int_list, int);

struct saxdb {
    char *name;
    char *filename;
    char *mondo_section;
    saxdb_reader_func_t *reader;
    saxdb_writer_func_t *writer;
    unsigned int write_interval;
    time_t last_write;
    unsigned int last_write_duration;
    struct saxdb *prev;
};

#define COMPLEX(CTX) ((CTX)->complex.used ? ((CTX)->complex.list[(CTX)->complex.used-1]) : 1)

static struct saxdb *last_db;
static struct dict *saxdbs; /* -> struct saxdb */
static struct dict *mondo_db;
static struct module *saxdb_module;

static SAXDB_WRITER(saxdb_mondo_writer);
static void saxdb_timed_write(void *data);

static void
saxdb_read_db(struct saxdb *db) {
    struct dict *data;

    assert(db);
    assert(db->filename);
    data = parse_database(db->filename);
    if (!data)
        return;
    if (db->writer == saxdb_mondo_writer) {
        mondo_db = data;
    } else {
        db->reader(data);
        free_database(data);
    }
}

struct saxdb *
saxdb_register(const char *name, saxdb_reader_func_t *reader, saxdb_writer_func_t *writer) {
    struct saxdb *db;
    struct dict *conf;
    int ii;
    const char *filename = NULL, *str;
    char conf_path[MAXLEN];

    db = calloc(1, sizeof(*db));
    db->name = strdup(name);
    db->reader = reader;
    db->writer = writer;
    /* Look up configuration */
    sprintf(conf_path, "dbs/%s", name);
    if ((conf = conf_get_data(conf_path, RECDB_OBJECT))) {
        if ((str = database_get_data(conf, "mondo_section", RECDB_QSTRING))) {
            db->mondo_section = strdup(str);
        }
        str = database_get_data(conf, "frequency", RECDB_QSTRING);
        db->write_interval = str ? ParseInterval(str) : 1800;
        filename = database_get_data(conf, "filename", RECDB_QSTRING);
    } else {
        db->write_interval = 1800;
    }
    /* Schedule database writes */
    if (db->write_interval && !db->mondo_section) {
        timeq_add(now + db->write_interval, saxdb_timed_write, db);
    }
    /* Insert filename */
    if (filename) {
        db->filename = strdup(filename);
    } else {
        db->filename = malloc(strlen(db->name)+4);
        for (ii=0; db->name[ii]; ++ii) db->filename[ii] = tolower(db->name[ii]);
        strcpy(db->filename+ii, ".db");
    }
    /* Read from disk (or mondo DB) */
    if (db->mondo_section) {
        if (mondo_db && (conf = database_get_data(mondo_db, db->mondo_section, RECDB_OBJECT))) {
            db->reader(conf);
        }
    } else {
        saxdb_read_db(db);
    }
    /* Remember the database */
    dict_insert(saxdbs, db->name, db);
    db->prev = last_db;
    last_db = db;
    return db;
}

static int
saxdb_write_db(struct saxdb *db) {
    struct saxdb_context ctx;
    char tmp_fname[MAXLEN];
    int res, res2;
    time_t start, finish;

    assert(db->filename);
    sprintf(tmp_fname, "%s.new", db->filename);
    memset(&ctx, 0, sizeof(ctx));
    ctx.output = fopen(tmp_fname, "w+");
    int_list_init(&ctx.complex);
    if (!ctx.output) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to write to %s: %s", tmp_fname, strerror(errno));
        int_list_clean(&ctx.complex);
        return 1;
    }
    start = time(NULL);
    if ((res = setjmp(ctx.jbuf)) || (res2 = db->writer(&ctx))) {
        if (res) {
            log_module(MAIN_LOG, LOG_ERROR, "Error writing to %s: %s", tmp_fname, strerror(res));
        } else {
            log_module(MAIN_LOG, LOG_ERROR, "Internal error %d while writing to %s", res2, tmp_fname);
        }
        int_list_clean(&ctx.complex);
        fclose(ctx.output);
        remove(tmp_fname);
        return 2;
    }
    finish = time(NULL);
    assert(ctx.complex.used == 0);
    int_list_clean(&ctx.complex);
    fclose(ctx.output);
    if (rename(tmp_fname, db->filename) < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to rename %s to %s: %s", tmp_fname, db->filename, strerror(errno));
    }
    db->last_write = now;
    db->last_write_duration = finish - start;
    log_module(MAIN_LOG, LOG_INFO, "Wrote %s database to disk.", db->name);
    return 0;
}

static void
saxdb_timed_write(void *data) {
    struct saxdb *db = data;
    saxdb_write_db(db);
    timeq_add(now + db->write_interval, saxdb_timed_write, db);
}

void
saxdb_write(const char *db_name) {
    struct saxdb *db;
    db = dict_find(saxdbs, db_name, NULL);
    if (db) saxdb_write_db(db);
}

void
saxdb_write_all(void) {
    dict_iterator_t it;
    struct saxdb *db;

    for (it = dict_first(saxdbs); it; it = iter_next(it)) {
        db = iter_data(it);
        if (!db->mondo_section)
            saxdb_write_db(db);
    }
}

#define saxdb_put_char(DEST, CH) do { \
    if (fputc(CH, (DEST)->output) == EOF) \
        longjmp((DEST)->jbuf, errno); \
    } while (0)
#define saxdb_put_string(DEST, CH) do { \
    if (fputs(CH, (DEST)->output) == EOF) \
        longjmp((DEST)->jbuf, errno); \
    } while (0)

static inline void
saxdb_put_nchars(struct saxdb_context *dest, const char *name, int len) {
    while (len--)
        if (fputc(*name++, dest->output) == EOF)
            longjmp(dest->jbuf, errno);
}

static void
saxdb_put_qstring(struct saxdb_context *dest, const char *str) {
    const char *esc;

    assert(str);
    saxdb_put_char(dest, '"');
    while ((esc = strpbrk(str, "\\\a\b\t\n\v\f\r\""))) {
        if (esc != str)
            saxdb_put_nchars(dest, str, esc-str);
        saxdb_put_char(dest, '\\');
        switch (*esc) {
        case '\a': saxdb_put_char(dest, 'a'); break;
        case '\b': saxdb_put_char(dest, 'b'); break;
        case '\t': saxdb_put_char(dest, 't'); break;
        case '\n': saxdb_put_char(dest, 'n'); break;
        case '\v': saxdb_put_char(dest, 'v'); break;
        case '\f': saxdb_put_char(dest, 'f'); break;
        case '\r': saxdb_put_char(dest, 'r'); break;
        case '\\': saxdb_put_char(dest, '\\'); break;
        case '"': saxdb_put_char(dest, '"'); break;
        }
        str = esc + 1;
    }
    saxdb_put_string(dest, str);
    saxdb_put_char(dest, '"');
}

#ifndef NDEBUG
static void
saxdb_pre_object(struct saxdb_context *dest) {
    unsigned int ii;
    if (COMPLEX(dest)) {
        for (ii=0; ii<dest->indent; ++ii) saxdb_put_char(dest, '\t');
    }
}
#else
#define saxdb_pre_object(DEST) 
#endif

static inline void
saxdb_post_object(struct saxdb_context *dest) {
    saxdb_put_char(dest, ';');
    saxdb_put_char(dest, COMPLEX(dest) ? '\n' : ' ');
}

void
saxdb_start_record(struct saxdb_context *dest, const char *name, int complex) {
    saxdb_pre_object(dest);
    saxdb_put_qstring(dest, name);
    saxdb_put_string(dest, " { ");
    int_list_append(&dest->complex, complex);
    if (complex) {
        dest->indent++;
        saxdb_put_char(dest, '\n');
    }
}

void
saxdb_end_record(struct saxdb_context *dest) {
    assert(dest->complex.used > 0);
    if (COMPLEX(dest)) dest->indent--;
    saxdb_pre_object(dest);
    dest->complex.used--;
    saxdb_put_char(dest, '}');
    saxdb_post_object(dest);
}

void
saxdb_write_string_list(struct saxdb_context *dest, const char *name, struct string_list *list) {
    unsigned int ii;

    saxdb_pre_object(dest);
    saxdb_put_qstring(dest, name);
    saxdb_put_string(dest, " (");
    if (list->used) {
        for (ii=0; ii<list->used-1; ++ii) {
            saxdb_put_qstring(dest, list->list[ii]);
            saxdb_put_string(dest, ", ");
        }
        saxdb_put_qstring(dest, list->list[list->used-1]);
    }
    saxdb_put_string(dest, ")");
    saxdb_post_object(dest);
}

void
saxdb_write_string(struct saxdb_context *dest, const char *name, const char *value) {
    saxdb_pre_object(dest);
    saxdb_put_qstring(dest, name);
    saxdb_put_char(dest, ' ');
    saxdb_put_qstring(dest, value);
    saxdb_post_object(dest);
}

void
saxdb_write_int(struct saxdb_context *dest, const char *name, unsigned long value) {
    unsigned char buf[16];
    /* we could optimize this to take advantage of the fact that buf will never need escapes */
    snprintf(buf, sizeof(buf), "%lu", value);
    saxdb_write_string(dest, name, buf);
}

static void
saxdb_free(void *data) {
    struct saxdb *db = data;
    free(db->name);
    free(db->filename);
    free(db->mondo_section);
    free(db);
}

static int
saxdb_mondo_read(struct dict *db, struct saxdb *saxdb) {
    int res;
    struct dict *subdb;

    if (saxdb->prev && (res = saxdb_mondo_read(db, saxdb->prev))) return res;
    if (saxdb->mondo_section
        && (subdb = database_get_data(db, saxdb->mondo_section, RECDB_OBJECT))
        && (res = saxdb->reader(subdb))) {
        log_module(MAIN_LOG, LOG_INFO, " mondo section read for %s failed: %d", saxdb->mondo_section, res);
        return res;
    }
    return 0;
}

static SAXDB_READER(saxdb_mondo_reader) {
    return saxdb_mondo_read(db, last_db);
}

static int
saxdb_mondo_write(struct saxdb_context *ctx, struct saxdb *saxdb) {
    int res;
    if (saxdb->prev && (res = saxdb_mondo_write(ctx, saxdb->prev))) return res;
    if (saxdb->mondo_section) {
        saxdb_start_record(ctx, saxdb->mondo_section, 1);
        if ((res = saxdb->writer(ctx))) {
            log_module(MAIN_LOG, LOG_INFO, " mondo section write for %s failed: %d", saxdb->mondo_section, res);
            return res;
        }
        saxdb_end_record(ctx);
        /* cheat a little here to put a newline between mondo sections */
        saxdb_put_char(ctx, '\n');
    }
    return 0;
}

static SAXDB_WRITER(saxdb_mondo_writer) {
    return saxdb_mondo_write(ctx, last_db);
}

static MODCMD_FUNC(cmd_write) {
    struct timeval start, stop;
    unsigned int ii, written;
    struct saxdb *db;

    assert(argc >= 2);
    written = 0;
    for (ii=1; ii<argc; ++ii) {
        if (!(db = dict_find(saxdbs, argv[ii], NULL))) {
            reply("MSG_DB_UNKNOWN", argv[ii]);
            continue;
        }
        if (db->mondo_section) {
            reply("MSG_DB_IS_MONDO", db->name);
            continue;
        }
        gettimeofday(&start, NULL);
        if (saxdb_write_db(db)) {
            reply("MSG_DB_WRITE_ERROR", db->name);
        } else {
            gettimeofday(&stop, NULL);
            stop.tv_sec -= start.tv_sec;
            stop.tv_usec -= start.tv_usec;
            if (stop.tv_usec < 0) {
                stop.tv_sec -= 1;
                stop.tv_usec += 1000000;
            }
            reply("MSG_DB_WROTE_DB", db->name, stop.tv_sec, stop.tv_usec);
            written++;
        }
    }
    return written;
}

static MODCMD_FUNC(cmd_writeall) {
    struct timeval start, stop;

    gettimeofday(&start, NULL);
    saxdb_write_all();
    gettimeofday(&stop, NULL);
    stop.tv_sec -= start.tv_sec;
    stop.tv_usec -= start.tv_usec;
    if (stop.tv_usec < 0) {
        stop.tv_sec -= 1;
        stop.tv_usec += 1000000;
    }
    reply("MSG_DB_WROTE_ALL", stop.tv_sec, stop.tv_usec);
    return 1;
}

static MODCMD_FUNC(cmd_stats_databases) {
    struct helpfile_table tbl;
    dict_iterator_t it;
    unsigned int ii;

    tbl.length = dict_size(saxdbs) + 1;
    tbl.width = 5;
    tbl.flags = TABLE_NO_FREE;
    tbl.contents = calloc(tbl.length, sizeof(tbl.contents[0]));
    tbl.contents[0] = calloc(tbl.width, sizeof(tbl.contents[0][0]));
    tbl.contents[0][0] = "Database";
    tbl.contents[0][1] = "Filename/Section";
    tbl.contents[0][2] = "Interval";
    tbl.contents[0][3] = "Last Written";
    tbl.contents[0][4] = "Last Duration";
    for (ii=1, it=dict_first(saxdbs); it; it=iter_next(it), ++ii) {
        struct saxdb *db = iter_data(it);
        char *buf = malloc(INTERVALLEN*3);
        tbl.contents[ii] = calloc(tbl.width, sizeof(tbl.contents[ii][0]));
        tbl.contents[ii][0] = db->name;
        tbl.contents[ii][1] = db->mondo_section ? db->mondo_section : db->filename;
        if (db->write_interval) {
            intervalString(buf, db->write_interval, user->handle_info);
        } else {
            strcpy(buf, "Never");
        }
        tbl.contents[ii][2] = buf;
        if (db->last_write) {
            intervalString(buf+INTERVALLEN, now - db->last_write, user->handle_info);
            intervalString(buf+INTERVALLEN*2, db->last_write_duration, user->handle_info);
        } else {
            strcpy(buf+INTERVALLEN, "Never");
            strcpy(buf+INTERVALLEN*2, "Never");
        }
        tbl.contents[ii][3] = buf+INTERVALLEN;
        tbl.contents[ii][4] = buf+INTERVALLEN*2;
    }
    table_send(cmd->parent->bot, user->nick, 0, 0, tbl);
    free(tbl.contents[0]);
    for (ii=1; ii<tbl.length; ++ii) {
        free((char*)tbl.contents[ii][2]);
        free(tbl.contents[ii]);
    }
    free(tbl.contents);
    return 0;
}

static void
saxdb_cleanup(void) {
    dict_delete(saxdbs);
}

static struct helpfile_expansion
saxdb_expand_help(const char *variable) {
    struct helpfile_expansion exp;
    if (!strcasecmp(variable, "dblist")) {
        dict_iterator_t it;
        struct string_buffer sbuf;
        struct saxdb *db;

        exp.type = HF_STRING;
        string_buffer_init(&sbuf);
        for (it = dict_first(saxdbs); it; it = iter_next(it)) {
            db = iter_data(it);
            if (db->mondo_section) continue;
            if (sbuf.used) string_buffer_append_string(&sbuf, ", ");
            string_buffer_append_string(&sbuf, iter_key(it));
        }
        exp.value.str = sbuf.list;
    } else {
        exp.type = HF_STRING;
        exp.value.str = NULL;
    }
    return exp;
}

void
saxdb_init(void) {
    reg_exit_func(saxdb_cleanup);
    saxdbs = dict_new();
    dict_set_free_data(saxdbs, saxdb_free);
    saxdb_register("mondo", saxdb_mondo_reader, saxdb_mondo_writer);
    saxdb_module = module_register("saxdb", MAIN_LOG, "saxdb.help", saxdb_expand_help);
    modcmd_register(saxdb_module, "write", cmd_write, 2, MODCMD_REQUIRE_AUTHED, "level", "800", NULL);
    modcmd_register(saxdb_module, "writeall", cmd_writeall, 0, MODCMD_REQUIRE_AUTHED, "level", "800", NULL);
    modcmd_register(saxdb_module, "stats databases", cmd_stats_databases, 0, 0, NULL);
}

void
saxdb_finalize(void) {
    free_database(mondo_db);
}

static void
write_database_helper(struct saxdb_context *ctx, struct dict *db) {
    dict_iterator_t it;
    struct record_data *rd;

    for (it = dict_first(db); it; it = iter_next(it)) {
        rd = iter_data(it);
        switch (rd->type) {
        case RECDB_INVALID: break;
        case RECDB_QSTRING: saxdb_write_string(ctx, iter_key(it), rd->d.qstring); break;
        case RECDB_STRING_LIST: saxdb_write_string_list(ctx, iter_key(it), rd->d.slist); break;
        case RECDB_OBJECT:
            saxdb_start_record(ctx, iter_key(it), 1);
            write_database_helper(ctx, rd->d.object);
            saxdb_end_record(ctx);
            break;
        }
    }
}

int
write_database(FILE *out, struct dict *db) {
    struct saxdb_context ctx;
    int res;

    ctx.output = out;
    ctx.indent = 0;
    int_list_init(&ctx.complex);
    if (!(res = setjmp(ctx.jbuf))) {
        write_database_helper(&ctx, db);
    } else {
        log_module(MAIN_LOG, LOG_ERROR, "Exception %d caught while writing to stream", res);
        int_list_clean(&ctx.complex);
        return 1;
    }
    assert(ctx.complex.used == 0);
    int_list_clean(&ctx.complex);
    return 0;
}

struct saxdb_context *
saxdb_open_context(FILE *file) {
    struct saxdb_context *ctx;

    assert(file);
    ctx = calloc(1, sizeof(*ctx));
    ctx->output = file;
    int_list_init(&ctx->complex);

    return ctx;
}

void
saxdb_close_context(struct saxdb_context *ctx) {
    assert(ctx->complex.used == 0);
    int_list_clean(&ctx->complex);
    free(ctx);
}

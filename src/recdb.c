/* recdb.c - recursive/record database implementation
 * Copyright 2000-2004 srvx Development Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.  Important limitations are
 * listed in the COPYING file that accompanies this software.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, email srvx-maintainers@srvx.net.
 */

#include "recdb.h"
#include "log.h"

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

/* 4 MiB on x86 */
#define MMAP_MAP_LENGTH (getpagesize()*1024)

/* file format (grammar in Backus-Naur Form):
 *
 * database := record*
 * record := qstring [ '=' ] ( qstring | object | stringlist ) ';'
 * qstring := '"' ( [^\\] | ('\\' [\\n]) )* '"'
 * object := '{' record* '}'
 * stringlist := '(' [ qstring [',' qstring]* ] ')'
 *
 */

/* when a database or object is read from disk, it is represented as
 * a dictionary object, keys are names (what's left of the '=') and
 * values are 'struct record_data's
 */

struct recdb_context {
    int line;
    int col;
};

enum recdb_filetype {
    RECDB_FILE,
    RECDB_STRING,
    RECDB_MMAP
};

typedef struct recdb_file {
    const char *source;
    FILE *f; /* For RECDB_FILE, RECDB_MMAP */
    char *s; /* For RECDB_STRING, RECDB_MMAP */
    enum recdb_filetype type;
    size_t length;
    off_t pos;
    struct recdb_context ctx;
    jmp_buf env;
} RECDB;

typedef struct recdb_outfile {
    FILE *f; /* For RECDB_FILE, RECDB_MMAP */
    char *s; /* For RECDB_STRING, RECDB_MMAP */
    union {
        struct { /* For RECDB_STRING */
            size_t chunksize;
            size_t alloc_length;
        } s;
        struct { /* For RECDB_MMAP */
            off_t mmap_begin;
            size_t mmap_length;
        } m;
    } state;
    enum recdb_filetype type;
    off_t pos;
    int tablvl;
#ifdef NDEBUG
    int need_tab;
#endif
} RECDB_OUT;

#ifdef HAVE_MMAP
static int mmap_error=0;
#endif

#define EOL '\n'

#if 1
#define ABORT(recdb, code, ch) longjmp((recdb)->env, ((code) << 8) | (ch))
#else
static void
ABORT(RECDB *recdb, int code, unsigned char ch) {
    longjmp(recdb->env, code << 8 | ch);
}
#endif

enum fail_codes {
    UNTERMINATED_STRING,
    EXPECTED_OPEN_QUOTE,
    EXPECTED_OPEN_BRACE,
    EXPECTED_OPEN_PAREN,
    EXPECTED_COMMA,
    EXPECTED_START_RECORD_DATA,
    EXPECTED_SEMICOLON,
    EXPECTED_RECORD_DATA
};

static void parse_record_int(RECDB *recdb, char **pname, struct record_data **prd);

/* allocation functions */

#define alloc_record_data_int() malloc(sizeof(struct record_data))

struct record_data *
alloc_record_data_qstring(const char *string)
{
    struct record_data *rd;
    rd = alloc_record_data_int();
    SET_RECORD_QSTRING(rd, string);
    return rd;
}

struct record_data *
alloc_record_data_object(dict_t obj)
{
    struct record_data *rd;
    rd = alloc_record_data_int();
    SET_RECORD_OBJECT(rd, obj);
    return rd;
}

struct record_data *
alloc_record_data_string_list(struct string_list *slist)
{
    struct record_data *rd;
    rd = alloc_record_data_int();
    SET_RECORD_STRING_LIST(rd, slist);
    return rd;
}

struct string_list*
alloc_string_list(int size)
{
    struct string_list *slist;
    slist = malloc(sizeof(struct string_list));
    slist->used = 0;
    slist->size = size;
    slist->list = slist->size ? malloc(size*sizeof(char*)) : NULL;
    return slist;
}

dict_t
alloc_database(void)
{
    dict_t db = dict_new();
    dict_set_free_data(db, free_record_data);
    return db;
}

/* misc. operations */

void
string_list_append(struct string_list *slist, char *string)
{
    if (slist->used == slist->size) {
        if (slist->size) {
            slist->size <<= 1;
            slist->list = realloc(slist->list, slist->size*sizeof(char*));
        } else {
            slist->size = 4;
            slist->list = malloc(slist->size*sizeof(char*));
        }
    }
    slist->list[slist->used++] = string;
}

struct string_list *
string_list_copy(struct string_list *slist)
{
    struct string_list *new_list;
    unsigned int i;
    new_list = alloc_string_list(slist->size);
    new_list->used = slist->used;
    for (i=0; i<new_list->used; i++) {
        new_list->list[i] = strdup(slist->list[i]);
    }
    return new_list;
}

int slist_compare_two(const void *pa, const void *pb)
{
    return irccasecmp(*(const char**)pa, *(const char **)pb);
}

void
string_list_sort(struct string_list *slist)
{
    qsort(slist->list, slist->used, sizeof(slist->list[0]), slist_compare_two);
}

struct record_data*
database_get_path(dict_t db, const char *path)
{
    char *new_path = strdup(path), *orig_path = new_path;
    char *part;
    struct record_data *rd;

    for (part=new_path; *new_path; new_path++) {
        if (*new_path != '/') continue;
        *new_path = 0;

        rd = dict_find(db, part, NULL);
        if (!rd || rd->type != RECDB_OBJECT) {
            free(orig_path);
            return NULL;
        }

        db = rd->d.object;
        part = new_path+1;
    }

    rd = dict_find(db, part, NULL);
    free(orig_path);
    return rd;
}

void*
database_get_data(dict_t db, const char *path, enum recdb_type type)
{
    struct record_data *rd = database_get_path(db, path);
    return (rd && rd->type == type) ? rd->d.whatever : NULL;
}

/* free functions */

void
free_string_list(struct string_list *slist)
{
    unsigned int i;
    if (!slist)
        return;
    for (i=0; i<slist->used; i++)
        free(slist->list[i]);
    free(slist->list);
    free(slist);
}

void
free_record_data(void *rdata)
{
    struct record_data *r = rdata;
    switch (r->type) {
    case RECDB_INVALID: break;
    case RECDB_QSTRING: free(r->d.qstring); break;
    case RECDB_OBJECT: dict_delete(r->d.object); break;
    case RECDB_STRING_LIST: free_string_list(r->d.slist); break;
    }
    free(r);
}

/* parse functions */

static int
dbeof(RECDB *recdb)
{
    switch (recdb->type) {
        case RECDB_FILE:
            return feof(recdb->f);
            break;
        case RECDB_STRING:
            return !*recdb->s;
            break;
        case RECDB_MMAP:
            return ((size_t)recdb->pos >= recdb->length);
            break;
        default:
            return 1;
            break;
    }
}

static int
dbgetc(RECDB *recdb)
{
    int res;
    switch (recdb->type) {
        case RECDB_FILE:
            res = fgetc(recdb->f);
            break;
        case RECDB_STRING:
        case RECDB_MMAP:
            res = dbeof(recdb) ? EOF : recdb->s[recdb->pos++];
            break;
        default:
            res = EOF;
            break;
    }
    if (res == EOL) recdb->ctx.line++, recdb->ctx.col=1;
    else if (res != EOF) recdb->ctx.col++;
    return res;
}

static void
dbungetc(int c, RECDB *recdb)
{
    switch (recdb->type) {
        case RECDB_FILE:
            ungetc(c, recdb->f);
            break;
        case RECDB_STRING:
        case RECDB_MMAP:
            recdb->s[--recdb->pos] = c;
            break;
    }
    if (c == EOL) recdb->ctx.line--, recdb->ctx.col=-1;
    else recdb->ctx.col--;
}

/* returns first non-whitespace, non-comment character (-1 for EOF found) */
int
parse_skip_ws(RECDB *recdb)
{
    int c, d, in_comment = 0;
    while (!dbeof(recdb)) {
        c = dbgetc(recdb);
        if (c == EOF) return EOF;
        if (isspace(c)) continue;
        if (c != '/') return c;
        if ((d = dbgetc(recdb)) == '*') {
            /* C style comment, with slash star comment star slash */
            in_comment = 1;
            do {
                do {
                    c = dbgetc(recdb);
                } while (c != '*' && c != EOF);
                if ((c = dbgetc(recdb)) == '/') in_comment = 0;
            } while (in_comment);
        } else if (d == '/') {
            /* C++ style comment, with slash slash comment newline */
            do {
                c = dbgetc(recdb);
            } while (c != EOF && c != EOL);
        } else {
            if (d != EOF) dbungetc(d, recdb);
            return c;
        }
    }
    return -1;
}

char *
parse_qstring(RECDB *recdb)
{
    char *buff;
    int used=0, size=8, c;
    struct recdb_context start_ctx;
    unsigned int i;

    if ((c = parse_skip_ws(recdb)) == EOF) return NULL;
    start_ctx = recdb->ctx;
    if (c != '"') ABORT(recdb, EXPECTED_OPEN_QUOTE, c);
    buff = malloc(size);
    while (!dbeof(recdb) && (c = dbgetc(recdb)) != '"') {
        if (c != '\\') {
            /* There should never be a literal newline, as it is saved as a \n */
            if (c == EOL) {
                dbungetc(c, recdb);
                ABORT(recdb, UNTERMINATED_STRING, ' ');
            }
            buff[used++] = c;
        } else {
            switch (c = dbgetc(recdb)) {
                case '0': /* \<octal>, 000 through 377 */
                case '1':
                case '2':
                case '3':
                case '4':
                case '5':
                case '6':
                case '7':
                    {
                        char digits[3] = { (char)c, '\0', '\0' };
                        for (i=1; i < 3; i++) {
                            /* Maximum of \377, so there's a max of 2 digits
                             * if digits[0] > '3' (no \400, but \40 is fine) */
                            if (i == 2 && digits[0] > '3') {
                                break;
                            }
                            if ((c = dbgetc(recdb)) == EOF) {
                                break;
                            }
                            if ((c < '0') || (c > '7')) {
                                dbungetc(c, recdb);
                                break;
                            }
                            digits[i] = (char)c;
                        }
                        if (i) {
                            c = (int)strtol(digits, NULL, 8);
                            buff[used++] = c;
                        } else {
                            buff[used++] = '\0';
                        }
                    }
                    break;
                case 'x': /* Hex */
                    {
                        char digits[3] = { '\0', '\0', '\0' };
                        for (i=0; i < 2; i++) {
                            if ((c = dbgetc(recdb)) == EOF) {
                                break;
                            }
                            if (!isxdigit(c)) {
                                dbungetc(c, recdb);
                                break;
                            }
                            digits[i] = (char)c;
                        }
                        if (i) {
                            c = (int)strtol(digits, NULL, 16);
                            buff[used++] = c;
                        } else {
                            buff[used++] = '\\';
                            buff[used++] = 'x';
                        }
                    }
                    break;
                case 'a': buff[used++] = '\a'; break;
                case 'b': buff[used++] = '\b'; break;
                case 't': buff[used++] = '\t'; break;
                case 'n': buff[used++] = EOL; break;
                case 'v': buff[used++] = '\v'; break;
                case 'f': buff[used++] = '\f'; break;
                case 'r': buff[used++] = '\r'; break;
                case '\\': buff[used++] = '\\'; break;
                case '"': buff[used++] = '"'; break;
                default: buff[used++] = '\\'; buff[used++] = c; break;
            }
        }
        if (used == size) {
            size <<= 1;
            buff = realloc(buff, size);
        }
    }
    if (c != '"' && dbeof(recdb)) {
        free(buff);
        recdb->ctx = start_ctx;
        ABORT(recdb, UNTERMINATED_STRING, EOF);
    }
    buff[used] = 0;
    return buff;
}

dict_t
parse_object(RECDB *recdb)
{
    dict_t obj;
    char *name;
    struct record_data *rd;
    int c;
    if ((c = parse_skip_ws(recdb)) == EOF) return NULL;
    if (c != '{') ABORT(recdb, EXPECTED_OPEN_BRACE, c);
    obj = alloc_object();
    dict_set_free_keys(obj, free);
    while (!dbeof(recdb)) {
        if ((c = parse_skip_ws(recdb)) == '}') break;
        if (c == EOF) break;
        dbungetc(c, recdb);
        parse_record_int(recdb, &name, &rd);
        dict_insert(obj, name, rd);
    }
    return obj;
}

struct string_list *
parse_string_list(RECDB *recdb)
{
    struct string_list *slist;
    int c;
    if ((c = parse_skip_ws(recdb)) == EOF) return NULL;
    if (c != '(') ABORT(recdb, EXPECTED_OPEN_PAREN, c);
    slist = alloc_string_list(4);
    while (true) {
        c = parse_skip_ws(recdb);
        if (c == EOF || c == ')') break;
        dbungetc(c, recdb);
        string_list_append(slist, parse_qstring(recdb));
        c = parse_skip_ws(recdb);
        if (c == EOF || c == ')') break;
        if (c != ',') ABORT(recdb, EXPECTED_COMMA, c);
    }
    return slist;
}

static void
parse_record_int(RECDB *recdb, char **pname, struct record_data **prd)
{
    int c;
    *pname = parse_qstring(recdb);
    c = parse_skip_ws(recdb);
    if (c == EOF) {
        if (!*pname) return;
        free(*pname);
        ABORT(recdb, EXPECTED_RECORD_DATA, EOF);
    }
    if (c == '=') c = parse_skip_ws(recdb);
    dbungetc(c, recdb);
    *prd = malloc(sizeof(**prd));
    switch (c) {
    case '"':
        /* Don't use SET_RECORD_QSTRING, since that does an extra strdup() of the string. */
        (*prd)->type = RECDB_QSTRING;
        (*prd)->d.qstring = parse_qstring(recdb);
        break;
    case '{': SET_RECORD_OBJECT(*prd, parse_object(recdb)); break;
    case '(': SET_RECORD_STRING_LIST(*prd, parse_string_list(recdb)); break;
    default: ABORT(recdb, EXPECTED_START_RECORD_DATA, c);
    }
    if ((c = parse_skip_ws(recdb)) != ';') ABORT(recdb, EXPECTED_SEMICOLON, c);
}

static dict_t
parse_database_int(RECDB *recdb)
{
    char *name;
    struct record_data *rd;
    dict_t db = alloc_database();
    dict_set_free_keys(db, free);
    while (!dbeof(recdb)) {
        parse_record_int(recdb, &name, &rd);
        if (name) dict_insert(db, name, rd);
    }
    return db;
}

const char *
failure_reason(int code)
{
    const char *reason;
    switch (code >> 8) {
    case UNTERMINATED_STRING: reason = "Unterminated string"; break;
    case EXPECTED_OPEN_QUOTE: reason = "Expected '\"'"; break;
    case EXPECTED_OPEN_BRACE: reason = "Expected '{'"; break;
    case EXPECTED_OPEN_PAREN: reason = "Expected '('"; break;
    case EXPECTED_COMMA: reason = "Expected ','"; break;
    case EXPECTED_START_RECORD_DATA: reason = "Expected start of some record data"; break;
    case EXPECTED_SEMICOLON: reason = "Expected ';'"; break;
    case EXPECTED_RECORD_DATA: reason = "Expected record data"; break;
    default: reason = "Unknown error";
    }
    if (code == -1) reason = "Premature end of file";
    return reason;
}

void
explain_failure(RECDB *recdb, int code)
{
    log_module(MAIN_LOG, LOG_ERROR, "%s (got '%c') at %s line %d column %d.",
               failure_reason(code), code & 255,
               recdb->source, recdb->ctx.line, recdb->ctx.col);
}

const char *
parse_record(const char *text, char **pname, struct record_data **prd)
{
    RECDB recdb;
    int res;
    *pname = NULL;
    *prd = NULL;
    recdb.source = "<user-supplied text>";
    recdb.f = NULL;
    recdb.s = strdup(text);
    recdb.length = strlen(text);
    recdb.pos = 0;
    recdb.type = RECDB_STRING;
    recdb.ctx.line = recdb.ctx.col = 1;
    if ((res = setjmp(recdb.env)) == 0) {
        parse_record_int(&recdb, pname, prd);
        return 0;
    } else {
        free(*pname);
        free(*prd);
        return failure_reason(res);
    }
}

dict_t
parse_database(const char *filename)
{
    RECDB recdb;
    int res;
    dict_t db;
    struct stat statinfo;

    recdb.source = filename;
    if (!(recdb.f = fopen(filename, "r"))) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to open database file '%s' for reading: %s", filename, strerror(errno));
        return NULL;
    }

    if (fstat(fileno(recdb.f), &statinfo)) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to fstat database file '%s': %s", filename, strerror(errno));
        return NULL;
    }
    recdb.length = (size_t)statinfo.st_size;

#ifdef HAVE_MMAP
    /* Try mmap */
    if (!mmap_error && (recdb.s = mmap(NULL, recdb.length, PROT_READ|PROT_WRITE, MAP_PRIVATE, fileno(recdb.f), 0)) != MAP_FAILED) {
        recdb.type = RECDB_MMAP;
    } else {
        /* Fall back to stdio */
        if (!mmap_error) {
            log_module(MAIN_LOG, LOG_WARNING, "Unable to mmap database file '%s' (falling back to stdio): %s", filename, strerror(errno));
            mmap_error = 1;
        }
#else
    if (1) {
#endif
        recdb.s = NULL;
        recdb.type = RECDB_FILE;
    }

    recdb.ctx.line = recdb.ctx.col = 1;
    recdb.pos = 0;

    if ((res = setjmp(recdb.env)) == 0) {
        db = parse_database_int(&recdb);
    } else {
        explain_failure(&recdb, res);
        _exit(1);
    }

    switch (recdb.type) {
        case RECDB_MMAP:
#ifdef HAVE_MMAP
            munmap(recdb.s, recdb.length);
#endif
        case RECDB_FILE:
            fclose(recdb.f);
            break;
        /* Appease gcc */
        default:
            break;
    }
    return db;
}

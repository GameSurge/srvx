/* helpfile.c - Help file loading and display
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

#include "conf.h"
#include "helpfile.h"
#include "log.h"
#include "modcmd.h"
#include "nickserv.h"

#include <dirent.h>

static const struct message_entry msgtab[] = {
    { "HFMSG_MISSING_HELPFILE", "The help file could not be found.  Sorry!" },
    { "HFMSG_HELP_NOT_STRING", "Help file error (help data was not a string)." },
    { NULL, NULL }
};

#define DEFAULT_LINE_SIZE	MAX_LINE_SIZE
#define DEFAULT_TABLE_SIZE      80

extern struct userNode *global, *chanserv, *opserv, *nickserv;
struct userNode *message_dest;
struct userNode *message_source;
struct language *lang_C;
struct dict *languages;

static void language_cleanup(void)
{
    dict_delete(languages);
}

static void language_free_helpfile(void *data)
{
    struct helpfile *hf = data;
    close_helpfile(hf);
}

static void language_free(void *data)
{
    struct language *lang = data;
    dict_delete(lang->messages);
    dict_delete(lang->helpfiles);
    free(lang->name);
    free(lang);
}

static struct language *language_alloc(const char *name)
{
    struct language *lang = calloc(1, sizeof(*lang));
    lang->name = strdup(name);
    lang->parent = lang_C;
    if (!languages) {
        languages = dict_new();
        dict_set_free_data(languages, language_free);
    }
    dict_insert(languages, lang->name, lang);
    return lang;
}

/* Language names should use a lang or lang_COUNTRY type system, where
 * lang is a two-letter code according to ISO-639-1 (or three-letter
 * code according to ISO-639-2 for languages not in ISO-639-1), and
 * COUNTRY is the ISO 3166 country code in all upper case.
 * 
 * See also:
 * http://www.loc.gov/standards/iso639-2/
 * http://www.loc.gov/standards/iso639-2/langhome.html
 * http://www.iso.ch/iso/en/prods-services/iso3166ma/index.html
 */
struct language *language_find(const char *name)
{
    struct language *lang;
    char alt_name[MAXLEN];
    const char *uscore;

    if ((lang = dict_find(languages, name, NULL)))
        return lang;
    if ((uscore = strchr(name, '_'))) {
        strncpy(alt_name, name, uscore-name);
        alt_name[uscore-name] = 0;
        if ((lang = dict_find(languages, alt_name, NULL)))
            return lang;
    }
    if (!lang_C) {
        lang_C = language_alloc("C");
        lang_C->messages = dict_new();
        lang_C->helpfiles = dict_new();
    }
    return lang_C;
}

static void language_set_messages(struct language *lang, dict_t dict)
{
    dict_iterator_t it, it2;
    struct record_data *rd;
    char *msg;
    int extra, missing;

    extra = missing = 0;
    for (it = dict_first(dict), it2 = dict_first(lang_C->messages); it; ) {
        const char *msgid = iter_key(it);
        int diff = it2 ? irccasecmp(msgid, iter_key(it2)) : -1;
        if (diff < 0) {
            extra++;
            it = iter_next(it);
            continue;
        } else if (diff > 0) {
            missing++;
            it2 = iter_next(it2);
            continue;
        }
        rd = iter_data(it);
        switch (rd->type) {
        case RECDB_QSTRING:
            msg = strdup(rd->d.qstring);
            break;
        case RECDB_STRING_LIST:
            /* XXX: maybe do an unlistify_help() type thing */
        default:
            log_module(MAIN_LOG, LOG_WARNING, "Unsupported record type for message %s in language %s", msgid, lang->name);
            continue;
        }
        dict_insert(lang->messages, strdup(msgid), msg);
        it = iter_next(it);
        it2 = iter_next(it2);
    }
    while (it2) {
        missing++;
        it2 = iter_next(it2);
    }
    if (extra || missing)
        log_module(MAIN_LOG, LOG_WARNING, "In language %s, %d extra and %d missing messages.", lang->name, extra, missing);
}

static struct language *language_read(const char *name)
{
    DIR *dir;
    struct dirent *dirent;
    struct language *lang;
    struct helpfile *hf;
    char filename[MAXLEN], *uscore;
    FILE *file;
    dict_t dict;

    /* Never try to read the C language from disk. */
    if (!irccasecmp(name, "C"))
        return lang_C;

    /* Open the directory stream; if we can't, fail. */
    snprintf(filename, sizeof(filename), "languages/%s", name);
    if (!(dir = opendir(filename)))
        return NULL;
    if (!(lang = dict_find(languages, name, NULL)))
        lang = language_alloc(name);

    /* Find the parent language. */
    snprintf(filename, sizeof(filename), "languages/%s/parent", name);
    if (!(file = fopen(filename, "r"))
        || !fgets(filename, sizeof(filename), file)) {
        strcpy(filename, "C");
    }
    if (!(lang->parent = language_find(filename))) {
        uscore = strchr(filename, '_');
        if (uscore) {
            *uscore = 0;
            lang->parent = language_find(filename);
        }
        if (!lang->parent)
            lang->parent = lang_C;
    }

    /* (Re-)initialize the language's dicts. */
    dict_delete(lang->messages);
    lang->messages = dict_new();
    dict_set_free_keys(lang->messages, free);
    dict_set_free_data(lang->messages, free);
    lang->helpfiles = dict_new();
    dict_set_free_data(lang->helpfiles, language_free_helpfile);

    /* Read all the translations from the directory. */
    while ((dirent = readdir(dir))) {
        snprintf(filename, sizeof(filename), "languages/%s/%s", name, dirent->d_name);
        if (!strcmp(dirent->d_name,"parent")) {
            continue;
        } else if (!strcmp(dirent->d_name, "strings.db")) {
            dict = parse_database(filename);
            language_set_messages(lang, dict);
            free_database(dict);
        } else if ((hf = dict_find(lang_C->helpfiles, dirent->d_name, NULL))) {
            hf = open_helpfile(filename, hf->expand);
            dict_insert(lang->helpfiles, hf->name, hf);
        }
    }

    /* All done. */
    closedir(dir);
    return lang;
}

static void language_read_list(void)
{
    struct dirent *dirent;
    DIR *dir;

    if (!(dir = opendir("languages")))
        return;
    while ((dirent = readdir(dir))) {
        if (!strcmp(dirent->d_name, ".") || !strcmp(dirent->d_name, ".."))
            continue;
        language_alloc(dirent->d_name);
    }
    closedir(dir);
}

static void language_read_all(void)
{
    struct string_list *slist;
    struct dirent *dirent;
    DIR *dir;
    unsigned int ii;

    /* Read into an in-memory list and sort so we are likely to load
     * parent languages before their children (de_DE sorts after de).
     */
    if (!(dir = opendir("languages")))
        return;
    slist = alloc_string_list(4);
    while ((dirent = readdir(dir)))
        string_list_append(slist, strdup(dirent->d_name));
    closedir(dir);
    string_list_sort(slist);
    for (ii = 0; ii < slist->used; ++ii) {
        if (!strcmp(slist->list[ii], ".") || !strcmp(slist->list[ii], ".."))
            continue;
        language_read(slist->list[ii]);
    }
    free_string_list(slist);
}

const char *language_find_message(struct language *lang, const char *msgid) {
    struct language *curr;
    const char *msg;
    if (!lang)
        lang = lang_C;
    for (curr = lang; curr; curr = curr->parent)
        if ((msg = dict_find(curr->messages, msgid, NULL)))
            return msg;
    log_module(MAIN_LOG, LOG_ERROR, "Tried to find unregistered message \"%s\" (original language %s)", msgid, lang->name);
    return NULL;
}

void
table_send(struct userNode *from, const char *to, unsigned int size, irc_send_func irc_send, struct helpfile_table table) {
    unsigned int ii, jj, len, nreps, reps, tot_width, pos, spaces, *max_width;
    char line[MAX_LINE_SIZE+1];
    struct handle_info *hi;

    if (IsChannelName(to) || *to == '$') {
        message_dest = NULL;
        hi = NULL;
    } else {
        message_dest = GetUserH(to);
        if (!message_dest) {
            log_module(MAIN_LOG, LOG_ERROR, "Unable to find user with nickname %s (in table_send from %s).", to, from->nick);
            return;
        }
        hi = message_dest->handle_info;
#ifdef WITH_PROTOCOL_P10
        to = message_dest->numeric;
#endif
    }
    message_source = from;

    /* If size or irc_send are 0, we should try to use a default. */
    if (size)
        {} /* keep size */
    else if (!hi)
        size = DEFAULT_TABLE_SIZE;
    else if (hi->table_width)
        size = hi->table_width;
    else if (hi->screen_width)
        size = hi->screen_width;
    else
        size = DEFAULT_TABLE_SIZE;

    if (irc_send)
        {} /* use that function */
    else if (hi)
        irc_send = HANDLE_FLAGGED(hi, USE_PRIVMSG) ? irc_privmsg : irc_notice;
    else
        irc_send = IsChannelName(to) ? irc_privmsg : irc_notice;

    /* Limit size to how much we can show at once */
    if (size > sizeof(line))
        size = sizeof(line);

    /* Figure out how wide columns should be */
    max_width = alloca(table.width * sizeof(int));
    for (jj=tot_width=0; jj<table.width; jj++) {
        /* Find the widest width for this column */
        max_width[jj] = 0;
        for (ii=0; ii<table.length; ii++) {
            len = strlen(table.contents[ii][jj]);
            if (len > max_width[jj])
                max_width[jj] = len;
        }
        /* Separate columns with spaces */
        tot_width += max_width[jj] + 1;
    }
    /* How many rows to put in a line? */
    if ((table.flags & TABLE_REPEAT_ROWS) && (size > tot_width))
        nreps = size / tot_width;
    else
        nreps = 1;
    /* Send headers line.. */
    if (table.flags & TABLE_NO_HEADERS) {
        ii = 0;
    } else {
        /* Sending headers needs special treatment: either show them
         * once, or repeat them as many times as we repeat the columns
         * in a row. */
        for (pos=ii=0; ii<((table.flags & TABLE_REPEAT_HEADERS)?nreps:1); ii++) {
            for (jj=0; 1; ) {
                len = strlen(table.contents[0][jj]);
                spaces = max_width[jj] - len;
                if (table.flags & TABLE_PAD_LEFT)
                    while (spaces--)
                        line[pos++] = ' ';
                memcpy(line+pos, table.contents[0][jj], len);
                pos += len;
                if (++jj == table.width)
                    break;
                if (!(table.flags & TABLE_PAD_LEFT))
                    while (spaces--)
                        line[pos++] = ' ';
                line[pos++] = ' ';
            }
        }
        line[pos] = 0;
        irc_send(from, to, line);
        ii = 1;
    }
    /* Send the table. */
    for (jj=0, pos=0, reps=0; ii<table.length; ) {
        while (1) {
            len = strlen(table.contents[ii][jj]);
            spaces = max_width[jj] - len;
            if (table.flags & TABLE_PAD_LEFT)
                while (spaces--) line[pos++] = ' ';
            memcpy(line+pos, table.contents[ii][jj], len);
            pos += len;
            if (++jj == table.width) {
                jj = 0, ++ii, ++reps;
                if ((reps == nreps) || (ii == table.length)) {
                    line[pos] = 0;
                    irc_send(from, to, line);
                    pos = reps = 0;
                    break;
                }
            }
            if (!(table.flags & TABLE_PAD_LEFT))
                while (spaces--)
                    line[pos++] = ' ';
            line[pos++] = ' ';
        }
    }
    if (!(table.flags & TABLE_NO_FREE)) {
        /* Deallocate table memory (but not the string memory). */
        for (ii=0; ii<table.length; ii++)
            free(table.contents[ii]);
        free(table.contents);
    }
}

static int
vsend_message(const char *dest, struct userNode *src, struct handle_info *handle, int msg_type, expand_func_t expand_f, const char *format, va_list al)
{
    void (*irc_send)(struct userNode *from, const char *to, const char *msg);
    static struct string_buffer input;
    unsigned int size, ipos, pos, length, chars_sent, use_color;
    unsigned int expand_pos, expand_ipos, newline_ipos;
    char line[MAX_LINE_SIZE];

    if (IsChannelName(dest) || *dest == '$') {
        message_dest = NULL;
    } else if (!(message_dest = GetUserH(dest))) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to find user with nickname %s (in vsend_message from %s).", dest, src->nick);
        return 0;
    } else if (message_dest->dead) {
        /* No point in sending to a user who is leaving. */
        return 0;
    } else {
#ifdef WITH_PROTOCOL_P10
        dest = message_dest->numeric;
#endif
    }
    message_source = src;
    if (!(msg_type & 4) && !(format = handle_find_message(handle, format)))
        return 0;
    /* fill in a buffer with the string */
    input.used = 0;
    string_buffer_append_vprintf(&input, format, al);

    /* figure out how to send the messages */
    if (handle) {
	msg_type |= (HANDLE_FLAGGED(handle, USE_PRIVMSG) ? 1 : 0);
	use_color = HANDLE_FLAGGED(handle, MIRC_COLOR);
        size = handle->screen_width;
        if (size > sizeof(line))
            size = sizeof(line);
    } else {
        size = sizeof(line);
        use_color = 1;
    }
    if (!size)
        size = DEFAULT_LINE_SIZE;
    switch (msg_type & 3) {
        case 0:
            irc_send = irc_notice;
            break;
        case 2:
            irc_send = irc_wallchops;
            break;
        case 1:
        default:
            irc_send = irc_privmsg;
    }

    /* This used to be two passes, but if you do that and allow
     * arbitrary sizes for ${}-expansions (as with help indexes),
     * that requires a very big intermediate buffer.
     */
    expand_ipos = newline_ipos = ipos = 0;
    expand_pos = pos = 0;
    chars_sent = 0;
    while (input.list[ipos]) {
	char ch, *value, *free_value;

        while ((ch = input.list[ipos]) && (ch != '$') && (ch != '\n') && (pos < size)) {
	    line[pos++] = ch;
            ipos++;
	}

	if (!input.list[ipos])
            goto send_line;
        if (input.list[ipos] == '\n') {
            ipos++;
            goto send_line;
        }
	if (pos == size) {
            unsigned int new_ipos;
            /* Scan backwards for a space in the input, until we hit
             * either the last newline or the last variable expansion.
             * Print the line up to that point, and start from there.
             */
            for (new_ipos = ipos;
                 (new_ipos > expand_ipos) && (new_ipos > newline_ipos);
                 --new_ipos)
                if (input.list[new_ipos] == ' ')
                    break;
            pos -= ipos - new_ipos;
            if (new_ipos == newline_ipos) {
                /* Single word was too big to fit on one line; skip
                 * forward to its end and print it as a whole.
                 */
                while (input.list[new_ipos]
                       && (input.list[new_ipos] != ' ')
                       && (input.list[new_ipos] != '\n')
                       && (input.list[new_ipos] != '$'))
                    line[pos++] = input.list[new_ipos++];
            }
            ipos = new_ipos;
            while (input.list[ipos] == ' ')
                ipos++;
	    goto send_line;
	}

        free_value = 0;
	switch (input.list[++ipos]) {
        /* Literal '$' or end of string. */
	case 0:
	    ipos--;
	case '$':
	    value = "$";
	    break;
	/* The following two expand to mIRC color codes if enabled
	   by the user. */
	case 'b':
	    value = use_color ? "\002" : "";
	    break;
	case 'o':
	    value = use_color ? "\017" : "";
	    break;
        case 'r':
            value = use_color ? "\026" : "";
            break;
	case 'u':
	    value = use_color ? "\037" : "";
	    break;
	/* Service nicks. */
        case 'S':
            value = src->nick;
            break;
	case 'G':
	    value = global ? global->nick : "Global";
	    break;
	case 'C':
	    value = chanserv ? chanserv->nick : "ChanServ";
	    break;
	case 'O':
	    value = opserv ? opserv->nick : "OpServ";
	    break;
	case 'N':
            value = nickserv ? nickserv->nick : "NickServ";
            break;
        case 's':
            value = self->name;
            break;
	case 'H':
	    value = handle ? handle->handle : "Account";
	    break;
#define SEND_LINE() do { line[pos] = 0; if (pos > 0) irc_send(src, dest, line); chars_sent += pos; pos = 0; newline_ipos = ipos; } while (0)
	/* Custom expansion handled by helpfile-specific function. */
	case '{':
	case '(': {
            struct helpfile_expansion exp;
            char *name_end = input.list + ipos + 1, *colon = NULL;

            while (*name_end != '}' && *name_end != ')' && *name_end) {
                if (*name_end == ':') {
                    colon = name_end;
                    *colon = '\0';
                }
                name_end++;
            }
            if (!*name_end)
                goto fallthrough;
            *name_end = '\0';
            if (colon) {
                struct module *module = module_find(input.list + ipos + 1);
                if (module && module->expand_help)
                    exp = module->expand_help(colon + 1);
                else {
                    *colon = ':';
                    goto fallthrough;
                }
            } else if (expand_f)
                exp = expand_f(input.list + ipos + 1);
            else
                goto fallthrough;
            switch (exp.type) {
            case HF_STRING:
                free_value = value = exp.value.str;
                if (!value)
                    value = "";
                break;
            case HF_TABLE:
                /* Must send current line, then emit table. */
                SEND_LINE();
                table_send(src, (message_dest ? message_dest->nick : dest), 0, irc_send, exp.value.table);
                value = "";
                break;
            default:
                value = "";
                log_module(MAIN_LOG, LOG_ERROR, "Invalid exp.type %d from expansion function %p.", exp.type, expand_f);
                break;
            }
            ipos = name_end - input.list;
            break;
        }
	default:
        fallthrough:
            value = alloca(3);
            value[0] = '$';
            value[1] = input.list[ipos];
            value[2] = 0;
	}
	ipos++;
        while ((pos + strlen(value) > size) || strchr(value, '\n')) {
            unsigned int avail;
            avail = size - pos - 1;
            length = strcspn(value, "\n ");
            if (length <= avail) {
                strncpy(line+pos, value, length);
                pos += length;
                value += length;
                /* copy over spaces, until (possible) end of line */
                while (*value == ' ') {
                    if (pos < size-1)
                        line[pos++] = *value;
                    value++;
                }
            } else {
                /* word to send is too big to send now.. what to do? */
                if (pos > 0) {
                    /* try to put it on a separate line */
                    SEND_LINE();
                } else {
                    /* already at start of line; only send part of it */
                    strncpy(line, value, avail);
                    pos += avail;
                    value += length;
                    /* skip any trailing spaces */
                    while (*value == ' ')
                        value++;
                }
            }
            /* if we're looking at a newline, send the accumulated text */
            if (*value == '\n') {
                SEND_LINE();
                value++;
            }
        }
        length = strlen(value);
	memcpy(line + pos, value, length);
        if (free_value)
            free(free_value);
	pos += length;
        if ((pos < size-1) && input.list[ipos]) {
            expand_pos = pos;
            expand_ipos = ipos;
            continue;
        }
      send_line:
        expand_pos = pos;
        expand_ipos = ipos;
        SEND_LINE();
#undef SEND_LINE
    }
    return chars_sent;
}

int
send_message(struct userNode *dest, struct userNode *src, const char *format, ...)
{
    int res;
    va_list ap;

    if (IsLocal(dest)) return 0;
    va_start(ap, format);
    res = vsend_message(dest->nick, src, dest->handle_info, 0, NULL, format, ap);
    va_end(ap);
    return res;
}

int
send_message_type(int msg_type, struct userNode *dest, struct userNode *src, const char *format, ...) {
    int res;
    va_list ap;

    if (IsLocal(dest)) return 0;
    va_start(ap, format);
    res = vsend_message(dest->nick, src, dest->handle_info, msg_type, NULL, format, ap);
    va_end(ap);
    return res;
}

int
send_target_message(int msg_type, const char *dest, struct userNode *src, const char *format, ...)
{
    int res;
    va_list ap;

    va_start(ap, format);
    res = vsend_message(dest, src, NULL, msg_type, NULL, format, ap);
    va_end(ap);
    return res;
}

int
_send_help(struct userNode *dest, struct userNode *src, expand_func_t expand, const char *format, ...)
{
    int res;

    va_list ap;
    va_start(ap, format);
    res = vsend_message(dest->nick, src, dest->handle_info, 4, expand, format, ap);
    va_end(ap);
    return res;
}

int
send_help(struct userNode *dest, struct userNode *src, struct helpfile *hf, const char *topic)
{
    struct helpfile *lang_hf;
    struct record_data *rec;
    struct language *curr;

    if (!topic)
        topic = "<index>";
    if (!hf) {
        _send_help(dest, src, NULL, "HFMSG_MISSING_HELPFILE");
        return 0;
    }
    for (curr = (dest->handle_info ? dest->handle_info->language : lang_C);
         curr;
         curr = curr->parent) {
        lang_hf = dict_find(curr->helpfiles, hf->name, NULL);
        if (!lang_hf)
            continue;
        rec = dict_find(lang_hf->db, topic, NULL);
        if (rec && rec->type == RECDB_QSTRING)
            return _send_help(dest, src, hf->expand, rec->d.qstring);
    }
    rec = dict_find(hf->db, "<missing>", NULL);
    if (!rec)
        return send_message(dest, src, "MSG_TOPIC_UNKNOWN");
    if (rec->type != RECDB_QSTRING)
	return send_message(dest, src, "HFMSG_HELP_NOT_STRING");
    return _send_help(dest, src, hf->expand, rec->d.qstring);
}

/* Grammar supported by this parser:
 * condition = expr | prefix expr
 * expr = atomicexpr | atomicexpr op atomicexpr
 * op = '&&' | '||' | 'and' | 'or'
 * atomicexpr = '(' expr ')' | identifier
 * identifier = ( '0'-'9' 'A'-'Z' 'a'-'z' '-' '_' '/' )+ | ! identifier
 *
 * Whitespace is ignored. The parser is implemented as a recursive
 * descent parser by functions like:
 *   static int helpfile_eval_<element>(const char *start, const char **end);
 */

enum helpfile_op {
    OP_INVALID,
    OP_BOOL_AND,
    OP_BOOL_OR
};

static const struct {
    const char *str;
    enum helpfile_op op;
} helpfile_operators[] = {
    { "&&", OP_BOOL_AND },
    { "and", OP_BOOL_AND },
    { "||", OP_BOOL_OR },
    { "or", OP_BOOL_OR },
    { NULL, OP_INVALID }
};

static const char *identifier_chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz-_/";

static int helpfile_eval_expr(const char *start, const char **end);
static int helpfile_eval_atomicexpr(const char *start, const char **end);
static int helpfile_eval_identifier(const char *start, const char **end);

static int
helpfile_eval_identifier(const char *start, const char **end)
{
    /* Skip leading whitespace. */
    while (isspace(*start) && (start < *end))
        start++;
    if (start == *end) {
        log_module(MAIN_LOG, LOG_FATAL, "Expected identifier in helpfile condition.");
        return -1;
    }

    if (start[0] == '!') {
        int res = helpfile_eval_identifier(start+1, end);
        if (res < 0)
            return res;
        return !res;
    } else if (start[0] == '/') {
        const char *sep;
        char *id_str, *value;

        for (sep = start;
             strchr(identifier_chars, sep[0]) && (sep < *end);
             ++sep) ;
        memcpy(id_str = alloca(sep+1-start), start, sep-start);
        id_str[sep-start] = '\0';
        value = conf_get_data(id_str+1, RECDB_QSTRING);
        *end = sep;
        if (!value)
            return 0;
        return enabled_string(value) || true_string(value);
    } else if ((*end - start >= 4) && !ircncasecmp(start, "true", 4)) {
        *end = start + 4;
        return 1;
    } else if ((*end - start >= 5) && !ircncasecmp(start, "false", 5)) {
        *end = start + 5;
        return 0;
    } else {
        log_module(MAIN_LOG, LOG_FATAL, "Unexpected helpfile identifier '%.*s'.", *end-start, start);
        return -1;
    }
}

static int
helpfile_eval_atomicexpr(const char *start, const char **end)
{
    const char *sep;
    int res;

    /* Skip leading whitespace. */
    while (isspace(*start) && (start < *end))
        start++;
    if (start == *end) {
        log_module(MAIN_LOG, LOG_FATAL, "Expected atomic expression in helpfile condition.");
        return -1;
    }

    /* If it's not parenthesized, it better be a valid identifier. */
    if (*start != '(')
        return helpfile_eval_identifier(start, end);

    /* Parse the internal expression. */
    start++;
    sep = *end;
    res = helpfile_eval_expr(start, &sep);

    /* Check for the closing parenthesis. */
    while (isspace(*sep) && (sep < *end))
        sep++;
    if ((sep == *end) || (sep[0] != ')')) {
        log_module(MAIN_LOG, LOG_FATAL, "Expected close parenthesis at '%.*s'.", *end-sep, sep);
        return -1;
    }

    /* Report the end location and result. */
    *end = sep + 1;
    return res;
}

static int
helpfile_eval_expr(const char *start, const char **end)
{
    const char *sep, *sep2;
    unsigned int ii, len;
    int res_a, res_b;
    enum helpfile_op op;

    /* Parse the first atomicexpr. */
    sep = *end;
    res_a = helpfile_eval_atomicexpr(start, &sep);
    if (res_a < 0)
        return res_a;

    /* Figure out what follows that. */
    while (isspace(*sep) && (sep < *end))
        sep++;
    if (sep == *end)
        return res_a;
    op = OP_INVALID;
    for (ii = 0; helpfile_operators[ii].str; ++ii) {
        len = strlen(helpfile_operators[ii].str);
        if (ircncasecmp(sep, helpfile_operators[ii].str, len))
            continue;
        op = helpfile_operators[ii].op;
        sep += len;
    }
    if (op == OP_INVALID) {
        log_module(MAIN_LOG, LOG_FATAL, "Unrecognized helpfile operator at '%.*s'.", *end-sep, sep);
        return -1;
    }

    /* Parse the next atomicexpr. */
    sep2 = *end;
    res_b = helpfile_eval_atomicexpr(sep, &sep2);
    if (res_b < 0)
        return res_b;

    /* Make sure there's no trailing garbage */
    while (isspace(*sep2) && (sep2 < *end))
        sep2++;
    if (sep2 != *end) {
        log_module(MAIN_LOG, LOG_FATAL, "Trailing garbage in helpfile expression: '%.*s'.", *end-sep2, sep2);
        return -1;
    }

    /* Record where we stopped parsing. */
    *end = sep2;

    /* Do the logic on the subexpressions. */
    switch (op) {
    case OP_BOOL_AND:
        return res_a && res_b;
    case OP_BOOL_OR:
        return res_a || res_b;
    default:
        return -1;
    }
}

static int
helpfile_eval_condition(const char *start, const char **end)
{
    const char *term;

    /* Skip the prefix if there is one. */
    for (term = start; isalnum(*term) && (term < *end); ++term) ;
    if (term != start) {
        if ((term + 2 >= *end) || (term[0] != ':') || (term[1] != ' ')) {
            log_module(MAIN_LOG, LOG_FATAL, "In helpfile condition '%.*s' expected prefix to end with ': '.", *end-start, start);
            return -1;
        }
        start = term + 2;
    }

    /* Evaluate the remaining string as an expression. */
    return helpfile_eval_expr(start, end);
}

static int
unlistify_help(const char *key, void *data, void *extra)
{
    struct record_data *rd = data;
    dict_t newdb = extra;

    switch (rd->type) {
    case RECDB_QSTRING:
	dict_insert(newdb, strdup(key), alloc_record_data_qstring(GET_RECORD_QSTRING(rd)));
	return 0;
    case RECDB_STRING_LIST: {
	struct string_list *slist = GET_RECORD_STRING_LIST(rd);
	char *dest;
	unsigned int totlen, len, i;

	for (i=totlen=0; i<slist->used; i++)
	    totlen = totlen + strlen(slist->list[i]) + 1;
	dest = alloca(totlen+1);
	for (i=totlen=0; i<slist->used; i++) {
	    len = strlen(slist->list[i]);
	    memcpy(dest+totlen, slist->list[i], len);
	    dest[totlen+len] = '\n';
	    totlen = totlen + len + 1;
	}
	dest[totlen] = 0;
	dict_insert(newdb, strdup(key), alloc_record_data_qstring(dest));
	return 0;
    }
    case RECDB_OBJECT: {
        dict_iterator_t it;

        for (it = dict_first(GET_RECORD_OBJECT(rd)); it; it = iter_next(it)) {
            const char *k2, *end;
            int res;

            /* Evaluate the expression for this subentry. */
            k2 = iter_key(it);
            end = k2 + strlen(k2);
            res = helpfile_eval_condition(k2, &end);
            /* If the evaluation failed, bail. */
            if (res < 0) {
                log_module(MAIN_LOG, LOG_FATAL, " .. while processing entry '%s' condition '%s'.", key, k2);
                return 1;
            }
            /* If the condition was false, try another. */
            if (!res)
                continue;
            /* If we cannot unlistify the contents, bail. */
            if (unlistify_help(key, iter_data(it), extra))
                return 1;
            return 0;
        }
        /* If none of the conditions apply, just omit the entry. */
        return 0;
    }
    default:
	return 1;
    }
}

struct helpfile *
open_helpfile(const char *fname, expand_func_t expand)
{
    struct helpfile *hf;
    char *slash;
    dict_t db = parse_database(fname);
    hf = calloc(1, sizeof(*hf));
    hf->expand = expand;
    hf->db = alloc_database();
    dict_set_free_keys(hf->db, free);
    if ((slash = strrchr(fname, '/'))) {
        hf->name = strdup(slash + 1);
    } else {
        hf->name = strdup(fname);
        dict_insert(language_find("C")->helpfiles, hf->name, hf);
    }
    if (db) {
	dict_foreach(db, unlistify_help, hf->db);
	free_database(db);
    }
    return hf;
}

void close_helpfile(struct helpfile *hf)
{
    if (!hf)
        return;
    free((char*)hf->name);
    free_database(hf->db);
    free(hf);
}

void message_register_table(const struct message_entry *table)
{
    if (!lang_C)
        language_find("C");
    while (table->msgid) {
        dict_insert(lang_C->messages, table->msgid, (char*)table->format);
        table++;
    }
}

void helpfile_init(void)
{
    message_register_table(msgtab);
    language_read_list();
}

void helpfile_finalize(void)
{
    language_read_all();
    reg_exit_func(language_cleanup);
}

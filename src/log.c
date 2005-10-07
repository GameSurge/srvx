/* log.c - Diagnostic and error logging
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
#include "log.h"
#include "helpfile.h" /* send_message, message_register, etc */
#include "nickserv.h"

struct logDestination;

struct logDest_vtable {
    const char *type_name;
    struct logDestination* (*open)(const char *args);
    void (*reopen)(struct logDestination *self);
    void (*close)(struct logDestination *self);
    void (*log_audit)(struct logDestination *self, struct log_type *type, struct logEntry *entry);
    void (*log_replay)(struct logDestination *self, struct log_type *type, int is_write, const char *line);
    void (*log_module)(struct logDestination *self, struct log_type *type, enum log_severity sev, const char *message);
};

struct logDestination {
    struct logDest_vtable *vtbl;
    char *name;
    int refcnt;
};

DECLARE_LIST(logList, struct logDestination*);

struct log_type {
    char *name;
    struct logList logs[LOG_NUM_SEVERITIES];
    struct logEntry *log_oldest;
    struct logEntry *log_newest;
    unsigned int log_count;
    unsigned int max_age;
    unsigned int max_count;
    unsigned int default_set : 1;
};

static const char *log_severity_names[] = {
    "replay",   /* 0 */
    "debug",
    "command",
    "info",
    "override",
    "staff",    /* 5 */
    "warning",
    "error",
    "fatal",
    0
};

static struct dict *log_dest_types;
static struct dict *log_dests;
static struct dict *log_types;
static struct log_type *log_default;
static int log_inited, log_debugged;

DEFINE_LIST(logList, struct logDestination*);
static void log_format_audit(struct logEntry *entry);
static const struct message_entry msgtab[] = {
    { "MSG_INVALID_FACILITY", "$b%s$b is an invalid log facility." },
    { "MSG_INVALID_SEVERITY", "$b%s$b is an invalid severity level." },
    { NULL, NULL }
};

static struct logDestination *
log_open(const char *name)
{
    struct logDest_vtable *vtbl;
    struct logDestination *ld;
    char *sep;
    char type_name[32];

    if ((ld = dict_find(log_dests, name, NULL))) {
        ld->refcnt++;
        return ld;
    }
    if ((sep = strchr(name, ':'))) {
        memcpy(type_name, name, sep-name);
        type_name[sep-name] = 0;
    } else {
        strcpy(type_name, name);
    }
    if (!(vtbl = dict_find(log_dest_types, type_name, NULL))) {
        log_module(MAIN_LOG, LOG_ERROR, "Invalid log type for log '%s'.", name);
        return 0;
    }
    if (!(ld = vtbl->open(sep ? sep+1 : 0)))
        return 0;
    ld->name = strdup(name);
    dict_insert(log_dests, ld->name, ld);
    ld->refcnt = 1;
    return ld;
}

static void
logList_open(struct logList *ll, struct record_data *rd)
{
    struct logDestination *ld;
    unsigned int ii;

    if (!ll->size)
        logList_init(ll);
    switch (rd->type) {
    case RECDB_QSTRING:
        if ((ld = log_open(rd->d.qstring)))
            logList_append(ll, ld);
        break;
    case RECDB_STRING_LIST:
        for (ii=0; ii<rd->d.slist->used; ++ii) {
            if ((ld = log_open(rd->d.slist->list[ii])))
                logList_append(ll, ld);
        }
        break;
    default:
        break;
    }
}

static void
logList_join(struct logList *target, const struct logList *source)
{
    unsigned int ii, jj, kk;

    if (!source->used)
        return;
    jj = target->used;
    target->used += source->used;
    target->size += source->used;
    target->list = realloc(target->list, target->size * sizeof(target->list[0]));
    for (ii = 0; ii < source->used; ++ii, ++jj) {
        int dup;
        for (dup = 0, kk = 0; kk < jj; kk++) {
            if (target->list[kk] == source->list[ii]) {
                dup = 1;
                break;
            }
        }
        if (dup) {
            jj--;
            target->used--;
            continue;
        }
        target->list[jj] = source->list[ii];
        target->list[jj]->refcnt++;
    }
}

static void
logList_close(struct logList *ll)
{
    unsigned int ii;
    for (ii=0; ii<ll->used; ++ii) {
        if (!--ll->list[ii]->refcnt) {
            struct logDestination *ld = ll->list[ii];
            ld->vtbl->close(ld);
        }
    }
    logList_clean(ll);
}

static void
close_logs(void)
{
    dict_iterator_t it;
    struct log_type *lt;
    enum log_severity ls;

    for (it = dict_first(log_types); it; it = iter_next(it)) {
        lt = iter_data(it);
        for (ls = 0; ls < LOG_NUM_SEVERITIES; ls++) {
            logList_close(&lt->logs[ls]);

            lt->logs[ls].size = 0;
            lt->logs[ls].used = 0;
            lt->logs[ls].list = 0;
        }
    }
}

static void
log_type_free_oldest(struct log_type *lt)
{
    struct logEntry *next;

    if (!lt->log_oldest)
        return;
    next = lt->log_oldest->next;
    free(lt->log_oldest->default_desc);
    free(lt->log_oldest);
    lt->log_oldest = next;
    lt->log_count--;
}

static void
log_type_free(void *ptr)
{
    struct log_type *lt = ptr;

    while (lt->log_oldest)
        log_type_free_oldest(lt);
    free(lt);
}

static void
cleanup_logs(void)
{

    close_logs();
    dict_delete(log_types);
    dict_delete(log_dests);
    dict_delete(log_dest_types);
}

static enum log_severity
find_severity(const char *text)
{
    enum log_severity ls;
    for (ls = 0; ls < LOG_NUM_SEVERITIES; ++ls)
        if (!ircncasecmp(text, log_severity_names[ls], strlen(log_severity_names[ls])))
            return ls;
    return LOG_NUM_SEVERITIES;
}

/* Log keys are based on syslog.conf syntax:
 *   KEY := LOGSET '.' SEVSET
 *   LOGSET := LOGLIT | LOGLIT ',' LOGSET
 *   LOGLIT := a registered log type
 *   SEVSET := '*' | SEVLIT | '<' SEVLIT | '<=' SEVLIT | '>' SEVLIT | '>=' SEVLIT | SEVLIT ',' SEVSET
 *   SEVLIT := one of log_severity_names
 * A KEY contains the Cartesian product of the logs in its LOGSET
 * and the severities in its SEVSET.
 */

static void
log_parse_logset(char *buffer, struct string_list *slist)
{
    slist->used = 0;
    while (buffer) {
        char *cont = strchr(buffer, ',');
        if (cont)
            *cont++ = 0;
        string_list_append(slist, strdup(buffer));
        buffer = cont;
    }
}

static void
log_parse_sevset(char *buffer, char targets[LOG_NUM_SEVERITIES])
{
    memset(targets, 0, LOG_NUM_SEVERITIES);
    while (buffer) {
        char *cont;
        enum log_severity bound;
        int first;

        cont = strchr(buffer, ',');
        if (cont)
            *cont++ = 0;
        if (buffer[0] == '*' && buffer[1] == 0) {
            for (bound = 0; bound < LOG_NUM_SEVERITIES; bound++) {
                /* make people explicitly specify replay targets */
                if (bound != LOG_REPLAY)
                    targets[bound] = 1;
            }
        } else if (buffer[0] == '<') {
            if (buffer[1] == '=')
                bound = find_severity(buffer+2) + 1;
            else
                bound = find_severity(buffer+1);
            for (first = 1; bound > 0; bound--) {
                /* make people explicitly specify replay targets */
                if (bound != LOG_REPLAY || first) {
                    targets[bound] = 1;
                    first = 0;
                }
            }
        } else if (buffer[0] == '>') {
            if (buffer[1] == '=')
                bound = find_severity(buffer+2);
            else
                bound = find_severity(buffer+1) + 1;
            for (first = 1; bound < LOG_NUM_SEVERITIES; bound++) {
                /* make people explicitly specify replay targets */
                if (bound != LOG_REPLAY || first) {
                    targets[bound] = 1;
                    first = 0;
                }
            }
        } else {
            if (buffer[0] == '=')
                buffer++;
            bound = find_severity(buffer);
            targets[bound] = 1;
        }
        buffer = cont;
    }
}

static void
log_parse_cross(const char *buffer, struct string_list *types, char sevset[LOG_NUM_SEVERITIES])
{
    char *dup, *sep;

    dup = strdup(buffer);
    sep = strchr(dup, '.');
    *sep++ = 0;
    log_parse_logset(dup, types);
    log_parse_sevset(sep, sevset);
    free(dup);
}

static void
log_parse_options(struct log_type *type, struct dict *conf)
{
    const char *opt;
    opt = database_get_data(conf, "max_age", RECDB_QSTRING);
    if (opt)
        type->max_age = ParseInterval(opt);
    opt = database_get_data(conf, "max_count", RECDB_QSTRING);
    if (opt)
        type->max_count = strtoul(opt, NULL, 10);
}

static void
log_conf_read(void)
{
    struct record_data *rd, *rd2;
    dict_iterator_t it;
    const char *sep;
    struct log_type *type;
    enum log_severity sev;
    unsigned int ii;

    close_logs();
    dict_delete(log_dests);

    log_dests = dict_new();
    dict_set_free_keys(log_dests, free);

    rd = conf_get_node("logs");
    if (rd && (rd->type == RECDB_OBJECT)) {
        for (it = dict_first(rd->d.object); it; it = iter_next(it)) {
            if ((sep = strchr(iter_key(it), '.'))) {
                struct logList logList;
                char sevset[LOG_NUM_SEVERITIES];
                struct string_list *slist;

                /* It looks like a <type>.<severity> record.  Try to parse it. */
                slist = alloc_string_list(4);
                log_parse_cross(iter_key(it), slist, sevset);
                logList.size = 0;
                logList_open(&logList, iter_data(it));
                for (ii = 0; ii < slist->used; ++ii) {
                    type = log_register_type(slist->list[ii], NULL);
                    for (sev = 0; sev < LOG_NUM_SEVERITIES; ++sev) {
                        if (!sevset[sev])
                            continue;
                        logList_join(&type->logs[sev], &logList);
                    }
                }
                logList_close(&logList);
                free_string_list(slist);
            } else if ((rd2 = iter_data(it))
                       && (rd2->type == RECDB_OBJECT)
                       && (type = log_register_type(iter_key(it), NULL))) {
                log_parse_options(type, rd2->d.object);
            } else {
                log_module(MAIN_LOG, LOG_ERROR, "Unknown logs subkey '%s'.", iter_key(it));
            }
        }
    }
    if (log_debugged)
        log_debug();
}

void
log_debug(void)
{
    enum log_severity sev;
    struct logDestination *log_stdout;
    struct logList target;

    log_stdout = log_open("std:out");
    logList_init(&target);
    logList_append(&target, log_stdout);

    for (sev = 0; sev < LOG_NUM_SEVERITIES; ++sev)
        logList_join(&log_default->logs[sev], &target);

    logList_close(&target);
    log_debugged = 1;
}

void
log_reopen(void)
{
    dict_iterator_t it;
    for (it = dict_first(log_dests); it; it = iter_next(it)) {
        struct logDestination *ld = iter_data(it);
        ld->vtbl->reopen(ld);
    }
}

struct log_type *
log_register_type(const char *name, const char *default_log)
{
    struct log_type *type;
    struct logDestination *dest;
    enum log_severity sev;

    if (!(type = dict_find(log_types, name, NULL))) {
        type = calloc(1, sizeof(*type));
        type->name = strdup(name);
        type->max_age = 600;
        type->max_count = 1024;
        dict_insert(log_types, type->name, type);
    }
    if (default_log && !type->default_set) {
        /* If any severity level was unspecified in the config, use the default. */
        dest = NULL;
        for (sev = 0; sev < LOG_NUM_SEVERITIES; ++sev) {
            if (sev == LOG_REPLAY)
                continue; /* never default LOG_REPLAY */
            if (!type->logs[sev].size) {
                logList_init(&type->logs[sev]);
                if (!dest) {
                    if (!(dest = log_open(default_log)))
                        break;
                    dest->refcnt--;
                }
                logList_append(&type->logs[sev], dest);
                dest->refcnt++;
            }
        }
        type->default_set = 1;
    }
    return type;
}

/* logging functions */

void
log_audit(struct log_type *type, enum log_severity sev, struct userNode *user, struct userNode *bot, const char *channel_name, unsigned int flags, const char *command)
{
    struct logEntry *entry;
    unsigned int size, ii;
    char *str_next;

    /* First make sure severity is appropriate */
    if ((sev != LOG_COMMAND) && (sev != LOG_OVERRIDE) && (sev != LOG_STAFF)) {
        log_module(MAIN_LOG, LOG_ERROR, "Illegal audit severity %d", sev);
        return;
    }
    /* Allocate and fill in the log entry */
    size = sizeof(*entry) + strlen(user->nick) + strlen(command) + 2;
    if (user->handle_info)
        size += strlen(user->handle_info->handle) + 1;
    if (channel_name)
        size += strlen(channel_name) + 1;
    if (flags & AUDIT_HOSTMASK)
        size += strlen(user->ident) + strlen(user->hostname) + 2;
    entry = calloc(1, size);
    str_next = (char*)(entry + 1);
    entry->time = now;
    entry->slvl = sev;
    entry->bot = bot;
    if (channel_name) {
        size = strlen(channel_name) + 1;
        entry->channel_name = memcpy(str_next, channel_name, size);
        str_next += size;
    }
    if (true) {
        size = strlen(user->nick) + 1;
        entry->user_nick = memcpy(str_next, user->nick, size);
        str_next += size;
    }
    if (user->handle_info) {
        size = strlen(user->handle_info->handle) + 1;
        entry->user_account = memcpy(str_next, user->handle_info->handle, size);
        str_next += size;
    }
    if (flags & AUDIT_HOSTMASK) {
        size = sprintf(str_next, "%s@%s", user->ident, user->hostname) + 1;
        entry->user_hostmask = str_next;
        str_next += size;
    } else {
        entry->user_hostmask = 0;
    }
    if (true) {
        size = strlen(command) + 1;
        entry->command = memcpy(str_next, command, size);
        str_next += size;
    }

    /* fill in the default text for the event */
    log_format_audit(entry);

    /* insert into the linked list */
    entry->next = 0;
    entry->prev = type->log_newest;
    if (type->log_newest)
        type->log_newest->next = entry;
    else
        type->log_oldest = entry;
    type->log_newest = entry;
    type->log_count++;

    /* remove old elements from the linked list */
    while (type->log_count > type->max_count)
        log_type_free_oldest(type);
    while (type->log_oldest && (type->log_oldest->time + (time_t)type->max_age < now))
        log_type_free_oldest(type);
    if (type->log_oldest)
        type->log_oldest->prev = 0;
    else
        type->log_newest = 0;

    /* call the destination logs */
    for (ii=0; ii<type->logs[sev].used; ++ii) {
        struct logDestination *ld = type->logs[sev].list[ii];
        ld->vtbl->log_audit(ld, type, entry);
    }
    for (ii=0; ii<log_default->logs[sev].used; ++ii) {
        struct logDestination *ld = log_default->logs[sev].list[ii];
        ld->vtbl->log_audit(ld, type, entry);
    }
}

void
log_replay(struct log_type *type, int is_write, const char *line)
{
    unsigned int ii;

    for (ii=0; ii<type->logs[LOG_REPLAY].used; ++ii) {
        struct logDestination *ld = type->logs[LOG_REPLAY].list[ii];
        ld->vtbl->log_replay(ld, type, is_write, line);
    }
    for (ii=0; ii<log_default->logs[LOG_REPLAY].used; ++ii) {
        struct logDestination *ld = log_default->logs[LOG_REPLAY].list[ii];
        ld->vtbl->log_replay(ld, type, is_write, line);
    }
}

void
log_module(struct log_type *type, enum log_severity sev, const char *format, ...)
{
    char msgbuf[1024];
    unsigned int ii;
    va_list args;

    if (sev > LOG_FATAL) {
        log_module(MAIN_LOG, LOG_ERROR, "Illegal log_module severity %d", sev);
        return;
    }
    va_start(args, format);
    vsnprintf(msgbuf, sizeof(msgbuf), format, args);
    va_end(args);
    if (log_inited) {
        for (ii=0; ii<type->logs[sev].used; ++ii) {
            struct logDestination *ld = type->logs[sev].list[ii];
            ld->vtbl->log_module(ld, type, sev, msgbuf);
        }
        for (ii=0; ii<log_default->logs[sev].used; ++ii) {
            struct logDestination *ld = log_default->logs[sev].list[ii];
            ld->vtbl->log_module(ld, type, sev, msgbuf);
        }
    } else {
        /* Special behavior before we start full operation */
        fprintf(stderr, "%s: %s\n", log_severity_names[sev], msgbuf);
    }
    if (sev == LOG_FATAL) {
        assert(0 && "fatal message logged");
        _exit(1);
    }
}

/* audit log searching */

struct logSearch *
log_discrim_create(struct userNode *service, struct userNode *user, unsigned int argc, char *argv[])
{
    unsigned int ii;
    struct logSearch *discrim;

    /* Assume all criteria require arguments. */
    if((argc - 1) % 2)
    {
	send_message(user, service, "MSG_MISSING_PARAMS", argv[0]);
	return NULL;
    }

    discrim = malloc(sizeof(struct logSearch));
    memset(discrim, 0, sizeof(*discrim));
    discrim->limit = 25;
    discrim->max_time = INT_MAX;
    discrim->severities = ~0;

    for (ii=1; ii<argc-1; ii++) {
        if (!irccasecmp(argv[ii], "bot")) {
            struct userNode *bot = GetUserH(argv[++ii]);
            if (!bot) {
                send_message(user, service, "MSG_NICK_UNKNOWN", argv[ii]);
                goto fail;
            } else if (!IsLocal(bot)) {
                send_message(user, service, "MSG_NOT_A_SERVICE", argv[ii]);
                goto fail;
            }
            discrim->masks.bot = bot;
        } else if (!irccasecmp(argv[ii], "channel")) {
            discrim->masks.channel_name = argv[++ii];
        } else if (!irccasecmp(argv[ii], "nick")) {
            discrim->masks.user_nick = argv[++ii];
        } else if (!irccasecmp(argv[ii], "account")) {
            discrim->masks.user_account = argv[++ii];
        } else if (!irccasecmp(argv[ii], "hostmask")) {
            discrim->masks.user_hostmask = argv[++ii];
        } else if (!irccasecmp(argv[ii], "command")) {
            discrim->masks.command = argv[++ii];
        } else if (!irccasecmp(argv[ii], "age")) {
            const char *cmp = argv[++ii];
            if (cmp[0] == '<') {
                if (cmp[1] == '=')
                    discrim->min_time = now - ParseInterval(cmp+2);
                else
                    discrim->min_time = now - (ParseInterval(cmp+1) - 1);
            } else if (cmp[0] == '>') {
                if (cmp[1] == '=')
                    discrim->max_time = now - ParseInterval(cmp+2);
                else
                    discrim->max_time = now - (ParseInterval(cmp+1) - 1);
            } else {
                discrim->min_time = now - ParseInterval(cmp+2);
            }
        } else if (!irccasecmp(argv[ii], "limit")) {
            discrim->limit = strtoul(argv[++ii], NULL, 10);
        } else if (!irccasecmp(argv[ii], "level")) {
            char *severity = argv[++ii];
            discrim->severities = 0;
            while (1) {
                enum log_severity sev = find_severity(severity);
                if (sev == LOG_NUM_SEVERITIES) {
                    send_message(user, service, "MSG_INVALID_SEVERITY", severity);
                    goto fail;
                }
                discrim->severities |= 1 << sev;
                severity = strchr(severity, ',');
                if (!severity)
                    break;
                severity++;
            }
        } else if (!irccasecmp(argv[ii], "type")) {
            if (!(discrim->type = dict_find(log_types, argv[++ii], NULL))) {
                send_message(user, service, "MSG_INVALID_FACILITY", argv[ii]);
                goto fail;
            }
	} else {
	    send_message(user, service, "MSG_INVALID_CRITERIA", argv[ii]);
	    goto fail;
	}
    }

    return discrim;
  fail:
    free(discrim);
    return NULL;
}

static int
entry_match(struct logSearch *discrim, struct logEntry *entry)
{
    if ((entry->time < discrim->min_time)
        || (entry->time > discrim->max_time)
        || !(discrim->severities & (1 << entry->slvl))
        || (discrim->masks.bot && (discrim->masks.bot != entry->bot))
        /* don't do glob matching, so that !events #a*b does not match #acb */
        || (discrim->masks.channel_name
            && (!entry->channel_name
                || irccasecmp(entry->channel_name, discrim->masks.channel_name)))
        || (discrim->masks.user_nick
            && !match_ircglob(entry->user_nick, discrim->masks.user_nick))
        || (discrim->masks.user_account
            && (!entry->user_account
                || !match_ircglob(entry->user_account, discrim->masks.user_account)))
        || (discrim->masks.user_hostmask
            && entry->user_hostmask
            && !match_ircglob(entry->user_hostmask, discrim->masks.user_hostmask))
        || (discrim->masks.command
            && !match_ircglob(entry->command, discrim->masks.command))) {
	return 0;
    }
    return 1;
}

void
log_report_entry(struct logEntry *match, void *extra)
{
    struct logReport *rpt = extra;
    send_message_type(4, rpt->user, rpt->reporter, "%s", match->default_desc);
}

unsigned int
log_entry_search(struct logSearch *discrim, entry_search_func esf, void *data)
{
    unsigned int matched = 0;

    if (discrim->type) {
        static volatile struct logEntry *last;
        struct logEntry *entry;

        for (entry = discrim->type->log_oldest, last = NULL;
             entry;
             last = entry, entry = entry->next) {
            verify(entry);
            if (entry_match(discrim, entry)) {
                esf(entry, data);
                if (++matched >= discrim->limit)
                    break;
            }
        }
    } else {
        dict_iterator_t it;

        for (it = dict_first(log_types); it; it = iter_next(it)) {
            discrim->type = iter_data(it);
            matched += log_entry_search(discrim, esf, data);
        }
    }

    return matched;
}

/* generic helper functions */

static void
log_format_timestamp(time_t when, struct string_buffer *sbuf)
{
    struct tm local;
    localtime_r(&when, &local);
    if (sbuf->size < 24) {
        sbuf->size = 24;
        free(sbuf->list);
        sbuf->list = calloc(1, 24);
    }
    sbuf->used = sprintf(sbuf->list, "[%02d:%02d:%02d %02d/%02d/%04d]", local.tm_hour, local.tm_min, local.tm_sec, local.tm_mon+1, local.tm_mday, local.tm_year+1900);
}

static void
log_format_audit(struct logEntry *entry)
{
    struct string_buffer sbuf;
    memset(&sbuf, 0, sizeof(sbuf));
    log_format_timestamp(entry->time, &sbuf);
    string_buffer_append_string(&sbuf, " (");
    string_buffer_append_string(&sbuf, entry->bot->nick);
    if (entry->channel_name) {
        string_buffer_append(&sbuf, ':');
        string_buffer_append_string(&sbuf, entry->channel_name);
    }
    string_buffer_append_string(&sbuf, ") [");
    string_buffer_append_string(&sbuf, entry->user_nick);
    if (entry->user_hostmask) {
        string_buffer_append(&sbuf, '!');
        string_buffer_append_string(&sbuf, entry->user_hostmask);
    }
    if (entry->user_account) {
        string_buffer_append(&sbuf, ':');
        string_buffer_append_string(&sbuf, entry->user_account);
    }
    string_buffer_append_string(&sbuf, "]: ");
    string_buffer_append_string(&sbuf, entry->command);
    entry->default_desc = strdup(sbuf.list);
    free(sbuf.list);
}

/* shared stub log operations act as a noop */

static void
ldNop_reopen(UNUSED_ARG(struct logDestination *self_)) {
    /* no operation necessary */
}

static void
ldNop_replay(UNUSED_ARG(struct logDestination *self_), UNUSED_ARG(struct log_type *type), UNUSED_ARG(int is_write), UNUSED_ARG(const char *line)) {
    /* no operation necessary */
}

/* file: log type */

struct logDest_file {
    struct logDestination base;
    char *fname;
    FILE *output;
};
static struct logDest_vtable ldFile_vtbl;

static struct logDestination *
ldFile_open(const char *args) {
    struct logDest_file *ld;
    ld = calloc(1, sizeof(*ld));
    ld->base.vtbl = &ldFile_vtbl;
    ld->fname = strdup(args);
    ld->output = fopen(ld->fname, "a");
    return &ld->base;
}

static void
ldFile_reopen(struct logDestination *self_) {
    struct logDest_file *self = (struct logDest_file*)self_;
    fclose(self->output);
    self->output = fopen(self->fname, "a");
}

static void
ldFile_close(struct logDestination *self_) {
    struct logDest_file *self = (struct logDest_file*)self_;
    fclose(self->output);
    free(self->fname);
    free(self);
}

static void
ldFile_audit(struct logDestination *self_, UNUSED_ARG(struct log_type *type), struct logEntry *entry) {
    struct logDest_file *self = (struct logDest_file*)self_;
    fputs(entry->default_desc, self->output);
    fputc('\n', self->output);
    fflush(self->output);
}

static void
ldFile_replay(struct logDestination *self_, UNUSED_ARG(struct log_type *type), int is_write, const char *line) {
    struct logDest_file *self = (struct logDest_file*)self_;
    struct string_buffer sbuf;
    memset(&sbuf, 0, sizeof(sbuf));
    log_format_timestamp(now, &sbuf);
    string_buffer_append_string(&sbuf, is_write ? "W: " : "   ");
    string_buffer_append_string(&sbuf, line);
    fputs(sbuf.list, self->output);
    fputc('\n', self->output);
    free(sbuf.list);
    fflush(self->output);
}

static void
ldFile_module(struct logDestination *self_, struct log_type *type, enum log_severity sev, const char *message) {
    struct logDest_file *self = (struct logDest_file*)self_;
    struct string_buffer sbuf;
    memset(&sbuf, 0, sizeof(sbuf));
    log_format_timestamp(now, &sbuf);
    fprintf(self->output, "%s (%s:%s) %s\n", sbuf.list, type->name, log_severity_names[sev], message);
    free(sbuf.list);
    fflush(self->output);
}

static struct logDest_vtable ldFile_vtbl = {
    "file",
    ldFile_open,
    ldFile_reopen,
    ldFile_close,
    ldFile_audit,
    ldFile_replay,
    ldFile_module
};

/* std: log type */

static struct logDest_vtable ldStd_vtbl;

static struct logDestination *
ldStd_open(const char *args) {
    struct logDest_file *ld;
    ld = calloc(1, sizeof(*ld));
    ld->base.vtbl = &ldStd_vtbl;
    ld->fname = strdup(args);

    /* Print to stderr if given "err" and default to stdout otherwise. */
    if (atoi(args))
        ld->output = fdopen(atoi(args), "a");
    else if (!strcasecmp(args, "err"))
        ld->output = stdout;
    else
        ld->output = stderr;

    return &ld->base;
}

static void
ldStd_close(struct logDestination *self_) {
    struct logDest_file *self = (struct logDest_file*)self_;
    free(self->fname);
    free(self);
}

static void
ldStd_replay(struct logDestination *self_, UNUSED_ARG(struct log_type *type), int is_write, const char *line) {
    struct logDest_file *self = (struct logDest_file*)self_;
    fprintf(self->output, "%s%s\n", is_write ? "W: " : "   ", line);
}

static void
ldStd_module(struct logDestination *self_, UNUSED_ARG(struct log_type *type), enum log_severity sev, const char *message) {
    struct logDest_file *self = (struct logDest_file*)self_;
    fprintf(self->output, "%s: %s\n", log_severity_names[sev], message);
}

static struct logDest_vtable ldStd_vtbl = {
    "std",
    ldStd_open,
    ldNop_reopen,
    ldStd_close,
    ldFile_audit,
    ldStd_replay,
    ldStd_module
};

/* irc: log type */

struct logDest_irc {
    struct logDestination base;
    char *target;
};
static struct logDest_vtable ldIrc_vtbl;

static struct logDestination *
ldIrc_open(const char *args) {
    struct logDest_irc *ld;
    ld = calloc(1, sizeof(*ld));
    ld->base.vtbl = &ldIrc_vtbl;
    ld->target = strdup(args);
    return &ld->base;
}

static void
ldIrc_close(struct logDestination *self_) {
    struct logDest_irc *self = (struct logDest_irc*)self_;
    free(self->target);
    free(self);
}

static void
ldIrc_audit(struct logDestination *self_, UNUSED_ARG(struct log_type *type), struct logEntry *entry) {
    struct logDest_irc *self = (struct logDest_irc*)self_;

    if (entry->channel_name) {
        send_target_message(4, self->target, entry->bot, "(%s", strchr(strchr(entry->default_desc, ' '), ':')+1);
    } else {
        send_target_message(4, self->target, entry->bot, "%s", strchr(entry->default_desc, ')')+2);
    }
}

static void
ldIrc_module(struct logDestination *self_, struct log_type *type, enum log_severity sev, const char *message) {
    struct logDest_irc *self = (struct logDest_irc*)self_;
    extern struct userNode *opserv;

    send_target_message(4, self->target, opserv, "%s %s: %s\n", type->name, log_severity_names[sev], message);
}

static struct logDest_vtable ldIrc_vtbl = {
    "irc",
    ldIrc_open,
    ldNop_reopen,
    ldIrc_close,
    ldIrc_audit,
    ldNop_replay, /* totally ignore this - it would be a recipe for disaster */
    ldIrc_module
};

void
log_init(void)
{
    log_types = dict_new();
    dict_set_free_keys(log_types, free);
    dict_set_free_data(log_types, log_type_free);
    log_dest_types = dict_new();
    /* register log types */
    dict_insert(log_dest_types, ldFile_vtbl.type_name, &ldFile_vtbl);
    dict_insert(log_dest_types, ldStd_vtbl.type_name, &ldStd_vtbl);
    dict_insert(log_dest_types, ldIrc_vtbl.type_name, &ldIrc_vtbl);
    conf_register_reload(log_conf_read);
    log_default = log_register_type("*", NULL);
    reg_exit_func(cleanup_logs);
    message_register_table(msgtab);
    log_inited = 1;
}

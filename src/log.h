/* log.h - Diagnostic and error logging
 * Copyright 2000-2003 srvx Development Team
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

#ifndef LOG_H
#define LOG_H

#include "common.h"

enum log_severity {
    LOG_REPLAY,   /* 0 */
    LOG_DEBUG,
    LOG_COMMAND,
    LOG_INFO,
    LOG_OVERRIDE,
    LOG_STAFF,    /* 5 */
    LOG_WARNING,
    LOG_ERROR,
    LOG_FATAL,
    LOG_NUM_SEVERITIES
};

struct log_type;

void log_init(void);
void log_reopen(void);
void log_debug(void);

/* bitmap values in flags parameter to log_audit */
#define AUDIT_HOSTMASK  0x01

struct log_type *log_register_type(const char *name, const char *default_log);
/* constraint for log_audit: sev one of LOG_COMMAND, LOG_OVERRIDE, LOG_STAFF */
void log_audit(struct log_type *type, enum log_severity sev, struct userNode *user, struct userNode *bot, const char *channel_name, unsigned int flags, const char *command);
/* constraint for log_module: sev < LOG_COMMAND */
void log_module(struct log_type *type, enum log_severity sev, const char *format, ...) PRINTF_LIKE(3, 4);
void log_replay(struct log_type *type, int is_write, const char *line);

/* Log searching functions - ONLY searches log_audit'ed data */

struct logEntry
{
                                      /* field nullable in real entries? */
    time_t            time;
    enum log_severity slvl;
    struct userNode   *bot;           /* no */
    char              *channel_name;  /* yes */
    char              *user_nick;     /* no */
    char              *user_account;  /* yes */
    char              *user_hostmask; /* yes */
    char              *command;       /* no */
    char              *default_desc;
    struct logEntry   *next;
    struct logEntry   *prev;
};

struct logSearch
{
    struct logEntry  masks;
    struct log_type  *type;
    time_t           min_time;
    time_t           max_time;
    unsigned int     limit;
    unsigned int     severities;
};

struct logReport
{
    struct userNode  *reporter;
    struct userNode  *user;
};

typedef void (*entry_search_func)(struct logEntry *match, void *extra);
void log_report_entry(struct logEntry *match, void *extra);
struct logSearch* log_discrim_create(struct userNode *service, struct userNode *user, unsigned int argc, char *argv[]);
unsigned int log_entry_search(struct logSearch *discrim, entry_search_func esf, void *data);
void report_entry(struct userNode *service, struct userNode *user, struct logEntry *entry);

#endif

/* Blacklist module for srvx 1.x
 * Copyright 2007 Michael Poole <mdpoole@troilus.org>
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
#include "gline.h"
#include "modcmd.h"
#include "proto.h"

const char *blacklist_module_deps[] = { NULL };

static struct log_type *bl_log;
static dict_t blacklist_hosts; /* maps IPs or hostnames to reasons from blacklist_reasons */
static dict_t blacklist_reasons; /* maps strings to themselves (poor man's data sharing) */

static struct {
    unsigned long gline_duration;
} conf;

static int
blacklist_check_user(struct userNode *user)
{
    const char *reason;
    const char *host;
    char ip[IRC_NTOP_MAX_SIZE];

    irc_ntop(ip, sizeof(ip), &user->ip);
    reason = dict_find(blacklist_hosts, host = ip, NULL);
    if (reason == NULL) {
        reason = dict_find(blacklist_hosts, host = user->hostname, NULL);
    }
    if (reason != NULL) {
        char *target;
        target = alloca(strlen(host) + 3);
        target[0] = '*';
        target[1] = '@';
        strcpy(target + 2, host);
        gline_add(self->name, target, conf.gline_duration, reason, now, now, 1);
    }
    return 0;
}

static void
blacklist_load_file(const char *filename, const char *default_reason)
{
    FILE *file;
    const char *reason;
    char *mapped_reason;
    char *sep;
    size_t len;
    char linebuf[MAXLEN];

    if (!filename)
        return;
    if (!default_reason)
        default_reason = "client is blacklisted";
    file = fopen(filename, "r");
    if (!file) {
        log_module(bl_log, LOG_ERROR, "Unable to open %s for reading: %s", filename, strerror(errno));
        return;
    }
    log_module(bl_log, LOG_DEBUG, "Loading blacklist from %s.", filename);
    while (fgets(linebuf, sizeof(linebuf), file)) {
        /* Trim whitespace from end of line. */
        len = strlen(linebuf);
        while (isspace(linebuf[len-1]))
            linebuf[--len] = '\0';

        /* Figure out which reason string we should use. */
        reason = default_reason;
        sep = strchr(linebuf, ' ');
        if (sep) {
            *sep++ = '\0';
            while (isspace(*sep))
                sep++;
            if (*sep != '\0')
                reason = sep;
        }

        /* See if the reason string is already known. */
        mapped_reason = dict_find(blacklist_reasons, reason, NULL);
        if (!mapped_reason) {
            mapped_reason = strdup(reason);
            dict_insert(blacklist_reasons, mapped_reason, (char*)mapped_reason);
        }

        /* Store the blacklist entry. */
        dict_insert(blacklist_hosts, strdup(linebuf), mapped_reason);
    }
    fclose(file);
}

static void
blacklist_conf_read(void)
{
    dict_t node;
    const char *str1;
    const char *str2;

    dict_delete(blacklist_hosts);
    blacklist_hosts = dict_new();
    dict_set_free_keys(blacklist_hosts, free);

    dict_delete(blacklist_reasons);
    blacklist_reasons = dict_new();
    dict_set_free_keys(blacklist_reasons, free);

    node = conf_get_data("modules/blacklist", RECDB_OBJECT);
    if (node == NULL)
        return;

    str1 = database_get_data(node, "file", RECDB_QSTRING);
    str2 = database_get_data(node, "file_reason", RECDB_QSTRING);
    blacklist_load_file(str1, str2);

    str1 = database_get_data(node, "gline_duration", RECDB_QSTRING);
    if (str1 == NULL)
        str1 = "1h";
    conf.gline_duration = ParseInterval(str1);
}

static void
blacklist_cleanup(void)
{
    dict_delete(blacklist_hosts);
    dict_delete(blacklist_reasons);
}

int
blacklist_init(void)
{
    bl_log = log_register_type("Blacklist", "file:blacklist.log");
    conf_register_reload(blacklist_conf_read);
    reg_new_user_func(blacklist_check_user);
    reg_exit_func(blacklist_cleanup);
    return 1;
}

int
blacklist_finalize(void)
{
    return 1;
}

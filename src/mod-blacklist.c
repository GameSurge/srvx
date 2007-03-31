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
#include "sar.h"

const char *blacklist_module_deps[] = { NULL };

struct dnsbl_zone {
    struct string_list reasons;
    const char *description;
    const char *reason;
    unsigned int duration;
    unsigned int mask;
    char zone[1];
};

struct dnsbl_data {
    char client_ip[IRC_NTOP_MAX_SIZE];
    char zone_name[1];
};

static struct log_type *bl_log;
static dict_t blacklist_zones; /* contains struct dnsbl_zone */
static dict_t blacklist_hosts; /* maps IPs or hostnames to reasons from blacklist_reasons */
static dict_t blacklist_reasons; /* maps strings to themselves (poor man's data sharing) */

static struct {
    unsigned long gline_duration;
} conf;

static void
do_expandos(char *output, unsigned int out_len, const char *input, ...)
{
    va_list args;
    const char *key;
    const char *datum;
    char *found;
    unsigned int klen;
    unsigned int dlen;
    unsigned int rlen;

    safestrncpy(output, input, out_len);
    va_start(args, input);
    while ((key = va_arg(args, const char*)) != NULL) {
        datum = va_arg(args, const char *);
        klen = strlen(key);
        dlen = strlen(datum);
        for (found = output; (found = strstr(output, key)) != NULL; found += dlen) {
            rlen = strlen(found + klen);
            if ((dlen > klen) && ((unsigned)(found + dlen + rlen - output) > out_len))
                rlen = output + out_len - found - dlen;
            memmove(found + dlen, found + klen, rlen);
            memcpy(found, datum, dlen + 1);
        }
    }
    va_end(args);
}

static void
dnsbl_hit(struct sar_request *req, struct dns_header *hdr, struct dns_rr *rr, unsigned char *raw, unsigned int raw_size)
{
    struct dnsbl_data *data;
    struct dnsbl_zone *zone;
    const char *message;
    char *txt;
    unsigned int mask;
    unsigned int pos;
    unsigned int len;
    unsigned int ii;
    char reason[MAXLEN];
    char target[IRC_NTOP_MAX_SIZE + 2];

    /* Get the DNSBL zone (to make sure it has not disappeared in a rehash). */
    data = (struct dnsbl_data*)(req + 1);
    zone = dict_find(blacklist_zones, data->zone_name, NULL);
    if (!zone)
        return;

    /* Scan the results. */
    for (mask = 0, ii = 0, txt = NULL; ii < hdr->ancount; ++ii) {
        pos = rr[ii].rd_start;
        switch (rr[ii].type) {
        case REQ_TYPE_A:
            if (rr[ii].rdlength != 4)
                break;
            if (pos + 3 < raw_size)
                mask |= (1 << raw[pos + 3]);
            break;
        case REQ_TYPE_TXT:
            len = raw[pos];
            txt = malloc(len + 1);
            memcpy(txt, raw + pos + 1, len);
            txt[len] = '\0';
            break;
        }
    }

    /* Do we care about one of the masks we found? */
    if (mask & zone->mask) {
        /* See if a per-result message was provided. */
        for (ii = 0, message = NULL; mask && (ii < zone->reasons.used); ++ii, mask >>= 1) {
            if (0 == (mask & 1))
                continue;
            if (NULL != (message = zone->reasons.list[ii]))
                break;
        }

        /* If not, use a standard fallback. */
        if (message == NULL) {
            message = zone->reason;
            if (message == NULL)
                message = "client is blacklisted";
        }

        /* Expand elements of the message as necessary. */
        do_expandos(reason, sizeof(reason), message, "%txt%", (txt ? txt : "(no-txt)"), "%ip%", data->client_ip, NULL);

        /* Now generate the G-line. */
        target[0] = '*';
        target[1] = '@';
        strcpy(target + 2, data->client_ip);
        gline_add(self->name, target, zone->duration, reason, now, now, 1);
    }
    free(txt);
}

static int
blacklist_check_user(struct userNode *user)
{
    static const char *hexdigits = "0123456789abcdef";
    dict_iterator_t it;
    const char *reason;
    const char *host;
    unsigned int dnsbl_len;
    unsigned int ii;
    char ip[IRC_NTOP_MAX_SIZE];
    char dnsbl_target[128];

    /* Users with bogus IPs are probably service bots. */
    if (!irc_in_addr_is_valid(user->ip))
        return 0;

    /* Check local file-based blacklist. */
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

    /* Figure out the base part of a DNS blacklist hostname. */
    if (irc_in_addr_is_ipv4(user->ip)) {
        dnsbl_len = snprintf(dnsbl_target, sizeof(dnsbl_target), "%d.%d.%d.%d.", user->ip.in6_8[15], user->ip.in6_8[14], user->ip.in6_8[13], user->ip.in6_8[12]);
    } else if (irc_in_addr_is_ipv6(user->ip)) {
        for (ii = 0; ii < 16; ++ii) {
            dnsbl_target[ii * 4 + 0] = hexdigits[user->ip.in6_8[15 - ii] & 15];
            dnsbl_target[ii * 4 + 1] = '.';
            dnsbl_target[ii * 4 + 2] = hexdigits[user->ip.in6_8[15 - ii] >> 4];
            dnsbl_target[ii * 4 + 3] = '.';
        }
        dnsbl_len = 48;
    } else {
        return 0;
    }

    /* Start a lookup for the appropriate hostname in each DNSBL. */
    for (it = dict_first(blacklist_zones); it; it = iter_next(it)) {
        struct dnsbl_data *data;
        struct sar_request *req;
        const char *zone;

        zone = iter_key(it);
        safestrncpy(dnsbl_target + dnsbl_len, zone, sizeof(dnsbl_target) - dnsbl_len);
        req = sar_request_simple(sizeof(*data) + strlen(zone), dnsbl_hit, NULL, dnsbl_target, REQ_QTYPE_ALL, NULL);
        if (req) {
            data = (struct dnsbl_data*)(req + 1);
            strcpy(data->client_ip, ip);
            strcpy(data->zone_name, zone);            
        }
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
dnsbl_zone_free(void *pointer)
{
    struct dnsbl_zone *zone;
    zone = pointer;
    free(zone->reasons.list);
    free(zone);
}

static void
blacklist_conf_read(void)
{
    dict_t node;
    dict_t subnode;
    const char *str1;
    const char *str2;

    dict_delete(blacklist_zones);
    blacklist_zones = dict_new();
    dict_set_free_data(blacklist_zones, free);

    dict_delete(blacklist_hosts);
    blacklist_hosts = dict_new();
    dict_set_free_keys(blacklist_hosts, free);

    dict_delete(blacklist_reasons);
    blacklist_reasons = dict_new();
    dict_set_free_keys(blacklist_reasons, dnsbl_zone_free);

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

    subnode = database_get_data(node, "dnsbl", RECDB_OBJECT);
    if (subnode) {
        static const char *reason_prefix = "reason_";
        static const unsigned int max_id = 255;
        struct dnsbl_zone *zone;
        dict_iterator_t it;
        dict_iterator_t it2;
        dict_t dnsbl;
        unsigned int id;

        for (it = dict_first(subnode); it; it = iter_next(it)) {
            dnsbl = GET_RECORD_OBJECT((struct record_data*)iter_data(it));
            if (!dnsbl)
                continue;

            zone = malloc(sizeof(*zone) + strlen(iter_key(it)));
            strcpy(zone->zone, iter_key(it));
            zone->description = database_get_data(dnsbl, "description", RECDB_QSTRING);
            zone->reason = database_get_data(dnsbl, "reason", RECDB_QSTRING);
            str1 = database_get_data(dnsbl, "duration", RECDB_QSTRING);
            zone->duration = str1 ? ParseInterval(str1) : 3600;
            str1 = database_get_data(dnsbl, "mask", RECDB_QSTRING);
            zone->mask = str1 ? strtoul(str1, NULL, 0) : ~0u;
            zone->reasons.used = 0;
            zone->reasons.size = 0;
            zone->reasons.list = NULL;
            dict_insert(blacklist_zones, zone->zone, zone);

            for (it2 = dict_first(dnsbl); it2; it2 = iter_next(it2)) {
                str1 = GET_RECORD_QSTRING((struct record_data*)(iter_data(it2)));
                if (!str1 || memcmp(iter_key(it2), reason_prefix, strlen(reason_prefix)))
                    continue;
                id = strtoul(iter_key(it2) + strlen(reason_prefix), NULL, 0);
                if (id > max_id) {
                    log_module(bl_log, LOG_ERROR, "Invalid code for DNSBL %s %s -- only %d responses supported.", iter_key(it), iter_key(it2), max_id);
                    continue;
                }
                if (zone->reasons.size < id + 1) {
                    zone->reasons.size = id + 1;
                    zone->reasons.list = realloc(zone->reasons.list, zone->reasons.size * sizeof(zone->reasons.list[0]));
                }
                zone->reasons.list[id] = (char*)str1;
                if (zone->reasons.used < id + 1)
                    zone->reasons.used = id + 1;
            }
        }
    }
}

static void
blacklist_cleanup(void)
{
    dict_delete(blacklist_zones);
    dict_delete(blacklist_hosts);
    dict_delete(blacklist_reasons);
}

int
blacklist_init(void)
{
    bl_log = log_register_type("blacklist", "file:blacklist.log");
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

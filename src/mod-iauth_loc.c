/* IAuth Login-on-Connect module for srvx 1.x
 * Copyright 2012 Michael Poole <mdpoole@troilus.org>
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
#include "nickserv.h"

const char *iauth_loc_module_deps[] = { NULL };

static struct log_type *loc_log;

static struct {
    struct userNode *debug_bot;
    struct chanNode *debug_channel;
} conf;

#if defined(GCC_VARMACROS)
# define loc_debug(ARGS...) do { if (conf.debug_bot && conf.debug_channel) send_channel_notice(conf.debug_channel, conf.debug_bot, ARGS); } while (0)
#elif defined(C99_VARMACROS)
# define loc_debug(...) do { if (conf.debug_bot && conf.debug_channel) send_channel_notice(conf.debug_channel, conf.debug_bot, __VA_ARGS__); } while (0)
#endif

static void
iauth_loc_xquery(struct server *source, const char routing[], const char query[])
{
    if (!strncmp(query, "LOGIN2 ", strlen("LOGIN2 "))) {
        /* Make "user" static for better valgrind tests. */
        static struct userNode user;
        const char *ip_str, *hostname, *username, *account, *password;
        struct handle_info *hi = NULL;
        char *qdup, *saveptr = NULL;
        unsigned int ii;
        int valid = 0;

        /* Parse the arguments. */
        qdup = strdup(query + strlen("LOGIN2 "));
        ip_str   = strtok_r(qdup, " ", &saveptr);
        hostname = strtok_r(NULL, " ", &saveptr);
        username = strtok_r(NULL, " ", &saveptr);
        account  = strtok_r(NULL, " ", &saveptr);
        password = strtok_r(NULL, " ", &saveptr);
        if (!password) {
        login2_bad_syntax:
            irc_xresponse(source, routing, "NO Bad LOGIN2 syntax");
            free(qdup);
            return;
        }
        if (account[0] == ':')
            account++;

        /* Set up (the rest of) the fake user. */
        user.nick = "?";
        if (!irc_pton(&user.ip, NULL, ip_str))
            goto login2_bad_syntax;
        strncpy(user.ident, username, sizeof(user.ident));
        strncpy(user.hostname, hostname, sizeof(user.hostname));

        /* Check against the account. */
        hi = get_handle_info(account);
        if (hi && (hi->masks->used == 0))
            valid = 1;
        for (ii = 0; hi && (ii < hi->masks->used); ++ii) {
            if (user_matches_glob(&user, hi->masks->list[ii], 0))
                valid = 1;
        }
        if (hi && !checkpass(password, hi->passwd))
            valid = 0;

        /* Send our response. */
        free(qdup);
        if (!valid) {
            irc_xresponse(source, routing, "AGAIN Bad username, account or source");
        } else if (hi && HANDLE_FLAGGED(hi, SUSPENDED)) {
            irc_xresponse(source, routing, "AGAIN That account is suspended");
        } else {
            char response[68];
            snprintf(response, sizeof(response), "OK %s:%lu", hi->handle, hi->registered);
            irc_xresponse(source, routing, response);
        }
    } /* else unknown or unsupported command */
}

static void
iauth_loc_conf_read(void)
{
    dict_t node;
    const char *str1;
    const char *str2;


    node = conf_get_data("modules/blacklist", RECDB_OBJECT);
    if (node == NULL)
        return;

    str1 = database_get_data(node, "debug_bot", RECDB_QSTRING);
    if (str1)
        conf.debug_bot = GetUserH(str1);

    str1 = database_get_data(node, "debug_channel", RECDB_QSTRING);
    if (conf.debug_bot && str1) {
        str2 = database_get_data(node, "debug_channel_modes", RECDB_QSTRING);
        if (!str2)
            str2 = "+tinms";
        conf.debug_channel = AddChannel(str1, now, str2, NULL);
        AddChannelUser(conf.debug_bot, conf.debug_channel)->modes |= MODE_CHANOP;
    } else {
        conf.debug_channel = NULL;
    }
}

int
iauth_loc_init(void)
{
    loc_log = log_register_type("iauth_loc", "file:iauth_loc.log");
    conf_register_reload(iauth_loc_conf_read);
    reg_xquery_func(iauth_loc_xquery);
    return 1;
}

int
iauth_loc_finalize(void)
{
    return 1;
}

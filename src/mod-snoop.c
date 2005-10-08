/* mod-snoop.c - User surveillance module (per pomac's spec)
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

/* Adds new section to srvx.conf:
 * "modules" {
 *     "snoop" {
 *         // Where to send snoop messages?
 *         "channel" "#wherever";
 *         // Which bot?
 *         "bot" "OpServ";
 *         // Show new users and joins from net joins?  (off by default)
 *         "show_bursts" "0";
 *     };
 * };
 */

#include "conf.h"
#include "helpfile.h"
#include "nickserv.h"

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

extern time_t now;
static struct {
    struct chanNode *channel;
    struct userNode *bot;
    unsigned int show_bursts : 1;
    unsigned int enabled : 1;
} snoop_cfg;
static char timestamp[16];
const char *snoop_module_deps[] = { NULL };

static int finalized;
int snoop_finalize(void);

#define SNOOP(FORMAT, ARGS...) send_channel_message(snoop_cfg.channel, snoop_cfg.bot, "%s "FORMAT, timestamp , ## ARGS)
#define UPDATE_TIMESTAMP() strftime(timestamp, sizeof(timestamp), "[%H:%M:%S]", localtime(&now))

static void
snoop_nick_change(struct userNode *user, const char *old_nick) {
    if (!snoop_cfg.enabled) return;
    UPDATE_TIMESTAMP();
    SNOOP("$bNICK$b change %s -> %s", old_nick, user->nick);
}

static int
snoop_join(struct modeNode *mNode) {
    struct userNode *user = mNode->user;
    struct chanNode *chan = mNode->channel;
    if (!snoop_cfg.enabled) return 0;
    if (user->uplink->burst && !snoop_cfg.show_bursts) return 0;
    UPDATE_TIMESTAMP();
    if (chan->members.used == 1) {
        SNOOP("$bCREATE$b %s by %s", chan->name, user->nick);
    } else {
        SNOOP("$bJOIN$b %s by %s", chan->name, user->nick);
    }
    return 0;
}

static void
snoop_part(struct modeNode *mn, const char *reason) {
    if (!snoop_cfg.enabled) return;
    if (mn->user->dead) return;
    UPDATE_TIMESTAMP();
    SNOOP("$bPART$b %s by %s (%s)", mn->channel->name, mn->user->nick, reason ? reason : "");
}

static void
snoop_kick(struct userNode *kicker, struct userNode *victim, struct chanNode *chan) {
    if (!snoop_cfg.enabled) return;
    UPDATE_TIMESTAMP();
    SNOOP("$bKICK$b %s from %s by %s", victim->nick, chan->name, (kicker ? kicker->nick : "some server"));
}

static int
snoop_new_user(struct userNode *user) {
    if (!snoop_cfg.enabled) return 0;
    if (user->uplink->burst && !snoop_cfg.show_bursts) return 0;
    UPDATE_TIMESTAMP();
    SNOOP("$bNICK$b %s %s@%s [%s] on %s", user->nick, user->ident, user->hostname, irc_ntoa(&user->ip), user->uplink->name);
    return 0;
}

static void
snoop_del_user(struct userNode *user, struct userNode *killer, const char *why) {
    if (!snoop_cfg.enabled) return;
    UPDATE_TIMESTAMP();
    if (killer) {
        SNOOP("$bKILL$b %s (%s@%s, on %s) by %s (%s)", user->nick, user->ident, user->hostname, user->uplink->name, killer->nick, why);
    } else {
        SNOOP("$bQUIT$b %s (%s@%s, on %s) (%s)", user->nick, user->ident, user->hostname, user->uplink->name, why);
    }
}

static void
snoop_auth(struct userNode *user, UNUSED_ARG(struct handle_info *old_handle)) {
    if (!snoop_cfg.enabled) return;
    if (user->uplink->burst && !snoop_cfg.show_bursts) return;
    if (user->handle_info) {
        UPDATE_TIMESTAMP();
        SNOOP("$bAUTH$b %s as %s", user->nick, user->handle_info->handle);
    }
}

static void
snoop_conf_read(void) {
    dict_t node;
    char *str;

    node = conf_get_data("modules/snoop", RECDB_OBJECT);
    if (!node)
        return;
    str = database_get_data(node, "channel", RECDB_QSTRING);
    if (!str)
        return;
    snoop_cfg.channel = AddChannel(str, now, "+sntim", NULL);
    if (!snoop_cfg.channel)
        return;
    str = database_get_data(node, "show_bursts", RECDB_QSTRING);
    snoop_cfg.show_bursts = str ? enabled_string(str) : 0;
    snoop_cfg.enabled = 1;
    if (finalized)
        snoop_finalize();
}

void
snoop_cleanup(void) {
    snoop_cfg.enabled = 0;
    unreg_del_user_func(snoop_del_user);
}

int
snoop_init(void) {
    reg_exit_func(snoop_cleanup);
    conf_register_reload(snoop_conf_read);
    reg_nick_change_func(snoop_nick_change);
    reg_join_func(snoop_join);
    reg_part_func(snoop_part);
    reg_kick_func(snoop_kick);
    reg_new_user_func(snoop_new_user);
    reg_del_user_func(snoop_del_user);
    reg_auth_func(snoop_auth);
    /* Not implemented since hooks don't exist or lack data desired:
     * chanmode (issuing user not listed)
     * usermode (no hook)
     */
    return 1;
}

int
snoop_finalize(void) {
    struct mod_chanmode change;
    dict_t node;
    char *str;

    finalized = 1;
    node = conf_get_data("modules/snoop", RECDB_OBJECT);
    if (!node)
        return 0;
    str = database_get_data(node, "bot", RECDB_QSTRING);
    if (!str)
        return 0;
    snoop_cfg.bot = GetUserH(str);
    if (!snoop_cfg.bot)
        return 0;
    mod_chanmode_init(&change);
    change.argc = 1;
    change.args[0].mode = MODE_CHANOP;
    change.args[0].u.member = AddChannelUser(snoop_cfg.bot, snoop_cfg.channel);
    mod_chanmode_announce(snoop_cfg.bot, snoop_cfg.channel, &change);
    return 1;
}

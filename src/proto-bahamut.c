/* proto-bahamut.c - IRC protocol output
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

#include "proto-common.c"

#define CAPAB_TS3       0x01
#define CAPAB_NOQUIT    0x02
#define CAPAB_SSJOIN    0x04
#define CAPAB_BURST     0x08
#define CAPAB_UNCONNECT 0x10
#define CAPAB_NICKIP    0x20
#define CAPAB_TSMODE    0x40
#define CAPAB_ZIP       0x80

struct service_message_info {
    privmsg_func_t on_privmsg;
    privmsg_func_t on_notice;
};

static dict_t service_msginfo_dict; /* holds service_message_info structs */
static int uplink_capab;
static void privmsg_user_helper(struct userNode *un, void *data);

void irc_svsmode(struct userNode *target, char *modes, unsigned long stamp);

struct server *
AddServer(struct server *uplink, const char *name, int hops, time_t boot, time_t link, UNUSED_ARG(const char *numeric), const char *description) {
    struct server* sNode;

    sNode = calloc(1, sizeof(*sNode));
    sNode->uplink = uplink;
    safestrncpy(sNode->name, name, sizeof(sNode->name));
    sNode->hops = hops;
    sNode->boot = boot;
    sNode->link = link;
    sNode->users = dict_new();
    safestrncpy(sNode->description, description, sizeof(sNode->description));
    serverList_init(&sNode->children);
    if (sNode->uplink) {
        /* uplink may be NULL if we're just building ourself */
        serverList_append(&sNode->uplink->children, sNode);
    }
    dict_insert(servers, sNode->name, sNode);

    if (hops && !self->burst) {
        unsigned int n;
        for (n=0; n<slf_used; n++) {
            slf_list[n](sNode);
        }
    }

    return sNode;
}

void
DelServer(struct server* serv, int announce, const char *message) {
    unsigned int nn;
    dict_iterator_t it, next;

    if (!serv) return;
    if (announce && (serv->uplink == self) && (serv != self->uplink)) {
        irc_squit(serv, message, NULL);
    }
    for (nn=serv->children.used; nn>0;) {
        if (serv->children.list[--nn] != self) {
            DelServer(serv->children.list[nn], false, "uplink delinking");
        }
    }
    for (it=dict_first(serv->users); it; it=next) {
        next = iter_next(it);
        DelUser(iter_data(it), NULL, false, "server delinking");
    }
    if (serv->uplink) serverList_remove(&serv->uplink->children, serv);
    if (serv == self->uplink) self->uplink = NULL;
    dict_remove(servers, serv->name);
    serverList_clean(&serv->children);
    dict_delete(serv->users);
    free(serv);
}

int
is_valid_nick(const char *nick) {
    /* IRC has some of The Most Fucked-Up ideas about character sets
     * in the world.. */
    if ((*nick < 'A') || (*nick >= '~')) return 0;
    for (++nick; *nick; ++nick) {
        if (!((*nick >= 'A') && (*nick < '~'))
            && !isdigit(*nick)
            && (*nick != '-')) {
            return 0;
        }
    }
    if (strlen(nick) > nicklen) return 0;
    return 1;
}

struct userNode *
AddUser(struct server* uplink, const char *nick, const char *ident, const char *hostname, const char *modes, const char *userinfo, time_t timestamp, struct in_addr realip, const char *stamp) {
    struct userNode *uNode, *oldUser;
    unsigned int nn;

    if (!uplink) {
        log_module(MAIN_LOG, LOG_WARNING, "AddUser(%p, %s, ...): server does not exist!", uplink, nick);
        return NULL;
    }

    if (!is_valid_nick(nick)) {
        log_module(MAIN_LOG, LOG_WARNING, "AddUser(%p, %s, ...): invalid nickname detected.", uplink, nick);
        return NULL;
    }

    if ((oldUser = GetUserH(nick))) {
        if (IsLocal(oldUser) && IsService(oldUser)) {
            /* The service should collide the new user off. */
            oldUser->timestamp = timestamp - 1;
            irc_user(oldUser);
        }
        if (oldUser->timestamp > timestamp) {
            /* "Old" user is really newer; remove them */
            DelUser(oldUser, 0, 1, "Overruled by older nick");
        } else {
            /* User being added is too new */
            return NULL;
        }
    }

    uNode = calloc(1, sizeof(*uNode));
    uNode->nick = strdup(nick);
    safestrncpy(uNode->ident, ident, sizeof(uNode->ident));
    safestrncpy(uNode->info, userinfo, sizeof(uNode->info));
    safestrncpy(uNode->hostname, hostname, sizeof(uNode->hostname));
    uNode->ip = realip;
    uNode->timestamp = timestamp;
    modeList_init(&uNode->channels);
    uNode->uplink = uplink;
    dict_insert(uplink->users, uNode->nick, uNode);
    if (++uNode->uplink->clients > uNode->uplink->max_clients) {
        uNode->uplink->max_clients = uNode->uplink->clients;
    }

    dict_insert(clients, uNode->nick, uNode);
    if (dict_size(clients) > max_clients) {
        max_clients = dict_size(clients);
        max_clients_time = now;
    }

    mod_usermode(uNode, modes);
    if (stamp) call_account_func(uNode, stamp);
    if (IsLocal(uNode)) irc_user(uNode);
    for (nn=0; nn<nuf_used; nn++) {
        if (nuf_list[nn](uNode)) break;
    }
    return uNode;
}

struct userNode *
AddService(const char *nick, const char *desc, const char *hostname) {
    time_t timestamp = now;
    struct userNode *old_user = GetUserH(nick);
    struct in_addr ipaddr = { INADDR_LOOPBACK };
    if (old_user) {
        if (IsLocal(old_user))
            return old_user;
        timestamp = old_user->timestamp - 1;
    }
    if (!hostname)
        hostname = self->name;
    return AddUser(self, nick, nick, hostname, "+oikr", desc, timestamp, ipaddr, 0);
}

struct userNode *
AddClone(const char *nick, const char *ident, const char *hostname, const char *desc) {
    time_t timestamp = now;
    struct userNode *old_user = GetUserH(nick);
    struct in_addr ipaddr = { INADDR_LOOPBACK };
    if (old_user) {
        if (IsLocal(old_user))
            return old_user;
        timestamp = old_user->timestamp - 1;
    }
    return AddUser(self, nick, ident, hostname, "+ir", desc, timestamp, ipaddr, 0);
}

void
free_user(struct userNode *user)
{
    free(user->nick);
    free(user);
}

void
DelUser(struct userNode* user, struct userNode *killer, int announce, const char *why) {
    unsigned int nn;

    for (nn=user->channels.used; nn>0;) {
        DelChannelUser(user, user->channels.list[--nn]->channel, false, 0);
    }
    for (nn=duf_used; nn>0; ) duf_list[--nn](user, killer, why);
    user->uplink->clients--;
    dict_remove(user->uplink->users, user->nick);
    if (IsOper(user)) userList_remove(&curr_opers, user);
    if (IsInvisible(user)) invis_clients--;
    if (user == dict_find(clients, user->nick, NULL)) dict_remove(clients, user->nick);
    if (announce) {
        if (IsLocal(user)) {
            irc_quit(user, why);
        } else {
            irc_kill(killer, user, why);
        }
    }
    modeList_clean(&user->channels);
    user->dead = 1;
    if (dead_users.size) {
        userList_append(&dead_users, user);
    } else {
        free_user(user);
    }
}

void
irc_server(struct server *srv) {
    if (srv == self) {
        putsock("SERVER %s %d :%s", srv->name, srv->hops, srv->description);
    } else {
        putsock(":%s SERVER %s %d :%s", self->name, srv->name, srv->hops, srv->description);
    }
}

void
irc_user(struct userNode *user) {
    char modes[32];
    int modelen = 0;
    if (IsOper(user)) modes[modelen++] = 'o';
    if (IsInvisible(user)) modes[modelen++] = 'i';
    if (IsWallOp(user)) modes[modelen++] = 'w';
    if (IsService(user)) modes[modelen++] = 'k';
    if (IsServNotice(user)) modes[modelen++] = 's';
    if (IsDeaf(user)) modes[modelen++] = 'd';
    if (IsReggedNick(user)) modes[modelen++] = 'r';
    if (IsGlobal(user)) modes[modelen++] = 'g';
    modes[modelen] = 0;
    putsock("NICK %s %d "FMT_TIME_T" +%s %s %s %s %d %u :%s",
            user->nick, user->uplink->hops+2, user->timestamp, modes,
            user->ident, user->hostname, user->uplink->name, 0, ntohl(user->ip.s_addr), user->info);
}

void
irc_account(struct userNode *user, const char *stamp)
{
    if (IsReggedNick(user)) {
        irc_svsmode(user, "+rd", base64toint(stamp, IDLEN));
    } else {
        irc_svsmode(user, "+d", base64toint(stamp, IDLEN));
    }
}

void
irc_fakehost(UNUSED_ARG(struct userNode *user), UNUSED_ARG(const char *host))
{
    /* not supported in bahamut */
}

void
irc_regnick(struct userNode *user)
{
    if (IsReggedNick(user)) {
        irc_svsmode(user, "+r", 0);
    } else {
        irc_svsmode(user, "-r", 0);
    }
}

void
irc_nick(struct userNode *user, const char *old_nick) {
    if (user->uplink == self) {
        /* update entries in PRIVMSG/NOTICE handlers (if they exist) */
        struct service_message_info *smi = dict_find(service_msginfo_dict, user->nick, NULL);
        if (smi) {
            dict_remove2(service_msginfo_dict, old_nick, 1);
            dict_insert(service_msginfo_dict, user->nick, smi);
        }
    }
    putsock(":%s NICK %s :"FMT_TIME_T, old_nick, user->nick, user->timestamp);
}

void
irc_pass(const char *passwd) {
    putsock("PASS %s :TS", passwd);
}

void
irc_capab() {
    putsock("CAPAB TS3 NOQUIT SSJOIN BURST UNCONNECT NICKIP TSMODE");
}

void
irc_svinfo() {
    putsock("SVINFO 3 3 0 :"FMT_TIME_T, now);
}

void
irc_introduce(const char *passwd) {
    extern time_t burst_begin;

    irc_pass(passwd);
    irc_capab();
    irc_server(self);
    irc_svinfo();
    burst_length = 0;
    burst_begin = now;
    timeq_add(now + ping_freq, timed_send_ping, 0);
}

void
irc_ping(const char *something) {
    putsock("PING :%s", something);
}

void
irc_pong(const char *who, const char *data) {
    putsock(":%s PONG %s :%s", self->name, who, data);
}

void
irc_quit(struct userNode *user, const char *message) {
    putsock(":%s QUIT :%s", user->nick, message);
}

void
irc_squit(struct server *srv, const char *message, const char *service_message) {
    if (!service_message) service_message = message;
    /* If we're leaving the network, QUIT all our clients. */
    if ((srv == self) && (cManager.uplink->state == CONNECTED)) {
        dict_iterator_t it;
        for (it = dict_first(srv->users); it; it = iter_next(it)) {
            irc_quit(iter_data(it), service_message);
        }
    }
    putsock(":%s SQUIT %s 0 :%s", self->name, srv->name, message);
    if (srv == self) {
        /* Reconnect to the currently selected server. */
        cManager.uplink->tries = 0;
        log_module(MAIN_LOG, LOG_INFO, "Squitting from uplink: %s", message);
        close_socket();
    }
}

void
irc_privmsg(struct userNode *from, const char *to, const char *message) {
    putsock(":%s PRIVMSG %s :%s", from->nick, to, message);
}

void
irc_notice(struct userNode *from, const char *to, const char *message) {
    putsock(":%s NOTICE %s :%s", from->nick, to, message);
}

void
irc_notice_user(struct userNode *from, struct userNode *to, const char *message) {
    putsock(":%s NOTICE %s :%s", from->nick, to->nick, message);
}

void
irc_wallchops(UNUSED_ARG(struct userNode *from), UNUSED_ARG(const char *to), UNUSED_ARG(const char *message)) {
}

void
irc_join(struct userNode *who, struct chanNode *what) {
    if (what->members.used == 1) {
        putsock(":%s SJOIN "FMT_TIME_T" %s + :@%s", self->name, what->timestamp, what->name, who->nick);
    } else {
        putsock(":%s SJOIN "FMT_TIME_T" %s", who->nick, what->timestamp, what->name);
    }
}

void
irc_invite(struct userNode *from, struct userNode *who, struct chanNode *to) {
    putsock(":%s INVITE %s %s", from->nick, who->nick, to->name);
}

void
irc_mode(struct userNode *who, struct chanNode *target, const char *modes) {
    putsock(":%s MODE %s "FMT_TIME_T" %s", who->nick, target->name, target->timestamp, modes);
}

void
irc_svsmode(struct userNode *target, char *modes, unsigned long stamp) {
    extern struct userNode *nickserv;
    if (stamp) {
	putsock(":%s SVSMODE %s "FMT_TIME_T" %s %lu", nickserv->nick, target->nick, target->timestamp, modes, stamp);
    } else {
	putsock(":%s SVSMODE %s "FMT_TIME_T" %s", nickserv->nick, target->nick, target->timestamp, modes);
    }
}

void
irc_kick(struct userNode *who, struct userNode *target, struct chanNode *from, const char *msg) {
    putsock(":%s KICK %s %s :%s", who->nick, from->name, target->nick, msg);
    ChannelUserKicked(who, target, from);
}

void
irc_part(struct userNode *who, struct chanNode *what, const char *reason) {
    if (reason) {
        putsock(":%s PART %s :%s", who->nick, what->name, reason);
    } else {
        putsock(":%s PART %s", who->nick, what->name);
    }
}

void
irc_topic(struct userNode *who, struct chanNode *what, const char *topic) {
    putsock(":%s TOPIC %s :%s", who->nick, what->name, topic);
}

void
irc_fetchtopic(struct userNode *from, const char *to) {
    if (!from || !to) return;
    putsock(":%s TOPIC %s", from->nick, to);
}

void
irc_gline(struct server *srv, struct gline *gline) {
    char host[HOSTLEN+1], ident[USERLEN+1], *sep;
    unsigned int len;
    if (srv) {
        log_module(MAIN_LOG, LOG_WARNING, "%s tried to send a targeted G-line for %s (not supported by protocol!)", gline->issuer, gline->target);
        return;
    }
    if (!(sep = strchr(gline->target, '@'))) {
        log_module(MAIN_LOG, LOG_ERROR, "%s tried to add G-line with bad mask %s", gline->issuer, gline->target);
        return;
    }
    len = sep - gline->target + 1;
    if (len > ArrayLength(ident)) len = ArrayLength(ident);
    safestrncpy(ident, gline->target, len);
    safestrncpy(host, sep+1, ArrayLength(host));
    putsock(":%s AKILL %s %s "FMT_TIME_T" %s "FMT_TIME_T" :%s", self->name, host, ident, gline->expires-gline->issued, gline->issuer, gline->issued, gline->reason);
}

void
irc_settime(UNUSED_ARG(const char *srv_name_mask), UNUSED_ARG(time_t new_time))
{
    /* Bahamut has nothing like this, so ignore it. */
}

void
irc_ungline(const char *mask) {
    char host[HOSTLEN+1], ident[USERLEN+1], *sep;
    unsigned int len;
    if (!(sep = strchr(mask, '@'))) {
        log_module(MAIN_LOG, LOG_ERROR, "Tried to remove G-line with bad mask %s", mask);
        return;
    }
    len = sep - mask + 1;
    if (len > ArrayLength(ident)) len = ArrayLength(ident);
    safestrncpy(ident, mask, len);
    safestrncpy(host, sep+1, ArrayLength(host));
    putsock(":%s RAKILL %s %s", self->name, host, ident);
}

void
irc_error(const char *to, const char *message) {
    if (to) {
        putsock("%s ERROR :%s", to, message);
    } else {
        putsock(":%s ERROR :%s", self->name, message);
    }
}

void
irc_kill(struct userNode *from, struct userNode *target, const char *message) {
    if (from) {
        putsock(":%s KILL %s :%s!%s (%s)", from->nick, target->nick, self->name, from->nick, message);
    } else {
        putsock(":%s KILL %s :%s (%s)", self->name, target->nick, self->name, message);
    }
}

void
irc_raw(const char *what) {
    putsock("%s", what);
}

void
irc_stats(struct userNode *from, struct server *target, char type) {
    putsock(":%s STATS %c :%s", from->nick, type, target->name);
}

void
irc_svsnick(struct userNode *from, struct userNode *target, const char *newnick)
{
    putsock(":%s SVSNICK %s %s :"FMT_TIME_T, from->nick, target->nick, newnick, now);
}

void
irc_numeric(struct userNode *user, unsigned int num, const char *format, ...) {
    va_list arg_list;
    char buffer[MAXLEN];
    va_start(arg_list, format);
    vsnprintf(buffer, MAXLEN-2, format, arg_list);
    buffer[MAXLEN-1] = 0;
    putsock(":%s %03d %s %s", self->name, num, user->nick, buffer);
}

static void
parse_foreach(char *target_list, foreach_chanfunc cf, foreach_nonchan nc, foreach_userfunc uf, foreach_nonuser nu, void *data) {
    char *j, old;
    do {
	j = target_list;
	while (*j != 0 && *j != ',') j++;
	old = *j;
        *j = 0;
	if (IsChannelName(target_list)) {
	    struct chanNode *chan = GetChannel(target_list);
            if (chan) {
                if (cf) cf(chan, data);
            } else {
                if (nc) nc(target_list, data);
            }
	} else {
	    struct userNode *user;
            struct privmsg_desc *pd = data;

            pd->is_qualified = 0;
            if (*target_list == '@') {
                user = NULL;
            } else if (strchr(target_list, '@')) {
		struct server *server;

                pd->is_qualified = 1;
		user = GetUserH(strtok(target_list, "@"));
		server = GetServerH(strtok(NULL, "@"));

		if (user && (user->uplink != server)) {
		    /* Don't attempt to index into any arrays
		       using a user's numeric on another server. */
		    user = NULL;
		}
	    } else {
		user = GetUserH(target_list);
	    }

            if (user) {
                if (uf) uf(user, data);
            } else {
                if (nu) nu(target_list, data);
            }
	}
	target_list = j+1;
    } while (old == ',');
}

static CMD_FUNC(cmd_notice) {
    struct privmsg_desc pd;
    if ((argc < 3) || !origin) return 0;
    if (!(pd.user = GetUserH(origin))) return 1;
    if (IsGagged(pd.user) && !IsOper(pd.user)) return 1;
    pd.is_notice = 1;
    pd.text = argv[2];
    parse_foreach(argv[1], privmsg_chan_helper, NULL, privmsg_user_helper, privmsg_invalid, &pd);
    return 1;
}

static CMD_FUNC(cmd_privmsg) {
    struct privmsg_desc pd;
    if ((argc < 3) || !origin) return 0;
    if (!(pd.user = GetUserH(origin))) return 1;
    if (IsGagged(pd.user) && !IsOper(pd.user)) return 1;
    pd.is_notice = 0;
    pd.text = argv[2];
    parse_foreach(argv[1], privmsg_chan_helper, NULL, privmsg_user_helper, privmsg_invalid, &pd);
    return 1;
}

static CMD_FUNC(cmd_whois) {
    struct userNode *from;
    struct userNode *who;

    if (argc < 3)
        return 0;
    if (!(from = GetUserH(origin))) {
        log_module(MAIN_LOG, LOG_ERROR, "Could not find WHOIS origin user %s", origin);
        return 0;
    }
    if(!(who = GetUserH(argv[2]))) {
        irc_numeric(from, ERR_NOSUCHNICK, "%s@%s :No such nick", argv[2], self->name);
        return 1;
    }
    if (IsHiddenHost(who) && !IsOper(from)) {
        /* Just stay quiet. */
        return 1;
    }
    irc_numeric(from, RPL_WHOISUSER, "%s %s %s * :%s", who->nick, who->ident, who->hostname, who->info);
#ifdef WITH_PROTOCOL_P10
    if (his_servername && his_servercomment)
      irc_numeric(from, RPL_WHOISSERVER, "%s %s :%s", who->nick, his_servername, his_servercomment);
    else
#endif
    irc_numeric(from, RPL_WHOISSERVER, "%s %s :%s", who->nick, who->uplink->name, who->uplink->description);

    if (IsOper(who)) {
        irc_numeric(from, RPL_WHOISOPERATOR, "%s :is a megalomaniacal power hungry tyrant", who->nick);
    }
    irc_numeric(from, RPL_ENDOFWHOIS, "%s :End of /WHOIS list", who->nick);
    return 1;
}

static CMD_FUNC(cmd_capab) {
    static const struct {
        const char *name;
        unsigned int mask;
    } capabs[] = {
        { "TS3", CAPAB_TS3 },
        { "NOQUIT", CAPAB_NOQUIT },
        { "SSJOIN", CAPAB_SSJOIN },
        { "BURST", CAPAB_BURST },
        { "UNCONNECT", CAPAB_UNCONNECT },
        { "NICKIP", CAPAB_NICKIP },
        { "TSMODE", CAPAB_TSMODE },
        { "ZIP", CAPAB_ZIP },
        { NULL, 0 }
    };
    unsigned int nn, mm;

    uplink_capab = 0;
    for(nn=1; nn<argc; nn++) {
        for (mm=0; capabs[mm].name && irccasecmp(capabs[mm].name, argv[nn]); mm++) ;
        if (capabs[mm].name) {
            uplink_capab |= capabs[mm].mask;
        } else {
            log_module(MAIN_LOG, LOG_INFO, "Saw unrecognized/unhandled capability %s.  Please notify srvx developers so they can add it.", argv[nn]);
        }
    }
    return 1;
}

static void burst_channel(struct chanNode *chan) {
    char line[510];
    int pos, base_len, len, queued;
    unsigned int nn;

    if (!chan->members.used) return;
    /* send list of users in the channel.. */
    base_len = sprintf(line, ":%s SJOIN "FMT_TIME_T" %s ", self->name, chan->timestamp, chan->name);
    len = irc_make_chanmode(chan, line+base_len);
    pos = base_len + len;
    line[pos++] = ' ';
    line[pos++] = ':';
    for (nn=0; nn<chan->members.used; nn++) {
        struct modeNode *mn = chan->members.list[nn];
        len = strlen(mn->user->nick);
        if (pos + len > 500) {
            line[pos-1] = 0;
            putsock("%s", line);
            pos = base_len;
            line[pos++] = '0';
            line[pos++] = ' ';
            line[pos++] = ':';
        }
        if (mn->modes & MODE_CHANOP) line[pos++] = '@';
        if (mn->modes & MODE_VOICE) line[pos++] = '+';
        memcpy(line+pos, mn->user->nick, len);
        pos = pos + len;
        line[pos++] = ' ';
    }
    /* print the last line */
    line[pos] = 0;
    putsock("%s", line);
    /* now send the bans.. */
    base_len = sprintf(line, ":%s MODE "FMT_TIME_T" %s +", self->name, chan->timestamp, chan->name);
    pos = sizeof(line)-1;
    line[pos] = 0;
    for (nn=queued=0; nn<chan->banlist.used; nn++) {
        struct banNode *bn = chan->banlist.list[nn];
        len = strlen(bn->ban);
        if (pos < base_len+queued+len+4) {
            while (queued) {
                line[--pos] = 'b';
                queued--;
            }
            putsock("%s%s", line, line+pos);
            pos = sizeof(line)-1;
        }
        pos -= len;
        memcpy(line+pos, bn->ban, len);
        line[--pos] = ' ';
        queued++;
    }
    if (queued) {
        while (queued) {
            line[--pos] = 'b';
            queued--;
        }
        putsock("%s%s", line, line+pos);
    }
}

static void send_burst() {
    dict_iterator_t it;
    for (it = dict_first(servers); it; it = iter_next(it)) {
        struct server *serv = iter_data(it);
        if ((serv != self) && (serv != self->uplink)) irc_server(serv);
    }
    putsock("BURST");
    for (it = dict_first(clients); it; it = iter_next(it)) {
        irc_user(iter_data(it));
    }
    for (it = dict_first(channels); it; it = iter_next(it)) {
        burst_channel(iter_data(it));
    }
    /* Uplink will do AWAY and TOPIC bursts before sending BURST 0, but we don't */
    putsock("BURST 0");
}

static CMD_FUNC(cmd_server) {
    if (argc < 4) return 0;
    if (origin) {
        AddServer(GetServerH(origin), argv[1], atoi(argv[2]), 0, now, 0, argv[3]);
    } else {
        self->uplink = AddServer(self, argv[1], atoi(argv[2]), 0, now, 0, argv[3]);
    }
    return 1;
}

static CMD_FUNC(cmd_svinfo) {
    if (argc < 5) return 0;
    if ((atoi(argv[1]) < 3) || (atoi(argv[2]) > 3)) return 0;
    /* TODO: something with the timestamp we get from the other guy */
    send_burst();
    return 1;
}

static CMD_FUNC(cmd_ping)
{
    irc_pong(self->name, argc > 1 ? argv[1] : origin);
    timeq_del(0, timed_send_ping, 0, TIMEQ_IGNORE_WHEN|TIMEQ_IGNORE_DATA);
    timeq_del(0, timed_ping_timeout, 0, TIMEQ_IGNORE_WHEN|TIMEQ_IGNORE_DATA);
    timeq_add(now + ping_freq, timed_send_ping, 0);
    received_ping();
    return 1;
}

static CMD_FUNC(cmd_burst) {
    struct server *sender = GetServerH(origin);
    if (!sender) return 0;
    if (argc == 1) return 1;
    if (sender == self->uplink) {
        cManager.uplink->state = CONNECTED;
    }
    sender->self_burst = 0;
    recalc_bursts(sender);
    return 1;
}

static CMD_FUNC(cmd_nick) {
    struct userNode *un;
    if ((un = GetUserH(origin))) {
        /* nick change */
        if (argc < 2) return 0;
        NickChange(un, argv[1], 1);
    } else {
        /* new nick from a server */
        char id[8];
        unsigned long stamp;
        struct in_addr ip;

        if (argc < 10) return 0;
        stamp = strtoul(argv[8], NULL, 0);
        if (stamp) inttobase64(id, stamp, IDLEN);
        ip.s_addr = (argc > 10) ? atoi(argv[9]) : 0;
        un = AddUser(GetServerH(argv[7]), argv[1], argv[5], argv[6], argv[4], argv[argc-1], atoi(argv[3]), ip, (stamp ? id : 0));
    }
    return 1;
}

static CMD_FUNC(cmd_sjoin) {
    struct chanNode *cNode;
    struct userNode *uNode;
    struct modeNode *mNode;
    unsigned int next = 4, last;
    char *nick, *nickend;

    if ((argc == 3) && (uNode = GetUserH(origin))) {
        /* normal JOIN */
        if (!(cNode = GetChannel(argv[2]))) {
            log_module(MAIN_LOG, LOG_ERROR, "Unable to find SJOIN target %s", argv[2]);
            return 0;
        }
        AddChannelUser(uNode, cNode);
        return 1;
    }
    if (argc < 5) return 0;
    if (argv[3][0] == '+') {
        char modes[MAXLEN], *pos;
        int n_modes;
        for (pos = argv[3], n_modes = 1; *pos; pos++) {
            if ((*pos == 'k') || (*pos == 'l')) n_modes++;
        }
        unsplit_string(argv+3, n_modes, modes);
        cNode = AddChannel(argv[2], atoi(argv[1]), modes, NULL);
    } else if (argv[3][0] == '0') {
        cNode = GetChannel(argv[2]);
    } else {
        log_module(MAIN_LOG, LOG_ERROR, "Unsure how to handle SJOIN when arg 3 is %s", argv[3]);
        return 0;
    }

    /* argv[next] is now the space-delimited, @+-prefixed list of
     * nicks in the channel.  Split it and add the users. */
    for (last = 0, nick = argv[next]; !last; nick = nickend + 1) {
        int mode = 0;
        for (nickend = nick; *nickend && (*nickend != ' '); nickend++) ;
        if (!*nickend) last = 1;
        *nickend = 0;
        if (*nick == '@') { mode |= MODE_CHANOP; nick++; }
        if (*nick == '+') { mode |= MODE_VOICE; nick++; }
        if ((uNode = GetUserH(nick)) && (mNode = AddChannelUser(uNode, cNode))) {
            mNode->modes = mode;
        }
    }
    return 1;
}

static CMD_FUNC(cmd_mode) {
    struct userNode *un;

    if (argc < 2) {
        log_module(MAIN_LOG, LOG_ERROR, "Illegal MODE from %s (no arguments).", origin);
        return 0;
    } else if (IsChannelName(argv[1])) {
        struct chanNode *cn;
        struct modeNode *mn;

        if (!(cn = GetChannel(argv[1]))) {
            log_module(MAIN_LOG, LOG_ERROR, "Unable to find channel %s whose mode is changing", argv[1]);
            return 0;
        }

        if ((un = GetUserH(origin))) {
            /* Update idle time for the user */
            if ((mn = GetUserMode(cn, un)))
                mn->idle_since = now;
        } else {
            /* Must be a server in burst or something.  Make sure we're using the right timestamp. */
            cn->timestamp = atoi(argv[2]);
        }

        return mod_chanmode(un, cn, argv+3, argc-3, MCP_ALLOW_OVB|MCP_FROM_SERVER|MC_ANNOUNCE);
    } else if ((un = GetUserH(argv[1]))) {
        mod_usermode(un, argv[2]);
        return 1;
    } else {
        log_module(MAIN_LOG, LOG_ERROR, "Not sure what MODE %s is affecting (not a channel name and no such user)", argv[1]);
        return 0;
    }
}

static CMD_FUNC(cmd_topic) {
    struct chanNode *cn;
    if (argc < 5) return 0;
    if (!(cn = GetChannel(argv[1]))) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to find channel %s whose topic is being set", argv[1]);
        return 0;
    }
    if (irccasecmp(origin, argv[2])) {
        /* coming from a topic burst; the origin is a server */
        safestrncpy(cn->topic, argv[4], sizeof(cn->topic));
        safestrncpy(cn->topic_nick, argv[2], sizeof(cn->topic_nick));
        cn->topic_time = atoi(argv[3]);
    } else {
        SetChannelTopic(cn, GetUserH(argv[2]), argv[4], 0);
    }
    return 1;
}

static CMD_FUNC(cmd_part) {
    if (argc < 2) return 0;
    parse_foreach(argv[1], part_helper, NULL, NULL, NULL, GetUserH(origin));
    return 1;
}

static CMD_FUNC(cmd_away) {
    struct userNode *un;

    if (!(un = GetUserH(origin))) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to find user %s sending AWAY", origin);
        return 0;
    }
    if (argc > 1) {
        un->modes |= FLAGS_AWAY;
    } else {
        un->modes &= ~FLAGS_AWAY;
    }
    return 1;
}

static CMD_FUNC(cmd_kick) {
    if (argc < 3) return 0;
    ChannelUserKicked(GetUserH(origin), GetUserH(argv[2]), GetChannel(argv[1]));
    return 1;
}

static CMD_FUNC(cmd_kill) {
    struct userNode *user;
    if (argc < 3) return 0;
    if (!(user = GetUserH(argv[1]))) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to find kill victim %s", argv[1]);
        return 0;
    }
    if (IsLocal(user) && IsService(user)) {
        ReintroduceUser(user);
    } else {
        DelUser(user, GetUserH(origin), false, ((argc >= 3) ? argv[2] : NULL));
    }
    return 1;
}

static CMD_FUNC(cmd_pong)
{
    if (argc < 3) return 0;
    if (!strcmp(argv[2], self->name)) {
	timeq_del(0, timed_send_ping, 0, TIMEQ_IGNORE_WHEN|TIMEQ_IGNORE_DATA);
	timeq_del(0, timed_ping_timeout, 0, TIMEQ_IGNORE_WHEN|TIMEQ_IGNORE_DATA);
	timeq_add(now + ping_freq, timed_send_ping, 0);
	received_ping();
    }
    return 1;
}

static CMD_FUNC(cmd_num_topic)
{
    static struct chanNode *cn;

    if (!argv[0])
        return 0; /* huh? */
    if (argv[2]) {
        cn = GetChannel(argv[2]);
        if (!cn) {
            log_module(MAIN_LOG, LOG_ERROR, "Unable to find channel %s in topic reply", argv[2]);
            return 0;
        }
    } else
        return 0;

    switch (atoi(argv[0])) {
    case 331:
        cn->topic_time = 0;
        break;  /* no topic */
    case 332:
        if (argc < 4)
            return 0;
        safestrncpy(cn->topic, unsplit_string(argv+3, argc-3, NULL), sizeof(cn->topic));
        break;
    case 333:
        if (argc < 5)
            return 0;
        safestrncpy(cn->topic_nick, argv[3], sizeof(cn->topic_nick));
        cn->topic_time = atoi(argv[4]);
        break;
    default:
        return 0; /* should never happen */
    }
    return 1;
}

static CMD_FUNC(cmd_quit)
{
    struct userNode *user;
    if (argc < 2) return 0;
    /* Sometimes we get a KILL then a QUIT or the like, so we don't want to
     * call DelUser unless we have the user in our grasp. */
    if ((user = GetUserH(origin))) {
        DelUser(user, NULL, false, argv[1]);
    }
    return 1;
}

static CMD_FUNC(cmd_squit)
{
    struct server *server;
    if (argc < 3)
        return 0;
    if (!(server = GetServerH(argv[1])))
        return 0;
    if (server == self->uplink) {
        /* Force a reconnect to the currently selected server. */
        cManager.uplink->tries = 0;
        log_module(MAIN_LOG, LOG_INFO, "Squitting from uplink: %s", argv[3]);
        close_socket();
        return 1;
    }

    DelServer(server, 0, argv[3]);
    return 1;
}

static CMD_FUNC(cmd_svsnick)
{
    struct userNode *target, *dest;
    if (argc < 4) return 0;
    if (!(target = GetUserH(argv[1]))) return 0;
    if (!IsLocal(target)) return 0;
    if ((dest = GetUserH(argv[2]))) return 0; /* Note: Bahamut will /KILL instead. */
    NickChange(target, argv[2], 0);
    return 1;
}

static oper_func_t *of_list;
static unsigned int of_size = 0, of_used = 0;

void parse_cleanup(void) {
    unsigned int nn;
    if (of_list) free(of_list);
    dict_delete(irc_func_dict);
    dict_delete(service_msginfo_dict);
    free(mcf_list);
    for (nn=0; nn<dead_users.used; nn++) free_user(dead_users.list[nn]);
    userList_clean(&dead_users);
}

void init_parse(void) {
    const char *str, *desc;

    str = conf_get_data("server/ping_freq", RECDB_QSTRING);
    ping_freq = str ? ParseInterval(str) : 120;
    str = conf_get_data("server/ping_timeout", RECDB_QSTRING);
    ping_timeout = str ? ParseInterval(str) : 30;
    str = conf_get_data("server/hostname", RECDB_QSTRING);
    desc = conf_get_data("server/description", RECDB_QSTRING);
    if (!str || !desc) {
        log_module(MAIN_LOG, LOG_ERROR, "No server/hostname entry in config file.");
        exit(1);
    }
    self = AddServer(NULL, str, 0, boot_time, now, NULL, desc);

    str = conf_get_data("server/ping_freq", RECDB_QSTRING);
    ping_freq = str ? ParseInterval(str) : 120;
    str = conf_get_data("server/ping_timeout", RECDB_QSTRING);
    ping_timeout = str ? ParseInterval(str) : 30;

    service_msginfo_dict = dict_new();
    dict_set_free_data(service_msginfo_dict, free);
    irc_func_dict = dict_new();
    dict_insert(irc_func_dict, "ADMIN", cmd_admin);
    dict_insert(irc_func_dict, "AWAY", cmd_away);
    dict_insert(irc_func_dict, "BURST", cmd_burst);
    dict_insert(irc_func_dict, "CAPAB", cmd_capab);
    dict_insert(irc_func_dict, "ERROR", cmd_error);
    dict_insert(irc_func_dict, "GNOTICE", cmd_dummy);
    dict_insert(irc_func_dict, "INVITE", cmd_dummy);
    dict_insert(irc_func_dict, "KICK", cmd_kick);
    dict_insert(irc_func_dict, "KILL", cmd_kill);
    dict_insert(irc_func_dict, "LUSERSLOCK", cmd_dummy);
    dict_insert(irc_func_dict, "MODE", cmd_mode);
    dict_insert(irc_func_dict, "NICK", cmd_nick);
    dict_insert(irc_func_dict, "NOTICE", cmd_notice);
    dict_insert(irc_func_dict, "PART", cmd_part);
    dict_insert(irc_func_dict, "PASS", cmd_pass);
    dict_insert(irc_func_dict, "PING", cmd_ping);
    dict_insert(irc_func_dict, "PONG", cmd_pong);
    dict_insert(irc_func_dict, "PRIVMSG", cmd_privmsg);
    dict_insert(irc_func_dict, "QUIT", cmd_quit);
    dict_insert(irc_func_dict, "SERVER", cmd_server);
    dict_insert(irc_func_dict, "SJOIN", cmd_sjoin);
    dict_insert(irc_func_dict, "SQUIT", cmd_squit);
    dict_insert(irc_func_dict, "STATS", cmd_stats);
    dict_insert(irc_func_dict, "SVSNICK", cmd_svsnick);
    dict_insert(irc_func_dict, "SVINFO", cmd_svinfo);
    dict_insert(irc_func_dict, "TOPIC", cmd_topic);
    dict_insert(irc_func_dict, "VERSION", cmd_version);
    dict_insert(irc_func_dict, "WHOIS", cmd_whois);
    dict_insert(irc_func_dict, "331", cmd_num_topic);
    dict_insert(irc_func_dict, "332", cmd_num_topic);
    dict_insert(irc_func_dict, "333", cmd_num_topic);
    dict_insert(irc_func_dict, "413", cmd_num_topic);

    userList_init(&dead_users);
    reg_exit_func(parse_cleanup);
}

int parse_line(char *line, int recursive) {
    char *argv[MAXNUMPARAMS];
    int argc, cmd, res;
    cmd_func_t *func;

    argc = split_line(line, true, ArrayLength(argv), argv);
    cmd = line[0] == ':';
    if ((argc > cmd) && (func = dict_find(irc_func_dict, argv[cmd], NULL))) {
        char *origin;
        if (cmd) {
            origin = argv[0] + 1;
        } else if (self->uplink) {
            origin = self->uplink->name;
        } else {
            origin = NULL;
        }
        res = func(origin, argc-cmd, argv+cmd);
    } else {
        res = 0;
    }
    if (!res) {
	log_module(MAIN_LOG, LOG_ERROR, "PARSE ERROR on line: %s", unsplit_string(argv, argc, NULL));
    } else if (!recursive) {
        unsigned int i;
        for (i=0; i<dead_users.used; i++) {
            free_user(dead_users.list[i]);
        }
        dead_users.used = 0;
    }
    return res;
}

static void
privmsg_user_helper(struct userNode *un, void *data)
{
    struct privmsg_desc *pd = data;
    struct service_message_info *info = dict_find(service_msginfo_dict, un->nick, 0);
    if (info) {
        if (pd->is_notice) {
            if (info->on_notice) info->on_notice(pd->user, un, pd->text, pd->is_qualified);
        } else {
            if (info->on_privmsg) info->on_privmsg(pd->user, un, pd->text, pd->is_qualified);
        }
    }
}

void
reg_privmsg_func(struct userNode *user, privmsg_func_t handler) {
    struct service_message_info *info = dict_find(service_msginfo_dict, user->nick, NULL);
    if (!info) {
        info = calloc(1, sizeof(*info));
        dict_insert(service_msginfo_dict, user->nick, info);
    }
    info->on_privmsg = handler;
}

void
reg_notice_func(struct userNode *user, privmsg_func_t handler) {
    struct service_message_info *info = dict_find(service_msginfo_dict, user->nick, NULL);
    if (!info) {
        info = calloc(1, sizeof(*info));
        dict_insert(service_msginfo_dict, user->nick, info);
    }
    info->on_notice = handler;
}

void
reg_oper_func(oper_func_t handler)
{
    if (of_used == of_size) {
	if (of_size) {
	    of_size <<= 1;
	    of_list = realloc(of_list, of_size*sizeof(oper_func_t));
	} else {
	    of_size = 8;
	    of_list = malloc(of_size*sizeof(oper_func_t));
	}
    }
    of_list[of_used++] = handler;
}

static void
call_oper_funcs(struct userNode *user)
{
    unsigned int n;
    if (IsLocal(user)) return;
    for (n=0; n<of_used; n++)
    {
	of_list[n](user);
    }
}

void mod_usermode(struct userNode *user, const char *mode_change) {
    int add = 1;

    if (!user || !mode_change) return;
    while (1) {
#define do_user_mode(FLAG) do { if (add) user->modes |= FLAG; else user->modes &= ~FLAG; } while (0)
	switch (*mode_change++) {
	case 0: return;
	case '+': add = 1; break;
	case '-': add = 0; break;
	case 'o':
	    do_user_mode(FLAGS_OPER);
	    if (add) {
		userList_append(&curr_opers, user);
		call_oper_funcs(user);
	    } else {
		userList_remove(&curr_opers, user);
	    }
	    break;
	case 'O': do_user_mode(FLAGS_LOCOP); break;
	case 'i': do_user_mode(FLAGS_INVISIBLE);
	    if (add) invis_clients++; else invis_clients--;
	    break;
	case 'w': do_user_mode(FLAGS_WALLOP); break;
	case 's': do_user_mode(FLAGS_SERVNOTICE); break;
	case 'd': do_user_mode(FLAGS_DEAF); break;
	case 'r': do_user_mode(FLAGS_REGNICK); break;
	case 'k': do_user_mode(FLAGS_SERVICE); break;
	case 'g': do_user_mode(FLAGS_GLOBAL); break;
	case 'h': do_user_mode(FLAGS_HELPER); break;
	}
#undef do_user_mode
    }
}

struct mod_chanmode *
mod_chanmode_parse(struct chanNode *channel, char **modes, unsigned int argc, unsigned int flags)
{
    struct mod_chanmode *change;
    unsigned int ii, in_arg, ch_arg, add;

    if (argc == 0)
        return NULL;
    if (!(change = mod_chanmode_alloc(argc)))
        return NULL;

    for (ii = ch_arg = 0, in_arg = add = 1;
         (modes[0][ii] != '\0') && (modes[0][ii] != ' ');
         ++ii) {
        switch (modes[0][ii]) {
        case '+':
            add = 1;
            break;
        case '-':
            add = 0;
            break;
#define do_chan_mode(FLAG) do { if (add) change->modes_set |= FLAG, change->modes_clear &= ~FLAG; else change->modes_clear |= FLAG, change->modes_set &= ~FLAG; } while(0)
        case 'R': do_chan_mode(MODE_REGONLY); break;
        case 'r': do_chan_mode(MODE_REGISTERED); break;
        case 'D': do_chan_mode(MODE_DELAYJOINS); break;
        case 'c': do_chan_mode(MODE_NOCOLORS); break;
        case 'i': do_chan_mode(MODE_INVITEONLY); break;
        case 'm': do_chan_mode(MODE_MODERATED); break;
        case 'n': do_chan_mode(MODE_NOPRIVMSGS); break;
        case 'p': do_chan_mode(MODE_PRIVATE); break;
        case 's': do_chan_mode(MODE_SECRET); break;
        case 't': do_chan_mode(MODE_TOPICLIMIT); break;
	case 'r':
	    if (!(flags & MCP_REGISTERED)) {
	     do_chan_mode(MODE_REGISTERED);
	    } else {
	     mod_chanmode_free(change);
	     return NULL;
	    }
	    break;
#undef do_chan_mode
        case 'l':
            if (add) {
                if (in_arg >= argc)
                    goto error;
                change->modes_set |= MODE_LIMIT;
                change->new_limit = atoi(modes[in_arg++]);
            } else {
                change->modes_clear |= MODE_LIMIT;
            }
            break;
        case 'k':
            if (add) {
                if (in_arg >= argc)
                    goto error;
                change->modes_set |= MODE_KEY;
                safestrncpy(change->new_key, modes[in_arg++], sizeof(change->new_key));
            } else {
                change->modes_clear |= MODE_KEY;
                if (!(flags & MCP_KEY_FREE)) {
                    if (in_arg >= argc)
                        goto error;
                    in_arg++;
                }
            }
            break;
        case 'b':
            if (!(flags & MCP_ALLOW_OVB))
                goto error;
            if (in_arg >= argc)
                goto error;
            change->args[ch_arg].mode = MODE_BAN;
            if (!add)
                change->args[ch_arg].mode |= MODE_REMOVE;
            change->args[ch_arg++].hostmask = modes[in_arg++];
            break;
        case 'o': case 'v':
        {
            struct userNode *victim;
            if (!(flags & MCP_ALLOW_OVB))
                goto error;
            if (in_arg >= argc)
                goto error;
            change->args[ch_arg].mode = (modes[0][ii] == 'o') ? MODE_CHANOP : MODE_VOICE;
            if (!add)
                change->args[ch_arg].mode |= MODE_REMOVE;
            victim = GetUserH(modes[in_arg++]);
            if (!victim)
                continue;
            if ((change->args[ch_arg].member = GetUserMode(channel, victim)))
                ch_arg++;
            break;
        }
        default:
            if (!(flags & MCP_FROM_SERVER))
                goto error;
            break;
        }
    }
    change->argc = ch_arg; /* in case any turned out to be ignored */
    if (change->modes_set & MODE_SECRET) {
        change->modes_set &= ~(MODE_PRIVATE);
        change->modes_clear |= MODE_PRIVATE;
    } else if (change->modes_set & MODE_PRIVATE) {
        change->modes_set &= ~(MODE_SECRET);
        change->modes_clear |= MODE_SECRET;
    }
    return change;
  error:
    mod_chanmode_free(change);
    return NULL;
}

struct chanmode_buffer {
    char modes[MAXLEN];
    char args[MAXLEN];
    struct chanNode *channel;
    struct userNode *actor;
    unsigned int modes_used;
    unsigned int args_used;
    size_t chname_len;
    unsigned int is_add : 1;
};

static void
mod_chanmode_append(struct chanmode_buffer *buf, char ch, const char *arg)
{
    size_t arg_len = strlen(arg);
    if (buf->modes_used + buf->args_used + buf->chname_len + arg_len > 450) {
        memcpy(buf->modes + buf->modes_used, buf->args, buf->args_used);
        buf->modes[buf->modes_used + buf->args_used] = '\0';
        irc_mode(buf->actor, buf->channel, buf->modes);
        buf->modes[0] = buf->is_add ? '+' : '-';
        buf->modes_used = 1;
        buf->args_used = 0;
    }
    buf->modes[buf->modes_used++] = ch;
    buf->args[buf->args_used++] = ' ';
    memcpy(buf->args + buf->args_used, arg, arg_len);
    buf->args_used += arg_len;
}

void
mod_chanmode_announce(struct userNode *who, struct chanNode *channel, struct mod_chanmode *change)
{
    struct chanmode_buffer chbuf;
    char int_buff[32];
    unsigned int arg;

    assert(change->argc <= change->alloc_argc);
    memset(&chbuf, 0, sizeof(chbuf));
    chbuf.channel = channel;
    chbuf.actor = who;
    chbuf.chname_len = strlen(channel->name);

    /* First remove modes */
    chbuf.is_add = 0;
    if (change->modes_clear) {
        chbuf.modes[chbuf.modes_used++] = '-';
#define DO_MODE_CHAR(BIT, CHAR) if (change->modes_clear & MODE_##BIT) chbuf.modes[chbuf.modes_used++] = CHAR;
        DO_MODE_CHAR(PRIVATE, 'p');
        DO_MODE_CHAR(SECRET, 's');
        DO_MODE_CHAR(MODERATED, 'm');
        DO_MODE_CHAR(TOPICLIMIT, 't');
        DO_MODE_CHAR(INVITEONLY, 'i');
        DO_MODE_CHAR(NOPRIVMSGS, 'n');
        DO_MODE_CHAR(LIMIT, 'l');
        DO_MODE_CHAR(DELAYJOINS, 'D');
        DO_MODE_CHAR(REGONLY, 'R');
        DO_MODE_CHAR(NOCOLORS, 'c');
        DO_MODE_CHAR(REGISTERED, 'r');
#undef DO_MODE_CHAR
        if (change->modes_clear & channel->modes & MODE_KEY)
            mod_chanmode_append(&chbuf, 'k', channel->key);
    }
    for (arg = 0; arg < change->argc; ++arg) {
        if (!(change->args[arg].mode & MODE_REMOVE))
            continue;
        switch (change->args[arg].mode & ~MODE_REMOVE) {
        case MODE_BAN:
            mod_chanmode_append(&chbuf, 'b', change->args[arg].hostmask);
            break;
        default:
            if (change->args[arg].mode & MODE_CHANOP)
                mod_chanmode_append(&chbuf, 'o', change->args[arg].member->user->nick);
            if (change->args[arg].mode & MODE_VOICE)
                mod_chanmode_append(&chbuf, 'v', change->args[arg].member->user->nick);
            break;
        }
    }

    /* Then set them */
    chbuf.is_add = 1;
    if (change->modes_set) {
        chbuf.modes[chbuf.modes_used++] = '+';
#define DO_MODE_CHAR(BIT, CHAR) if (change->modes_set & MODE_##BIT) chbuf.modes[chbuf.modes_used++] = CHAR;
        DO_MODE_CHAR(PRIVATE, 'p');
        DO_MODE_CHAR(SECRET, 's');
        DO_MODE_CHAR(MODERATED, 'm');
        DO_MODE_CHAR(TOPICLIMIT, 't');
        DO_MODE_CHAR(INVITEONLY, 'i');
        DO_MODE_CHAR(NOPRIVMSGS, 'n');
        DO_MODE_CHAR(DELAYJOINS, 'D');
        DO_MODE_CHAR(REGONLY, 'R');
        DO_MODE_CHAR(NOCOLORS, 'c');
        DO_MODE_CHAR(REGISTERED, 'r');
#undef DO_MODE_CHAR
        if (change->modes_set & MODE_KEY)
            mod_chanmode_append(&chbuf, 'k', change->new_key);
        if (change->modes_set & MODE_LIMIT)
        {
            sprintf(int_buff, "%d", change->new_limit);
            mod_chanmode_append(&chbuf, 'l', int_buff);
        }
    }
    for (arg = 0; arg < change->argc; ++arg) {
        if (change->args[arg].mode & MODE_REMOVE)
            continue;
        switch (change->args[arg].mode) {
        case MODE_BAN:
            mod_chanmode_append(&chbuf, 'b', change->args[arg].hostmask);
            break;
        default:
            if (change->args[arg].mode & MODE_CHANOP)
                mod_chanmode_append(&chbuf, 'o', change->args[arg].member->user->nick);
            if (change->args[arg].mode & MODE_VOICE)
                mod_chanmode_append(&chbuf, 'v', change->args[arg].member->user->nick);
            break;
        }
    }

    /* Flush the buffer and apply changes locally */
    if (chbuf.modes_used > 0) {
        memcpy(chbuf.modes + chbuf.modes_used, chbuf.args, chbuf.args_used);
        chbuf.modes[chbuf.modes_used + chbuf.args_used] = '\0';
        irc_mode(chbuf.actor, chbuf.channel, chbuf.modes);
    }
    mod_chanmode_apply(who, channel, change);
}

char *
mod_chanmode_format(struct mod_chanmode *change, char *outbuff)
{
    unsigned int used = 0;
    assert(change->argc <= change->alloc_argc);
    if (change->modes_clear) {
        outbuff[used++] = '-';
#define DO_MODE_CHAR(BIT, CHAR) if (change->modes_clear & MODE_##BIT) outbuff[used++] = CHAR
        DO_MODE_CHAR(PRIVATE, 'p');
        DO_MODE_CHAR(SECRET, 's');
        DO_MODE_CHAR(MODERATED, 'm');
        DO_MODE_CHAR(TOPICLIMIT, 't');
        DO_MODE_CHAR(INVITEONLY, 'i');
        DO_MODE_CHAR(NOPRIVMSGS, 'n');
        DO_MODE_CHAR(LIMIT, 'l');
        DO_MODE_CHAR(KEY, 'k');
        DO_MODE_CHAR(DELAYJOINS, 'D');
        DO_MODE_CHAR(REGONLY, 'R');
        DO_MODE_CHAR(NOCOLORS, 'c');
        DO_MODE_CHAR(REGISTERED, 'r');
#undef DO_MODE_CHAR
    }
    if (change->modes_set) {
        outbuff[used++] = '+';
#define DO_MODE_CHAR(BIT, CHAR) if (change->modes_set & MODE_##BIT) outbuff[used++] = CHAR
        DO_MODE_CHAR(PRIVATE, 'p');
        DO_MODE_CHAR(SECRET, 's');
        DO_MODE_CHAR(MODERATED, 'm');
        DO_MODE_CHAR(TOPICLIMIT, 't');
        DO_MODE_CHAR(INVITEONLY, 'i');
        DO_MODE_CHAR(NOPRIVMSGS, 'n');
        DO_MODE_CHAR(DELAYJOINS, 'D');
        DO_MODE_CHAR(REGONLY, 'R');
        DO_MODE_CHAR(NOCOLORS, 'c');
        DO_MODE_CHAR(REGISTERED, 'r');
#undef DO_MODE_CHAR
        switch (change->modes_set & (MODE_KEY|MODE_LIMIT)) {
        case MODE_KEY|MODE_LIMIT:
            used += sprintf(outbuff+used, "lk %d %s", change->new_limit, change->new_key);
            break;
        case MODE_KEY:
            used += sprintf(outbuff+used, "k %s", change->new_key);
            break;
        case MODE_LIMIT:
            used += sprintf(outbuff+used, "l %d", change->new_limit);
            break;
        }
    }
    outbuff[used] = 0;
    return outbuff;
}

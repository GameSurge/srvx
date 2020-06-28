/* proto-common.c - common IRC protocol parsing/sending support
 * Copyright 2000-2006 srvx Development Team
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
#include "ioset.h"
#include "log.h"
#include "nickserv.h"
#include "timeq.h"
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

unsigned int lines_processed;
FILE *replay_file;
struct io_fd *socket_io_fd;
int force_n2k;
const char *hidden_host_suffix;

static char replay_line[MAXLEN+80];
static int ping_freq;
static int ping_timeout;
static int replay_connected;
static unsigned int nicklen = NICKLEN; /* how long do we think servers allow nicks to be? */
static struct userList dead_users;

extern struct cManagerNode cManager;
extern unsigned long burst_length;
extern struct cManagerNode cManager;
extern struct policer_params *oper_policer_params, *luser_policer_params;
extern server_link_func_t *slf_list;
extern unsigned int slf_size, slf_used;
extern new_user_func_t *nuf_list;
extern unsigned int nuf_size, nuf_used;
extern del_user_func_t *duf_list;
extern unsigned int duf_size, duf_used;
extern unsigned long boot_time;

void received_ping(void);

static int replay_read(void);
static dict_t irc_func_dict;

typedef void (*foreach_chanfunc) (struct chanNode *chan, void *data);
typedef void (*foreach_nonchan) (char *name, void *data);
typedef void (*foreach_userfunc) (struct userNode *user, void *data);
typedef void (*foreach_nonuser) (char *name, void *data);
static void parse_foreach(char *target_list, foreach_chanfunc cf, foreach_nonchan nc, foreach_userfunc uf, foreach_nonuser nu, void *data);

static void
uplink_readable(struct io_fd *fd) {
    static char buffer[MAXLEN];
    char *eol;
    int pos;

    pos = ioset_line_read(fd, buffer, sizeof(buffer));
    if (pos <= 0) {
        close_socket();
        return;
    }
    if ((eol = strpbrk(buffer, "\r\n")))
        *eol = 0;
    log_replay(MAIN_LOG, false, buffer);
    if (cManager.uplink->state != DISCONNECTED)
        parse_line(buffer, 0);
    lines_processed++;
}

void
socket_destroyed(struct io_fd *fd)
{
    if (fd && fd->state != IO_CONNECTED)
        log_module(MAIN_LOG, LOG_ERROR, "Connection to server lost.");
    socket_io_fd = NULL;
    cManager.uplink->state = DISCONNECTED;
    if (self->uplink)
        DelServer(self->uplink, 0, NULL);
}

void replay_event_loop(void)
{
    while (!quit_services) {
        if (!replay_connected) {
            /* this time fudging is to get some of the logging right */
            self->link_time = self->boot = now;
            cManager.uplink->state = AUTHENTICATING;
            irc_introduce(cManager.uplink->password);
            replay_connected = 1;
        } else if (!replay_read()) {
            log_module(MAIN_LOG, LOG_ERROR, "Connection to server lost.");
            close_socket();
        }
        timeq_run();
    }
}

int
create_socket_client(struct uplinkNode *target)
{
    int port = target->port;
    const char *addr = target->host;

    if (replay_file)
        return feof(replay_file) ? 0 : 1;

    if (socket_io_fd) {
        /* Leave the existing socket open, say we failed. */
        log_module(MAIN_LOG, LOG_ERROR, "Refusing to create second connection to %s:%d.", addr, port);
        return 0;
    }

    log_module(MAIN_LOG, LOG_INFO, "Connecting to %s:%i...", addr, port);

    socket_io_fd = ioset_connect(cManager.uplink->bind_addr, cManager.uplink->bind_addr_len, addr, port, 1, 0, NULL);
    if (!socket_io_fd) {
        log_module(MAIN_LOG, LOG_ERROR, "Connection to uplink failed: %s (%d)", strerror(errno), errno);
        target->state = DISCONNECTED;
        target->tries++;
        return 0;
    }
    socket_io_fd->readable_cb = uplink_readable;
    socket_io_fd->destroy_cb = socket_destroyed;
    socket_io_fd->line_reads = 1;
    log_module(MAIN_LOG, LOG_INFO, "Connection to server established.");
    cManager.uplink = target;
    target->state = AUTHENTICATING;
    target->tries = 0;
    return 1;
}

void
replay_read_line(void)
{
    struct tm timestamp;
    unsigned long new_time;

    if (replay_line[0]) return;
  read_line:
    if (!fgets(replay_line, sizeof(replay_line), replay_file)) {
        if (feof(replay_file)) {
            quit_services = 1;
            memset(replay_line, 0, sizeof(replay_line));
            return;
        }
    }
    if ((replay_line[0] != '[')
        || (replay_line[3] != ':')
        || (replay_line[6] != ':')
        || (replay_line[9] != ' ')
        || (replay_line[12] != '/')
        || (replay_line[15] != '/')
        || (replay_line[20] != ']')
        || (replay_line[21] != ' ')) {
        log_module(MAIN_LOG, LOG_ERROR, "Unrecognized timestamp in replay file: %s", replay_line);
        goto read_line;
    }
    timestamp.tm_hour = strtoul(replay_line+1, NULL, 10);
    timestamp.tm_min = strtoul(replay_line+4, NULL, 10);
    timestamp.tm_sec = strtoul(replay_line+7, NULL, 10);
    timestamp.tm_mon = strtoul(replay_line+10, NULL, 10) - 1;
    timestamp.tm_mday = strtoul(replay_line+13, NULL, 10);
    timestamp.tm_year = strtoul(replay_line+16, NULL, 10) - 1900;
    timestamp.tm_isdst = 0;
    new_time = mktime(&timestamp);
    if (new_time == (unsigned long)-1) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to parse time struct tm_sec=%d tm_min=%d tm_hour=%d tm_mday=%d tm_mon=%d tm_year=%d", timestamp.tm_sec, timestamp.tm_min, timestamp.tm_hour, timestamp.tm_mday, timestamp.tm_mon, timestamp.tm_year);
    } else {
        now = new_time;
    }

    if (strncmp(replay_line+22, "(info) ", 7))
        goto read_line;
    return;
}

static int
replay_read(void)
{
    size_t len;
    char read_line[MAXLEN];
    while (1) {
        replay_read_line();
        /* if it's a sent line, break out to handle it */
        if (!strncmp(replay_line+29, "   ", 3))
            break;
        if (!strncmp(replay_line+29, "W: ", 3)) {
            log_module(MAIN_LOG, LOG_ERROR, "Expected response from services: %s", replay_line+32);
            replay_line[0] = 0;
        } else {
            return 0;
        }
    }
    log_replay(MAIN_LOG, false, replay_line+32);
    safestrncpy(read_line, replay_line+32, sizeof(read_line));
    len = strlen(read_line);
    if (read_line[len-1] == '\n')
        read_line[--len] = 0;
    replay_line[0] = 0;
    parse_line(read_line, 0);
    lines_processed++;
    return 1;
}

static void
replay_write(char *text)
{
    replay_read_line();
    if (strncmp(replay_line+29, "W: ", 3)) {
        log_module(MAIN_LOG, LOG_ERROR, "Unexpected output during replay: %s", text);
        return;
    } else {
        if (strcmp(replay_line+32, text)) {
            log_module(MAIN_LOG, LOG_ERROR, "Incorrect output during replay:\nReceived: %sExpected: %s", text, replay_line+32);
        } else {
            log_replay(MAIN_LOG, true, text);
        }
        replay_line[0] = 0;
    }
}

void putsock(const char *text, ...) PRINTF_LIKE(1, 2);

void
putsock(const char *text, ...)
{
    va_list arg_list;
    char buffer[MAXLEN];
    int pos;

    if (!cManager.uplink || cManager.uplink->state == DISCONNECTED) return;
    buffer[0] = '\0';
    va_start(arg_list, text);
    pos = vsnprintf(buffer, MAXLEN - 2, text, arg_list);
    va_end(arg_list);
    if (pos < 0 || pos > (MAXLEN - 2)) pos = MAXLEN - 2;
    buffer[pos] = 0;
    if (!replay_file) {
        log_replay(MAIN_LOG, true, buffer);
        buffer[pos++] = '\n';
        buffer[pos] = 0;
        ioset_write(socket_io_fd, buffer, pos);
    } else {
        replay_write(buffer);
    }
}

void
close_socket(void)
{
    if (replay_file) {
        replay_connected = 0;
        socket_destroyed(socket_io_fd);
    } else {
        ioset_close(socket_io_fd, 3);
        socket_io_fd = NULL;
    }
}

#define CMD_FUNC(NAME) int NAME(UNUSED_ARG(const char *origin), UNUSED_ARG(unsigned int argc), UNUSED_ARG(char **argv))
typedef CMD_FUNC(cmd_func_t);

static void timed_ping_timeout(void *data);

/* Ping state is kept in the timeq (only one of these two can be in
 * the queue at any given time). */
void
timed_send_ping(UNUSED_ARG(void *data))
{
    irc_ping(self->name);
    timeq_add(now + ping_timeout, timed_ping_timeout, 0);
}

static void
timed_ping_timeout(UNUSED_ARG(void *data))
{
    /* Uplink "health" tracking could be accomplished by counting the
       number of ping timeouts that happen for each uplink. After the
       timeouts per time period exceeds some amount, the uplink could
       be marked as unavalable.*/
    irc_squit(self, "Ping timeout.", NULL);
}

static CMD_FUNC(cmd_pass)
{
    const char *true_pass;

    if (argc < 2)
        return 0;
    true_pass = cManager.uplink->their_password;
    if (true_pass && strcmp(true_pass, argv[1])) {
        /* It might be good to mark the uplink as unavailable when
           this happens, though there should be a way of resetting
           the flag. */
        irc_squit(self, "Incorrect password received.", NULL);
        return 1;
    }

    cManager.uplink->state = BURSTING;
    return 1;
}

static CMD_FUNC(cmd_dummy)
{
    /* we don't care about these messages */
    return 1;
}

static CMD_FUNC(cmd_error)
{
    if (argv[1]) log_module(MAIN_LOG, LOG_ERROR, "Error from ircd: %s", argv[1]);
    log_module(MAIN_LOG, LOG_ERROR, "Error received from uplink, squitting.");

    if (cManager.uplink->state != CONNECTED) {
        /* Error messages while connected should be fine. */
        cManager.uplink->flags |= UPLINK_UNAVAILABLE;
        log_module(MAIN_LOG, LOG_ERROR, "Disabling uplink.");
    }

    close_socket();
    return 1;
}

static CMD_FUNC(cmd_stats)
{
    struct userNode *un;

    if (argc < 2)
        return 0;
    if (!(un = GetUserH(origin)))
        return 0;
    switch (argv[1][0]) {
    case 'u': {
        unsigned long uptime;
        uptime = now - boot_time;
        irc_numeric(un, RPL_STATSUPTIME, ":Server Up %lu days %lu:%02lu:%02lu",
                    uptime/(24*60*60), (uptime/(60*60))%24, (uptime/60)%60, uptime%60);
        irc_numeric(un, RPL_MAXCONNECTIONS, ":Highest connection count: %u (%u clients)",
                    self->max_clients+1, self->max_clients);
        break;
    }
    default: /* unrecognized/unhandled stats output */ break;
    }
    irc_numeric(un, 219, "%s :End of /STATS report", argv[1]);
    return 1;
}

static CMD_FUNC(cmd_version)
{
    struct userNode *user;
    if (!(user = GetUserH(origin))) {
        log_module(MAIN_LOG, LOG_ERROR, "Could not find VERSION origin user %s", origin);
        return 0;
    }
    irc_numeric(user, 351, "%s.%s %s :%s", PACKAGE_TARNAME, PACKAGE_VERSION, self->name, CODENAME);
    return 1;
}

static CMD_FUNC(cmd_admin)
{
    struct userNode *user;
    struct string_list *slist;

    if (!(user = GetUserH(origin))) {
        log_module(MAIN_LOG, LOG_ERROR, "Could not find ADMIN origin user %s", origin);
        return 0;
    }
    if ((slist = conf_get_data("server/admin", RECDB_STRING_LIST)) && slist->used) {
        unsigned int i;

        irc_numeric(user, 256, ":Administrative info about %s", self->name);
        for (i = 0; i < slist->used && i < 3; i++)
            irc_numeric(user, 257 + i, ":%s", slist->list[i]);
    } else {
        irc_numeric(user, 423, ":No administrative info available");
    }
    return 1;
}

static void
recalc_bursts(struct server *eob_server)
{
    unsigned int nn;
    eob_server->burst = eob_server->self_burst;
    if (eob_server->uplink != self)
        eob_server->burst = eob_server->burst || eob_server->uplink->burst;
    for (nn=0; nn < eob_server->children.used; nn++)
        recalc_bursts(eob_server->children.list[nn]);
}

static struct chanmsg_func {
    chanmsg_func_t func;
    struct userNode *service;
} chanmsg_funcs[256]; /* indexed by trigger character */

static struct allchanmsg_func {
    chanmsg_func_t func;
    struct userNode *service;
} allchanmsg_funcs[ALLCHANMSG_FUNCS_MAX];

struct privmsg_desc {
    struct userNode *user;
    char *text;
    unsigned int is_notice : 1;
    unsigned int is_qualified : 1;
};

static void
privmsg_chan_helper(struct chanNode *cn, void *data)
{
    extern unsigned short offchannel_allowed[256];
    struct privmsg_desc *pd = data;
    struct modeNode *mn;
    struct chanmsg_func *cf;
    int x;

    /* Don't complain if it can't find the modeNode because the channel might
     * be -n */
    if ((mn = GetUserMode(cn, pd->user)))
        mn->idle_since = now;

    /* Never send a NOTICE to a channel to one of the services */
    cf = &chanmsg_funcs[(unsigned char)pd->text[0]];
    if (cf->func && !pd->is_notice
        && (offchannel_allowed[(unsigned char)pd->text[0]]
            || (GetUserMode(cn, cf->service) && !IsDeaf(cf->service))))
        cf->func(pd->user, cn, pd->text+1, cf->service, pd->is_notice);

    /* This catches *all* text sent to the channel that the services server sees */
    for (x = 0; x < ALLCHANMSG_FUNCS_MAX; x++) {
       cf = (struct chanmsg_func *)&allchanmsg_funcs[x];
       if (!cf->func)
           break; /* end of list */
       else
           cf->func(pd->user, cn, pd->text, cf->service, pd->is_notice);
    }
}

static void
privmsg_invalid(char *name, void *data)
{
    struct privmsg_desc *pd = data;

    if (*name == '$')
        return;
    irc_numeric(pd->user, ERR_NOSUCHNICK, "%s@%s :No such nick", name, self->name);
}

struct part_desc {
    struct userNode *user;
    const char *text;
};

static void
part_helper(struct chanNode *cn, void *data)
{
    struct part_desc *desc = data;
    DelChannelUser(desc->user, cn, desc->text, false);
}

static CMD_FUNC(cmd_part)
{
    struct part_desc desc;

    if (argc < 2)
        return 0;
    desc.user = GetUserH(origin);
    if (!desc.user)
        return 0;
    desc.text = (argc > 2) ? argv[argc - 1] : NULL;
    parse_foreach(argv[1], part_helper, NULL, NULL, NULL, &desc);
    return 1;
}

void
reg_chanmsg_func(unsigned char prefix, struct userNode *service, chanmsg_func_t handler)
{
    if (chanmsg_funcs[prefix].func)
        log_module(MAIN_LOG, LOG_WARNING, "Re-registering new chanmsg handler for character `%c'.", prefix);
    chanmsg_funcs[prefix].func = handler;
    chanmsg_funcs[prefix].service = service;
}

void
reg_allchanmsg_func(struct userNode *service, chanmsg_func_t handler)
{
    int x;
    for (x = 0; x < ALLCHANMSG_FUNCS_MAX; x++) {
       if (allchanmsg_funcs[x].func)
         continue;
       allchanmsg_funcs[x].func = handler;
       allchanmsg_funcs[x].service = service;
       break;
    }
}

struct userNode *
get_chanmsg_bot(unsigned char prefix)
{
    return chanmsg_funcs[prefix].service;
}

static mode_change_func_t *mcf_list;
static unsigned int mcf_size = 0, mcf_used = 0;

void
reg_mode_change_func(mode_change_func_t handler)
{
    if (mcf_used == mcf_size) {
        if (mcf_size) {
            mcf_size <<= 1;
            mcf_list = realloc(mcf_list, mcf_size*sizeof(mode_change_func_t));
        } else {
            mcf_size = 8;
            mcf_list = malloc(mcf_size*sizeof(mode_change_func_t));
        }
    }
    mcf_list[mcf_used++] = handler;
}

static oper_func_t *of_list;
static unsigned int of_size = 0, of_used = 0;

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
    if (IsLocal(user))
        return;
    for (n=0; (n<of_used) && !user->dead; n++)
    {
        of_list[n](user);
    }
}

static xquery_func_t *xqf_list;
static unsigned int xqf_size = 0, xqf_used = 0;

void
reg_xquery_func(xquery_func_t handler)
{
    if (xqf_used == xqf_size) {
        if (xqf_size) {
            xqf_size <<= 1;
            xqf_list = realloc(xqf_list, xqf_size*sizeof(xquery_func_t));
        } else {
            xqf_size = 8;
            xqf_list = malloc(xqf_size*sizeof(xquery_func_t));
        }
    }
    xqf_list[xqf_used++] = handler;
}

static void
call_xquery_funcs(struct server *source, const char routing[], const char query[])
{
    unsigned int n;
    for (n=0; n < xqf_used; n++)
    {
        xqf_list[n](source, routing, query);
    }
}

struct mod_chanmode *
mod_chanmode_alloc(unsigned int argc)
{
    struct mod_chanmode *res;
    if (argc > 1)
        res = calloc(1, sizeof(*res) + (argc-1)*sizeof(res->args[0]));
    else
        res = calloc(1, sizeof(*res));
    if (res) {
#if !defined(NDEBUG)
        res->alloc_argc = argc;
#endif
        res->argc = argc;
    }
    return res;
}

struct mod_chanmode *
mod_chanmode_dup(struct mod_chanmode *orig, unsigned int extra)
{
    struct mod_chanmode *res;
    res = mod_chanmode_alloc(orig->argc + extra);
    if (res) {
        res->modes_set = orig->modes_set;
        res->modes_clear = orig->modes_clear;
        res->new_limit = orig->new_limit;
        memcpy(res->new_key, orig->new_key, sizeof(res->new_key));
        memcpy(res->new_upass, orig->new_upass, sizeof(res->new_upass));
        memcpy(res->new_apass, orig->new_apass, sizeof(res->new_apass));
        res->argc = orig->argc;
        memcpy(res->args, orig->args, orig->argc*sizeof(orig->args[0]));
    }
    return res;
}

void
mod_chanmode_apply(struct userNode *who, struct chanNode *channel, struct mod_chanmode *change)
{
    struct banNode *bn;
    unsigned int ii, jj;

    assert(change->argc <= change->alloc_argc);
    channel->modes = (channel->modes & ~change->modes_clear) | change->modes_set;
    if (change->modes_set & MODE_LIMIT)
        channel->limit = change->new_limit;
    if (change->modes_set & MODE_KEY)
        strcpy(channel->key, change->new_key);
    if (change->modes_set & MODE_UPASS)
       strcpy(channel->upass, change->new_upass);
    if (change->modes_set & MODE_APASS)
       strcpy(channel->apass, change->new_apass);
    for (ii = 0; ii < change->argc; ++ii) {
        switch (change->args[ii].mode) {
        case MODE_BAN:
            /* If any existing ban is a subset of the new ban,
             * silently remove it.  The new ban is not allowed
             * to be more specific than an existing ban.
             */
            for (jj=0; jj<channel->banlist.used; ++jj) {
                bn = channel->banlist.list[jj];
                if (match_ircglobs(change->args[ii].u.hostmask, bn->ban)) {
                    banList_remove(&channel->banlist, bn);
                    free(bn);
                    jj--;
                }
            }
            bn = calloc(1, sizeof(*bn));
            safestrncpy(bn->ban, change->args[ii].u.hostmask, sizeof(bn->ban));
            if (who)
                safestrncpy(bn->who, who->nick, sizeof(bn->who));
            else
                safestrncpy(bn->who, "<unknown>", sizeof(bn->who));
            bn->set = now;
            banList_append(&channel->banlist, bn);
            break;
        case MODE_REMOVE|MODE_BAN:
            for (jj=0; jj<channel->banlist.used; ++jj) {
                bn = channel->banlist.list[jj];
                if (strcmp(bn->ban, change->args[ii].u.hostmask))
                    continue;
                banList_remove(&channel->banlist, bn);
                free(bn);
                break;
            }
            break;
        case MODE_CHANOP:
        case MODE_VOICE:
        case MODE_VOICE|MODE_CHANOP:
        case MODE_REMOVE|MODE_CHANOP:
        case MODE_REMOVE|MODE_VOICE:
        case MODE_REMOVE|MODE_VOICE|MODE_CHANOP:
            if (change->args[ii].mode & MODE_REMOVE)
                change->args[ii].u.member->modes &= ~change->args[ii].mode;
            else
                change->args[ii].u.member->modes |= change->args[ii].mode;
            break;
        default:
            assert(0 && "Invalid mode argument");
            continue;
        }
    }
}

void
mod_chanmode_free(struct mod_chanmode *change)
{
    free(change);
}

int
mod_chanmode(struct userNode *who, struct chanNode *channel, char **modes, unsigned int argc, unsigned int flags)
{
    struct modeNode *member;
    struct mod_chanmode *change;
    unsigned int ii;
    short base_oplevel;

    if (!modes || !modes[0])
        return 0;
    if (who && (member = GetUserMode(channel, who)))
        base_oplevel = member->oplevel;
    else
        base_oplevel = MAXOPLEVEL;
    if (!(change = mod_chanmode_parse(channel, modes, argc, flags, base_oplevel)))
        return 0;
    if (flags & MC_ANNOUNCE)
        mod_chanmode_announce(who, channel, change);
    else
        mod_chanmode_apply(who, channel, change);
    if (flags & MC_NOTIFY)
        for (ii = 0; ii < mcf_used; ++ii)
            mcf_list[ii](channel, who, change);
    mod_chanmode_free(change);
    return 1;
}

int
irc_make_chanmode(struct chanNode *chan, char *out)
{
    struct mod_chanmode change;
    mod_chanmode_init(&change);
    change.modes_set = chan->modes;
    change.new_limit = chan->limit;
    safestrncpy(change.new_key, chan->key, sizeof(change.new_key));
    safestrncpy(change.new_upass, chan->upass, sizeof(change.new_upass));
    safestrncpy(change.new_apass, chan->apass, sizeof(change.new_apass));
    return strlen(mod_chanmode_format(&change, out));
}

char *
generate_hostmask(struct userNode *user, int options)
{
    irc_in_addr_t ip;
    char *nickname, *ident, *hostname, *mask;
    int len, ii;

    /* figure out string parts */
    if (options & GENMASK_OMITNICK)
        nickname = NULL;
    else if (options & GENMASK_USENICK)
        nickname = user->nick;
    else
        nickname = "*";
    if (options & GENMASK_STRICT_IDENT)
        ident = user->ident;
    else if (options & GENMASK_ANY_IDENT)
        ident = "*";
    else if (IsFakeIdent(user) && IsHiddenHost(user) && !(options & GENMASK_NO_HIDING))
        ident = user->fakeident;
    else {
        ident = alloca(strlen(user->ident)+2);
        ident[0] = '*';
        strcpy(ident+1, user->ident + ((*user->ident == '~')?1:0));
    }
    hostname = user->hostname;
    if (IsFakeHost(user) && IsHiddenHost(user) && !(options & GENMASK_NO_HIDING)) {
        hostname = user->fakehost;
    } else if (IsHiddenHost(user) && user->handle_info && hidden_host_suffix && !(options & GENMASK_NO_HIDING)) {
        hostname = alloca(strlen(user->handle_info->handle) + strlen(hidden_host_suffix) + 2);
        sprintf(hostname, "%s.%s", user->handle_info->handle, hidden_host_suffix);
    } else if (options & GENMASK_STRICT_HOST) {
        if (options & GENMASK_BYIP)
            hostname = (char*)irc_ntoa(&user->ip);
    } else if ((options & GENMASK_BYIP) || irc_pton(&ip, NULL, hostname)) {
        /* Should generate an IP-based hostmask. */
        hostname = alloca(IRC_NTOP_MAX_SIZE);
        hostname[IRC_NTOP_MAX_SIZE-1] = '\0';
        if (irc_in_addr_is_ipv4(user->ip)) {
            /* By popular acclaim, a /16 hostmask is used. */
            sprintf(hostname, "%d.%d.*", user->ip.in6_8[12], user->ip.in6_8[13]);
        } else if (irc_in_addr_is_ipv6(user->ip)) {
            /* Who knows what the default mask should be?  Use a /48 to start with. */
            sprintf(hostname, "%x:%x:%x:*", ntohs(user->ip.in6[0]), ntohs(user->ip.in6[1]), ntohs(user->ip.in6[2]));
        } else {
            /* Unknown type; just copy IP directly. */
            irc_ntop(hostname, IRC_NTOP_MAX_SIZE, &user->ip);
        }
    } else {
        int cnt;
        /* This heuristic could be made smarter.  Is it worth the effort? */
        for (ii=cnt=0; hostname[ii]; ii++)
            if (hostname[ii] == '.')
                cnt++;
        if (cnt == 0 || cnt == 1) {
            /* only a one- or two-level domain name; leave hostname */
        } else if (cnt == 2) {
            for (ii=0; user->hostname[ii] != '.'; ii++) ;
            /* Add 3 to account for the *. and \0. */
            hostname = alloca(strlen(user->hostname+ii)+3);
            sprintf(hostname, "*.%s", user->hostname+ii+1);
        } else {
            for (cnt=3, ii--; cnt; ii--)
                if (user->hostname[ii] == '.')
                    cnt--;
            /* The loop above will overshoot the dot one character;
               we skip forward two (the one character and the dot)
               when printing, so we only add one for the \0. */
            hostname = alloca(strlen(user->hostname+ii)+1);
            sprintf(hostname, "*.%s", user->hostname+ii+2);
        }
    }
    /* Emit hostmask */
    len = strlen(ident) + strlen(hostname) + 2;
    if (nickname) {
        len += strlen(nickname) + 1;
        mask = malloc(len);
        sprintf(mask, "%s!%s@%s", nickname, ident, hostname);
    } else {
        mask = malloc(len);
        sprintf(mask, "%s@%s", ident, hostname);
    }
    return mask;
}

int
IsChannelName(const char *name) {
    unsigned int ii;

    if (*name !='#')
        return 0;
    for (ii=1; name[ii]; ++ii) {
        if ((name[ii] > 0) && (name[ii] <= 32))
            return 0;
        if (name[ii] == ',')
            return 0;
        if (name[ii] == '\xa0')
            return 0;
    }
    return 1;
}

unsigned int
irc_user_modes(const struct userNode *user, char modes[], size_t length)
{
    unsigned int ii, jj;

    for (ii = jj = 0; (jj < length) && (irc_user_mode_chars[ii] != '\0'); ++ii) {
        if ((user->modes & (1 << ii)) && (irc_user_mode_chars[ii] != ' '))
            modes[jj++] = irc_user_mode_chars[ii];
    }

    ii = jj;
    while (jj < length)
        modes[jj++] = '\0';

    return ii;
}

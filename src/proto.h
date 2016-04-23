/* proto.h - IRC protocol output
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

#if !defined(PROTO_H)
#define PROTO_H

/* Warning for those looking at how this code does multi-protocol
 * support: It's an awful, nasty hack job.  It is intended for short
 * term use, not long term, since we are already developing srvx2,
 * which has much nicer interfaces that hide most of the ugly
 * differences between protocol dialects. */

#define COMBO_NUMERIC_LEN   5   /* 1/2, 1/3 or 2/3 digits for server/client parts */
#define MAXLEN              512   /* Maximum IRC line length */
#define MAXNUMPARAMS        200
#define ALLCHANMSG_FUNCS_MAX  4 /* +1 == 5 potential 'allchanmsg' funcs */

struct gline;
struct server;
struct userNode;
struct chanNode;

/* connection manager state */

enum cState
{
    DISCONNECTED,
    AUTHENTICATING,
    BURSTING,
    CONNECTED
};

#define UPLINK_UNAVAILABLE  0x001

struct uplinkNode
{
    char    *name;

    char    *host;
    int     port;

    struct sockaddr *bind_addr;
    int     bind_addr_len;

    char    *password;
    char    *their_password;

    enum cState state;
    int         tries;
    int         max_tries;
    long        flags;

    struct uplinkNode   *prev;
    struct uplinkNode   *next;
};

struct cManagerNode
{
    struct uplinkNode   *uplinks;
    struct uplinkNode   *uplink;

    int     cycles;
    int     enabled;
};

#ifdef WITH_PROTOCOL_P10
struct server* GetServerN(const char *numeric);
struct userNode* GetUserN(const char *numeric);
#endif

/* Basic protocol parsing support. */
void init_parse(void);
int parse_line(char *line, int recursive);

/* Callback notifications for protocol support. */
typedef void (*chanmsg_func_t) (struct userNode *user, struct chanNode *chan, const char *text, struct userNode *bot, unsigned int is_notice);
void reg_chanmsg_func(unsigned char prefix, struct userNode *service, chanmsg_func_t handler);
void reg_allchanmsg_func(struct userNode *service, chanmsg_func_t handler);
struct userNode *get_chanmsg_bot(unsigned char prefix);

typedef void (*privmsg_func_t) (struct userNode *user, struct userNode *target, const char *text, int server_qualified);
void reg_privmsg_func(struct userNode *user, privmsg_func_t handler);
void reg_notice_func(struct userNode *user, privmsg_func_t handler);
void unreg_privmsg_func(struct userNode *user);
void unreg_notice_func(struct userNode *user);

typedef void (*oper_func_t) (struct userNode *user);
void reg_oper_func(oper_func_t handler);

typedef void (*xquery_func_t) (struct server *source, const char routing[], const char query[]);
void reg_xquery_func(xquery_func_t handler);

/* replay silliness */
void replay_read_line(void);
void replay_event_loop(void);

/* connection maintenance */
void irc_server(struct server *srv);
void irc_user(struct userNode *user);
void irc_nick(struct userNode *user, const char *old_nick);
void irc_introduce(const char *passwd);
void irc_ping(const char *something);
void irc_pong(const char *who, const char *data);
void irc_quit(struct userNode *user, const char *message);
void irc_squit(struct server *srv, const char *message, const char *service_message);

/* messages */
void irc_privmsg(struct userNode *from, const char *to, const char *message);
void irc_notice(struct userNode *from, const char *to, const char *message);
void irc_notice_user(struct userNode *from, struct userNode *to, const char *message);
void irc_wallchops(struct userNode *from, const char *to, const char *message);

/* channel maintenance */
void irc_join(struct userNode *who, struct chanNode *what);
void irc_invite(struct userNode *from, struct userNode *who, struct chanNode *to);
void irc_mode(struct userNode *who, struct chanNode *target, const char *modes);
void irc_kick(struct userNode *who, struct userNode *target, struct chanNode *from, const char *msg);
void irc_part(struct userNode *who, struct chanNode *what, const char *reason);
void irc_topic(struct userNode *who, struct chanNode *what, const char *topic);
void irc_fetchtopic(struct userNode *from, const char *to);

/* network maintenance */
void irc_gline(struct server *srv, struct gline *gline);
void irc_settime(const char *srv_name_mask, unsigned long new_time);
void irc_ungline(const char *mask);
void irc_error(const char *to, const char *message);
void irc_kill(struct userNode *from, struct userNode *target, const char *message);
void irc_raw(const char *what);
void irc_stats(struct userNode *from, struct server *target, char type);
void irc_svsnick(struct userNode *from, struct userNode *target, const char *newnick);
void irc_xresponse(struct server *target, const char *routing, const char *response);

/* account maintenance */
void irc_account(struct userNode *user, const char *stamp, unsigned long timestamp, unsigned long serial);
void irc_regnick(struct userNode *user);
void irc_fakehost(struct userNode *user, const char *host, const char *ident, int force);

/* numeric messages */
void irc_numeric(struct userNode *user, unsigned int num, const char *format, ...);
/* RFC1459-compliant numeric responses */
#define RPL_ENDOFSTATS          219
#define RPL_STATSUPTIME         242
#define RPL_MAXCONNECTIONS      250
#define RPL_AWAY                301
#define RPL_WHOISUSER           311
#define RPL_WHOISSERVER         312
#define RPL_WHOISOPERATOR       313
#define RPL_WHOISIDLE           317
#define RPL_ENDOFWHOIS          318
#define RPL_WHOISCHANNELS       319
#define RPL_WHOISACCOUNT        330
#define RPL_WHOISACTUALLY       338
#define ERR_NOSUCHNICK          401

/* stuff originally from other headers that is really protocol-specific */
int IsChannelName(const char *name);
int is_valid_nick(const char *nick);
struct userNode *AddLocalUser(const char *nick, const char *ident, const char *hostname, const char *desc, const char *modes);
struct server* AddServer(struct server* uplink, const char *name, int hops, unsigned long boot, unsigned long link, const char *numeric, const char *description);
void DelServer(struct server* serv, int announce, const char *message);
void DelUser(struct userNode* user, struct userNode *killer, int announce, const char *why);
/* Most protocols will want to make an AddUser helper function. */

/* User modes */
extern const char irc_user_mode_chars[];
void mod_usermode(struct userNode *user, const char *modes);
unsigned int irc_user_modes(const struct userNode *user, char modes[], size_t length);

/* Channel mode manipulation */
#define KEYLEN          23
typedef unsigned long chan_mode_t;
/* Rules for struct mod_chanmode:
 * For a membership mode change, args[n].mode can contain more than
 * one mode bit (e.g. MODE_CHANOP|MODE_VOICE).  Hostmask strings are
 * "owned" by the caller and are not freed by mod_chanmode_free().
 */
struct mod_chanmode {
    chan_mode_t modes_set, modes_clear;
    unsigned int new_limit, argc;
#ifndef NDEBUG
    unsigned int alloc_argc;
#endif
    char new_key[KEYLEN + 1];
    char new_upass[KEYLEN + 1];
    char new_apass[KEYLEN + 1];
    struct {
        unsigned int mode;
        union {
            struct modeNode *member;
            const char *hostmask;
        } u;
    } args[1];
};
#define MCP_ALLOW_OVB     0x0001 /* allow op, voice, ban manipulation */
#define MCP_FROM_SERVER   0x0002 /* parse as from a server */
#define MCP_KEY_FREE      0x0004 /* -k without a key argument */
#define MCP_REGISTERED    0x0008 /* chan is already registered; do not allow changes to MODE_REGISTERED */
#define MCP_UPASS_FREE    0x0010 /* -U without a key argument */
#define MCP_APASS_FREE    0x0020 /* -A without a key argument */
#define MCP_NO_APASS      0x0040 /* Do not allow +/-A or +/-U */
#define MC_ANNOUNCE       0x0100 /* send a mod_chanmode() change out */
#define MC_NOTIFY         0x0200 /* make local callbacks to announce */
#ifdef NDEBUG
#define mod_chanmode_init(CHANMODE) do { memset((CHANMODE), 0, sizeof(*CHANMODE)); } while (0)
#else
#define mod_chanmode_init(CHANMODE) do { memset((CHANMODE), 0, sizeof(*CHANMODE)); (CHANMODE)->alloc_argc = ArrayLength((CHANMODE)->args); } while (0)
#endif

struct mod_chanmode *mod_chanmode_alloc(unsigned int argc);
struct mod_chanmode *mod_chanmode_dup(struct mod_chanmode *orig, unsigned int extra);
struct mod_chanmode *mod_chanmode_parse(struct chanNode *channel, char **modes, unsigned int argc, unsigned int flags, short base_oplevel);
void mod_chanmode_apply(struct userNode *who, struct chanNode *channel, struct mod_chanmode *change);
void mod_chanmode_announce(struct userNode *who, struct chanNode *channel, struct mod_chanmode *change);
char *mod_chanmode_format(struct mod_chanmode *desc, char *buffer);
void mod_chanmode_free(struct mod_chanmode *change);
int mod_chanmode(struct userNode *who, struct chanNode *channel, char **modes, unsigned int argc, unsigned int flags);
typedef void (*mode_change_func_t) (struct chanNode *channel, struct userNode *user, const struct mod_chanmode *change);
void reg_mode_change_func(mode_change_func_t handler);
/* irc_parse_chamode() is like mod_chanmode_parse(), but expects to *not*
 * have arguments for modes.  It is meant for building matching rules.
 */
int irc_parse_chanmode(const char *text, chan_mode_t *set, chan_mode_t *clear);
int irc_make_chanmode(struct chanNode *chan, char *out);

/* The "default" for generate_hostmask is to have all of these options off. */
#define GENMASK_STRICT_HOST   1
#define GENMASK_STRICT_IDENT  32
#define GENMASK_ANY_IDENT     64
#define GENMASK_STRICT   (GENMASK_STRICT_IDENT|GENMASK_STRICT_HOST)
#define GENMASK_USENICK  2
#define GENMASK_OMITNICK 4  /* Hurray for Kevin! */
#define GENMASK_BYIP     8
#define GENMASK_SRVXMASK 16
#define GENMASK_NO_HIDING 128
char *generate_hostmask(struct userNode *user, int options);

#endif /* !defined(PROTO_H) */

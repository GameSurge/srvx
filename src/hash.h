/* hash.h - IRC network state database
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

#ifndef HASH_H
#define HASH_H

#include "common.h"
#include "dict.h"
#include "policer.h"

#define MODE_CHANOP		0x0001 /* +o USER */
#define MODE_VOICE		0x0002 /* +v USER */
#define MODE_PRIVATE		0x0004 /* +p */
#define MODE_SECRET		0x0008 /* +s */
#define MODE_MODERATED		0x0010 /* +m */
#define MODE_TOPICLIMIT		0x0020 /* +t */
#define MODE_INVITEONLY		0x0040 /* +i */
#define MODE_NOPRIVMSGS		0x0080 /* +n */
#define MODE_KEY		0x0100 /* +k KEY */
#define MODE_BAN		0x0200 /* +b BAN */
#define MODE_LIMIT		0x0400 /* +l LIMIT */
#define MODE_DELAYJOINS         0x0800 /* +D */
#define MODE_REGONLY            0x1000 /* ircu +r, Bahamut +R */
#define MODE_NOCOLORS           0x2000 /* +c */
#define MODE_NOCTCPS            0x4000 /* +C */
#define MODE_REGISTERED         0x8000 /* Bahamut +r */
#define MODE_APASS		0x10000 /* +A adminpass */
#define MODE_UPASS		0x20000 /* +U userpass */
#define MODE_REMOVE             0x80000000

#define FLAGS_OPER		0x0001 /* Operator +O */
#define FLAGS_INVISIBLE		0x0004 /* invisible +i */
#define FLAGS_WALLOP		0x0008 /* receives wallops +w */
#define FLAGS_DEAF		0x0020 /* deaf +d */
#define FLAGS_SERVICE		0x0040 /* cannot be kicked, killed or deoped +k */
#define FLAGS_GLOBAL		0x0080 /* receives global messages +g */
#define FLAGS_PERSISTENT	0x0200 /* for reserved nicks, this isn't just one-shot */
#define FLAGS_GAGGED		0x0400 /* for gagged users */
#define FLAGS_AWAY		0x0800 /* for away users */
#define FLAGS_STAMPED           0x1000 /* for users who have been stamped */
#define FLAGS_HIDDEN_HOST       0x2000 /* user's host is masked by their account */
#define FLAGS_REGNICK           0x4000 /* user owns their current nick */
#define FLAGS_REGISTERING       0x8000 /* user has issued account register command, is waiting for email cookie */

#define IsOper(x)               ((x)->modes & FLAGS_OPER)
#define IsService(x)            ((x)->modes & FLAGS_SERVICE)
#define IsDeaf(x)               ((x)->modes & FLAGS_DEAF)
#define IsInvisible(x)          ((x)->modes & FLAGS_INVISIBLE)
#define IsGlobal(x)             ((x)->modes & FLAGS_GLOBAL)
#define IsWallOp(x)             ((x)->modes & FLAGS_WALLOP)
#define IsGagged(x)             ((x)->modes & FLAGS_GAGGED)
#define IsPersistent(x)         ((x)->modes & FLAGS_PERSISTENT) 
#define IsAway(x)               ((x)->modes & FLAGS_AWAY)
#define IsStamped(x)            ((x)->modes & FLAGS_STAMPED)
#define IsHiddenHost(x)         ((x)->modes & FLAGS_HIDDEN_HOST)
#define IsReggedNick(x)         ((x)->modes & FLAGS_REGNICK)
#define IsRegistering(x)	((x)->modes & FLAGS_REGISTERING)
#define IsFakeHost(x)           ((x)->fakehost[0] != '\0')
#define IsLocal(x)              ((x)->uplink == self)

#define NICKLEN         30
#define USERLEN         10
#define HOSTLEN         63
#define REALLEN         50
#define TOPICLEN        250
#define CHANNELLEN      200
#define MAXOPLEVEL      999

#define MAXMODEPARAMS	6
#define MAXBANS		45

/* IDLEN is 6 because it takes 5.33 Base64 digits to store 32 bytes. */
#define IDLEN           6

DECLARE_LIST(userList, struct userNode*);
DECLARE_LIST(modeList, struct modeNode*);
DECLARE_LIST(banList, struct banNode*);
DECLARE_LIST(channelList, struct chanNode*);
DECLARE_LIST(serverList, struct server*);

struct userNode {
    char *nick;                   /* Unique name of the client, nick or host */
    char ident[USERLEN + 1];      /* Per-host identification for user */
    char info[REALLEN + 1];       /* Free form additional client information */
    char hostname[HOSTLEN + 1];   /* DNS name or IP address */
    char fakehost[HOSTLEN + 1];   /* Assigned fake host */
#ifdef WITH_PROTOCOL_P10
    char numeric[COMBO_NUMERIC_LEN+1];
    unsigned int num_local : 18;
#endif
    unsigned int dead : 1;        /* Is user waiting to be recycled? */
    irc_in_addr_t ip;             /* User's IP address */
    long modes;                   /* user flags +isw etc... */

    time_t timestamp;             /* Time of last nick change */
    struct server *uplink;        /* Server that user is connected to */
    struct modeList channels;     /* Vector of channels user is in */

    /* from nickserv */
    struct handle_info *handle_info;
    struct userNode *next_authed;
    struct policer auth_policer;
};

struct chanNode {
    chan_mode_t modes;
    unsigned int limit, locks;
    char key[KEYLEN + 1];
    char upass[KEYLEN + 1];
    char apass[KEYLEN + 1];
    time_t timestamp; /* creation time */

    char topic[TOPICLEN + 1];
    char topic_nick[NICKLEN + 1];
    time_t topic_time;

    struct modeList members;
    struct banList banlist;
    struct policer join_policer;
    unsigned int join_flooded : 1;
    unsigned int bad_channel : 1;

    struct chanData *channel_info;
    struct channel_help *channel_help;
    char name[1];
};

struct banNode {
    char ban[NICKLEN + USERLEN + HOSTLEN + 3]; /* 1 for '\0', 1 for ! and 1 for @ = 3 */
    char who[NICKLEN + 1]; /* who set ban */
    time_t set; /* time ban was set */
};

struct modeNode {
    struct chanNode *channel;
    struct userNode *user;
    unsigned short modes;
    short oplevel;
    time_t idle_since;
};

#define SERVERNAMEMAX 64
#define SERVERDESCRIPTMAX 128

struct server {
    char name[SERVERNAMEMAX+1];
    time_t boot;
    time_t link;
    char description[SERVERDESCRIPTMAX+1];
#ifdef WITH_PROTOCOL_P10
    char numeric[COMBO_NUMERIC_LEN+1];
    unsigned int num_mask;
#endif
    unsigned int hops, clients, max_clients;
    unsigned int burst : 1, self_burst : 1;
    struct server *uplink;
#ifdef WITH_PROTOCOL_P10
    struct userNode **users; /* flat indexed by numeric */
#else
    dict_t users; /* indexed by nick */
#endif
    struct serverList children;
};

extern struct server *self;
extern dict_t channels;
extern dict_t clients;
extern dict_t servers;
extern unsigned int max_clients, invis_clients;
extern time_t max_clients_time;
extern struct userList curr_opers, curr_helpers;

struct server* GetServerH(const char *name); /* using full name */
struct userNode* GetUserH(const char *nick);   /* using nick */
struct chanNode* GetChannel(const char *name);
struct modeNode* GetUserMode(struct chanNode* channel, struct userNode* user);

typedef void (*server_link_func_t) (struct server *server);
void reg_server_link_func(server_link_func_t handler);

typedef int (*new_user_func_t) (struct userNode *user);
void reg_new_user_func(new_user_func_t handler);
typedef void (*del_user_func_t) (struct userNode *user, struct userNode *killer, const char *why);
void reg_del_user_func(del_user_func_t handler);
void unreg_del_user_func(del_user_func_t handler);
void ReintroduceUser(struct userNode* user);
typedef void (*nick_change_func_t)(struct userNode *user, const char *old_nick);
void reg_nick_change_func(nick_change_func_t handler);
void NickChange(struct userNode* user, const char *new_nick, int no_announce);

typedef void (*account_func_t) (struct userNode *user, const char *stamp);
void reg_account_func(account_func_t handler);
void call_account_func(struct userNode *user, const char *stamp);
void StampUser(struct userNode *user, const char *stamp);
void assign_fakehost(struct userNode *user, const char *host, int announce);

typedef void (*new_channel_func_t) (struct chanNode *chan);
void reg_new_channel_func(new_channel_func_t handler);
typedef int (*join_func_t) (struct modeNode *mNode);
void reg_join_func(join_func_t handler);
typedef void (*del_channel_func_t) (struct chanNode *chan);
void reg_del_channel_func(del_channel_func_t handler);

struct chanNode* AddChannel(const char *name, time_t time_, const char *modes, char *banlist);
void LockChannel(struct chanNode *channel);
void UnlockChannel(struct chanNode *channel);

struct modeNode* AddChannelUser(struct userNode* user, struct chanNode* channel);

typedef void (*part_func_t) (struct modeNode *mn, const char *reason);
void reg_part_func(part_func_t handler);
void unreg_part_func(part_func_t handler);
void DelChannelUser(struct userNode* user, struct chanNode* channel, const char *reason, int deleting);
void KickChannelUser(struct userNode* target, struct chanNode* channel, struct userNode *kicker, const char *why);

typedef void (*kick_func_t) (struct userNode *kicker, struct userNode *user, struct chanNode *chan);
void reg_kick_func(kick_func_t handler);
void ChannelUserKicked(struct userNode* kicker, struct userNode* victim, struct chanNode* channel);

int ChannelBanExists(struct chanNode *channel, const char *ban);

typedef int (*topic_func_t)(struct userNode *who, struct chanNode *chan, const char *old_topic);
void reg_topic_func(topic_func_t handler);
void SetChannelTopic(struct chanNode *channel, struct userNode *user, const char *topic, int announce);

void init_structs(void);

#endif

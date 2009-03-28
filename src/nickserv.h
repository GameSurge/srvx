/* nickserv.h - Nick/authentiction service
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

#ifndef _nickserv_h
#define _nickserv_h

#include "hash.h"   /* for NICKLEN, etc., and common.h */
struct svccmd;

#define NICKSERV_HANDLE_LEN NICKLEN
#define COOKIELEN 10

/* HI_FLAG_* go into handle_info.flags */
#define HI_FLAG_OPER_SUSPENDED 0x00000001
#define HI_FLAG_USE_PRIVMSG    0x00000002
#define HI_FLAG_SUPPORT_HELPER 0x00000004
#define HI_FLAG_HELPING        0x00000008
#define HI_FLAG_SUSPENDED      0x00000010
#define HI_FLAG_MIRC_COLOR     0x00000020
#define HI_FLAG_FROZEN         0x00000040
#define HI_FLAG_NODELETE       0x00000080
#define HI_FLAG_NETWORK_HELPER 0x00000100
#define HI_FLAG_BOT            0x00000200
/* Flag characters for the above.  First char is LSB, etc. */
#define HANDLE_FLAGS "SphgscfnHb"

/* HI_STYLE_* go into handle_info.userlist_style */
#define HI_STYLE_DEF    'd'
#define HI_STYLE_ZOOT   'Z'

#define HI_DEFAULT_FLAGS       (HI_FLAG_MIRC_COLOR)
#define HI_DEFAULT_STYLE       HI_STYLE_DEF

#define HANDLE_FLAGGED(hi, tok) ((hi)->flags & HI_FLAG_##tok)
#define HANDLE_SET_FLAG(hi, tok) ((hi)->flags |= HI_FLAG_##tok)
#define HANDLE_TOGGLE_FLAG(hi, tok) ((hi)->flags ^= HI_FLAG_##tok)
#define HANDLE_CLEAR_FLAG(hi, tok) ((hi)->flags &= ~HI_FLAG_##tok)

#define IsSupportHelper(user) (user->handle_info && HANDLE_FLAGGED(user->handle_info, SUPPORT_HELPER))
#define IsNetworkHelper(user) (user->handle_info && HANDLE_FLAGGED(user->handle_info, NETWORK_HELPER))
#define IsHelper(user) (IsSupportHelper(user) || IsNetworkHelper(user))
#define IsHelping(user) (user->handle_info && HANDLE_FLAGGED(user->handle_info, HELPING))
#define IsStaff(user) (IsOper(user) || IsSupportHelper(user) || IsNetworkHelper(user))
#define IsBot(user) (user->handle_info && HANDLE_FLAGGED(user->handle_info, BOT))

enum cookie_type {
    ACTIVATION,
    PASSWORD_CHANGE,
    EMAIL_CHANGE,
    ALLOWAUTH
};

struct handle_cookie {
    struct handle_info *hi;
    char *data;
    enum cookie_type type;
    unsigned long expires;
    char cookie[COOKIELEN+1];
};

struct handle_note {
    struct handle_note *next;
    unsigned long expires;
    unsigned long set;
    int id;
    char setter[NICKSERV_HANDLE_LEN+1];
    char note[1];
};

struct handle_info {
    struct nick_info *nicks;
    struct string_list *masks;
    struct userNode *users;
    struct userData *channels;
    struct handle_cookie *cookie;
    struct handle_note *notes;
    struct language *language;
    char *email_addr;
    char *epithet;
    char *infoline;
    char *handle;
    char *fakehost;
    char *fakeident;
    unsigned long id;
    unsigned long registered;
    unsigned long lastseen;
    int karma;
    unsigned short flags;
    unsigned short opserv_level;
    unsigned short screen_width;
    unsigned short table_width;
    unsigned char userlist_style;
    unsigned char maxlogins;
    char passwd[MD5_CRYPT_LENGTH+1];
    char last_quit_host[USERLEN+HOSTLEN+2];
};

struct nick_info {
    struct handle_info *owner;
    struct nick_info *next; /* next nick owned by same handle */
    char nick[NICKLEN+1];
};

struct handle_info_list {
    unsigned int used, size;
    struct handle_info **list;
    char *tag; /* e.g. email address */
};

extern const char *handle_flags;

void init_nickserv(const char *nick);
struct handle_info *get_handle_info(const char *handle);
struct handle_info *smart_get_handle_info(struct userNode *service, struct userNode *user, const char *name);
int oper_try_set_access(struct userNode *user, struct userNode *bot, struct handle_info *target, unsigned int new_level);
int oper_outranks(struct userNode *user, struct handle_info *hi);
struct nick_info *get_nick_info(const char *nick);
struct modeNode *find_handle_in_channel(struct chanNode *channel, struct handle_info *handle, struct userNode *except);
int nickserv_modify_handle_flags(struct userNode *user, struct userNode *bot, const char *str, unsigned long *add, unsigned long *remove);
int oper_has_access(struct userNode *user, struct userNode *bot, unsigned int min_level, unsigned int quiet);
void nickserv_show_oper_accounts(struct userNode *user, struct svccmd *cmd);

/* auth_funcs are called when a user gets a new handle_info.  They are
 * called *after* user->handle_info has been updated.  */
typedef void (*auth_func_t)(struct userNode *user, struct handle_info *old_handle);
void reg_auth_func(auth_func_t func);

/* Called just after a handle is renamed. */
typedef void (*handle_rename_func_t)(struct handle_info *handle, const char *old_handle);
void reg_handle_rename_func(handle_rename_func_t func);

/* unreg_funcs are called right before a handle is unregistered.
 * `user' is the person who caused the handle to be unregistered (either a
 * client authed to the handle, or an oper). */
typedef void (*unreg_func_t)(struct userNode *user, struct handle_info *handle);
void reg_unreg_func(unreg_func_t func);

/* Called just before a handle is merged */
typedef void (*handle_merge_func_t)(struct userNode *user, struct handle_info *handle_to, struct handle_info *handle_from);
void reg_handle_merge_func(handle_merge_func_t);

/* Called after an allowauth. handle is null if allowauth authorization was
 * removed */
typedef void (*allowauth_func_t)(struct userNode *user, struct userNode *target, struct handle_info *handle);
void reg_allowauth_func(allowauth_func_t func);

/* Called when an auth attempt fails because of a bad password */
typedef void (*failpw_func_t)(struct userNode *user, struct handle_info *handle);
void reg_failpw_func(failpw_func_t func);

#endif

/* mod-memoserv.c - MemoServ module for srvx
 * Copyright 2003-2004 Martijn Smit and srvx Development Team
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
 *     "memoserv" {
 *         "bot" "NickServ";
 *         "message_expiry" "30d"; // age when messages are deleted; set
 *                                 // to 0 to disable message expiration
 *     };
 *  };
 *
 * After that, to make the module active on an existing bot:
 * /msg opserv bind nickserv * *memoserv.*
 *
 * If you want a dedicated MemoServ bot, make sure the service control
 * commands are bound to OpServ:
 * /msg opserv bind opserv service *modcmd.joiner
 * /msg opserv bind opserv service\ add *modcmd.service\ add
 * /msg opserv bind opserv service\ rename *modcmd.service\ rename
 * /msg opserv bind opserv service\ trigger *modcmd.service\ trigger
 * /msg opserv bind opserv service\ remove *modcmd.service\ remove
 * Add the bot:
 * /msg opserv service add MemoServ User-to-user Memorandum Service
 * /msg opserv bind memoserv help *modcmd.help
 * Restart srvx with the updated conf file (as above, butwith "bot"
 * "MemoServ"), and bind the commands to it:
 * /msg opserv bind memoserv * *memoserv.*
 * /msg opserv bind memoserv set *modcmd.joiner
 */

#include "chanserv.h"
#include "conf.h"
#include "modcmd.h"
#include "saxdb.h"
#include "timeq.h"

#define KEY_SENT "sent"
#define KEY_RECIPIENT "to"
#define KEY_FROM "from"
#define KEY_MESSAGE "msg"
#define KEY_READ "read"

static const struct message_entry msgtab[] = {
    { "MSMSG_CANNOT_SEND", "You cannot send to account $b%s$b." },
    { "MSMSG_MEMO_SENT", "Message sent to $b%s$b." },
    { "MSMSG_NO_MESSAGES", "You have no messages." },
    { "MSMSG_MEMOS_FOUND", "Found $b%d$b matches.\nUse /msg $S READ <ID> to read a message." },
    { "MSMSG_CLEAN_INBOX", "You have $b%d$b or more messages, please clean out your inbox.\nUse /msg $S READ <ID> to read a message." },
    { "MSMSG_LIST_HEAD", "$bID$b   $bFrom$b       $bTime Sent$b" },
    { "MSMSG_LIST_FORMAT", "%-2u     %s           %s" },
    { "MSMSG_MEMO_HEAD", "Memo %u From $b%s$b, received on %s:" },
    { "MSMSG_BAD_MESSAGE_ID", "$b%s$b is not a valid message ID (it should be a number between 0 and %u)." },
    { "MSMSG_NO_SUCH_MEMO", "You have no memo with that ID." },
    { "MSMSG_MEMO_DELETED", "Memo $b%d$b deleted." },
    { "MSMSG_EXPIRY_OFF", "I am currently not expiring messages. (turned off)" },
    { "MSMSG_EXPIRY", "Messages will be expired when they are %s old (%d seconds)." },
    { "MSMSG_MESSAGES_EXPIRED", "$b%lu$b message(s) expired." },
    { "MSMSG_MEMOS_INBOX", "You have $b%d$b new message(s) in your inbox and %d old messages.  Use /msg $S LIST to list them." },
    { "MSMSG_NEW_MESSAGE", "You have a new message from $b%s$b." },
    { "MSMSG_DELETED_ALL", "Deleted all of your messages." },
    { "MSMSG_USE_CONFIRM", "Please use /msg $S DELETE * $bCONFIRM$b to delete $uall$u of your messages." },
    { "MSMSG_STATUS_TOTAL", "I have $b%u$b memos in my database." },
    { "MSMSG_STATUS_EXPIRED", "$b%ld$b memos expired during the time I am awake." },
    { "MSMSG_STATUS_SENT", "$b%ld$b memos have been sent." },
    { "MSMSG_SET_NOTIFY",     "$bNotify:       $b %s" },
    { "MSMSG_SET_AUTHNOTIFY", "$bAuthNotify:   $b %s" },
    { "MSMSG_SET_PRIVATE",    "$bPrivate:      $b %s" },
    { NULL, NULL }
};

struct memo {
    struct memo_account *recipient;
    struct memo_account *sender;
    char *message;
    time_t sent;
    unsigned int is_read : 1;
};

DECLARE_LIST(memoList, struct memo*);
DEFINE_LIST(memoList, struct memo*);

/* memo_account.flags fields */
#define MEMO_NOTIFY_NEW   1
#define MEMO_NOTIFY_LOGIN 2
#define MEMO_DENY_NONCHANNEL 4

struct memo_account {
    struct handle_info *handle;
    unsigned int flags;
    struct memoList sent;
    struct memoList recvd;
};

static struct {
    struct userNode *bot;
    int message_expiry;
} memoserv_conf;

const char *memoserv_module_deps[] = { NULL };
static struct module *memoserv_module;
static struct log_type *MS_LOG;
static unsigned long memosSent, memosExpired;
static struct dict *memos; /* memo_account->handle->handle -> memo_account */

static struct memo_account *
memoserv_get_account(struct handle_info *hi)
{
    struct memo_account *ma;
    if (!hi)
        return NULL;
    ma = dict_find(memos, hi->handle, NULL);
    if (ma)
        return ma;
    ma = calloc(1, sizeof(*ma));
    if (!ma)
        return ma;
    ma->handle = hi;
    ma->flags = MEMO_NOTIFY_NEW | MEMO_NOTIFY_LOGIN;
    dict_insert(memos, ma->handle->handle, ma);
    return ma;
}

static void
delete_memo(struct memo *memo)
{
    memoList_remove(&memo->recipient->recvd, memo);
    memoList_remove(&memo->sender->sent, memo);
    free(memo->message);
    free(memo);
}

static void
delete_memo_account(void *data)
{
    struct memo_account *ma = data;

    while (ma->recvd.used)
        delete_memo(ma->recvd.list[0]);
    while (ma->sent.used)
        delete_memo(ma->sent.list[0]);
    memoList_clean(&ma->recvd);
    memoList_clean(&ma->sent);
    free(ma);
}

void
do_expire(void)
{
    dict_iterator_t it;
    for (it = dict_first(memos); it; it = iter_next(it)) {
        struct memo_account *acct = iter_data(it);
        unsigned int ii;
        for (ii = 0; ii < acct->sent.used; ++ii) {
            struct memo *memo = acct->sent.list[ii];
            if ((now - memo->sent) > memoserv_conf.message_expiry) {
                delete_memo(memo);
                memosExpired++;
                ii--;
            }
        }
    }
}

static void
expire_memos(UNUSED_ARG(void *data))
{
    if (memoserv_conf.message_expiry) {
        do_expire();
        timeq_add(now + memoserv_conf.message_expiry, expire_memos, NULL);
    }
}

static struct memo*
add_memo(time_t sent, struct memo_account *recipient, struct memo_account *sender, char *message)
{
    struct memo *memo;

    memo = calloc(1, sizeof(*memo));
    if (!memo)
        return NULL;

    memo->recipient = recipient;
    memoList_append(&recipient->recvd, memo);
    memo->sender = sender;
    memoList_append(&sender->sent, memo);
    memo->sent = sent;
    memo->message = strdup(message);
    memosSent++;
    return memo;
}

static int
memoserv_can_send(struct userNode *bot, struct userNode *user, struct memo_account *acct)
{
    extern struct userData *_GetChannelUser(struct chanData *channel, struct handle_info *handle, int override, int allow_suspended);
    struct userData *dest;

    if (!user->handle_info)
        return 0;
    if (!(acct->flags & MEMO_DENY_NONCHANNEL))
        return 1;
    for (dest = acct->handle->channels; dest; dest = dest->u_next)
        if (_GetChannelUser(dest->channel, user->handle_info, 1, 0))
            return 1;
    send_message(user, bot, "MSMSG_CANNOT_SEND", acct->handle->handle);
    return 0;
}

static struct memo *find_memo(struct userNode *user, struct svccmd *cmd, struct memo_account *ma, const char *msgid, unsigned int *id)
{
    unsigned int memoid;
    if (!isdigit(msgid[0])) {
        if (ma->recvd.used)
            reply("MSMSG_BAD_MESSAGE_ID", msgid, ma->recvd.used - 1);
        else
            reply("MSMSG_NO_MESSAGES");
        return NULL;
    }
    memoid = atoi(msgid);
    if (memoid >= ma->recvd.used) {
        reply("MSMSG_NO_SUCH_MEMO");
        return NULL;
    }
    return ma->recvd.list[*id = memoid];
}

static MODCMD_FUNC(cmd_send)
{
    char *message;
    struct handle_info *hi;
    struct memo_account *ma, *sender;

    if (!(hi = modcmd_get_handle_info(user, argv[1])))
        return 0;
    if (!(sender = memoserv_get_account(user->handle_info))
        || !(ma = memoserv_get_account(hi))) {
        reply("MSG_INTERNAL_FAILURE");
        return 0;
    }
    if (!(memoserv_can_send(cmd->parent->bot, user, ma)))
        return 0;
    message = unsplit_string(argv + 2, argc - 2, NULL);
    add_memo(now, ma, sender, message);
    if (ma->flags & MEMO_NOTIFY_NEW) {
        struct userNode *other;
        for (other = ma->handle->users; other; other = other->next_authed)
            send_message(other, cmd->parent->bot, "MSMSG_NEW_MESSAGE", user->nick);
    }
    reply("MSMSG_MEMO_SENT", ma->handle->handle);
    return 1;
}

static MODCMD_FUNC(cmd_list)
{
    struct memo_account *ma;
    struct memo *memo;
    unsigned int ii;
    char posted[24];
    struct tm tm;

    if (!(ma = memoserv_get_account(user->handle_info)))
        return 0;
    reply("MSMSG_LIST_HEAD");
    for (ii = 0; (ii < ma->recvd.used) && (ii < 15); ++ii) {
        memo = ma->recvd.list[ii];
        localtime_r(&memo->sent, &tm);
        strftime(posted, sizeof(posted), "%I:%M %p, %m/%d/%Y", &tm);
        reply("MSMSG_LIST_FORMAT", ii, memo->sender->handle->handle, posted);
    }
    if (ii == 0)
        reply("MSG_NONE");
    else if (ii == 15)
        reply("MSMSG_CLEAN_INBOX", ii);
    else
        reply("MSMSG_MEMOS_FOUND", ii);
    return 1;
}

static MODCMD_FUNC(cmd_read)
{
    struct memo_account *ma;
    unsigned int memoid;
    struct memo *memo;
    char posted[24];
    struct tm tm;

    if (!(ma = memoserv_get_account(user->handle_info)))
        return 0;
    if (!(memo = find_memo(user, cmd, ma, argv[1], &memoid)))
        return 0;
    localtime_r(&memo->sent, &tm);
    strftime(posted, sizeof(posted), "%I:%M %p, %m/%d/%Y", &tm);
    reply("MSMSG_MEMO_HEAD", memoid, memo->sender->handle->handle, posted);
    send_message_type(4, user, cmd->parent->bot, "%s", memo->message);
    memo->is_read = 1;
    return 1;
}

static MODCMD_FUNC(cmd_delete)
{
    struct memo_account *ma;
    struct memo *memo;
    unsigned int memoid;

    if (!(ma = memoserv_get_account(user->handle_info)))
        return 0;
    if (!irccasecmp(argv[1], "*") || !irccasecmp(argv[1], "all")) {
        if ((argc < 3) || irccasecmp(argv[2], "confirm")) {
            reply("MSMSG_USE_CONFIRM");
            return 0;
        }
        while (ma->recvd.used)
            delete_memo(ma->recvd.list[0]);
        reply("MSMSG_DELETED_ALL");
        return 1;
    }

    if (!(memo = find_memo(user, cmd, ma, argv[1], &memoid)))
        return 0;
    delete_memo(memo);
    reply("MSMSG_MEMO_DELETED", memoid);
    return 1;
}

static MODCMD_FUNC(cmd_expire)
{
    unsigned long old_expired = memosExpired;
    do_expire();
    reply("MSMSG_MESSAGES_EXPIRED", memosExpired - old_expired);
    return 1;
}

static MODCMD_FUNC(cmd_expiry)
{
    char interval[INTERVALLEN];

    if (!memoserv_conf.message_expiry) {
        reply("MSMSG_EXPIRY_OFF");
        return 1;
    }

    intervalString(interval, memoserv_conf.message_expiry, user->handle_info);
    reply("MSMSG_EXPIRY", interval, memoserv_conf.message_expiry);
    return 1;
}

static MODCMD_FUNC(cmd_set_notify)
{
    struct memo_account *ma;
    char *choice;

    if (!(ma = memoserv_get_account(user->handle_info)))
        return 0;
    if (argc > 1) {
        choice = argv[1];
        if (enabled_string(choice)) {
            ma->flags |= MEMO_NOTIFY_NEW;
        } else if (disabled_string(choice)) {
            ma->flags &= ~MEMO_NOTIFY_NEW;
        } else {
            reply("MSG_INVALID_BINARY", choice);
            return 0;
        }
    }

    choice = (ma->flags & MEMO_NOTIFY_NEW) ? "on" : "off";
    reply("MSMSG_SET_NOTIFY", choice);
    return 1;
}

static MODCMD_FUNC(cmd_set_authnotify)
{
    struct memo_account *ma;
    char *choice;

    if (!(ma = memoserv_get_account(user->handle_info)))
        return 0;
    if (argc > 1) {
        choice = argv[1];
        if (enabled_string(choice)) {
            ma->flags |= MEMO_NOTIFY_LOGIN;
        } else if (disabled_string(choice)) {
            ma->flags &= ~MEMO_NOTIFY_LOGIN;
        } else {
            reply("MSG_INVALID_BINARY", choice);
            return 0;
        }
    }

    choice = (ma->flags & MEMO_NOTIFY_LOGIN) ? "on" : "off";
    reply("MSMSG_SET_AUTHNOTIFY", choice);
    return 1;
}

static MODCMD_FUNC(cmd_set_private)
{
    struct memo_account *ma;
    char *choice;

    if (!(ma = memoserv_get_account(user->handle_info)))
        return 0;
    if (argc > 1) {
        choice = argv[1];
        if (enabled_string(choice)) {
            ma->flags |= MEMO_DENY_NONCHANNEL;
        } else if (disabled_string(choice)) {
            ma->flags &= ~MEMO_DENY_NONCHANNEL;
        } else {
            reply("MSG_INVALID_BINARY", choice);
            return 0;
        }
    }

    choice = (ma->flags & MEMO_DENY_NONCHANNEL) ? "on" : "off";
    reply("MSMSG_SET_PRIVATE", choice);
    return 1;
}

static MODCMD_FUNC(cmd_status)
{
    reply("MSMSG_STATUS_TOTAL", dict_size(memos));
    reply("MSMSG_STATUS_EXPIRED", memosExpired);
    reply("MSMSG_STATUS_SENT", memosSent);
    return 1;
}

static void
memoserv_conf_read(void)
{
    dict_t conf_node;
    const char *str;

    str = "modules/memoserv";
    if (!(conf_node = conf_get_data(str, RECDB_OBJECT))) {
        log_module(MS_LOG, LOG_ERROR, "config node `%s' is missing or has wrong type.", str);
        return;
    }

    str = database_get_data(conf_node, "message_expiry", RECDB_QSTRING);
    memoserv_conf.message_expiry = str ? ParseInterval(str) : 60*24*30;
}

static int
memoserv_saxdb_read(struct dict *db)
{
    char *str;
    struct handle_info *sender, *recipient;
    struct record_data *hir;
    struct memo *memo;
    dict_iterator_t it;
    time_t sent;

    for (it = dict_first(db); it; it = iter_next(it)) {
        hir = iter_data(it);
        if (hir->type != RECDB_OBJECT) {
            log_module(MS_LOG, LOG_WARNING, "Unexpected rectype %d for %s.", hir->type, iter_key(it));
            continue;
        }

        if (!(str = database_get_data(hir->d.object, KEY_SENT, RECDB_QSTRING))) {
            log_module(MS_LOG, LOG_ERROR, "Date sent not present in memo %s; skipping", iter_key(it));
            continue;
        }
        sent = atoi(str);

        if (!(str = database_get_data(hir->d.object, KEY_RECIPIENT, RECDB_QSTRING))) {
            log_module(MS_LOG, LOG_ERROR, "Recipient not present in memo %s; skipping", iter_key(it));
            continue;
        } else if (!(recipient = get_handle_info(str))) {
            log_module(MS_LOG, LOG_ERROR, "Invalid recipient %s in memo %s; skipping", str, iter_key(it));
            continue;
        }

        if (!(str = database_get_data(hir->d.object, KEY_FROM, RECDB_QSTRING))) {
            log_module(MS_LOG, LOG_ERROR, "Sender not present in memo %s; skipping", iter_key(it));
            continue;
        } else if (!(sender = get_handle_info(str))) {
            log_module(MS_LOG, LOG_ERROR, "Invalid sender %s in memo %s; skipping", str, iter_key(it));
            continue;
        }

        if (!(str = database_get_data(hir->d.object, KEY_MESSAGE, RECDB_QSTRING))) {
            log_module(MS_LOG, LOG_ERROR, "Message not present in memo %s; skipping", iter_key(it));
            continue;
        }

        memo = add_memo(sent, memoserv_get_account(recipient), memoserv_get_account(sender), str);
        if ((str = database_get_data(hir->d.object, KEY_READ, RECDB_QSTRING)))
            memo->is_read = 1;
    }
    return 0;
}

static int
memoserv_saxdb_write(struct saxdb_context *ctx)
{
    dict_iterator_t it;
    struct memo_account *ma;
    struct memo *memo;
    char str[7];
    unsigned int id = 0, ii;

    for (it = dict_first(memos); it; it = iter_next(it)) {
        ma = iter_data(it);
        for (ii = 0; ii < ma->recvd.used; ++ii) {
            memo = ma->recvd.list[ii];
            saxdb_start_record(ctx, inttobase64(str, id++, sizeof(str)), 0);
            saxdb_write_int(ctx, KEY_SENT, memo->sent);
            saxdb_write_string(ctx, KEY_RECIPIENT, memo->recipient->handle->handle);
            saxdb_write_string(ctx, KEY_FROM, memo->sender->handle->handle);
            saxdb_write_string(ctx, KEY_MESSAGE, memo->message);
            if (memo->is_read)
                saxdb_write_int(ctx, KEY_READ, 1);
            saxdb_end_record(ctx);
        }
    }
    return 0;
}

static void
memoserv_cleanup(void)
{
    dict_delete(memos);
}

static void
memoserv_check_messages(struct userNode *user, UNUSED_ARG(struct handle_info *old_handle))
{
    unsigned int ii, unseen;
    struct memo_account *ma;
    struct memo *memo;

    if (!(ma = memoserv_get_account(user->handle_info))
        || !(ma->flags & MEMO_NOTIFY_LOGIN))
        return;
    for (ii = unseen = 0; ii < ma->recvd.used; ++ii) {
        memo = ma->recvd.list[ii];
        if (!memo->is_read)
            unseen++;
    }
    if (ma->recvd.used && memoserv_conf.bot)
        send_message(user, memoserv_conf.bot, "MSMSG_MEMOS_INBOX", unseen, ma->recvd.used - unseen);
}

static void
memoserv_rename_account(struct handle_info *hi, const char *old_handle)
{
    struct memo_account *ma;
    if (!(ma = dict_find(memos, old_handle, NULL)))
        return;
    dict_remove2(memos, old_handle, 1);
    dict_insert(memos, hi->handle, ma);
}

static void
memoserv_unreg_account(UNUSED_ARG(struct userNode *user), struct handle_info *handle)
{
    dict_remove(memos, handle->handle);
}

int
memoserv_init(void)
{
    MS_LOG = log_register_type("MemoServ", "file:memoserv.log");
    memos = dict_new();
    dict_set_free_data(memos, delete_memo_account);
    reg_auth_func(memoserv_check_messages);
    reg_handle_rename_func(memoserv_rename_account);
    reg_unreg_func(memoserv_unreg_account);
    conf_register_reload(memoserv_conf_read);
    reg_exit_func(memoserv_cleanup);
    saxdb_register("MemoServ", memoserv_saxdb_read, memoserv_saxdb_write);

    memoserv_module = module_register("MemoServ", MS_LOG, "mod-memoserv.help", NULL);
    modcmd_register(memoserv_module, "send", cmd_send, 3, MODCMD_REQUIRE_AUTHED, NULL);
    modcmd_register(memoserv_module, "list", cmd_list, 1, MODCMD_REQUIRE_AUTHED, NULL);
    modcmd_register(memoserv_module, "read", cmd_read, 2, MODCMD_REQUIRE_AUTHED, NULL);
    modcmd_register(memoserv_module, "delete", cmd_delete, 2, MODCMD_REQUIRE_AUTHED, NULL);
    modcmd_register(memoserv_module, "expire", cmd_expire, 1, MODCMD_REQUIRE_AUTHED, "flags", "+oper", NULL);
    modcmd_register(memoserv_module, "expiry", cmd_expiry, 1, 0, NULL);
    modcmd_register(memoserv_module, "status", cmd_status, 1, 0, NULL);
    modcmd_register(memoserv_module, "set notify", cmd_set_notify, 1, 0, NULL);
    modcmd_register(memoserv_module, "set authnotify", cmd_set_authnotify, 1, 0, NULL);
    modcmd_register(memoserv_module, "set private", cmd_set_private, 1, 0, NULL);
    message_register_table(msgtab);

    if (memoserv_conf.message_expiry)
        timeq_add(now + memoserv_conf.message_expiry, expire_memos, NULL);
    return 1;
}

int
memoserv_finalize(void) {
    dict_t conf_node;
    const char *str;

    str = "modules/memoserv";
    if (!(conf_node = conf_get_data(str, RECDB_OBJECT))) {
        log_module(MS_LOG, LOG_ERROR, "config node `%s' is missing or has wrong type.", str);
        return 0;
    }

    str = database_get_data(conf_node, "bot", RECDB_QSTRING);
    if (str)
        memoserv_conf.bot = GetUserH(str);
    return 1;
}

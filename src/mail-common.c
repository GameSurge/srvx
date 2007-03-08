/* mail-common.c - mail sending utilities
 * Copyright 2002-2004, 2007 srvx Development Team
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
#include "modcmd.h"
#include "nickserv.h"
#include "saxdb.h"

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#define KEY_PROHIBITED   "prohibited"

static const struct message_entry msgtab[] = {
    { "MAILMSG_EMAIL_ALREADY_BANNED", "%s is already banned (%s)." },
    { "MAILMSG_EMAIL_BANNED", "Email to %s has been forbidden." },
    { "MAILMSG_EMAIL_NOT_BANNED", "Email to %s was not forbidden." },
    { "MAILMSG_EMAIL_UNBANNED", "Email to %s is now allowed." },
    { "MAILMSG_PROHIBITED_EMAIL", "%s: %s" },
    { "MAILMSG_NO_PROHIBITED_EMAIL", "All email addresses are accepted." },
    { NULL, NULL }
};

static dict_t prohibited_addrs, prohibited_masks;
struct module *mail_module;

const char *
mail_prohibited_address(const char *addr)
{
    dict_iterator_t it;
    const char *data;

    if (prohibited_addrs && (data = dict_find(prohibited_addrs, addr, NULL)))
        return data;
    if (prohibited_masks)
        for (it = dict_first(prohibited_masks); it; it = iter_next(it))
            if (match_ircglob(addr, iter_key(it)))
                return iter_data(it);
    return NULL;
}

static int
mail_ban_address(struct userNode *user, struct userNode *bot, const char *addr, const char *reason) {
    dict_t target;
    const char *str;

    target = strpbrk(addr, "*?") ? prohibited_masks : prohibited_addrs;
    if ((str = dict_find(target, addr, NULL))) {
        if (user)
            send_message(user, bot, "MAILMSG_EMAIL_ALREADY_BANNED", addr, str);
        return 0;
    }
    dict_insert(target, strdup(addr), strdup(reason));
    if (user) send_message(user, bot, "MAILMSG_EMAIL_BANNED", addr);
    return 1;
}

static MODCMD_FUNC(cmd_banemail) {
    char *reason = unsplit_string(argv+2, argc-2, NULL);
    return mail_ban_address(user, cmd->parent->bot, argv[1], reason);
}

static MODCMD_FUNC(cmd_unbanemail) {
    dict_t target;
    const char *addr;

    addr = argv[1];
    target = strpbrk(addr, "*?") ? prohibited_masks : prohibited_addrs;
    if (dict_remove(target, addr))
        reply("MAILMSG_EMAIL_UNBANNED", addr);
    else
        reply("MAILMSG_EMAIL_NOT_BANNED", addr);
    return 1;
}

static MODCMD_FUNC(cmd_stats_email) {
    dict_iterator_t it;
    int found = 0;

    for (it=dict_first(prohibited_addrs); it; it=iter_next(it)) {
        reply("MAILMSG_PROHIBITED_EMAIL", iter_key(it), (const char*)iter_data(it));
        found = 1;
    }
    for (it=dict_first(prohibited_masks); it; it=iter_next(it)) {
        reply("MAILMSG_PROHIBITED_EMAIL", iter_key(it), (const char*)iter_data(it));
        found = 1;
    }
    if (!found)
        reply("MAILMSG_NO_PROHIBITED_EMAIL");
    return 0;
}

static int
mail_saxdb_read(struct dict *db) {
    struct dict *subdb;
    struct record_data *rd;
    dict_iterator_t it;

    if ((subdb = database_get_data(db, KEY_PROHIBITED, RECDB_OBJECT))) {
        for (it = dict_first(subdb); it; it = iter_next(it)) {
            rd = iter_data(it);
            if (rd->type == RECDB_QSTRING)
                mail_ban_address(NULL, NULL, iter_key(it), rd->d.qstring);
        }
    }
    return 0;
}

static int
mail_saxdb_write(struct saxdb_context *ctx) {
    dict_iterator_t it;

    saxdb_start_record(ctx, KEY_PROHIBITED, 0);
    for (it = dict_first(prohibited_masks); it; it = iter_next(it))
        saxdb_write_string(ctx, iter_key(it), iter_data(it));
    for (it = dict_first(prohibited_addrs); it; it = iter_next(it))
        saxdb_write_string(ctx, iter_key(it), iter_data(it));
    saxdb_end_record(ctx);
    return 0;
}

static void
mail_common_cleanup(void)
{
    dict_delete(prohibited_addrs);
    dict_delete(prohibited_masks);
}

static void
mail_common_init(void)
{
    prohibited_addrs = dict_new();
    dict_set_free_keys(prohibited_addrs, free);
    dict_set_free_data(prohibited_addrs, free);
    prohibited_masks = dict_new();
    dict_set_free_keys(prohibited_masks, free);
    dict_set_free_data(prohibited_masks, free);
    reg_exit_func(mail_common_cleanup);
    saxdb_register("sendmail", mail_saxdb_read, mail_saxdb_write);
    mail_module = module_register("sendmail", MAIN_LOG, "mail.help", NULL);
    modcmd_register(mail_module, "banemail", cmd_banemail, 3, 0, "level", "601", NULL);
    modcmd_register(mail_module, "stats email", cmd_stats_email, 0, 0, "flags", "+oper", NULL);
    modcmd_register(mail_module, "unbanemail", cmd_unbanemail, 2, 0, "level", "601", NULL);
    message_register_table(msgtab);
}

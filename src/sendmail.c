/* sendmail.c - mail sending utilities
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
struct module *sendmail_module;

const char *
sendmail_prohibited_address(const char *addr)
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

/* This function sends the given "paragraph" as flowed text, as
 * defined in RFC 2646.  It lets us only worry about line wrapping
 * here, and not in the code that generates mail.
 */
static void
send_flowed_text(FILE *where, const char *para)
{
    const char *eol = strchr(para, '\n');
    unsigned int shift;

    while (*para) {
        /* Do we need to space-stuff the line? */
        if ((*para == ' ') || (*para == '>') || !strncmp(para, "From ", 5)) {
            fputc(' ', where);
            shift = 1;
        } else {
            shift = 0;
        }
        /* How much can we put on this line? */
        if (!eol && (strlen(para) < (80 - shift))) {
            /* End of paragraph; can put on one line. */
            fputs(para, where);
            fputs("\n", where);
            break;
        } else if (eol && (eol < para + (80 - shift))) {
            /* Newline inside paragraph, no need to wrap. */
            fprintf(where, "%.*s\n", eol - para, para);
            para = eol + 1;
        } else {
            int pos;
            /* Need to wrap.  Where's the last space in the line? */
            for (pos=72-shift; pos && (para[pos] != ' '); pos--) ;
            /* If we didn't find a space, look ahead instead. */
            if (pos == 0) pos = strcspn(para, " \n");
            fprintf(where, "%.*s\n", pos+1, para);
            para += pos + 1;
        }
        if (eol && (eol < para)) eol = strchr(para, '\n');
    }
}

void
sendmail(struct userNode *from, struct handle_info *to, const char *subject, const char *body, int first_time)
{
    pid_t child;
    int infds[2], outfds[2];
    const char *fromaddr, *str;

    /* Grab some config items first. */
    str = conf_get_data("mail/enable", RECDB_QSTRING);
    if (!str || !enabled_string(str))
        return;
    fromaddr = conf_get_data("mail/from_address", RECDB_QSTRING);

    /* How this works: We fork, and the child tries to send the mail.
     * It does this by setting up a pipe pair, and forking again (the
     * grandchild exec()'s the mailer program).  The mid-level child
     * sends the text to the grandchild's stdin, and then logs the
     * success or failure.
     */

    child = fork();
    if (child < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "sendmail() to %s couldn't fork(): %s (%d)", to->email_addr, strerror(errno), errno);
        return;
    } else if (child > 0) {
        return;
    }
    /* We're in a child now; must _exit() to die properly. */
    if (pipe(infds) < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "sendmail() child to %s couldn't pipe(infds): %s (%d)", to->email_addr, strerror(errno), errno);
        _exit(1);
    }
    if (pipe(outfds) < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "sendmail() child to %s couldn't pipe(outfds): %s (%d)", to->email_addr, strerror(errno), errno);
        _exit(1);
    }
    child = fork();
    if (child < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "sendmail() child to %s couldn't fork(): %s (%d)", to->email_addr, strerror(errno), errno);
        _exit(1);
    } else if (child > 0) {
        /* Mid-level child; get ready to send the mail. */
        FILE *out = fdopen(infds[1], "w");
        struct string_list *extras;
        unsigned int nn;
        int res, rv;

        /* Close the end of pipes we do not use. */
        close(infds[0]);
        close(outfds[1]);

        /* Do we have any "extra" headers to send? */
        extras = conf_get_data("mail/extra_headers", RECDB_STRING_LIST);
        if (extras) {
            for (nn=0; nn<extras->used; nn++) {
                fputs(extras->list[nn], out);
                fputs("\n", out);
            }
        }

        /* Content type?  (format=flowed is a standard for plain text
         * that lets the receiver reconstruct paragraphs, defined in
         * RFC 2646.  See comment above send_flowed_text() for more.)
         */
        if (!(str = conf_get_data("mail/charset", RECDB_QSTRING))) str = "us-ascii";
        fprintf(out, "Content-Type: text/plain; charset=%s; format=flowed\n", str);

        /* Send From, To and Subject headers */
        if (!fromaddr) fromaddr = "admin@poorly.configured.network";
        fprintf(out, "From: %s <%s>\n", from->nick, fromaddr);
        fprintf(out, "To: \"%s\" <%s>\n", to->handle, to->email_addr);
        fprintf(out, "Subject: %s\n", subject);

        /* Send mail body */
        fputs("\n", out); /* terminate headers */
        extras = conf_get_data((first_time?"mail/body_prefix_first":"mail/body_prefix"), RECDB_STRING_LIST);
        if (extras) {
            for (nn=0; nn<extras->used; nn++) {
                send_flowed_text(out, extras->list[nn]);
            }
            fputs("\n", out);
        }
        send_flowed_text(out, body);
        extras = conf_get_data((first_time?"mail/body_suffix_first":"mail/body_suffix"), RECDB_STRING_LIST);
        if (extras) {
            fputs("\n", out);
            for (nn=0; nn<extras->used; nn++)
                send_flowed_text(out, extras->list[nn]);
        }

        /* Close file (sending mail) and check for return code */
        fflush(out);
        fclose(out);
        do {
            rv = wait4(child, &res, 0, NULL);
        } while ((rv == -1) && (errno == EINTR));
        if (rv == child) {
            /* accept the wait() result */
        } else {
            log_module(MAIN_LOG, LOG_ERROR, "sendmail() child to %s: Bad wait() return code %d: %s (%d)", to->email_addr, rv, strerror(errno), errno);
            _exit(1);
        }
        if (res) {
            log_module(MAIN_LOG, LOG_ERROR, "sendmail() grandchild to %s: Exited with code %d", to->email_addr, res);
            _exit(1);
        } else {
            log_module(MAIN_LOG, LOG_INFO, "sendmail() sent email to %s <%s>: %s", to->handle, to->email_addr, subject);
        }
        _exit(0);
    } else {
        /* Grandchild; dup2 the fds and exec the mailer. */
        const char *argv[10], *mpath;
        unsigned int argc = 0;

        /* Close the end of pipes we do not use. */
        close(infds[1]);
        close(outfds[0]);

        dup2(infds[0], STDIN_FILENO);
        dup2(outfds[1], STDOUT_FILENO);
        mpath = conf_get_data("mail/mailer", RECDB_QSTRING);
        if (!mpath) mpath = "/usr/sbin/sendmail";
        argv[argc++] = mpath;
        if (fromaddr) {
            argv[argc++] = "-f";
            argv[argc++] = fromaddr;
        }
        argv[argc++] = to->email_addr;
        argv[argc++] = NULL;
        if (execv(mpath, (char**)argv) < 0) {
            log_module(MAIN_LOG, LOG_ERROR, "sendmail() grandchild to %s couldn't execv(): %s (%d)", to->email_addr, strerror(errno), errno);
        }
        _exit(1);
    }
}

static int
sendmail_ban_address(struct userNode *user, struct userNode *bot, const char *addr, const char *reason) {
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
    return sendmail_ban_address(user, cmd->parent->bot, argv[1], reason);
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
sendmail_saxdb_read(struct dict *db) {
    struct dict *subdb;
    struct record_data *rd;
    dict_iterator_t it;

    if ((subdb = database_get_data(db, KEY_PROHIBITED, RECDB_OBJECT))) {
        for (it = dict_first(subdb); it; it = iter_next(it)) {
            rd = iter_data(it);
            if (rd->type == RECDB_QSTRING)
                sendmail_ban_address(NULL, NULL, iter_key(it), rd->d.qstring);
        }
    }
    return 0;
}

static int
sendmail_saxdb_write(struct saxdb_context *ctx) {
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
sendmail_cleanup(void)
{
    dict_delete(prohibited_addrs);
    dict_delete(prohibited_masks);
}

void
sendmail_init(void)
{
    prohibited_addrs = dict_new();
    dict_set_free_keys(prohibited_addrs, free);
    dict_set_free_data(prohibited_addrs, free);
    prohibited_masks = dict_new();
    dict_set_free_keys(prohibited_masks, free);
    dict_set_free_data(prohibited_masks, free);
    reg_exit_func(sendmail_cleanup);
    saxdb_register("sendmail", sendmail_saxdb_read, sendmail_saxdb_write);
    sendmail_module = module_register("sendmail", MAIN_LOG, "sendmail.help", NULL);
    modcmd_register(sendmail_module, "banemail", cmd_banemail, 3, 0, "level", "601", NULL);
    modcmd_register(sendmail_module, "stats email", cmd_stats_email, 0, 0, "flags", "+oper", NULL);
    modcmd_register(sendmail_module, "unbanemail", cmd_unbanemail, 2, 0, "level", "601", NULL);
    message_register_table(msgtab);
}

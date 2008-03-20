/* mail-smtp.c - mail sending utilities
 * Copyright 2007 srvx Development Team
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

#include "ioset.h"

static void mail_println(const char *fmt, ...);

#include "mail-common.c"

struct pending_mail {
    const char *from;
    const char *to_name;
    const char *to_email;
    const char *subject;
    const char *body;
    int first_time;
};

DECLARE_LIST(mail_queue, struct pending_mail *);

enum smtp_socket_state {
    CLOSED, /* no connection active */
    CONNECTING, /* initial connection in progress */
    WAITING_GREETING, /* waiting for server to send 220 <whatever> */
    IDLE, /* between messages, waiting to see if we get a new one soon */
    SENT_EHLO, /* sent EHLO <ourname>, waiting for response */
    SENT_HELO, /* sent HELO <ourname>, waiting for response */
    SENT_MAIL_FROM, /* sent MAIL FROM:<address>, waiting for response */
    SENT_RCPT_TO, /* sent RCPT TO:<address>, waiting for response */
    SENT_DATA, /* sent DATA, waiting for response */
    SENT_BODY, /* sent message body, waiting for response */
    SENT_RSET, /* sent RSET, waiting for response */
    SENT_QUIT, /* asked server to close connection, waiting for response */
};

static const char * const smtp_state_names[] = {
    "closed",
    "connecting",
    "greeting",
    "idle",
    "ehlo",
    "helo",
    "mail-from",
    "rcpt-to",
    "data",
    "body",
    "rset",
    "quit",
};

static struct pending_mail *active_mail;
static struct log_type *MAIL_LOG;
static struct io_fd *smtp_fd;
static enum smtp_socket_state smtp_state;
static struct mail_queue mail_queue;

static struct {
    const char *smtp_server;
    const char *smtp_service;
    const char *smtp_myname;
    const char *smtp_from;
    int enabled;
} smtp_conf;

DEFINE_LIST(mail_queue, struct pending_mail *)

static void mail_println(const char *fmt, ...)
{
    char tmpbuf[1024];
    va_list ap;
    int res;

    va_start(ap, fmt);
    res = vsnprintf(tmpbuf, sizeof(tmpbuf) - 2, fmt, ap);
    va_end(ap);
    if (res > 0 && (size_t)res <= sizeof(tmpbuf) - 2)
    {
        tmpbuf[res++] = '\r';
        tmpbuf[res++] = '\n';
        ioset_write(smtp_fd, tmpbuf, res);
    }
}

static void mail_smtp_read_config(void)
{
    dict_t conf_node;
    const char *str;

    memset(&smtp_conf, 0, sizeof(smtp_conf));
    conf_node = conf_get_data("mail", RECDB_OBJECT);
    if (!conf_node)
        return;
    str = database_get_data(conf_node, "enabled", RECDB_QSTRING);
    smtp_conf.enabled = (str != NULL) && enabled_string(str);
    smtp_conf.smtp_server = database_get_data(conf_node, "smtp_server", RECDB_QSTRING);
    if (!smtp_conf.smtp_server)
	log_module(MAIL_LOG, LOG_FATAL, "No mail smtp_server configuration setting.");
    str = database_get_data(conf_node, "smtp_service", RECDB_QSTRING);
    if (!str) str = "25";
    smtp_conf.smtp_service = str;
    smtp_conf.smtp_myname = database_get_data(conf_node, "smtp_myname", RECDB_QSTRING);
    /* myname defaults to [ip.v4.add.r] */
    smtp_conf.smtp_from = database_get_data(conf_node, "from_address", RECDB_QSTRING);
    if (!smtp_conf.smtp_from)
	log_module(MAIL_LOG, LOG_FATAL, "No mail from_address configuration setting.");
}

static void smtp_fill_name(char *namebuf, size_t buflen)
{
    char sockaddr[128];
    struct sockaddr *sa;
    socklen_t sa_len;
    int res;

    sa = (void*)sockaddr;
    sa_len = sizeof(sockaddr);
    res = getsockname(smtp_fd->fd, sa, &sa_len);
    if (res < 0) {
        log_module(MAIL_LOG, LOG_ERROR, "Unable to get SMTP socket name: %s", strerror(errno));
        namebuf[0] = '\0';
    }
    res = getnameinfo(sa, sa_len, namebuf, buflen, NULL, 0, NI_NUMERICHOST);
    if (res != 0) {
        log_module(MAIL_LOG, LOG_ERROR, "Unable to get text form of socket name: %s", strerror(errno));
    }
}

static void smtp_handle_greeting(const char *linebuf, short code)
{
    if (linebuf[3] == '-') {
	return;
    } else if (code >= 500) {
	log_module(MAIL_LOG, LOG_ERROR, "SMTP server error on connection: %s", linebuf);
        ioset_close(smtp_fd, 1);
    } else if (code >= 400) {
	log_module(MAIL_LOG, LOG_WARNING, "SMTP server error on connection: %s", linebuf);
        ioset_close(smtp_fd, 1);
    } else {
	if (smtp_conf.smtp_myname) {
            mail_println("EHLO %s", smtp_conf.smtp_myname);
        } else {
            char namebuf[64];
            smtp_fill_name(namebuf, sizeof(namebuf));
            mail_println("EHLO [%s]", namebuf);
        }
        smtp_state = SENT_EHLO;
    }
}

static void discard_mail(void)
{
    mail_queue_remove(&mail_queue, active_mail);
    free(active_mail);
    active_mail = NULL;
    mail_println("RSET");
    smtp_state = SENT_RSET;
}

static void smtp_idle_work(void)
{
    if ((smtp_state != IDLE) || (mail_queue.used == 0))
        return;
    active_mail = mail_queue.list[0];
    mail_println("MAIL FROM:<%s>", smtp_conf.smtp_from);
    smtp_state = SENT_MAIL_FROM;
}

static void smtp_handle_ehlo(const char *linebuf, short code)
{
    if (linebuf[3] == '-') {
	return;
    } else if (code >= 500) {
	log_module(MAIL_LOG, LOG_DEBUG, "Falling back from EHLO to HELO");
	if (smtp_conf.smtp_myname) {
            mail_println("HELO %s", smtp_conf.smtp_myname);
        } else {
            char namebuf[64];
            smtp_fill_name(namebuf, sizeof(namebuf));
            mail_println("HELO [%s]", namebuf);
        }
        smtp_state = SENT_HELO;
    } else if (code >= 400) {
        log_module(MAIL_LOG, LOG_WARNING, "SMTP server error after EHLO: %s", linebuf);
        ioset_close(smtp_fd, 1);
    } else {
        smtp_state = IDLE;
        smtp_idle_work();
    }
}

static void smtp_handle_helo(const char *linebuf, short code)
{
    if (linebuf[3] == '-') {
	return;
    } else if (code >= 500) {
	log_module(MAIL_LOG, LOG_WARNING, "SMTP server error after HELO: %s", linebuf);
        ioset_close(smtp_fd, 1);
    } else if (code >= 400) {
        log_module(MAIL_LOG, LOG_WARNING, "SMTP server error after HELO: %s", linebuf);
        ioset_close(smtp_fd, 1);
    } else {
        smtp_state = IDLE;
        smtp_idle_work();
    }
}

static void smtp_handle_mail_from(const char *linebuf, short code)
{
    assert(active_mail != NULL);
    if (linebuf[3] == '-') {
        return;
    } else if (code >= 500) {
        log_module(MAIL_LOG, LOG_ERROR, "SMTP server error after MAIL FROM: %s", linebuf);
        discard_mail();
    } else if (code >= 400) {
        log_module(MAIL_LOG, LOG_WARNING, "SMTP server error after MAIL FROM: %s", linebuf);
    } else {
        mail_println("RCPT TO:<%s>", active_mail->to_email);
        smtp_state = SENT_RCPT_TO;
    }
}

static void smtp_handle_rcpt_to(const char *linebuf, short code)
{
    assert(active_mail != NULL);
    if (linebuf[3] == '-') {
        return;
    } else if (code >= 500) {
        log_module(MAIL_LOG, LOG_ERROR, "SMTP server error after RCPT TO: %s", linebuf);
        discard_mail();
    } else if (code >= 400) {
        log_module(MAIL_LOG, LOG_WARNING, "SMTP server error after RCPT TO: %s", linebuf);
    } else {
        mail_println("DATA");
        smtp_state = SENT_DATA;
    }
}

static void smtp_handle_data(const char *linebuf, short code)
{
    assert(active_mail != NULL);
    if (linebuf[3] == '-') {
        return;
    } else if (code >= 500) {
        log_module(MAIL_LOG, LOG_ERROR, "SMTP server error after DATA: %s", linebuf);
        discard_mail();
    } else if (code >= 400) {
        log_module(MAIL_LOG, LOG_WARNING, "SMTP server error after DATA: %s", linebuf);
    } else {
        /* TODO: print the mail contents properly */
        smtp_state = SENT_BODY;
    }
}

static void smtp_handle_body(const char *linebuf, short code)
{
    assert(active_mail != NULL);
    if (linebuf[3] == '-') {
        return;
    } else if (code >= 500) {
        log_module(MAIL_LOG, LOG_ERROR, "SMTP server error after DATA: %s", linebuf);
        discard_mail();
    } else if (code >= 400) {
        log_module(MAIL_LOG, LOG_WARNING, "SMTP server error after DATA: %s", linebuf);
    } else {
        log_module(MAIL_LOG, LOG_INFO, "Sent mail to %s <%s>: %s", active_mail->to_name, active_mail->to_email, active_mail->subject);
        mail_queue_remove(&mail_queue, active_mail);
        free(active_mail);
        active_mail = NULL;
        smtp_state = IDLE;
        if (mail_queue.used > 0)
            smtp_idle_work();
    }
}

static void smtp_handle_rset(const char *linebuf, short code)
{
    assert(active_mail != NULL);
    smtp_state = IDLE;
    if (mail_queue.used > 0)
        smtp_idle_work();
    (void)linebuf; (void)code;
}

static void mail_readable(struct io_fd *fd)
{
    char linebuf[1024];
    int nbr;
    short code;

    assert(fd == smtp_fd);

    /* Try to read a line from the socket. */
    nbr = ioset_line_read(fd, linebuf, sizeof(linebuf));
    if (nbr < 0) {
        /* should only happen when there is no complete line */
        log_module(MAIL_LOG, LOG_DEBUG, "Unexpectedly got empty line in mail_readable().");
        return;
    } else if (nbr == 0) {
        log_module(MAIL_LOG, LOG_DEBUG, "Mail connection has been closed.");
        ioset_close(fd, 1);
        return;
    } else if ((size_t)nbr > sizeof(linebuf)) {
        log_module(MAIL_LOG, LOG_WARNING, "Got %u-byte line from server, truncating to 1024 bytes.", nbr);
        nbr = sizeof(linebuf);
        linebuf[nbr - 1] = '\0';
    }

    /* Trim CRLF at end of line */
    while (linebuf[nbr - 1] == '\r' || linebuf[nbr - 1] == '\n')
        linebuf[--nbr] = '\0';

    /* Check that the input line looks reasonable. */
    if (!isdigit(linebuf[0]) || !isdigit(linebuf[1]) || !isdigit(linebuf[2])
        || (linebuf[3] != ' ' && linebuf[3] != '-'))
    {
        log_module(MAIL_LOG, LOG_ERROR, "Got malformed SMTP line: %s", linebuf);
    }
    code = strtoul(linebuf, NULL, 10);

    /* Log it at debug level. */
    log_module(MAIL_LOG, LOG_REPLAY, "S[%s]: %s", smtp_state_names[smtp_state], linebuf);

    /* Dispatch line based on connection's current state. */
    switch (smtp_state)
    {
    case CLOSED:
	log_module(MAIL_LOG, LOG_ERROR, "Unexpectedly got readable callback when SMTP in CLOSED state.");
	break;
    case CONNECTING:
	log_module(MAIL_LOG, LOG_ERROR, "Unexpectedly got readable callback when SMTP in CONNECTING state.");
	break;
    case WAITING_GREETING:
	smtp_handle_greeting(linebuf, code);
	break;
    case SENT_EHLO:
	smtp_handle_ehlo(linebuf, code);
	break;
    case SENT_HELO:
	smtp_handle_helo(linebuf, code);
	break;
    case SENT_MAIL_FROM:
	smtp_handle_mail_from(linebuf, code);
	break;
    case SENT_RCPT_TO:
	smtp_handle_rcpt_to(linebuf, code);
	break;
    case SENT_DATA:
	smtp_handle_data(linebuf, code);
	break;
    case SENT_BODY:
	smtp_handle_body(linebuf, code);
	break;
    case SENT_RSET:
	smtp_handle_rset(linebuf, code);
	break;
    case IDLE:
    case SENT_QUIT:
	/* there's not much we can do to sensibly handle these cases */
	break;
    }
}

static void mail_destroyed(struct io_fd *fd)
{
    assert(smtp_fd == fd);
    smtp_state = CLOSED;
    smtp_fd = NULL;
    (void)fd; /* in case NDEBUG causes assert() to be empty */
}

static void mail_connected(struct io_fd *fd, int error)
{
    if (error)
    {
        log_module(MAIL_LOG, LOG_ERROR, "Unable to connect to SMTP server: %s", strerror(error));
        smtp_state = CLOSED;
        smtp_fd = NULL;
        return;
    }

    fd->line_reads = 1;
    fd->readable_cb = mail_readable;
    fd->destroy_cb = mail_destroyed;
    smtp_state = WAITING_GREETING;
}

void
mail_send(struct userNode *from, struct handle_info *to, const char *subject, const char *body, int first_time)
{
    struct pending_mail *new_mail;
    char *pos;
    size_t from_len;
    size_t to_name_len;
    size_t to_email_len;
    size_t subj_len;
    size_t body_len;

    /* Build a new pending_mail structure. */
    from_len = strlen(from->nick) + 1;
    to_name_len = strlen(to->handle) + 1;
    to_email_len = strlen(to->email_addr) + 1;
    subj_len = strlen(subject) + 1;
    body_len = strlen(body) + 1;
    new_mail = malloc(sizeof(*new_mail) + from_len + to_name_len + to_email_len + subj_len + body_len);
    pos = (char*)(new_mail + 1);
    new_mail->from = memcpy(pos, from->nick, from_len), pos += from_len;
    new_mail->to_name = memcpy(pos, to->handle, to_name_len), pos += to_name_len;
    new_mail->to_email = memcpy(pos, to->email_addr, to_email_len), pos += to_email_len;
    new_mail->subject = memcpy(pos, subject, subj_len), pos += subj_len;
    new_mail->body = memcpy(pos, body, body_len), pos += body_len;
    new_mail->first_time = first_time;

    /* Stick the structure onto the pending list and ask for a transmit. */
    mail_queue_append(&mail_queue, new_mail);

    /* Initiate a mail connection if necessary; otherwise, poke an idle one. */
    if (!smtp_fd) {
        smtp_state = CONNECTING;
        smtp_fd = ioset_connect(NULL, 0, smtp_conf.smtp_server, strtoul(smtp_conf.smtp_service, NULL, 10), 0, NULL, mail_connected);
    } else if (smtp_state == IDLE) {
        smtp_idle_work();
    }
}

void
mail_init(void)
{
    smtp_state = CLOSED;
    MAIL_LOG = log_register_type("mail", "file:mail.log");
    mail_common_init();
    conf_register_reload(mail_smtp_read_config);
}

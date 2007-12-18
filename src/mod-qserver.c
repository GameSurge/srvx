/* Direct Query Server module for srvx 1.x
 * Copyright 2006 Michael Poole <mdpoole@troilus.org>
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
#include "hash.h"
#include "ioset.h"
#include "log.h"
#include "modcmd.h"
#include "proto.h"

const char *qserver_module_deps[] = { NULL };

struct qserverClient {
    struct userNode *user;
    struct io_fd *fd;
    unsigned int id;
    unsigned int password_ok : 1;
};

static struct log_type *qserver_log;
static struct io_fd *qserver_listener;
static struct qserverClient **qserver_clients;
static dict_t qserver_dict;
static unsigned int qserver_nbots;

static struct {
    const char *password;
} conf;

static void
qserver_privmsg(struct userNode *user, struct userNode *target, const char *text, UNUSED_ARG(int server_qualified))
{
    struct qserverClient *client;

    client = dict_find(qserver_dict, target->nick, NULL);
    assert(client->user == target);
    ioset_printf(client->fd, "%s P :%s\n", user->nick, text);
}

static void
qserver_notice(struct userNode *user, struct userNode *target, const char *text, UNUSED_ARG(int server_qualified))
{
    struct qserverClient *client;

    client = dict_find(qserver_dict, target->nick, NULL);
    assert(client->user == target);
    ioset_printf(client->fd, "%s N :%s\n", user->nick, text);
}

static void
qserver_readable(struct io_fd *fd)
{
    struct qserverClient *client;
    struct service *service;
    char *argv[MAXNUMPARAMS];
    unsigned int argc;
    size_t len;
    int res;
    char tmpline[MAXLEN];

    client = fd->data;
    assert(client->fd == fd);
    res = ioset_line_read(fd, tmpline, sizeof(tmpline));
    if (res < 0)
        return;
    else if (res == 0) {
        ioset_close(fd, 1);
        return;
    }
    len = strlen(tmpline);
    while (tmpline[len - 1] == '\r' || tmpline[len - 1] == '\n')
        tmpline[--len] = '\0';
    argc = split_line(tmpline, false, ArrayLength(argv), argv);
    if (argc < 3) {
        ioset_printf(fd, "MISSING_ARGS\n");
        return;
    }
    if (!strcmp(argv[1], "PASS")
        && conf.password
        && !strcmp(argv[2], conf.password)) {
        client->password_ok = 1;
    } else if ((client->password_ok || !conf.password)
               && (service = service_find(argv[1])) != NULL) {
        ioset_printf(fd, "%s S\n", argv[0]);
        svccmd_invoke_argv(client->user, service, NULL, argc - 2, argv + 2, 1);
        ioset_printf(fd, "%s E\n", argv[0]);
    } else {
        ioset_printf(fd, "%s X %s\n", argv[0], argv[1]);
    }
}

static void
qserver_destroy_fd(struct io_fd *fd)
{
    struct qserverClient *client;

    client = fd->data;
    assert(client->fd == fd);
    dict_remove(qserver_dict, client->user->nick);
    DelUser(client->user, NULL, 0, "client disconnected");
    qserver_clients[client->id] = NULL;
    free(client);
}

static void
qserver_accept(UNUSED_ARG(struct io_fd *listener), struct io_fd *fd)
{
    struct qserverClient *client;
    struct sockaddr_storage ss;
    socklen_t sa_len;
    unsigned int ii;
    unsigned int jj;
    int res;
    char nick[NICKLEN+1];
    char host[HOSTLEN+1];
    char ip[HOSTLEN+1];

    client = calloc(1, sizeof(*client));
    fd->data = client;
    fd->line_reads = 1;
    fd->readable_cb = qserver_readable;
    fd->destroy_cb = qserver_destroy_fd;

    for (ii = 0; ii < qserver_nbots; ++ii)
        if (qserver_clients[ii] == NULL)
            break;
    if (ii == qserver_nbots) {
        qserver_nbots += 8;
        qserver_clients = realloc(qserver_clients, qserver_nbots * sizeof(qserver_clients[0]));
        for (jj = ii; jj < qserver_nbots; ++jj)
            qserver_clients[jj] = NULL;
    }
    client->id = ii;
    client->fd = fd;
    qserver_clients[client->id] = client;
    snprintf(nick, sizeof(nick), " QServ%04d", client->id);
    safestrncpy(host, "srvx.dummy.user", sizeof(host));
    safestrncpy(ip, "0.0.0.0", sizeof(ip));
    sa_len = sizeof(ss);
    res = getpeername(fd->fd, (struct sockaddr*)&ss, &sa_len);
    if (res == 0) {
        getnameinfo((struct sockaddr*)&ss, sa_len, ip, sizeof(host), NULL, 0, NI_NUMERICHOST);
        if (getnameinfo((struct sockaddr*)&ss, sa_len, host, sizeof(host), NULL, 0, 0) != 0)
            safestrncpy(host, ip, sizeof(host));
    }
    client->user = AddLocalUser(nick, nick+1, host, "qserver dummy user", "*+oi");
    irc_pton(&client->user->ip, NULL, ip);
    dict_insert(qserver_dict, client->user->nick, client);

    reg_privmsg_func(client->user, qserver_privmsg);
    reg_notice_func(client->user, qserver_notice);
}

static void
qserver_conf_read(void)
{
    struct addrinfo hints;
    struct addrinfo *ai;
    dict_t node;
    const char *str1;
    const char *str2;
    int res;

    ioset_close(qserver_listener, 1);
    qserver_listener = NULL;
    node = conf_get_data("modules/qserver", RECDB_OBJECT);
    if (!node)
        return;
    str1 = database_get_data(node, "bind_address", RECDB_QSTRING);
    if (!str1)
        str1 = database_get_data(node, "address", RECDB_QSTRING);
    str2 = database_get_data(node, "port", RECDB_QSTRING);
    if (!str2)
        return;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;
    res = getaddrinfo(str1, str2, &hints, &ai);
    if (res) {
        log_module(qserver_log, LOG_ERROR, "Unable to find address [%s]:%s: %s", str1 ? str1 : "", str2, gai_strerror(res));
    } else if (!(qserver_listener = ioset_listen(ai->ai_addr, ai->ai_addrlen, NULL, qserver_accept))) {
        log_module(qserver_log, LOG_ERROR, "Unable to listen on [%s]:%s", str1 ? str1 : "", str2);
    }
    conf.password = database_get_data(node, "password", RECDB_QSTRING);
    freeaddrinfo(ai);
}

void
qserver_cleanup(void)
{
    unsigned int ii;

    ioset_close(qserver_listener, 1);
    for (ii = 0; ii < qserver_nbots; ++ii)
        if (qserver_clients[ii])
            DelUser(qserver_clients[ii]->user, NULL, 0, "module finalizing");
    dict_delete(qserver_dict);
}

int
qserver_init(void)
{
    qserver_log = log_register_type("QServer", "file:qserver.log");
    conf_register_reload(qserver_conf_read);
    qserver_dict = dict_new();
    reg_exit_func(qserver_cleanup);
    return 1;
}

int
qserver_finalize(void)
{
    return 1;
}

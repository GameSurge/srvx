/* global.c - Global notice service
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

#include "conf.h"
#include "global.h"
#include "modcmd.h"
#include "nickserv.h"
#include "saxdb.h"
#include "timeq.h"

#define GLOBAL_CONF_NAME	"services/global"

#define GLOBAL_DB		"global.db"
#define GLOBAL_TEMP_DB		"global.db.new"

/* Global options */
#define KEY_DB_BACKUP_FREQ	"db_backup_freq"
#define KEY_ANNOUNCEMENTS_DEFAULT "announcements_default"
#define KEY_NICK		"nick"

/* Message data */
#define KEY_FLAGS		"flags"
#define KEY_POSTED		"posted"
#define KEY_DURATION		"duration"
#define KEY_FROM		"from"
#define KEY_MESSAGE		"message"

/* Clarification: Notices are immediate, they are sent to matching users
   _once_, then forgotten. Messages are stored in Global's database and
   continually sent to users as they match the target specification until
   they are deleted. */
static const struct message_entry msgtab[] = {
    { "GMSG_INVALID_TARGET", "$b%s$b is an invalid message target." },
    { "GMSG_MESSAGE_REQUIRED", "You $bmust$b provide a message to send." },
    { "GMSG_MESSAGE_SENT", "Message to $b%s$b sent." },
    { "GMSG_MESSAGE_ADDED", "Message to $b%s$b with ID %ld added." },
    { "GMSG_MESSAGE_DELETED", "Message $b%s$b deleted." },
    { "GMSG_ID_INVALID", "$b%s$b is an invalid message ID." },
    { "GMSG_MESSAGE_COUNT", "$b%d$b messages found." },
    { "GMSG_NO_MESSAGES", "There are no messages for you." },
    { "GMSG_NOTICE_SOURCE", "[$b%s$b] Notice from %s:" },
    { "GMSG_MESSAGE_SOURCE", "[$b%s$b] Notice from %s, posted %s:" },
    { "GMSG_MOTD_HEADER", "$b------------- MESSAGE(S) OF THE DAY --------------$b" },
    { "GMSG_MOTD_FOOTER", "$b---------- END OF MESSAGE(S) OF THE DAY ----------$b" },
    { NULL, NULL }
};

#define GLOBAL_SYNTAX()   svccmd_send_help(user, global, cmd)
#define GLOBAL_FUNC(NAME) MODCMD_FUNC(NAME)

struct userNode *global;

static struct module *global_module;
static struct service *global_service;
static struct globalMessage *messageList;
static long messageCount;
static time_t last_max_alert;
static struct log_type *G_LOG;

static struct
{
    unsigned long db_backup_frequency;
    unsigned int announcements_default : 1;
} global_conf;

#define global_notice(target, format...) send_message(target , global , ## format)

void message_expire(void *data);

static struct globalMessage*
message_add(long flags, time_t posted, unsigned long duration, char *from, const char *msg)
{
    struct globalMessage *message;

    message = malloc(sizeof(struct globalMessage));

    if(!message)
    {
	return NULL;
    }

    message->id = messageCount++;
    message->flags = flags;
    message->posted = posted;
    message->duration = duration;
    message->from = strdup(from);
    message->message = strdup(msg);

    if(messageList)
    {
	messageList->prev = message;
    }

    message->prev = NULL;
    message->next = messageList;

    messageList = message;

    if(duration)
    {
	timeq_add(now + duration, message_expire, message);
    }

    return message;
}

static void
message_del(struct globalMessage *message)
{
    if(message->duration)
    {
	timeq_del(0, NULL, message, TIMEQ_IGNORE_FUNC | TIMEQ_IGNORE_WHEN);
    }

    if(message->prev) message->prev->next = message->next;
    else messageList = message->next;

    if(message->next) message->next->prev = message->prev;

    free(message->from);
    free(message->message);
    free(message);
}

void message_expire(void *data)
{
    struct globalMessage *message = data;

    message->duration = 0;
    message_del(message);
}

static struct globalMessage*
message_create(struct userNode *user, unsigned int argc, char *argv[])
{
    unsigned long duration = 0;
    char *text = NULL;
    long flags = 0;
    unsigned int i;

    for(i = 0; i < argc; i++)
    {
	if((i + 1) > argc)
	{
	    global_notice(user, "MSG_MISSING_PARAMS", argv[argc]);
	    return NULL;
	}

	if(!irccasecmp(argv[i], "text"))
	{
	    i++;
	    text = unsplit_string(argv + i, argc - i, NULL);
	    break;
	} else if (!irccasecmp(argv[i], "sourceless")) {
	    i++;
	    flags |= MESSAGE_OPTION_SOURCELESS;
	} else if (!irccasecmp(argv[i], "target")) {
	    i++;

	    if(!irccasecmp(argv[i], "all")) {
		flags |= MESSAGE_RECIPIENT_ALL;
	    } else if(!irccasecmp(argv[i], "users")) {
		flags |= MESSAGE_RECIPIENT_LUSERS;
	    } else if(!irccasecmp(argv[i], "helpers")) {
		flags |= MESSAGE_RECIPIENT_HELPERS;
	    } else if(!irccasecmp(argv[i], "opers")) {
		flags |= MESSAGE_RECIPIENT_OPERS;
	    } else if(!irccasecmp(argv[i], "staff") || !irccasecmp(argv[i], "privileged")) {
		flags |= MESSAGE_RECIPIENT_STAFF;
	    } else if(!irccasecmp(argv[i], "channels")) {
		flags |= MESSAGE_RECIPIENT_CHANNELS;
            } else if(!irccasecmp(argv[i], "announcement") || !irccasecmp(argv[i], "announce")) {
                flags |= MESSAGE_RECIPIENT_ANNOUNCE;
	    } else {
		global_notice(user, "GMSG_INVALID_TARGET", argv[i]);
		return NULL;
	    }
	} else if (irccasecmp(argv[i], "duration") == 0) {
	    duration = ParseInterval(argv[++i]);
	} else {
	    global_notice(user, "MSG_INVALID_CRITERIA", argv[i]);
	    return NULL;
	}
    }

    if(!flags)
    {
	flags = MESSAGE_RECIPIENT_LUSERS;
    }

    if(!text) {
	global_notice(user, "GMSG_MESSAGE_REQUIRED");
	return NULL;
    }

    return message_add(flags, now, duration, user->handle_info->handle, text);
}

static const char *
messageType(const struct globalMessage *message)
{
    if((message->flags & MESSAGE_RECIPIENT_ALL) == MESSAGE_RECIPIENT_ALL)
    {
	return "all";
    }
    if((message->flags & MESSAGE_RECIPIENT_STAFF) == MESSAGE_RECIPIENT_STAFF)
    {
	return "staff";
    }
    else if(message->flags & MESSAGE_RECIPIENT_ANNOUNCE)
    {
        return "announcement";
    }
    else if(message->flags & MESSAGE_RECIPIENT_OPERS)
    {
	return "opers";
    }
    else if(message->flags & MESSAGE_RECIPIENT_HELPERS)
    {
	return "helpers";
    }
    else if(message->flags & MESSAGE_RECIPIENT_LUSERS)
    {
	return "users";
    }
    else
    {
	return "channels";
    }
}

static void
notice_target(const char *target, struct globalMessage *message)
{
    if(!(message->flags & MESSAGE_OPTION_SOURCELESS))
    {
	if(message->flags & MESSAGE_OPTION_IMMEDIATE)
	{
	    send_target_message(0, target, global, "GMSG_NOTICE_SOURCE", messageType(message), message->from);
	}
	else
	{
	    char posted[24];
	    struct tm tm;

	    localtime_r(&message->posted, &tm);
	    strftime(posted, sizeof(posted), "%I:%M %p, %m/%d/%Y", &tm);
	    send_target_message(0, target, global, "GMSG_MESSAGE_SOURCE", messageType(message), message->from, posted);
	}
    }

    send_target_message(4, target, global, "%s", message->message);
}

static int
notice_channel(const char *key, void *data, void *extra)
{
    struct chanNode *channel = data;
    /* It should be safe to assume channel is not NULL. */
    if(channel->channel_info)
 	notice_target(key, extra);
    return 0;
}

static void
message_send(struct globalMessage *message)
{
    struct userNode *user;
    unsigned long n;
    dict_iterator_t it;

    if(message->flags & MESSAGE_RECIPIENT_CHANNELS)
    {
	dict_foreach(channels, notice_channel, message);
    }

    if(message->flags & MESSAGE_RECIPIENT_LUSERS)
    {
	notice_target("$*", message);
	return;
    }

    if(message->flags & MESSAGE_RECIPIENT_ANNOUNCE)
    {
        char announce;

        for (it = dict_first(clients); it; it = iter_next(it)) {
            user = iter_data(it);
            if (user->uplink == self) continue;
            announce = user->handle_info ? user->handle_info->announcements : '?';
            if (announce == 'n') continue;
            if ((announce == '?') && !global_conf.announcements_default) continue;
            notice_target(user->nick, message);
        }
    }

    if(message->flags & MESSAGE_RECIPIENT_OPERS)
    {
	for(n = 0; n < curr_opers.used; n++)
	{
	    user = curr_opers.list[n];

	    if(user->uplink != self)
	    {
		notice_target(user->nick, message);
	    }
	}
    }

    if(message->flags & MESSAGE_RECIPIENT_HELPERS)
    {
	for(n = 0; n < curr_helpers.used; n++)
	{
	    user = curr_helpers.list[n];
            if (IsOper(user))
                continue;
	    notice_target(user->nick, message);
	}
    }
}

void
global_message(long targets, char *text)
{
    struct globalMessage *message;

    if(!targets || !global)
	return;

    message = message_add(targets | MESSAGE_OPTION_SOURCELESS, now, 0, "", text);
    if(!message)
	return;

    message_send(message);
    message_del(message);
}

static GLOBAL_FUNC(cmd_notice)
{
    struct globalMessage *message = NULL;
    const char *recipient = NULL, *text;
    long target = 0;

    assert(argc >= 3);
    if(!irccasecmp(argv[1], "all")) {
	target = MESSAGE_RECIPIENT_ALL;
    } else if(!irccasecmp(argv[1], "users")) {
	target = MESSAGE_RECIPIENT_LUSERS;
    } else if(!irccasecmp(argv[1], "helpers")) {
	target = MESSAGE_RECIPIENT_HELPERS;
    } else if(!irccasecmp(argv[1], "opers")) {
	target = MESSAGE_RECIPIENT_OPERS;
    } else if(!irccasecmp(argv[1], "staff") || !irccasecmp(argv[1], "privileged")) {
	target |= MESSAGE_RECIPIENT_HELPERS | MESSAGE_RECIPIENT_OPERS;
    } else if(!irccasecmp(argv[1], "announcement") || !irccasecmp(argv[1], "announce")) {
        target |= MESSAGE_RECIPIENT_ANNOUNCE;
    } else if(!irccasecmp(argv[1], "channels")) {
	target = MESSAGE_RECIPIENT_CHANNELS;
    } else {
	global_notice(user, "GMSG_INVALID_TARGET", argv[1]);
	return 0;
    }

    text = unsplit_string(argv + 2, argc - 2, NULL);
    message = message_add(target | MESSAGE_OPTION_IMMEDIATE, now, 0, user->handle_info->handle, text);

    if(!message)
    {
	return 0;
    }

    recipient = messageType(message);

    message_send(message);
    message_del(message);

    global_notice(user, "GMSG_MESSAGE_SENT", recipient);
    return 1;
}

static GLOBAL_FUNC(cmd_message)
{
    struct globalMessage *message = NULL;
    const char *recipient = NULL;

    assert(argc >= 3);
    message = message_create(user, argc - 1, argv + 1);
    if(!message)
        return 0;
    recipient = messageType(message);
    global_notice(user, "GMSG_MESSAGE_ADDED", recipient, message->id);
    return 1;
}

static GLOBAL_FUNC(cmd_list)
{
    struct globalMessage *message;
    struct helpfile_table table;
    unsigned int length, nn;

    if(!messageList)
    {
	global_notice(user, "GMSG_NO_MESSAGES");
        return 1;
    }

    for(nn=0, message = messageList; message; nn++, message=message->next) ;
    table.length = nn+1;
    table.width = 5;
    table.flags = TABLE_NO_FREE;
    table.contents = calloc(table.length, sizeof(char**));
    table.contents[0] = calloc(table.width, sizeof(char*));
    table.contents[0][0] = "ID";
    table.contents[0][1] = "Target";
    table.contents[0][2] = "Expires";
    table.contents[0][3] = "From";
    table.contents[0][4] = "Message";

    for(nn=1, message = messageList; message; nn++, message = message->next)
    {
        char buffer[64];

        table.contents[nn] = calloc(table.width, sizeof(char*));
        snprintf(buffer, sizeof(buffer), "%lu", message->id);
        table.contents[nn][0] = strdup(buffer);
        table.contents[nn][1] = messageType(message);
        if(message->duration)
            intervalString(buffer, message->posted + message->duration - now, user->handle_info);
        else
            strcpy(buffer, "Never.");
        table.contents[nn][2] = strdup(buffer);
        table.contents[nn][3] = message->from;
	length = strlen(message->message);
	safestrncpy(buffer, message->message, sizeof(buffer));
	if(length > (sizeof(buffer) - 4))
	{
	    buffer[sizeof(buffer) - 1] = 0;
	    buffer[sizeof(buffer) - 2] = buffer[sizeof(buffer) - 3] = buffer[sizeof(buffer) - 4] = '.';
	}
        table.contents[nn][4] = strdup(buffer);
    }
    table_send(global, user->nick, 0, NULL, table);
    for (nn=1; nn<table.length; nn++)
    {
        free((char*)table.contents[nn][0]);
        free((char*)table.contents[nn][2]);
        free((char*)table.contents[nn][4]);
        free(table.contents[nn]);
    }
    free(table.contents[0]);
    free(table.contents);

    return 1;
}

static GLOBAL_FUNC(cmd_remove)
{
    struct globalMessage *message = NULL;
    unsigned long id;

    assert(argc >= 2);
    id = strtoul(argv[1], NULL, 0);

    for(message = messageList; message; message = message->next)
    {
	if(message->id == id)
	{
	    message_del(message);
	    global_notice(user, "GMSG_MESSAGE_DELETED", argv[1]);
	    return 1;
	}
    }

    global_notice(user, "GMSG_ID_INVALID", argv[1]);
    return 0;
}

static unsigned int
send_messages(struct userNode *user, long mask, int obstreperize)
{
    struct globalMessage *message = messageList;
    unsigned int count = 0;

    while(message)
    {
	if(message->flags & mask)
	{
            if (obstreperize && !count)
                send_target_message(0, user->nick, global, "GMSG_MOTD_HEADER");
	    notice_target(user->nick, message);
	    count++;
	}

	message = message->next;
    }
    if (obstreperize && count)
        send_target_message(0, user->nick, global, "GMSG_MOTD_FOOTER");
    return count;
}

static GLOBAL_FUNC(cmd_messages)
{
    long mask = MESSAGE_RECIPIENT_LUSERS | MESSAGE_RECIPIENT_CHANNELS;
    unsigned int count;

    if(IsOper(user))
	mask |= MESSAGE_RECIPIENT_OPERS;

    if(IsHelper(user))
	mask |= MESSAGE_RECIPIENT_HELPERS;

    count = send_messages(user, mask, 0);
    if(count)
	global_notice(user, "GMSG_MESSAGE_COUNT", count);
    else
	global_notice(user, "GMSG_NO_MESSAGES");

    return 1;
}

static int
global_process_user(struct userNode *user)
{
    if(IsLocal(user) || self->uplink->burst || user->uplink->burst)
        return 0;
    send_messages(user, MESSAGE_RECIPIENT_LUSERS, 1);

    /* only alert on new usercount if the record was broken in the last
     * 30 seconds, and no alert had been sent in that time.
     */
    if((now - max_clients_time) <= 30 && (now - last_max_alert) > 30)
    {
	char *message;
	message = alloca(36);
	sprintf(message, "New user count record: %d", max_clients);
	global_message(MESSAGE_RECIPIENT_OPERS, message);
	last_max_alert = now;
    }

    return 0;
}

static void
global_process_auth(struct userNode *user, UNUSED_ARG(struct handle_info *old_handle))
{
    if(IsHelper(user))
	send_messages(user, MESSAGE_RECIPIENT_HELPERS, 0);
}

static void
global_process_oper(struct userNode *user)
{
    if(user->uplink->burst)
        return;
    send_messages(user, MESSAGE_RECIPIENT_OPERS, 0);
}

static void
global_conf_read(void)
{
    dict_t conf_node;
    const char *str;

    if (!(conf_node = conf_get_data(GLOBAL_CONF_NAME, RECDB_OBJECT))) {
	log_module(G_LOG, LOG_ERROR, "config node `%s' is missing or has wrong type.", GLOBAL_CONF_NAME);
	return;
    }

    str = database_get_data(conf_node, KEY_DB_BACKUP_FREQ, RECDB_QSTRING);
    global_conf.db_backup_frequency = str ? ParseInterval(str) : 7200;
    str = database_get_data(conf_node, KEY_ANNOUNCEMENTS_DEFAULT, RECDB_QSTRING);
    global_conf.announcements_default = str ? enabled_string(str) : 1;

    str = database_get_data(conf_node, KEY_NICK, RECDB_QSTRING);
    if(global && str)
        NickChange(global, str, 0);
}

static int
global_saxdb_read(struct dict *db)
{
    struct record_data *hir;
    time_t posted;
    long flags;
    unsigned long duration;
    char *str, *from, *message;
    dict_iterator_t it;

    for(it=dict_first(db); it; it=iter_next(it))
    {
        hir = iter_data(it);
	if(hir->type != RECDB_OBJECT)
	{
	    log_module(G_LOG, LOG_WARNING, "Unexpected rectype %d for %s.", hir->type, iter_key(it));
            continue;
	}

        str = database_get_data(hir->d.object, KEY_FLAGS, RECDB_QSTRING);
        flags = str ? strtoul(str, NULL, 0) : 0;

        str = database_get_data(hir->d.object, KEY_POSTED, RECDB_QSTRING);
        posted = str ? strtoul(str, NULL, 0) : 0;

        str = database_get_data(hir->d.object, KEY_DURATION, RECDB_QSTRING);
        duration = str ? strtoul(str, NULL, 0) : 0;

        from = database_get_data(hir->d.object, KEY_FROM, RECDB_QSTRING);
        message = database_get_data(hir->d.object, KEY_MESSAGE, RECDB_QSTRING);

	message_add(flags, posted, duration, from, message);
    }
    return 0;
}

static int
global_saxdb_write(struct saxdb_context *ctx)
{
    struct globalMessage *message;
    char str[16];

    for(message = messageList; message; message = message->next) {
        snprintf(str, sizeof(str), "%li", message->id);
        saxdb_start_record(ctx, str, 0);
        saxdb_write_int(ctx, KEY_FLAGS, message->flags);
        saxdb_write_int(ctx, KEY_POSTED, message->posted);
        saxdb_write_int(ctx, KEY_DURATION, message->duration);
        saxdb_write_string(ctx, KEY_FROM, message->from);
        saxdb_write_string(ctx, KEY_MESSAGE, message->message);
        saxdb_end_record(ctx);
    }
    return 0;
}

static void
global_db_cleanup(void)
{
    while(messageList)
        message_del(messageList);
}

void
init_global(const char *nick)
{
    G_LOG = log_register_type("Global", "file:global.log");
    reg_new_user_func(global_process_user);
    reg_auth_func(global_process_auth);
    reg_oper_func(global_process_oper);

    conf_register_reload(global_conf_read);

    global_module = module_register("Global", G_LOG, "global.help", NULL);
    modcmd_register(global_module, "LIST", cmd_list, 1, 0, "flags", "+oper", NULL);
    modcmd_register(global_module, "MESSAGE", cmd_message, 3, MODCMD_REQUIRE_AUTHED, "flags", "+oper", NULL);
    modcmd_register(global_module, "MESSAGES", cmd_messages, 1, 0, NULL);
    modcmd_register(global_module, "NOTICE", cmd_notice, 3, MODCMD_REQUIRE_AUTHED, "flags", "+oper", NULL);
    modcmd_register(global_module, "REMOVE", cmd_remove, 2, MODCMD_REQUIRE_AUTHED, "flags", "+oper", NULL);

    if(nick)
    {
        global = AddService(nick, "Global Services", NULL);
        global_service = service_register(global);
    }
    saxdb_register("Global", global_saxdb_read, global_saxdb_write);
    reg_exit_func(global_db_cleanup);
    message_register_table(msgtab);
}

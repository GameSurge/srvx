/* modcmd.c - Generalized module command support
 * Copyright 2002-2004 srvx Development Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.  Important limitations are
 * listed in the COPYING file that accompanies this software.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, email srvx-maintainers@srvx.net.
 */

#include "arch-version.h"
#include "chanserv.h"
#include "conf.h"
#include "modcmd.h"
#include "saxdb.h"

struct pending_template {
    struct svccmd *cmd;
    char *base;
    struct pending_template *next;
};

static struct dict *modules;
static struct dict *services;
static struct pending_template *pending_templates;
static struct module *modcmd_module;
static struct modcmd *bind_command, *help_command, *version_command;
static const struct message_entry msgtab[] = {
    { "MCMSG_VERSION", "$b"PACKAGE_STRING"$b ("CODENAME"), Built: " __DATE__ ", " __TIME__"." },
    { "MCMSG_BARE_FLAG", "Flag %.*s must be preceeded by a + or -." },
    { "MCMSG_UNKNOWN_FLAG", "Unknown module flag %.*s." },
    { "MCMSG_BAD_OPSERV_LEVEL", "Invalid $O access level %s." },
    { "MCMSG_BAD_CHANSERV_LEVEL", "Invalid $C access level %s." },
    { "MCMSG_LEVEL_TOO_LOW", "You cannot set the access requirements for %s (your level is too low.)" },
    { "MCMSG_LEVEL_TOO_HIGH", "You cannot set the access requirements to %s (that is too high)." },
    { "MCMSG_BAD_OPTION", "Unknown option %s." },
    { "MCMSG_MUST_QUALIFY", "You $bMUST$b \"/msg %s@$s %s\" (not just /msg %s)." },
    { "MCMSG_ACCOUNT_SUSPENDED", "Your account has been suspended." },
    { "MCMSG_CHAN_NOT_REGISTERED", "%s has not been registered with $C." },
    { "MCMSG_CHAN_SUSPENDED", "$b$C$b access to $b%s$b has been temporarily suspended (%s)." },
    { "MCMSG_NO_CHANNEL_ACCESS", "You lack access to %s." },
    { "MCMSG_LOW_CHANNEL_ACCESS", "You lack sufficient access in %s to use this command." },
    { "MCMSG_REQUIRES_JOINABLE", "You must be in %s (or on its userlist) to use this command." },
    { "MCMSG_MUST_BE_HELPING", "You must have security override (helping mode) on to use this command." },
    { "MCMSG_MISSING_COMMAND", "You must specify a command as well as a channel." },
    { "MCMSG_NO_CHANNEL_BEFORE", "You may not give a channel name before this command." },
    { "MCMSG_NO_PLUS_CHANNEL", "You may not use a +channel with this command." },
    { "MCMSG_COMMAND_ALIASES", "%s is an alias for: %s" },
    { "MCMSG_COMMAND_BINDING", "%s is a binding of: %s" },
    { "MCMSG_ALIAS_ERROR", "Error in alias expansion for %s; check the error log for details." },
    { "MCMSG_INTERNAL_COMMAND", "$b%s$b is an internal command and cannot be called directly; please check command bindings." },
    { "MCMSG_UNKNOWN_MODULE", "Unknown module %s." },
    { "MCMSG_UNKNOWN_SERVICE", "Unknown service %s." },
    { "MCMSG_ALREADY_BOUND", "%s already has a command bound as %s." },
    { "MCMSG_UNKNOWN_COMMAND_2", "Unknown command name %s (relative to service %s)." },
    { "MCMSG_COMMAND_MODIFIED", "Option $b%s$b for $b%s$b has been set." },
    { "MCMSG_INSPECTION_REFUSED", "You do not have access to inspect command %s." },
    { "MCMSG_CANNOT_DOUBLE_ALIAS", "You cannot bind to a complex (argument-carrying) bind." },
    { "MCMSG_BAD_ALIAS_ARGUMENT", "Invalid alias argument $b%s$b." },
    { "MCMSG_COMMAND_BOUND", "New command %s bound to %s." },
    { "MCMSG_MODULE_BOUND", "Bound %d commands from %s to %s." },
    { "MCMSG_NO_COMMAND_BOUND", "%s has nothing bound as command %s." },
    { "MCMSG_UNBIND_PROHIBITED", "It wouldn't be very much fun to unbind the last %s command, now would it?" },
    { "MCMSG_COMMAND_UNBOUND", "Unbound command %s from %s." },
    { "MCMSG_HELPFILE_UNBOUND", "Since that was the last command from module %s on the service, the helpfile for %s was removed." },
    { "MCMSG_NO_HELPFILE", "Module %s does not have a help file." },
    { "MCMSG_HELPFILE_ERROR", "Syntax error reading %s; help contents not changed." },
    { "MCMSG_HELPFILE_READ", "Read %s help database in "FMT_TIME_T".%03lu seconds." },
    { "MCMSG_COMMAND_TIME", "Command $b%s$b finished in "FMT_TIME_T".%06lu seconds." },
    { "MCMSG_NEED_OPSERV_LEVEL", "You must have $O access of at least $b%u$b." },
    { "MCMSG_NEED_CHANSERV_LEVEL", "You must have $C access of at least $b%u$b in the channel." },
    { "MCMSG_NEED_ACCOUNT_FLAGS", "You must have account flags $b%s$b." },
    { "MCMSG_NEED_NOTHING", "Anyone may use the $b%s$b command." },
    { "MCMSG_NEED_STAFF_ACCESS", "You must be network staff." },
    { "MCMSG_NEED_STAFF_OPER", "You must be an IRC operator." },
    { "MCMSG_NEED_STAFF_NETHELPER", "You must be a network helper." },
    { "MCMSG_NEED_STAFF_NETHELPER_OR_OPER", "You must be a network helper or IRC operator." },
    { "MCMSG_NEED_STAFF_SHELPER", "You must be a support helper." },
    { "MCMSG_NEED_STAFF_SHELPER_OR_OPER", "You must be a support helper or IRC operator." },
    { "MCMSG_NEED_STAFF_HELPER", "You must be a network or support helper." },
    { "MCMSG_NEED_JOINABLE", "The channel must be open or you must be in the channel or on its userlist." },
    { "MCMSG_NEED_CHANUSER_CSUSPENDABLE", "You must be on the channel's userlist, and the channel can be suspended." },
    { "MCMSG_NEED_CHANUSER", "You must be on the channel's userlist." },
    { "MCMSG_NEED_REGCHAN", "You must specify a channel registered with $C." },
    { "MCMSG_NEED_CHANNEL", "You must specify a channel that exists." },
    { "MCMSG_NEED_AUTHED", "You must be authenticated with $N." },
    { "MCMSG_IS_TOY", "$b%s$b is a toy command." },
    { "MCMSG_END_REQUIREMENTS", "End of requirements for $b%s$b." },
    { "MCMSG_ALREADY_HELPING", "You already have security override enabled." },
    { "MCMSG_ALREADY_NOT_HELPING", "You already have security override disabled." },
    { "MCMSG_NOW_HELPING", "Security override has been enabled." },
    { "MCMSG_NOW_NOT_HELPING", "Security override has been disabled." },
    { "MCMSG_JOINER_CHOICES", "Subcommands of %s: %s" },
    { "MCMSG_MODULE_INFO", "Commands exported by module $b%s$b:" },
    { "MCMSG_SERVICE_INFO", "Commands bound to service $b%s$b:" },
    { "MCMSG_TOYS_DISABLED", "Toys are disabled in %s." },
    { "MCMSG_PUBLIC_DENY", "Public commands in $b%s$b are restricted." },
    { "MCMSG_HELPFILE_SEQUENCE", "Help priority %d: %s" },
    { "MCMSG_HELPFILE_SEQUENCE_SET", "Set helpfile priority sequence for %s." },
    { "MCMSG_BAD_SERVICE_NICK", "$b%s$b is an invalid nickname." },
    { "MCMSG_ALREADY_SERVICE", "$b%s$b is already a service." },
    { "MCMSG_NEW_SERVICE", "Added new service bot $b%s$b." },
    { "MCMSG_SERVICE_RENAMED", "Service renamed to $b%s$b." },
    { "MCMSG_NO_TRIGGER", "$b%s$b does not have an in-channel trigger." },
    { "MCMSG_REMOVED_TRIGGER", "Removed trigger from $b%s$b." },
    { "MCMSG_DUPLICATE_TRIGGER", "$b%s$b already uses trigger $b%c$b." },
    { "MCMSG_CURRENT_TRIGGER", "Trigger for $b%s$b is $b%c$b." },
    { "MCMSG_NEW_TRIGGER", "Changed trigger for $b%s$b to $b%c$b." },
    { "MCMSG_SERVICE_PRIVILEGED", "Service $b%s$b privileged: $b%s$b." },
    { "MCMSG_SERVICE_REMOVED", "Service $b%s$b has been deleted." },
    { "MCMSG_FILE_NOT_OPENED", "Unable to open file $b%s$b for writing." },
    { "MCMSG_MESSAGES_DUMPED", "Messages written to $b%s$b." },
    { NULL, NULL }
};
struct userData *_GetChannelUser(struct chanData *channel, struct handle_info *handle, int override, int allow_suspended);

#define ACTION_ALLOW     1
#define ACTION_OVERRIDE  2
#define ACTION_NOCHANNEL 4
#define ACTION_STAFF     8

#define RESOLVE_DEPTH    4

static struct modcmd_flag {
    const char *name;
    unsigned int flag;
} flags[] = {
    { "acceptchan", MODCMD_ACCEPT_CHANNEL },
    { "acceptpluschan", MODCMD_ACCEPT_PCHANNEL },
    { "authed", MODCMD_REQUIRE_AUTHED },
    { "channel", MODCMD_REQUIRE_CHANNEL },
    { "chanuser", MODCMD_REQUIRE_CHANUSER },
    { "disabled", MODCMD_DISABLED },
    { "ignore_csuspend", MODCMD_IGNORE_CSUSPEND },
    { "joinable", MODCMD_REQUIRE_JOINABLE },
    { "keepbound", MODCMD_KEEP_BOUND },
    { "loghostmask", MODCMD_LOG_HOSTMASK },
    { "nolog", MODCMD_NO_LOG },
    { "networkhelper", MODCMD_REQUIRE_NETWORK_HELPER },
    { "never_csuspend", MODCMD_NEVER_CSUSPEND },
    { "oper", MODCMD_REQUIRE_OPER },
    { "qualified", MODCMD_REQUIRE_QUALIFIED },
    { "regchan", MODCMD_REQUIRE_REGCHAN },
    { "supporthelper", MODCMD_REQUIRE_SUPPORT_HELPER },
    { "helping", MODCMD_REQUIRE_HELPING },
    { "toy", MODCMD_TOY },
    { NULL, 0 }
};

static int
flags_bsearch(const void *a, const void *b) {
    const char *key = a;
    const struct modcmd_flag *flag = b;
    return ircncasecmp(key, flag->name, strlen(flag->name));
}

static int
flags_qsort(const void *a, const void *b) {
    const struct modcmd_flag *fa = a, *fb = b;
    return irccasecmp(fa->name, fb->name);
}

DEFINE_LIST(svccmd_list, struct svccmd*);
DEFINE_LIST(module_list, struct module*);

static void
free_service_command(void *data) {
    struct svccmd *svccmd;
    unsigned int nn;

    svccmd = data;
    if (svccmd->alias.used) {
        for (nn=0; nn<svccmd->alias.used; ++nn)
            free(svccmd->alias.list[nn]);
        free(svccmd->alias.list);
    }
    free(svccmd->name);
    free(svccmd);
}

static void
free_service(void *data) {
    struct service *service = data;
    dict_delete(service->commands);
    module_list_clean(&service->modules);
    free(service);
}

static void
free_module_command(void *data) {
    struct modcmd *modcmd = data;
    free_service_command(modcmd->defaults);
    free(modcmd->name);
    free(modcmd);
}

static void
free_module(void *data) {
    struct module *module = data;
    dict_delete(module->commands);
    close_helpfile(module->helpfile);
    free(module->name);
    free(module);
}

struct module *
module_register(const char *name, struct log_type *clog, const char *helpfile_name, expand_func_t expand_help) {
    struct module *newmod;

    newmod = calloc(1, sizeof(*newmod));
    newmod->name = strdup(name);
    newmod->commands = dict_new();
    dict_set_free_data(newmod->commands, free_module_command);
    newmod->clog = clog;
    newmod->helpfile_name = helpfile_name;
    newmod->expand_help = expand_help;
    if (newmod->helpfile_name) {
        newmod->helpfile = open_helpfile(newmod->helpfile_name, newmod->expand_help);
    }
    dict_insert(modules, newmod->name, newmod);
    return newmod;
}

struct module *
module_find(const char *name) {
    return dict_find(modules, name, NULL);
}

static void
add_pending_template(struct svccmd *cmd, const char *target) {
    struct pending_template *pending = calloc(1, sizeof(*pending));
    pending->cmd = cmd;
    pending->base = strdup(target);
    pending->next = pending_templates;
    pending_templates = pending;
}

static struct svccmd *
svccmd_resolve_name(struct svccmd *origin, const char *name) {
    char *sep, svcname[MAXLEN];

    if ((sep = strchr(name, '.'))) {
        memcpy(svcname, name, sep-name);
        svcname[sep-name] = 0;
        name = sep + 1;
        if (svcname[0] == '*') {
            struct module *module = module_find(svcname+1);
            struct modcmd *cmd = module ? dict_find(module->commands, name, NULL) : NULL;
            return cmd ? cmd->defaults : NULL;
        } else {
            struct service *service = service_find(svcname);
            return service ? dict_find(service->commands, name, NULL) : NULL;
        }
    } else {
        if (origin->parent) {
            return dict_find(origin->parent->commands, name, NULL);
        } else {
            struct modcmd *cmd = dict_find(origin->command->parent->commands, name, NULL);
            return cmd ? cmd->defaults : NULL;
        }
    }
}

static void
modcmd_set_effective_flags(struct svccmd *cmd) {
    int flags = cmd->flags | cmd->command->flags;
    if (cmd->min_opserv_level > 0)
        flags |= MODCMD_REQUIRE_OPER;
    if (cmd->min_channel_access > 0)
        flags |= MODCMD_REQUIRE_CHANUSER;
    if (flags & MODCMD_REQUIRE_CHANUSER)
        flags |= MODCMD_REQUIRE_REGCHAN;
    if (flags & MODCMD_REQUIRE_REGCHAN)
        flags |= MODCMD_REQUIRE_CHANNEL;
    if (flags & (MODCMD_REQUIRE_STAFF|MODCMD_REQUIRE_HELPING))
        flags |= MODCMD_REQUIRE_AUTHED;
    cmd->effective_flags = flags;
}

static void
svccmd_copy_rules(struct svccmd *dest, struct svccmd *src) {
    dest->flags |= src->flags;
    dest->req_account_flags |= src->req_account_flags;
    dest->deny_account_flags |= src->deny_account_flags;
    if (src->min_opserv_level > dest->min_opserv_level)
        dest->min_opserv_level = src->min_opserv_level;
    if (src->min_channel_access > dest->min_channel_access)
        dest->min_channel_access = src->min_channel_access;
    modcmd_set_effective_flags(dest);
}

static int
svccmd_configure(struct svccmd *cmd, struct userNode *user, struct userNode *bot, const char *param, const char *value) {
    if (!irccasecmp(param, "flags")) {
        unsigned int set_flags, rem_flags;
        struct modcmd_flag *flag;
        int opt, end;

        for (set_flags = rem_flags = 0; 1; value += end) {
            end = strcspn(value, ",");
            if (*value == '+')
                opt = 1;
            else if (*value == '-')
                opt = 0;
            else {
                if (user)
                    send_message(user, bot, "MCMSG_BARE_FLAG", end, value);
                else
                    log_module(MAIN_LOG, LOG_ERROR, "Flag %.*s must be preceded by a + or - (for command %s).", end, value, cmd->name);
                return 0;
            }
            value++;
            flag = bsearch(value, flags, ArrayLength(flags)-1, sizeof(flags[0]), flags_bsearch);
            if (!flag) {
                if (user)
                    send_message(user, bot, "MCMSG_UNKNOWN_FLAG", end, value);
                else
                    log_module(MAIN_LOG, LOG_ERROR, "Unknown module flag %.*s (for command %s).", end, value, cmd->name);
                return 0;
            }
            if (opt)
                set_flags |= flag->flag, rem_flags &= ~flag->flag;
            else
                rem_flags |= flag->flag, set_flags &= ~flag->flag;
            if (!value[end-1])
                break;
        }
        cmd->flags = (cmd->flags | set_flags) & ~rem_flags;
        return 1;
    } else if (!irccasecmp(param, "channel_level") || !irccasecmp(param, "channel_access") || !irccasecmp(param, "access")) {
        unsigned short ul;
        if (!irccasecmp(value, "none")) {
            cmd->min_channel_access = 0;
            return 1;
        } else if ((ul = user_level_from_name(value, UL_OWNER)) > 0) {
            cmd->min_channel_access = ul;
            return 1;
        } else if (user) {
            send_message(user, bot, "MCMSG_BAD_CHANSERV_LEVEL", value);
            return 0;
        } else {
            log_module(MAIN_LOG, LOG_ERROR, "Invalid ChanServ access level %s (for command %s).", value, cmd->name);
            return 0;
        }
    } else if (!irccasecmp(param, "oper_level") || !irccasecmp(param, "oper_access") || !irccasecmp(param, "level")) {
        unsigned int newval = atoi(value);
        if (!isdigit(value[0]) || (newval > 1000)) {
            if (user)
                send_message(user, bot, "MCMSG_BAD_OPSERV_LEVEL", value);
            else
                log_module(MAIN_LOG, LOG_ERROR, "Invalid OpServ access level %s (for command %s).", value, cmd->name);
            return 0;
        }
        if (user && (!user->handle_info || (cmd->min_opserv_level > user->handle_info->opserv_level))) {
            send_message(user, bot, "MCMSG_LEVEL_TOO_LOW", cmd->name);
            return 0;
        }
        if (user && (!user->handle_info || (newval > user->handle_info->opserv_level))) {
            send_message(user, bot, "MCMSG_LEVEL_TOO_HIGH", value);
            return 0;
        }
        cmd->min_opserv_level = newval;
        return 1;
    } else if (!irccasecmp(param, "account_flags")) {
        return nickserv_modify_handle_flags(user, bot, value, &cmd->req_account_flags, &cmd->deny_account_flags);
    } else {
        if (user)
            send_message(user, bot, "MCMSG_BAD_OPTION", param);
        else
            log_module(MAIN_LOG, LOG_ERROR, "Unknown option %s (for command %s).", param, cmd->name);
        return 0;
    }
}

struct modcmd *
modcmd_register(struct module *module, const char *name, modcmd_func_t func, unsigned int min_argc, unsigned int flags, ...) {
    struct modcmd *newcmd;
    va_list args;
    const char *param, *value;

    newcmd = calloc(1, sizeof(*newcmd));
    newcmd->name = strdup(name);
    newcmd->parent = module;
    newcmd->func = func;
    newcmd->min_argc = min_argc;
    newcmd->flags = flags;
    newcmd->defaults = calloc(1, sizeof(*newcmd->defaults));
    newcmd->defaults->name = strdup(newcmd->name);
    newcmd->defaults->command = newcmd;
    dict_insert(module->commands, newcmd->name, newcmd);
    if (newcmd->flags & (MODCMD_REQUIRE_REGCHAN|MODCMD_REQUIRE_CHANNEL|MODCMD_REQUIRE_CHANUSER|MODCMD_REQUIRE_JOINABLE)) {
        newcmd->defaults->flags |= MODCMD_ACCEPT_CHANNEL;
    }
    if (newcmd->flags & MODCMD_REQUIRE_STAFF) {
        newcmd->defaults->flags |= MODCMD_REQUIRE_AUTHED;
    }
    va_start(args, flags);
    while ((param = va_arg(args, const char*))) {
        value = va_arg(args, const char*);
        if (!irccasecmp(param, "template")) {
            struct svccmd *svccmd = svccmd_resolve_name(newcmd->defaults, value);
            if (svccmd) {
                svccmd_copy_rules(newcmd->defaults, svccmd);
            } else {
                log_module(MAIN_LOG, LOG_ERROR, "Unable to resolve template name %s for %s.%s.", value, newcmd->parent->name, newcmd->name);
            }
            add_pending_template(newcmd->defaults, value);
        } else {
            svccmd_configure(newcmd->defaults, NULL, NULL, param, value);
        }
    }
    modcmd_set_effective_flags(newcmd->defaults);
    va_end(args);
    return newcmd;
}

/* This is kind of a lame hack, but it is actually simpler than having
 * the permission check vary based on the command itself, or having a
 * more generic rule system.
 */
int
svccmd_can_invoke(struct userNode *user, struct userNode *bot, struct svccmd *cmd, struct chanNode *channel, int options) {
    unsigned int uData_checked = 0;
    struct userData *uData = NULL;
    int rflags = 0, flags = cmd->effective_flags;

    if (flags & MODCMD_DISABLED) {
        if (options & SVCCMD_NOISY)
            send_message(user, bot, "MSG_COMMAND_DISABLED", cmd->name);
        return 0;
    }
    if ((flags & MODCMD_REQUIRE_QUALIFIED) && !(options & SVCCMD_QUALIFIED)) {
        if (options & SVCCMD_NOISY)
            send_message(user, bot, "MCMSG_MUST_QUALIFY", bot->nick, cmd->name, bot->nick);
        return 0;
    }
    if (flags & MODCMD_REQUIRE_AUTHED) {
        if (!user->handle_info) {
            if (options & SVCCMD_NOISY)
                send_message(user, bot, "MSG_AUTHENTICATE");
            return 0;
        }
        if (HANDLE_FLAGGED(user->handle_info, SUSPENDED)) {
            if (options & SVCCMD_NOISY)
                send_message(user, bot, "MCMSG_ACCOUNT_SUSPENDED");
            return 0;
        }
    }
    if (channel || (options & SVCCMD_NOISY)) {
        if ((flags & MODCMD_REQUIRE_CHANNEL) && !channel) {
            if (options & SVCCMD_NOISY)
                send_message(user, bot, "MSG_INVALID_CHANNEL");
            return 0;
        }
        if (flags & MODCMD_REQUIRE_REGCHAN) {
            if (!channel->channel_info) {
                if (options & SVCCMD_NOISY)
                    send_message(user, bot, "MCMSG_CHAN_NOT_REGISTERED", channel->name);
                return 0;
            } else if (IsSuspended(channel->channel_info) && !(flags & MODCMD_IGNORE_CSUSPEND)) {
                /* allow security-override users to always ignore channel suspensions, but flag it as a staff command */
                if (!user->handle_info
                    || !HANDLE_FLAGGED(user->handle_info, HELPING)
                    || (flags & MODCMD_NEVER_CSUSPEND)) {
                    if (options & SVCCMD_NOISY)
                        send_message(user, bot, "MCMSG_CHAN_SUSPENDED", channel->name, channel->channel_info->suspended->reason);
                    return 0;
                }
                rflags |= ACTION_STAFF;
            }
        }
        if (flags & MODCMD_REQUIRE_CHANUSER) {
            if (!uData_checked)
                uData = _GetChannelUser(channel->channel_info, user->handle_info, 1, 0), uData_checked = 1;
            if (!uData) {
                if (options & SVCCMD_NOISY)
                    send_message(user, bot, "MCMSG_NO_CHANNEL_ACCESS", channel->name);
                return 0;
            } else if (uData->access < cmd->min_channel_access) {
                if (options & SVCCMD_NOISY)
                    send_message(user, bot, "MCMSG_LOW_CHANNEL_ACCESS", channel->name);
                return 0;
            }
        }
        if ((flags & MODCMD_REQUIRE_JOINABLE) && channel) {
            if (!uData_checked)
                uData = _GetChannelUser(channel->channel_info, user->handle_info, 1, 0), uData_checked = 1;
            if ((channel->modes & (MODE_INVITEONLY|MODE_KEY|MODE_SECRET))
                && !uData
                && !IsService(user)
                && !GetUserMode(channel, user)) {
                if (options & SVCCMD_NOISY)
                    send_message(user, bot, "MCMSG_REQUIRES_JOINABLE", channel->name);
                return 0;
            }
        }
        if ((flags & MODCMD_TOY) && channel) {
            if (!channel->channel_info)
                rflags |= ACTION_NOCHANNEL;
            else switch (channel->channel_info->chOpts[chToys]) {
            case 'd':
                if (options & SVCCMD_NOISY)
                    send_message(user, bot, "MCMSG_TOYS_DISABLED", channel->name);
                return 0;
            case 'n':
                rflags |= ACTION_NOCHANNEL;
                break;
            case 'p':
                break;
            }
        }
    }
    if (flags & MODCMD_REQUIRE_STAFF) {
        if (((flags & MODCMD_REQUIRE_OPER) && IsOper(user))
            || ((flags & MODCMD_REQUIRE_NETWORK_HELPER) && IsNetworkHelper(user))
            || ((flags & MODCMD_REQUIRE_SUPPORT_HELPER) && IsSupportHelper(user))) {
            /* allow it */
            rflags |= ACTION_STAFF;
        } else {
            if (options & SVCCMD_NOISY)
                send_message(user, bot, "MSG_COMMAND_PRIVILEGED", cmd->name);
            return 0;
        }
    }
    if (flags & MODCMD_REQUIRE_HELPING) {
        if (!HANDLE_FLAGGED(user->handle_info, HELPING)) {
            if (options & SVCCMD_NOISY)
                send_message(user, bot, "MCMSG_MUST_BE_HELPING");
            return 0;
        }
        rflags |= ACTION_STAFF;
    }
    if (cmd->min_opserv_level > 0) {
        if (!oper_has_access(user, bot, cmd->min_opserv_level, !(options & SVCCMD_NOISY))) return 0;
        rflags |= ACTION_STAFF;
    }
    if (cmd->req_account_flags || cmd->deny_account_flags) {
        if (!user->handle_info) {
            if (options & SVCCMD_NOISY)
                send_message(user, bot, "MSG_AUTHENTICATE");
            return 0;
        }
        /* Do we want separate or different messages here? */
        if ((cmd->req_account_flags & ~user->handle_info->flags)
            || (cmd->deny_account_flags & user->handle_info->flags)) {
            if (options & SVCCMD_NOISY)
                send_message(user, bot, "MSG_COMMAND_PRIVILEGED", cmd->name);
            return 0;
        }
    }

    /* If it's an override, return a special value. */
    if ((flags & MODCMD_REQUIRE_CHANUSER)
        && (options & SVCCMD_NOISY)
        && (uData->access > 500)
        && (!(uData = _GetChannelUser(channel->channel_info, user->handle_info, 0, 0))
            || uData->access < cmd->min_channel_access)
        && !(flags & (MODCMD_REQUIRE_STAFF|MODCMD_REQUIRE_HELPING))) {
        rflags |= ACTION_OVERRIDE;
    }
    return rflags | ACTION_ALLOW;
}

static int
svccmd_expand_alias(struct svccmd *cmd, unsigned int old_argc, char *old_argv[], char *new_argv[]) {
    unsigned int ii, new_argc;
    char *arg;

    for (ii=new_argc=0; ii<cmd->alias.used; ++ii) {
        arg = cmd->alias.list[ii];
        if (arg[0] != '$') {
            new_argv[new_argc++] = arg;
            continue;
        }
        if (arg[1] == '$') {
            new_argv[new_argc++] = arg + 1;
        } else if (isdigit(arg[1])) {
            unsigned int lbound, ubound, jj;
            char *end_num;

            lbound = strtoul(arg+1, &end_num, 10);
            switch (end_num[0]) {
            case 0: ubound = lbound; break;
            case '-':
                if (end_num[1] == 0) {
                    ubound = old_argc - 1;
                    break;
                } else if (isdigit(end_num[1])) {
                    ubound = strtoul(end_num+1, NULL, 10);
                    break;
                }
                /* else fall through to default case */
            default:
                log_module(MAIN_LOG, LOG_ERROR, "Alias expansion parse error in %s (near %s; %s.%s arg %d).", arg, end_num, cmd->parent->bot->nick, cmd->name, ii);
                return 0;
            }
            if (ubound >= old_argc)
                ubound = old_argc - 1;
            if (lbound < old_argc)
                for (jj = lbound; jj <= ubound; )
                    new_argv[new_argc++] = old_argv[jj++];
        } else {
            log_module(MAIN_LOG, LOG_ERROR, "Alias expansion: I do not know how to handle %s (%s.%s arg %d).", arg, cmd->parent->bot->nick, cmd->name, ii);
            return 0;
        }
    }
    return new_argc;
}

int
svccmd_invoke_argv(struct userNode *user, struct service *service, struct chanNode *channel, unsigned int argc, char *argv[], unsigned int server_qualified) {
    extern struct userNode *chanserv;
    struct svccmd *cmd;
    unsigned int cmd_arg, perms, flags, options;
    char channel_name[CHANNELLEN+1];

    /* First check pubcmd for the channel. */
    if (channel && (channel->channel_info) && (service->bot == chanserv)
        && !check_user_level(channel, user, lvlPubCmd, 1, 0)) {
        send_message(user, service->bot, "MCMSG_PUBLIC_DENY", channel->name);
        return 0;
    }

    options = (server_qualified ? SVCCMD_QUALIFIED : 0) | SVCCMD_DEBIT | SVCCMD_NOISY;
    /* Find the command argument. */
    cmd_arg = IsChannelName(argv[0]) ? 1 : 0;
    if (argc < cmd_arg+1) {
        send_message(user, service->bot, "MCMSG_MISSING_COMMAND");
        return 0;
    }
    if (!isalnum(*argv[cmd_arg])) {
        /* Silently ignore stuff that doesn't begin with a letter or number. */
        return 0;
    }
    cmd = dict_find(service->commands, argv[cmd_arg], NULL);
    if (!cmd) {
        send_message(user, service->bot, "MSG_COMMAND_UNKNOWN", argv[cmd_arg]);
        return 0;
    }
    flags = cmd->effective_flags;
    /* If they put a channel name first, check if the command allows
     * it.  If so, swap it with the command name.
     */
    if (cmd_arg == 1) {
        char *tmp;
        /* Complain if we're not supposed to accept the channel. */
        if (!(flags & MODCMD_ACCEPT_CHANNEL)) {
            send_message(user, service->bot, "MCMSG_NO_CHANNEL_BEFORE");
            return 0;
        }
        if (!(flags & MODCMD_ACCEPT_PCHANNEL)
            && (argv[0][0] == '+')) {
            send_message(user, service->bot, "MCMSG_NO_PLUS_CHANNEL");
            return 0;
        }
        tmp = argv[1];
        argv[1] = argv[0];
        argv[0] = tmp;
    }

    /* Try to grab a channel handle before alias expansion.
     * If the command accepts a channel name, and argv[1] is
     * one, use it as a channel name, and hide it from logging.
     */
    if ((argc > 1)
        && (flags & MODCMD_ACCEPT_CHANNEL)
        && IsChannelName(argv[1])
        && ((argv[1][0] != '+') || (flags & MODCMD_ACCEPT_PCHANNEL))
        && (channel = dict_find(channels, argv[1], NULL))) {
        argv[1] = argv[0];
        argv++, argc--;
        cmd_arg = 1;
    }

    /* Expand the alias arguments, if there are any. */
    if (cmd->alias.used) {
        char *new_argv[MAXNUMPARAMS];
        argc = svccmd_expand_alias(cmd, argc, argv, new_argv);
        if (!argc) {
            send_message(service->bot, user, "MCMSG_ALIAS_ERROR", cmd->name);
            return 0;
        }
        argv = new_argv;

        /* Try again to grab a handle to the channel after alias
         * expansion, overwriting any previous channel. This should,
         * of course, only be done again if an alias was acually
         * expanded. */
        if ((argc > 1)
            && (flags & MODCMD_ACCEPT_CHANNEL)
            && IsChannelName(argv[1])
            && ((argv[1][0] != '+') || (flags & MODCMD_ACCEPT_PCHANNEL))
            && (channel = dict_find(channels, argv[1], NULL))) {
            argv[1] = argv[0];
            argv++, argc--;
            cmd_arg = 1;
        }
    }

    /* Figure out what actions we should do for it.. */
    if (cmd_arg && (flags & MODCMD_TOY)) {
        /* Do not let user manually specify a channel. */
        channel = NULL;
    }
    if (argc < cmd->command->min_argc) {
        send_message(user, service->bot, "MSG_MISSING_PARAMS", cmd->name);
        return 0;
    }
    if (!cmd->command->func) {
        send_message(user, service->bot, "MCMSG_INTERNAL_COMMAND", cmd->name);
        return 0;
    }
    perms = svccmd_can_invoke(user, service->bot, cmd, channel, options);
    if (!perms)
        return 0;
    cmd->uses++;
    if (perms & ACTION_NOCHANNEL)
        channel = NULL;

    if (channel)
        safestrncpy(channel_name, channel->name, sizeof(channel_name));
    else
        channel_name[0] = 0;
    if (!cmd->command->func(user, channel, argc, argv, cmd))
        return 0;
    if (!(flags & MODCMD_NO_LOG)) {
        enum log_severity slvl;
        if (perms & ACTION_STAFF)
            slvl = LOG_STAFF;
        else if (perms & ACTION_OVERRIDE)
            slvl = LOG_OVERRIDE;
        else
            slvl = LOG_COMMAND;
        /* Unsplit argv after running the function to get the benefit
         * of any mangling/hiding done by the commands. */
        log_audit(cmd->command->parent->clog, slvl, user, service->bot, channel_name, ((flags & MODCMD_LOG_HOSTMASK) ? AUDIT_HOSTMASK : 0), unsplit_string(argv, argc, NULL));
    }
    return 1;
}

int
svccmd_send_help(struct userNode *user, struct userNode *bot, struct svccmd *cmd) {
    char cmdname[MAXLEN];
    unsigned int nn;
    /* Show command name (in bold). */
    for (nn=0; cmd->name[nn]; nn++)
        cmdname[nn] = toupper(cmd->name[nn]);
    cmdname[nn] = 0;
    send_message_type(4, user, bot, "$b%s$b", cmdname);
    /* If it's an alias, show what it's an alias for. */
    if (cmd->alias.used) {
        char alias_text[MAXLEN];
        unsplit_string((char**)cmd->alias.list, cmd->alias.used, alias_text);
        send_message(user, bot, "MCMSG_COMMAND_ALIASES", cmd->name, alias_text);
    }
    /* Show the help entry for the underlying command. */
    return send_help(user, bot, cmd->command->parent->helpfile, cmd->command->name);
}

int
svccmd_send_help_2(struct userNode *user, struct service *service, const char *topic) {
    struct module *module;
    struct svccmd *cmd;
    unsigned int ii;

    if ((cmd = dict_find(service->commands, topic, NULL)))
        return svccmd_send_help(user, service->bot, cmd);
    if (!topic)
        topic = "<index>";
    for (ii = 0; ii < service->modules.used; ++ii) {
        module = service->modules.list[ii];
        if (!module->helpfile)
            continue;
        if (dict_find(module->helpfile->db, topic, NULL))
            return send_help(user, service->bot, module->helpfile, topic);
    }
    send_message(user, service->bot, "MSG_TOPIC_UNKNOWN");
    return 0;
}

static int
svccmd_invoke(struct userNode *user, struct service *service, struct chanNode *channel, char *text, int server_qualified) {
    unsigned int argc;
    char *argv[MAXNUMPARAMS];

    if (!*text)
        return 0;
    if (service->privileged) {
        if (!IsOper(user)) {
            send_message(user, service->bot, "MSG_SERVICE_PRIVILEGED", service->bot->nick);
            return 0;
        }
        if (!user->handle_info) {
            send_message(user, service->bot, "MSG_AUTHENTICATE");
            return 0;
        }
        if (HANDLE_FLAGGED(user->handle_info, OPER_SUSPENDED)) {
            send_message(user, service->bot, "MSG_OPER_SUSPENDED");
            return 0;
        }
    }
    argc = split_line(text, false, ArrayLength(argv), argv);
    return argc ? svccmd_invoke_argv(user, service, channel, argc, argv, server_qualified) : 0;
}

void
modcmd_privmsg(struct userNode *user, struct userNode *bot, char *text, int server_qualified) {
    struct service *service;
    if (!(service = dict_find(services, bot->nick, NULL))) {
        log_module(MAIN_LOG, LOG_ERROR, "modcmd_privmsg got privmsg for unhandled service %s, unregistering.", bot->nick);
        reg_privmsg_func(bot, NULL);
        return;
    }
    if (service->msg_hook && service->msg_hook(user, bot, text, server_qualified))
        return;
    svccmd_invoke(user, service, NULL, text, server_qualified);
}

void
modcmd_chanmsg(struct userNode *user, struct chanNode *chan, char *text, struct userNode *bot) {
    struct service *service;
    if (!(service = dict_find(services, bot->nick, NULL))) return;
    svccmd_invoke(user, service, chan, text, 0);
}

struct service *
service_register(struct userNode *bot, char trigger) {
    struct service *service;
    if ((service = dict_find(services, bot->nick, NULL)))
        return service;
    service = calloc(1, sizeof(*service));
    module_list_init(&service->modules);
    service->commands = dict_new();
    service->bot = bot;
    service->trigger = trigger;
    dict_set_free_data(service->commands, free_service_command);
    dict_insert(services, service->bot->nick, service);
    reg_privmsg_func(bot, modcmd_privmsg);
    if (trigger)
        reg_chanmsg_func(trigger, bot, modcmd_chanmsg);
    return service;
}

struct service *
service_find(const char *name) {
    return dict_find(services, name, NULL);
}

static void
svccmd_insert(struct service *service, char *name, struct svccmd *svccmd, struct modcmd *modcmd) {
    unsigned int ii;
    svccmd->parent = service;
    svccmd->name = name;
    svccmd->command = modcmd;
    svccmd->command->bind_count++;
    dict_insert(service->commands, svccmd->name, svccmd);
    for (ii=0; ii<service->modules.used; ++ii) {
        if (service->modules.list[ii] == svccmd->command->parent) break;
    }
    if (ii == service->modules.used) {
        module_list_append(&service->modules, svccmd->command->parent);
    }
}

struct svccmd *
service_bind_modcmd(struct service *service, struct modcmd *cmd, const char *name) {
    struct svccmd *svccmd;
    if ((svccmd = dict_find(service->commands, name, NULL))) {
        if (svccmd->command == cmd) return svccmd;
        log_module(MAIN_LOG, LOG_ERROR, "Tried to bind command %s.%s into service %s as %s, but already bound (as %s.%s).", cmd->parent->name, cmd->name, service->bot->nick, name, svccmd->command->parent->name, svccmd->command->name);
        return NULL;
    }
    svccmd = calloc(1, sizeof(*svccmd));
    svccmd_insert(service, strdup(name), svccmd, cmd);
    svccmd_copy_rules(svccmd, cmd->defaults);
    return svccmd;
}

static unsigned int
service_bind_module(struct service *service, struct module *module) {
    dict_iterator_t it;
    struct modcmd *modcmd;
    unsigned int count;

    count = 0;
    for (it = dict_first(module->commands); it; it = iter_next(it)) {
        modcmd = iter_data(it);
        if (!((modcmd->flags | modcmd->defaults->flags) & MODCMD_NO_DEFAULT_BIND))
            if (service_bind_modcmd(service, modcmd, iter_key(it)))
                count++;
    }
    return count;
}

/* This MUST return argc if the alias expansion code knows how to deal
 * with every argument in argv; otherwise, it MUST return the index of
 * an argument that the expansion code does not know how to deal with.
 */
static unsigned int
check_alias_args(char *argv[], unsigned int argc) {
    unsigned int arg;

    for (arg=0; arg<argc; ++arg) {
        if (argv[arg][0] != '$') {
            continue;
        } else if (argv[arg][1] == '$') {
            continue;
        } else if (isdigit(argv[arg][1])) {
            char *end_num;

            strtoul(argv[arg]+1, &end_num, 10);
            switch (end_num[0]) {
            case 0:
                continue;
            case '-':
                if (end_num[1] == 0)
                    continue;
                else if (isdigit(end_num[1]))
                    continue;
                /* else fall through to default case */
            default:
                return arg;
            }
        } else
            return arg;
    }
    return arg;
}

static unsigned int
collapse_cmdname(char *argv[], unsigned int argc, char *dest) {
    unsigned int ii, pos, arg;
    if (!argc) {
        dest[0] = 0;
        return 0;
    }
    for (ii=pos=0, arg=0; argv[arg][ii]; ) {
        if (argv[arg][ii] == '\\') {
            if (argv[arg][ii+1]) {
                /* escaping a real character just puts it in literally */
                dest[pos++] = argv[arg][++ii];
            } else if ((arg+1) == argc) {
                /* we ran to the end of the argument list; abort */
                break;
            } else {
                /* escape at end of a word is a space */
                dest[pos++] = ' ';
                ii = 0;
                arg++;
            }
        } else {
            /* normal characters don't need escapes */
            dest[pos++] = argv[arg][ii++];
        }
    }
    dest[pos] = 0;
    return arg + 1;
}

static MODCMD_FUNC(cmd_bind) {
    struct service *service;
    struct svccmd *template, *newcmd;
    char *svcname, *dot;
    char newname[MAXLEN], cmdname[MAXLEN];
    unsigned int arg, diff;

    assert(argc > 3);
    svcname = argv[1];
    arg = collapse_cmdname(argv+2, argc-2, newname) + 2;
    if (!arg) {
        reply("MSG_MISSING_PARAMS", cmd->name);
        return 0;
    }
    diff = collapse_cmdname(argv+arg, argc-arg, cmdname);
    if (!diff) {
        reply("MSG_MISSING_PARAMS", cmd->name);
        return 0;
    }
    arg += diff;

    if (!(service = service_find(svcname))) {
        reply("MCMSG_UNKNOWN_SERVICE", svcname);
        return 0;
    }

    if ((newcmd = dict_find(service->commands, newname, NULL))) {
        reply("MCMSG_ALREADY_BOUND", service->bot->nick, newname);
        return 0;
    }

    if ((dot = strchr(cmdname, '.')) && (dot[1] == '*') && (dot[2] == 0)) {
        unsigned int count;
        struct module *module;
        *dot = 0;
        module = module_find((cmdname[0] == '*') ? cmdname+1 : cmdname);
        if (!module) {
            reply("MSG_MODULE_UNKNOWN", cmdname);
            return 0;
        }
        count = service_bind_module(service, module);
        reply("MCMSG_MODULE_BOUND", count, module->name, service->bot->nick);
        return count != 0;
    }
    newcmd = calloc(1, sizeof(*newcmd));
    newcmd->name = strdup(newname);
    newcmd->parent = service;
    if (!(template = svccmd_resolve_name(newcmd, cmdname))) {
        reply("MCMSG_UNKNOWN_COMMAND_2", cmdname, service->bot->nick);
        free(newcmd->name);
        free(newcmd);
        return 0;
    }
    if (template->alias.used) {
        reply("MCMSG_CANNOT_DOUBLE_ALIAS");
        free(newcmd->name);
        free(newcmd);
        return 0;
    }

    if (argc > arg) {
        /* a more complicated alias; fix it up */
        unsigned int nn;

        arg -= diff;
        nn = check_alias_args(argv+arg, argc-arg);
        if (nn+arg < argc) {
            reply("MCMSG_BAD_ALIAS_ARGUMENT", argv[nn+arg]);
            free(newcmd->name);
            free(newcmd);
            return 0;
        }
        newcmd->alias.used = newcmd->alias.size = argc-arg;
        newcmd->alias.list = calloc(newcmd->alias.size, sizeof(newcmd->alias.list[0]));
        for (nn=0; nn<newcmd->alias.used; ++nn)
            newcmd->alias.list[nn] = strdup(argv[nn+arg]);
    }

    svccmd_insert(service, newcmd->name, newcmd, template->command);
    svccmd_copy_rules(newcmd, template);
    reply("MCMSG_COMMAND_BOUND", newcmd->name, newcmd->parent->bot->nick);
    return 1;
}

static int
service_recheck_bindings(struct service *service, struct module *module) {
    dict_iterator_t it;
    struct svccmd *cmd;

    for (it = dict_first(service->commands); it; it = iter_next(it)) {
        cmd = iter_data(it);
        if (cmd->command->parent == module) return 0;
    }
    /* No more bindings, remove it from our list. */
    module_list_remove(&service->modules, module);
    return 1;
}

static svccmd_unbind_func_t *suf_list;
unsigned int suf_size, suf_used;

void
reg_svccmd_unbind_func(svccmd_unbind_func_t handler) {
    if (suf_used == suf_size) {
        if (suf_size) {
            suf_size <<= 1;
            suf_list = realloc(suf_list, suf_size*sizeof(svccmd_unbind_func_t));
        } else {
            suf_size = 8;
            suf_list = malloc(suf_size*sizeof(svccmd_unbind_func_t));
        }
    }
    suf_list[suf_used++] = handler;
}

static MODCMD_FUNC(cmd_unbind) {
    struct service *service;
    struct userNode *bot;
    struct svccmd *bound;
    struct module *module;
    const char *svcname;
    unsigned int arg, ii;
    char cmdname[MAXLEN];

    assert(argc > 2);
    svcname = argv[1];
    arg = collapse_cmdname(argv+2, argc-2, cmdname) + 2;
    if (!arg) {
        reply("MSG_MISSING_PARAMS", cmd->name);
        return 0;
    }
    if (!(service = service_find(svcname))) {
        reply("MCMSG_UNKNOWN_SERVICE", svcname);
        return 0;
    }
    if (!(bound = dict_find(service->commands, cmdname, NULL))) {
        reply("MCMSG_NO_COMMAND_BOUND", service->bot->nick, cmdname);
        return 0;
    }
    if ((bound->command->flags & MODCMD_KEEP_BOUND) && (bound->command->bind_count == 1)) {
        reply("MCMSG_UNBIND_PROHIBITED", bound->command->name);
        return 0;
    }

    for (ii=0; ii<suf_used; ii++)
        suf_list[ii](bound);
    /* If this command binding is removing itself, we must take care
     * not to dereference it after the dict_remove.
     */
    bot = cmd->parent->bot;
    module = cmd->command->parent;
    dict_remove(service->commands, bound->name);
    send_message(user, bot, "MCMSG_COMMAND_UNBOUND", cmdname, service->bot->nick);
    if (service_recheck_bindings(service, module))
        send_message(user, bot, "MCMSG_HELPFILE_UNBOUND", module->name, module->name);
    return 1;
}

static MODCMD_FUNC(cmd_readhelp) {
    const char *modname;
    struct module *module;
    struct helpfile *old_helpfile;
    struct timeval start, stop;

    assert(argc > 1);
    modname = argv[1];
    if (!(module = module_find(modname))) {
        reply("MSG_MODULE_UNKNOWN", modname);
        return 0;
    }
    if (!module->helpfile_name) {
        reply("MCMSG_NO_HELPFILE", module->name);
        return 0;
    }
    old_helpfile = module->helpfile;
    gettimeofday(&start, NULL);
    module->helpfile = open_helpfile(module->helpfile_name, module->expand_help);
    if (!module->helpfile) {
        module->helpfile = old_helpfile;
        reply("MCMSG_HELPFILE_ERROR", module->helpfile_name);
        return 0;
    }
    if (old_helpfile) close_helpfile(old_helpfile);
    gettimeofday(&stop, NULL);
    stop.tv_sec -= start.tv_sec;
    stop.tv_usec -= start.tv_usec;
    if (stop.tv_usec < 0) {
	stop.tv_sec -= 1;
	stop.tv_usec += 1000000;
    }
    reply("MCMSG_HELPFILE_READ", module->name, stop.tv_sec, stop.tv_usec/1000);
    return 1;
}

static MODCMD_FUNC(cmd_help) {
    const char *topic;

    topic = (argc < 2) ? NULL : unsplit_string(argv+1, argc-1, NULL);
    return svccmd_send_help_2(user, cmd->parent, topic);
}

static MODCMD_FUNC(cmd_timecmd) {
    struct timeval start, stop;
    char cmd_text[MAXLEN];

    unsplit_string(argv+1, argc-1, cmd_text);
    gettimeofday(&start, NULL);
    svccmd_invoke(user, cmd->parent, channel, cmd_text, 0);
    gettimeofday(&stop, NULL);
    stop.tv_sec -= start.tv_sec;
    stop.tv_usec -= start.tv_usec;
    if (stop.tv_usec < 0) {
	stop.tv_sec -= 1;
	stop.tv_usec += 1000000;
    }
    reply("MCMSG_COMMAND_TIME", cmd_text, stop.tv_sec, stop.tv_usec);
    return 1;
}

static MODCMD_FUNC(cmd_command) {
    struct svccmd *svccmd;
    const char *cmd_name, *fmt_str;
    unsigned int flags, shown_flags, nn, pos;
    char buf[MAXLEN];

    assert(argc >= 2);
    cmd_name = unsplit_string(argv+1, argc-1, NULL);
    if (!(svccmd = svccmd_resolve_name(cmd, cmd_name))) {
        reply("MCMSG_UNKNOWN_COMMAND_2", cmd_name, cmd->parent->bot->nick);
        return 0;
    }
    pos = snprintf(buf, sizeof(buf), "%s.%s", svccmd->command->parent->name, svccmd->command->name);
    if (svccmd->alias.used) {
        buf[pos++] = ' ';
        unsplit_string((char**)svccmd->alias.list+1, svccmd->alias.used-1, buf+pos);
        reply("MCMSG_COMMAND_ALIASES", svccmd->name, buf);
    } else {
        reply("MCMSG_COMMAND_BINDING", svccmd->name, buf);
    }
    flags = svccmd->effective_flags;
    if ((svccmd->parent && svccmd->parent->privileged && !IsOper(user))
        || ((flags & MODCMD_REQUIRE_STAFF)
            && !IsOper(user) && !IsNetworkHelper(user) && !IsSupportHelper(user))) {
        reply("MCMSG_INSPECTION_REFUSED", svccmd->name);
        return 0;
    }
    if (flags & MODCMD_DISABLED) {
        reply("MSG_COMMAND_DISABLED", svccmd->name);
        return 1;
    }
    shown_flags = 0;
    if (svccmd->min_opserv_level > 0) {
        reply("MCMSG_NEED_OPSERV_LEVEL", svccmd->min_opserv_level);
        shown_flags |= MODCMD_REQUIRE_OPER | MODCMD_REQUIRE_AUTHED;
    }
    if (svccmd->min_channel_access > 0) {
        reply("MCMSG_NEED_CHANSERV_LEVEL", svccmd->min_channel_access);
        shown_flags |= MODCMD_REQUIRE_CHANUSER | MODCMD_REQUIRE_REGCHAN | MODCMD_REQUIRE_CHANNEL | MODCMD_REQUIRE_AUTHED;
    }
    if (svccmd->req_account_flags) {
        for (nn=pos=0; nn<32; nn++) {
            if (!(svccmd->req_account_flags & (1 << nn))) continue;
            buf[pos++] = HANDLE_FLAGS[nn];
        }
        buf[pos] = 0;
        reply("MCMSG_NEED_ACCOUNT_FLAGS", buf);
        shown_flags |= MODCMD_REQUIRE_AUTHED;
    }
    if (!flags && !shown_flags) {
        reply("MCMSG_NEED_NOTHING", svccmd->name);
        return 1;
    }
    if (flags & ~shown_flags & MODCMD_REQUIRE_HELPING) {
        reply("MCMSG_MUST_BE_HELPING");
        shown_flags |= MODCMD_REQUIRE_AUTHED | MODCMD_REQUIRE_STAFF;
    }
    if (flags & ~shown_flags & MODCMD_REQUIRE_STAFF) {
        switch (flags & MODCMD_REQUIRE_STAFF) {
        default: case MODCMD_REQUIRE_STAFF:
            fmt_str = "MCMSG_NEED_STAFF_ACCESS";
            break;
        case MODCMD_REQUIRE_OPER:
            fmt_str = "MCMSG_NEED_STAFF_OPER";
            break;
        case MODCMD_REQUIRE_NETWORK_HELPER:
            fmt_str = "MCMSG_NEED_STAFF_NETHELPER";
            break;
        case MODCMD_REQUIRE_OPER|MODCMD_REQUIRE_NETWORK_HELPER:
            fmt_str = "MCMSG_NEED_STAFF_NETHELPER_OR_OPER";
            break;
        case MODCMD_REQUIRE_SUPPORT_HELPER:
            fmt_str = "MCMSG_NEED_STAFF_SHELPER";
            break;
        case MODCMD_REQUIRE_OPER|MODCMD_REQUIRE_SUPPORT_HELPER:
            fmt_str = "MCMSG_NEED_STAFF_SHELPER_OR_OPER";
            break;
        case MODCMD_REQUIRE_SUPPORT_HELPER|MODCMD_REQUIRE_NETWORK_HELPER:
            fmt_str = "MCMSG_NEED_STAFF_HELPER";
            break;
        }
        reply(fmt_str);
        shown_flags |= MODCMD_REQUIRE_AUTHED | MODCMD_REQUIRE_STAFF;
    }
    if (flags & ~shown_flags & MODCMD_REQUIRE_JOINABLE) {
        reply("MCMSG_NEED_JOINABLE");
        shown_flags |= MODCMD_REQUIRE_CHANUSER;
    }
    if (flags & ~shown_flags & MODCMD_REQUIRE_CHANUSER) {
        if (flags & ~shown_flags & MODCMD_IGNORE_CSUSPEND)
            reply("MCMSG_NEED_CHANUSER_CSUSPENDABLE");
        else
            reply("MCMSG_NEED_CHANUSER");
        shown_flags |= MODCMD_IGNORE_CSUSPEND | MODCMD_REQUIRE_REGCHAN | MODCMD_REQUIRE_CHANNEL | MODCMD_REQUIRE_AUTHED;
    }
    if (flags & ~shown_flags & MODCMD_REQUIRE_REGCHAN) {
        reply("MCMSG_NEED_REGCHAN");
        shown_flags |= MODCMD_REQUIRE_CHANNEL;
    }
    if (flags & ~shown_flags & MODCMD_REQUIRE_CHANNEL)
        reply("MCMSG_NEED_CHANNEL");
    if (flags & ~shown_flags & MODCMD_REQUIRE_AUTHED)
        reply("MCMSG_NEED_AUTHED");
    if (flags & ~shown_flags & MODCMD_TOY)
        reply("MCMSG_IS_TOY", svccmd->name);
    if (flags & ~shown_flags & MODCMD_REQUIRE_QUALIFIED) {
        const char *botnick = svccmd->parent ? svccmd->parent->bot->nick : "SomeBot";
        reply("MCMSG_MUST_QUALIFY", botnick, svccmd->name, botnick);
    }
    reply("MCMSG_END_REQUIREMENTS", svccmd->name);
    return 1;
}

static MODCMD_FUNC(cmd_modcmd) {
    struct svccmd *svccmd;
    unsigned int arg, changed;
    char cmdname[MAXLEN];

    assert(argc >= 2);
    arg = collapse_cmdname(argv+1, argc-1, cmdname) + 1;
    if (!arg || (arg+2 < argc)) {
        reply("MSG_MISSING_PARAMS", cmd->name);
        return 0;
    }
    if (!(svccmd = svccmd_resolve_name(cmd, cmdname))) {
        reply("MCMSG_UNKNOWN_COMMAND_2", cmdname, cmd->parent->bot->nick);
        return 0;
    }
    changed = 0;
    while (arg+1 < argc) {
        if (svccmd_configure(svccmd, user, cmd->parent->bot, argv[arg], argv[arg+1])) {
            reply("MCMSG_COMMAND_MODIFIED", argv[arg], svccmd->name);
            changed = 1;
        }
        arg += 2;
    }
    if (changed)
        modcmd_set_effective_flags(svccmd);
    return changed;
}

static MODCMD_FUNC(cmd_god) {
    int helping;

    if (argc > 1) {
        if (enabled_string(argv[1])) {
            if (HANDLE_FLAGGED(user->handle_info, HELPING)) {
                reply("MCMSG_ALREADY_HELPING");
                return 0;
            }
	    helping = 1;
        } else if (disabled_string(argv[1])) {
            if (!HANDLE_FLAGGED(user->handle_info, HELPING)) {
                reply("MCMSG_ALREADY_NOT_HELPING");
                return 0;
            }
	    helping = 0;
        } else {
            reply("MSG_INVALID_BINARY", argv[1]);
            return 0;
	}
    } else {
        helping = !IsHelping(user);
    }

    if (helping) {
        HANDLE_SET_FLAG(user->handle_info, HELPING);
        reply("MCMSG_NOW_HELPING");
    } else {
        HANDLE_CLEAR_FLAG(user->handle_info, HELPING);
        reply("MCMSG_NOW_NOT_HELPING");
    }

    return 1;
}

static MODCMD_FUNC(cmd_joiner) {
    char cmdname[80];

    if (argc < 2) {
        int len = sprintf(cmdname, "%s ", cmd->name);
        dict_iterator_t it;
        struct string_buffer sbuf;

        string_buffer_init(&sbuf);
        for (it = dict_first(cmd->parent->commands); it; it = iter_next(it)) {
            if (!ircncasecmp(iter_key(it), cmdname, len)) {
                if (sbuf.used) string_buffer_append_string(&sbuf, ", ");
                string_buffer_append_string(&sbuf, iter_key(it));
            }
        }
        if (!sbuf.used) string_buffer_append(&sbuf, 0);
        reply("MCMSG_JOINER_CHOICES", cmd->name, sbuf.list);
        string_buffer_clean(&sbuf);
        return 1;
    }
    sprintf(cmdname, "%s %s", cmd->name, argv[1]);
    argv[1] = cmdname;
    svccmd_invoke_argv(user, cmd->parent, channel, argc-1, argv+1, 0);
    return 0; /* never try to log this; the recursive one logs it */
}

static MODCMD_FUNC(cmd_stats_modules) {
    struct helpfile_table tbl;
    dict_iterator_t it;
    unsigned int ii;
    struct module *mod;
    struct modcmd *modcmd;

    if (argc < 2) {
        tbl.length = dict_size(modules) + 1;
        tbl.width = 3;
        tbl.flags = TABLE_PAD_LEFT;
        tbl.contents = calloc(tbl.length, sizeof(tbl.contents[0]));
        tbl.contents[0] = calloc(tbl.width, sizeof(tbl.contents[0][0]));
        tbl.contents[0][0] = "Module";
        tbl.contents[0][1] = "Commands";
        tbl.contents[0][2] = "Helpfile";
        for (ii=1, it=dict_first(modules); it; it=iter_next(it), ii++) {
            mod = iter_data(it);
            tbl.contents[ii] = calloc(tbl.width, sizeof(tbl.contents[ii][0]));
            tbl.contents[ii][0] = mod->name;
            tbl.contents[ii][1] = strtab(dict_size(mod->commands));
            tbl.contents[ii][2] = mod->helpfile_name ? mod->helpfile_name : "(none)";
        }
    } else if (!(mod = dict_find(modules, argv[1], NULL))) {
        reply("MCMSG_UNKNOWN_MODULE", argv[1]);
        return 0;
    } else {
        reply("MCMSG_MODULE_INFO", mod->name);
        tbl.length = dict_size(mod->commands) + 1;
        tbl.width = 3;
        tbl.flags = TABLE_PAD_LEFT;
        tbl.contents = calloc(tbl.length, sizeof(tbl.contents[0]));
        tbl.contents[0] = calloc(tbl.width, sizeof(tbl.contents[0][0]));
        tbl.contents[0][0] = "Command";
        tbl.contents[0][1] = "Min. Args";
        tbl.contents[0][2] = "Bind Count";
        for (ii=1, it=dict_first(mod->commands); it; it=iter_next(it), ii++) {
            modcmd = iter_data(it);
            tbl.contents[ii] = calloc(tbl.width, sizeof(tbl.contents[ii][0]));
            tbl.contents[ii][0] = modcmd->name;
            tbl.contents[ii][1] = strtab(modcmd->min_argc);
            tbl.contents[ii][2] = strtab(modcmd->bind_count);
        }
    }
    table_send(cmd->parent->bot, user->nick, 0, 0, tbl);
    return 0;
}

static MODCMD_FUNC(cmd_stats_services) {
    struct helpfile_table tbl;
    dict_iterator_t it;
    unsigned int ii;
    struct service *service;
    struct svccmd *svccmd;
    char *extra;

    if (argc < 2) {
        tbl.length = dict_size(services) + 1;
        tbl.width = 4;
        tbl.flags = TABLE_PAD_LEFT;
        tbl.contents = calloc(tbl.length, sizeof(tbl.contents[0]));
        tbl.contents[0] = calloc(tbl.width, sizeof(tbl.contents[0][0]));
        tbl.contents[0][0] = "Service";
        tbl.contents[0][1] = "Commands";
        tbl.contents[0][2] = "Priv'd?";
        tbl.contents[0][3] = "Trigger";
        extra = calloc(2, tbl.length);
        for (ii=1, it=dict_first(services); it; it=iter_next(it), ii++) {
            service = iter_data(it);
            tbl.contents[ii] = calloc(tbl.width, sizeof(tbl.contents[ii][0]));
            tbl.contents[ii][0] = service->bot->nick;
            tbl.contents[ii][1] = strtab(dict_size(service->commands));
            tbl.contents[ii][2] = service->privileged ? "yes" : "no";
            extra[ii*2] = service->trigger;
            tbl.contents[ii][3] = extra+ii*2;
        }
        table_send(cmd->parent->bot, user->nick, 0, 0, tbl);
        free(extra);
        return 0;
    } else if (!(service = dict_find(services, argv[1], NULL))) {
        reply("MCMSG_UNKNOWN_SERVICE", argv[1]);
        return 0;
    } else {
        tbl.length = dict_size(service->commands) + 1;
        tbl.width = 5;
        tbl.flags = TABLE_PAD_LEFT | TABLE_NO_FREE;
        tbl.contents = calloc(tbl.length, sizeof(tbl.contents[0]));
        tbl.contents[0] = calloc(tbl.width, sizeof(tbl.contents[0][0]));
        tbl.contents[0][0] = "Command";
        tbl.contents[0][1] = "Module";
        tbl.contents[0][2] = "ModCmd";
        tbl.contents[0][3] = "Alias?";
        tbl.contents[0][4] = strdup("Uses");
        for (ii=1, it=dict_first(service->commands); it; it=iter_next(it), ii++) {
            svccmd = iter_data(it);
            tbl.contents[ii] = calloc(tbl.width, sizeof(tbl.contents[ii][0]));
            tbl.contents[ii][0] = svccmd->name;
            tbl.contents[ii][1] = svccmd->command->parent->name;
            tbl.contents[ii][2] = svccmd->command->name;
            tbl.contents[ii][3] = svccmd->alias.used ? "yes" : "no";
            tbl.contents[ii][4] = extra = malloc(12);
            sprintf(extra, "%u", svccmd->uses);
        }
        reply("MCMSG_SERVICE_INFO", service->bot->nick);
        table_send(cmd->parent->bot, user->nick, 0, 0, tbl);
        for (ii=0; ii<tbl.length; ii++) {
            free((char*)tbl.contents[ii][4]);
            free(tbl.contents[ii]);
        }
        free(tbl.contents);
        return 0;
    }
}

static MODCMD_FUNC(cmd_showcommands) {
    struct svccmd_list commands;
    struct helpfile_table tbl;
    struct svccmd *svccmd;
    dict_iterator_t it;
    unsigned int ii, ignore_flags = 0;
    unsigned int max_opserv_level = 1000;
    unsigned short max_chanserv_level = 500;
    char show_opserv_level = 0, show_channel_access = 0;

    /* Check to see what the max access they want to see is. */
    for (ii=1; ii<argc; ++ii) {
        if (isdigit(argv[ii][0]))
            max_opserv_level = atoi(argv[ii]);
        else
            max_chanserv_level = user_level_from_name(argv[ii], UL_OWNER);
    }

    /* Find the matching commands. */
    svccmd_list_init(&commands);
    if (cmd->parent->privileged)
        ignore_flags = MODCMD_REQUIRE_OPER;
    for (it = dict_first(cmd->parent->commands); it; it = iter_next(it)) {
        svccmd = iter_data(it);
        if (strchr(svccmd->name, ' '))
            continue;
        if (!svccmd_can_invoke(user, svccmd->parent->bot, svccmd, channel, SVCCMD_QUALIFIED))
            continue;
        if (svccmd->min_opserv_level > max_opserv_level)
            continue;
        if (svccmd->min_channel_access > max_chanserv_level)
            continue;
        if (svccmd->min_opserv_level > 0)
            show_opserv_level = 1;
        if (svccmd->min_channel_access > 0)
            show_channel_access = 1;
        if (svccmd->effective_flags
            & (MODCMD_REQUIRE_STAFF|MODCMD_REQUIRE_HELPING)
            & ~ignore_flags) {
            show_channel_access = 1;
        }
        svccmd_list_append(&commands, svccmd);
    }

    /* Build the table. */
    tbl.length = commands.used + 1;
    tbl.width = 1 + show_opserv_level + show_channel_access;
    tbl.flags = TABLE_REPEAT_ROWS;
    tbl.contents = calloc(tbl.length, sizeof(tbl.contents[0]));
    tbl.contents[0] = calloc(tbl.width, sizeof(tbl.contents[0][0]));
    tbl.contents[0][ii = 0] = "Command";
    if (show_opserv_level)
        tbl.contents[0][++ii] = "OpServ Level";
    if (show_channel_access)
        tbl.contents[0][++ii] = "ChanServ Access";
    for (ii=0; ii<commands.used; ++ii) {
        svccmd = commands.list[ii];
        tbl.contents[ii+1] = calloc(tbl.width, sizeof(tbl.contents[0][0]));
        tbl.contents[ii+1][0] = svccmd->name;
        if (show_opserv_level)
            tbl.contents[ii+1][1] = strtab(svccmd->min_opserv_level);
        if (show_channel_access) {
            const char *access;
            int flags = svccmd->effective_flags;
            if (flags & MODCMD_REQUIRE_HELPING)
                access = "helping";
            else if (flags & MODCMD_REQUIRE_STAFF) {
                switch (flags & MODCMD_REQUIRE_STAFF) {
                case MODCMD_REQUIRE_OPER: access = "oper"; break;
                case MODCMD_REQUIRE_OPER | MODCMD_REQUIRE_NETWORK_HELPER:
                case MODCMD_REQUIRE_NETWORK_HELPER: access = "net.helper"; break;
                default: access = "staff"; break;
                }
            } else
                access = strtab(svccmd->min_channel_access);
            tbl.contents[ii+1][1+show_opserv_level] = access;
        }
    }
    svccmd_list_clean(&commands);
    table_send(cmd->parent->bot, user->nick, 0, 0, tbl);
    return 0;
}

static MODCMD_FUNC(cmd_helpfiles) {
    struct service *service;
    unsigned int ii;

    if (!(service = dict_find(services, argv[1], NULL))) {
        reply("MCMSG_UNKNOWN_SERVICE", argv[1]);
        return 0;
    }

    if (argc < 3) {
        for (ii=0; ii<service->modules.used; ++ii)
            reply("MCMSG_HELPFILE_SEQUENCE", ii+1, service->modules.list[ii]->name);
        return 0;
    }

    service->modules.used = 0;
    for (ii=0; ii<argc-2; ii++) {
        struct module *module = dict_find(modules, argv[ii+2], NULL);
        if (!module) {
            reply("MCMSG_UNKNOWN_MODULE", argv[ii+2]);
            continue;
        }
        module_list_append(&service->modules, module);
    }
    reply("MCMSG_HELPFILE_SEQUENCE_SET", service->bot->nick);
    return 1;
}

static MODCMD_FUNC(cmd_service_add) {
    const char *nick, *desc;
    struct userNode *bot;

    nick = argv[1];
    if (!is_valid_nick(nick)) {
        reply("MCMSG_BAD_SERVICE_NICK", nick);
        return 0;
    }
    desc = unsplit_string(argv+2, argc-2, NULL);
    bot = GetUserH(nick);
    if (bot && IsService(bot)) {
        reply("MCMSG_ALREADY_SERVICE", bot->nick);
        return 0;
    }
    bot = AddService(nick, desc);
    service_register(bot, '\0');
    reply("MCMSG_NEW_SERVICE", bot->nick);
    return 1;
}

static MODCMD_FUNC(cmd_service_rename) {
    struct service *service;

    if (!(service = service_find(argv[1]))) {
        reply("MCMSG_UNKNOWN_SERVICE", argv[1]);
        return 0;
    }
    NickChange(service->bot, argv[2], 0);
    reply("MCMSG_SERVICE_RENAMED", service->bot->nick);
    return 1;
}

static MODCMD_FUNC(cmd_service_trigger) {
    struct userNode *bogon;
    struct service *service;

    if (!(service = service_find(argv[1]))) {
        reply("MCMSG_UNKNOWN_SERVICE", argv[1]);
        return 0;
    }
    if (argc < 3) {
        if (service->trigger)
            reply("MCMSG_CURRENT_TRIGGER", service->bot->nick, service->trigger);
        else
            reply("MCMSG_NO_TRIGGER", service->bot->nick);
        return 1;
    }
    if (service->trigger)
        reg_chanmsg_func(service->trigger, NULL, NULL);
    if (!irccasecmp(argv[2], "none") || !irccasecmp(argv[2], "remove")) {
        service->trigger = 0;
        reply("MCMSG_REMOVED_TRIGGER", service->bot->nick);
    } else if ((bogon = get_chanmsg_bot(argv[2][0]))) {
        reply("MCMSG_DUPLICATE_TRIGGER", bogon->nick, argv[2][0]);
        return 1;
    } else {
        service->trigger = argv[2][0];
        reg_chanmsg_func(service->trigger, service->bot, modcmd_chanmsg);
        reply("MCMSG_NEW_TRIGGER", service->bot->nick, service->trigger);
    }
    return 1;
}

static MODCMD_FUNC(cmd_service_privileged) {
    struct service *service;
    const char *newval;

    if (!(service = service_find(argv[1]))) {
        reply("MCMSG_UNKNOWN_SERVICE", argv[1]);
        return 0;
    }
    if (argc >= 3)
        service->privileged = true_string(argv[2]) || enabled_string(argv[2]);
    newval = user_find_message(user, service->privileged ? "MSG_ON" : "MSG_OFF");
    reply("MCMSG_SERVICE_PRIVILEGED", service->bot->nick, newval);
    return 1;
}

static MODCMD_FUNC(cmd_service_remove) {
    char *name, *reason;
    struct service *service;

    name = argv[1];
    if (argc > 2)
        reason = unsplit_string(argv+2, argc-2, NULL);
    else
        reason = "Removing bot";
    if (!(service = service_find(name))) {
        reply("MCMSG_UNKNOWN_SERVICE", name);
        return 0;
    }
    DelUser(service->bot, NULL, 1, reason);
    reply("MCMSG_SERVICE_REMOVED", name);
    dict_remove(services, name);
    return 1;
}

static MODCMD_FUNC(cmd_dump_messages) {
    const char *fname = "strings.db";
    struct saxdb_context *ctx;
    dict_iterator_t it;
    FILE *pf;

    if (!(pf = fopen(fname, "w"))) {
        reply("MCMSG_FILE_NOT_OPENED", fname);
        return 0;
    }
    if (!(ctx = saxdb_open_context(pf))) {
        reply("MSG_INTERNAL_FAILURE");
        return 0;
    }
    for (it = dict_first(lang_C->messages); it; it = iter_next(it))
        saxdb_write_string(ctx, iter_key(it), iter_data(it));
    saxdb_close_context(ctx);
    fclose(pf);
    reply("MCMSG_MESSAGES_DUMPED", fname);
    return 1;
}

static MODCMD_FUNC(cmd_version) {
    reply("MCMSG_VERSION");
    send_message_type(4, user, cmd->parent->bot, "Copyright 2000-2004 srvx Development Team.\nThe srvx Development Team includes Paul Chang, Adrian Dewhurst, Miles Peterson, Michael Poole and others.\nThe srvx Development Team can be reached at http://sf.net/projects/srvx/ or in #srvx on irc.gamesurge.net.");
    if ((argc > 1) && !irccasecmp(argv[1], "arch"))
        send_message_type(4, user, cmd->parent->bot, "%s", ARCH_VERSION);
    return 1;
}


void
modcmd_nick_change(struct userNode *user, const char *old_nick) {
    struct service *svc;
    if (!(svc = dict_find(services, old_nick, NULL)))
        return;
    dict_remove2(services, old_nick, 1);
    dict_insert(services, user->nick, svc);
}

void
modcmd_cleanup(void) {
    dict_delete(services);
    dict_delete(modules);
    if (suf_list)
        free(suf_list);
}

static void
modcmd_saxdb_write_command(struct saxdb_context *ctx, struct svccmd *cmd) {
    char buf[MAXLEN];
    unsigned int nn, len, pos;
    struct svccmd *template = cmd->command->defaults;

    saxdb_start_record(ctx, cmd->name, 0);
    sprintf(buf, "%s.%s", cmd->command->parent->name, cmd->command->name);
    saxdb_write_string(ctx, "command", buf);
    if (cmd->alias.used)
        saxdb_write_string_list(ctx, "aliased", &cmd->alias);
    if (cmd->min_opserv_level != template->min_opserv_level)
        saxdb_write_int(ctx, "oper_access", cmd->min_opserv_level);
    if (cmd->min_channel_access != template->min_channel_access)
        saxdb_write_int(ctx, "channel_access", cmd->min_channel_access);
    if (cmd->flags != template->flags) {
        if (cmd->flags) {
            for (nn=pos=0; flags[nn].name; ++nn) {
                if (cmd->flags & flags[nn].flag) {
                    buf[pos++] = '+';
                    len = strlen(flags[nn].name);
                    memcpy(buf+pos, flags[nn].name, len);
                    pos += len;
                    buf[pos++] = ',';
                }
            }
        } else {
            pos = 1;
        }
        buf[--pos] = 0;
        saxdb_write_string(ctx, "flags", buf);
    }
    if ((cmd->req_account_flags != template->req_account_flags)
        || (cmd->deny_account_flags != template->req_account_flags)) {
        pos = 0;
        if (cmd->req_account_flags) {
            buf[pos++] = '+';
            for (nn=0; nn<32; nn++)
                if (cmd->req_account_flags & (1 << nn))
                    buf[pos++] = handle_flags[nn];
        }
        if (cmd->deny_account_flags) {
            buf[pos++] = '-';
            for (nn=0; nn<32; nn++)
                if (cmd->deny_account_flags & (1 << nn))
                    buf[pos++] = handle_flags[nn];
        }
        buf[pos] = 0;
        saxdb_write_string(ctx, "account_flags", buf);
    }
    saxdb_end_record(ctx);
}

static int
modcmd_saxdb_write(struct saxdb_context *ctx) {
    struct string_list slist;
    dict_iterator_t it, it2;
    struct service *service;
    unsigned int ii;

    saxdb_start_record(ctx, "bots", 1);
    for (it = dict_first(services); it; it = iter_next(it)) {
        char buff[16];
        service = iter_data(it);
        saxdb_start_record(ctx, service->bot->nick, 1);
        if (service->trigger) {
            buff[0] = service->trigger;
            buff[1] = '\0';
            saxdb_write_string(ctx, "trigger", buff);
        }
        saxdb_write_string(ctx, "description", service->bot->info);
        if (service->privileged)
            saxdb_write_string(ctx, "privileged", "1");
        saxdb_end_record(ctx);
    }
    saxdb_end_record(ctx);

    saxdb_start_record(ctx, "services", 1);
    for (it = dict_first(services); it; it = iter_next(it)) {
        service = iter_data(it);
        saxdb_start_record(ctx, service->bot->nick, 1);
        for (it2 = dict_first(service->commands); it2; it2 = iter_next(it2))
            modcmd_saxdb_write_command(ctx, iter_data(it2));
        saxdb_end_record(ctx);
    }
    saxdb_end_record(ctx);

    saxdb_start_record(ctx, "helpfiles", 1);
    slist.size = 0;
    for (it = dict_first(services); it; it = iter_next(it)) {
        service = iter_data(it);
        slist.used = 0;
        for (ii = 0; ii < service->modules.used; ++ii)
            string_list_append(&slist, service->modules.list[ii]->name);
        saxdb_write_string_list(ctx, iter_key(it), &slist);
    }
    if (slist.list)
        free(slist.list);
    saxdb_end_record(ctx);

    return 0;
}

static int
append_entry(const char *key, UNUSED_ARG(void *data), void *extra) {
    struct helpfile_expansion *exp = extra;
    int row = exp->value.table.length++;
    exp->value.table.contents[row] = calloc(1, sizeof(char*));
    exp->value.table.contents[row][0] = key;
    return 0;
}

static struct helpfile_expansion
modcmd_expand(const char *variable) {
    struct helpfile_expansion exp;
    extern struct userNode *message_source;
    struct service *service;

    service = dict_find(services, message_source->nick, NULL);
    if (!irccasecmp(variable, "index")) {
        exp.type = HF_TABLE;
        exp.value.table.length = 1;
        exp.value.table.width = 1;
        exp.value.table.flags = TABLE_REPEAT_ROWS;
        exp.value.table.contents = calloc(dict_size(service->commands)+1, sizeof(char**));
        exp.value.table.contents[0] = calloc(1, sizeof(char*));
        exp.value.table.contents[0][0] = "Commands:";
        dict_foreach(service->commands, append_entry, &exp);
        return exp;
    } else if (!irccasecmp(variable, "languages")) {
        struct string_buffer sbuf;
        dict_iterator_t it;
        sbuf.used = sbuf.size = 0;
        sbuf.list = NULL;
        for (it = dict_first(languages); it; it = iter_next(it)) {
            string_buffer_append_string(&sbuf, iter_key(it));
            string_buffer_append(&sbuf, ' ');
        }
        sbuf.list[--sbuf.used] = 0;
        exp.type = HF_STRING;
        exp.value.str = sbuf.list;
        return exp;
    }
    exp.type = HF_STRING;
    exp.value.str = NULL;
    return exp;
}

static void
modcmd_load_bots(struct dict *db) {
    dict_iterator_t it;

    for (it = dict_first(db); it; it = iter_next(it)) {
        struct record_data *rd;
        struct userNode *bot;
        const char *nick, *desc;
        char trigger;

        rd = iter_data(it);
        if (rd->type != RECDB_OBJECT) {
            log_module(MAIN_LOG, LOG_ERROR, "Bad type for 'bots/%s' in modcmd db (expected object).", iter_key(it));
            continue;
        }
        nick = database_get_data(rd->d.object, "nick", RECDB_QSTRING);
        if (!nick)
            nick = iter_key(it);
        if (service_find(nick))
            continue;
        desc = database_get_data(rd->d.object, "trigger", RECDB_QSTRING);
        trigger = desc ? desc[0] : '\0';
        desc = database_get_data(rd->d.object, "description", RECDB_QSTRING);
        if (desc)
        {
            struct service *svc;
            bot = AddService(nick, desc);
            svc = service_register(bot, trigger);
            desc = database_get_data(rd->d.object, "privileged", RECDB_QSTRING);
            if (desc && (true_string(desc) || enabled_string(desc)))
                svc->privileged = 1;
        }
    }
}

static void
modcmd_conf_read(void) {
    modcmd_load_bots(conf_get_data("services", RECDB_OBJECT));
}

void
modcmd_init(void) {
    qsort(flags, ArrayLength(flags)-1, sizeof(flags[0]), flags_qsort);
    modules = dict_new();
    dict_set_free_data(modules, free_module);
    services = dict_new();
    dict_set_free_data(services, free_service);
    reg_nick_change_func(modcmd_nick_change);
    reg_exit_func(modcmd_cleanup);
    conf_register_reload(modcmd_conf_read);

    modcmd_module = module_register("modcmd", MAIN_LOG, "modcmd.help", modcmd_expand);
    bind_command = modcmd_register(modcmd_module, "bind", cmd_bind, 4, MODCMD_KEEP_BOUND, "oper_level", "800", NULL);
    help_command = modcmd_register(modcmd_module, "help", cmd_help, 1, 0, "flags", "+nolog", NULL);
    modcmd_register(modcmd_module, "command", cmd_command, 2, 0, "flags", "+nolog", NULL);
    modcmd_register(modcmd_module, "modcmd", cmd_modcmd, 4, MODCMD_KEEP_BOUND, "template", "bind", NULL);
    modcmd_register(modcmd_module, "god", cmd_god, 0, MODCMD_REQUIRE_AUTHED, "flags", "+oper,+networkhelper", NULL);
    modcmd_register(modcmd_module, "readhelp", cmd_readhelp, 2, 0, "oper_level", "650", NULL);
    modcmd_register(modcmd_module, "timecmd", cmd_timecmd, 2, 0, "oper_level", "1", NULL);
    modcmd_register(modcmd_module, "unbind", cmd_unbind, 3, 0, "template", "bind", NULL);
    modcmd_register(modcmd_module, "joiner", cmd_joiner, 1, 0, NULL);
    modcmd_register(modcmd_module, "stats modules", cmd_stats_modules, 1, 0, "flags", "+oper", NULL);
    modcmd_register(modcmd_module, "stats services", cmd_stats_services, 1, 0, "flags", "+oper", NULL);
    modcmd_register(modcmd_module, "showcommands", cmd_showcommands, 1, 0, "flags", "+acceptchan", NULL);
    modcmd_register(modcmd_module, "helpfiles", cmd_helpfiles, 2, 0, "template", "bind", NULL);
    modcmd_register(modcmd_module, "service add", cmd_service_add, 3, 0, "flags", "+oper", NULL);
    modcmd_register(modcmd_module, "service rename", cmd_service_rename, 3, 0, "flags", "+oper", NULL);
    modcmd_register(modcmd_module, "service trigger", cmd_service_trigger, 2, 0, "flags", "+oper", NULL);
    modcmd_register(modcmd_module, "service privileged", cmd_service_privileged, 2, 0, "flags", "+oper", NULL);
    modcmd_register(modcmd_module, "service remove", cmd_service_remove, 2, 0, "flags", "+oper", NULL);
    modcmd_register(modcmd_module, "dumpmessages", cmd_dump_messages, 1, 0, "oper_level", "1000", NULL);
    version_command = modcmd_register(modcmd_module, "version", cmd_version, 1, 0, NULL);
    message_register_table(msgtab);
}

static void
modcmd_db_load_command(struct service *service, const char *cmdname, struct dict *obj) {
    struct svccmd *svccmd;
    struct module *module;
    struct modcmd *modcmd;
    struct string_list *slist;
    const char *str, *sep;
    char buf[MAXLEN];

    str = database_get_data(obj, "command", RECDB_QSTRING);
    if (!str) {
        log_module(MAIN_LOG, LOG_ERROR, "Missing command for service %s command %s in modcmd.db", service->bot->nick, cmdname);
        return;
    }
    sep = strchr(str, '.');
    if (!sep) {
        log_module(MAIN_LOG, LOG_ERROR, "Invalid command %s for service %s command %s in modcmd.db", str, service->bot->nick, cmdname);
        return;
    }
    memcpy(buf, str, sep-str);
    buf[sep-str] = 0;
    if (!(module = module_find(buf))) {
        log_module(MAIN_LOG, LOG_ERROR, "Unknown module %s for service %s command %s in modcmd.db", buf, service->bot->nick, cmdname);
        return;
    }
    if (!(modcmd = dict_find(module->commands, sep+1, NULL))) {
        log_module(MAIN_LOG, LOG_ERROR, "Unknown command %s in module %s for service %s command %s", sep+1, module->name, service->bot->nick, cmdname);
        return;
    }
    /* Now that we know we have a command to use, fill in the basics. */
    svccmd = calloc(1, sizeof(*svccmd));
    svccmd_insert(service, strdup(cmdname), svccmd, modcmd);
    if ((str = database_get_data(obj, "template", RECDB_QSTRING))) {
        add_pending_template(svccmd, str);
    } else {
        svccmd_copy_rules(svccmd, modcmd->defaults);
    }
    if ((str = database_get_data(obj, "account_flags", RECDB_QSTRING))) {
        svccmd->req_account_flags = svccmd->deny_account_flags = 0;
        svccmd_configure(svccmd, NULL, service->bot, "account_flags", str);
    }
    if ((str = database_get_data(obj, "flags", RECDB_QSTRING))) {
        svccmd->flags = 0;
        svccmd_configure(svccmd, NULL, service->bot, "flags", str);
    }
    if ((str = database_get_data(obj, "oper_access", RECDB_QSTRING))
        || (str = database_get_data(obj, "opserv_level", RECDB_QSTRING))) {
        svccmd_configure(svccmd, NULL, service->bot, "oper_access", str);
    }
    if ((str = database_get_data(obj, "channel_access", RECDB_QSTRING))
        || (str = database_get_data(obj, "chanserv_level", RECDB_QSTRING))) {
        svccmd_configure(svccmd, NULL, service->bot, "channel_access", str);
    }
    if ((slist = database_get_data(obj, "aliased", RECDB_STRING_LIST))) {
        unsigned int nn;
        svccmd->alias.used = svccmd->alias.size = slist->used;
        svccmd->alias.list = calloc(svccmd->alias.size, sizeof(svccmd->alias.list[0]));
        for (nn=0; nn<slist->used; ++nn)
            svccmd->alias.list[nn] = strdup(slist->list[nn]);
    }
    modcmd_set_effective_flags(svccmd);
}

static struct svccmd *
service_make_alias(struct service *service, const char *alias, ...) {
    char *arg, *argv[MAXNUMPARAMS];
    unsigned int nn, argc;
    struct svccmd *svccmd, *template;
    va_list args;

    va_start(args, alias);
    argc = 0;
    while (1) {
        arg = va_arg(args, char*);
        if (!arg)
            break;
        argv[argc++] = arg;
    }
    va_end(args);
    svccmd = calloc(1, sizeof(*svccmd));
    if (!(template = svccmd_resolve_name(svccmd, argv[0]))) {
        log_module(MAIN_LOG, LOG_ERROR, "Invalid base command %s for alias %s in service %s", argv[0], alias, service->bot->nick);
        free(svccmd->name);
        free(svccmd);
        return NULL;
    }
    if (argc > 1) {
        svccmd->alias.used = svccmd->alias.size = argc;
        svccmd->alias.list = calloc(svccmd->alias.size, sizeof(svccmd->alias.list[0]));
        for (nn=0; nn<argc; nn++)
            svccmd->alias.list[nn] = strdup(argv[nn]);
    }
    svccmd_insert(service, strdup(alias), svccmd, template->command);
    svccmd_copy_rules(svccmd, template);
    return svccmd;
}

static int saxdb_present;

static int
modcmd_saxdb_read(struct dict *db) {
    struct dict *db2;
    dict_iterator_t it, it2;
    struct record_data *rd, *rd2;
    struct service *service;

    modcmd_load_bots(database_get_data(db, "bots", RECDB_OBJECT));
    db2 = database_get_data(db, "services", RECDB_OBJECT);
    if (!db2) {
        log_module(MAIN_LOG, LOG_ERROR, "Missing section 'services' in modcmd db.");
        return 1;
    }
    for (it = dict_first(db2); it; it = iter_next(it)) {
        rd = iter_data(it);
        if (rd->type != RECDB_OBJECT) {
            log_module(MAIN_LOG, LOG_ERROR, "Bad type for 'services/%s' in modcmd db (expected object).", iter_key(it));
            continue;
        }
        if (!(service = service_find(iter_key(it)))) {
            log_module(MAIN_LOG, LOG_ERROR, "Unknown service '%s' listed in modcmd db.", iter_key(it));
            continue;
        }
        for (it2 = dict_first(rd->d.object); it2; it2 = iter_next(it2)) {
            rd2 = iter_data(it2);
            if (rd2->type != RECDB_OBJECT) {
                log_module(MAIN_LOG, LOG_ERROR, "Bad type for 'services/%s/%s' in modcmd db (expected object).", iter_key(it), iter_key(it2));
                continue;
            }
            modcmd_db_load_command(service, iter_key(it2), rd2->d.object);
        }
    }
    db2 = database_get_data(db, "helpfiles", RECDB_OBJECT);
    for (it = dict_first(db2); it; it = iter_next(it)) {
        struct module *module;
        struct string_list *slist;
        unsigned int ii;

        rd = iter_data(it);
        if (rd->type != RECDB_STRING_LIST) {
            log_module(MAIN_LOG, LOG_ERROR, "Bad type for 'helpfiles/%s' in modcmd db (expected string list).", iter_key(it));
            continue;
        }
        slist = rd->d.slist;
        if (!(service = service_find(iter_key(it)))) {
            /* We probably whined about the service being missing above. */
            continue;
        }
        service->modules.used = 0;
        for (ii=0; ii<slist->used; ++ii) {
            if (!(module = dict_find(modules, slist->list[ii], NULL))) {
                log_module(MAIN_LOG, LOG_ERROR, "Unknown module '%s' listed in modcmd 'helpfiles/%s'.", slist->list[ii], iter_key(it));
                continue;
            }
            module_list_append(&service->modules, module);
        }
    }
    saxdb_present = 1;
    return 0;
}

static void
create_default_binds(void) {
    /* Which services should import which modules by default? */
    struct {
        const char *svcname;
        /* C is lame and requires a fixed size for this array.
         * Be sure you NULL-terminate each array and increment the
         * size here if you add more default modules to any
         * service. */
        const char *modnames[8];
    } def_binds[] = {
        { "ChanServ", { "ChanServ", NULL } },
        { "Global", { "Global", NULL } },
        { "NickServ", { "NickServ", NULL } },
        { "OpServ", { "OpServ", "modcmd", "sendmail", "saxdb", "proxycheck", NULL } },
        { NULL, { NULL } }
    };
    unsigned int ii, jj;
    char buf[128], *nick;
    struct service *service;
    struct module *module;

    for (ii = 0; def_binds[ii].svcname; ++ii) {
        sprintf(buf, "services/%s/nick", def_binds[ii].svcname);
        if (!(nick = conf_get_data(buf, RECDB_QSTRING)))
            continue;
        if (!(service = service_find(nick)))
            continue;
        if (dict_size(service->commands) > 0)
            continue;

        /* Bind the default modules for this service to it */
        for (jj = 0; def_binds[ii].modnames[jj]; ++jj) {
            if (!(module = module_find(def_binds[ii].modnames[jj])))
                continue;
            service_bind_module(service, module);
        }

        /* Bind the help and version commands to this service */
        service_bind_modcmd(service, help_command, help_command->name);
        service_bind_modcmd(service, version_command, version_command->name);

        /* Now some silly hax.. (aliases that most people want) */
        if (!irccasecmp(def_binds[ii].svcname, "ChanServ")) {
            service_make_alias(service, "addowner", "*chanserv.adduser", "$1", "owner", NULL);
            service_make_alias(service, "addcoowner", "*chanserv.adduser", "$1", "coowner", NULL);
            service_make_alias(service, "addmaster", "*chanserv.adduser", "$1", "master", NULL);
            service_make_alias(service, "addop", "*chanserv.adduser", "$1", "op", NULL);
            service_make_alias(service, "addpeon", "*chanserv.adduser", "$1", "peon", NULL);
            service_make_alias(service, "delowner", "*chanserv.deluser", "owner", "$1", NULL);
            service_make_alias(service, "delcoowner", "*chanserv.deluser", "coowner", "$1", NULL);
            service_make_alias(service, "delmaster", "*chanserv.deluser", "master", "$1", NULL);
            service_make_alias(service, "delop", "*chanserv.deluser", "op", "$1", NULL);
            service_make_alias(service, "delpeon", "*chanserv.deluser", "peon", "$1", NULL);
            service_make_alias(service, "command", "*modcmd.command", NULL);
            service_make_alias(service, "god", "*modcmd.god", NULL);
        } else if (!irccasecmp(def_binds[ii].svcname, "OpServ")) {
            struct svccmd *svccmd;
            svccmd = service_make_alias(service, "stats", "*modcmd.joiner", NULL);
            svccmd->min_opserv_level = 101;
            svccmd = service_make_alias(service, "service", "*modcmd.joiner", NULL);
            svccmd->min_opserv_level = 900;
        }
    }
}

static void
import_aliases_db() {
    struct dict *db;
    dict_iterator_t it, it2;
    struct record_data *rd, *rd2;
    struct service *service;
    struct module *module;

    if (!(db = parse_database("aliases.db")))
        return;
    for (it = dict_first(db); it; it = iter_next(it)) {
        service = service_find(iter_key(it));
        if (!service)
            continue;
        module = module_find(service->bot->nick);
        rd = iter_data(it);
        if (rd->type != RECDB_OBJECT)
            continue;
        for (it2 = dict_first(rd->d.object); it2; it2 = iter_next(it2)) {
            struct modcmd *command;
            rd2 = iter_data(it2);
            if (rd2->type != RECDB_QSTRING)
                continue;
            command = dict_find(module->commands, rd2->d.qstring, NULL);
            if (!command)
                continue;
            service_bind_modcmd(service, command, iter_key(it2));
        }
    }
}

void
modcmd_finalize(void) {
    /* Check databases. */
    saxdb_register("modcmd", modcmd_saxdb_read, modcmd_saxdb_write);
    create_default_binds();
    if (!saxdb_present)
        import_aliases_db();

    /* Resolve command rule-templates. */
    while (pending_templates) {
        struct pending_template *ptempl = pending_templates;
        struct svccmd *svccmd;

        pending_templates = ptempl->next;
        /* Only overwrite the current template if we have a valid template. */
        if (!strcmp(ptempl->base, "*")) {
            /* Do nothing. */
        } else if ((svccmd = svccmd_resolve_name(ptempl->cmd, ptempl->base))) {
            svccmd_copy_rules(ptempl->cmd, svccmd);
        } else {
            assert(ptempl->cmd->parent);
            log_module(MAIN_LOG, LOG_ERROR, "Unable to resolve template name %s for command %s in service %s.", ptempl->base, ptempl->cmd->name, ptempl->cmd->parent->bot->nick);
        }
        free(ptempl->base);
        free(ptempl);
    }
}

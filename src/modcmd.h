/* modcmd.h - Generalized module command support
 * Copyright 2002-2006 srvx Development Team
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

#if !defined(MODCMDS_H)
#define MODCMDS_H

#include "recdb.h"
#include "helpfile.h"
#include "log.h"

struct service;
struct svccmd;
struct module;
struct modcmd;

#define MODCMD_FUNC(NAME) int NAME(struct userNode *user, UNUSED_ARG(struct chanNode *channel), UNUSED_ARG(unsigned int argc), UNUSED_ARG(char **argv), UNUSED_ARG(struct svccmd *cmd))
typedef MODCMD_FUNC(modcmd_func_t);
#define SVCMSG_HOOK(NAME) int NAME(struct userNode *user, struct userNode *target, const char *text, int server_qualified)
typedef SVCMSG_HOOK(svcmsg_hook_t);

DECLARE_LIST(svccmd_list, struct svccmd*);
DECLARE_LIST(module_list, struct module*);

#if defined(GCC_VARMACROS)
# define reply(ARGS...) send_message(user, cmd->parent->bot, ARGS)
#elif defined(C99_VARMACROS)
# define reply(...) send_message(user, cmd->parent->bot, __VA_ARGS__)
#endif

#define modcmd_get_handle_info(USER, NAME) smart_get_handle_info(cmd->parent->bot, USER, NAME)
#define modcmd_chanmode_announce(CHANGE) mod_chanmode_announce(cmd->parent->bot, channel, CHANGE)
#define modcmd_chanmode(ARGV, ARGC, FLAGS) mod_chanmode(cmd->parent->bot, channel, ARGV, ARGC, FLAGS)

/* Miscellaneous flags controlling a command */
#define MODCMD_DISABLED                  0x001
#define MODCMD_NO_LOG                    0x002
#define MODCMD_KEEP_BOUND                0x004
#define MODCMD_ACCEPT_CHANNEL            0x008
#define MODCMD_ACCEPT_PCHANNEL           0x010
#define MODCMD_NO_DEFAULT_BIND           0x020
#define MODCMD_LOG_HOSTMASK              0x040
#define MODCMD_IGNORE_CSUSPEND           0x080
#define MODCMD_NEVER_CSUSPEND            0x100
/* Requirement (access control) flags */
#define MODCMD_REQUIRE_AUTHED         0x001000
#define MODCMD_REQUIRE_CHANNEL        0x002000
#define MODCMD_REQUIRE_REGCHAN        0x004000
#define MODCMD_REQUIRE_CHANUSER       0x008000
#define MODCMD_REQUIRE_JOINABLE       0x010000
#define MODCMD_REQUIRE_QUALIFIED      0x020000
#define MODCMD_REQUIRE_OPER           0x040000
#define MODCMD_REQUIRE_NETWORK_HELPER 0x080000
#define MODCMD_REQUIRE_SUPPORT_HELPER 0x100000
#define MODCMD_REQUIRE_HELPING        0x200000
#define MODCMD_TOY                    0x400000
#define MODCMD_REQUIRE_STAFF          (MODCMD_REQUIRE_OPER|MODCMD_REQUIRE_NETWORK_HELPER|MODCMD_REQUIRE_SUPPORT_HELPER)

#define SVCCMD_QUALIFIED              0x000001
#define SVCCMD_DEBIT                  0x000002
#define SVCCMD_NOISY                  0x000004

/* We do not use constants for 0 (no logging) and 1 (regular logging) as those
 * are used very often and are intuitive enough.
 */
#define CMD_LOG_STAFF       0x02
#define CMD_LOG_OVERRIDE    0x04

/* Modularized commands work like this:
 *
 * Modules define commands.  Services contain "bindings" of those
 * commands to names.
 *
 * The module-defined commands (modcmd structs) contain the parameters
 * fixed by code; for example, assuming a channel was provided, or
 * that the user has ChanServ access to that channel.
 *
 * Service command bindings (svccmd structs) define additional access
 * controls (and a count of how many times the command has been used)
 * as well as a link to the modcmd providing the function.
 *
 * Aliased commands are special svccmds that have alias expansion
 * information in an "extra" pointer.  In the future, this may be
 * moved into the svccmd struct if there are no other commands that
 * need "extra" data.
 *
 * The user must meet all the requirements (in flags, access levels,
 * etc.) before the command is executed.  As an exception, for the
 * "staff" permission checks (oper/network helper/support helper), any
 * one is sufficient to permit the command usage.
 */

struct service {
    struct userNode *bot;
    struct module_list modules;
    struct dict *commands; /* contains struct svccmd* */
    svcmsg_hook_t *msg_hook;
    unsigned int privileged : 1;
    char trigger;
};

struct svccmd {
    char *name;
    struct service *parent; /* where is this command bound? */
    struct modcmd *command; /* what is the implementation? */
    struct string_list alias; /* if it's a complicated binding, what is the expansion? */
    unsigned int uses; /* how many times was this command used? */
    unsigned int flags;
    unsigned long req_account_flags;
    unsigned long deny_account_flags;
    unsigned int min_opserv_level;
    unsigned int min_channel_access;
    unsigned int effective_flags;
};

struct module {
    char *name;                /* name of module */
    struct dict *commands;     /* contains struct modcmd* */
    struct log_type *clog;     /* where to send logged commands */
    const char *helpfile_name; /* name to use for helpfile */
    expand_func_t expand_help; /* expander function for helpfile */
    struct helpfile *helpfile; /* help file to use in case of syntax error */
};

struct modcmd {
    char *name;
    struct module *parent;
    modcmd_func_t *func;
    struct svccmd *defaults;
    unsigned int min_argc;
    unsigned int flags;
    unsigned int bind_count;
};

/* Register a command.  The varadic argument section consists of a set
 * of name/value pairs, where the name and value are strings that give
 * the default parameters for the command.  (The "flags" argument
 * gives the required parameters.)  The set is ended by a null name
 * pointer (without any value argument).
 */
struct modcmd *modcmd_register(struct module *module, const char *name, modcmd_func_t func, unsigned int min_argc, unsigned int flags, ...);

/* Register a command-providing module.  clog is where to log loggable
 * commands (those without the MODCMD_NO_LOG flag and which succeed).
 */
struct module *module_register(const char *name, struct log_type *clog, const char *helpfile_name, expand_func_t expand_help);
/* Find a module by name.  Returns NULL if no such module is registered. */
struct module *module_find(const char *name);

/* Register a command-using service. */
struct service *service_register(struct userNode *bot);
/* Find a service by name. */
struct service *service_find(const char *name);
/* Bind one command to a service. */
struct svccmd *service_bind_modcmd(struct service *service, struct modcmd *cmd, const char *name);

/* Send help for a command to a user. */
int svccmd_send_help(struct userNode *user, struct userNode *bot, struct svccmd *cmd);
/* .. and if somebody doesn't have a modcmd handy .. */
int svccmd_send_help_2(struct userNode *user, struct service *service, const char *topic);
/* Check whether a user may invoke a command or not.  If they can,
 * return non-zero.  If they cannot (and noisy is non-zero), tell them
 * why not and return 0.
 */
int svccmd_can_invoke(struct userNode *user, struct userNode *bot, struct svccmd *cmd, struct chanNode *channel, int flags);
/* Execute a command.  Returns non-zero on success. */
int svccmd_invoke_argv(struct userNode *user, struct service *service, struct chanNode *channel, unsigned int argc, char *argv[], unsigned int server_qualified);
/* Get notification when a command is being unbound.  This lets
 * services which cache svccmd references remove them.
 */
typedef void (*svccmd_unbind_func_t)(struct svccmd *target);
void reg_svccmd_unbind_func(svccmd_unbind_func_t handler);

/* Initialize the module command subsystem. */
void modcmd_init(void);
/* Finalize the command mappings, read aliases, etc.  Do this after
 * all other modules have registered their commands.
 */
void modcmd_finalize(void);

#endif /* !defined(MODCMDS_H) */

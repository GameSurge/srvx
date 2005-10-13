/* opserv.c - IRC Operator assistance service
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
#include "gline.h"
#include "global.h"
#include "nickserv.h"
#include "modcmd.h"
#include "opserv.h"
#include "timeq.h"
#include "saxdb.h"

#ifdef HAVE_SYS_TIMES_H
#include <sys/times.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#define OPSERV_CONF_NAME "services/opserv"

#define KEY_ALERT_CHANNEL "alert_channel"
#define KEY_ALERT_CHANNEL_MODES "alert_channel_modes"
#define KEY_DEBUG_CHANNEL "debug_channel"
#define KEY_DEBUG_CHANNEL_MODES "debug_channel_modes"
#define KEY_UNTRUSTED_MAX "untrusted_max"
#define KEY_PURGE_LOCK_DELAY "purge_lock_delay"
#define KEY_JOIN_FLOOD_MODERATE "join_flood_moderate"
#define KEY_JOIN_FLOOD_MODERATE_THRESH "join_flood_moderate_threshold"
#define KEY_NICK "nick"
#define KEY_JOIN_POLICER "join_policer"
#define KEY_NEW_USER_POLICER "new_user_policer"
#define KEY_REASON "reason"
#define KEY_RESERVES "reserves"
#define KEY_IDENT "username" /* for compatibility with 1.0 DBs */
#define KEY_HOSTNAME "hostname"
#define KEY_DESC "description"
#define KEY_BAD_WORDS "bad"
#define KEY_EXEMPT_CHANNELS "exempt"
#define KEY_SECRET_WORDS "secret"
#define KEY_TRUSTED_HOSTS "trusted"
#define KEY_OWNER "owner"
#define KEY_GAGS "gags"
#define KEY_ALERTS "alerts"
#define KEY_REACTION "reaction"
#define KEY_DISCRIM "discrim"
#define KEY_WARN "chanwarn"
#define KEY_MAX "max"
#define KEY_TIME "time"
#define KEY_MAX_CLIENTS "max_clients"
#define KEY_LIMIT "limit"
#define KEY_EXPIRES "expires"
#define KEY_STAFF_AUTH_CHANNEL "staff_auth_channel"
#define KEY_STAFF_AUTH_CHANNEL_MODES "staff_auth_channel_modes"
#define KEY_CLONE_GLINE_DURATION "clone_gline_duration"
#define KEY_BLOCK_GLINE_DURATION "block_gline_duration"
#define KEY_ISSUER "issuer"
#define KEY_ISSUED "issued"

#define IDENT_FORMAT		"%s [%s@%s/%s]"
#define IDENT_DATA(user)	user->nick, user->ident, user->hostname, irc_ntoa(&user->ip)
#define MAX_CHANNELS_WHOIS	50
#define OSMSG_PART_REASON       "%s has no reason."
#define OSMSG_KICK_REQUESTED    "Kick requested by %s."
#define OSMSG_KILL_REQUESTED    "Kill requested by %s."
#define OSMSG_GAG_REQUESTED     "Gag requested by %s."

static const struct message_entry msgtab[] = {
    { "OSMSG_USER_ACCESS_IS", "$b%s$b (account $b%s$b) has %d access." },
    { "OSMSG_LEVEL_TOO_LOW", "You lack sufficient access to use this command." },
    { "OSMSG_NEED_CHANNEL", "You must specify a channel for $b%s$b." },
    { "OSMSG_INVALID_IRCMASK", "$b%s$b is an invalid IRC hostmask." },
    { "OSMSG_ADDED_BAN", "I have banned $b%s$b from $b%s$b." },
    { "OSMSG_GLINE_ISSUED", "G-line issued for $b%s$b." },
    { "OSMSG_GLINE_REMOVED", "G-line removed for $b%s$b." },
    { "OSMSG_GLINE_FORCE_REMOVED", "Unknown/expired G-line removed for $b%s$b." },
    { "OSMSG_GLINES_ONE_REFRESHED", "All G-lines resent to $b%s$b." },
    { "OSMSG_GLINES_REFRESHED", "All G-lines refreshed." },
    { "OSMSG_CLEARBANS_DONE", "Cleared all bans from channel $b%s$b." },
    { "OSMSG_CLEARMODES_DONE", "Cleared all modes from channel $b%s$b." },
    { "OSMSG_NO_CHANNEL_MODES", "Channel $b%s$b had no modes to clear." },
    { "OSMSG_DEOP_DONE", "Deopped the requested lusers." },
    { "OSMSG_DEOPALL_DONE", "Deopped everyone on $b%s$b." },
    { "OSMSG_NO_DEBUG_CHANNEL", "No debug channel has been configured." },
    { "OSMSG_INVITE_DONE", "Invited $b%s$b to $b%s$b." },
    { "OSMSG_ALREADY_THERE", "You are already in $b%s$b." },
    { "OSMSG_JOIN_DONE", "I have joined $b%s$b." },
    { "OSMSG_ALREADY_JOINED", "I am already in $b%s$b." },
    { "OSMSG_NOT_ON_CHANNEL", "$b%s$b does not seem to be on $b%s$b." },
    { "OSMSG_KICKALL_DONE", "I have cleared out %s." },
    { "OSMSG_LEAVING", "Leaving $b%s$b." },
    { "OSMSG_MODE_SET", "I have set the modes for $b%s$b." },
    { "OSMSG_OP_DONE", "Opped the requested lusers." },
    { "OSMSG_OPALL_DONE", "Opped everyone on $b%s$b." },
    { "OSMSG_WHOIS_IDENT", "%s (%s@%s) from %d.%d.%d.%d" },
    { "OSMSG_WHOIS_NICK", "Nick    : %s" },
    { "OSMSG_WHOIS_HOST", "Host    : %s@%s" },
    { "OSMSG_WHOIS_FAKEHOST", "Fakehost: %s" },
    { "OSMSG_WHOIS_IP",   "Real IP : %s" },
    { "OSMSG_WHOIS_MODES", "Modes   : +%s " },
    { "OSMSG_WHOIS_INFO", "Info    : %s" },
    { "OSMSG_WHOIS_NUMERIC", "Numnick : %s" },
    { "OSMSG_WHOIS_SERVER", "Server  : %s" },
    { "OSMSG_WHOIS_NICK_AGE", "Nick Age: %s" },
    { "OSMSG_WHOIS_ACCOUNT", "Account : %s" },
    { "OSMSG_WHOIS_CHANNELS", "Channels: %s" },
    { "OSMSG_WHOIS_HIDECHANS", "Channel list omitted for your sanity." },
    { "OSMSG_UNBAN_DONE", "Ban(s) removed from channel %s." },
    { "OSMSG_CHANNEL_VOICED", "All users on %s voiced." },
    { "OSMSG_CHANNEL_DEVOICED", "All voiced users on %s de-voiced." },
    { "OSMSG_BAD_MODIFIER", "Unknown bad-word modifier $b%s$b." },
    { "OSMSG_BAD_REDUNDANT", "$b%s$b is already covered by a bad word ($b%s$b)." },
    { "OSMSG_BAD_GROWING", "Replacing bad word $b%s$b with shorter bad word $b%s$b." },
    { "OSMSG_BAD_NUKING", " .. and removing redundant bad word $b%s$b." },
    { "OSMSG_ADDED_BAD", "Added $b%s$b to the bad-word list." },
    { "OSMSG_REMOVED_BAD", "Removed $b%s$b from the bad-word list." },
    { "OSMSG_NOT_BAD_WORD", "$b%s$b is not a bad word." },
    { "OSMSG_ADDED_EXEMPTION", "Added $b%s$b to the bad-word exemption list." },
    { "OSMSG_ADDED_EXEMPTIONS", "Added %d exception(s) to the bad word list." },
    { "OSMSG_REMOVED_EXEMPTION", "Removed $b%s$b from the exemption list." },
    { "OSMSG_NOT_EXEMPT", "$b%s$b is not on the exempt list." },
    { "OSMSG_ALREADY_TRUSTED", "Host $b%s$b is already trusted (use $bdeltrust$b and then $baddtrust$b to adjust)." },
    { "OSMSG_NOT_TRUSTED", "Host $b%s$b is not trusted." },
    { "OSMSG_BAD_IP", "$b%s$b is not a valid IP address" },
    { "OSMSG_BAD_NUMBER", "$b%s$b is not a number" },
    { "OSMSG_ADDED_TRUSTED", "Added trusted hosts to the trusted-hosts list." },
    { "OSMSG_UPDATED_TRUSTED", "Updated trusted host $b%s$b." },
    { "OSMSG_REMOVED_TRUSTED", "Removed trusted hosts from the trusted-hosts list." },
    { "OSMSG_CLONE_EXISTS", "Nick $b%s$b is already in use." },
    { "OSMSG_NOT_A_HOSTMASK", "The hostmask must be in user@host form." },
    { "OSMSG_BADWORD_LIST", "Bad words: %s" },
    { "OSMSG_EXEMPTED_LIST", "Exempted channels: %s" },
    { "OSMSG_GLINE_COUNT", "There are %d glines active on the network." },
    { "OSMSG_LINKS_SERVER", "%s%s (%u clients; %s)" },
    { "OSMSG_MAX_CLIENTS", "Max clients: %d at %s" },
    { "OSMSG_NETWORK_INFO", "Total users: %d (%d invisible, %d opers)" },
    { "OSMSG_RESERVED_LIST", "List of reserved nicks:" },
    { "OSMSG_TRUSTED_LIST", "List of trusted hosts:" },
    { "OSMSG_HOST_IS_TRUSTED", "%s (%s; set %s ago by %s; expires %s: %s)" },
    { "OSMSG_HOST_NOT_TRUSTED", "%s does not have a special trust." },
    { "OSMSG_UPTIME_STATS", "Uptime: %s (%u lines processed, CPU time %.2fu/%.2fs)" },
    { "OSMSG_LINE_DUMPED", "Raw line sent." },
    { "OSMSG_RAW_PARSE_ERROR", "Error parsing raw line (not dumping to uplink)." },
    { "OSMSG_COLLIDED_NICK", "Now temporarily holding nick $b%s$b." },
    { "OSMSG_RESERVED_NICK", "Now reserving nick $b%s$b." },
    { "OSMSG_NICK_UNRESERVED", "Nick $b%s$b is no longer reserved." },
    { "OSMSG_NOT_RESERVED", "Nick $b%s$b is not reserved." },
    { "OSMSG_ILLEGAL_REASON", "This channel is illegal." },
    { "OSMSG_ILLEGAL_KILL_REASON", "Joined an illegal modeless channel - do not repeat." },
    { "OSMSG_ILLEGAL_CHANNEL", "$b%s$b is an ILLEGAL channel. Do not re-join it." },
    { "OSMSG_FLOOD_MODERATE", "This channel has been temporarily moderated due to a possible join flood attack detected in this channel; network staff have been notified and will investigate." },
    { "OSMSG_CLONE_WARNING", "WARNING: You have connected the maximum permitted number of clients from one IP address (clones).  If you connect any more, your host will be temporarily banned from the network." },
    { "OSMSG_CLONE_ADDED", "Added clone $b%s$b." },
    { "OSMSG_CLONE_FAILED", "Unable to add user $b%s$b." },
    { "OSMSG_NOT_A_CLONE", "Har har.  $b%s$b isn't a clone." },
    { "OSMSG_CLONE_REMOVED", "Removed clone $b%s$b." },
    { "OSMSG_CLONE_JOINED", "$b%s$b has joined $b%s$b." },
    { "OSMSG_CLONE_PARTED", "$b%s$b has left $b%s$b." },
    { "OSMSG_OPS_GIVEN", "I have given ops in $b%s$b to $b%s$b." },
    { "OSMSG_CLONE_SAID", "$b%s$b has spoken to $b%s$b." },
    { "OSMSG_UNKNOWN_SUBCOMMAND", "$b%s$b is not a valid subcommand of $b%s$b." },
    { "OSMSG_UNKNOWN_OPTION", "$b%s$b has not been set." },
    { "OSMSG_OPTION_IS", "$b%s$b is set to $b%s$b." },
    { "OSMSG_OPTION_ROOT", "The following keys can be queried:" },
    { "OSMSG_OPTION_LIST", "$b%s$b contains the following values:" },
    { "OSMSG_OPTION_KEYS", "$b%s$b contains the following keys:" },
    { "OSMSG_OPTION_LIST_EMPTY", "Empty list." },
    { "OSMSG_SET_NOT_SET", "$b%s$b does not exist, and cannot be set." },
    { "OSMSG_SET_BAD_TYPE", "$b%s$b is not a string, and cannot be set." },
    { "OSMSG_SET_SUCCESS", "$b%s$b has been set to $b%s$b." },
    { "OSMSG_SETTIME_SUCCESS", "Set time for servers named like $b%s$b." },
    { "OSMSG_BAD_ACTION", "Unrecognized trace action $b%s$b." },
    { "OSMSG_USER_SEARCH_RESULTS", "The following users were found:" },
    { "OSMSG_CHANNEL_SEARCH_RESULTS", "The following channels were found:" },
    { "OSMSG_GLINE_SEARCH_RESULTS", "The following glines were found:" },
    { "OSMSG_LOG_SEARCH_RESULTS", "The following log entries were found:" },
    { "OSMSG_GSYNC_RUNNING", "Synchronizing glines from %s." },
    { "OSMSG_GTRACE_FORMAT", "%s (issued %s by %s, expires %s): %s" },
    { "OSMSG_GAG_APPLIED", "Gagged $b%s$b, affecting %d users." },
    { "OSMSG_GAG_ADDED", "Gagged $b%s$b." },
    { "OSMSG_REDUNDANT_GAG", "Gag $b%s$b is redundant." },
    { "OSMSG_GAG_NOT_FOUND", "Could not find gag $b%s$b." },
    { "OSMSG_NO_GAGS", "No gags have been set." },
    { "OSMSG_UNGAG_APPLIED", "Ungagged $b%s$b, affecting %d users." },
    { "OSMSG_UNGAG_ADDED", "Ungagged $b%s$b." },
    { "OSMSG_TIMEQ_INFO", "%u events in timeq; next in %lu seconds." },
    { "OSMSG_ALERT_EXISTS", "An alert named $b%s$b already exists." },
    { "OSMSG_UNKNOWN_REACTION", "Unknown alert reaction $b%s$b." },
    { "OSMSG_ADDED_ALERT", "Added alert named $b%s$b." },
    { "OSMSG_REMOVED_ALERT", "Removed alert named $b%s$b." },
    { "OSMSG_NO_SUCH_ALERT", "No alert named $b%s$b could be found." },
    { "OSMSG_ALERT_IS", "%s (by %s, reaction %s): %s" },
    { "OSMSG_ALERTS_LIST", "Current $O alerts:" },
    { "OSMSG_REHASH_COMPLETE", "Completed rehash of configuration database." },
    { "OSMSG_REHASH_FAILED", "Rehash of configuration database failed, previous configuration is intact." },
    { "OSMSG_REOPEN_COMPLETE", "Closed and reopened all log files." },
    { "OSMSG_RECONNECTING", "Reconnecting to my uplink." },
    { "OSMSG_NUMERIC_COLLIDE", "Numeric %d (%s) is already in use." },
    { "OSMSG_NAME_COLLIDE", "That name is already in use." },
    { "OSMSG_SRV_CREATE_FAILED", "Server creation failed -- check log files." },
    { "OSMSG_SERVER_JUPED", "Added new jupe server %s." },
    { "OSMSG_SERVER_NOT_JUPE", "That server is not a juped server." },
    { "OSMSG_SERVER_UNJUPED", "Server jupe removed." },
    { "OSMSG_WARN_ADDED", "Added channel activity warning for $b%s$b (%s)" },
    { "OSMSG_WARN_EXISTS", "Channel activity warning for $b%s$b already exists." },
    { "OSMSG_WARN_DELETED", "Removed channel activity warning for $b%s$b" },
    { "OSMSG_WARN_NOEXIST", "Channel activity warning for $b%s$b does not exist." },
    { "OSMSG_WARN_LISTSTART", "Channel activity warnings:" },
    { "OSMSG_WARN_LISTENTRY", "%s (%s)" },
    { "OSMSG_WARN_LISTEND", "End of activity warning list." },
    { "OSMSG_UPLINK_CONNECTING", "Establishing connection with %s (%s:%d)." },
    { "OSMSG_CURRENT_UPLINK", "$b%s$b is already the current uplink." },
    { "OSMSG_INVALID_UPLINK", "$b%s$b is not a valid uplink name." },
    { "OSMSG_UPLINK_DISABLED", "$b%s$b is a disabled or unavailable uplink." },
    { "OSMSG_UPLINK_START", "Uplink $b%s$b:" },
    { "OSMSG_UPLINK_ADDRESS", "Address: %s:%d" },
    { "OSMSG_STUPID_GLINE", "Gline %s?  Now $bthat$b would be smooth." },
    { "OSMSG_ACCOUNTMASK_AUTHED", "Invalid criteria: it is impossible to match an account mask but not be authed" },
    { "OSMSG_CHANINFO_HEADER", "%s Information" },
    { "OSMSG_CHANINFO_TIMESTAMP", "Created on: %a %b %d %H:%M:%S %Y (%s)" },
    { "OSMSG_CHANINFO_MODES", "Modes: %s" },
    { "OSMSG_CHANINFO_MODES_BADWORD", "Modes: %s; bad-word channel" },
    { "OSMSG_CHANINFO_TOPIC", "Topic (set by %%s, %a %b %d %H:%M:%S %Y): %%s" },
    { "OSMSG_CHANINFO_TOPIC_UNKNOWN", "Topic: (none / not gathered)" },
    { "OSMSG_CHANINFO_BAN_COUNT", "Bans (%d):" },
    { "OSMSG_CHANINFO_BAN", "%%s by %%s (%a %b %d %H:%M:%S %Y)" },
    { "OSMSG_CHANINFO_MANY_USERS", "%d users (\"/msg $S %s %s users\" for the list)" },
    { "OSMSG_CHANINFO_USER_COUNT", "Users (%d):" },
    { "OSMSG_CSEARCH_CHANNEL_INFO", "%s [%d users] %s %s" },
    { NULL, NULL }
};

#define OPSERV_SYNTAX() svccmd_send_help(user, opserv, cmd)

typedef int (*discrim_search_func)(struct userNode *match, void *extra);

struct userNode *opserv;

static dict_t opserv_chan_warn; /* data is char* */
static dict_t opserv_reserved_nick_dict; /* data is struct userNode* */
static struct string_list *opserv_bad_words;
static dict_t opserv_exempt_channels; /* data is not used */
static dict_t opserv_trusted_hosts; /* data is struct trusted_host* */
static dict_t opserv_hostinfo_dict; /* data is struct opserv_hostinfo* */
static dict_t opserv_user_alerts; /* data is struct opserv_user_alert* */
static dict_t opserv_nick_based_alerts; /* data is struct opserv_user_alert* */
static dict_t opserv_channel_alerts; /* data is struct opserv_user_alert* */
static struct module *opserv_module;
static struct log_type *OS_LOG;
static unsigned int new_user_flood;
static char *level_strings[1001];

static struct {
    struct chanNode *debug_channel;
    struct chanNode *alert_channel;
    struct chanNode *staff_auth_channel;
    struct policer_params *join_policer_params;
    struct policer new_user_policer;
    unsigned long untrusted_max;
    unsigned long clone_gline_duration;
    unsigned long block_gline_duration;
    unsigned long purge_lock_delay;
    unsigned long join_flood_moderate;
    unsigned long join_flood_moderate_threshold;
} opserv_conf;

struct trusted_host {
    char *ipaddr;
    char *issuer;
    char *reason;
    unsigned long limit;
    time_t issued;
    time_t expires;
};

struct gag_entry {
    char *mask;
    char *owner;
    char *reason;
    time_t expires;
    struct gag_entry *next;
};

static struct gag_entry *gagList;

struct opserv_hostinfo {
    struct userList clients;
    struct trusted_host *trusted;
};

static void
opserv_free_hostinfo(void *data)
{
    struct opserv_hostinfo *ohi = data;
    userList_clean(&ohi->clients);
    free(ohi);
}

typedef struct opservDiscrim {
    struct chanNode *channel;
    char *mask_nick, *mask_ident, *mask_host, *mask_info, *server, *reason, *accountmask;
    irc_in_addr_t ip_mask;
    unsigned long limit;
    time_t min_ts, max_ts;
    unsigned int min_level, max_level, domain_depth, duration, min_clones, min_channels, max_channels;
    unsigned char ip_mask_bits;
    unsigned int match_opers : 1, option_log : 1;
    unsigned int chan_req_modes : 2, chan_no_modes : 2;
    int authed : 2, info_space : 2;
} *discrim_t;

struct discrim_and_source {
    discrim_t discrim;
    struct userNode *source;
    dict_t dict;
    unsigned int disp_limit;
};

static discrim_t opserv_discrim_create(struct userNode *user, unsigned int argc, char *argv[], int allow_channel);
static unsigned int opserv_discrim_search(discrim_t discrim, discrim_search_func dsf, void *data);
static int gag_helper_func(struct userNode *match, void *extra);
static int ungag_helper_func(struct userNode *match, void *extra);

typedef enum {
    REACT_NOTICE,
    REACT_KILL,
    REACT_GLINE
} opserv_alert_reaction;

struct opserv_user_alert {
    char *owner;
    char *text_discrim, *split_discrim;
    discrim_t discrim;
    opserv_alert_reaction reaction;
};

/* funny type to make it acceptible to dict_set_free_data, far below */
static void
opserv_free_user_alert(void *data)
{
    struct opserv_user_alert *alert = data;
    if (alert->discrim->channel)
        UnlockChannel(alert->discrim->channel);
    free(alert->owner);
    free(alert->text_discrim);
    free(alert->split_discrim);
    free(alert->discrim->reason);
    free(alert->discrim);
    free(alert);
}

#define opserv_debug(format...) do { if (opserv_conf.debug_channel) send_channel_notice(opserv_conf.debug_channel , opserv , ## format); } while (0)
#define opserv_alert(format...) do { if (opserv_conf.alert_channel) send_channel_notice(opserv_conf.alert_channel , opserv , ## format); } while (0)

/* A lot of these commands are very similar to what ChanServ can do,
 * but OpServ can do them even on channels that aren't registered.
 */

static MODCMD_FUNC(cmd_access)
{
    struct handle_info *hi;
    const char *target;
    unsigned int res;

    target = (argc > 1) ? (const char*)argv[1] : user->nick;
    if (!irccasecmp(target, "*")) {
        nickserv_show_oper_accounts(user, cmd);
        return 1;
    }
    if (!(hi = modcmd_get_handle_info(user, target)))
        return 0;
    res = (argc > 2) ? oper_try_set_access(user, cmd->parent->bot, hi, strtoul(argv[2], NULL, 0)) : 0;
    reply("OSMSG_USER_ACCESS_IS", target, hi->handle, hi->opserv_level);
    return res;
}

static MODCMD_FUNC(cmd_ban)
{
    struct mod_chanmode change;
    struct userNode *victim;

    mod_chanmode_init(&change);
    change.argc = 1;
    change.args[0].mode = MODE_BAN;
    if (is_ircmask(argv[1]))
        change.args[0].u.hostmask = strdup(argv[1]);
    else if ((victim = GetUserH(argv[1])))
        change.args[0].u.hostmask = generate_hostmask(victim, 0);
    else {
	reply("OSMSG_INVALID_IRCMASK", argv[1]);
	return 0;
    }
    modcmd_chanmode_announce(&change);
    reply("OSMSG_ADDED_BAN", change.args[0].u.hostmask, channel->name);
    free((char*)change.args[0].u.hostmask);
    return 1;
}

static MODCMD_FUNC(cmd_chaninfo)
{
    char buffer[MAXLEN];
    const char *fmt;
    struct banNode *ban;
    struct modeNode *moden;
    unsigned int n;

    reply("OSMSG_CHANINFO_HEADER", channel->name);
    fmt = user_find_message(user, "OSMSG_CHANINFO_TIMESTAMP");
    strftime(buffer, sizeof(buffer), fmt, gmtime(&channel->timestamp));
    send_message_type(4, user, cmd->parent->bot, "%s", buffer);
    irc_make_chanmode(channel, buffer);
    if (channel->bad_channel)
        reply("OSMSG_CHANINFO_MODES_BADWORD", buffer);
    else
        reply("OSMSG_CHANINFO_MODES", buffer);
    if (channel->topic_time) {
        fmt = user_find_message(user, "OSMSG_CHANINFO_TOPIC");
        strftime(buffer, sizeof(buffer), fmt, gmtime(&channel->topic_time));
        send_message_type(4, user, cmd->parent->bot, buffer, channel->topic_nick, channel->topic);
    } else {
	irc_fetchtopic(cmd->parent->bot, channel->name);
	reply("OSMSG_CHANINFO_TOPIC_UNKNOWN");
    }
    if (channel->banlist.used) {
	reply("OSMSG_CHANINFO_BAN_COUNT", channel->banlist.used);
        fmt = user_find_message(user, "OSMSG_CHANINFO_BAN");
	for (n = 0; n < channel->banlist.used; n++) {
    	    ban = channel->banlist.list[n];
	    strftime(buffer, sizeof(buffer), fmt, localtime(&ban->set));
	    send_message_type(4, user, cmd->parent->bot, buffer, ban->ban, ban->who);
	}
    }
    if ((argc < 2) && (channel->members.used >= 50)) {
        /* early out unless they ask for users */
        reply("OSMSG_CHANINFO_MANY_USERS", channel->members.used, argv[0], channel->name);
        return 1;
    }
    reply("OSMSG_CHANINFO_USER_COUNT", channel->members.used);
    for (n=0; n<channel->members.used; n++) {
	    moden = channel->members.list[n];
	    if (moden->modes & MODE_CHANOP)
        {
                if (moden->oplevel >= 0)
                {
                    send_message_type(4, user, cmd->parent->bot, " (%d)@%s (%s@%s)", moden->oplevel, moden->user->nick, moden->user->ident, moden->user->hostname);
                } else {
                    send_message_type(4, user, cmd->parent->bot, " @%s (%s@%s)", moden->user->nick, moden->user->ident, moden->user->hostname);
                }
        }
    }
    for (n=0; n<channel->members.used; n++) {
	moden = channel->members.list[n];
	if ((moden->modes & (MODE_CHANOP|MODE_VOICE)) == MODE_VOICE)
            send_message_type(4, user, cmd->parent->bot, " +%s (%s@%s)", moden->user->nick, moden->user->ident, moden->user->hostname);
    }
    for (n=0; n<channel->members.used; n++) {
	moden = channel->members.list[n];
	if ((moden->modes & (MODE_CHANOP|MODE_VOICE)) == 0)
            send_message_type(4, user, cmd->parent->bot, "  %s (%s@%s)", moden->user->nick, moden->user->ident, moden->user->hostname);
    }
    return 1;
}

static MODCMD_FUNC(cmd_warn) 
{
    char *reason, *message;

    if (!IsChannelName(argv[1])) {
	reply("OSMSG_NEED_CHANNEL", argv[0]);
	return 0;
    }
    reason = dict_find(opserv_chan_warn, argv[1], NULL);
    if (reason) {
        reply("OSMSG_WARN_EXISTS", argv[1]);
        return 0;
    }
    if (argv[2])
        reason = strdup(unsplit_string(argv+2, argc-2, NULL));
    else
        reason = strdup("No reason");
    dict_insert(opserv_chan_warn, strdup(argv[1]), reason);
    reply("OSMSG_WARN_ADDED", argv[1], reason);
    if (dict_find(channels, argv[1], NULL)) {
        message = alloca(strlen(reason) + strlen(argv[1]) + 55);
        sprintf(message, "Channel activity warning for channel %s: %s", argv[1], reason);
        global_message(MESSAGE_RECIPIENT_OPERS, message);
    }
    return 1;
}

static MODCMD_FUNC(cmd_unwarn)
{
    if ((argc < 2) || !IsChannelName(argv[1])) {
        reply("OSMSG_NEED_CHANNEL", argv[0]);
	return 0;
    }
    if (!dict_remove(opserv_chan_warn, argv[1])) {
        reply("OSMSG_WARN_NOEXIST", argv[1]);
        return 0;
    }
    reply("OSMSG_WARN_DELETED", argv[1]);
    return 1;
}

static MODCMD_FUNC(cmd_clearbans)
{
    struct mod_chanmode *change;
    unsigned int ii;

    change = mod_chanmode_alloc(channel->banlist.used);
    for (ii=0; ii<channel->banlist.used; ii++) {
        change->args[ii].mode = MODE_REMOVE | MODE_BAN;
        change->args[ii].u.hostmask = strdup(channel->banlist.list[ii]->ban);
    }
    modcmd_chanmode_announce(change);
    for (ii=0; ii<change->argc; ++ii)
        free((char*)change->args[ii].u.hostmask);
    mod_chanmode_free(change);
    reply("OSMSG_CLEARBANS_DONE", channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_clearmodes)
{
    struct mod_chanmode change;

    if (!channel->modes) {
	reply("OSMSG_NO_CHANNEL_MODES", channel->name);
        return 0;
    }
    mod_chanmode_init(&change);
    change.modes_clear = channel->modes;
    modcmd_chanmode_announce(&change);
    reply("OSMSG_CLEARMODES_DONE", channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_deop)
{
    struct mod_chanmode *change;
    unsigned int arg, count;

    change = mod_chanmode_alloc(argc-1);
    for (arg = 1, count = 0; arg < argc; ++arg) {
        struct userNode *victim = GetUserH(argv[arg]);
        struct modeNode *mn;
	if (!victim || IsService(victim)
            || !(mn = GetUserMode(channel, victim))
            || !(mn->modes & MODE_CHANOP))
            continue;
        change->args[count].mode = MODE_REMOVE | MODE_CHANOP;
        change->args[count++].u.member = mn;
    }
    if (count) {
        change->argc = count;
        modcmd_chanmode_announce(change);
    }
    mod_chanmode_free(change);
    reply("OSMSG_DEOP_DONE");
    return 1;
}

static MODCMD_FUNC(cmd_deopall)
{
    struct mod_chanmode *change;
    unsigned int ii, count;

    change = mod_chanmode_alloc(channel->members.used);
    for (ii = count = 0; ii < channel->members.used; ++ii) {
	struct modeNode *mn = channel->members.list[ii];
	if (IsService(mn->user) || !(mn->modes & MODE_CHANOP))
            continue;
        change->args[count].mode = MODE_REMOVE | MODE_CHANOP;
        change->args[count++].u.member = mn;
    }
    if (count) {
        change->argc = count;
        modcmd_chanmode_announce(change);
    }
    mod_chanmode_free(change);
    reply("OSMSG_DEOPALL_DONE", channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_rehash)
{
    extern char *services_config;

    if (conf_read(services_config))
	reply("OSMSG_REHASH_COMPLETE");
    else
	reply("OSMSG_REHASH_FAILED");
    return 1;
}

static MODCMD_FUNC(cmd_reopen)
{
    log_reopen();
    reply("OSMSG_REOPEN_COMPLETE");
    return 1;
}

static MODCMD_FUNC(cmd_reconnect)
{
    reply("OSMSG_RECONNECTING");
    irc_squit(self, "Reconnecting.", NULL);
    return 1;
}

static MODCMD_FUNC(cmd_jupe)
{
    extern int force_n2k;
    struct server *newsrv;
    unsigned int num;
    char numeric[COMBO_NUMERIC_LEN+1], srvdesc[SERVERDESCRIPTMAX+1];

    num = atoi(argv[2]);
    if ((num < 64) && !force_n2k) {
        inttobase64(numeric, num, 1);
        inttobase64(numeric+1, 64*64-1, 2);
    } else {
        inttobase64(numeric, num, 2);
        inttobase64(numeric+2, 64*64*64-1, 3);
    }
#ifdef WITH_PROTOCOL_P10
    if (GetServerN(numeric)) {
        reply("OSMSG_NUMERIC_COLLIDE", num, numeric);
        return 0;
    }
#endif
    if (GetServerH(argv[1])) {
        reply("OSMSG_NAME_COLLIDE");
        return 0;
    }
    snprintf(srvdesc, sizeof(srvdesc), "JUPE %s", unsplit_string(argv+3, argc-3, NULL));
    newsrv = AddServer(self, argv[1], 1, now, now, numeric, srvdesc);
    if (!newsrv) {
        reply("OSMSG_SRV_CREATE_FAILED");
        return 0;
    }
    irc_server(newsrv);
    reply("OSMSG_SERVER_JUPED", argv[1]);
    return 1;
}

static MODCMD_FUNC(cmd_unjupe)
{
    struct server *srv;
    char *reason;

    srv = GetServerH(argv[1]);
    if (!srv) {
        reply("MSG_SERVER_UNKNOWN", argv[1]);
        return 0;
    }
    if (strncmp(srv->description, "JUPE", 4)) {
        reply("OSMSG_SERVER_NOT_JUPE");
        return 0;
    }
    reason = (argc > 2) ? unsplit_string(argv+2, argc-2, NULL) : "Unjuping server";
    DelServer(srv, 1, reason);
    reply("OSMSG_SERVER_UNJUPED");
    return 1;
}

static MODCMD_FUNC(cmd_jump)
{
    extern struct cManagerNode cManager;
    void uplink_select(char *name);
    struct uplinkNode *uplink_find(char *name);
    struct uplinkNode *uplink;
    char *target;

    target = unsplit_string(argv+1, argc-1, NULL);

    if (!strcmp(cManager.uplink->name, target)) {
	reply("OSMSG_CURRENT_UPLINK", cManager.uplink->name);
	return 0;
    }

    uplink = uplink_find(target);
    if (!uplink) {
	reply("OSMSG_INVALID_UPLINK", target);
	return 0;
    }
    if (uplink->flags & UPLINK_UNAVAILABLE) {
        reply("OSMSG_UPLINK_DISABLED", uplink->name);
        return 0;
    }

    reply("OSMSG_UPLINK_CONNECTING", uplink->name, uplink->host, uplink->port);
    uplink_select(target);
    irc_squit(self, "Reconnecting.", NULL);
    return 1;
}

static MODCMD_FUNC(cmd_die)
{
    char *reason, *text;

    text = unsplit_string(argv+1, argc-1, NULL);
    reason = alloca(strlen(text) + strlen(user->nick) + 20);
    sprintf(reason, "Disconnected by %s [%s]", user->nick, text);
    irc_squit(self, reason, text);
    quit_services = 1;
    return 1;
}

static MODCMD_FUNC(cmd_restart)
{
    extern int services_argc;
    extern char **services_argv;
    char **restart_argv, *reason, *text;

    text = unsplit_string(argv+1, argc-1, NULL);
    reason = alloca(strlen(text) + strlen(user->nick) + 17);
    sprintf(reason, "Restarted by %s [%s]", user->nick, text);
    irc_squit(self, reason, text);

    /* Append a NULL to the end of argv[]. */
    restart_argv = (char **)alloca((services_argc + 1) * sizeof(char *));
    memcpy(restart_argv, services_argv, services_argc * sizeof(char *));
    restart_argv[services_argc] = NULL;

    call_exit_funcs();

    /* Don't blink. */
    execv(services_argv[0], restart_argv);

    /* If we're still here, that means something went wrong. Reconnect. */
    return 1;
}

static struct gline *
opserv_block(struct userNode *target, char *src_handle, char *reason, unsigned long duration)
{
    char mask[IRC_NTOP_MAX_SIZE+3] = { '*', '@', '\0' };
    irc_ntop(mask + 2, sizeof(mask) - 2, &target->ip);
    if (!reason)
        snprintf(reason = alloca(MAXLEN), MAXLEN,
                 "G-line requested by %s.", src_handle);
    if (!duration)
        duration = opserv_conf.block_gline_duration;
    return gline_add(src_handle, mask, duration, reason, now, 1);
}

static MODCMD_FUNC(cmd_block)
{
    struct userNode *target;
    struct gline *gline;
    char *reason;

    target = GetUserH(argv[1]);
    if (!target) {
	reply("MSG_NICK_UNKNOWN", argv[1]);
	return 0;
    }
    if (IsService(target)) {
	reply("MSG_SERVICE_IMMUNE", target->nick);
	return 0;
    }
    reason = (argc > 2) ? unsplit_string(argv+2, argc-2, NULL) : NULL;
    gline = opserv_block(target, user->handle_info->handle, reason, 0);
    reply("OSMSG_GLINE_ISSUED", gline->target);
    return 1;
}

static MODCMD_FUNC(cmd_gline)
{
    unsigned long duration;
    char *reason;
    struct gline *gline;

    reason = unsplit_string(argv+3, argc-3, NULL);
    if (!is_gline(argv[1]) && !IsChannelName(argv[1]) && (argv[1][0] != '&')) {
	reply("MSG_INVALID_GLINE", argv[1]);
	return 0;
    }
    if (!argv[1][strspn(argv[1], "#&*?@.")] && (strlen(argv[1]) < 10)) {
        reply("OSMSG_STUPID_GLINE", argv[1]);
        return 0;
    }
    duration = ParseInterval(argv[2]);
    if (!duration) {
        reply("MSG_INVALID_DURATION", argv[2]);
        return 0;
    }
    gline = gline_add(user->handle_info->handle, argv[1], duration, reason, now, 1);
    reply("OSMSG_GLINE_ISSUED", gline->target);
    return 1;
}

static MODCMD_FUNC(cmd_ungline)
{
    if (gline_remove(argv[1], 1))
        reply("OSMSG_GLINE_REMOVED", argv[1]);
    else
        reply("OSMSG_GLINE_FORCE_REMOVED", argv[1]);
    return 1;
}

static MODCMD_FUNC(cmd_refreshg)
{
    if (argc > 1) {
        unsigned int count;
        dict_iterator_t it;
        struct server *srv;

        for (it=dict_first(servers), count=0; it; it=iter_next(it)) {
            srv = iter_data(it);
            if ((srv == self) || !match_ircglob(srv->name, argv[1]))
                continue;
            gline_refresh_server(srv);
            reply("OSMSG_GLINES_ONE_REFRESHED", srv->name);
            count++;
        }
        if (!count) {
            reply("MSG_SERVER_UNKNOWN", argv[1]);
            return 0;
        }
    } else {
        gline_refresh_all();
        reply("OSMSG_GLINES_REFRESHED");
    }
    return 1;
}

static void
opserv_ison(struct userNode *tell, struct userNode *target, const char *message)
{
    struct modeNode *mn;
    unsigned int count, here_len, n, maxlen;
    char buff[MAXLEN];

    maxlen = tell->handle_info ? tell->handle_info->screen_width : 0;
    if (!maxlen)
        maxlen = MAX_LINE_SIZE;
    for (n=count=0; n<target->channels.used; n++) {
	mn = target->channels.list[n];
	here_len = strlen(mn->channel->name);
	if ((count + here_len + 4) > maxlen) {
	    buff[count] = 0;
            send_message(tell, opserv, message, buff);
	    count = 0;
	}
	if (mn->modes & MODE_CHANOP)
            buff[count++] = '@';
	if (mn->modes & MODE_VOICE)
            buff[count++] = '+';
	memcpy(buff+count, mn->channel->name, here_len);
	count += here_len;
	buff[count++] = ' ';
    }
    if (count) {
	buff[count] = 0;
	send_message(tell, opserv, message, buff);
    }
}

static MODCMD_FUNC(cmd_inviteme)
{
    struct userNode *target;

    if (argc < 2) {
	target = user;
    } else {
	target = GetUserH(argv[1]);
	if (!target) {
	    reply("MSG_NICK_UNKNOWN", argv[1]);
	    return 0;
	}
    }
    if (opserv_conf.debug_channel == NULL) {
	reply("OSMSG_NO_DEBUG_CHANNEL");
	return 0;
    }
    if (GetUserMode(opserv_conf.debug_channel, user)) {
        reply("OSMSG_ALREADY_THERE", opserv_conf.debug_channel->name);
        return 0;
    }
    irc_invite(cmd->parent->bot, target, opserv_conf.debug_channel);
    if (target != user)
	reply("OSMSG_INVITE_DONE", target->nick, opserv_conf.debug_channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_invite)
{
    if (GetUserMode(channel, user)) {
        reply("OSMSG_ALREADY_THERE", channel->name);
        return 0;
    }
    irc_invite(cmd->parent->bot, user, channel);
    return 1;
}

static MODCMD_FUNC(cmd_join)
{
    struct userNode *bot = cmd->parent->bot;

    if (!IsChannelName(argv[1])) {
        reply("MSG_NOT_CHANNEL_NAME");
        return 0;
    } else if (!(channel = GetChannel(argv[1]))) {
        channel = AddChannel(argv[1], now, NULL, NULL);
        AddChannelUser(bot, channel)->modes |= MODE_CHANOP;
    } else if (GetUserMode(channel, bot)) {
        reply("OSMSG_ALREADY_JOINED", channel->name);
        return 0;
    } else {
        struct mod_chanmode change;
        mod_chanmode_init(&change);
        change.argc = 1;
        change.args[0].mode = MODE_CHANOP;
        change.args[0].u.member = AddChannelUser(bot, channel);
        modcmd_chanmode_announce(&change);
    }
    irc_fetchtopic(bot, channel->name);
    reply("OSMSG_JOIN_DONE", channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_kick)
{
    struct userNode *target;
    char *reason;

    if (argc < 3) {
	reason = alloca(strlen(OSMSG_KICK_REQUESTED)+strlen(user->nick)+1);
	sprintf(reason, OSMSG_KICK_REQUESTED, user->nick);
    } else {
	reason = unsplit_string(argv+2, argc-2, NULL);
    }
    target = GetUserH(argv[1]);
    if (!target) {
	reply("MSG_NICK_UNKNOWN", argv[1]);
	return 0;
    }
    if (!GetUserMode(channel, target)) {
	reply("OSMSG_NOT_ON_CHANNEL", target->nick, channel->name);
	return 0;
    }
    KickChannelUser(target, channel, cmd->parent->bot, reason);
    return 1;
}

static MODCMD_FUNC(cmd_kickall)
{
    unsigned int limit, n, inchan;
    struct modeNode *mn;
    char *reason;
    struct userNode *bot = cmd->parent->bot;

    /* ircu doesn't let servers KICK users, so if OpServ's not in the
     * channel, we have to join it in temporarily. */
    if (!(inchan = GetUserMode(channel, bot) ? 1 : 0)) {
        struct mod_chanmode change;
        mod_chanmode_init(&change);
        change.args[0].mode = MODE_CHANOP;
        change.args[0].u.member = AddChannelUser(bot, channel);
        modcmd_chanmode_announce(&change);
    }
    if (argc < 2) {
	reason = alloca(strlen(OSMSG_KICK_REQUESTED)+strlen(user->nick)+1);
	sprintf(reason, OSMSG_KICK_REQUESTED, user->nick);
    } else {
	reason = unsplit_string(argv+1, argc-1, NULL);
    }
    limit = user->handle_info->opserv_level;
    for (n=channel->members.used; n>0;) {
	mn = channel->members.list[--n];
	if (IsService(mn->user)
	    || (mn->user->handle_info
		&& (mn->user->handle_info->opserv_level >= limit))) {
	    continue;
	}
	KickChannelUser(mn->user, channel, bot, reason);
    }
    if (!inchan)
        DelChannelUser(bot, channel, "My work here is done", 0);
    reply("OSMSG_KICKALL_DONE", channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_kickban)
{
    struct mod_chanmode change;
    struct userNode *target;
    char *reason;
    char *mask;

    if (argc == 2) {
	reason = alloca(strlen(OSMSG_KICK_REQUESTED)+strlen(user->nick)+1);
	sprintf(reason, OSMSG_KICK_REQUESTED, user->nick);
    } else {
	reason = unsplit_string(argv+2, argc-2, NULL);
    }
    target = GetUserH(argv[1]);
    if (!target) {
	reply("MSG_NICK_UNKNOWN", argv[1]);
	return 0;
    }
    if (!GetUserMode(channel, target)) {
	reply("OSMSG_NOT_ON_CHANNEL", target->nick, channel->name);
	return 0;
    }
    mod_chanmode_init(&change);
    change.argc = 1;
    change.args[0].mode = MODE_BAN;
    change.args[0].u.hostmask = mask = generate_hostmask(target, 0);
    modcmd_chanmode_announce(&change);
    KickChannelUser(target, channel, cmd->parent->bot, reason);
    free(mask);
    return 1;
}

static MODCMD_FUNC(cmd_kickbanall)
{
    struct modeNode *mn;
    struct userNode *bot = cmd->parent->bot;
    struct mod_chanmode *change;
    char *reason;
    unsigned int limit, n, inchan;

    /* ircu doesn't let servers KICK users, so if OpServ's not in the
     * channel, we have to join it in temporarily. */
    if (!(inchan = GetUserMode(channel, bot) ? 1 : 0)) {
        change = mod_chanmode_alloc(2);
        change->args[0].mode = MODE_CHANOP;
        change->args[0].u.member = AddChannelUser(bot, channel);
        change->args[1].mode = MODE_BAN;
        change->args[1].u.hostmask = "*!*@*";
    } else {
        change = mod_chanmode_alloc(1);
        change->args[0].mode = MODE_BAN;
        change->args[0].u.hostmask = "*!*@*";
    }
    modcmd_chanmode_announce(change);
    mod_chanmode_free(change);
    if (argc < 2) {
	reason = alloca(strlen(OSMSG_KICK_REQUESTED)+strlen(user->nick)+1);
	sprintf(reason, OSMSG_KICK_REQUESTED, user->nick);
    } else {
	reason = unsplit_string(argv+1, argc-1, NULL);
    }
    /* now kick them */
    limit = user->handle_info->opserv_level;
    for (n=channel->members.used; n>0; ) {
	mn = channel->members.list[--n];
	if (IsService(mn->user)
	    || (mn->user->handle_info
		&& (mn->user->handle_info->opserv_level >= limit))) {
	    continue;
	}
	KickChannelUser(mn->user, channel, bot, reason);
    }
    if (!inchan)
        DelChannelUser(bot, channel, "My work here is done", 0);
    reply("OSMSG_KICKALL_DONE", channel->name);
    return 1;    
}

static MODCMD_FUNC(cmd_part)
{
    char *reason;

    if (!IsChannelName(argv[1])) {
        reply("MSG_NOT_CHANNEL_NAME");
        return 0;
    }
    if ((channel = GetChannel(argv[1]))) {
        if (!GetUserMode(channel, cmd->parent->bot)) {
            reply("OSMSG_NOT_ON_CHANNEL", cmd->parent->bot->nick, channel->name);
            return 0;
        }
        reason = (argc < 3) ? "Leaving." : unsplit_string(argv+2, argc-2, NULL);
        reply("OSMSG_LEAVING", channel->name);
        DelChannelUser(cmd->parent->bot, channel, reason, 0);
    }
    return 1;
}

static MODCMD_FUNC(cmd_mode)
{
    if (!modcmd_chanmode(argv+1, argc-1, MCP_ALLOW_OVB|MCP_KEY_FREE|MC_ANNOUNCE)) {
        reply("MSG_INVALID_MODES", unsplit_string(argv+1, argc-1, NULL));
        return 0;
    }
    reply("OSMSG_MODE_SET", channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_op)
{
    struct mod_chanmode *change;
    unsigned int arg, count;

    change = mod_chanmode_alloc(argc-1);
    for (arg = 1, count = 0; arg < argc; ++arg) {
        struct userNode *victim;
        struct modeNode *mn;
        if (!(victim = GetUserH(argv[arg])))
            continue;
        if (!(mn =  GetUserMode(channel, victim)))
            continue;
        if (mn->modes & MODE_CHANOP)
            continue;
        change->args[count].mode = MODE_CHANOP;
        change->args[count++].u.member = mn;
    }
    if (count) {
        change->argc = count;
        modcmd_chanmode_announce(change);
    }
    mod_chanmode_free(change);
    reply("OSMSG_OP_DONE");
    return 1;
}

static MODCMD_FUNC(cmd_opall)
{
    struct mod_chanmode *change;
    unsigned int ii, count;

    change = mod_chanmode_alloc(channel->members.used);
    for (ii = count = 0; ii < channel->members.used; ++ii) {
	struct modeNode *mn = channel->members.list[ii];
	if (mn->modes & MODE_CHANOP)
            continue;
        change->args[count].mode = MODE_CHANOP;
        change->args[count++].u.member = mn;
    }
    if (count) {
        change->argc = count;
	modcmd_chanmode_announce(change);
    }
    mod_chanmode_free(change);
    reply("OSMSG_OPALL_DONE", channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_whois)
{
    struct userNode *target;
    char buffer[128];
    int bpos, herelen;

#ifdef WITH_PROTOCOL_P10
    if (argv[1][0] == '*')
        target = GetUserN(argv[1]+1);
    else
#endif
    target = GetUserH(argv[1]);
    if (!target) {
        reply("MSG_NICK_UNKNOWN", argv[1]);
        return 0;
    }
    reply("OSMSG_WHOIS_NICK", target->nick);
    reply("OSMSG_WHOIS_HOST", target->ident, target->hostname);
    if (IsFakeHost(target))
        reply("OSMSG_WHOIS_FAKEHOST", target->fakehost);
    reply("OSMSG_WHOIS_IP", irc_ntoa(&target->ip));
    if (target->modes) {
	bpos = 0;
#define buffer_cat(str) (herelen = strlen(str), memcpy(buffer+bpos, str, herelen), bpos += herelen)
	if (IsInvisible(target)) buffer[bpos++] = 'i';
	if (IsWallOp(target)) buffer[bpos++] = 'w';
	if (IsOper(target)) buffer[bpos++] = 'o';
	if (IsGlobal(target)) buffer[bpos++] = 'g';
	if (IsServNotice(target)) buffer[bpos++] = 's';
	if (IsHelperIrcu(target)) buffer[bpos++] = 'h';
	if (IsService(target)) buffer[bpos++] = 'k';
	if (IsDeaf(target)) buffer[bpos++] = 'd';
        if (IsHiddenHost(target)) buffer[bpos++] = 'x';
        if (IsGagged(target)) buffer_cat(" (gagged)");
	if (IsRegistering(target)) buffer_cat(" (registered account)");
	buffer[bpos] = 0;
	if (bpos > 0)
            reply("OSMSG_WHOIS_MODES", buffer);
    }
    reply("OSMSG_WHOIS_INFO", target->info);
#ifdef WITH_PROTOCOL_P10
    reply("OSMSG_WHOIS_NUMERIC", target->numeric);
#endif
    reply("OSMSG_WHOIS_SERVER", target->uplink->name);
    reply("OSMSG_WHOIS_ACCOUNT", (target->handle_info ? target->handle_info->handle : "Not authenticated"));
    intervalString(buffer, now - target->timestamp, user->handle_info);
    reply("OSMSG_WHOIS_NICK_AGE", buffer);
    if (target->channels.used <= MAX_CHANNELS_WHOIS)
	opserv_ison(user, target, "OSMSG_WHOIS_CHANNELS");
    else
	reply("OSMSG_WHOIS_HIDECHANS");
    return 1;
}

static MODCMD_FUNC(cmd_unban)
{
    struct mod_chanmode change;
    mod_chanmode_init(&change);
    change.argc = 1;
    change.args[0].mode = MODE_REMOVE | MODE_BAN;
    change.args[0].u.hostmask = argv[1];
    modcmd_chanmode_announce(&change);
    reply("OSMSG_UNBAN_DONE", channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_voiceall)
{
    struct mod_chanmode *change;
    unsigned int ii, count;

    change = mod_chanmode_alloc(channel->members.used);
    for (ii = count = 0; ii < channel->members.used; ++ii) {
	struct modeNode *mn = channel->members.list[ii];
	if (mn->modes & (MODE_CHANOP|MODE_VOICE))
            continue;
        change->args[count].mode = MODE_VOICE;
        change->args[count++].u.member = mn;
    }
    if (count) {
        change->argc = count;
	modcmd_chanmode_announce(change);
    }
    mod_chanmode_free(change);
    reply("OSMSG_CHANNEL_VOICED", channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_devoiceall)
{
    struct mod_chanmode *change;
    unsigned int ii, count;

    change = mod_chanmode_alloc(channel->members.used);
    for (ii = count = 0; ii < channel->members.used; ++ii) {
	struct modeNode *mn = channel->members.list[ii];
	if (!(mn->modes & MODE_VOICE))
            continue;
        change->args[count].mode = MODE_REMOVE | MODE_VOICE;
        change->args[count++].u.member = mn;
    }
    if (count) {
        change->argc = count;
	modcmd_chanmode_announce(change);
    }
    mod_chanmode_free(change);
    reply("OSMSG_CHANNEL_DEVOICED", channel->name);
    return 1;
}

static MODCMD_FUNC(cmd_stats_bad) {
    dict_iterator_t it;
    unsigned int ii, end, here_len;
    char buffer[400];

    /* Show the bad word list.. */
    for (ii=end=0; ii<opserv_bad_words->used; ii++) {
        here_len = strlen(opserv_bad_words->list[ii]);
        if ((end + here_len + 2) > sizeof(buffer)) {
            buffer[end] = 0;
            reply("OSMSG_BADWORD_LIST", buffer);
            end = 0;
        }
        memcpy(buffer+end, opserv_bad_words->list[ii], here_len);
        end += here_len;
        buffer[end++] = ' ';
    }
    buffer[end] = 0;
    reply("OSMSG_BADWORD_LIST", buffer);

    /* Show the exemption list.. */
    for (it=dict_first(opserv_exempt_channels), end=0; it; it=iter_next(it)) {
        here_len = strlen(iter_key(it));
        if ((end + here_len + 2) > sizeof(buffer)) {
            buffer[end] = 0;
            reply("OSMSG_EXEMPTED_LIST", buffer);
            end = 0;
        }
        memcpy(buffer+end, iter_key(it), here_len);
        end += here_len;
        buffer[end++] = ' ';
    }
    buffer[end] = 0;
    reply("OSMSG_EXEMPTED_LIST", buffer);
    return 1;
}

static MODCMD_FUNC(cmd_stats_glines) {
    reply("OSMSG_GLINE_COUNT", gline_count());
    return 1;
}

static void
trace_links(struct userNode *bot, struct userNode *user, struct server *server, unsigned int depth) {
    unsigned int nn, pos;
    char buffer[400];

    for (nn=1; nn<=depth; nn<<=1) ;
    for (pos=0, nn>>=1; nn>1; ) {
        nn >>= 1;
        buffer[pos++] = (depth & nn) ? ((nn == 1) ? '`' : ' ') : '|';
        buffer[pos++] = (nn == 1) ? '-': ' ';
    }
    buffer[pos] = 0;
    send_message(user, bot, "OSMSG_LINKS_SERVER", buffer, server->name, server->clients, server->description);
    if (!server->children.used)
        return;
    for (nn=0; nn<server->children.used-1; nn++) {
        trace_links(bot, user, server->children.list[nn], depth<<1);
    }
    trace_links(bot, user, server->children.list[nn], (depth<<1)|1);
}

static MODCMD_FUNC(cmd_stats_links) {
    trace_links(cmd->parent->bot, user, self, 1);
    return 1;
}


static MODCMD_FUNC(cmd_stats_max) {
    reply("OSMSG_MAX_CLIENTS", max_clients, asctime(localtime(&max_clients_time)));
    return 1;
}

static MODCMD_FUNC(cmd_stats_network) {
    struct helpfile_table tbl;
    unsigned int nn, tot_clients;
    dict_iterator_t it;

    tot_clients = dict_size(clients);
    reply("OSMSG_NETWORK_INFO", tot_clients, invis_clients, curr_opers.used);
    tbl.length = dict_size(servers)+1;
    tbl.width = 3;
    tbl.flags = TABLE_NO_FREE;
    tbl.contents = calloc(tbl.length, sizeof(*tbl.contents));
    tbl.contents[0] = calloc(tbl.width, sizeof(**tbl.contents));
    tbl.contents[0][0] = "Server Name";
    tbl.contents[0][1] = "Clients";
    tbl.contents[0][2] = "Load";
    for (it=dict_first(servers), nn=1; it; it=iter_next(it)) {
        struct server *server = iter_data(it);
        char *buffer = malloc(32);
        tbl.contents[nn] = calloc(tbl.width, sizeof(**tbl.contents));
        tbl.contents[nn][0] = server->name;
        tbl.contents[nn][1] = buffer;
        sprintf(buffer, "%u", server->clients);
        tbl.contents[nn][2] = buffer + 16;
        sprintf(buffer+16, "%3.3g%%", ((double)server->clients/tot_clients)*100);
        nn++;
    }
    table_send(cmd->parent->bot, user->nick, 0, 0, tbl);
    for (nn=1; nn<tbl.length; nn++) {
        free((char*)tbl.contents[nn][1]);
        free(tbl.contents[nn]);
    }
    free(tbl.contents[0]);
    free(tbl.contents);
    return 1;
}

static MODCMD_FUNC(cmd_stats_network2) {
    struct helpfile_table tbl;
    unsigned int nn;
    dict_iterator_t it;

    tbl.length = dict_size(servers)+1;
    tbl.width = 3;
    tbl.flags = TABLE_NO_FREE;
    tbl.contents = calloc(tbl.length, sizeof(*tbl.contents));
    tbl.contents[0] = calloc(tbl.width, sizeof(**tbl.contents));
    tbl.contents[0][0] = "Server Name";
    tbl.contents[0][1] = "Numeric";
    tbl.contents[0][2] = "Link Time";
    for (it=dict_first(servers), nn=1; it; it=iter_next(it)) {
        struct server *server = iter_data(it);
        char *buffer = malloc(64);
        int ofs;

        tbl.contents[nn] = calloc(tbl.width, sizeof(**tbl.contents));
        tbl.contents[nn][0] = server->name;
#ifdef WITH_PROTOCOL_P10
        sprintf(buffer, "%s (%ld)", server->numeric, base64toint(server->numeric, strlen(server->numeric)));
#else
        buffer[0] = 0;
#endif
        tbl.contents[nn][1] = buffer;
        ofs = strlen(buffer) + 1;
        intervalString(buffer + ofs, now - server->link, user->handle_info);
        if (server->self_burst)
            strcat(buffer + ofs, " Bursting");
        tbl.contents[nn][2] = buffer + ofs;
        nn++;
    }
    table_send(cmd->parent->bot, user->nick, 0, 0, tbl);
    for (nn=1; nn<tbl.length; nn++) {
        free((char*)tbl.contents[nn][1]);
        free(tbl.contents[nn]);
    }
    free(tbl.contents[0]);
    free(tbl.contents);
    return 1;
}

static MODCMD_FUNC(cmd_stats_reserved) {
    dict_iterator_t it;

    reply("OSMSG_RESERVED_LIST");
    for (it = dict_first(opserv_reserved_nick_dict); it; it = iter_next(it))
        send_message_type(4, user, cmd->parent->bot, "%s", iter_key(it));
    return 1;
}

static MODCMD_FUNC(cmd_stats_trusted) {
    dict_iterator_t it;
    struct trusted_host *th;
    char length[INTERVALLEN], issued[INTERVALLEN], limit[32];

    if (argc > 1) {
        th = dict_find(opserv_trusted_hosts, argv[1], NULL);
        if (th) {
            if (th->issued)
                intervalString(issued, now - th->issued, user->handle_info);
            if (th->expires)
                intervalString(length, th->expires - now, user->handle_info);
            if (th->limit)
                sprintf(limit, "limit %lu", th->limit);
            reply("OSMSG_HOST_IS_TRUSTED",
                  th->ipaddr,
                  (th->limit ? limit : "no limit"),
                  (th->issued ? issued : "some time"),
                  (th->issuer ? th->issuer : "<unknown>"),
                  (th->expires ? length : "never"),
                  (th->reason ? th->reason : "<unknown>"));
        } else {
            reply("OSMSG_HOST_NOT_TRUSTED", argv[1]);
        }
    } else {
        reply("OSMSG_TRUSTED_LIST");
        for (it = dict_first(opserv_trusted_hosts); it; it = iter_next(it)) {
            th = iter_data(it);
            if (th->issued)
                intervalString(issued, now - th->issued, user->handle_info);
            if (th->expires)
                intervalString(length, th->expires - now, user->handle_info);
            if (th->limit)
                sprintf(limit, "limit %lu", th->limit);
            reply("OSMSG_HOST_IS_TRUSTED", iter_key(it),
                  (th->limit ? limit : "no limit"),
                  (th->issued ? issued : "some time"),
                  (th->issuer ? th->issuer : "<unknown>"),
                  (th->expires ? length : "never"),
                  (th->reason ? th->reason : "<unknown>"));
        }
    }
    return 1;
}

static MODCMD_FUNC(cmd_stats_uplink) {
    extern struct cManagerNode cManager;
    struct uplinkNode *uplink;

    uplink = cManager.uplink;
    reply("OSMSG_UPLINK_START", uplink->name);
    reply("OSMSG_UPLINK_ADDRESS", uplink->host, uplink->port);
    return 1;
}

static MODCMD_FUNC(cmd_stats_uptime) {
    char uptime[INTERVALLEN];
    struct tms buf;
    extern time_t boot_time;
    extern int lines_processed;
    static long clocks_per_sec;

    if (!clocks_per_sec) {
#if defined(HAVE_SYSCONF) && defined(_SC_CLK_TCK)
        clocks_per_sec = sysconf(_SC_CLK_TCK);
        if (clocks_per_sec <= 0)
#endif
        {
            log_module(OS_LOG, LOG_ERROR, "Unable to query sysconf(_SC_CLK_TCK), output of 'stats uptime' will be wrong");
            clocks_per_sec = CLOCKS_PER_SEC;
        }
    }
    intervalString(uptime, time(NULL)-boot_time, user->handle_info);
    times(&buf);
    reply("OSMSG_UPTIME_STATS",
          uptime, lines_processed,
          buf.tms_utime/(double)clocks_per_sec,
          buf.tms_stime/(double)clocks_per_sec);
    return 1;
}

static MODCMD_FUNC(cmd_stats_alerts) {
    dict_iterator_t it;
    struct opserv_user_alert *alert;
    const char *reaction;

    reply("OSMSG_ALERTS_LIST");
    for (it = dict_first(opserv_user_alerts); it; it = iter_next(it)) {
        alert = iter_data(it);
        switch (alert->reaction) {
        case REACT_NOTICE: reaction = "notice"; break;
        case REACT_KILL: reaction = "kill"; break;
        case REACT_GLINE: reaction = "gline"; break;
        default: reaction = "<unknown>"; break;
        }
        reply("OSMSG_ALERT_IS", iter_key(it), alert->owner, reaction, alert->text_discrim);
    }
    return 1;
}

static MODCMD_FUNC(cmd_stats_gags) {
    struct gag_entry *gag;
    struct helpfile_table table;
    unsigned int nn;

    if (!gagList) {
	reply("OSMSG_NO_GAGS");
        return 1;
    }
    for (nn=0, gag=gagList; gag; nn++, gag=gag->next) ;
    table.length = nn+1;
    table.width = 4;
    table.flags = TABLE_NO_FREE;
    table.contents = calloc(table.length, sizeof(char**));
    table.contents[0] = calloc(table.width, sizeof(char*));
    table.contents[0][0] = "Mask";
    table.contents[0][1] = "Owner";
    table.contents[0][2] = "Expires";
    table.contents[0][3] = "Reason";
    for (nn=1, gag=gagList; gag; nn++, gag=gag->next) {
        char expstr[INTERVALLEN];
        if (gag->expires)
            intervalString(expstr, gag->expires - now, user->handle_info);
        else
            strcpy(expstr, "Never");
        table.contents[nn] = calloc(table.width, sizeof(char*));
        table.contents[nn][0] = gag->mask;
        table.contents[nn][1] = gag->owner;
        table.contents[nn][2] = strdup(expstr);
        table.contents[nn][3] = gag->reason;
    }
    table_send(cmd->parent->bot, user->nick, 0, NULL, table);
    for (nn=1; nn<table.length; nn++) {
        free((char*)table.contents[nn][2]);
        free(table.contents[nn]);
    }
    free(table.contents[0]);
    free(table.contents);
    return 1;
}

static MODCMD_FUNC(cmd_stats_timeq) {
    reply("OSMSG_TIMEQ_INFO", timeq_size(), timeq_next()-now);
    return 1;
}

static MODCMD_FUNC(cmd_stats_warn) {
    dict_iterator_t it;

    reply("OSMSG_WARN_LISTSTART");
    for (it=dict_first(opserv_chan_warn); it; it=iter_next(it))
        reply("OSMSG_WARN_LISTENTRY", iter_key(it), (char*)iter_data(it));
    reply("OSMSG_WARN_LISTEND");
    return 1;
}

#if defined(WITH_MALLOC_SRVX)
static MODCMD_FUNC(cmd_stats_memory) {
    extern unsigned long alloc_count, alloc_size;
    send_message_type(MSG_TYPE_NOXLATE, user, cmd->parent->bot,
                      "%u allocations totalling %u bytes.",
                      alloc_count, alloc_size);
    return 1;
}
#elif defined(WITH_MALLOC_SLAB)
static MODCMD_FUNC(cmd_stats_memory) {
    extern unsigned long slab_alloc_count, slab_count, slab_alloc_size;
    extern unsigned long big_alloc_count, big_alloc_size;
    send_message_type(MSG_TYPE_NOXLATE, user, cmd->parent->bot,
                      "%u allocations in %u slabs totalling %u bytes.",
                      slab_alloc_count, slab_count, slab_alloc_size);
    send_message_type(MSG_TYPE_NOXLATE, user, cmd->parent->bot,
                      "%u big allocations totalling %u bytes.",
                      big_alloc_count, big_alloc_size);
    return 1;
}
#endif

static MODCMD_FUNC(cmd_dump)
{
    char linedup[MAXLEN], original[MAXLEN];

    unsplit_string(argv+1, argc-1, original);
    safestrncpy(linedup, original, sizeof(linedup));
    /* assume it's only valid IRC if we can parse it */
    if (parse_line(linedup, 1)) {
	irc_raw(original);
	reply("OSMSG_LINE_DUMPED");
    } else
	reply("OSMSG_RAW_PARSE_ERROR");
    return 1;
}

static MODCMD_FUNC(cmd_raw)
{
    char linedup[MAXLEN], original[MAXLEN];

    unsplit_string(argv+1, argc-1, original);
    safestrncpy(linedup, original, sizeof(linedup));
    /* Try to parse the line before sending it; if it's too wrong,
     * maybe it will core us instead of our uplink. */
    parse_line(linedup, 1);
    irc_raw(original);
    reply("OSMSG_LINE_DUMPED");
    return 1;
}

static struct userNode *
opserv_add_reserve(struct svccmd *cmd, struct userNode *user, const char *nick, const char *ident, const char *host, const char *desc)
{
    struct userNode *resv = GetUserH(nick);
    if (resv) {
	if (IsService(resv)) {
	    reply("MSG_SERVICE_IMMUNE", resv->nick);
	    return NULL;
	}
	if (resv->handle_info
	    && resv->handle_info->opserv_level > user->handle_info->opserv_level) {
	    reply("OSMSG_LEVEL_TOO_LOW");
	    return NULL;
	}
    }
    if ((resv = AddClone(nick, ident, host, desc))) {
        dict_insert(opserv_reserved_nick_dict, resv->nick, resv);
    }
    return resv;
}

static MODCMD_FUNC(cmd_collide)
{
    struct userNode *resv;

    resv = opserv_add_reserve(cmd, user, argv[1], argv[2], argv[3], unsplit_string(argv+4, argc-4, NULL));
    if (resv) {
	reply("OSMSG_COLLIDED_NICK", resv->nick);
	return 1;
    } else {
        reply("OSMSG_CLONE_FAILED", argv[1]);
	return 0;
    }
}

static MODCMD_FUNC(cmd_reserve)
{
    struct userNode *resv;

    resv = opserv_add_reserve(cmd, user, argv[1], argv[2], argv[3], unsplit_string(argv+4, argc-4, NULL));
    if (resv) {
	resv->modes |= FLAGS_PERSISTENT;
	reply("OSMSG_RESERVED_NICK", resv->nick);
	return 1;
    } else {
        reply("OSMSG_CLONE_FAILED", argv[1]);
	return 0;
    }
}

static int
free_reserve(char *nick)
{
    struct userNode *resv;
    unsigned int rlen;
    char *reason;

    resv = dict_find(opserv_reserved_nick_dict, nick, NULL);
    if (!resv)
        return 0;

    rlen = strlen(resv->nick)+strlen(OSMSG_PART_REASON);
    reason = alloca(rlen);
    snprintf(reason, rlen, OSMSG_PART_REASON, resv->nick);
    DelUser(resv, NULL, 1, reason);
    dict_remove(opserv_reserved_nick_dict, nick);
    return 1;
}

static MODCMD_FUNC(cmd_unreserve)
{
    if (free_reserve(argv[1]))
	reply("OSMSG_NICK_UNRESERVED", argv[1]);
    else
	reply("OSMSG_NOT_RESERVED", argv[1]);
    return 1;
}

static void
opserv_part_channel(void *data)
{
    DelChannelUser(opserv, data, "Leaving.", 0);
}

static int alert_check_user(const char *key, void *data, void *extra);

static int
opserv_new_user_check(struct userNode *user)
{
    struct opserv_hostinfo *ohi;
    struct gag_entry *gag;
    char addr[IRC_NTOP_MAX_SIZE];

    /* Check to see if we should ignore them entirely. */
    if (IsLocal(user) || IsService(user))
        return 0;

    /* Check for alerts, and stop if we find one that kills them. */
    if (dict_foreach(opserv_user_alerts, alert_check_user, user))
        return 1;

    /* Gag them if appropriate. */
    for (gag = gagList; gag; gag = gag->next) {
        if (user_matches_glob(user, gag->mask, 1)) {
            gag_helper_func(user, NULL);
            break;
        }
    }

    /* Add to host info struct */
    irc_ntop(addr, sizeof(addr), &user->ip);
    if (!(ohi = dict_find(opserv_hostinfo_dict, addr, NULL))) {
        ohi = calloc(1, sizeof(*ohi));
        dict_insert(opserv_hostinfo_dict, strdup(addr), ohi);
        userList_init(&ohi->clients);
    }
    userList_append(&ohi->clients, user);

    /* Only warn of new user floods outside of bursts. */
    if (!user->uplink->burst) {
        if (!policer_conforms(&opserv_conf.new_user_policer, now, 10)) {
            if (!new_user_flood) {
                new_user_flood = 1;
                opserv_alert("Warning: Possible new-user flood.");
            }
        } else {
            new_user_flood = 0;
        }
    }

    /* Only warn or G-line if there's an untrusted max and their IP is sane. */
    if (opserv_conf.untrusted_max
        && irc_in_addr_is_valid(user->ip)
        && !irc_in_addr_is_loopback(user->ip)) {
        struct trusted_host *th = dict_find(opserv_trusted_hosts, addr, NULL);
        unsigned int limit = th ? th->limit : opserv_conf.untrusted_max;
        if (!limit) {
            /* 0 means unlimited hosts */
        } else if (ohi->clients.used == limit) {
            unsigned int nn;
            for (nn=0; nn<ohi->clients.used; nn++)
                send_message(ohi->clients.list[nn], opserv, "OSMSG_CLONE_WARNING");
        } else if (ohi->clients.used > limit) {
            char target[IRC_NTOP_MAX_SIZE + 3] = { '*', '@', '\0' };
            strcpy(target + 2, addr);
            gline_add(opserv->nick, target, opserv_conf.clone_gline_duration, "AUTO Excessive connections from a single host.", now, 1);
        }
    }

    return 0;
}

static void
opserv_user_cleanup(struct userNode *user, UNUSED_ARG(struct userNode *killer), UNUSED_ARG(const char *why))
{
    struct opserv_hostinfo *ohi;
    char addr[IRC_NTOP_MAX_SIZE];

    if (IsLocal(user)) {
        /* Try to remove it from the reserved nick dict without
         * calling free_reserve, because that would call DelUser(),
         * and we'd loop back to here. */
        dict_remove(opserv_reserved_nick_dict, user->nick);
        return;
    }
    irc_ntop(addr, sizeof(addr), &user->ip);
    if ((ohi = dict_find(opserv_hostinfo_dict, addr, NULL))) {
        userList_remove(&ohi->clients, user);
        if (ohi->clients.used == 0)
            dict_remove(opserv_hostinfo_dict, addr);
    }
}

int
opserv_bad_channel(const char *name)
{
    unsigned int found;
    int present;

    dict_find(opserv_exempt_channels, name, &present);
    if (present)
        return 0;

    if (gline_find(name))
        return 1;

    for (found=0; found<opserv_bad_words->used; ++found)
        if (irccasestr(name, opserv_bad_words->list[found]))
            return 1;

    return 0;
}

static void
opserv_shutdown_channel(struct chanNode *channel, const char *reason)
{
    struct mod_chanmode *change;
    unsigned int nn;

    change = mod_chanmode_alloc(2);
    change->modes_set = MODE_SECRET | MODE_INVITEONLY;
    change->args[0].mode = MODE_CHANOP;
    change->args[0].u.member = AddChannelUser(opserv, channel);
    change->args[1].mode = MODE_BAN;
    change->args[1].u.hostmask = "*!*@*";
    mod_chanmode_announce(opserv, channel, change);
    mod_chanmode_free(change);
    for (nn=channel->members.used; nn>0; ) {
        struct modeNode *mNode = channel->members.list[--nn];
        if (IsService(mNode->user))
            continue;
        KickChannelUser(mNode->user, channel, opserv, user_find_message(mNode->user, reason));
    }
    timeq_add(now + opserv_conf.purge_lock_delay, opserv_part_channel, channel);
}

static void
opserv_channel_check(struct chanNode *newchan)
{
    char *warning;

    if (!newchan->join_policer.params) {
        newchan->join_policer.last_req = now;
        newchan->join_policer.params = opserv_conf.join_policer_params;
    }
    if ((warning = dict_find(opserv_chan_warn, newchan->name, NULL))) {
        char message[MAXLEN];
        snprintf(message, sizeof(message), "Channel activity warning for channel %s: %s", newchan->name, warning);
        global_message(MESSAGE_RECIPIENT_OPERS, message);
    }

    /* Wait until the join check to shut channels down. */
    newchan->bad_channel = opserv_bad_channel(newchan->name);
}

static void
opserv_channel_delete(struct chanNode *chan)
{
    timeq_del(0, opserv_part_channel, chan, TIMEQ_IGNORE_WHEN);
}

static int
opserv_join_check(struct modeNode *mNode)
{
    struct userNode *user = mNode->user;
    struct chanNode *channel = mNode->channel;
    const char *msg;

    if (IsService(user))
        return 0;

    dict_foreach(opserv_channel_alerts, alert_check_user, user);

    if (channel->bad_channel) {
        opserv_debug("Found $b%s$b in bad-word channel $b%s$b; removing the user.", user->nick, channel->name);
        if (channel->name[0] != '#')
            DelUser(user, opserv, 1, "OSMSG_ILLEGAL_KILL_REASON");
        else if (!GetUserMode(channel, opserv))
            opserv_shutdown_channel(channel, "OSMSG_ILLEGAL_REASON");
        else {
            send_message(user, opserv, "OSMSG_ILLEGAL_CHANNEL", channel->name);
            msg = user_find_message(user, "OSMSG_ILLEGAL_REASON");
            KickChannelUser(user, channel, opserv, msg);
        }
        return 1;
    }

    if (user->uplink->burst)
        return 0;
    if (policer_conforms(&channel->join_policer, now, 1.0)) {
        channel->join_flooded = 0;
        return 0;
    }
    if (!channel->join_flooded) {
        /* Don't moderate the channel unless it is activated and
           the number of users in the channel is over the threshold. */
        struct mod_chanmode change;
        mod_chanmode_init(&change);
        channel->join_flooded = 1;
        if (opserv_conf.join_flood_moderate && (channel->members.used > opserv_conf.join_flood_moderate_threshold)) {
            if (!GetUserMode(channel, opserv)) {
                /* If we aren't in the channel, join it. */
                change.args[0].mode = MODE_CHANOP;
                change.args[0].u.member = AddChannelUser(opserv, channel);
                change.argc++;
            }
            change.modes_set = (MODE_MODERATED | MODE_DELAYJOINS) & ~channel->modes;
            if (change.modes_set || change.argc)
                mod_chanmode_announce(opserv, channel, &change);
            send_target_message(0, channel->name, opserv, "OSMSG_FLOOD_MODERATE");
            opserv_alert("Warning: Possible join flood in %s (currently %d users; channel moderated).", channel->name, channel->members.used);
        } else {
            opserv_alert("Warning: Possible join flood in %s (currently %d users).", channel->name, channel->members.used);
        }
    }
    log_module(OS_LOG, LOG_INFO, "Join to %s during flood: "IDENT_FORMAT, channel->name, IDENT_DATA(user));
    return 0;
}

static int
opserv_add_bad_word(struct svccmd *cmd, struct userNode *user, const char *new_bad) {
    unsigned int bad_idx;

    for (bad_idx = 0; bad_idx < opserv_bad_words->used; ++bad_idx) {
        char *orig_bad = opserv_bad_words->list[bad_idx];
        if (irccasestr(new_bad, orig_bad)) {
            if (user)
                reply("OSMSG_BAD_REDUNDANT", new_bad, orig_bad);
            return 0;
        } else if (irccasestr(orig_bad, new_bad)) {
            if (user)
                reply("OSMSG_BAD_GROWING", orig_bad, new_bad);
            free(orig_bad);
            opserv_bad_words->list[bad_idx] = strdup(new_bad);
            for (bad_idx++; bad_idx < opserv_bad_words->used; bad_idx++) {
                orig_bad = opserv_bad_words->list[bad_idx];
                if (!irccasestr(orig_bad, new_bad))
                    continue;
                if (user)
                    reply("OSMSG_BAD_NUKING", orig_bad);
                string_list_delete(opserv_bad_words, bad_idx);
                bad_idx--;
                free(orig_bad);
            }
            return 1;
        }
    }
    string_list_append(opserv_bad_words, strdup(new_bad));
    if (user)
        reply("OSMSG_ADDED_BAD", new_bad);
    return 1;
}

static MODCMD_FUNC(cmd_addbad)
{
    unsigned int arg, count;
    dict_iterator_t it;
    int bad_found, exempt_found;

    /* Create the bad word if it doesn't exist. */
    bad_found = !opserv_add_bad_word(cmd, user, argv[1]);

    /* Look for exception modifiers. */
    for (arg=2; arg<argc; arg++) {
        if (!irccasecmp(argv[arg], "except")) {
            reply("MSG_DEPRECATED_COMMAND", "addbad ... except", "addexempt");
            if (++arg > argc) {
                reply("MSG_MISSING_PARAMS", "except");
                break;
            }
            for (count = 0; (arg < argc) && IsChannelName(argv[arg]); arg++) {
                dict_find(opserv_exempt_channels, argv[arg], &exempt_found);
                if (!exempt_found) {
                    dict_insert(opserv_exempt_channels, strdup(argv[arg]), NULL);
                    count++;
                }
            }
            reply("OSMSG_ADDED_EXEMPTIONS", count);
        } else {
            reply("MSG_DEPRECATED_COMMAND", "addbad (with modifiers)", "addbad");
            reply("OSMSG_BAD_MODIFIER", argv[arg]);
        }
    }

    /* Scan for existing channels that match the new bad word. */
    if (!bad_found) {
        for (it = dict_first(channels); it; it = iter_next(it)) {
            struct chanNode *channel = iter_data(it);

            if (!opserv_bad_channel(channel->name))
                continue;
            channel->bad_channel = 1;
            if (channel->name[0] == '#')
                opserv_shutdown_channel(channel, "OSMSG_ILLEGAL_REASON");
            else {
                unsigned int nn;
                for (nn=0; nn<channel->members.used; nn++) {
                    struct userNode *user = channel->members.list[nn]->user;
                    DelUser(user, cmd->parent->bot, 1, "OSMSG_ILLEGAL_KILL_REASON");
                }
            }
        }
    }

    return 1;
}

static MODCMD_FUNC(cmd_delbad)
{
    dict_iterator_t it;
    unsigned int nn;

    for (nn=0; nn<opserv_bad_words->used; nn++) {
        if (!irccasecmp(opserv_bad_words->list[nn], argv[1])) {
            string_list_delete(opserv_bad_words, nn);
            for (it = dict_first(channels); it; it = iter_next(it)) {
                channel = iter_data(it);
                if (irccasestr(channel->name, argv[1])
                    && !opserv_bad_channel(channel->name)) {
                    DelChannelUser(cmd->parent->bot, channel, "Channel name no longer contains a bad word.", 1);
                    timeq_del(0, opserv_part_channel, channel, TIMEQ_IGNORE_WHEN);
                    channel->bad_channel = 0;
                }
            }
            reply("OSMSG_REMOVED_BAD", argv[1]);
            return 1;
        }
    }
    reply("OSMSG_NOT_BAD_WORD", argv[1]);
    return 0;
}

static MODCMD_FUNC(cmd_addexempt)
{
    const char *chanName;

    if ((argc > 1) && IsChannelName(argv[1])) {
        chanName = argv[1];
    } else {
        reply("MSG_NOT_CHANNEL_NAME");
        OPSERV_SYNTAX();
        return 0;
    }
    dict_insert(opserv_exempt_channels, strdup(chanName), NULL);
    channel = GetChannel(chanName);
    if (channel) {
        if (channel->bad_channel) {
            DelChannelUser(cmd->parent->bot, channel, "Channel is now exempt from bad-word checking.", 1);
            timeq_del(0, opserv_part_channel, channel, TIMEQ_IGNORE_WHEN);
        }
        channel->bad_channel = 0;
    }
    reply("OSMSG_ADDED_EXEMPTION", chanName);
    return 1;
}

static MODCMD_FUNC(cmd_delexempt)
{
    const char *chanName;

    if ((argc > 1) && IsChannelName(argv[1])) {
        chanName = argv[1];
    } else {
        reply("MSG_NOT_CHANNEL_NAME");
        OPSERV_SYNTAX();
        return 0;
    }
    if (!dict_remove(opserv_exempt_channels, chanName)) {
        reply("OSMSG_NOT_EXEMPT", chanName);
        return 0;
    }
    reply("OSMSG_REMOVED_EXEMPTION", chanName);
    return 1;
}

static void
opserv_expire_trusted_host(void *data)
{
    struct trusted_host *th = data;
    dict_remove(opserv_trusted_hosts, th->ipaddr);
}

static void
opserv_add_trusted_host(const char *ipaddr, unsigned int limit, const char *issuer, time_t issued, time_t expires, const char *reason)
{
    struct trusted_host *th;
    th = calloc(1, sizeof(*th));
    if (!th)
        return;
    th->ipaddr = strdup(ipaddr);
    th->reason = reason ? strdup(reason) : NULL;
    th->issuer = issuer ? strdup(issuer) : NULL;
    th->issued = issued;
    th->limit = limit;
    th->expires = expires;
    dict_insert(opserv_trusted_hosts, th->ipaddr, th);
    if (th->expires)
        timeq_add(th->expires, opserv_expire_trusted_host, th);
}

static void
free_trusted_host(void *data)
{
    struct trusted_host *th = data;
    free(th->ipaddr);
    free(th->reason);
    free(th->issuer);
    free(th);
}

static MODCMD_FUNC(cmd_addtrust)
{
    unsigned long interval;
    char *reason, *tmp;
    irc_in_addr_t tmpaddr;
    unsigned int count;

    if (dict_find(opserv_trusted_hosts, argv[1], NULL)) {
        reply("OSMSG_ALREADY_TRUSTED", argv[1]);
        return 0;
    }

    if (!irc_pton(&tmpaddr, NULL, argv[1])) {
        reply("OSMSG_BAD_IP", argv[1]);
        return 0;
    }

    count = strtoul(argv[2], &tmp, 10);
    if (*tmp != '\0') {
        reply("OSMSG_BAD_NUMBER", argv[2]);
        return 0;
    }

    interval = ParseInterval(argv[3]);
    if (!interval && strcmp(argv[3], "0")) {
        reply("MSG_INVALID_DURATION", argv[3]);
        return 0;
    }

    reason = unsplit_string(argv+4, argc-4, NULL);
    opserv_add_trusted_host(argv[1], count, user->handle_info->handle, now, interval ? (now + interval) : 0, reason);
    reply("OSMSG_ADDED_TRUSTED");
    return 1;
}

static MODCMD_FUNC(cmd_edittrust)
{
    unsigned long interval;
    struct trusted_host *th;
    char *reason, *tmp;
    unsigned int count;

    th = dict_find(opserv_trusted_hosts, argv[1], NULL);
    if (!th) {
        reply("OSMSG_NOT_TRUSTED", argv[1]);
        return 0;
    }
    count = strtoul(argv[2], &tmp, 10);
    if (!count || *tmp) {
        reply("OSMSG_BAD_NUMBER", argv[2]);
        return 0;
    }
    interval = ParseInterval(argv[3]);
    if (!interval && strcmp(argv[3], "0")) {
        reply("MSG_INVALID_DURATION", argv[3]);
        return 0;
    }
    reason = unsplit_string(argv+4, argc-4, NULL);
    if (th->expires)
        timeq_del(th->expires, opserv_expire_trusted_host, th, 0);

    free(th->reason);
    th->reason = strdup(reason);
    free(th->issuer);
    th->issuer = strdup(user->handle_info->handle);
    th->issued = now;
    th->limit = count;
    if (interval) {
        th->expires = now + interval;
        timeq_add(th->expires, opserv_expire_trusted_host, th);
    } else
        th->expires = 0;
    reply("OSMSG_UPDATED_TRUSTED", th->ipaddr);
    return 1;
}

static MODCMD_FUNC(cmd_deltrust)
{
    unsigned int n;

    for (n=1; n<argc; n++) {
        struct trusted_host *th = dict_find(opserv_trusted_hosts, argv[n], NULL);
        if (!th)
            continue;
        if (th->expires)
            timeq_del(th->expires, opserv_expire_trusted_host, th, 0);
        dict_remove(opserv_trusted_hosts, argv[n]);
    }
    reply("OSMSG_REMOVED_TRUSTED");
    return 1;
}

/* This doesn't use dict_t because it's a little simpler to open-code the
 * comparisons (and simpler arg-passing for the ADD subcommand).
 */
static MODCMD_FUNC(cmd_clone)
{
    int i;
    struct userNode *clone;

    clone = GetUserH(argv[2]);
    if (!irccasecmp(argv[1], "ADD")) {
        char *userinfo;
        char ident[USERLEN+1];

	if (argc < 5) {
	    reply("MSG_MISSING_PARAMS", argv[1]);
	    OPSERV_SYNTAX();
	    return 0;
	}
	if (clone) {
	    reply("OSMSG_CLONE_EXISTS", argv[2]);
	    return 0;
	}
	userinfo = unsplit_string(argv+4, argc-4, NULL);
	for (i=0; argv[3][i] && (i<USERLEN); i++) {
	    if (argv[3][i] == '@') {
		ident[i++] = 0;
		break;
	    } else {
                ident[i] = argv[3][i];
            }
	}
	if (!argv[3][i] || (i==USERLEN)) {
	    reply("OSMSG_NOT_A_HOSTMASK");
	    return 0;
	}
	if (!(clone = AddClone(argv[2], ident, argv[3]+i, userinfo))) {
            reply("OSMSG_CLONE_FAILED", argv[2]);
            return 0;
        }
        reply("OSMSG_CLONE_ADDED", clone->nick);
	return 1;
    }
    if (!clone) {
	reply("MSG_NICK_UNKNOWN", argv[2]);
	return 0;
    }
    if (clone->uplink != self || IsService(clone)) {
	reply("OSMSG_NOT_A_CLONE", clone->nick);
	return 0;
    }
    if (!irccasecmp(argv[1], "REMOVE")) {
	const char *reason;
	if (argc > 3) {
	    reason = unsplit_string(argv+3, argc-3, NULL);
	} else {
	    char *tmp;
	    tmp = alloca(strlen(clone->nick) + strlen(OSMSG_PART_REASON));
	    sprintf(tmp, OSMSG_PART_REASON, clone->nick);
	    reason = tmp;
	}
	DelUser(clone, NULL, 1, reason);
	reply("OSMSG_CLONE_REMOVED", argv[2]);
	return 1;
    }
    if (argc < 4) {
	reply("MSG_MISSING_PARAMS", argv[1]);
	OPSERV_SYNTAX();
	return 0;
    }
    channel = GetChannel(argv[3]);
    if (!irccasecmp(argv[1], "JOIN")) {
	if (!channel
	    && !(channel = AddChannel(argv[3], now, NULL, NULL))) {
	    reply("MSG_CHANNEL_UNKNOWN", argv[3]);
	    return 0;
	}
	AddChannelUser(clone, channel);
	reply("OSMSG_CLONE_JOINED", clone->nick, channel->name);
	return 1;
    }
    if (!irccasecmp(argv[1], "PART")) {
	if (!channel) {
	    reply("MSG_CHANNEL_UNKNOWN", argv[3]);
	    return 0;
	}
	if (!GetUserMode(channel, clone)) {
	    reply("OSMSG_NOT_ON_CHANNEL", clone->nick, channel->name);
	    return 0;
	}
	reply("OSMSG_CLONE_PARTED", clone->nick, channel->name);
	DelChannelUser(clone, channel, "Leaving.", 0);
	return 1;
    }
    if (!irccasecmp(argv[1], "OP")) {
        struct mod_chanmode change;
	if (!channel) {
	    reply("MSG_CHANNEL_UNKNOWN", argv[3]);
	    return 0;
	}
        mod_chanmode_init(&change);
        change.argc = 1;
        change.args[0].mode = MODE_CHANOP;
        change.args[0].u.member = GetUserMode(channel, clone);
        if (!change.args[0].u.member) {
            reply("OSMSG_NOT_ON_CHANNEL", clone->nick, channel->name);
            return 0;
	}
        modcmd_chanmode_announce(&change);
	reply("OSMSG_OPS_GIVEN", channel->name, clone->nick);
	return 1;
    }
    if (argc < 5) {
	reply("MSG_MISSING_PARAMS", argv[1]);
	OPSERV_SYNTAX();
	return 0;
    }
    if (!irccasecmp(argv[1], "SAY")) {
	char *text = unsplit_string(argv+4, argc-4, NULL);
	irc_privmsg(clone, argv[3], text);
	reply("OSMSG_CLONE_SAID", clone->nick, argv[3]);
	return 1;
    }
    reply("OSMSG_UNKNOWN_SUBCOMMAND", argv[1], argv[0]);
    return 0;
}

static struct helpfile_expansion
opserv_help_expand(const char *variable)
{
    extern struct userNode *message_source;
    struct helpfile_expansion exp;
    struct service *service;
    struct svccmd *cmd;
    dict_iterator_t it;
    int row;
    unsigned int level;

    if (!(service = service_find(message_source->nick))) {
        exp.type = HF_STRING;
        exp.value.str = NULL;
    } else if (!irccasecmp(variable, "index")) {
        exp.type = HF_TABLE;
        exp.value.table.length = 1;
        exp.value.table.width = 2;
        exp.value.table.flags = TABLE_REPEAT_HEADERS | TABLE_REPEAT_ROWS;
        exp.value.table.contents = calloc(dict_size(service->commands)+1, sizeof(char**));
        exp.value.table.contents[0] = calloc(exp.value.table.width, sizeof(char*));
        exp.value.table.contents[0][0] = "Command";
        exp.value.table.contents[0][1] = "Level";
        for (it=dict_first(service->commands); it; it=iter_next(it)) {
            cmd = iter_data(it);
            row = exp.value.table.length++;
            exp.value.table.contents[row] = calloc(exp.value.table.width, sizeof(char*));
            exp.value.table.contents[row][0] = iter_key(it);
            level = cmd->min_opserv_level;
            if (!level_strings[level]) {
                level_strings[level] = malloc(16);
                snprintf(level_strings[level], 16, "%3d", level);
            }
            exp.value.table.contents[row][1] = level_strings[level];
        }
    } else if (!strncasecmp(variable, "level", 5)) {
        cmd = dict_find(service->commands, variable+6, NULL);
        exp.type = HF_STRING;
        if (cmd) {
            level = cmd->min_opserv_level;
            exp.value.str = malloc(16);
            snprintf(exp.value.str, 16, "%3d", level);
        } else {
            exp.value.str = NULL;
        }
    } else {
        exp.type = HF_STRING;
        exp.value.str = NULL;
    }
    return exp;
}

struct modcmd *
opserv_define_func(const char *name, modcmd_func_t *func, int min_level, int reqchan, int min_argc)
{
    char buf[16], *flags = NULL;
    unsigned int iflags = 0;
    sprintf(buf, "%d", min_level);
    switch (reqchan) {
    case 1: flags = "+acceptchan"; break;
    case 3: flags = "+acceptpluschan"; /* fall through */
    case 2: iflags = MODCMD_REQUIRE_CHANNEL; break;
    }
    if (flags) {
        return modcmd_register(opserv_module, name, func, min_argc, iflags, "level", buf, "flags", flags, "flags", "+oper", NULL);
    } else {
        return modcmd_register(opserv_module, name, func, min_argc, iflags, "level", buf, "flags", "+oper", NULL);
    }
}

int add_reserved(const char *key, void *data, void *extra)
{
    struct record_data *rd = data;
    const char *ident, *hostname, *desc;
    struct userNode *reserve;
    ident = database_get_data(rd->d.object, KEY_IDENT, RECDB_QSTRING);
    if (!ident) {
	log_module(OS_LOG, LOG_ERROR, "Missing ident for reserve of %s", key);
	return 0;
    }
    hostname = database_get_data(rd->d.object, KEY_HOSTNAME, RECDB_QSTRING);
    if (!hostname) {
	log_module(OS_LOG, LOG_ERROR, "Missing hostname for reserve of %s", key);
	return 0;
    }
    desc = database_get_data(rd->d.object, KEY_DESC, RECDB_QSTRING);
    if (!desc) {
	log_module(OS_LOG, LOG_ERROR, "Missing description for reserve of %s", key);
	return 0;
    }
    if ((reserve = AddClone(key, ident, hostname, desc))) {
        reserve->modes |= FLAGS_PERSISTENT;
        dict_insert(extra, reserve->nick, reserve);
    }
    return 0;
}

static unsigned int
foreach_matching_user(const char *hostmask, discrim_search_func func, void *extra)
{
    discrim_t discrim;
    char *dupmask;
    unsigned int matched;

    if (!self->uplink) return 0;
    discrim = calloc(1, sizeof(*discrim));
    discrim->limit = dict_size(clients);
    discrim->max_level = ~0;
    discrim->max_ts = now;
    discrim->max_channels = INT_MAX;
    discrim->authed = -1;
    discrim->info_space = -1;
    dupmask = strdup(hostmask);
    if (split_ircmask(dupmask, &discrim->mask_nick, &discrim->mask_ident, &discrim->mask_host)) {
        if (!irc_pton(&discrim->ip_mask, &discrim->ip_mask_bits, discrim->mask_host))
            discrim->ip_mask_bits = 0;
        matched = opserv_discrim_search(discrim, func, extra);
    } else {
	log_module(OS_LOG, LOG_ERROR, "Couldn't split IRC mask for gag %s!", hostmask);
        matched = 0;
    }
    free(discrim);
    free(dupmask);
    return matched;
}

static unsigned int
gag_free(struct gag_entry *gag)
{
    unsigned int ungagged;

    /* Remove from gag list */
    if (gagList == gag) {
        gagList = gag->next;
    } else {
        struct gag_entry *prev;
        for (prev = gagList; prev->next != gag; prev = prev->next) ;
        prev->next = gag->next;
    }

    ungagged = foreach_matching_user(gag->mask, ungag_helper_func, NULL);

    /* Deallocate storage */
    free(gag->reason);
    free(gag->owner);
    free(gag->mask);
    free(gag);

    return ungagged;
}

static void
gag_expire(void *data)
{
    gag_free(data);
}

unsigned int
gag_create(const char *mask, const char *owner, const char *reason, time_t expires)
{
    struct gag_entry *gag;

    /* Create gag and put it into linked list */
    gag = calloc(1, sizeof(*gag));
    gag->mask = strdup(mask);
    gag->owner = strdup(owner ? owner : "<unknown>");
    gag->reason = strdup(reason ? reason : "<unknown>");
    gag->expires = expires;
    if (gag->expires)
        timeq_add(gag->expires, gag_expire, gag);
    gag->next = gagList;
    gagList = gag;

    /* If we're linked, see if who the gag applies to */
    return foreach_matching_user(mask, gag_helper_func, gag);
}

static int
add_gag_helper(const char *key, void *data, UNUSED_ARG(void *extra))
{
    struct record_data *rd = data;
    char *owner, *reason, *expstr;
    time_t expires;

    owner = database_get_data(rd->d.object, KEY_OWNER, RECDB_QSTRING);
    reason = database_get_data(rd->d.object, KEY_REASON, RECDB_QSTRING);
    expstr = database_get_data(rd->d.object, KEY_EXPIRES, RECDB_QSTRING);
    expires = expstr ? strtoul(expstr, NULL, 0) : 0;
    gag_create(key, owner, reason, expires);

    return 0;
}

static struct opserv_user_alert *
opserv_add_user_alert(struct userNode *req, const char *name, opserv_alert_reaction reaction, const char *text_discrim)
{
    unsigned int wordc;
    char *wordv[MAXNUMPARAMS], *discrim_copy;
    struct opserv_user_alert *alert;
    char *name_dup;

    if (dict_find(opserv_user_alerts, name, NULL)) {
	send_message(req, opserv, "OSMSG_ALERT_EXISTS", name);
	return NULL;
    }
    alert = malloc(sizeof(*alert));
    alert->owner = strdup(req->handle_info ? req->handle_info->handle : req->nick);
    alert->text_discrim = strdup(text_discrim);
    discrim_copy = strdup(text_discrim); /* save a copy of the discrim */
    wordc = split_line(discrim_copy, false, ArrayLength(wordv), wordv);
    alert->discrim = opserv_discrim_create(req, wordc, wordv, 0);
    if (!alert->discrim) {
        free(alert->text_discrim);
        free(discrim_copy);
        free(alert);
        return NULL;
    }
    alert->split_discrim = discrim_copy;
    name_dup = strdup(name);
    if (!alert->discrim->reason)
        alert->discrim->reason = strdup(name);
    alert->reaction = reaction;
    dict_insert(opserv_user_alerts, name_dup, alert);
    /* Stick the alert into the appropriate additional alert dict(s).
     * For channel alerts, we only use channels and min_channels;
     * max_channels would have to be checked on /part, which we do not
     * yet do, and which seems of questionable value.
     */
    if (alert->discrim->channel || alert->discrim->min_channels)
        dict_insert(opserv_channel_alerts, name_dup, alert);
    if (alert->discrim->mask_nick)
        dict_insert(opserv_nick_based_alerts, name_dup, alert);
    return alert;
}

static int
add_chan_warn(const char *key, void *data, UNUSED_ARG(void *extra))
{
    struct record_data *rd = data;
    char *reason = GET_RECORD_QSTRING(rd);

    /* i hope this can't happen */
    if (!reason)
        reason = "No Reason";

    dict_insert(opserv_chan_warn, strdup(key), strdup(reason));
    return 0;
}

static int
add_user_alert(const char *key, void *data, UNUSED_ARG(void *extra))
{
    dict_t alert_dict;
    const char *discrim, *react, *owner;
    opserv_alert_reaction reaction;
    struct opserv_user_alert *alert;

    if (!(alert_dict = GET_RECORD_OBJECT((struct record_data *)data))) {
        log_module(OS_LOG, LOG_ERROR, "Bad type (not a record) for alert %s.", key);
        return 1;
    }
    discrim = database_get_data(alert_dict, KEY_DISCRIM, RECDB_QSTRING);
    react = database_get_data(alert_dict, KEY_REACTION, RECDB_QSTRING);
    if (!react || !irccasecmp(react, "notice"))
        reaction = REACT_NOTICE;
    else if (!irccasecmp(react, "kill"))
        reaction = REACT_KILL;
    else if (!irccasecmp(react, "gline"))
        reaction = REACT_GLINE;
    else {
        log_module(OS_LOG, LOG_ERROR, "Invalid reaction %s for alert %s.", react, key);
        return 0;
    }
    alert = opserv_add_user_alert(opserv, key, reaction, discrim);
    if (!alert) {
        log_module(OS_LOG, LOG_ERROR, "Unable to create alert %s from database.", key);
        return 0;
    }
    owner = database_get_data(alert_dict, KEY_OWNER, RECDB_QSTRING);
    free(alert->owner);
    alert->owner = strdup(owner ? owner : "<unknown>");
    return 0;
}

static int
trusted_host_read(const char *host, void *data, UNUSED_ARG(void *extra))
{
    struct record_data *rd = data;
    const char *limit, *str, *reason, *issuer;
    time_t issued, expires;

    if (rd->type == RECDB_QSTRING) {
        /* old style host by itself */
        limit = GET_RECORD_QSTRING(rd);
        issued = 0;
        issuer = NULL;
        expires = 0;
        reason = NULL;
    } else if (rd->type == RECDB_OBJECT) {
        dict_t obj = GET_RECORD_OBJECT(rd);
        /* new style structure */
        limit = database_get_data(obj, KEY_LIMIT, RECDB_QSTRING);
        str = database_get_data(obj, KEY_EXPIRES, RECDB_QSTRING);
        expires = str ? ParseInterval(str) : 0;
        reason = database_get_data(obj, KEY_REASON, RECDB_QSTRING);
        issuer = database_get_data(obj, KEY_ISSUER, RECDB_QSTRING);
        str = database_get_data(obj, KEY_ISSUED, RECDB_QSTRING);
        issued = str ? ParseInterval(str) : 0;
    } else
        return 0;

    if (expires && (expires < now))
        return 0;
    opserv_add_trusted_host(host, (limit ? strtoul(limit, NULL, 0) : 0), issuer, issued, expires, reason);
    return 0;
}

static int
opserv_saxdb_read(struct dict *conf_db)
{
    dict_t object;
    struct record_data *rd;
    dict_iterator_t it;
    unsigned int nn;

    if ((object = database_get_data(conf_db, KEY_RESERVES, RECDB_OBJECT)))
        dict_foreach(object, add_reserved, opserv_reserved_nick_dict);
    if ((rd = database_get_path(conf_db, KEY_BAD_WORDS))) {
        switch (rd->type) {
        case RECDB_STRING_LIST:
            /* Add words one by one just in case there are overlaps from an old DB. */
            for (nn=0; nn<rd->d.slist->used; ++nn)
                opserv_add_bad_word(NULL, NULL, rd->d.slist->list[nn]);
            break;
        case RECDB_OBJECT:
            for (it=dict_first(rd->d.object); it; it=iter_next(it)) {
                opserv_add_bad_word(NULL, NULL, iter_key(it));
                rd = iter_data(it);
                if (rd->type == RECDB_STRING_LIST)
                    for (nn=0; nn<rd->d.slist->used; nn++)
                        dict_insert(opserv_exempt_channels, strdup(rd->d.slist->list[nn]), NULL);
            }
            break;
        default:
            /* do nothing */;
        }
    }
    if ((rd = database_get_path(conf_db, KEY_EXEMPT_CHANNELS))
        && (rd->type == RECDB_STRING_LIST)) {
        for (nn=0; nn<rd->d.slist->used; ++nn)
            dict_insert(opserv_exempt_channels, strdup(rd->d.slist->list[nn]), NULL);
    }
    if ((object = database_get_data(conf_db, KEY_MAX_CLIENTS, RECDB_OBJECT))) {
        char *str;
        if ((str = database_get_data(object, KEY_MAX, RECDB_QSTRING)))
            max_clients = atoi(str);
        if ((str = database_get_data(object, KEY_TIME, RECDB_QSTRING)))
            max_clients_time = atoi(str);
    }
    if ((object = database_get_data(conf_db, KEY_TRUSTED_HOSTS, RECDB_OBJECT)))
        dict_foreach(object, trusted_host_read, opserv_trusted_hosts);
    if ((object = database_get_data(conf_db, KEY_GAGS, RECDB_OBJECT)))
        dict_foreach(object, add_gag_helper, NULL);
    if ((object = database_get_data(conf_db, KEY_ALERTS, RECDB_OBJECT)))
        dict_foreach(object, add_user_alert, NULL);
    if ((object = database_get_data(conf_db, KEY_WARN, RECDB_OBJECT)))
        dict_foreach(object, add_chan_warn, NULL);
    return 0;
}

static int
opserv_saxdb_write(struct saxdb_context *ctx)
{
    struct string_list *slist;
    dict_iterator_t it;

    /* reserved nicks */
    if (dict_size(opserv_reserved_nick_dict)) {
        saxdb_start_record(ctx, KEY_RESERVES, 1);
        for (it = dict_first(opserv_reserved_nick_dict); it; it = iter_next(it)) {
            struct userNode *user = iter_data(it);
            if (!IsPersistent(user)) continue;
            saxdb_start_record(ctx, iter_key(it), 0);
            saxdb_write_string(ctx, KEY_IDENT, user->ident);
            saxdb_write_string(ctx, KEY_HOSTNAME, user->hostname);
            saxdb_write_string(ctx, KEY_DESC, user->info);
            saxdb_end_record(ctx);
        }
        saxdb_end_record(ctx);
    }
    /* bad word set */
    if (opserv_bad_words->used) {
        saxdb_write_string_list(ctx, KEY_BAD_WORDS, opserv_bad_words);
    }
    /* insert exempt channel names */
    if (dict_size(opserv_exempt_channels)) {
        slist = alloc_string_list(dict_size(opserv_exempt_channels));
        for (it=dict_first(opserv_exempt_channels); it; it=iter_next(it)) {
            string_list_append(slist, strdup(iter_key(it)));
        }
        saxdb_write_string_list(ctx, KEY_EXEMPT_CHANNELS, slist);
        free_string_list(slist);
    }
    /* trusted hosts takes a little more work */
    if (dict_size(opserv_trusted_hosts)) {
        saxdb_start_record(ctx, KEY_TRUSTED_HOSTS, 1);
        for (it = dict_first(opserv_trusted_hosts); it; it = iter_next(it)) {
            struct trusted_host *th = iter_data(it);
            saxdb_start_record(ctx, iter_key(it), 0);
            if (th->limit) saxdb_write_int(ctx, KEY_LIMIT, th->limit);
            if (th->expires) saxdb_write_int(ctx, KEY_EXPIRES, th->expires);
            if (th->issued) saxdb_write_int(ctx, KEY_ISSUED, th->issued);
            if (th->issuer) saxdb_write_string(ctx, KEY_ISSUER, th->issuer);
            if (th->reason) saxdb_write_string(ctx, KEY_REASON, th->reason);
            saxdb_end_record(ctx);
        }
        saxdb_end_record(ctx);
    }
    /* gags */
    if (gagList) {
        struct gag_entry *gag;
        saxdb_start_record(ctx, KEY_GAGS, 1);
        for (gag = gagList; gag; gag = gag->next) {
            saxdb_start_record(ctx, gag->mask, 0);
            saxdb_write_string(ctx, KEY_OWNER, gag->owner);
            saxdb_write_string(ctx, KEY_REASON, gag->reason);
            if (gag->expires) saxdb_write_int(ctx, KEY_EXPIRES, gag->expires);
            saxdb_end_record(ctx);
        }
        saxdb_end_record(ctx);
    }
    /* channel warnings */
    if (dict_size(opserv_chan_warn)) {
        saxdb_start_record(ctx, KEY_WARN, 0);
        for (it = dict_first(opserv_chan_warn); it; it = iter_next(it)) {
            saxdb_write_string(ctx, iter_key(it), iter_data(it));
        }
        saxdb_end_record(ctx);
    }
    /* alerts */
    if (dict_size(opserv_user_alerts)) {
        saxdb_start_record(ctx, KEY_ALERTS, 1);
        for (it = dict_first(opserv_user_alerts); it; it = iter_next(it)) {
            struct opserv_user_alert *alert = iter_data(it);
            const char *reaction;
            saxdb_start_record(ctx, iter_key(it), 0);
            saxdb_write_string(ctx, KEY_DISCRIM, alert->text_discrim);
            saxdb_write_string(ctx, KEY_OWNER, alert->owner);
            switch (alert->reaction) {
            case REACT_NOTICE: reaction = "notice"; break;
            case REACT_KILL: reaction = "kill"; break;
            case REACT_GLINE: reaction = "gline"; break;
            default:
                reaction = NULL;
                log_module(OS_LOG, LOG_ERROR, "Invalid reaction type %d for alert %s (while writing database).", alert->reaction, iter_key(it));
                break;
            }
            if (reaction) saxdb_write_string(ctx, KEY_REACTION, reaction);
            saxdb_end_record(ctx);
        }
        saxdb_end_record(ctx);
    }
    /* max clients */
    saxdb_start_record(ctx, KEY_MAX_CLIENTS, 0);
    saxdb_write_int(ctx, KEY_MAX, max_clients);
    saxdb_write_int(ctx, KEY_TIME, max_clients_time);
    saxdb_end_record(ctx);
    return 0;
}

static int
query_keys_helper(const char *key, UNUSED_ARG(void *data), void *extra)
{
    send_message_type(4, extra, opserv, "$b%s$b", key);
    return 0;
}

static MODCMD_FUNC(cmd_query)
{
    struct record_data *rd;
    unsigned int i;
    char *nodename;

    if (argc < 2) {
	reply("OSMSG_OPTION_ROOT");
	conf_enum_root(query_keys_helper, user);
	return 1;
    }

    nodename = unsplit_string(argv+1, argc-1, NULL);
    if (!(rd = conf_get_node(nodename))) {
	reply("OSMSG_UNKNOWN_OPTION", nodename);
	return 0;
    }

    if (rd->type == RECDB_QSTRING)
	reply("OSMSG_OPTION_IS", nodename, rd->d.qstring);
    else if (rd->type == RECDB_STRING_LIST) {
	reply("OSMSG_OPTION_LIST", nodename);
	if (rd->d.slist->used)
	    for (i=0; i<rd->d.slist->used; i++)
		send_message_type(4, user, cmd->parent->bot, "$b%s$b", rd->d.slist->list[i]);
	else
	    reply("OSMSG_OPTION_LIST_EMPTY");
    } else if (rd->type == RECDB_OBJECT) {
	reply("OSMSG_OPTION_KEYS", nodename);
	dict_foreach(rd->d.object, query_keys_helper, user);
    }

    return 1;
}

static MODCMD_FUNC(cmd_set)
{
    struct record_data *rd;

    /* I originally wanted to be able to fully manipulate the config
       db with this, but i wussed out. feel free to fix this - you'll
       need to handle quoted strings which have been split, and likely
       invent a syntax for it. -Zoot */

    if (!(rd = conf_get_node(argv[1]))) {
	reply("OSMSG_SET_NOT_SET", argv[1]);
	return 0;
    }

    if (rd->type != RECDB_QSTRING) {
	reply("OSMSG_SET_BAD_TYPE", argv[1]);
	return 0;
    }

    free(rd->d.qstring);
    rd->d.qstring = strdup(argv[2]);
    conf_call_reload_funcs();
    reply("OSMSG_SET_SUCCESS", argv[1], argv[2]);
    return 1;
}

static MODCMD_FUNC(cmd_settime)
{
    const char *srv_name_mask = "*";
    time_t new_time = now;

    if (argc > 1)
        srv_name_mask = argv[1];
    if (argc > 2)
        new_time = time(NULL);
    irc_settime(srv_name_mask, new_time);
    reply("OSMSG_SETTIME_SUCCESS", srv_name_mask);
    return 1;
}

static discrim_t
opserv_discrim_create(struct userNode *user, unsigned int argc, char *argv[], int allow_channel)
{
    unsigned int i, j;
    discrim_t discrim;

    discrim = calloc(1, sizeof(*discrim));
    discrim->limit = 250;
    discrim->max_level = ~0;
    discrim->max_ts = INT_MAX;
    discrim->domain_depth = 2;
    discrim->max_channels = INT_MAX;
    discrim->authed = -1;
    discrim->info_space = -1;

    for (i=0; i<argc; i++) {
        if (irccasecmp(argv[i], "log") == 0) {
            discrim->option_log = 1;
            continue;
        }
        /* Assume all other criteria require arguments. */
        if (i == argc - 1) {
            send_message(user, opserv, "MSG_MISSING_PARAMS", argv[i]);
            goto fail;
        }
	if (irccasecmp(argv[i], "mask") == 0) {
	    if (!is_ircmask(argv[++i])) {
		send_message(user, opserv, "OSMSG_INVALID_IRCMASK", argv[i]);
		goto fail;
	    }
	    if (!split_ircmask(argv[i],
                               &discrim->mask_nick,
                               &discrim->mask_ident,
                               &discrim->mask_host)) {
		send_message(user, opserv, "OSMSG_INVALID_IRCMASK", argv[i]);
		goto fail;
	    }
	} else if (irccasecmp(argv[i], "nick") == 0) {
	    discrim->mask_nick = argv[++i];
	} else if (irccasecmp(argv[i], "ident") == 0) {
	    discrim->mask_ident = argv[++i];
	} else if (irccasecmp(argv[i], "host") == 0) {
	    discrim->mask_host = argv[++i];
	} else if (irccasecmp(argv[i], "info") == 0) {
	    discrim->mask_info = argv[++i];
	} else if (irccasecmp(argv[i], "server") == 0) {
	    discrim->server = argv[++i];
	} else if (irccasecmp(argv[i], "ip") == 0) {
            j = irc_pton(&discrim->ip_mask, &discrim->ip_mask_bits, argv[++i]);
            if (!j) {
                send_message(user, opserv, "OSMSG_BAD_IP", argv[i]);
                goto fail;
            }
    } else if (irccasecmp(argv[i], "account") == 0) {
        if (discrim->authed == 0) {
            send_message(user, opserv, "OSMSG_ACCOUNTMASK_AUTHED");
            goto fail;
        }
        discrim->accountmask = argv[++i];
        discrim->authed = 1;
    } else if (irccasecmp(argv[i], "authed") == 0) {
        i++; /* true_string and false_string are macros! */
        if (true_string(argv[i])) {
            discrim->authed = 1;
        } else if (false_string(argv[i])) {
            if (discrim->accountmask) {
                send_message(user, opserv, "OSMSG_ACCOUNTMASK_AUTHED");
                goto fail;
            }
            discrim->authed = 0;
        } else {
            send_message(user, opserv, "MSG_INVALID_BINARY", argv[i]);
            goto fail;
        }
    } else if (irccasecmp(argv[i], "info_space") == 0) {
        /* XXX: A hack because you can't check explicitly for a space through
         * any other means */
        i++;
        if (true_string(argv[i])) {
            discrim->info_space = 1;
        } else if (false_string(argv[i])) {
            discrim->info_space = 0;
        } else {
            send_message(user, opserv, "MSG_INVALID_BINARY", argv[i]);
            goto fail;
        }
    } else if (irccasecmp(argv[i], "duration") == 0) {
        discrim->duration = ParseInterval(argv[++i]);
	} else if (irccasecmp(argv[i], "channel") == 0) {
            for (j=0, i++; ; j++) {
                switch (argv[i][j]) {
                case '#':
                    goto find_channel;
                case '-':
                    discrim->chan_no_modes  |= MODE_CHANOP | MODE_VOICE;
                    break;
                case '+':
                    discrim->chan_req_modes |= MODE_VOICE;
                    discrim->chan_no_modes  |= MODE_CHANOP;
                    break;
                case '@':
                    discrim->chan_req_modes |= MODE_CHANOP;
                    break;
                case '\0':
                    send_message(user, opserv, "MSG_NOT_CHANNEL_NAME");
                    goto fail;
                }
            }
          find_channel:
            discrim->chan_no_modes &= ~discrim->chan_req_modes;
	    if (!(discrim->channel = GetChannel(argv[i]+j))) {
                /* secretly "allow_channel" now means "if a channel name is
                 * specified, require that it currently exist" */
                if (allow_channel) {
                    send_message(user, opserv, "MSG_CHANNEL_UNKNOWN", argv[i]);
                    goto fail;
                } else {
                    discrim->channel = AddChannel(argv[i]+j, now, NULL, NULL);
                }
	    }
            LockChannel(discrim->channel);
        } else if (irccasecmp(argv[i], "numchannels") == 0) {
            discrim->min_channels = discrim->max_channels = strtoul(argv[++i], NULL, 10);
	} else if (irccasecmp(argv[i], "limit") == 0) {
	    discrim->limit = strtoul(argv[++i], NULL, 10);
        } else if (irccasecmp(argv[i], "reason") == 0) {
            discrim->reason = strdup(unsplit_string(argv+i+1, argc-i-1, NULL));
            i = argc;
        } else if (irccasecmp(argv[i], "last") == 0) {
            discrim->min_ts = now - ParseInterval(argv[++i]);
        } else if ((irccasecmp(argv[i], "linked") == 0)
                   || (irccasecmp(argv[i], "nickage") == 0)) {
            const char *cmp = argv[++i];
            if (cmp[0] == '<') {
                if (cmp[1] == '=') {
                    discrim->min_ts = now - ParseInterval(cmp+2);
                } else {
                    discrim->min_ts = now - (ParseInterval(cmp+1) - 1);
                }
            } else if (cmp[0] == '>') {
                if (cmp[1] == '=') {
                    discrim->max_ts = now - ParseInterval(cmp+2);
                } else {
                    discrim->max_ts = now - (ParseInterval(cmp+1) - 1);
                }
            } else {
                discrim->min_ts = now - ParseInterval(cmp+2);
            }
        } else if (irccasecmp(argv[i], "access") == 0) {
            const char *cmp = argv[++i];
            if (cmp[0] == '<') {
                if (discrim->min_level == 0) discrim->min_level = 1;
                if (cmp[1] == '=') {
                    discrim->max_level = strtoul(cmp+2, NULL, 0);
                } else {
                    discrim->max_level = strtoul(cmp+1, NULL, 0) - 1;
                }
            } else if (cmp[0] == '=') {
                discrim->min_level = discrim->max_level = strtoul(cmp+1, NULL, 0);
            } else if (cmp[0] == '>') {
                if (cmp[1] == '=') {
                    discrim->min_level = strtoul(cmp+2, NULL, 0);
                } else {
                    discrim->min_level = strtoul(cmp+1, NULL, 0) + 1;
                }
            } else {
                discrim->min_level = strtoul(cmp+2, NULL, 0);
            }
        } else if ((irccasecmp(argv[i], "abuse") == 0)
                   && (irccasecmp(argv[++i], "opers") == 0)) {
            discrim->match_opers = 1;
        } else if (irccasecmp(argv[i], "depth") == 0) {
            discrim->domain_depth = strtoul(argv[++i], NULL, 0);
        } else if (irccasecmp(argv[i], "clones") == 0) {
            discrim->min_clones = strtoul(argv[++i], NULL, 0);
        } else {
            send_message(user, opserv, "MSG_INVALID_CRITERIA", argv[i]);
            goto fail;
        }
    }

    if (discrim->mask_nick && !strcmp(discrim->mask_nick, "*")) {
	discrim->mask_nick = 0;
    }
    if (discrim->mask_ident && !strcmp(discrim->mask_ident, "*")) {
        discrim->mask_ident = 0;
    }
    if (discrim->mask_info && !strcmp(discrim->mask_info, "*")) {
	discrim->mask_info = 0;
    }
    if (discrim->mask_host && !discrim->mask_host[strspn(discrim->mask_host, "*.")]) {
        discrim->mask_host = 0;
    }
    return discrim;
  fail:
    free(discrim);
    return NULL;
}

static int
discrim_match(discrim_t discrim, struct userNode *user)
{
    unsigned int access;

    if ((user->timestamp < discrim->min_ts)
        || (user->timestamp > discrim->max_ts)
        || (user->channels.used < discrim->min_channels)
        || (user->channels.used > discrim->max_channels)
        || (discrim->authed == 0 && user->handle_info)
        || (discrim->authed == 1 && !user->handle_info)
        || (discrim->info_space == 0 && user->info[0] == ' ')
        || (discrim->info_space == 1 && user->info[0] != ' ')
        || (discrim->mask_nick && !match_ircglob(user->nick, discrim->mask_nick))
        || (discrim->mask_ident && !match_ircglob(user->ident, discrim->mask_ident))
        || (discrim->mask_host && !match_ircglob(user->hostname, discrim->mask_host))
        || (discrim->mask_info && !match_ircglob(user->info, discrim->mask_info))
        || (discrim->server && !match_ircglob(user->uplink->name, discrim->server))
        || (discrim->accountmask && (!user->handle_info || !match_ircglob(user->handle_info->handle, discrim->accountmask)))
        || (discrim->ip_mask_bits && !irc_check_mask(&user->ip, &discrim->ip_mask, discrim->ip_mask_bits))
        )
        return 0;
    if (discrim->channel && !GetUserMode(discrim->channel, user))
        return 0;
    access = user->handle_info ? user->handle_info->opserv_level : 0;
    if ((access < discrim->min_level)
        || (access > discrim->max_level)) {
        return 0;
    }
    if (discrim->min_clones > 1) {
        struct opserv_hostinfo *ohi = dict_find(opserv_hostinfo_dict, irc_ntoa(&user->ip), NULL);
        if (!ohi || (ohi->clients.used < discrim->min_clones))
            return 0;
    }
    return 1;
}

static unsigned int
opserv_discrim_search(discrim_t discrim, discrim_search_func dsf, void *data)
{
    unsigned int nn, count;
    struct userList matched;

    userList_init(&matched);
    /* Try most optimized search methods first */
    if (discrim->channel) {
        for (nn=0;
                (nn < discrim->channel->members.used)
                && (matched.used < discrim->limit);
                nn++) {
            struct modeNode *mn = discrim->channel->members.list[nn];
            if (((mn->modes & discrim->chan_req_modes) != discrim->chan_req_modes)
                    || ((mn->modes & discrim->chan_no_modes) != 0)) {
                continue;
            }
            if (discrim_match(discrim, mn->user)) {
                userList_append(&matched, mn->user);
            }
        }
    } else if (discrim->ip_mask_bits == 128) {
        struct opserv_hostinfo *ohi = dict_find(opserv_hostinfo_dict, irc_ntoa(&discrim->ip_mask), NULL);
        if (!ohi) {
            userList_clean(&matched);
            return 0;
        }
        for (nn=0; (nn<ohi->clients.used) && (matched.used < discrim->limit); nn++) {
            if (discrim_match(discrim, ohi->clients.list[nn])) {
                userList_append(&matched, ohi->clients.list[nn]);
            }
        }
    } else {
        dict_iterator_t it;
        for (it=dict_first(clients); it && (matched.used < discrim->limit); it=iter_next(it)) {
            if (discrim_match(discrim, iter_data(it))) {
                userList_append(&matched, iter_data(it));
            }
        }
    }

    if (!matched.used) {
        userList_clean(&matched);
        return 0;
    }

    if (discrim->option_log) {
        log_module(OS_LOG, LOG_INFO, "Logging matches for search:");
    }
    for (nn=0; nn<matched.used; nn++) {
        struct userNode *user = matched.list[nn];
        if (discrim->option_log) {
            log_module(OS_LOG, LOG_INFO, "  %s!%s@%s", user->nick, user->ident, user->hostname);
        }
        if (dsf(user, data)) {
	    /* If a search function returns true, it ran into a
	       problem. Stop going through the list. */
	    break;
	}
    }
    if (discrim->option_log) {
        log_module(OS_LOG, LOG_INFO, "End of matching users.");
    }
    count = matched.used;
    userList_clean(&matched);
    return count;
}

static int
trace_print_func(struct userNode *match, void *extra)
{
    struct discrim_and_source *das = extra;
    if (match->handle_info) {
        send_message_type(4, das->source, opserv, "%s!%s@%s %s", match->nick, match->ident, match->hostname, match->handle_info->handle);
    } else {
        send_message_type(4, das->source, opserv, "%s!%s@%s", match->nick, match->ident, match->hostname);
    }
    return 0;
}

static int
trace_count_func(UNUSED_ARG(struct userNode *match), UNUSED_ARG(void *extra))
{
    return 0;
}

static int
is_oper_victim(struct userNode *user, struct userNode *target, int match_opers)
{
    return !(IsService(target)
             || (!match_opers && IsOper(target))
             || (target->handle_info
                 && target->handle_info->opserv_level > user->handle_info->opserv_level));
}

static int
trace_gline_func(struct userNode *match, void *extra)
{
    struct discrim_and_source *das = extra;

    if (is_oper_victim(das->source, match, das->discrim->match_opers)) {
        opserv_block(match, das->source->handle_info->handle, das->discrim->reason, das->discrim->duration);
    }

    return 0;
}

static int
trace_kill_func(struct userNode *match, void *extra)
{
    struct discrim_and_source *das = extra;

    if (is_oper_victim(das->source, match, das->discrim->match_opers)) {
	char *reason;
        if (das->discrim->reason) {
            reason = das->discrim->reason;
        } else {
            reason = alloca(strlen(OSMSG_KILL_REQUESTED)+strlen(das->source->nick)+1);
            sprintf(reason, OSMSG_KILL_REQUESTED, das->source->nick);
        }
        DelUser(match, opserv, 1, reason);
    }

    return 0;
}

static int
is_gagged(char *mask)
{
    struct gag_entry *gag;

    for (gag = gagList; gag; gag = gag->next) {
        if (match_ircglobs(gag->mask, mask)) return 1;
    }
    return 0;
}

static int
trace_gag_func(struct userNode *match, void *extra)
{
    struct discrim_and_source *das = extra;

    if (is_oper_victim(das->source, match, das->discrim->match_opers)) {
        char *reason, *mask;
        int masksize;
        if (das->discrim->reason) {
            reason = das->discrim->reason;
        } else {
            reason = alloca(strlen(OSMSG_GAG_REQUESTED)+strlen(das->source->nick)+1);
            sprintf(reason, OSMSG_GAG_REQUESTED, das->source->nick);
        }
	masksize = 5+strlen(match->hostname);
	mask = alloca(masksize);
        snprintf(mask, masksize, "*!*@%s", match->hostname);
	if (!is_gagged(mask)) {
            gag_create(mask, das->source->handle_info->handle, reason,
                       das->discrim->duration ? (now + das->discrim->duration) : 0);
        }
    }

    return 0;
}

static int
trace_domains_func(struct userNode *match, void *extra)
{
    struct discrim_and_source *das = extra;
    irc_in_addr_t ip;
    unsigned long *count;
    unsigned int depth;
    char *hostname;
    char ipmask[IRC_NTOP_MASK_MAX_SIZE];

    if (irc_pton(&ip, NULL, match->hostname)) {
        if (irc_in_addr_is_ipv4(ip)) {
            unsigned long matchip = ntohl(ip.in6_32[3]);
            /* raw IP address.. use up to first three octets of IP */
            switch (das->discrim->domain_depth) {
            default:
                snprintf(ipmask, sizeof(ipmask), "%lu.%lu.%lu.*", (matchip>>24)&255, (matchip>>16)&255, (matchip>>8)&255);
                break;
            case 2:
                snprintf(ipmask, sizeof(ipmask), "%lu.%lu.*", (matchip>>24)&255, (matchip>>16)&255);
                break;
            case 1:
                snprintf(ipmask, sizeof(ipmask), "%lu.*", (matchip>>24)&255);
                break;
            }
        } else if (irc_in_addr_is_ipv6(ip)) {
            switch (das->discrim->domain_depth) {
            case 1:  depth = 16; goto ipv6_pfx;
            case 2:  depth = 24; goto ipv6_pfx;
            case 3:  depth = 32; goto ipv6_pfx;
            default: depth = das->discrim->domain_depth;
            ipv6_pfx:
                irc_ntop_mask(ipmask, sizeof(ipmask), &ip, depth);
            }
        } else safestrncpy(ipmask, match->hostname, sizeof(ipmask));
        ipmask[sizeof(ipmask) - 1] = '\0';
        hostname = ipmask;
    } else {
        hostname = match->hostname + strlen(match->hostname);
        for (depth=das->discrim->domain_depth;
             depth && (hostname > match->hostname);
             depth--) {
            hostname--;
            while ((hostname > match->hostname) && (*hostname != '.')) hostname--;
        }
        if (*hostname == '.') hostname++; /* advance past last dot we saw */
    }
    if (!(count = dict_find(das->dict, hostname, NULL))) {
        count = calloc(1, sizeof(*count));
        dict_insert(das->dict, strdup(hostname), count);
    }
    (*count)++;
    return 0;
}

static int
opserv_show_hostinfo(const char *key, void *data, void *extra)
{
    unsigned long *count = data;
    struct discrim_and_source *das = extra;

    send_message_type(4, das->source, opserv, "%s %lu", key, *count);
    return !--das->disp_limit;
}

static MODCMD_FUNC(cmd_trace)
{
    struct discrim_and_source das;
    discrim_search_func action;
    unsigned int matches;
    struct svccmd *subcmd;
    char buf[MAXLEN];

    sprintf(buf, "trace %s", argv[1]);
    if (!(subcmd = dict_find(cmd->parent->commands, buf, NULL))) {
	reply("OSMSG_BAD_ACTION", argv[1]);
        return 0;
    }
    if (!svccmd_can_invoke(user, cmd->parent->bot, subcmd, channel, SVCCMD_NOISY))
        return 0;
    if (!irccasecmp(argv[1], "print"))
        action = trace_print_func;
    else if (!irccasecmp(argv[1], "count"))
        action = trace_count_func;
    else if (!irccasecmp(argv[1], "domains"))
        action = trace_domains_func;
    else if (!irccasecmp(argv[1], "gline"))
        action = trace_gline_func;
    else if (!irccasecmp(argv[1], "kill"))
        action = trace_kill_func;
    else if (!irccasecmp(argv[1], "gag"))
        action = trace_gag_func;
    else {
	reply("OSMSG_BAD_ACTION", argv[1]);
	return 0;
    }

    if (user->handle_info->opserv_level < subcmd->min_opserv_level) {
        reply("OSMSG_LEVEL_TOO_LOW");
        return 0;
    }

    das.dict = NULL;
    das.source = user;
    das.discrim = opserv_discrim_create(user, argc-2, argv+2, 1);
    if (!das.discrim)
        return 0;

    if (action == trace_print_func)
	reply("OSMSG_USER_SEARCH_RESULTS");
    else if (action == trace_count_func)
	das.discrim->limit = INT_MAX;
    else if ((action == trace_gline_func) && !das.discrim->duration)
        das.discrim->duration = opserv_conf.block_gline_duration;
    else if (action == trace_domains_func) {
        das.dict = dict_new();
        dict_set_free_data(das.dict, free);
        dict_set_free_keys(das.dict, free);
        das.disp_limit = das.discrim->limit;
        das.discrim->limit = INT_MAX;
    }
    matches = opserv_discrim_search(das.discrim, action, &das);

    if (action == trace_domains_func)
        dict_foreach(das.dict, opserv_show_hostinfo, &das);

    if (matches)
	reply("MSG_MATCH_COUNT", matches);
    else
	reply("MSG_NO_MATCHES");

    if (das.discrim->channel)
        UnlockChannel(das.discrim->channel);
    free(das.discrim->reason);
    free(das.discrim);
    dict_delete(das.dict);
    return 1;
}

typedef void (*cdiscrim_search_func)(struct chanNode *match, void *data);

typedef struct channel_discrim {
    char *name, *topic;

    unsigned int min_users, max_users;
    time_t min_ts, max_ts;
    unsigned int limit;
} *cdiscrim_t;

static cdiscrim_t opserv_cdiscrim_create(struct userNode *user, unsigned int argc, char *argv[]);
static unsigned int opserv_cdiscrim_search(cdiscrim_t discrim, cdiscrim_search_func dsf, void *data);

static time_t
smart_parse_time(const char *str) {
    /* If an interval-style string is given, treat as time before now.
     * If it's all digits, treat directly as a Unix timestamp. */
    return str[strspn(str, "0123456789")] ? (time_t)(now - ParseInterval(str)) : (time_t)atoi(str);
}

static cdiscrim_t
opserv_cdiscrim_create(struct userNode *user, unsigned int argc, char *argv[])
{
    cdiscrim_t discrim;
    unsigned int i;

    discrim = calloc(1, sizeof(*discrim));
    discrim->limit = 25;

    for (i = 0; i < argc; i++) {
	/* Assume all criteria require arguments. */
	if (i == (argc - 1)) {
	    send_message(user, opserv, "MSG_MISSING_PARAMS", argv[i]);
	    return NULL;
	}

	if (!irccasecmp(argv[i], "name"))
	    discrim->name = argv[++i];
	else if (!irccasecmp(argv[i], "topic"))
	    discrim->topic = argv[++i];
	else if (!irccasecmp(argv[i], "users")) {
	    const char *cmp = argv[++i];
            if (cmp[0] == '<') {
                if (cmp[1] == '=')
                    discrim->max_users = strtoul(cmp+2, NULL, 0);
                else
                    discrim->max_users = strtoul(cmp+1, NULL, 0) - 1;
            } else if (cmp[0] == '=') {
                discrim->min_users = discrim->max_users = strtoul(cmp+1, NULL, 0);
            } else if (cmp[0] == '>') {
                if (cmp[1] == '=')
                    discrim->min_users = strtoul(cmp+2, NULL, 0);
                else
                    discrim->min_users = strtoul(cmp+1, NULL, 0) + 1;
            } else {
                discrim->min_users = strtoul(cmp+2, NULL, 0);
            }
	} else if (!irccasecmp(argv[i], "timestamp")) {
	    const char *cmp = argv[++i];
            if (cmp[0] == '<') {
                if (cmp[1] == '=')
                    discrim->max_ts = smart_parse_time(cmp+2);
                else
                    discrim->max_ts = smart_parse_time(cmp+1)-1;
            } else if (cmp[0] == '=') {
                discrim->min_ts = discrim->max_ts = smart_parse_time(cmp+1);
            } else if (cmp[0] == '>') {
                if (cmp[1] == '=')
                    discrim->min_ts = smart_parse_time(cmp+2);
                else
                    discrim->min_ts = smart_parse_time(cmp+1)+1;
            } else {
                discrim->min_ts = smart_parse_time(cmp);
            }
	} else if (!irccasecmp(argv[i], "limit")) {
	    discrim->limit = strtoul(argv[++i], NULL, 10);
	} else {
	    send_message(user, opserv, "MSG_INVALID_CRITERIA", argv[i]);
	    goto fail;
	}
    }

    if (discrim->name && !strcmp(discrim->name, "*"))
	discrim->name = 0;
    if (discrim->topic && !strcmp(discrim->topic, "*"))
	discrim->topic = 0;

    return discrim;
  fail:
    free(discrim);
    return NULL;
}

static int
cdiscrim_match(cdiscrim_t discrim, struct chanNode *chan)
{
    if ((discrim->name && !match_ircglob(chan->name, discrim->name)) ||
        (discrim->topic && !match_ircglob(chan->topic, discrim->topic)) ||
        (discrim->min_users && chan->members.used < discrim->min_users) ||
        (discrim->max_users && chan->members.used > discrim->max_users) ||
        (discrim->min_ts && chan->timestamp < discrim->min_ts) ||
            (discrim->max_ts && chan->timestamp > discrim->max_ts)) {
	return 0;
    }
    return 1;
}

static unsigned int opserv_cdiscrim_search(cdiscrim_t discrim, cdiscrim_search_func dsf, void *data)
{
    unsigned int count = 0;
    dict_iterator_t it, next;

    for (it = dict_first(channels); it && count < discrim->limit ; it = next) {
	struct chanNode *chan = iter_data(it);

	/* Hold on to the next channel in case we decide to
	   add actions that destructively modify the channel. */
	next = iter_next(it);
	if ((chan->members.used > 0) && cdiscrim_match(discrim, chan)) {
	    dsf(chan, data);
	    count++;
	}
    }

    return count;
}

void channel_count(UNUSED_ARG(struct chanNode *channel), UNUSED_ARG(void *data))
{
}

void channel_print(struct chanNode *channel, void *data)
{
    char modes[MAXLEN];
    irc_make_chanmode(channel, modes);
    send_message(data, opserv, "OSMSG_CSEARCH_CHANNEL_INFO", channel->name, channel->members.used, modes, channel->topic);
}

static MODCMD_FUNC(cmd_csearch)
{
    cdiscrim_t discrim;
    unsigned int matches;
    cdiscrim_search_func action;
    struct svccmd *subcmd;
    char buf[MAXLEN];

    if (!irccasecmp(argv[1], "count"))
	action = channel_count;
    else if (!irccasecmp(argv[1], "print"))
	action = channel_print;
    else {
	reply("OSMSG_BAD_ACTION", argv[1]);
	return 0;
    }

    sprintf(buf, "%s %s", argv[0], argv[0]);
    if ((subcmd = dict_find(cmd->parent->commands, buf, NULL))
        && !svccmd_can_invoke(user, cmd->parent->bot, subcmd, channel, SVCCMD_NOISY)) {
        return 0;
    }

    discrim = opserv_cdiscrim_create(user, argc - 2, argv + 2);
    if (!discrim)
	return 0;

    if (action == channel_print)
	reply("OSMSG_CHANNEL_SEARCH_RESULTS");
    else if (action == channel_count)
	discrim->limit = INT_MAX;

    matches = opserv_cdiscrim_search(discrim, action, user);

    if (matches)
	reply("MSG_MATCH_COUNT", matches);
    else
	reply("MSG_NO_MATCHES");

    free(discrim);
    return 1;
}

static MODCMD_FUNC(cmd_gsync)
{
    struct server *src;
    if (argc > 1) {
        src = GetServerH(argv[1]);
        if (!src) {
            reply("MSG_SERVER_UNKNOWN", argv[1]);
            return 0;
        }
    } else {
        src = self->uplink;
    }
    irc_stats(cmd->parent->bot, src, 'G');
    reply("OSMSG_GSYNC_RUNNING", src->name);
    return 1;
}

struct gline_extra {
    struct userNode *user;
    struct string_list *glines;
};

static void
gtrace_print_func(struct gline *gline, void *extra)
{
    struct gline_extra *xtra = extra;
    char *when_text, set_text[20];
    strftime(set_text, sizeof(set_text), "%Y-%m-%d", localtime(&gline->issued));
    when_text = asctime(localtime(&gline->expires));
    when_text[strlen(when_text)-1] = 0; /* strip lame \n */
    send_message(xtra->user, opserv, "OSMSG_GTRACE_FORMAT", gline->target, set_text, gline->issuer, when_text, gline->reason);
}

static void
gtrace_count_func(UNUSED_ARG(struct gline *gline), UNUSED_ARG(void *extra))
{
}

static void
gtrace_ungline_func(struct gline *gline, void *extra)
{
    struct gline_extra *xtra = extra;
    string_list_append(xtra->glines, strdup(gline->target));
}

static MODCMD_FUNC(cmd_gtrace)
{
    struct gline_discrim *discrim;
    gline_search_func action;
    unsigned int matches, nn;
    struct gline_extra extra;
    struct svccmd *subcmd;
    char buf[MAXLEN];

    if (!irccasecmp(argv[1], "print"))
        action = gtrace_print_func;
    else if (!irccasecmp(argv[1], "count"))
        action = gtrace_count_func;
    else if (!irccasecmp(argv[1], "ungline"))
        action = gtrace_ungline_func;
    else {
        reply("OSMSG_BAD_ACTION", argv[1]);
        return 0;
    }
    sprintf(buf, "%s %s", argv[0], argv[0]);
    if ((subcmd = dict_find(cmd->parent->commands, buf, NULL))
        && !svccmd_can_invoke(user, cmd->parent->bot, subcmd, channel, SVCCMD_NOISY)) {
        return 0;
    }

    discrim = gline_discrim_create(user, cmd->parent->bot, argc-2, argv+2);
    if (!discrim)
        return 0;

    if (action == gtrace_print_func)
        reply("OSMSG_GLINE_SEARCH_RESULTS");
    else if (action == gtrace_count_func)
        discrim->limit = INT_MAX;

    extra.user = user;
    extra.glines = alloc_string_list(4);
    matches = gline_discrim_search(discrim, action, &extra);

    if (action == gtrace_ungline_func)
        for (nn=0; nn<extra.glines->used; nn++)
            gline_remove(extra.glines->list[nn], 1);
    free_string_list(extra.glines);

    if (matches)
        reply("MSG_MATCH_COUNT", matches);
    else
        reply("MSG_NO_MATCHES");
    free(discrim->alt_target_mask);
    free(discrim);
    return 1;
}

static int
alert_check_user(const char *key, void *data, void *extra)
{
    struct opserv_user_alert *alert = data;
    struct userNode *user = extra;

    if (!discrim_match(alert->discrim, user))
        return 0;

    if ((alert->reaction != REACT_NOTICE)
        && IsOper(user)
        && !alert->discrim->match_opers) {
        return 0;
    }

    /* The user matches the alert criteria, so trigger the reaction. */
    if (alert->discrim->option_log)
        log_module(OS_LOG, LOG_INFO, "Alert %s triggered by user %s!%s@%s (%s).", key, user->nick, user->ident, user->hostname, alert->discrim->reason);

    /* Return 1 to halt alert matching, such as when killing the user
       that triggered the alert. */
    switch (alert->reaction) {
    case REACT_KILL:
        DelUser(user, opserv, 1, alert->discrim->reason);
        return 1;
    case REACT_GLINE:
        opserv_block(user, alert->owner, alert->discrim->reason, alert->discrim->duration);
        return 1;
    default:
        log_module(OS_LOG, LOG_ERROR, "Invalid reaction type %d for alert %s.", alert->reaction, key);
        /* fall through to REACT_NOTICE case */
    case REACT_NOTICE:
        opserv_alert("Alert $b%s$b triggered by user $b%s$b!%s@%s (%s).", key, user->nick, user->ident, user->hostname, alert->discrim->reason);
        break;
    }
    return 0;
}

static void
opserv_alert_check_nick(struct userNode *user, UNUSED_ARG(const char *old_nick))
{
    struct gag_entry *gag;
    dict_foreach(opserv_nick_based_alerts, alert_check_user, user);
    /* Gag them if appropriate (and only if). */
    user->modes &= ~FLAGS_GAGGED;
    for (gag = gagList; gag; gag = gag->next) {
        if (user_matches_glob(user, gag->mask, 1)) {
            gag_helper_func(user, NULL);
            break;
        }
    }
}

static void
opserv_staff_alert(struct userNode *user, UNUSED_ARG(struct handle_info *old_handle))
{
    const char *type;

    if (!opserv_conf.staff_auth_channel
        || user->uplink->burst
        || !user->handle_info)
        return;
    else if (user->handle_info->opserv_level)
        type = "OPER";
    else if (IsNetworkHelper(user))
        type = "NETWORK HELPER";
    else if (IsSupportHelper(user))
        type = "SUPPORT HELPER";
    else
        return;

    if (irc_in_addr_is_valid(user->ip))
        send_channel_notice(opserv_conf.staff_auth_channel, opserv, IDENT_FORMAT" authed to %s account %s", IDENT_DATA(user), type, user->handle_info->handle);
    else
        send_channel_notice(opserv_conf.staff_auth_channel, opserv, "%s [%s@%s] authed to %s account %s", user->nick, user->ident, user->hostname, type, user->handle_info->handle);
}

static MODCMD_FUNC(cmd_log)
{
    struct logSearch *discrim;
    unsigned int matches;
    struct logReport report;

    discrim = log_discrim_create(cmd->parent->bot, user, argc, argv);
    if (!discrim)
        return 0;

    reply("OSMSG_LOG_SEARCH_RESULTS");
    report.reporter = opserv;
    report.user = user;
    matches = log_entry_search(discrim, log_report_entry, &report);

    if (matches)
	reply("MSG_MATCH_COUNT", matches);
    else
	reply("MSG_NO_MATCHES");

    free(discrim);
    return 1;
}

static int
gag_helper_func(struct userNode *match, UNUSED_ARG(void *extra))
{
    if (IsOper(match) || IsLocal(match))
        return 0;
    match->modes |= FLAGS_GAGGED;
    return 0;
}

static MODCMD_FUNC(cmd_gag)
{
    struct gag_entry *gag;
    unsigned int gagged;
    unsigned long duration;
    char *reason;

    reason = unsplit_string(argv + 3, argc - 3, NULL);

    if (!is_ircmask(argv[1])) {
	reply("OSMSG_INVALID_IRCMASK", argv[1]);
        return 0;
    }

    for (gag = gagList; gag; gag = gag->next)
	if (match_ircglobs(gag->mask, argv[1]))
            break;

    if (gag) {
	reply("OSMSG_REDUNDANT_GAG", argv[1]);
	return 0;
    }

    duration = ParseInterval(argv[2]);
    gagged = gag_create(argv[1], user->handle_info->handle, reason, (duration?now+duration:0));

    if (gagged)
	reply("OSMSG_GAG_APPLIED", argv[1], gagged);
    else
	reply("OSMSG_GAG_ADDED", argv[1]);
    return 1;
}

static int
ungag_helper_func(struct userNode *match, UNUSED_ARG(void *extra))
{
    match->modes &= ~FLAGS_GAGGED;
    return 0;
}

static MODCMD_FUNC(cmd_ungag)
{
    struct gag_entry *gag;
    unsigned int ungagged;

    for (gag = gagList; gag; gag = gag->next)
	if (!strcmp(gag->mask, argv[1]))
            break;

    if (!gag) {
	reply("OSMSG_GAG_NOT_FOUND", argv[1]);
	return 0;
    }

    timeq_del(gag->expires, gag_expire, gag, 0);
    ungagged = gag_free(gag);

    if (ungagged)
	reply("OSMSG_UNGAG_APPLIED", argv[1], ungagged);
    else
	reply("OSMSG_UNGAG_ADDED", argv[1]);
    return 1;
}

static MODCMD_FUNC(cmd_addalert)
{
    opserv_alert_reaction reaction;
    struct svccmd *subcmd;
    const char *name;
    char buf[MAXLEN];

    name = argv[1];
    sprintf(buf, "addalert %s", argv[2]);
    if (!(subcmd = dict_find(cmd->parent->commands, buf, NULL))) {
	reply("OSMSG_UNKNOWN_REACTION", argv[2]);
	return 0;
    }
    if (!irccasecmp(argv[2], "notice"))
        reaction = REACT_NOTICE;
    else if (!irccasecmp(argv[2], "kill"))
        reaction = REACT_KILL;
    else if (!irccasecmp(argv[2], "gline"))
        reaction = REACT_GLINE;
    else {
	reply("OSMSG_UNKNOWN_REACTION", argv[2]);
	return 0;
    }
    if (!svccmd_can_invoke(user, cmd->parent->bot, subcmd, channel, SVCCMD_NOISY)
        || !opserv_add_user_alert(user, name, reaction, unsplit_string(argv + 3, argc - 3, NULL)))
        return 0;
    reply("OSMSG_ADDED_ALERT", name);
    return 1;
}

static MODCMD_FUNC(cmd_delalert)
{
    unsigned int i;
    for (i=1; i<argc; i++) {
        dict_remove(opserv_nick_based_alerts, argv[i]);
        dict_remove(opserv_channel_alerts, argv[i]);
	if (dict_remove(opserv_user_alerts, argv[i]))
	    reply("OSMSG_REMOVED_ALERT", argv[i]);
        else
	    reply("OSMSG_NO_SUCH_ALERT", argv[i]);
    }
    return 1;
}

static void
opserv_conf_read(void)
{
    struct record_data *rd;
    dict_t conf_node, child;
    const char *str, *str2;
    struct policer_params *pp;
    dict_iterator_t it;

    rd = conf_get_node(OPSERV_CONF_NAME);
    if (!rd || rd->type != RECDB_OBJECT) {
	log_module(OS_LOG, LOG_ERROR, "config node `%s' is missing or has wrong type.", OPSERV_CONF_NAME);
	return;
    }
    conf_node = rd->d.object;
    str = database_get_data(conf_node, KEY_DEBUG_CHANNEL, RECDB_QSTRING);
    if (opserv && str) {
        str2 = database_get_data(conf_node, KEY_DEBUG_CHANNEL_MODES, RECDB_QSTRING);
        if (!str2)
            str2 = "+tinms";
	opserv_conf.debug_channel = AddChannel(str, now, str2, NULL);
        AddChannelUser(opserv, opserv_conf.debug_channel)->modes |= MODE_CHANOP;
    } else {
	opserv_conf.debug_channel = NULL;
    }
    str = database_get_data(conf_node, KEY_ALERT_CHANNEL, RECDB_QSTRING);
    if (opserv && str) {
        str2 = database_get_data(conf_node, KEY_ALERT_CHANNEL_MODES, RECDB_QSTRING);
        if (!str2)
            str2 = "+tns";
	opserv_conf.alert_channel = AddChannel(str, now, str2, NULL);
        AddChannelUser(opserv, opserv_conf.alert_channel)->modes |= MODE_CHANOP;
    } else {
	opserv_conf.alert_channel = NULL;
    }
    str = database_get_data(conf_node, KEY_STAFF_AUTH_CHANNEL, RECDB_QSTRING);
    if (opserv && str) {
        str2 = database_get_data(conf_node, KEY_STAFF_AUTH_CHANNEL_MODES, RECDB_QSTRING);
        if (!str2)
            str2 = "+timns";
        opserv_conf.staff_auth_channel = AddChannel(str, now, str2, NULL);
        AddChannelUser(opserv, opserv_conf.staff_auth_channel)->modes |= MODE_CHANOP;
    } else {
        opserv_conf.staff_auth_channel = NULL;
    }
    str = database_get_data(conf_node, KEY_UNTRUSTED_MAX, RECDB_QSTRING);
    opserv_conf.untrusted_max = str ? strtoul(str, NULL, 0) : 5;
    str = database_get_data(conf_node, KEY_PURGE_LOCK_DELAY, RECDB_QSTRING);
    opserv_conf.purge_lock_delay = str ? strtoul(str, NULL, 0) : 60;
    str = database_get_data(conf_node, KEY_JOIN_FLOOD_MODERATE, RECDB_QSTRING);
    opserv_conf.join_flood_moderate = str ? strtoul(str, NULL, 0) : 1;
    str = database_get_data(conf_node, KEY_JOIN_FLOOD_MODERATE_THRESH, RECDB_QSTRING);
    opserv_conf.join_flood_moderate_threshold = str ? strtoul(str, NULL, 0) : 50;
    str = database_get_data(conf_node, KEY_NICK, RECDB_QSTRING);
    if (opserv && str)
        NickChange(opserv, str, 0);
    str = database_get_data(conf_node, KEY_CLONE_GLINE_DURATION, RECDB_QSTRING);
    opserv_conf.clone_gline_duration = str ? ParseInterval(str) : 3600;
    str = database_get_data(conf_node, KEY_BLOCK_GLINE_DURATION, RECDB_QSTRING);
    opserv_conf.block_gline_duration = str ? ParseInterval(str) : 3600;

    if (!opserv_conf.join_policer_params)
        opserv_conf.join_policer_params = policer_params_new();
    policer_params_set(opserv_conf.join_policer_params, "size", "20");
    policer_params_set(opserv_conf.join_policer_params, "drain-rate", "1");
    if ((child = database_get_data(conf_node, KEY_JOIN_POLICER, RECDB_OBJECT)))
	dict_foreach(child, set_policer_param, opserv_conf.join_policer_params);

    for (it = dict_first(channels); it; it = iter_next(it)) {
        struct chanNode *cNode = iter_data(it);
        cNode->join_policer.params = opserv_conf.join_policer_params;
    }

    if (opserv_conf.new_user_policer.params)
        pp = opserv_conf.new_user_policer.params;
    else
        pp = opserv_conf.new_user_policer.params = policer_params_new();
    policer_params_set(pp, "size", "200");
    policer_params_set(pp, "drain-rate", "3");
    if ((child = database_get_data(conf_node, KEY_NEW_USER_POLICER, RECDB_OBJECT)))
	dict_foreach(child, set_policer_param, pp);
}

static void
opserv_db_init(void) {
    /* set up opserv_trusted_hosts dict */
    dict_delete(opserv_trusted_hosts);
    opserv_trusted_hosts = dict_new();
    dict_set_free_data(opserv_trusted_hosts, free_trusted_host);
    /* set up opserv_chan_warn dict */
    dict_delete(opserv_chan_warn);
    opserv_chan_warn = dict_new();
    dict_set_free_keys(opserv_chan_warn, free);
    dict_set_free_data(opserv_chan_warn, free);
    /* set up opserv_user_alerts */
    dict_delete(opserv_channel_alerts);
    opserv_channel_alerts = dict_new();
    dict_delete(opserv_nick_based_alerts);
    opserv_nick_based_alerts = dict_new();
    dict_delete(opserv_user_alerts);
    opserv_user_alerts = dict_new();
    dict_set_free_keys(opserv_user_alerts, free);
    dict_set_free_data(opserv_user_alerts, opserv_free_user_alert);
    /* set up opserv_bad_words */
    free_string_list(opserv_bad_words);
    opserv_bad_words = alloc_string_list(4);
    /* and opserv_exempt_channels */
    dict_delete(opserv_exempt_channels);
    opserv_exempt_channels = dict_new();
    dict_set_free_keys(opserv_exempt_channels, free);
}

static void
opserv_db_cleanup(void)
{
    unsigned int nn;

    dict_delete(opserv_chan_warn);
    dict_delete(opserv_reserved_nick_dict);
    free_string_list(opserv_bad_words);
    dict_delete(opserv_exempt_channels);
    dict_delete(opserv_trusted_hosts);
    unreg_del_user_func(opserv_user_cleanup);
    dict_delete(opserv_hostinfo_dict);
    dict_delete(opserv_nick_based_alerts);
    dict_delete(opserv_channel_alerts);
    dict_delete(opserv_user_alerts);
    for (nn=0; nn<ArrayLength(level_strings); ++nn)
        free(level_strings[nn]);
    while (gagList)
        gag_free(gagList);
    policer_params_delete(opserv_conf.join_policer_params);
    policer_params_delete(opserv_conf.new_user_policer.params);
}

void
init_opserv(const char *nick)
{
    OS_LOG = log_register_type("OpServ", "file:opserv.log");
    if (nick) {
        const char *modes = conf_get_data("services/opserv/modes", RECDB_QSTRING);
        opserv = AddService(nick, modes ? modes : NULL, "Oper Services", NULL);
    }
    conf_register_reload(opserv_conf_read);

    memset(level_strings, 0, sizeof(level_strings));
    opserv_module = module_register("OpServ", OS_LOG, "opserv.help", opserv_help_expand);
    opserv_define_func("ACCESS", cmd_access, 0, 0, 0);
    opserv_define_func("ADDALERT", cmd_addalert, 800, 0, 4);
    opserv_define_func("ADDALERT NOTICE", NULL, 0, 0, 0);
    opserv_define_func("ADDALERT GLINE", NULL, 900, 0, 0);
    opserv_define_func("ADDALERT KILL", NULL, 900, 0, 0);
    opserv_define_func("ADDBAD", cmd_addbad, 800, 0, 2);
    opserv_define_func("ADDEXEMPT", cmd_addexempt, 800, 0, 2);
    opserv_define_func("ADDTRUST", cmd_addtrust, 800, 0, 5);
    opserv_define_func("BAN", cmd_ban, 100, 2, 2);
    opserv_define_func("BLOCK", cmd_block, 100, 0, 2);
    opserv_define_func("CHANINFO", cmd_chaninfo, 0, 3, 0);
    opserv_define_func("CLEARBANS", cmd_clearbans, 300, 2, 0);
    opserv_define_func("CLEARMODES", cmd_clearmodes, 400, 2, 0);
    opserv_define_func("CLONE", cmd_clone, 999, 0, 3);
    opserv_define_func("COLLIDE", cmd_collide, 800, 0, 5);
    opserv_define_func("CSEARCH", cmd_csearch, 100, 0, 3);
    opserv_define_func("CSEARCH COUNT", cmd_csearch, 0, 0, 0);
    opserv_define_func("CSEARCH PRINT", cmd_csearch, 0, 0, 0);
    opserv_define_func("DELALERT", cmd_delalert, 800, 0, 2);
    opserv_define_func("DELBAD", cmd_delbad, 800, 0, 2);
    opserv_define_func("DELEXEMPT", cmd_delexempt, 800, 0, 2);
    opserv_define_func("DELTRUST", cmd_deltrust, 800, 0, 2);
    opserv_define_func("DEOP", cmd_deop, 100, 2, 2);
    opserv_define_func("DEOPALL", cmd_deopall, 400, 2, 0);
    opserv_define_func("DEVOICEALL", cmd_devoiceall, 300, 2, 0);
    opserv_define_func("DIE", cmd_die, 900, 0, 2);
    opserv_define_func("DUMP", cmd_dump, 999, 0, 2);
    opserv_define_func("EDITTRUST", cmd_edittrust, 800, 0, 5);
    opserv_define_func("GAG", cmd_gag, 600, 0, 4);
    opserv_define_func("GLINE", cmd_gline, 600, 0, 4);
    opserv_define_func("GSYNC", cmd_gsync, 600, 0, 0);
    opserv_define_func("GTRACE", cmd_gtrace, 100, 0, 3);
    opserv_define_func("GTRACE COUNT", NULL, 0, 0, 0);
    opserv_define_func("GTRACE PRINT", NULL, 0, 0, 0);
    opserv_define_func("INVITE", cmd_invite, 100, 2, 0);
    opserv_define_func("INVITEME", cmd_inviteme, 100, 0, 0);
    opserv_define_func("JOIN", cmd_join, 601, 0, 2);
    opserv_define_func("JUMP", cmd_jump, 900, 0, 2);
    opserv_define_func("JUPE", cmd_jupe, 900, 0, 4);
    opserv_define_func("KICK", cmd_kick, 100, 2, 2);
    opserv_define_func("KICKALL", cmd_kickall, 400, 2, 0);
    opserv_define_func("KICKBAN", cmd_kickban, 100, 2, 2);
    opserv_define_func("KICKBANALL", cmd_kickbanall, 450, 2, 0);
    opserv_define_func("LOG", cmd_log, 900, 0, 2);
    opserv_define_func("MODE", cmd_mode, 100, 2, 2);
    opserv_define_func("OP", cmd_op, 100, 2, 2);
    opserv_define_func("OPALL", cmd_opall, 400, 2, 0);
    opserv_define_func("PART", cmd_part, 601, 0, 2);
    opserv_define_func("QUERY", cmd_query, 0, 0, 0);
    opserv_define_func("RAW", cmd_raw, 999, 0, 2);
    opserv_define_func("RECONNECT", cmd_reconnect, 900, 0, 0);
    opserv_define_func("REFRESHG", cmd_refreshg, 600, 0, 0);
    opserv_define_func("REHASH", cmd_rehash, 900, 0, 0);
    opserv_define_func("REOPEN", cmd_reopen, 900, 0, 0);
    opserv_define_func("RESERVE", cmd_reserve, 800, 0, 5);
    opserv_define_func("RESTART", cmd_restart, 900, 0, 2);
    opserv_define_func("SET", cmd_set, 900, 0, 3);
    opserv_define_func("SETTIME", cmd_settime, 901, 0, 0);
    opserv_define_func("STATS ALERTS", cmd_stats_alerts, 0, 0, 0);
    opserv_define_func("STATS BAD", cmd_stats_bad, 0, 0, 0);
    opserv_define_func("STATS GAGS", cmd_stats_gags, 0, 0, 0);
    opserv_define_func("STATS GLINES", cmd_stats_glines, 0, 0, 0);
    opserv_define_func("STATS LINKS", cmd_stats_links, 0, 0, 0);
    opserv_define_func("STATS MAX", cmd_stats_max, 0, 0, 0);
    opserv_define_func("STATS NETWORK", cmd_stats_network, 0, 0, 0);
    opserv_define_func("STATS NETWORK2", cmd_stats_network2, 0, 0, 0);
    opserv_define_func("STATS RESERVED", cmd_stats_reserved, 0, 0, 0);
    opserv_define_func("STATS TIMEQ", cmd_stats_timeq, 0, 0, 0);
    opserv_define_func("STATS TRUSTED", cmd_stats_trusted, 0, 0, 0);
    opserv_define_func("STATS UPLINK", cmd_stats_uplink, 0, 0, 0);
    opserv_define_func("STATS UPTIME", cmd_stats_uptime, 0, 0, 0);
    opserv_define_func("STATS WARN", cmd_stats_warn, 0, 0, 0);
#if defined(WITH_MALLOC_SRVX) || defined(WITH_MALLOC_SLAB)
    opserv_define_func("STATS MEMORY", cmd_stats_memory, 0, 0, 0);
#endif
    opserv_define_func("TRACE", cmd_trace, 100, 0, 3);
    opserv_define_func("TRACE PRINT", NULL, 0, 0, 0);
    opserv_define_func("TRACE COUNT", NULL, 0, 0, 0);
    opserv_define_func("TRACE DOMAINS", NULL, 0, 0, 0);
    opserv_define_func("TRACE GLINE", NULL, 600, 0, 0);
    opserv_define_func("TRACE GAG", NULL, 600, 0, 0);
    opserv_define_func("TRACE KILL", NULL, 600, 0, 0);
    opserv_define_func("UNBAN", cmd_unban, 100, 2, 2);
    opserv_define_func("UNGAG", cmd_ungag, 600, 0, 2);
    opserv_define_func("UNGLINE", cmd_ungline, 600, 0, 2);
    modcmd_register(opserv_module, "GTRACE UNGLINE", NULL, 0, 0, "template", "ungline", NULL);
    opserv_define_func("UNJUPE", cmd_unjupe, 900, 0, 2);
    opserv_define_func("UNRESERVE", cmd_unreserve, 800, 0, 2);
    opserv_define_func("UNWARN", cmd_unwarn, 800, 0, 0);
    opserv_define_func("VOICEALL", cmd_voiceall, 300, 2, 0);
    opserv_define_func("WARN", cmd_warn, 800, 0, 2);
    opserv_define_func("WHOIS", cmd_whois, 0, 0, 2);

    opserv_reserved_nick_dict = dict_new();
    opserv_hostinfo_dict = dict_new();
    dict_set_free_keys(opserv_hostinfo_dict, free);
    dict_set_free_data(opserv_hostinfo_dict, opserv_free_hostinfo);

    reg_new_user_func(opserv_new_user_check);
    reg_nick_change_func(opserv_alert_check_nick);
    reg_del_user_func(opserv_user_cleanup);
    reg_new_channel_func(opserv_channel_check);
    reg_del_channel_func(opserv_channel_delete);
    reg_join_func(opserv_join_check);
    reg_auth_func(opserv_staff_alert);

    opserv_db_init();
    saxdb_register("OpServ", opserv_saxdb_read, opserv_saxdb_write);
    if (nick)
        service_register(opserv)->trigger = '?';

    reg_exit_func(opserv_db_cleanup);
    message_register_table(msgtab);
}

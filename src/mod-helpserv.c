/* mod-helpserv.c - Support Helper assistant service
 * Copyright 2002-2003 srvx Development Team
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

/* Wishlist for helpserv.c
 * - Make HelpServ send unassigned request count to helpers as they join the
 *   channel
 * - FAQ responses
 * - Get cmd_statsreport to sort by time and show requests closed
 * - .. then make statsreport show weighted combination of time and requests closed :)
 * - Something for users to use a subset of commands... like closing their
 *   own request(s), viewing what they've sent so far, and finding their
 *   helper's nick.
 * - It would be nice to have some variable expansions for the custom
 *   request open/close/etc messages. ${request/id}, etc
 * - Allow manipulation of the persist types on a per-request basis... so
 *   if it's normally set to close requests after a user quits, but there
 *   is a long-term issue, then that single request can remain
 * - Bot suspension
 */

#include "conf.h"
#include "global.h"
#include "modcmd.h"
#include "nickserv.h"
#include "opserv.h"
#include "saxdb.h"
#include "timeq.h"

#define HELPSERV_CONF_NAME "modules/helpserv"
#define HELPSERV_HELPFILE_NAME "mod-helpserv.help"
const char *helpserv_module_deps[] = { NULL };

/* db entries */
#define KEY_BOTS "bots"
#define KEY_LAST_STATS_UPDATE "last_stats_update"
#define KEY_NICK "nick"
#define KEY_DB_BADCHANS "badchans"
#define KEY_HELP_CHANNEL "help_channel"
#define KEY_PAGE_DEST "page_dest"
#define KEY_CMDWORD "cmdword"
#define KEY_PERSIST_LENGTH "persist_length"
#define KEY_REGISTERED "registered"
#define KEY_REGISTRAR "registrar"
#define KEY_IDWRAP "id_wrap"
#define KEY_REQ_MAXLEN "req_maxlen"
#define KEY_LAST_REQUESTID "last_requestid"
#define KEY_HELPERS "users"
#define KEY_HELPER_LEVEL "level"
#define KEY_HELPER_HELPMODE "helpmode"
#define KEY_HELPER_WEEKSTART "weekstart"
#define KEY_HELPER_STATS "stats"
#define KEY_HELPER_STATS_TIME "time"
#define KEY_HELPER_STATS_PICKUP "picked_up"
#define KEY_HELPER_STATS_CLOSE "closed"
#define KEY_HELPER_STATS_REASSIGNFROM "reassign_from"
#define KEY_HELPER_STATS_REASSIGNTO "reassign_to"
#define KEY_REQUESTS "requests"
#define KEY_REQUEST_HELPER "helper"
#define KEY_REQUEST_ASSIGNED "assigned"
#define KEY_REQUEST_HANDLE "handle"
#define KEY_REQUEST_TEXT "text"
#define KEY_REQUEST_OPENED "opened"
#define KEY_REQUEST_NICK "nick"
#define KEY_REQUEST_USERHOST "userhost"
#define KEY_REQUEST_CLOSED "closed"
#define KEY_REQUEST_CLOSEREASON "closereason"
#define KEY_NOTIFICATION "notification"
#define KEY_PRIVMSG_ONLY "privmsg_only"
#define KEY_REQ_ON_JOIN "req_on_join"
#define KEY_AUTO_VOICE "auto_voice"
#define KEY_AUTO_DEVOICE "auto_devoice"
#define KEY_LAST_ACTIVE "last_active"

/* General */
#define HSFMT_TIME               "%a, %d %b %Y %H:%M:%S %Z"
static const struct message_entry msgtab[] = {
    { "HSMSG_READHELP_SUCCESS", "Read HelpServ help database in "FMT_TIME_T".%03ld seconds." },
    { "HSMSG_INVALID_BOT", "This command requires a valid HelpServ bot name." },
    { "HSMSG_ILLEGAL_CHANNEL", "$b%s$b is an illegal channel; cannot use it." },
    { "HSMSG_INTERNAL_COMMAND", "$b%s$b appears to be an internal HelpServ command, sorry." },
    { "HSMSG_NOT_IN_USERLIST", "%s lacks access to $b%s$b." },
    { "HSMSG_LEVEL_TOO_LOW", "You lack access to this command." },
    { "HSMSG_OPER_CMD", "This command can only be executed via $O." },
    { "HSMSG_WTF_WHO_ARE_YOU", "$bUnable to find you on the %s userlist.$b This is a bug. Please report it." },
    { "HSMSG_NO_USE_OPSERV", "This command cannot be used via $O. If you really need to use it, add yourself to the userlist." },
    { "HSMSG_OPSERV_NEED_USER", "To use this command via $O, you must supply a user to target." },
    { "HSMSG_PAGE_REQUEST", "Page from $b%s$b: $b%s$b" },
    { "HSMSG_BAD_REQ_TYPE", "I don't know how to list $b%s$b requests." },

/* Greetings */
    { "HSMSG_GREET_EXISTING_REQ", "Welcome back to %s. You have already opened request ID#%lu. Any messages you send to $S will be appended to that request." },
    { "HSMSG_GREET_PRIVMSG_EXISTREQ", "Hello again. Your account has a previously opened request ID#%lu. This message and any further messages you send to $S will be appended to that request." },

/* User Management */
    { "HSMSG_CANNOT_ADD", "You do not have permission to add users." },
    { "HSMSG_CANNOT_DEL", "You do not have permission to delete users." },
    { "HSMSG_CANNOT_CLVL", "You do not have permission to modify users' access." },
    { "HSMSG_NO_SELF_CLVL", "You cannot change your own access." },
    { "HSMSG_NO_BUMP_ACCESS", "You cannot give users the same or more access than yourself." },
    { "HSMSG_NO_TRANSFER_SELF", "You cannot give ownership to your own account." },
    { "HSMSG_ADDED_USER", "Added new $b%s$b %s to the user list." },
    { "HSMSG_DELETED_USER", "Deleted $b%s$b %s from the user list." },
    { "HSMSG_USER_EXISTS", "$b%s$b is already on the user list." },
    { "HSMSG_INVALID_ACCESS", "$b%s$b is an invalid access level." },
    { "HSMSG_CHANGED_ACCESS", "%s now has $b%s$b access." },
    { "HSMSG_EXPIRATION_DONE", "%d eligible HelpServ bots have retired." },
    { "HSMSG_BAD_WEEKDAY", "I do not know which day of the week $b%s$b is." },
    { "HSMSG_WEEK_STARTS", "$b%s$b's weeks start on $b%s$b." },

/* Registration */
    { "HSMSG_ILLEGAL_NICK", "$b%s$b is an illegal nick; cannot use it." },
    { "HSMSG_NICK_EXISTS", "The nick %s is in use by someone else." },
    { "HSMSG_REG_SUCCESS", "%s now has ownership of bot %s." },
    { "HSMSG_NEED_UNREG_CONFIRM", "To unregister this bot, you must /msg $S unregister CONFIRM" },
    { "HSMSG_ERROR_ADDING_SERVICE", "Error creating new user $b%s$b." },

/* Rename */
    { "HSMSG_RENAMED", "%s has been renamed to $b%s$b." },
    { "HSMSG_MOVE_SAME_CHANNEL", "You cannot move %s to the same channel it is on." },
    { "HSMSG_INVALID_MOVE", "$b%s$b is not a valid nick or channel name." },
    { "HSMSG_NEED_GIVEOWNERSHIP_CONFIRM", "To transfer ownership of this bot, you must /msg $S giveownership newowner CONFIRM" },
    { "HSMSG_MULTIPLE_OWNERS", "There is more than one owner of %s; please use other commands to change ownership." },
    { "HSMSG_NO_TRANSFER_SELF", "You cannot give ownership to your own account." },
    { "HSMSG_OWNERSHIP_GIVEN", "Ownership of $b%s$b has been transferred to account $b%s$b." },

/* Queue settings */
    { "HSMSG_INVALID_OPTION", "$b%s$b is not a valid option." },
    { "HSMSG_QUEUE_OPTIONS", "HelpServ Queue Options:" },
    { "HSMSG_SET_COMMAND_TYPE",   "$bPageType        $b %s" },
    { "HSMSG_SET_ALERT_TYPE",     "$bAlertPageType   $b %s" },
    { "HSMSG_SET_STATUS_TYPE",    "$bStatusPageType  $b %s" },
    { "HSMSG_SET_COMMAND_TARGET", "$bPageTarget      $b %s" },
    { "HSMSG_SET_ALERT_TARGET",   "$bAlertPageTarget $b %s" },
    { "HSMSG_SET_STATUS_TARGET",  "$bStatusPageTarget$b %s" },
    { "HSMSG_SET_GREETING",       "$bGreeting        $b %s" },
    { "HSMSG_SET_REQOPENED",      "$bReqOpened       $b %s" },
    { "HSMSG_SET_REQASSIGNED",    "$bReqAssigned     $b %s" },
    { "HSMSG_SET_REQCLOSED",      "$bReqClosed       $b %s" },
    { "HSMSG_SET_IDLEDELAY",      "$bIdleDelay       $b %s" },
    { "HSMSG_SET_WHINEDELAY",     "$bWhineDelay      $b %s" },
    { "HSMSG_SET_WHINEINTERVAL",  "$bWhineInterval   $b %s" },
    { "HSMSG_SET_EMPTYINTERVAL",  "$bEmptyInterval   $b %s" },
    { "HSMSG_SET_STALEDELAY",     "$bStaleDelay      $b %s" },
    { "HSMSG_SET_REQPERSIST",     "$bReqPersist      $b %s" },
    { "HSMSG_SET_HELPERPERSIST",  "$bHelperPersist   $b %s" },
    { "HSMSG_SET_NOTIFICATION",   "$bNotification    $b %s" },
    { "HSMSG_SET_IDWRAP",         "$bIDWrap          $b %d" },
    { "HSMSG_SET_REQMAXLEN",      "$bReqMaxLen       $b %d" },
    { "HSMSG_SET_PRIVMSGONLY",    "$bPrivmsgOnly     $b %s" },
    { "HSMSG_SET_REQONJOIN",      "$bReqOnJoin       $b %s" },
    { "HSMSG_SET_AUTOVOICE",      "$bAutoVoice       $b %s" },
    { "HSMSG_SET_AUTODEVOICE",    "$bAutoDevoice     $b %s" },
    { "HSMSG_PAGE_NOTICE", "notice" },
    { "HSMSG_PAGE_PRIVMSG", "privmsg" },
    { "HSMSG_PAGE_ONOTICE", "onotice" },
    { "HSMSG_LENGTH_PART", "part" },
    { "HSMSG_LENGTH_QUIT", "quit" },
    { "HSMSG_LENGTH_CLOSE", "close" },
    { "HSMSG_NOTIFY_DROP", "ReqDrop" },
    { "HSMSG_NOTIFY_USERCHANGES", "UserChanges" },
    { "HSMSG_NOTIFY_ACCOUNTCHANGES", "AccountChanges" },
    { "HSMSG_INVALID_INTERVAL", "Sorry, %s must be at least %s." },
    { "HSMSG_0_DISABLED", "0 (Disabled)" },
    { "HSMSG_NEED_MANAGER", "Only managers or higher can do this." },
    { "HSMSG_SET_NEED_OPER", "This option can only be set by an oper." },

/* Requests */
    { "HSMSG_REQ_INVALID", "$b%s$b is not a valid request ID, or there are no requests for that nick or account." },
    { "HSMSG_REQ_NOT_YOURS_ASSIGNED_TO", "Request $b%lu$b is not assigned to you; it is currently assigned to %s." },
    { "HSMSG_REQ_NOT_YOURS_UNASSIGNED", "Request $b%lu$b is not assigned to you; it is currently unassigned." },
    { "HSMSG_REQ_FOUNDMANY", "The user you entered had multiple requests. The oldest one is being used." },
    { "HSMSG_REQ_CLOSED", "Request $b%lu$b has been closed." },
    { "HSMSG_REQ_NO_UNASSIGNED", "There are no unassigned requests." },
    { "HSMSG_USERCMD_NO_REQUEST", "You must have an open request to use a user command." },
    { "HSMSG_USERCMD_UNKNOWN", "I do not know the user command $b%s$b." },
    { "HSMSG_REQ_YOU_NOT_IN_HELPCHAN_OPEN", "You cannot open this request as you are not in %s." },
    { "HSMSG_REQ_YOU_NOT_IN_HELPCHAN", "You cannot be assigned this request as you are not in %s." },
    { "HSMSG_REQ_HIM_NOT_IN_HELPCHAN", "%s cannot be assigned this request as they are not in %s." },
    { "HSMSG_REQ_SELF_NOT_IN_OVERRIDE", "You are being assigned this request even though you are not in %s, because the restriction was overridden (you are a manager or owner). If you join and then part, this request will be marked unassigned." },
    { "HSMSG_REQ_YOU_NOT_IN_OVERRIDE", "Note: You are being assigned this request even though you are not in %s, because the restriction was overridden by a manager or owner. If you join and then part, this request will be marked unassigned." },
    { "HSMSG_REQ_HIM_NOT_IN_OVERRIDE", "Note: %s is being assigned this request even though they are not in %s, because the restriction was overridden (you are a manager or owner). If they join and then part, this request will be marked unassigned." },
    { "HSMSG_REQ_ASSIGNED_YOU", "You have been assigned request ID#%lu:" },
    { "HSMSG_REQ_REASSIGNED", "You have been assigned request ID#%lu (reassigned from %s):" },
    { "HSMSG_REQ_INFO_1", "Request ID#%lu:" },
    { "HSMSG_REQ_INFO_2a", " - Nick %s / Account %s" },
    { "HSMSG_REQ_INFO_2b", " - Nick %s / Not authed" },
    { "HSMSG_REQ_INFO_2c", " - Online somewhere / Account %s" },
    { "HSMSG_REQ_INFO_2d", " - Not online / Account %s" },
    { "HSMSG_REQ_INFO_2e", " - Not online / No account" },
    { "HSMSG_REQ_INFO_3", " - Opened at %s (%s ago)" },
    { "HSMSG_REQ_INFO_4", " - Message:" },
    { "HSMSG_REQ_INFO_MESSAGE", "   %s" },
    { "HSMSG_REQ_ASSIGNED", "Your helper for request ID#%lu is %s (Current nick: %s)" },
    { "HSMSG_REQ_ASSIGNED_AGAIN", "Your helper for request ID#%lu has been changed to %s (Current nick: %s)" },
    { "HSMSG_REQ_UNASSIGNED", "Your helper for request ID#%lu has %s, so your request is no longer being handled by them. It has been placed near the front of the unhandled request queue, based on how long ago your request was opened." },
    { "HSMSG_REQ_NEW", "Your message has been recorded and assigned request ID#%lu. A helper should contact you shortly." },
    { "HSMSG_REQ_NEWONJOIN", "Welcome to %s. You have been assigned request ID#%lu. A helper should contact you shortly." },
    { "HSMSG_REQ_UNHANDLED_TIME", "The oldest unhandled request has been waiting for %s." },
    { "HSMSG_REQ_NO_UNHANDLED", "There are no other unhandled requests." },
    { "HSMSG_REQ_PERSIST_QUIT", "Everything you tell me until you are helped (or you quit) will be recorded. If you disconnect, your request will be lost." },
    { "HSMSG_REQ_PERSIST_PART", "Everything you tell me until you are helped (or you leave %s) will be recorded. If you part %s, your request will be lost." },
    { "HSMSG_REQ_PERSIST_HANDLE", "Everything you tell me until you are helped will be recorded." },
    { "HSMSG_REQ_MAXLEN", "Sorry, but your request has reached the maximum number of lines. Please wait to be assigned to a helper and continue explaining your request to them." },
    { "HSMSG_REQ_FOUND_ANOTHER", "Request ID#%lu has been closed. $S detected that you also have request ID#%lu open. If you send $S a message, it will be associated with that request." },

/* Messages that are inserted into request text */
    { "HSMSG_REQMSG_NOTE_ADDED", "Your note for request ID#%lu has been recorded." },

/* Automatically generated page messages */
    { "HSMSG_PAGE_NEW_REQUEST_AUTHED", "New request (ID#%lu) from $b%s$b (Account %s)" },
    { "HSMSG_PAGE_NEW_REQUEST_UNAUTHED", "New request (ID#%lu) from $b%s$b (Not logged in)" },
    { "HSMSG_PAGE_UPD_REQUEST_AUTHED", "Request ID#%lu has been updated by $b%s$b (Account %s). Request was initially opened at %s, and was last updated %s ago." },
    { "HSMSG_PAGE_UPD_REQUEST_NOT_AUTHED", "Request ID#%lu has been updated by $b%s$b (not authed). Request was initially opened at %s, and was last updated %s ago." },
    { "HSMSG_PAGE_CLOSE_REQUEST_1", "Request ID#%lu from $b%s$b (Account %s) has been closed by %s." },
    { "HSMSG_PAGE_CLOSE_REQUEST_2", "Request ID#%lu from $b%s$b (Not authed) has been closed by %s." },
    { "HSMSG_PAGE_CLOSE_REQUEST_3", "Request ID#%lu from an offline user (Account %s) has been closed by %s." },
    { "HSMSG_PAGE_CLOSE_REQUEST_4", "Request ID#%lu from an offline user (no account) has been closed by %s." },
    { "HSMSG_PAGE_ASSIGN_REQUEST_1", "Request ID#%lu from $b%s$b (Account %s) has been assigned to %s." },
    { "HSMSG_PAGE_ASSIGN_REQUEST_2", "Request ID#%lu from $b%s$b (Not authed) has been assigned to %s." },
    { "HSMSG_PAGE_ASSIGN_REQUEST_3", "Request ID#%lu from an offline user (Account %s) has been assigned to %s." },
    { "HSMSG_PAGE_ASSIGN_REQUEST_4", "Request ID#%lu from an offline user (no account) has been assigned to %s." },
    /* The last %s is still an I18N lose.  Blame whoever decided to overload it so much. */
    { "HSMSG_PAGE_HELPER_GONE_1", "Request ID#%lu from $b%s$b (Account %s) $bhas been unassigned$b, as its helper, %s has %s." },
    { "HSMSG_PAGE_HELPER_GONE_2", "Request ID#%lu from $b%s$b (Not authed) $bhas been unassigned$b, as its helper, %s has %s." },
    { "HSMSG_PAGE_HELPER_GONE_3", "Request ID#%lu from an offline user (Account %s) $bhas been unassigned$b, as its helper, %s has %s." },
    { "HSMSG_PAGE_HELPER_GONE_4", "Request ID#%lu from an offline user (No account) $bhas been unassigned$b, as its helper, %s has %s." },
    { "HSMSG_PAGE_WHINE_HEADER", "$b%u unhandled request(s)$b waiting at least $b%s$b (%u total)" },
    { "HSMSG_PAGE_IDLE_HEADER", "$b%u users$b in %s $bidle at least %s$b:" },
    { "HSMSG_PAGE_EMPTYALERT", "$b%s has no helpers present (%u unhandled request(s))$b" },
    { "HSMSG_PAGE_ONLYTRIALALERT", "$b%s has no full helpers present (%d trial(s) present; %u unhandled request(s))$b" },
    { "HSMSG_PAGE_FIRSTEMPTYALERT", "$b%s has no helpers present because %s has left (%u unhandled request(s))$b" },
    { "HSMSG_PAGE_FIRSTONLYTRIALALERT", "$b%s has no full helpers present because %s has left (%d trial(s) present; %u unhandled request(s))$b" },
    { "HSMSG_PAGE_EMPTYNOMORE", "%s has joined %s; cancelling the \"no helpers present\" alert" },

/* Notification messages */
    { "HSMSG_NOTIFY_USER_QUIT", "The user for request ID#%lu, $b%s$b, has disconnected." },
    { "HSMSG_NOTIFY_USER_MOVE", "The account for request ID#%lu, $b%s$b has been unregistered. It has been associated with user $b%s$b." },
    { "HSMSG_NOTIFY_USER_NICK", "The user for request ID#%lu has changed their nick from $b%s$b to $b%s$b." },
    { "HSMSG_NOTIFY_USER_FOUND", "The user for request ID#%lu is now online using nick $b%s$b." },
    { "HSMSG_NOTIFY_HAND_RENAME", "The account for request ID#%lu has been renamed from $b%s$b to $b%s$b." },
    { "HSMSG_NOTIFY_HAND_MOVE", "The account for request ID#%lu has been changed to $b%s$b from $b%s$b." },
    { "HSMSG_NOTIFY_HAND_STUCK", "The user for request ID#%lu, $b%s$b, has re-authenticated to account $b%s$b from $b%s$b, and the request remained associated with the old handle." },
    { "HSMSG_NOTIFY_HAND_AUTH", "The user for request ID#%lu, $b%s$b, has authenticated to account $b%s$b." },
    { "HSMSG_NOTIFY_HAND_UNREG", "The account for request ID#%lu, $b%s$b, has been unregistered by $b%s$b." },
    { "HSMSG_NOTIFY_HAND_MERGE", "The account for request ID#%lu, $b%s$b, has been merged with $b%s$b by %s." },
    { "HSMSG_NOTIFY_ALLOWAUTH", "The user for request ID#%lu, $b%s$b, has been permitted by %s to authenticate to account $b%s$b without hostmask checking." },
    { "HSMSG_NOTIFY_UNALLOWAUTH", "The user for request ID#%lu, $b%s$b, has had their ability to authenticate without hostmask checking revoked by %s." },
    { "HSMSG_NOTIFY_FAILPW", "The user for request ID#%lu, $b%s$b, has attempted to authenticate to account $b%s$b, but used an incorrect password." },
    { "HSMSG_NOTIFY_REQ_DROP_PART", "Request ID#%lu has been $bdropped$b because %s left the help channel." },
    { "HSMSG_NOTIFY_REQ_DROP_QUIT", "Request ID#%lu has been $bdropped$b because %s quit IRC." },
    { "HSMSG_NOTIFY_REQ_DROP_UNREGGED", "Request ID#%lu (account %s) has been $bdropped$b because the account was unregistered." },

/* Presence and request-related messages */
    { "HSMSG_REQ_DROPPED_PART", "You have left $b%s$b. Your help request (ID#%lu) has been deleted." },
    { "HSMSG_REQ_WARN_UNREG", "The account you were authenticated to ($b%s$b) has been unregistered. Therefore unless you register a new handle, or authenticate to another one, if you disconnect, your HelpServ $S (%s) request ID#%lu will be lost." },
    { "HSMSG_REQ_DROPPED_UNREG", "By unregistering the account $b%s$b, HelpServ $S (%s) request ID#%lu was dropped, as there was nobody online to associate the request with." },
    { "HSMSG_REQ_ASSIGNED_UNREG", "As the account $b%s$b was unregistered, HelpServ $S (%s) request ID#%lu was associated with you, as you were authenticated to that account. If you disconnect, then this request will be lost forever." },
    { "HSMSG_REQ_AUTH_STUCK", "By authenticating to account $b%s$b, HelpServ $S (%s) request ID#%lu remained with the previous account $b%s$b (because the request will remain until closed). This means that if you send $S a message, it $uwill not$u be associated with this request." },
    { "HSMSG_REQ_AUTH_MOVED", "By authenticating to account $b%s$b, HelpServ $S (%s) request ID#%lu ceased to be associated with your previously authenticated account ($b%s$b), and was transferred to your new account (because the request will only remain until you %s). This means that if you send $S a message, it $uwill$u be associated with this request." },

/* Lists */
    { "HSMSG_BOTLIST_HEADER", "$bCurrent HelpServ bots:$b" },
    { "HSMSG_USERLIST_HEADER", "$b%s users:$b" },
    { "HSMSG_USERLIST_ZOOT_LVL", "%s $b%ss$b:" },
    { "HSMSG_REQLIST_AUTH", "You are currently assigned these requests:" },
    { "HSMSG_REQ_LIST_TOP_UNASSIGNED", "Listing $ball unassigned$b requests (%d in list)." },
    { "HSMSG_REQ_LIST_TOP_ASSIGNED", "Listing $ball assigned$b requests (%d in list)." },
    { "HSMSG_REQ_LIST_TOP_YOUR", "Listing $byour$b requests (%d in list)." },
    { "HSMSG_REQ_LIST_TOP_ALL", "Listing $ball$b requests (%d in list)." },
    { "HSMSG_REQ_LIST_NONE", "There are no matching requests." },
    { "HSMSG_STATS_TOP", "Stats for %s user $b%s$b (week starts %s):" },
    { "HSMSG_STATS_TIME", "$uTime spent helping in %s:$u" },
    { "HSMSG_STATS_REQS", "$uRequest activity statistics:$u" },

/* Status report headers */
    { "HSMSG_STATS_REPORT_0", "Stats report for current week" },
    { "HSMSG_STATS_REPORT_1", "Stats report for one week ago" },
    { "HSMSG_STATS_REPORT_2", "Stats report for two weeks ago" },
    { "HSMSG_STATS_REPORT_3", "Stats report for three weeks ago" },

/* Responses to user commands */
    { "HSMSG_YOU_BEING_HELPED", "You are already being helped." },
    { "HSMSG_YOU_BEING_HELPED_BY", "You are already being helped by $b%s$b." },
    { "HSMSG_WAIT_STATUS", "You are %d of %d in line; the first person has waited %s." },
    { NULL, NULL }
};

enum helpserv_level {
    HlNone,
    HlTrial,
    HlHelper,
    HlManager,
    HlOwner,
    HlOper,
    HlCount
};

static const char *helpserv_level_names[] = {
    "None",
    "Trial",
    "Helper",
    "Manager",
    "Owner",
    "Oper",
    NULL
};

enum page_type {
    PAGE_NONE,
    PAGE_NOTICE,
    PAGE_PRIVMSG,
    PAGE_ONOTICE,
    PAGE_COUNT
};

static const struct {
    char *db_name;
    char *print_name;
    irc_send_func func;
} page_types[] = {
    { "none", "MSG_NONE", NULL },
    { "notice", "HSMSG_PAGE_NOTICE", irc_notice },
    { "privmsg", "HSMSG_PAGE_PRIVMSG", irc_privmsg },
    { "onotice", "HSMSG_PAGE_ONOTICE", irc_wallchops },
    { NULL, NULL, NULL }
};

enum page_source {
    PGSRC_COMMAND,
    PGSRC_ALERT,
    PGSRC_STATUS,
    PGSRC_COUNT
};

static const struct {
    char *db_name;
    char *print_target;
    char *print_type;
} page_sources[] = {
    { "command", "HSMSG_SET_COMMAND_TARGET", "HSMSG_SET_COMMAND_TYPE" },
    { "alert", "HSMSG_SET_ALERT_TARGET", "HSMSG_SET_ALERT_TYPE" },
    { "status", "HSMSG_SET_STATUS_TARGET", "HSMSG_SET_STATUS_TYPE" },
    { NULL, NULL, NULL }
};

enum message_type {
    MSGTYPE_GREETING,
    MSGTYPE_REQ_OPENED,
    MSGTYPE_REQ_ASSIGNED,
    MSGTYPE_REQ_CLOSED,
    MSGTYPE_REQ_DROPPED,
    MSGTYPE_COUNT
};

static const struct {
    char *db_name;
    char *print_name;
} message_types[] = {
    { "greeting", "HSMSG_SET_GREETING" },
    { "reqopened", "HSMSG_SET_REQOPENED" },
    { "reqassigned", "HSMSG_SET_REQASSIGNED" },
    { "reqclosed", "HSMSG_SET_REQCLOSED" },
    { "reqdropped", "HSMSG_SET_REQDROPPED" },
    { NULL, NULL }
};

enum interval_type {
    INTERVAL_IDLE_DELAY,
    INTERVAL_WHINE_DELAY,
    INTERVAL_WHINE_INTERVAL,
    INTERVAL_EMPTY_INTERVAL,
    INTERVAL_STALE_DELAY,
    INTERVAL_COUNT
};

static const struct {
    char *db_name;
    char *print_name;
} interval_types[] = {
    { "idledelay", "HSMSG_SET_IDLEDELAY" },
    { "whinedelay", "HSMSG_SET_WHINEDELAY" },
    { "whineinterval", "HSMSG_SET_WHINEINTERVAL" },
    { "emptyinterval", "HSMSG_SET_EMPTYINTERVAL" },
    { "staledelay", "HSMSG_SET_STALEDELAY" },
    { NULL, NULL }
};

enum persistence_type {
    PERSIST_T_REQUEST,
    PERSIST_T_HELPER,
    PERSIST_T_COUNT
};

static const struct {
    char *db_name;
    char *print_name;
} persistence_types[] = {
    { "reqpersist", "HSMSG_SET_REQPERSIST" },
    { "helperpersist", "HSMSG_SET_HELPERPERSIST" },
    { NULL, NULL }
};

enum persistence_length {
    PERSIST_PART,
    PERSIST_QUIT,
    PERSIST_CLOSE,
    PERSIST_COUNT
};

static const struct {
    char *db_name;
    char *print_name;
} persistence_lengths[] = {
    { "part", "HSMSG_LENGTH_PART" },
    { "quit", "HSMSG_LENGTH_QUIT" },
    { "close", "HSMSG_LENGTH_CLOSE" },
    { NULL, NULL }
};

enum notification_type {
    NOTIFY_NONE,
    NOTIFY_DROP,
    NOTIFY_USER,
    NOTIFY_HANDLE,
    NOTIFY_COUNT
};

static const struct {
    char *db_name;
    char *print_name;
} notification_types[] = {
    { "none", "MSG_NONE" },
    { "reqdrop", "HSMSG_NOTIFY_DROP" },
    { "userchanges", "HSMSG_NOTIFY_USERCHANGES" },
    { "accountchanges", "HSMSG_NOTIFY_ACCOUNTCHANGES" },
    { NULL, NULL }
};

static const char *weekday_names[] = {
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday",
    NULL
};

static const char *statsreport_week[] = {
    "HSMSG_STATS_REPORT_0",
    "HSMSG_STATS_REPORT_1",
    "HSMSG_STATS_REPORT_2",
    "HSMSG_STATS_REPORT_3"
};

static struct {
    const char *description;
    const char *reqlogfile;
    unsigned long db_backup_frequency;
    unsigned int expire_age;
    char user_escape;
} helpserv_conf;

static time_t last_stats_update;
static int shutting_down;
static FILE *reqlog_f;
static struct log_type *HS_LOG;

#define CMD_NEED_BOT            0x001
#define CMD_NOT_OVERRIDE        0x002
#define CMD_FROM_OPSERV_ONLY    0x004
#define CMD_IGNORE_EVENT        0x008
#define CMD_NEVER_FROM_OPSERV   0x010

struct helpserv_bot {
    struct userNode *helpserv;

    struct chanNode *helpchan;

    struct chanNode *page_targets[PGSRC_COUNT];
    enum page_type page_types[PGSRC_COUNT];
    char *messages[MSGTYPE_COUNT];
    unsigned long intervals[INTERVAL_COUNT];
    enum notification_type notify;

    /* This is a default; it can be changed on a per-request basis */
    enum persistence_type persist_types[PERSIST_T_COUNT];

    dict_t users; /* indexed by handle */

    struct helpserv_request *unhandled; /* linked list of unhandled requests */
    dict_t requests; /* indexed by request id */
    unsigned long last_requestid;
    unsigned long id_wrap;
    unsigned long req_maxlen; /* Maxmimum request length in lines */

    unsigned int privmsg_only : 1;
    unsigned int req_on_join : 1;
    unsigned int auto_voice : 1;
    unsigned int auto_devoice : 1;

    unsigned int helpchan_empty : 1;

    time_t registered;
    time_t last_active;
    char *registrar;
};

struct helpserv_user {
    struct handle_info *handle;
    struct helpserv_bot *hs;
    unsigned int help_mode : 1;
    unsigned int week_start : 3;
    enum helpserv_level level;
    /* statistics */
    time_t join_time; /* when they joined, or 0 if not in channel */
    /* [0] through [3] are n weeks ago, [4] is the total of everything before that */
    unsigned int time_per_week[5]; /* how long they've were in the channel the past 4 weeks */
    unsigned int picked_up[5]; /* how many requests they have picked up */
    unsigned int closed[5]; /* how many requests they have closed */
    unsigned int reassigned_from[5]; /* how many requests reassigned from them to others */
    unsigned int reassigned_to[5]; /* how many requests reassigned from others to them */
};

struct helpserv_request {
    struct helpserv_user *helper;
    struct helpserv_bot *hs;
    struct string_list *text;
    struct helpserv_request *next_unhandled;

    struct helpserv_reqlist *parent_nick_list;
    struct helpserv_reqlist *parent_hand_list;

    /* One, but not both, of "user" and "handle" may be NULL,
     * depending on what we know about the user.
     *
     * If persist == PERSIST_CLOSE when the user quits, then it
     * switches to handle instead of user... and stays that way (it's
     * possible to have >1 nick per handle, so you can't really decide
     * to reassign a userNode to it unless they send another message
     * to HelpServ).
     */
    struct userNode *user;
    struct handle_info *handle;

    unsigned long id;
    time_t opened;
    time_t assigned;
    time_t updated;
};

#define DEFINE_LIST_ALLOC(STRUCTNAME) \
struct STRUCTNAME * STRUCTNAME##_alloc() {\
    struct STRUCTNAME *newlist; \
    newlist = malloc(sizeof(struct STRUCTNAME)); \
    STRUCTNAME##_init(newlist); \
    return newlist; \
}\
void STRUCTNAME##_free(void *data) {\
    struct STRUCTNAME *list = data; /* void * to let dict_set_free_data use it */ \
    STRUCTNAME##_clean(list); \
    free(list); \
}

DECLARE_LIST(helpserv_botlist, struct helpserv_bot *);
DEFINE_LIST(helpserv_botlist, struct helpserv_bot *);
DEFINE_LIST_ALLOC(helpserv_botlist);

DECLARE_LIST(helpserv_reqlist, struct helpserv_request *);
DEFINE_LIST(helpserv_reqlist, struct helpserv_request *);
DEFINE_LIST_ALLOC(helpserv_reqlist);

DECLARE_LIST(helpserv_userlist, struct helpserv_user *);
DEFINE_LIST(helpserv_userlist, struct helpserv_user *);
DEFINE_LIST_ALLOC(helpserv_userlist);

struct helpfile *helpserv_helpfile;
static struct module *helpserv_module;
static dict_t helpserv_func_dict;
static dict_t helpserv_usercmd_dict; /* contains helpserv_usercmd_t */
static dict_t helpserv_option_dict;
static dict_t helpserv_bots_dict; /* indexed by nick */
static dict_t helpserv_bots_bychan_dict; /* indexed by chan, holds a struct helpserv_botlist */
/* QUESTION: why are these outside of any helpserv_bot struct? */
static dict_t helpserv_reqs_bynick_dict; /* indexed by nick, holds a struct helpserv_reqlist */
static dict_t helpserv_reqs_byhand_dict; /* indexed by handle, holds a struct helpserv_reqlist */
static dict_t helpserv_users_byhand_dict; /* indexed by handle, holds a struct helpserv_userlist */

/* This is so that overrides can "speak" from opserv */
extern struct userNode *opserv;

#define HELPSERV_SYNTAX() helpserv_help(hs, from_opserv, user, argv[0])
#define HELPSERV_FUNC(NAME) int NAME(struct userNode *user, UNUSED_ARG(struct helpserv_bot *hs), int from_opserv, UNUSED_ARG(unsigned int argc), UNUSED_ARG(char *argv[]))
typedef HELPSERV_FUNC(helpserv_func_t);
#define HELPSERV_USERCMD(NAME) void NAME(struct helpserv_request *req, UNUSED_ARG(struct userNode *likely_helper), UNUSED_ARG(char *args))
typedef HELPSERV_USERCMD(helpserv_usercmd_t);
#define HELPSERV_OPTION(NAME) HELPSERV_FUNC(NAME)
typedef HELPSERV_OPTION(helpserv_option_func_t);

static HELPSERV_FUNC(cmd_help);

#define REQUIRE_PARMS(N) if (argc < N) { \
        helpserv_notice(user, "MSG_MISSING_PARAMS", argv[0]); \
        HELPSERV_SYNTAX(); \
        return 0; }

/* For messages going to users being helped */
#define helpserv_msguser(target, format...) send_message_type((from_opserv ? 0 : hs->privmsg_only), (target), (from_opserv ? opserv : hs->helpserv) , ## format)
#define helpserv_user_reply(format...) send_message_type(req->hs->privmsg_only, req->user, req->hs->helpserv , ## format)
/* For messages going to helpers */
#define helpserv_notice(target, format...) send_message((target), (from_opserv ? opserv : hs->helpserv) , ## format)
#define helpserv_notify(helper, format...) do { struct userNode *_target; for (_target = (helper)->handle->users; _target; _target = _target->next_authed) { \
        send_message(_target, (helper)->hs->helpserv, ## format); \
    } } while (0)
#define helpserv_message(hs, target, id) do { if ((hs)->messages[id]) { \
    if (from_opserv) \
        send_message_type(4, (target), opserv, "%s", (hs)->messages[id]); \
    else \
        send_message_type(4 | hs->privmsg_only, (target), hs->helpserv, "%s", (hs)->messages[id]); \
    } } while (0)
#define helpserv_page(TYPE, FORMAT...) do { \
    struct chanNode *target=NULL; int msg_type=0; \
    target = hs->page_targets[TYPE]; \
    switch (hs->page_types[TYPE]) { \
        case PAGE_NOTICE: msg_type = 0; break; \
        case PAGE_PRIVMSG: msg_type = 1; break; \
        case PAGE_ONOTICE: msg_type = 2; break; \
        default: log_module(HS_LOG, LOG_ERROR, "helpserv_page() called but %s has an invalid page type %d.", hs->helpserv->nick, TYPE); \
        case PAGE_NONE: target = NULL; break; \
    } \
    if (target) send_target_message(msg_type, target->name, hs->helpserv, ## FORMAT); \
    } while (0)
#define helpserv_get_handle_info(user, text) smart_get_handle_info((from_opserv ? opserv : hs->helpserv) , (user), (text))

struct helpserv_cmd {
    enum helpserv_level access;
    helpserv_func_t *func;
    double weight;
    long flags;
};

static void run_empty_interval(void *data);

static void helpserv_interval(char *output, time_t interval) {
    int num_hours = interval / 3600;
    int num_minutes = (interval % 3600) / 60;
    sprintf(output, "%u hour%s, %u minute%s", num_hours, num_hours == 1 ? "" : "s", num_minutes, num_minutes == 1 ? "" : "s");
}

static const char * helpserv_level2str(enum helpserv_level level) {
    if (level <= HlOper) {
        return helpserv_level_names[level];
    } else {
        log_module(HS_LOG, LOG_ERROR, "helpserv_level2str receieved invalid level %d.", level);
        return "Error";
    }
}

static enum helpserv_level helpserv_str2level(const char *msg) {
    enum helpserv_level nn;
    for (nn=HlNone; nn<=HlOper; nn++) {
        if (!irccasecmp(msg, helpserv_level_names[nn]))
            return nn;
    }
    log_module(HS_LOG, LOG_ERROR, "helpserv_str2level received invalid level %s.", msg);
    return HlNone; /* Error */
}

static struct helpserv_user *GetHSUser(struct helpserv_bot *hs, struct handle_info *hi) {
    return dict_find(hs->users, hi->handle, NULL);
}

static void helpserv_log_request(struct helpserv_request *req, const char *reason) {
    char key[27+NICKLEN];
    char userhost[USERLEN+HOSTLEN+2];
    struct saxdb_context *ctx;
    int res;

    assert(req != NULL);
    assert(reason != NULL);
    if (!(ctx = saxdb_open_context(reqlog_f)))
        return;
    sprintf(key, "%s-" FMT_TIME_T "-%lu", req->hs->helpserv->nick, req->opened, req->id);
    if ((res = setjmp(ctx->jbuf)) != 0) {
        log_module(HS_LOG, LOG_ERROR, "Unable to log helpserv request: %s.", strerror(res));
    } else {
        saxdb_start_record(ctx, key, 1);
        if (req->helper) {
            saxdb_write_string(ctx, KEY_REQUEST_HELPER, req->helper->handle->handle);
            saxdb_write_int(ctx, KEY_REQUEST_ASSIGNED, req->assigned);
        }
        if (req->handle) {
            saxdb_write_string(ctx, KEY_REQUEST_HANDLE, req->handle->handle);
        }
        if (req->user) {
            saxdb_write_string(ctx, KEY_REQUEST_NICK, req->user->nick);
            sprintf(userhost, "%s@%s", req->user->ident, req->user->hostname);
            saxdb_write_string(ctx, KEY_REQUEST_USERHOST, userhost);
        }
        saxdb_write_int(ctx, KEY_REQUEST_OPENED, req->opened);
        saxdb_write_int(ctx, KEY_REQUEST_CLOSED, now);
        saxdb_write_string(ctx, KEY_REQUEST_CLOSEREASON, reason);
        saxdb_write_string_list(ctx, KEY_REQUEST_TEXT, req->text);
        saxdb_end_record(ctx);
        saxdb_close_context(ctx);
        fflush(reqlog_f);
    }
}

/* Searches for a request by number, nick, or account (num|nick|*account).
 * As there can potentially be >1 match, it takes a reqlist. The return
 * value is the "best" request found (explained in the comment block below).
 *
 * If num_results is not NULL, it is set to the number of potentially matching
 * requests.
 * If hs_user is not NULL, requests assigned to this helper will be given
 * preference (oldest assigned, falling back to oldest if there are none).
 */
static struct helpserv_request * smart_get_request(struct helpserv_bot *hs, struct helpserv_user *hs_user, const char *needle, int *num_results) {
    struct helpserv_reqlist *reqlist, resultlist;
    struct helpserv_request *req, *oldest=NULL, *oldest_assigned=NULL;
    struct userNode *user;
    unsigned int i;

    if (num_results)
        *num_results = 0;

    if (*needle == '*') {
        /* This test (handle) requires the least processing, so it's first */
        if (!(reqlist = dict_find(helpserv_reqs_byhand_dict, needle+1, NULL)))
            return NULL;
        helpserv_reqlist_init(&resultlist);
        for (i=0; i < reqlist->used; i++) {
            req = reqlist->list[i];
            if (req->hs == hs) {
                helpserv_reqlist_append(&resultlist, req);
                if (num_results)
                    (*num_results)++;
            }
        }
    } else if (!needle[strspn(needle, "0123456789")]) {
        /* The string is 100% numeric - a request id */
        if (!(req = dict_find(hs->requests, needle, NULL)))
            return NULL;
        if (num_results)
            *num_results = 1;
        return req;
    } else if ((user = GetUserH(needle))) {
        /* And finally, search by nick */
        if (!(reqlist = dict_find(helpserv_reqs_bynick_dict, needle, NULL)))
            return NULL;
        helpserv_reqlist_init(&resultlist);

        for (i=0; i < reqlist->used; i++) {
            req = reqlist->list[i];
            if (req->hs == hs) {
                helpserv_reqlist_append(&resultlist, req);
                if (num_results)
                    (*num_results)++;
            }
        }
        /* If the nick didn't have anything, try their handle */
        if (!resultlist.used && user->handle_info) {
            char star_handle[NICKSERV_HANDLE_LEN+2];

            helpserv_reqlist_clean(&resultlist);
            sprintf(star_handle, "*%s", user->handle_info->handle);

            return smart_get_request(hs, hs_user, star_handle, num_results);
        }
    } else {
        return NULL;
    }

    if (resultlist.used == 0) {
        helpserv_reqlist_clean(&resultlist);
        return NULL;
    } else if (resultlist.used == 1) {
        req = resultlist.list[0];
        helpserv_reqlist_clean(&resultlist);
        return req;
    }

    /* In case there is >1 request returned, use the oldest one assigned to
     * the helper executing the command. Otherwise, use the oldest request.
     * This may not be the intended result for cmd_pickup (first unhandled
     * request may be better), or cmd_reassign (first handled request), but
     * it's close enough, and there really aren't supposed to be multiple
     * requests per person anyway; they're just side effects of authing. */

    for (i=0; i < resultlist.used; i++) {
        req = resultlist.list[i];
        if (!oldest || req->opened < oldest->opened)
            oldest = req;
        if (hs_user && (!oldest_assigned || (req->helper == hs_user && req->opened < oldest_assigned->opened)))
            oldest_assigned = req;
    }

    helpserv_reqlist_clean(&resultlist);

    return oldest_assigned ? oldest_assigned : oldest;
}

static struct helpserv_request * create_request(struct userNode *user, struct helpserv_bot *hs, int from_join) {
    struct helpserv_request *req = calloc(1, sizeof(struct helpserv_request));
    char lbuf[3][MAX_LINE_SIZE], unh[INTERVALLEN];
    struct helpserv_reqlist *reqlist, *hand_reqlist;
    const unsigned int from_opserv = 0;
    const char *fmt;

    assert(req);

    req->id = ++hs->last_requestid;
    sprintf(unh, "%lu", req->id);
    dict_insert(hs->requests, strdup(unh), req);

    if (hs->id_wrap) {
        unsigned long i;
        char buf[12];
        if (hs->last_requestid < hs->id_wrap) {
            for (i=hs->last_requestid; i < hs->id_wrap; i++) {
                sprintf(buf, "%lu", i);
                if (!dict_find(hs->requests, buf, NULL)) {
                    hs->last_requestid = i-1;
                    break;
                }
            }
        }
        if (hs->last_requestid >= hs->id_wrap) {
            for (i=1; i < hs->id_wrap; i++) {
                sprintf(buf, "%lu", i);
                if (!dict_find(hs->requests, buf, NULL)) {
                    hs->last_requestid = i-1;
                    break;
                }
            }
            if (i >= hs->id_wrap) {
                log_module(HS_LOG, LOG_INFO, "%s has more requests than its id_wrap.", hs->helpserv->nick);
            }
        }
    }

    req->hs = hs;
    req->helper = NULL;
    req->text = alloc_string_list(4);
    req->user = user;
    req->handle = user->handle_info;
    if (from_join && self->burst) {
        extern time_t burst_begin;
        /* We need to keep all the requests during a burst join together,
         * even if the burst takes more than 1 second. ircu seems to burst
         * in reverse-join order. */
        req->opened = burst_begin;
    } else {
        req->opened = now;
    }
    req->updated = now;

    if (!hs->unhandled) {
        hs->unhandled = req;
        req->next_unhandled = NULL;
    } else if (self->burst && hs->unhandled->opened >= req->opened) {
        req->next_unhandled = hs->unhandled;
        hs->unhandled = req;
    } else if (self->burst) {
        struct helpserv_request *unh;
        /* Add the request to the beginning of the set of requests with
         * req->opened having the same value. This makes reqonjoin create
         * requests in the correct order while bursting. Note that this
         * does not correct request ids, so they will be in reverse order
         * though "/msg botname next" will work properly. */
        for (unh = hs->unhandled; unh->next_unhandled && unh->next_unhandled->opened < req->opened; unh = unh->next_unhandled) ;
        req->next_unhandled = unh->next_unhandled;
        unh->next_unhandled = req;
    } else {
        struct helpserv_request *unh;
        /* Add to the end */
        for (unh = hs->unhandled; unh->next_unhandled; unh = unh->next_unhandled) ;
        req->next_unhandled = NULL;
        unh->next_unhandled = req;
    }

    if (!(reqlist = dict_find(helpserv_reqs_bynick_dict, user->nick, NULL))) {
        reqlist = helpserv_reqlist_alloc();
        dict_insert(helpserv_reqs_bynick_dict, user->nick, reqlist);
    }
    req->parent_nick_list = reqlist;
    helpserv_reqlist_append(reqlist, req);

    if (user->handle_info) {
        if (!(hand_reqlist = dict_find(helpserv_reqs_byhand_dict, user->handle_info->handle, NULL))) {
            hand_reqlist = helpserv_reqlist_alloc();
            dict_insert(helpserv_reqs_byhand_dict, user->handle_info->handle, hand_reqlist);
        }
        req->parent_hand_list = hand_reqlist;
        helpserv_reqlist_append(hand_reqlist, req);
    } else {
        req->parent_hand_list = NULL;
    }

    if (from_join) {
        fmt = user_find_message(user, "HSMSG_REQ_NEWONJOIN");
        sprintf(lbuf[0], fmt, hs->helpchan->name, req->id);
    } else {
        fmt = user_find_message(user, "HSMSG_REQ_NEW");
        sprintf(lbuf[0], fmt, req->id);
    }
    if (req != hs->unhandled) {
        intervalString(unh, now - hs->unhandled->opened, user->handle_info);
        fmt = user_find_message(user, "HSMSG_REQ_UNHANDLED_TIME");
        sprintf(lbuf[1], fmt, unh);
    } else {
        fmt = user_find_message(user, "HSMSG_REQ_NO_UNHANDLED");
        sprintf(lbuf[1], fmt);
    }
    switch (hs->persist_types[PERSIST_T_REQUEST]) {
        case PERSIST_PART:
            fmt = user_find_message(user, "HSMSG_REQ_PERSIST_PART");
            sprintf(lbuf[2], fmt, hs->helpchan->name, hs->helpchan->name);
            break;
        case PERSIST_QUIT:
            fmt = user_find_message(user, "HSMSG_REQ_PERSIST_QUIT");
            sprintf(lbuf[2], fmt);
            break;
        default:
            log_module(HS_LOG, LOG_ERROR, "%s has an invalid req_persist.", hs->helpserv->nick);
        case PERSIST_CLOSE:
            if (user->handle_info) {
                fmt = user_find_message(user, "HSMSG_REQ_PERSIST_HANDLE");
                sprintf(lbuf[2], fmt);
            } else {
                fmt = user_find_message(user, "HSMSG_REQ_PERSIST_QUIT");
                sprintf(lbuf[2], fmt);
            }
            break;
    }
    helpserv_message(hs, user, MSGTYPE_REQ_OPENED);
    if (from_opserv)
        send_message_type(4, user, opserv, "%s %s %s", lbuf[0], lbuf[1], lbuf[2]);
    else
        send_message_type(4, user, hs->helpserv, "%s %s %s", lbuf[0], lbuf[1], lbuf[2]);

    if (hs->req_on_join && req == hs->unhandled && hs->helpchan_empty) {
        timeq_del(0, run_empty_interval, hs, TIMEQ_IGNORE_WHEN);
        run_empty_interval(hs);
    }

    return req;
}

/* Handle a message from a user to a HelpServ bot. */
static void helpserv_usermsg(struct userNode *user, struct helpserv_bot *hs, char *text) {
    const int from_opserv = 0; /* for helpserv_notice */
    struct helpserv_request *req=NULL, *newest=NULL;
    struct helpserv_reqlist *reqlist, *hand_reqlist;
    unsigned int i;

    if ((reqlist = dict_find(helpserv_reqs_bynick_dict, user->nick, NULL))) {
        for (i=0; i < reqlist->used; i++) {
            req = reqlist->list[i];
            if (req->hs != hs)
                continue;
            if (!newest || (newest->opened < req->opened))
                newest = req;
        }

        /* If nothing was found, this will set req to NULL */
        req = newest;
    }

    if (user->handle_info) {
        hand_reqlist = dict_find(helpserv_reqs_byhand_dict, user->handle_info->handle, NULL);
        if (!req && hand_reqlist) {
            /* Most recent first again */
            for (i=0; i < hand_reqlist->used; i++) {
                req = hand_reqlist->list[i];
                if ((req->hs != hs) || req->user)
                    continue;
                if (!newest || (newest->opened < req->opened))
                    newest = req;
            }
            req = newest;

            if (req) {
                req->user = user;
                if (!reqlist) {
                    reqlist = helpserv_reqlist_alloc();
                    dict_insert(helpserv_reqs_bynick_dict, user->nick, reqlist);
                }
                req->parent_nick_list = reqlist;
                helpserv_reqlist_append(reqlist, req);

                if (req->helper && (hs->notify >= NOTIFY_USER))
                    helpserv_notify(req->helper, "HSMSG_NOTIFY_USER_FOUND", req->id, user->nick);

                helpserv_msguser(user, "HSMSG_GREET_PRIVMSG_EXISTREQ", req->id);
            }
        }
    } else {
        hand_reqlist = NULL;
    }

    if (!req) {
        if (text[0] == helpserv_conf.user_escape) {
            helpserv_msguser(user, "HSMSG_USERCMD_NO_REQUEST");
            return;
        }
        if ((hs->persist_types[PERSIST_T_REQUEST] == PERSIST_PART) && !GetUserMode(hs->helpchan, user)) {
            helpserv_msguser(user, "HSMSG_REQ_YOU_NOT_IN_HELPCHAN_OPEN", hs->helpchan->name);
            return;
        }

        req = create_request(user, hs, 0);
        if (user->handle_info)
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_NEW_REQUEST_AUTHED", req->id, user->nick, user->handle_info->handle);
        else
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_NEW_REQUEST_UNAUTHED", req->id, user->nick);
    } else if (text[0] == helpserv_conf.user_escape) {
        char cmdname[MAXLEN], *space;
        helpserv_usercmd_t *usercmd;
        struct userNode *likely_helper;

        /* Find somebody likely to be the helper */
        if (!req->helper)
            likely_helper = NULL;
        else if ((likely_helper = req->helper->handle->users) && !likely_helper->next_authed) {
            /* only one user it could be :> */
        } else for (likely_helper = req->helper->handle->users; likely_helper; likely_helper = likely_helper->next_authed)
            if (GetUserMode(hs->helpchan, likely_helper))
                break;

        /* Parse out command name */
        space = strchr(text+1, ' ');
        if (space)
            strncpy(cmdname, text+1, space-text-1);
        else
            strcpy(cmdname, text+1);

        /* Call the user command function */
        usercmd = dict_find(helpserv_usercmd_dict, cmdname, NULL);
        if (usercmd)
            usercmd(req, likely_helper, space+1);
        else
            helpserv_msguser(user, "HSMSG_USERCMD_UNKNOWN", cmdname);
        return;
    } else if (hs->intervals[INTERVAL_STALE_DELAY]
               && (req->updated < (time_t)(now - hs->intervals[INTERVAL_STALE_DELAY]))
               && (!hs->req_maxlen || req->text->used < hs->req_maxlen)) {
        char buf[MAX_LINE_SIZE], updatestr[INTERVALLEN], timestr[MAX_LINE_SIZE];

        strftime(timestr, MAX_LINE_SIZE, HSFMT_TIME, localtime(&req->opened));
        intervalString(updatestr, now - req->updated, user->handle_info);
        if (req->helper && (hs->notify >= NOTIFY_USER))
            if (user->handle_info)
                helpserv_notify(req->helper, "HSMSG_PAGE_UPD_REQUEST_AUTHED", req->id, user->nick, user->handle_info->handle, timestr, updatestr);
            else
                helpserv_notify(req->helper, "HSMSG_PAGE_UPD_REQUESTNOT_AUTHED", req->id, user->nick, timestr, updatestr);
        else
            if (user->handle_info)
                helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_UPD_REQUEST_AUTHED", req->id, user->nick, user->handle_info->handle, timestr, updatestr);
            else
                helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_UPD_REQUEST_NOT_AUTHED", req->id, user->nick, timestr, updatestr);
        strftime(timestr, MAX_LINE_SIZE, HSFMT_TIME, localtime(&now));
        snprintf(buf, MAX_LINE_SIZE, "[Stale request updated at %s]", timestr);
        string_list_append(req->text, strdup(buf));
    }

    req->updated = now;
    if (!hs->req_maxlen || req->text->used < hs->req_maxlen)
        string_list_append(req->text, strdup(text));
    else
        helpserv_msguser(user, "HSMSG_REQ_MAXLEN");
}

/* Handle messages direct to a HelpServ bot. */
static void helpserv_botmsg(struct userNode *user, struct userNode *target, char *text, UNUSED_ARG(int server_qualified)) {
    struct helpserv_bot *hs;
    struct helpserv_cmd *cmd;
    struct helpserv_user *hs_user;
    char *argv[MAXNUMPARAMS];
    int argc, argv_shift;
    const int from_opserv = 0; /* for helpserv_notice */

    /* Ignore things consisting of empty lines or from ourselves */
    if (!*text || IsLocal(user))
        return;

    hs = dict_find(helpserv_bots_dict, target->nick, NULL);

    /* See if we should listen to their message as a command (helper)
     * or a help request (user) */
    if (!user->handle_info || !(hs_user = dict_find(hs->users, user->handle_info->handle, NULL))) {
        helpserv_usermsg(user, hs, text);
        return;
    }

    argv_shift = 1;
    argc = split_line(text, false, ArrayLength(argv)-argv_shift, argv+argv_shift);
    if (!argc)
        return;

    cmd = dict_find(helpserv_func_dict, argv[argv_shift], NULL);
    if (!cmd) {
        helpserv_notice(user, "MSG_COMMAND_UNKNOWN", argv[argv_shift]);
        return;
    }
    if (cmd->flags & CMD_FROM_OPSERV_ONLY) {
        helpserv_notice(user, "HSMSG_OPER_CMD");
        return;
    }
    if (cmd->access > hs_user->level) {
        helpserv_notice(user, "HSMSG_LEVEL_TOO_LOW");
        return;
    }
    if (!cmd->func) {
        helpserv_notice(user, "HSMSG_INTERNAL_COMMAND", argv[argv_shift]);
    } else if (cmd->func(user, hs, 0, argc, argv+argv_shift)) {
        unsplit_string(argv+argv_shift, argc, text);
        log_audit(HS_LOG, LOG_COMMAND, user, hs->helpserv, hs->helpchan->name, 0, text);
    }
}

/* Handle a control command from an IRC operator */
static MODCMD_FUNC(cmd_helpserv) {
    struct helpserv_bot *hs = NULL;
    struct helpserv_cmd *subcmd;
    const int from_opserv = 1; /* for helpserv_notice */
    char botnick[NICKLEN+1]; /* in case command is unregister */
    int retval;

    if (argc < 2) {
        send_help(user, opserv, helpserv_helpfile, NULL);
        return 0;
    }

    if (!(subcmd = dict_find(helpserv_func_dict, argv[1], NULL))) {
        helpserv_notice(user, "MSG_COMMAND_UNKNOWN", argv[1]);
        return 0;
    }

    if (!subcmd->func) {
        helpserv_notice(user, "HSMSG_INTERNAL_COMMAND", argv[1]);
        return 0;
    }

    if ((subcmd->flags & CMD_NEED_BOT) && ((argc < 3) || !(hs = dict_find(helpserv_bots_dict, argv[2], NULL)))) {
        helpserv_notice(user, "HSMSG_INVALID_BOT");
        return 0;
    }

    if (subcmd->flags & CMD_NEVER_FROM_OPSERV) {
        helpserv_notice(user, "HSMSG_NO_USE_OPSERV");
        return 0;
    }

    if (hs) {
        argv[2] = argv[1];
        strcpy(botnick, hs->helpserv->nick);
        retval = subcmd->func(user, hs, 1, argc-2, argv+2);
    } else {
        strcpy(botnick, "No bot");
        retval = subcmd->func(user, hs, 1, argc-1, argv+1);
    }

    return retval;
}

static void helpserv_help(struct helpserv_bot *hs, int from_opserv, struct userNode *user, const char *topic) {
    send_help(user, (from_opserv ? opserv : hs->helpserv), helpserv_helpfile, topic);
}

static int append_entry(const char *key, UNUSED_ARG(void *data), void *extra) {
    struct helpfile_expansion *exp = extra;
    int row;

    row = exp->value.table.length++;
    exp->value.table.contents[row] = calloc(1, sizeof(char*));
    exp->value.table.contents[row][0] = key;
    return 0;
}

static struct helpfile_expansion helpserv_expand_variable(const char *variable) {
    struct helpfile_expansion exp;

    if (!irccasecmp(variable, "index")) {
        exp.type = HF_TABLE;
        exp.value.table.length = 1;
        exp.value.table.width = 1;
        exp.value.table.flags = TABLE_REPEAT_ROWS;
        exp.value.table.contents = calloc(dict_size(helpserv_func_dict)+1, sizeof(char**));
        exp.value.table.contents[0] = calloc(1, sizeof(char*));
        exp.value.table.contents[0][0] = "Commands:";
        dict_foreach(helpserv_func_dict, append_entry, &exp);
        return exp;
    }

    exp.type = HF_STRING;
    exp.value.str = NULL;
    return exp;
}

static void helpserv_helpfile_read(void) {
    helpserv_helpfile = open_helpfile(HELPSERV_HELPFILE_NAME, helpserv_expand_variable);
}

static HELPSERV_USERCMD(usercmd_wait) {
    struct helpserv_request *other;
    int pos, count;
    char buf[INTERVALLEN];

    if (req->helper) {
        if (likely_helper)
            helpserv_user_reply("HSMSG_YOU_BEING_HELPED_BY", likely_helper->nick);
        else
            helpserv_user_reply("HSMSG_YOU_BEING_HELPED");
        return;
    }

    for (other = req->hs->unhandled, pos = -1, count = 0; 
         other;
         other = other->next_unhandled, ++count) {
        if (other == req)
            pos = count;
    }
    assert(pos >= 0);
    intervalString(buf, now - req->hs->unhandled->opened, req->user->handle_info);
    helpserv_user_reply("HSMSG_WAIT_STATUS", pos+1, count, buf);
}

static HELPSERV_FUNC(cmd_help) {
    const char *topic;

    if (argc < 2)
        topic = NULL;
    else
        topic = unsplit_string(argv+1, argc-1, NULL);
    helpserv_help(hs, from_opserv, user, topic);

    return 1;
}

static HELPSERV_FUNC(cmd_readhelp) {
    struct timeval start, stop;
    struct helpfile *old_helpfile = helpserv_helpfile;

    gettimeofday(&start, NULL);
    helpserv_helpfile_read();
    if (helpserv_helpfile) {
        close_helpfile(old_helpfile);
    } else {
        helpserv_helpfile = old_helpfile;
    }
    gettimeofday(&stop, NULL);
    stop.tv_sec -= start.tv_sec;
    stop.tv_usec -= start.tv_usec;
    if (stop.tv_usec < 0) {
        stop.tv_sec -= 1;
        stop.tv_usec += 1000000;
    }
    helpserv_notice(user, "HSMSG_READHELP_SUCCESS", stop.tv_sec, stop.tv_usec/1000);

    return 1;
}

static struct helpserv_user * helpserv_add_user(struct helpserv_bot *hs, struct handle_info *handle, enum helpserv_level level) {
    struct helpserv_user *hs_user;
    struct helpserv_userlist *userlist;

    hs_user = calloc(1, sizeof(struct helpserv_user));
    hs_user->handle = handle;
    hs_user->hs = hs;
    hs_user->help_mode = 0;
    hs_user->level = level;
    hs_user->join_time = find_handle_in_channel(hs->helpchan, handle, NULL) ? now : 0;
    dict_insert(hs->users, handle->handle, hs_user);

    if (!(userlist = dict_find(helpserv_users_byhand_dict, handle->handle, NULL))) {
        userlist = helpserv_userlist_alloc();
        dict_insert(helpserv_users_byhand_dict, handle->handle, userlist);
    }
    helpserv_userlist_append(userlist, hs_user);

    return hs_user;
}

static void helpserv_del_user(struct helpserv_bot *hs, struct helpserv_user *hs_user) {
    dict_remove(hs->users, hs_user->handle->handle);
}

static int cmd_add_user(struct helpserv_bot *hs, int from_opserv, struct userNode *user, enum helpserv_level level, int argc, char *argv[]) {
    struct helpserv_user *actor, *new_user;
    struct handle_info *handle;

    REQUIRE_PARMS(2);

    if (!from_opserv) {
        actor = GetHSUser(hs, user->handle_info);
        if (actor->level < HlManager) {
            helpserv_notice(user, "HSMSG_CANNOT_ADD");
            return 0;
        }
    } else {
        actor = NULL;
    }

    if (!(handle = helpserv_get_handle_info(user, argv[1])))
        return 0;

    if (GetHSUser(hs, handle)) {
        helpserv_notice(user, "HSMSG_USER_EXISTS", handle->handle);
        return 0;
    }

    if (!(from_opserv) && actor && (actor->level <= level)) {
        helpserv_notice(user, "HSMSG_NO_BUMP_ACCESS");
        return 0;
    }

    new_user = helpserv_add_user(hs, handle, level);

    helpserv_notice(user, "HSMSG_ADDED_USER", helpserv_level2str(level), handle->handle);
    return 1;
}

static HELPSERV_FUNC(cmd_deluser) {
    struct helpserv_user *actor=NULL, *victim;
    struct handle_info *handle;
    enum helpserv_level level;

    REQUIRE_PARMS(2);

    if (!from_opserv) {
        actor = GetHSUser(hs, user->handle_info);
        if (actor->level < HlManager) {
            helpserv_notice(user, "HSMSG_CANNOT_DEL");
            return 0;
        }
    }

    if (!(handle = helpserv_get_handle_info(user, argv[1])))
        return 0;

    if (!(victim = GetHSUser(hs, handle))) {
        helpserv_notice(user, "HSMSG_NOT_IN_USERLIST", handle->handle, hs->helpserv->nick);
        return 0;
    }

    if (!from_opserv && actor && (actor->level <= victim->level)) {
        helpserv_notice(user, "MSG_USER_OUTRANKED", victim->handle->handle);
        return 0;
    }

    level = victim->level;
    helpserv_del_user(hs, victim);
    helpserv_notice(user, "HSMSG_DELETED_USER", helpserv_level2str(level), handle->handle);
    return 1;
}

static int
helpserv_user_comp(const void *arg_a, const void *arg_b)
{
    const struct helpserv_user *a = *(struct helpserv_user**)arg_a;
    const struct helpserv_user *b = *(struct helpserv_user**)arg_b;
    int res;
    if (a->level != b->level)
        res = b->level - a->level;
    else
        res = irccasecmp(a->handle->handle, b->handle->handle);
    return res;
}

static int show_helper_range(struct userNode *user, struct helpserv_bot *hs, int from_opserv, enum helpserv_level min_lvl, enum helpserv_level max_lvl) {
    struct helpserv_userlist users;
    struct helpfile_table tbl;
    struct helpserv_user *hs_user;
    dict_iterator_t it;
    enum helpserv_level last_level;
    unsigned int ii;

    users.used = 0;
    users.size = dict_size(hs->users);
    users.list = alloca(users.size*sizeof(hs->users[0]));
    helpserv_notice(user, "HSMSG_USERLIST_HEADER", hs->helpserv->nick);
    for (it = dict_first(hs->users); it; it = iter_next(it)) {
        hs_user = iter_data(it);
        if (hs_user->level < min_lvl)
            continue;
        if (hs_user->level > max_lvl)
            continue;
        users.list[users.used++] = hs_user;
    }
    if (!users.used) {
        helpserv_notice(user, "MSG_NONE");
        return 1;
    }
    qsort(users.list, users.used, sizeof(users.list[0]), helpserv_user_comp);
    switch (user->handle_info->userlist_style) {
    case HI_STYLE_DEF:
        tbl.length = users.used + 1;
        tbl.width = 3;
        tbl.flags = TABLE_NO_FREE;
        tbl.contents = alloca(tbl.length * sizeof(tbl.contents[0]));
        tbl.contents[0] = alloca(tbl.width * sizeof(tbl.contents[0][0]));
        tbl.contents[0][0] = "Level";
        tbl.contents[0][1] = "Handle";
        tbl.contents[0][2] = "WeekStart";
        for (ii = 0; ii < users.used; ) {
            hs_user = users.list[ii++];
            tbl.contents[ii] = alloca(tbl.width * sizeof(tbl.contents[0][0]));
            tbl.contents[ii][0] = helpserv_level_names[hs_user->level];
            tbl.contents[ii][1] = hs_user->handle->handle;
            tbl.contents[ii][2] = weekday_names[hs_user->week_start];
        }
        table_send((from_opserv ? opserv : hs->helpserv), user->nick, 0, NULL, tbl);
        break;
    case HI_STYLE_ZOOT: default:
        last_level = HlNone;
        tbl.length = 0;
        tbl.width = 1;
        tbl.flags = TABLE_NO_FREE | TABLE_REPEAT_ROWS | TABLE_NO_HEADERS;
        tbl.contents = alloca(users.used * sizeof(tbl.contents[0]));
        for (ii = 0; ii < users.used; ) {
            hs_user = users.list[ii++];
            if (hs_user->level != last_level) {
                if (tbl.length) {
                    helpserv_notice(user, "HSMSG_USERLIST_ZOOT_LVL", hs->helpserv->nick, helpserv_level_names[last_level]);
                    table_send((from_opserv ? opserv : hs->helpserv), user->nick, 0, NULL, tbl);
                    tbl.length = 0;
                }
                last_level = hs_user->level;
            }
            tbl.contents[tbl.length] = alloca(tbl.width * sizeof(tbl.contents[0][0]));
            tbl.contents[tbl.length++][0] = hs_user->handle->handle;
        }
        if (tbl.length) {
            helpserv_notice(user, "HSMSG_USERLIST_ZOOT_LVL", hs->helpserv->nick, helpserv_level_names[last_level]);
            table_send((from_opserv ? opserv : hs->helpserv), user->nick, 0, NULL, tbl);
        }
    }
    return 1;
}

static HELPSERV_FUNC(cmd_helpers) {
    return show_helper_range(user, hs, from_opserv, HlTrial, HlOwner);
}

static HELPSERV_FUNC(cmd_wlist) {
    return show_helper_range(user, hs, from_opserv, HlOwner, HlOwner);
}

static HELPSERV_FUNC(cmd_mlist) {
    return show_helper_range(user, hs, from_opserv, HlManager, HlManager);
}

static HELPSERV_FUNC(cmd_hlist) {
    return show_helper_range(user, hs, from_opserv, HlHelper, HlHelper);
}

static HELPSERV_FUNC(cmd_tlist) {
    return show_helper_range(user, hs, from_opserv, HlTrial, HlTrial);
}

static HELPSERV_FUNC(cmd_addowner) {
    return cmd_add_user(hs, from_opserv, user, HlOwner, argc, argv);
}

static HELPSERV_FUNC(cmd_addmanager) {
    return cmd_add_user(hs, from_opserv, user, HlManager, argc, argv);
}

static HELPSERV_FUNC(cmd_addhelper) {
    return cmd_add_user(hs, from_opserv, user, HlHelper, argc, argv);
}

static HELPSERV_FUNC(cmd_addtrial) {
    return cmd_add_user(hs, from_opserv, user, HlTrial, argc, argv);
}

static HELPSERV_FUNC(cmd_clvl) {
    struct helpserv_user *actor=NULL, *victim;
    struct handle_info *handle;
    enum helpserv_level level;

    REQUIRE_PARMS(3);

    if (!from_opserv) {
        actor = GetHSUser(hs, user->handle_info);
        if (actor->level < HlManager) {
            helpserv_notice(user, "HSMSG_CANNOT_CLVL");
            return 0;
        }
    }

    if (!(handle = helpserv_get_handle_info(user, argv[1])))
        return 0;

    if (!(victim = GetHSUser(hs, handle))) {
        helpserv_notice(user, "HSMSG_NOT_IN_USERLIST", handle->handle, hs->helpserv->nick);
        return 0;
    }

    if (((level = helpserv_str2level(argv[2])) == HlNone) || level == HlOper) {
        helpserv_notice(user, "HSMSG_INVALID_ACCESS", argv[2]);
        return 0;
    }

    if (!(from_opserv) && actor) {
        if (actor == victim) {
            helpserv_notice(user, "HSMSG_NO_SELF_CLVL");
            return 0;
        }

        if (actor->level <= victim->level) {
            helpserv_notice(user, "MSG_USER_OUTRANKED", victim->handle->handle);
            return 0;
        }

        if (level >= actor->level) {
            helpserv_notice(user, "HSMSG_NO_BUMP_ACCESS");
            return 0;
        }
    }

    victim->level = level;
    helpserv_notice(user, "HSMSG_CHANGED_ACCESS", handle->handle, helpserv_level2str(level));

    return 1;
}

static void free_request(void *data) {
    struct helpserv_request *req = data;

    /* Logging */
    if (shutting_down && (req->hs->persist_types[PERSIST_T_REQUEST] != PERSIST_CLOSE || !req->handle)) {
        helpserv_log_request(req, "srvx shutdown");
    }

    /* Clean up from the unhandled queue */
    if (req->hs->unhandled) {
        if (req->hs->unhandled == req) {
            req->hs->unhandled = req->next_unhandled;
        } else {
            struct helpserv_request *uh;
            for (uh=req->hs->unhandled; uh->next_unhandled && (uh->next_unhandled != req); uh = uh->next_unhandled);
            if (uh->next_unhandled) {
                uh->next_unhandled = req->next_unhandled;
            }
        }
    }

    /* Clean up the lists */
    if (req->parent_nick_list) {
        if (req->parent_nick_list->used == 1) {
            dict_remove(helpserv_reqs_bynick_dict, req->user->nick);
        } else {
            helpserv_reqlist_remove(req->parent_nick_list, req);
        }
    }
    if (req->parent_hand_list) {
        if (req->parent_hand_list->used == 1) {
            dict_remove(helpserv_reqs_byhand_dict, req->handle->handle);
        } else {
            helpserv_reqlist_remove(req->parent_hand_list, req);
        }
    }

    free_string_list(req->text);
    free(req);
}

static HELPSERV_FUNC(cmd_close) {
    struct helpserv_request *req, *newest=NULL;
    struct helpserv_reqlist *nick_list, *hand_list;
    struct helpserv_user *hs_user=GetHSUser(hs, user->handle_info);
    struct userNode *req_user=NULL;
    char close_reason[MAXLEN], reqnum[12];
    unsigned long old_req;
    unsigned int i;
    int num_requests=0;

    REQUIRE_PARMS(2);

    assert(hs_user);

    if (!(req = smart_get_request(hs, hs_user, argv[1], &num_requests))) {
        helpserv_notice(user, "HSMSG_REQ_INVALID", argv[1]);
        return 0;
    }

    sprintf(reqnum, "%lu", req->id);

    if (num_requests > 1)
        helpserv_notice(user, "HSMSG_REQ_FOUNDMANY");

    if (hs_user->level < HlManager && req->helper != hs_user) {
        if (req->helper)
            helpserv_notice(user, "HSMSG_REQ_NOT_YOURS_ASSIGNED_TO", req->id, req->helper->handle->handle);
        else
            helpserv_notice(user, "HSMSG_REQ_NOT_YOURS_UNASSIGNED", req->id);
        return 0;
    }

    helpserv_notice(user, "HSMSG_REQ_CLOSED", req->id);
    if (req->user) {
        req_user = req->user;
        helpserv_message(hs, req->user, MSGTYPE_REQ_CLOSED);
        if (req->handle)
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_CLOSE_REQUEST_1", req->id, req->user->nick, req->handle->handle, user->nick);
        else
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_CLOSE_REQUEST_2", req->id, req->user->nick, user->nick);
    } else {
        if (req->handle)
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_CLOSE_REQUEST_3", req->id, req->handle->handle, user->nick);
        else
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_CLOSE_REQUEST_4", req->id, user->nick);
    }

    hs_user->closed[0]++;
    hs_user->closed[4]++;

    /* Set these to keep track of the lists after the request is gone, but
     * not if free_request() will helpserv_reqlist_free() them. */
    nick_list = req->parent_nick_list;
    if (nick_list && (nick_list->used == 1))
        nick_list = NULL;
    hand_list = req->parent_hand_list;
    if (hand_list && (hand_list->used == 1))
        hand_list = NULL;
    old_req = req->id;

    if (argc >= 3) {
        snprintf(close_reason, MAXLEN, "Closed by %s: %s", user->handle_info->handle, unsplit_string(argv+2, argc-2, NULL));
    } else {
        sprintf(close_reason, "Closed by %s", user->handle_info->handle);
    }
    helpserv_log_request(req, close_reason);
    dict_remove(hs->requests, reqnum);

    /* Look for other requests associated with them */
    if (nick_list) {
        for (i=0; i < nick_list->used; i++) {
            req = nick_list->list[i];

            if (req->hs != hs)
                continue;
            if (!newest || (newest->opened < req->opened))
                newest = req;
        }

        if (newest)
            helpserv_msguser(newest->user, "HSMSG_REQ_FOUND_ANOTHER", old_req, newest->id);
    }

    if (req_user && hs->auto_devoice) {
        struct modeNode *mn = GetUserMode(hs->helpchan, req_user);
        if ((!newest || !newest->helper) && mn && (mn->modes & MODE_VOICE)) {
            struct mod_chanmode change;
            mod_chanmode_init(&change);
            change.argc = 1;
            change.args[0].mode = MODE_REMOVE | MODE_VOICE;
            change.args[0].member = mn;
            mod_chanmode_announce(hs->helpserv, hs->helpchan, &change);
        }
    }

    return 1;
}

static HELPSERV_FUNC(cmd_list) {
    dict_iterator_t it;
    int searchtype;
    struct helpfile_table tbl;
    unsigned int line, total;
    struct helpserv_request *req;

    if ((argc < 2) || !irccasecmp(argv[1], "unassigned")) {
        for (req = hs->unhandled, total=0; req; req = req->next_unhandled, total++) ;
        helpserv_notice(user, "HSMSG_REQ_LIST_TOP_UNASSIGNED", total);
        searchtype = 1; /* Unassigned */
    } else if (!irccasecmp(argv[1], "assigned")) {
        for (req = hs->unhandled, total=dict_size(hs->requests); req; req = req->next_unhandled, total--) ;
        helpserv_notice(user, "HSMSG_REQ_LIST_TOP_ASSIGNED", total);
        searchtype = 2; /* Assigned */
    } else if (!irccasecmp(argv[1], "me")) {
        for (total = 0, it = dict_first(hs->requests); it; it = iter_next(it)) {
            req = iter_data(it);
            if (req->helper && (req->helper->handle == user->handle_info))
                total++;
        }
        helpserv_notice(user, "HSMSG_REQ_LIST_TOP_YOUR", total);
        searchtype = 4;
    } else if (!irccasecmp(argv[1], "all")) {
        total = dict_size(hs->requests);
        helpserv_notice(user, "HSMSG_REQ_LIST_TOP_ALL", total);
        searchtype = 3; /* All */
    } else {
        helpserv_notice(user, "HSMSG_BAD_REQ_TYPE", argv[1]);
        return 0;
    }

    if (!total) {
        helpserv_notice(user, "HSMSG_REQ_LIST_NONE");
        return 1;
    }

    tbl.length = total+1;
    tbl.width = 5;
    tbl.flags = TABLE_NO_FREE;
    tbl.contents = alloca(tbl.length * sizeof(*tbl.contents));
    tbl.contents[0] = alloca(tbl.width * sizeof(**tbl.contents));
    tbl.contents[0][0] = "ID#";
    tbl.contents[0][1] = "User";
    tbl.contents[0][2] = "Helper";
    tbl.contents[0][3] = "Time open";
    tbl.contents[0][4] = "User status";

    for (it=dict_first(hs->requests), line=0; it; it=iter_next(it)) {
        char opentime[INTERVALLEN], reqid[12], username[NICKLEN+2];

        req = iter_data(it);

        switch (searchtype) {
        case 1:
            if (req->helper)
                continue;
            break;
        case 2:
            if (!req->helper)
                continue;
            break;
        case 3:
        default:
            break;
        case 4:
            if (!req->helper || (req->helper->handle != user->handle_info))
                continue;
            break;
        }

        line++;

        tbl.contents[line] = alloca(tbl.width * sizeof(**tbl.contents));
        sprintf(reqid, "%lu", req->id);
        tbl.contents[line][0] = strdup(reqid);
        if (req->user) {
            strcpy(username, req->user->nick);
        } else {
            username[0] = '*';
            strcpy(username+1, req->handle->handle);
        }
        tbl.contents[line][1] = strdup(username);
        tbl.contents[line][2] = req->helper ? req->helper->handle->handle : "(Unassigned)";
        intervalString(opentime, now - req->opened, user->handle_info);
        tbl.contents[line][3] = strdup(opentime);
        tbl.contents[line][4] = ((req->user || req->handle->users) ? "Online" : "Offline");
    }

    table_send((from_opserv ? opserv : hs->helpserv), user->nick, 0, NULL, tbl);

    for (; line > 0; line--) {
        free((char *)tbl.contents[line][0]);
        free((char *)tbl.contents[line][1]);
        free((char *)tbl.contents[line][3]);
    }

    return 1;
}

static void helpserv_show(int from_opserv, struct helpserv_bot *hs, struct userNode *user, struct helpserv_request *req) {
    unsigned int nn;
    char buf[MAX_LINE_SIZE];
    char buf2[INTERVALLEN];

    if (req->user)
        if (req->handle)
            helpserv_notice(user, "HSMSG_REQ_INFO_2a", req->user->nick, req->handle->handle);
        else
            helpserv_notice(user, "HSMSG_REQ_INFO_2b", req->user->nick);
    else if (req->handle)
        if (req->handle->users)
            helpserv_notice(user, "HSMSG_REQ_INFO_2c", req->handle->handle);
        else
            helpserv_notice(user, "HSMSG_REQ_INFO_2d", req->handle->handle);
    else
        helpserv_notice(user, "HSMSG_REQ_INFO_2e");
    strftime(buf, MAX_LINE_SIZE, HSFMT_TIME, localtime(&req->opened));
    intervalString(buf2, now - req->opened, user->handle_info);
    helpserv_notice(user, "HSMSG_REQ_INFO_3", buf, buf2);
    helpserv_notice(user, "HSMSG_REQ_INFO_4");
    for (nn=0; nn < req->text->used; nn++)
        helpserv_notice(user, "HSMSG_REQ_INFO_MESSAGE", req->text->list[nn]);
}

/* actor is the one who executed the command... it should == user except from
 * cmd_assign */
static int helpserv_assign(int from_opserv, struct helpserv_bot *hs, struct userNode *user, struct userNode *actor, struct helpserv_request *req) {
    struct helpserv_request *req2;
    struct helpserv_user *old_helper;

    if (!user->handle_info)
        return 0;
    if ((hs->persist_types[PERSIST_T_HELPER] == PERSIST_PART) && !GetUserMode(hs->helpchan, user)) {
        struct helpserv_user *hsuser_actor = GetHSUser(hs, actor->handle_info);
        if (hsuser_actor->level < HlManager) {
            helpserv_notice(user, "HSMSG_REQ_YOU_NOT_IN_HELPCHAN", hs->helpchan->name);
            return 0;
        } else if (user != actor) {
            helpserv_notice(user, "HSMSG_REQ_YOU_NOT_IN_OVERRIDE", hs->helpchan->name);
            helpserv_notice(actor, "HSMSG_REQ_HIM_NOT_IN_OVERRIDE", user->nick, hs->helpchan->name);
        } else
            helpserv_notice(user, "HSMSG_REQ_SELF_NOT_IN_OVERRIDE", hs->helpchan->name);
    }

    hs->last_active = now;
    if ((old_helper = req->helper)) {
        /* don't need to remove from the unhandled queue */
    } else if (hs->unhandled == req) {
        hs->unhandled = req->next_unhandled;
    } else for (req2 = hs->unhandled; req2; req2 = req2->next_unhandled) {
        if (req2->next_unhandled == req) {
            req2->next_unhandled = req->next_unhandled;
            break;
        }
    }
    req->next_unhandled = NULL;
    req->helper = GetHSUser(hs, user->handle_info);
    assert(req->helper);
    req->assigned = now;

    if (old_helper) {
        helpserv_notice(user, "HSMSG_REQ_REASSIGNED", req->id, old_helper->handle->handle);
        req->helper->reassigned_to[0]++;
        req->helper->reassigned_to[4]++;
        old_helper->reassigned_from[0]++;
        old_helper->reassigned_from[4]++;
    } else {
        helpserv_notice(user, "HSMSG_REQ_ASSIGNED_YOU", req->id);
        req->helper->picked_up[0]++;
        req->helper->picked_up[4]++;
    }
    helpserv_show(from_opserv, hs, user, req);
    if (req->user) {
        helpserv_message(hs, req->user, MSGTYPE_REQ_ASSIGNED);
        if (old_helper) {
            helpserv_msguser(req->user, "HSMSG_REQ_ASSIGNED_AGAIN", req->id, user->handle_info->handle, user->nick);
        } else {
            helpserv_msguser(req->user, "HSMSG_REQ_ASSIGNED", req->id, user->handle_info->handle, user->nick);
        }
        if (req->handle)
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_ASSIGN_REQUEST_1", req->id, req->user->nick, req->handle->handle, user->nick);
        else
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_ASSIGN_REQUEST_2", req->id, req->user->nick, user->nick);
    } else {
        if (req->handle)
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_ASSIGN_REQUEST_3", req->id, req->handle->handle, user->nick);
        else
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_ASSIGN_REQUEST_4", req->id, user->nick);
    }

    if (req->user && hs->auto_voice) {
        struct mod_chanmode change;
        mod_chanmode_init(&change);
        change.argc = 1;
        change.args[0].mode = MODE_VOICE;
        if ((change.args[0].member = GetUserMode(hs->helpchan, req->user)))
            mod_chanmode_announce(hs->helpserv, hs->helpchan, &change);
    }

    return 1;
}

static HELPSERV_FUNC(cmd_next) {
    struct helpserv_request *req;

    if (!(req = hs->unhandled)) {
        helpserv_notice(user, "HSMSG_REQ_NO_UNASSIGNED");
        return 0;
    }
    return helpserv_assign(from_opserv, hs, user, user, req);
}

static HELPSERV_FUNC(cmd_show) {
    struct helpserv_request *req;
    struct helpserv_user *hs_user=GetHSUser(hs, user->handle_info);
    int num_requests=0;

    REQUIRE_PARMS(2);

    assert(hs_user);

    if (!(req = smart_get_request(hs, hs_user, argv[1], &num_requests))) {
        helpserv_notice(user, "HSMSG_REQ_INVALID", argv[1]);
        return 0;
    }

    if (num_requests > 1)
        helpserv_notice(user, "HSMSG_REQ_FOUNDMANY");

    helpserv_notice(user, "HSMSG_REQ_INFO_1", req->id);
    helpserv_show(from_opserv, hs, user, req);
    return 1;
}

static HELPSERV_FUNC(cmd_pickup) {
    struct helpserv_request *req;
    struct helpserv_user *hs_user=GetHSUser(hs, user->handle_info);
    int num_requests=0;

    REQUIRE_PARMS(2);

    assert(hs_user);

    if (!(req = smart_get_request(hs, hs_user, argv[1], &num_requests))) {
        helpserv_notice(user, "HSMSG_REQ_INVALID", argv[1]);
        return 0;
    }

    if (num_requests > 1)
        helpserv_notice(user, "HSMSG_REQ_FOUNDMANY");

    return helpserv_assign(from_opserv, hs, user, user, req);
}

static HELPSERV_FUNC(cmd_reassign) {
    struct helpserv_request *req;
    struct userNode *targetuser;
    struct helpserv_user *target;
    struct helpserv_user *hs_user=GetHSUser(hs, user->handle_info);
    int num_requests=0;

    REQUIRE_PARMS(3);

    assert(hs_user);

    if (!(req = smart_get_request(hs, hs_user, argv[1], &num_requests))) {
        helpserv_notice(user, "HSMSG_REQ_INVALID", argv[1]);
        return 0;
    }

    if (num_requests > 1)
        helpserv_notice(user, "HSMSG_REQ_FOUNDMANY");

    if (!(targetuser = GetUserH(argv[2]))) {
        helpserv_notice(user, "MSG_NICK_UNKNOWN", argv[2]);
        return 0;
    }

    if (!targetuser->handle_info) {
        helpserv_notice(user, "MSG_USER_AUTHENTICATE", targetuser->nick);
        return 0;
    }

    if (!(target = GetHSUser(hs, targetuser->handle_info))) {
        helpserv_notice(user, "HSMSG_NOT_IN_USERLIST", targetuser->nick, hs->helpserv->nick);
        return 0;
    }

    if ((hs->persist_types[PERSIST_T_HELPER] == PERSIST_PART) && !GetUserMode(hs->helpchan, user) && (hs_user->level < HlManager)) {
        helpserv_notice(user, "HSMSG_REQ_HIM_NOT_IN_HELPCHAN", targetuser->nick, hs->helpchan->name);
        return 0;
    }

    helpserv_assign(from_opserv, hs, targetuser, user, req);
    return 1;
}

static HELPSERV_FUNC(cmd_addnote) {
    char text[MAX_LINE_SIZE], timestr[MAX_LINE_SIZE], *note;
    struct helpserv_request *req;
    struct helpserv_user *hs_user=GetHSUser(hs, user->handle_info);
    int num_requests=0;

    REQUIRE_PARMS(3);

    assert(hs_user);

    if (!(req = smart_get_request(hs, hs_user, argv[1], &num_requests))) {
        helpserv_notice(user, "HSMSG_REQ_INVALID", argv[1]);
        return 0;
    }

    if (num_requests > 1)
        helpserv_notice(user, "HSMSG_REQ_FOUNDMANY");

    note = unsplit_string(argv+2, argc-2, NULL);

    strftime(timestr, MAX_LINE_SIZE, HSFMT_TIME, localtime(&now));
    snprintf(text, MAX_LINE_SIZE, "[Helper note at %s]:", timestr);
    string_list_append(req->text, strdup(text));
    snprintf(text, MAX_LINE_SIZE, "  <%s> %s", user->handle_info->handle, note);
    string_list_append(req->text, strdup(text));

    helpserv_notice(user, "HSMSG_REQMSG_NOTE_ADDED", req->id);

    return 1;
}

static HELPSERV_FUNC(cmd_page) {
    REQUIRE_PARMS(2);

    helpserv_page(PGSRC_COMMAND, "HSMSG_PAGE_REQUEST", user->nick, unsplit_string(argv+1, argc-1, NULL));

    return 1;
}

static HELPSERV_FUNC(cmd_stats) {
    struct helpserv_user *target, *hs_user;
    struct handle_info *target_handle;
    struct helpfile_table tbl;
    int i;
    char intervalstr[INTERVALLEN], buf[16];

    hs_user = from_opserv ? NULL : GetHSUser(hs, user->handle_info);

    if (argc > 1) {
        if (!from_opserv && (hs_user->level < HlManager)) {
            helpserv_notice(user, "HSMSG_NEED_MANAGER");
            return 0;
        }

        if (!(target_handle = helpserv_get_handle_info(user, argv[1]))) {
            return 0;
        }

        if (!(target = GetHSUser(hs, target_handle))) {
            helpserv_notice(user, "HSMSG_NOT_IN_USERLIST", target_handle->handle, hs->helpserv->nick);
            return 0;
        }
    } else {
        if (from_opserv) {
            helpserv_notice(user, "HSMSG_OPSERV_NEED_USER");
            return 0;
        }
        target = hs_user;
    }

    helpserv_notice(user, "HSMSG_STATS_TOP", hs->helpserv->nick, target->handle->handle, weekday_names[target->week_start]);

    tbl.length = 6;
    tbl.width = 2;
    tbl.flags = TABLE_NO_FREE;
    tbl.contents = alloca(tbl.length * sizeof(*tbl.contents));
    tbl.contents[0] = alloca(tbl.width * sizeof(**tbl.contents));
    tbl.contents[0][0] = "";
    tbl.contents[0][1] = "Recorded time";
    for (i=0; i < 5; i++) {
        unsigned int week_time = target->time_per_week[i];
        tbl.contents[i+1] = alloca(tbl.width * sizeof(**tbl.contents));
        if ((i == 0 || i == 4) && target->join_time)
            week_time += now - target->join_time;
        helpserv_interval(intervalstr, week_time);
        tbl.contents[i+1][1] = strdup(intervalstr);
    }
    tbl.contents[1][0] = "This week";
    tbl.contents[2][0] = "Last week";
    tbl.contents[3][0] = "2 weeks ago";
    tbl.contents[4][0] = "3 weeks ago";
    tbl.contents[5][0] = "Total";

    helpserv_notice(user, "HSMSG_STATS_TIME", hs->helpchan->name);
    table_send((from_opserv ? opserv : hs->helpserv), user->nick, 0, NULL, tbl);

    for (i=1; i <= 5; i++)
        free((char *)tbl.contents[i][1]);

    tbl.length = 5;
    tbl.width = 4;
    tbl.flags = TABLE_NO_FREE;
    tbl.contents = alloca(tbl.length * sizeof(*tbl.contents));
    tbl.contents[0] = alloca(tbl.width * sizeof(**tbl.contents));
    tbl.contents[1] = alloca(tbl.width * sizeof(**tbl.contents));
    tbl.contents[2] = alloca(tbl.width * sizeof(**tbl.contents));
    tbl.contents[3] = alloca(tbl.width * sizeof(**tbl.contents));
    tbl.contents[4] = alloca(tbl.width * sizeof(**tbl.contents));
    tbl.contents[0][0] = "Category";
    tbl.contents[0][1] = "This week";
    tbl.contents[0][2] = "Last week";
    tbl.contents[0][3] = "Total";

    tbl.contents[1][0] = "Requests picked up";
    for (i=0; i < 3; i++) {
        sprintf(buf, "%u", target->picked_up[(i == 2 ? 4 : i)]);
        tbl.contents[1][i+1] = strdup(buf);
    }
    tbl.contents[2][0] = "Requests closed";
    for (i=0; i < 3; i++) {
        sprintf(buf, "%u", target->closed[(i == 2 ? 4 : i)]);
        tbl.contents[2][i+1] = strdup(buf);
    }
    tbl.contents[3][0] = "Reassigned from";
    for (i=0; i < 3; i++) {
        sprintf(buf, "%u", target->reassigned_from[(i == 2 ? 4 : i)]);
        tbl.contents[3][i+1] = strdup(buf);
    }
    tbl.contents[4][0] = "Reassigned to";
    for (i=0; i < 3; i++) {
        sprintf(buf, "%u", target->reassigned_to[(i == 2 ? 4 : i)]);
        tbl.contents[4][i+1] = strdup(buf);
    }

    helpserv_notice(user, "HSMSG_STATS_REQS");
    table_send((from_opserv ? opserv : hs->helpserv), user->nick, 0, NULL, tbl);

    for (i=1; i < 5; i++) {
        free((char *)tbl.contents[i][1]);
        free((char *)tbl.contents[i][2]);
        free((char *)tbl.contents[i][3]);
    }

    return 1;
}

static HELPSERV_FUNC(cmd_statsreport) {
    int use_privmsg=1;
    struct helpfile_table tbl;
    dict_iterator_t it;
    unsigned int line, i;
    struct userNode *srcbot = from_opserv ? opserv : hs->helpserv;

    if ((argc > 1) && !irccasecmp(argv[1], "NOTICE"))
        use_privmsg = 0;

    tbl.length = dict_size(hs->users)+1;
    tbl.width = 3;
    tbl.flags = TABLE_NO_FREE;
    tbl.contents = alloca(tbl.length * sizeof(*tbl.contents));
    tbl.contents[0] = alloca(tbl.width * sizeof(**tbl.contents));
    tbl.contents[0][0] = "Account";
    tbl.contents[0][1] = "Requests";
    tbl.contents[0][2] = "Time helping";

    for (it=dict_first(hs->users), line=0; it; it=iter_next(it)) {
        struct helpserv_user *hs_user=iter_data(it);

        tbl.contents[++line] = alloca(tbl.width * sizeof(**tbl.contents));
        tbl.contents[line][0] = hs_user->handle->handle;
        tbl.contents[line][1] = malloc(12);
        tbl.contents[line][2] = malloc(32); /* A bit more than needed */
    }

    /* 4 to 1 instead of 3 to 0 because it's unsigned */
    for (i=4; i > 0; i--) {
        for (it=dict_first(hs->users), line=0; it; it=iter_next(it)) {
            struct helpserv_user *hs_user = iter_data(it);
            /* Time */
            unsigned int week_time = hs_user->time_per_week[i-1];
            if ((i==1) && hs_user->join_time)
                week_time += now - hs_user->join_time;
            helpserv_interval((char *)tbl.contents[++line][2], week_time);

            /* Requests */
            sprintf((char *)tbl.contents[line][1], "%u", hs_user->picked_up[i-1]+hs_user->reassigned_to[i-1]);
        }
        send_target_message(use_privmsg, user->nick, srcbot, statsreport_week[i-1]);
        table_send(srcbot, user->nick, 0, (use_privmsg ? irc_privmsg : irc_notice), tbl);
    }

    for (line=1; line <= dict_size(hs->users); line++) {
        free((char *)tbl.contents[line][1]);
        free((char *)tbl.contents[line][2]);
    }

    return 1;
}

static int
helpserv_in_channel(struct helpserv_bot *hs, struct chanNode *channel) {
    enum page_source pgsrc;
    if (channel == hs->helpchan)
        return 1;
    for (pgsrc=0; pgsrc<PGSRC_COUNT; pgsrc++)
        if (channel == hs->page_targets[pgsrc])
            return 1;
    return 0;
}

static HELPSERV_FUNC(cmd_move) {
    if (!hs) {
        helpserv_notice(user, "HSMSG_INVALID_BOT");
        return 0;
    }

    REQUIRE_PARMS(2);

    if (is_valid_nick(argv[1])) {
        char *newnick = argv[1], oldnick[NICKLEN], reason[MAXLEN];

        strcpy(oldnick, hs->helpserv->nick);

        if (GetUserH(newnick)) {
            helpserv_notice(user, "HSMSG_NICK_EXISTS", newnick);
            return 0;
        }

        dict_remove2(helpserv_bots_dict, hs->helpserv->nick, 1);
        NickChange(hs->helpserv, newnick, 0);
        dict_insert(helpserv_bots_dict, hs->helpserv->nick, hs);

        helpserv_notice(user, "HSMSG_RENAMED", oldnick, newnick);

        snprintf(reason, MAXLEN, "HelpServ bot %s (in %s) renamed to %s by %s.", oldnick, hs->helpchan->name, newnick, user->nick);
        global_message(MESSAGE_RECIPIENT_OPERS, reason);

        return 1;
    } else if (IsChannelName(argv[1])) {
        struct chanNode *old_helpchan = hs->helpchan;
        char *newchan = argv[1], oldchan[CHANNELLEN], reason[MAXLEN];
        struct helpserv_botlist *botlist;

        strcpy(oldchan, hs->helpchan->name);

        if (!irccasecmp(oldchan, newchan)) {
            helpserv_notice(user, "HSMSG_MOVE_SAME_CHANNEL", hs->helpserv->nick);
            return 0;
        }

        if (opserv_bad_channel(newchan)) {
            helpserv_notice(user, "HSMSG_ILLEGAL_CHANNEL", newchan);
            return 0;
        }

        botlist = dict_find(helpserv_bots_bychan_dict, hs->helpchan->name, NULL);
        helpserv_botlist_remove(botlist, hs);
        if (botlist->used == 0) {
            dict_remove(helpserv_bots_bychan_dict, hs->helpchan->name);
        }

        hs->helpchan = NULL;
        if (!helpserv_in_channel(hs, old_helpchan)) {
            snprintf(reason, MAXLEN, "Moved to %s by %s.", newchan, user->nick);
            DelChannelUser(hs->helpserv, old_helpchan, reason, 0);
        }

        if (!(hs->helpchan = GetChannel(newchan))) {
            hs->helpchan = AddChannel(newchan, now, NULL, NULL);
            AddChannelUser(hs->helpserv, hs->helpchan)->modes |= MODE_CHANOP;
        } else if (!helpserv_in_channel(hs, old_helpchan)) {
            struct mod_chanmode change;
            mod_chanmode_init(&change);
            change.argc = 1;
            change.args[0].mode = MODE_CHANOP;
            change.args[0].member = AddChannelUser(hs->helpserv, hs->helpchan);
            mod_chanmode_announce(hs->helpserv, hs->helpchan, &change);
        }

        if (!(botlist = dict_find(helpserv_bots_bychan_dict, hs->helpchan->name, NULL))) {
            botlist = helpserv_botlist_alloc();
            dict_insert(helpserv_bots_bychan_dict, hs->helpchan->name, botlist);
        }
        helpserv_botlist_append(botlist, hs);

        snprintf(reason, MAXLEN, "HelpServ %s (%s) moved to %s by %s.", hs->helpserv->nick, oldchan, newchan, user->nick);
        global_message(MESSAGE_RECIPIENT_OPERS, reason);

        return 1;
    } else {
        helpserv_notice(user, "HSMSG_INVALID_MOVE", argv[1]);
        return 0;
    }
}

static HELPSERV_FUNC(cmd_bots) {
    dict_iterator_t it;
    struct helpfile_table tbl;
    unsigned int i;

    helpserv_notice(user, "HSMSG_BOTLIST_HEADER");

    tbl.length = dict_size(helpserv_bots_dict)+1;
    tbl.width = 4;
    tbl.flags = TABLE_NO_FREE;
    tbl.contents = alloca(tbl.length * sizeof(*tbl.contents));
    tbl.contents[0] = alloca(tbl.width * sizeof(**tbl.contents));
    tbl.contents[0][0] = "Bot";
    tbl.contents[0][1] = "Channel";
    tbl.contents[0][2] = "Owner";
    tbl.contents[0][3] = "Inactivity";

    for (it=dict_first(helpserv_bots_dict), i=1; it; it=iter_next(it), i++) {
        dict_iterator_t it2;
        struct helpserv_bot *bot;
        struct helpserv_user *owner=NULL;

        bot = iter_data(it);
        
        for (it2=dict_first(bot->users); it2; it2=iter_next(it2)) {
            if (((struct helpserv_user *)iter_data(it2))->level == HlOwner) {
                owner = iter_data(it2);
                break;
            }
        }

        tbl.contents[i] = alloca(tbl.width * sizeof(**tbl.contents));
        tbl.contents[i][0] = iter_key(it);
        tbl.contents[i][1] = bot->helpchan->name;
        tbl.contents[i][2] = owner ? owner->handle->handle : "None";
        tbl.contents[i][3] = alloca(INTERVALLEN);
        intervalString((char*)tbl.contents[i][3], now - bot->last_active, user->handle_info);
    }

    table_send((from_opserv ? opserv : hs->helpserv), user->nick, 0, NULL, tbl);

    return 1;
}

static void helpserv_page_helper_gone(struct helpserv_bot *hs, struct helpserv_request *req, const char *reason) {
    const int from_opserv = 0;

    if (!req->helper)
        return;

    /* Let the user know that their request is now unhandled */
    if (req->user) {
        struct modeNode *mn = GetUserMode(hs->helpchan, req->user);
        helpserv_msguser(req->user, "HSMSG_REQ_UNASSIGNED", req->id, reason);
        if (hs->auto_devoice && mn && (mn->modes & MODE_VOICE)) {
            struct mod_chanmode change;
            mod_chanmode_init(&change);
            change.argc = 1;
            change.args[0].mode = MODE_REMOVE | MODE_VOICE;
            change.args[0].member = mn;
            mod_chanmode_announce(hs->helpserv, hs->helpchan, &change);
        }
        if(req->handle)
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_HELPER_GONE_1", req->id, req->user->nick, req->handle->handle, req->helper->handle->handle, reason);
        else
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_HELPER_GONE_2", req->id, req->user->nick, req->helper->handle->handle, reason);
    } else {
        if(req->handle)
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_HELPER_GONE_3", req->id, req->handle->handle, req->helper->handle->handle, reason);
        else
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_HELPER_GONE_2", req->id, req->helper->handle->handle, reason);
    }

    /* Now put it back in the queue */
    if (hs->unhandled == NULL) {
        /* Nothing there, put it at the front */
        hs->unhandled = req;
        req->next_unhandled = NULL;
    } else {
        /* Should it be at the front? */
        if (hs->unhandled->opened >= req->opened) {
            req->next_unhandled = hs->unhandled;
            hs->unhandled = req;
        } else {
            struct helpserv_request *unhandled;
            /* Find the request that this should be inserted AFTER */
            for (unhandled=hs->unhandled; unhandled->next_unhandled && (unhandled->next_unhandled->opened < req->opened); unhandled = unhandled->next_unhandled);
            req->next_unhandled = unhandled->next_unhandled;
            unhandled->next_unhandled = req;
        }
    }

    req->helper = NULL;
}

/* This takes care of WHINE_DELAY and IDLE_DELAY */
static void run_whine_interval(void *data) {
    struct helpserv_bot *hs=data;
    struct helpfile_table tbl;
    unsigned int i;

    /* First, run the WHINE_DELAY */
    if (hs->intervals[INTERVAL_WHINE_DELAY]
        && (hs->page_types[PGSRC_ALERT] != PAGE_NONE)
        && (hs->page_targets[PGSRC_ALERT] != NULL)
        && (!hs->intervals[INTERVAL_EMPTY_INTERVAL] || !hs->helpchan_empty)) {
        struct helpserv_request *unh;
        struct helpserv_reqlist reqlist;
        unsigned int queuesize=0;

        helpserv_reqlist_init(&reqlist);

        for (unh = hs->unhandled; unh; unh = unh->next_unhandled) {
            queuesize++;
            if ((now - unh->opened) >= (time_t)hs->intervals[INTERVAL_WHINE_DELAY]) {
                helpserv_reqlist_append(&reqlist, unh);
            }
        }

        if (reqlist.used) {
            char strwhinedelay[INTERVALLEN];

            intervalString(strwhinedelay, (time_t)hs->intervals[INTERVAL_WHINE_DELAY], NULL);
#if ANNOYING_ALERT_PAGES
            tbl.length = reqlist.used + 1;
            tbl.width = 4;
            tbl.flags = TABLE_NO_FREE;
            tbl.contents = alloca(tbl.length * sizeof(*tbl.contents));
            tbl.contents[0] = alloca(tbl.width * sizeof(**tbl.contents));
            tbl.contents[0][0] = "ID#";
            tbl.contents[0][1] = "Nick";
            tbl.contents[0][2] = "Account";
            tbl.contents[0][3] = "Waiting time";

            for (i=1; i <= reqlist.used; i++) {
                char reqid[12], unh_time[INTERVALLEN];
                unh = reqlist.list[i-1];

                tbl.contents[i] = alloca(tbl.width * sizeof(**tbl.contents));
                sprintf(reqid, "%lu", unh->id);
                tbl.contents[i][0] = strdup(reqid);
                tbl.contents[i][1] = unh->user ? unh->user->nick : "Not online";
                tbl.contents[i][2] = unh->handle ? unh->handle->handle : "Not authed";
                intervalString(unh_time, now - unh->opened, NULL);
                tbl.contents[i][3] = strdup(unh_time);
            }

            helpserv_page(PGSRC_ALERT, "HSMSG_PAGE_WHINE_HEADER", reqlist.used, strwhinedelay, queuesize);
            table_send(hs->helpserv, hs->page_targets[PGSRC_ALERT]->name, 0, page_type_funcs[hs->page_types[PGSRC_ALERT]], tbl);

            for (i=1; i <= reqlist.used; i++) {
                free((char *)tbl.contents[i][0]);
                free((char *)tbl.contents[i][3]);
            }
#else
            helpserv_page(PGSRC_ALERT, "HSMSG_PAGE_WHINE_HEADER", reqlist.used, strwhinedelay, queuesize);
#endif
        }

        helpserv_reqlist_clean(&reqlist);
    }

    /* Next run IDLE_DELAY */
    if (hs->intervals[INTERVAL_IDLE_DELAY]
        && (hs->page_types[PGSRC_STATUS] != PAGE_NONE)
        && (hs->page_targets[PGSRC_STATUS] != NULL)) {
        struct modeList mode_list;

        modeList_init(&mode_list);

        for (i=0; i < hs->helpchan->members.used; i++) {
            struct modeNode *mn = hs->helpchan->members.list[i];
            /* Ignore ops. Perhaps this should be a set option? */
            if (mn->modes & MODE_CHANOP)
                continue;
            /* Check if they've been idle long enough */
            if ((unsigned)(now - mn->idle_since) < hs->intervals[INTERVAL_IDLE_DELAY])
                continue;
            /* Add them to the list of idle people.. */
            modeList_append(&mode_list, mn);
        }

        if (mode_list.used) {
            char stridledelay[INTERVALLEN];

            tbl.length = mode_list.used + 1;
            tbl.width = 4;
            tbl.flags = TABLE_NO_FREE;
            tbl.contents = alloca(tbl.length * sizeof(*tbl.contents));
            tbl.contents[0] = alloca(tbl.width * sizeof(**tbl.contents));
            tbl.contents[0][0] = "Nick";
            tbl.contents[0][1] = "Account";
            tbl.contents[0][2] = "ID#";
            tbl.contents[0][3] = "Idle time";

            for (i=1; i <= mode_list.used; i++) {
                char reqid[12], idle_time[INTERVALLEN];
                struct helpserv_reqlist *reqlist;
                struct modeNode *mn = mode_list.list[i-1];

                tbl.contents[i] = alloca(tbl.width * sizeof(**tbl.contents));
                tbl.contents[i][0] = mn->user->nick;
                tbl.contents[i][1] = mn->user->handle_info ? mn->user->handle_info->handle : "Not authed";

                if ((reqlist = dict_find(helpserv_reqs_bynick_dict, mn->user->nick, NULL))) {
                    int j;

                    for (j = reqlist->used-1; j >= 0; j--) {
                        struct helpserv_request *req = reqlist->list[j];

                        if (req->hs == hs) {
                            sprintf(reqid, "%lu", req->id);
                            break;
                        }
                    }

                    if (j < 0)
                        strcpy(reqid, "None");
                } else {
                    strcpy(reqid, "None");
                }
                tbl.contents[i][2] = strdup(reqid);

                intervalString(idle_time, now - mn->idle_since, NULL);
                tbl.contents[i][3] = strdup(idle_time);
            }

            intervalString(stridledelay, (time_t)hs->intervals[INTERVAL_IDLE_DELAY], NULL);
            helpserv_page(PGSRC_STATUS, "HSMSG_PAGE_IDLE_HEADER", mode_list.used, hs->helpchan->name, stridledelay);
            table_send(hs->helpserv, hs->page_targets[PGSRC_STATUS]->name, 0, page_types[hs->page_types[PGSRC_STATUS]].func, tbl);

            for (i=1; i <= mode_list.used; i++) {
                free((char *)tbl.contents[i][2]);
                free((char *)tbl.contents[i][3]);
            }
        }

        modeList_clean(&mode_list);
    }

    if (hs->intervals[INTERVAL_WHINE_INTERVAL]) {
        timeq_add(now + hs->intervals[INTERVAL_WHINE_INTERVAL], run_whine_interval, hs);
    }
}

/* Returns -1 if there's any helpers,
 * 0 if there are no helpers
 * >1 if there are trials (number of trials)
 */
static int find_helpchan_helpers(struct helpserv_bot *hs) {
    int num_trials=0;
    dict_iterator_t it;

    for (it=dict_first(hs->users); it; it=iter_next(it)) {
        struct helpserv_user *hs_user=iter_data(it);

        if (find_handle_in_channel(hs->helpchan, hs_user->handle, NULL)) {
            if (hs_user->level >= HlHelper) {
                hs->helpchan_empty = 0;
                return -1;
            }
            num_trials++;
        }
    }

    hs->helpchan_empty = 1;
    return num_trials;
}


static void run_empty_interval(void *data) {
    struct helpserv_bot *hs=data;
    int num_trials=find_helpchan_helpers(hs);
    unsigned int num_unh;
    struct helpserv_request *unh;

    if (num_trials == -1)
        return;
    if (hs->req_on_join && !hs->unhandled)
        return;

    for (num_unh=0, unh=hs->unhandled; unh; num_unh++)
        unh = unh->next_unhandled;

    if (num_trials)
        helpserv_page(PGSRC_ALERT, "HSMSG_PAGE_ONLYTRIALALERT", hs->helpchan->name, num_trials, num_unh);
    else
        helpserv_page(PGSRC_ALERT, "HSMSG_PAGE_EMPTYALERT", hs->helpchan->name, num_unh);

    if (hs->intervals[INTERVAL_EMPTY_INTERVAL])
        timeq_add(now + hs->intervals[INTERVAL_EMPTY_INTERVAL], run_empty_interval, hs);
}

static void free_user(void *data) {
    struct helpserv_user *hs_user = data;
    struct helpserv_bot *hs = hs_user->hs;
    struct helpserv_userlist *userlist;
    dict_iterator_t it;

    if (hs->requests) {
        for (it=dict_first(hs->requests); it; it=iter_next(it)) {
            struct helpserv_request *req = iter_data(it);

            if (req->helper == hs_user)
                helpserv_page_helper_gone(hs, req, "been deleted");
        }
    }

    userlist = dict_find(helpserv_users_byhand_dict, hs_user->handle->handle, NULL);
    if (userlist->used == 1) {
        dict_remove(helpserv_users_byhand_dict, hs_user->handle->handle);
    } else {
        helpserv_userlist_remove(userlist, hs_user);
    }

    free(data);
}

static struct helpserv_bot *register_helpserv(const char *nick, const char *help_channel, const char *registrar) {
    struct helpserv_bot *hs;
    struct helpserv_botlist *botlist;

    /* Laziness dictates calloc, since there's a lot to set to NULL or 0, and
     * it's a harmless default */
    hs = calloc(1, sizeof(struct helpserv_bot));

    if (!(hs->helpserv = AddService(nick, "+iok", helpserv_conf.description, NULL))) {
        free(hs);
        return NULL;
    }

    reg_privmsg_func(hs->helpserv, helpserv_botmsg);

    if (!(hs->helpchan = GetChannel(help_channel))) {
        hs->helpchan = AddChannel(help_channel, now, NULL, NULL);
        AddChannelUser(hs->helpserv, hs->helpchan)->modes |= MODE_CHANOP;
    } else {
        struct mod_chanmode change;
        mod_chanmode_init(&change);
        change.argc = 1;
        change.args[0].mode = MODE_CHANOP;
        change.args[0].member = AddChannelUser(hs->helpserv, hs->helpchan);
        mod_chanmode_announce(hs->helpserv, hs->helpchan, &change);
    }

    if (registrar)
        hs->registrar = strdup(registrar);

    hs->users = dict_new();
    /* Don't free keys - they use the handle_info's handle field */
    dict_set_free_data(hs->users, free_user);
    hs->requests = dict_new();
    dict_set_free_keys(hs->requests, free);
    dict_set_free_data(hs->requests, free_request);

    dict_insert(helpserv_bots_dict, hs->helpserv->nick, hs);

    if (!(botlist = dict_find(helpserv_bots_bychan_dict, hs->helpchan->name, NULL))) {
        botlist = helpserv_botlist_alloc();
        dict_insert(helpserv_bots_bychan_dict, hs->helpchan->name, botlist);
    }
    helpserv_botlist_append(botlist, hs);

    return hs;
}

static HELPSERV_FUNC(cmd_register) {
    char *nick, *helpchan, reason[MAXLEN];
    struct handle_info *handle;

    REQUIRE_PARMS(4);
    nick = argv[1];
    if (!is_valid_nick(nick)) {
        helpserv_notice(user, "HSMSG_ILLEGAL_NICK", nick);
        return 0;
    }
    if (GetUserH(nick)) {
        helpserv_notice(user, "HSMSG_NICK_EXISTS", nick);
        return 0;
    }
    helpchan = argv[2];
    if (!IsChannelName(helpchan)) {
        helpserv_notice(user, "HSMSG_ILLEGAL_CHANNEL", helpchan);
        HELPSERV_SYNTAX();
        return 0;
    }
    if (opserv_bad_channel(helpchan)) {
        helpserv_notice(user, "HSMSG_ILLEGAL_CHANNEL", helpchan);
        return 0;
    }
    if (!(handle = helpserv_get_handle_info(user, argv[3])))
        return 0;

    if (!(hs = register_helpserv(nick, helpchan, user->handle_info->handle))) {
        helpserv_notice(user, "HSMSG_ERROR_ADDING_SERVICE", nick);
        return 0;
    }

    hs->registered = now;
    helpserv_add_user(hs, handle, HlOwner);

    helpserv_notice(user, "HSMSG_REG_SUCCESS", handle->handle, nick);

    snprintf(reason, MAXLEN, "HelpServ %s (%s) registered to %s by %s.", nick, hs->helpchan->name, handle->handle, user->nick);
    /* Not sent to helpers, since they can't register HelpServ */
    global_message(MESSAGE_RECIPIENT_OPERS, reason);
    return 1;
}

static void unregister_helpserv(struct helpserv_bot *hs) {
    enum message_type msgtype;

    timeq_del(0, NULL, hs, TIMEQ_IGNORE_WHEN|TIMEQ_IGNORE_FUNC);

    /* Requests before users so that it doesn't spam mentioning now-unhandled
     * requests because the users were deleted */
    dict_delete(hs->requests);
    hs->requests = NULL; /* so we don't try to look up requests in free_user() */
    dict_delete(hs->users);
    free(hs->registrar);

    for (msgtype=0; msgtype<MSGTYPE_COUNT; msgtype++)
        free(hs->messages[msgtype]);
}

static void helpserv_free_bot(void *data) {
    unregister_helpserv(data);
    free(data);
}

static void helpserv_unregister(struct helpserv_bot *bot, const char *quit_fmt, const char *global_fmt, const char *actor) {
    char reason[MAXLEN], channame[CHANNELLEN], botname[NICKLEN];
    struct helpserv_botlist *botlist;
    size_t len;

    botlist = dict_find(helpserv_bots_bychan_dict, bot->helpchan->name, NULL);
    helpserv_botlist_remove(botlist, bot);
    if (!botlist->used)
        dict_remove(helpserv_bots_bychan_dict, bot->helpchan->name);
    len = strlen(bot->helpserv->nick) + 1;
    safestrncpy(botname, bot->helpserv->nick, len);
    len = strlen(bot->helpchan->name) + 1;
    safestrncpy(channame, bot->helpchan->name, len);
    snprintf(reason, sizeof(reason), quit_fmt, actor);
    DelUser(bot->helpserv, NULL, 1, reason);
    dict_remove(helpserv_bots_dict, botname);
    snprintf(reason, sizeof(reason), global_fmt, botname, channame, actor);
    global_message(MESSAGE_RECIPIENT_OPERS, reason);
}

static HELPSERV_FUNC(cmd_unregister) {
    if (!from_opserv) {
        if (argc < 2 || strcmp(argv[1], "CONFIRM")) {
            helpserv_notice(user, "HSMSG_NEED_UNREG_CONFIRM");
            return 0;
        }
        log_audit(HS_LOG, LOG_COMMAND, user, hs->helpserv, hs->helpchan->name, 0, "unregister CONFIRM");
    }

    helpserv_unregister(hs, "Unregistered by %s", "HelpServ %s (%s) unregistered by %s.", user->nick);
    return from_opserv;
}

static HELPSERV_FUNC(cmd_expire) {
    struct helpserv_botlist victims;
    struct helpserv_bot *bot;
    dict_iterator_t it, next;
    unsigned int count = 0;

    memset(&victims, 0, sizeof(victims));
    for (it = dict_first(helpserv_bots_dict); it; it = next) {
        bot = iter_data(it);
        next = iter_next(it);
        if ((unsigned int)(now - bot->last_active) < helpserv_conf.expire_age)
            continue;
        helpserv_unregister(bot, "Registration expired due to inactivity", "HelpServ %s (%s) expired at request of %s.", user->nick);
        count++;
    }
    helpserv_notice(user, "HSMSG_EXPIRATION_DONE", count);
    return 1;
}

static HELPSERV_FUNC(cmd_giveownership) {
    struct handle_info *hi;
    struct helpserv_user *new_owner, *old_owner, *hs_user;
    dict_iterator_t it;
    char reason[MAXLEN];

    if (!from_opserv && ((argc < 3) || strcmp(argv[2], "CONFIRM"))) {
        helpserv_notice(user, "HSMSG_NEED_GIVEOWNERSHIP_CONFIRM");
        return 0;
    }
    hi = helpserv_get_handle_info(user, argv[1]);
    if (!hi)
        return 0;
    new_owner = GetHSUser(hs, hi);
    if (!new_owner) {
        helpserv_notice(user, "HSMSG_NOT_IN_USERLIST", hi->handle, hs->helpserv->nick);
        return 0;
    }
    if (!from_opserv)
        old_owner = GetHSUser(hs, user->handle_info);
    else for (it = dict_first(hs->users), old_owner = NULL; it; it = iter_next(it)) {
        hs_user = iter_data(it);
        if (hs_user->level != HlOwner)
            continue;
        if (old_owner) {
            helpserv_notice(user, "HSMSG_MULTIPLE_OWNERS", hs->helpserv->nick);
            return 0;
        }
        old_owner = hs_user;
    }
    if (!from_opserv && (new_owner->handle == user->handle_info)) {
        helpserv_notice(user, "HSMSG_NO_TRANSFER_SELF");
        return 0;
    }
    if (old_owner)
        old_owner->level = HlManager;
    new_owner->level = HlOwner;
    helpserv_notice(user, "HSMSG_OWNERSHIP_GIVEN", hs->helpserv->nick, new_owner->handle->handle);
    sprintf(reason, "%s (%s) ownership transferred to %s by %s.", hs->helpserv->nick, hs->helpchan->name, new_owner->handle->handle, user->handle_info->handle);
    return 1;
}

static HELPSERV_FUNC(cmd_weekstart) {
    struct handle_info *hi;
    struct helpserv_user *actor, *victim;
    int changed = 0;

    REQUIRE_PARMS(2);
    actor = from_opserv ? NULL : GetHSUser(hs, user->handle_info);
    if (!(hi = helpserv_get_handle_info(user, argv[1])))
        return 0;
    if (!(victim = GetHSUser(hs, hi))) {
        helpserv_notice(user, "HSMSG_NOT_IN_USERLIST", hi->handle, hs->helpserv->nick);
        return 0;
    }
    if (actor && (actor->level <= victim->level) && (actor != victim)) {
        helpserv_notice(user, "MSG_USER_OUTRANKED", victim->handle->handle);
        return 0;
    }
    if (argc > 2 && (!actor || actor->level >= HlManager)) {
        int new_day = 7;
        switch (argv[2][0]) {
        case 's': case 'S':
            if ((argv[2][1] == 'u') || (argv[2][1] == 'U'))
                new_day = 0;
            else if ((argv[2][1] == 'a') || (argv[2][1] == 'A'))
                new_day = 6;
            break;
        case 'm': case 'M': new_day = 1; break;
        case 't': case 'T':
            if ((argv[2][1] == 'u') || (argv[2][1] == 'U'))
                new_day = 2;
            else if ((argv[2][1] == 'h') || (argv[2][1] == 'H'))
                new_day = 4;
            break;
        case 'w': case 'W': new_day = 3; break;
        case 'f': case 'F': new_day = 5; break;
        }
        if (new_day == 7) {
            helpserv_notice(user, "HSMSG_BAD_WEEKDAY", argv[2]);
            return 0;
        }
        victim->week_start = new_day;
        changed = 1;
    }
    helpserv_notice(user, "HSMSG_WEEK_STARTS", victim->handle->handle, weekday_names[victim->week_start]);
    return changed;
}

static void set_page_target(struct helpserv_bot *hs, enum page_source idx, const char *target) {
    struct chanNode *new_target, *old_target;

    if (target) {
        if (!IsChannelName(target)) {
            log_module(HS_LOG, LOG_ERROR, "%s has an invalid page target.", hs->helpserv->nick);
            return;
        }
        new_target = GetChannel(target);
        if (!new_target) {
            new_target = AddChannel(target, now, NULL, NULL);
            AddChannelUser(hs->helpserv, new_target);
        }
    } else {
        new_target = NULL;
    }
    if (new_target == hs->page_targets[idx])
        return;
    old_target = hs->page_targets[idx];
    hs->page_targets[idx] = NULL;
    if (old_target && !helpserv_in_channel(hs, old_target))
        DelChannelUser(hs->helpserv, old_target, "Changing page target.", 0);
    if (new_target && !helpserv_in_channel(hs, new_target)) {
        struct mod_chanmode change;
        mod_chanmode_init(&change);
        change.argc = 1;
        change.args[0].mode = MODE_CHANOP;
        change.args[0].member = AddChannelUser(hs->helpserv, new_target);
        mod_chanmode_announce(hs->helpserv, new_target, &change);
    }
    hs->page_targets[idx] = new_target;
}

static int opt_page_target(struct userNode *user, struct helpserv_bot *hs, int from_opserv, int argc, char *argv[], enum page_source idx) {
    int changed = 0;

    if (argc > 0) {
        if (!IsOper(user)) {
            helpserv_notice(user, "HSMSG_SET_NEED_OPER");
            return 0;
        }
        if (!strcmp(argv[0], "*")) {
            set_page_target(hs, idx, NULL);
            changed = 1;
        } else if (!IsChannelName(argv[0])) {
            helpserv_notice(user, "MSG_NOT_CHANNEL_NAME");
            return 0;
        } else {
            set_page_target(hs, idx, argv[0]);
            changed = 1;
        }
    }
    if (hs->page_targets[idx])
        helpserv_notice(user, page_sources[idx].print_target, hs->page_targets[idx]->name);
    else
        helpserv_notice(user, page_sources[idx].print_target, user_find_message(user, "MSG_NONE"));
    return changed;
}

static HELPSERV_OPTION(opt_pagetarget_command) {
    return opt_page_target(user, hs, from_opserv, argc, argv, PGSRC_COMMAND);
}

static HELPSERV_OPTION(opt_pagetarget_alert) {
    return opt_page_target(user, hs, from_opserv, argc, argv, PGSRC_ALERT);
}

static HELPSERV_OPTION(opt_pagetarget_status) {
    return opt_page_target(user, hs, from_opserv, argc, argv, PGSRC_STATUS);
}

static enum page_type page_type_from_name(const char *name) {
    enum page_type type;
    for (type=0; type<PAGE_COUNT; type++)
        if (!irccasecmp(page_types[type].db_name, name))
            return type;
    return PAGE_COUNT;
}

static int opt_page_type(struct userNode *user, struct helpserv_bot *hs, int from_opserv, int argc, char *argv[], enum page_source idx) {
    enum page_type new_type;
    int changed=0;

    if (argc > 0) {
        new_type = page_type_from_name(argv[0]);
        if (new_type == PAGE_COUNT) {
            helpserv_notice(user, "HSMSG_INVALID_OPTION", argv[0]);
            return 0;
        }
        hs->page_types[idx] = new_type;
        changed = 1;
    }
    helpserv_notice(user, page_sources[idx].print_type,
                    user_find_message(user, page_types[hs->page_types[idx]].print_name));
    return changed;
}

static HELPSERV_OPTION(opt_pagetype) {
    return opt_page_type(user, hs, from_opserv, argc, argv, PGSRC_COMMAND);
}

static HELPSERV_OPTION(opt_alert_page_type) {
    return opt_page_type(user, hs, from_opserv, argc, argv, PGSRC_ALERT);
}

static HELPSERV_OPTION(opt_status_page_type) {
    return opt_page_type(user, hs, from_opserv, argc, argv, PGSRC_STATUS);
}

static int opt_message(struct userNode *user, struct helpserv_bot *hs, int from_opserv, int argc, char *argv[], enum message_type idx) {
    int changed=0;

    if (argc > 0) {
        char *msg = unsplit_string(argv, argc, NULL);
        free(hs->messages[idx]);
        hs->messages[idx] = strcmp(msg, "*") ? strdup(msg) : NULL;
        changed = 1;
    }
    if (hs->messages[idx])
        helpserv_notice(user, message_types[idx].print_name, hs->messages[idx]);
    else
        helpserv_notice(user, message_types[idx].print_name, user_find_message(user, "MSG_NONE"));
    return changed;
}

static HELPSERV_OPTION(opt_greeting) {
    return opt_message(user, hs, from_opserv, argc, argv, MSGTYPE_GREETING);
}

static HELPSERV_OPTION(opt_req_opened) {
    return opt_message(user, hs, from_opserv, argc, argv, MSGTYPE_REQ_OPENED);
}

static HELPSERV_OPTION(opt_req_assigned) {
    return opt_message(user, hs, from_opserv, argc, argv, MSGTYPE_REQ_ASSIGNED);
}

static HELPSERV_OPTION(opt_req_closed) {
    return opt_message(user, hs, from_opserv, argc, argv, MSGTYPE_REQ_CLOSED);
}

static int opt_interval(struct userNode *user, struct helpserv_bot *hs, int from_opserv, int argc, char *argv[], enum interval_type idx, unsigned int min) {
    char buf[INTERVALLEN];
    int changed=0;

    if (argc > 0) {
        unsigned long new_int = ParseInterval(argv[0]);
        if (!new_int && strcmp(argv[0], "0")) {
            helpserv_notice(user, "MSG_INVALID_DURATION", argv[0]);
            return 0;
        }
        if (new_int && new_int < min) {
            intervalString(buf, min, user->handle_info);
            helpserv_notice(user, "HSMSG_INVALID_INTERVAL", user_find_message(user, interval_types[idx].print_name), buf);
            return 0;
        }
        hs->intervals[idx] = new_int;
        changed = 1;
    }
    if (hs->intervals[idx]) {
        intervalString(buf, hs->intervals[idx], user->handle_info);
        helpserv_notice(user, interval_types[idx].print_name, buf);
    } else
        helpserv_notice(user, interval_types[idx].print_name, user_find_message(user, "HSMSG_0_DISABLED"));
    return changed;
}

static HELPSERV_OPTION(opt_idle_delay) {
    return opt_interval(user, hs, from_opserv, argc, argv, INTERVAL_IDLE_DELAY, 60);
}

static HELPSERV_OPTION(opt_whine_delay) {
    return opt_interval(user, hs, from_opserv, argc, argv, INTERVAL_WHINE_DELAY, 60);
}

static HELPSERV_OPTION(opt_whine_interval) {
    unsigned int old_val = hs->intervals[INTERVAL_WHINE_INTERVAL];
    int retval;

    retval = opt_interval(user, hs, from_opserv, argc, argv, INTERVAL_WHINE_INTERVAL, 60);

    if (!old_val && hs->intervals[INTERVAL_WHINE_INTERVAL]) {
        timeq_add(now + hs->intervals[INTERVAL_WHINE_INTERVAL], run_whine_interval, hs);
    } else if (old_val && !hs->intervals[INTERVAL_WHINE_INTERVAL]) {
        timeq_del(0, run_whine_interval, hs, TIMEQ_IGNORE_WHEN);
    }

    return retval;
}

static HELPSERV_OPTION(opt_empty_interval) {
    unsigned int old_val = hs->intervals[INTERVAL_EMPTY_INTERVAL];
    int retval;

    retval = opt_interval(user, hs, from_opserv, argc, argv, INTERVAL_EMPTY_INTERVAL, 60);

    if (!old_val && hs->intervals[INTERVAL_EMPTY_INTERVAL]) {
        timeq_add(now + hs->intervals[INTERVAL_EMPTY_INTERVAL], run_empty_interval, hs);
    } else if (old_val && !hs->intervals[INTERVAL_EMPTY_INTERVAL]) {
        timeq_del(0, run_empty_interval, hs, TIMEQ_IGNORE_WHEN);
    }

    return retval;
}

static HELPSERV_OPTION(opt_stale_delay) {
    return opt_interval(user, hs, from_opserv, argc, argv, INTERVAL_STALE_DELAY, 60);
}

static enum persistence_length persistence_from_name(const char *name) {
    enum persistence_length pers;
    for (pers=0; pers<PERSIST_COUNT; pers++)
        if (!irccasecmp(name, persistence_lengths[pers].db_name))
            return pers;
    return PERSIST_COUNT;
}

static int opt_persist(struct userNode *user, struct helpserv_bot *hs, int from_opserv, int argc, char *argv[], enum persistence_type idx) {
    int changed=0;

    if (argc > 0) {
        enum persistence_length new_pers = persistence_from_name(argv[0]);
        if (new_pers == PERSIST_COUNT) {
            helpserv_notice(user, "HSMSG_INVALID_OPTION", argv[0]);
            return 0;
        }
        hs->persist_types[idx] = new_pers;
        changed = 1;
    }
    helpserv_notice(user, persistence_types[idx].print_name,
                    user_find_message(user, persistence_lengths[hs->persist_types[idx]].print_name));
    return changed;
}

static HELPSERV_OPTION(opt_request_persistence) {
    return opt_persist(user, hs, from_opserv, argc, argv, PERSIST_T_REQUEST);
}

static HELPSERV_OPTION(opt_helper_persistence) {
    return opt_persist(user, hs, from_opserv, argc, argv, PERSIST_T_HELPER);
}

static enum notification_type notification_from_name(const char *name) {
    enum notification_type notify;
    for (notify=0; notify<NOTIFY_COUNT; notify++)
        if (!irccasecmp(name, notification_types[notify].db_name))
            return notify;
    return NOTIFY_COUNT;
}

static HELPSERV_OPTION(opt_notification) {
    int changed=0;

    if (argc > 0) {
        enum notification_type new_notify = notification_from_name(argv[0]);
        if (new_notify == NOTIFY_COUNT) {
            helpserv_notice(user, "HSMSG_INVALID_OPTION", argv[0]);
            return 0;
        }
        if (!from_opserv && (new_notify == NOTIFY_HANDLE)) {
            helpserv_notice(user, "HSMSG_SET_NEED_OPER");
            return 0;
        }
        hs->notify = new_notify;
        changed = 1;
    }
    helpserv_notice(user, "HSMSG_SET_NOTIFICATION", user_find_message(user, notification_types[hs->notify].print_name));
    return changed;
}

#define OPTION_UINT(var, name) do { \
    int changed=0; \
    if (argc > 0) { \
        (var) = strtoul(argv[0], NULL, 0); \
        changed = 1; \
    } \
    helpserv_notice(user, name, (var)); \
    return changed; \
} while (0);

static HELPSERV_OPTION(opt_id_wrap) {
    OPTION_UINT(hs->id_wrap, "HSMSG_SET_IDWRAP");
}

static HELPSERV_OPTION(opt_req_maxlen) {
    OPTION_UINT(hs->req_maxlen, "HSMSG_SET_REQMAXLEN");
}

#define OPTION_BINARY(var, name) do { \
    int changed=0; \
    if (argc > 0) { \
        if (enabled_string(argv[0])) { \
            (var) = 1; \
            changed = 1; \
        } else if (disabled_string(argv[0])) { \
            (var) = 0; \
            changed = 1; \
        } else { \
            helpserv_notice(user, "MSG_INVALID_BINARY", argv[0]); \
            return 0; \
        } \
    } \
    helpserv_notice(user, name, user_find_message(user, (var) ? "MSG_ON" : "MSG_OFF")); \
    return changed; \
} while (0);

static HELPSERV_OPTION(opt_privmsg_only) {
    OPTION_BINARY(hs->privmsg_only, "HSMSG_SET_PRIVMSGONLY");
}

static HELPSERV_OPTION(opt_req_on_join) {
    OPTION_BINARY(hs->req_on_join, "HSMSG_SET_REQONJOIN");
}

static HELPSERV_OPTION(opt_auto_voice) {
    OPTION_BINARY(hs->auto_voice, "HSMSG_SET_AUTOVOICE");
}

static HELPSERV_OPTION(opt_auto_devoice) {
    OPTION_BINARY(hs->auto_devoice, "HSMSG_SET_AUTODEVOICE");
}

static HELPSERV_FUNC(cmd_set) {
    helpserv_option_func_t *opt;

    if (argc < 2) {
        unsigned int i;
        helpserv_option_func_t *display[] = {
            opt_pagetarget_command, opt_pagetarget_alert, opt_pagetarget_status,
            opt_pagetype, opt_alert_page_type, opt_status_page_type,
            opt_greeting, opt_req_opened, opt_req_assigned, opt_req_closed,
            opt_idle_delay, opt_whine_delay, opt_whine_interval,
            opt_empty_interval, opt_stale_delay, opt_request_persistence,
            opt_helper_persistence, opt_notification, opt_id_wrap,
            opt_req_maxlen, opt_privmsg_only, opt_req_on_join, opt_auto_voice,
            opt_auto_devoice
        };

        helpserv_notice(user, "HSMSG_QUEUE_OPTIONS");
        for (i=0; i<ArrayLength(display); i++)
            display[i](user, hs, from_opserv, 0, argv);
        return 1;
    }

    if (!(opt = dict_find(helpserv_option_dict, argv[1], NULL))) {
        helpserv_notice(user, "HSMSG_INVALID_OPTION", argv[1]);
        return 0;
    }

    if ((argc > 2) && !from_opserv) {
        struct helpserv_user *hs_user;

        if (!(hs_user = dict_find(hs->users, user->handle_info->handle, NULL))) {
            helpserv_notice(user, "HSMSG_WTF_WHO_ARE_YOU", hs->helpserv->nick);
            return 0;
        }

        if (hs_user->level < HlManager) {
            helpserv_notice(user, "HSMSG_NEED_MANAGER");
            return 0;
        }
    }
    return opt(user, hs, from_opserv, argc-2, argv+2);
}

static int user_write_helper(const char *key, void *data, void *extra) {
    struct helpserv_user *hs_user = data;
    struct saxdb_context *ctx = extra;
    struct string_list strlist;
    char str[5][16], *strs[5];
    unsigned int i;

    saxdb_start_record(ctx, key, 0);
    /* Helper identification. */
    saxdb_write_string(ctx, KEY_HELPER_LEVEL, helpserv_level2str(hs_user->level));
    saxdb_write_string(ctx, KEY_HELPER_HELPMODE, (hs_user->help_mode ? "1" : "0"));
    saxdb_write_int(ctx, KEY_HELPER_WEEKSTART, hs_user->week_start);
    /* Helper stats */
    saxdb_start_record(ctx, KEY_HELPER_STATS, 0);
    for (i=0; i < ArrayLength(strs); ++i)
        strs[i] = str[i];
    strlist.list = strs;
    strlist.used = 5;
    /* Time in help channel */
    for (i=0; i < strlist.used; i++) {
        unsigned int week_time = hs_user->time_per_week[i];
        if ((i==0 || i==4) && hs_user->join_time)
            week_time += now - hs_user->join_time;
        sprintf(str[i], "%u", week_time);
    }
    saxdb_write_string_list(ctx, KEY_HELPER_STATS_TIME, &strlist);
    /* Requests picked up */
    for (i=0; i < strlist.used; i++)
        sprintf(str[i], "%u", hs_user->picked_up[i]);
    saxdb_write_string_list(ctx, KEY_HELPER_STATS_PICKUP, &strlist);
    /* Requests closed */
    for (i=0; i < strlist.used; i++)
        sprintf(str[i], "%u", hs_user->closed[i]);
    saxdb_write_string_list(ctx, KEY_HELPER_STATS_CLOSE, &strlist);
    /* Requests reassigned from user */
    for (i=0; i < strlist.used; i++)
        sprintf(str[i], "%u", hs_user->reassigned_from[i]);
    saxdb_write_string_list(ctx, KEY_HELPER_STATS_REASSIGNFROM, &strlist);
    /* Requests reassigned to user */
    for (i=0; i < strlist.used; i++)
        sprintf(str[i], "%u", hs_user->reassigned_to[i]);
    saxdb_write_string_list(ctx, KEY_HELPER_STATS_REASSIGNTO, &strlist);
    /* End of stats and whole record. */
    saxdb_end_record(ctx);
    saxdb_end_record(ctx);
    return 0;
}

static int user_read_helper(const char *key, void *data, void *extra) {
    struct record_data *rd = data;
    struct helpserv_bot *hs = extra;
    struct helpserv_user *hs_user;
    struct handle_info *handle;
    dict_t stats;
    enum helpserv_level level;
    char *str;
    struct string_list *strlist;
    unsigned int i;

    if (rd->type != RECDB_OBJECT || !dict_size(rd->d.object)) {
        log_module(HS_LOG, LOG_ERROR, "Invalid user %s for %s.", key, hs->helpserv->nick);
        return 0;
    }

    if (!(handle = get_handle_info(key))) {
        log_module(HS_LOG, LOG_ERROR, "Nonexistant account %s for %s.", key, hs->helpserv->nick);
        return 0;
    }
    str = database_get_data(rd->d.object, KEY_HELPER_LEVEL, RECDB_QSTRING);
    if (str) {
        level = helpserv_str2level(str);
        if (level == HlNone) {
            log_module(HS_LOG, LOG_ERROR, "Account %s has invalid level %s.", key, str);
            return 0;
        }
    } else {
        log_module(HS_LOG, LOG_ERROR, "Account %s has no level field for %s.", key, hs->helpserv->nick);
        return 0;
    }

    hs_user = helpserv_add_user(hs, handle, level);

    str = database_get_data(rd->d.object, KEY_HELPER_HELPMODE, RECDB_QSTRING);
    hs_user->help_mode = (str && strtol(str, NULL, 0)) ? 1 : 0;
    str = database_get_data(rd->d.object, KEY_HELPER_WEEKSTART, RECDB_QSTRING);
    hs_user->week_start = str ? strtol(str, NULL, 0) : 0;

    /* Stats */
    stats = database_get_data(GET_RECORD_OBJECT(rd), KEY_HELPER_STATS, RECDB_OBJECT);

    if (stats) {
        /* The tests for strlist->used are for converting the old format to the new one */
        strlist = database_get_data(stats, KEY_HELPER_STATS_TIME, RECDB_STRING_LIST);
        if (strlist) {
            for (i=0; i < 5 && i < strlist->used; i++)
                hs_user->time_per_week[i] = strtoul(strlist->list[i], NULL, 0);
            if (strlist->used == 4)
                hs_user->time_per_week[4] = hs_user->time_per_week[0]+hs_user->time_per_week[1]+hs_user->time_per_week[2]+hs_user->time_per_week[3];
        }
        strlist = database_get_data(stats, KEY_HELPER_STATS_PICKUP, RECDB_STRING_LIST);
        if (strlist) {
            for (i=0; i < 5 && i < strlist->used; i++)
                hs_user->picked_up[i] = strtoul(strlist->list[i], NULL, 0);
            if (strlist->used == 2)
                hs_user->picked_up[4] = hs_user->picked_up[0]+hs_user->picked_up[1];
        }
        strlist = database_get_data(stats, KEY_HELPER_STATS_CLOSE, RECDB_STRING_LIST);
        if (strlist) {
            for (i=0; i < 5 && i < strlist->used; i++)
                hs_user->closed[i] = strtoul(strlist->list[i], NULL, 0);
            if (strlist->used == 2)
                hs_user->closed[4] = hs_user->closed[0]+hs_user->closed[1];
        }
        strlist = database_get_data(stats, KEY_HELPER_STATS_REASSIGNFROM, RECDB_STRING_LIST);
        if (strlist) {
            for (i=0; i < 5 && i < strlist->used; i++)
                hs_user->reassigned_from[i] = strtoul(strlist->list[i], NULL, 0);
            if (strlist->used == 2)
                hs_user->reassigned_from[4] = hs_user->reassigned_from[0]+hs_user->reassigned_from[1];
        }
        strlist = database_get_data(stats, KEY_HELPER_STATS_REASSIGNTO, RECDB_STRING_LIST);
        if (strlist) {
            for (i=0; i < 5 && i < strlist->used; i++)
                hs_user->reassigned_to[i] = strtoul(strlist->list[i], NULL, 0);
            if (strlist->used == 2)
                hs_user->reassigned_to[4] = hs_user->reassigned_to[0]+hs_user->reassigned_to[1];
        }
    }

    return 0;
}

static int request_write_helper(const char *key, void *data, void *extra) {
    struct helpserv_request *request = data;
    struct saxdb_context *ctx = extra;

    if (!request->handle)
        return 0;

    saxdb_start_record(ctx, key, 0);
    if (request->helper) {
        saxdb_write_string(ctx, KEY_REQUEST_HELPER, request->helper->handle->handle);
        saxdb_write_int(ctx, KEY_REQUEST_ASSIGNED, request->assigned);
    }
    saxdb_write_string(ctx, KEY_REQUEST_HANDLE, request->handle->handle);
    saxdb_write_int(ctx, KEY_REQUEST_OPENED, request->opened);
    saxdb_write_string_list(ctx, KEY_REQUEST_TEXT, request->text);
    saxdb_end_record(ctx);
    return 0;
}

static int request_read_helper(const char *key, void *data, void *extra) {
    struct record_data *rd = data;
    struct helpserv_bot *hs = extra;
    struct helpserv_request *request;
    struct string_list *strlist;
    char *str;

    if (rd->type != RECDB_OBJECT || !dict_size(rd->d.object)) {
        log_module(HS_LOG, LOG_ERROR, "Invalid request %s:%s.", hs->helpserv->nick, key);
        return 0;
    }

    request = calloc(1, sizeof(struct helpserv_request));

    request->id = strtoul(key, NULL, 0);
    request->hs = hs;
    request->user = NULL;
    request->parent_nick_list = request->parent_hand_list = NULL;

    str = database_get_data(rd->d.object, KEY_REQUEST_HANDLE, RECDB_QSTRING);
    if (!str || !(request->handle = get_handle_info(str))) {
        log_module(HS_LOG, LOG_ERROR, "Request %s:%s has an invalid or nonexistant account.", hs->helpserv->nick, key);
        free(request);
        return 0;
    }
    if (!(request->parent_hand_list = dict_find(helpserv_reqs_byhand_dict, request->handle->handle, NULL))) {
        request->parent_hand_list = helpserv_reqlist_alloc();
        dict_insert(helpserv_reqs_byhand_dict, request->handle->handle, request->parent_hand_list);
    }
    helpserv_reqlist_append(request->parent_hand_list, request);

    str = database_get_data(rd->d.object, KEY_REQUEST_OPENED, RECDB_QSTRING);
    if (!str) {
        log_module(HS_LOG, LOG_ERROR, "Request %s:%s has a nonexistant opening time. Using time(NULL).", hs->helpserv->nick, key);
        request->opened = time(NULL);
    } else {
        request->opened = (time_t)strtoul(str, NULL, 0);
    }

    str = database_get_data(rd->d.object, KEY_REQUEST_ASSIGNED, RECDB_QSTRING);
    if (str)
        request->assigned = (time_t)strtoul(str, NULL, 0);

    str = database_get_data(rd->d.object, KEY_REQUEST_HELPER, RECDB_QSTRING);
    if (str) {
        if (!(request->helper = dict_find(hs->users, str, NULL))) {
            log_module(HS_LOG, LOG_ERROR, "Request %s:%s has an invalid or nonexistant helper.", hs->helpserv->nick, key);
            free(request);
            return 0;
        }
    } else {
        if (!hs->unhandled) {
            request->next_unhandled = NULL;
            hs->unhandled = request;
        } else if (hs->unhandled->opened > request->opened) {
            request->next_unhandled = hs->unhandled;
            hs->unhandled = request;
        } else {
            struct helpserv_request *unh;
            for (unh = hs->unhandled; unh->next_unhandled && (unh->next_unhandled->opened < request->opened); unh = unh->next_unhandled);
            request->next_unhandled = unh->next_unhandled;
            unh->next_unhandled = request;
        }
    }

    strlist = database_get_data(rd->d.object, KEY_REQUEST_TEXT, RECDB_STRING_LIST);
    if (!strlist) {
        log_module(HS_LOG, LOG_ERROR, "Request %s:%s has no text.", hs->helpserv->nick, key);
        free(request);
        return 0;
    }
    request->text = string_list_copy(strlist);

    dict_insert(hs->requests, strdup(key), request);

    return 0;
}

static int
helpserv_bot_write(const char *key, void *data, void *extra) {
    const struct helpserv_bot *hs = data;
    struct saxdb_context *ctx = extra;
    enum page_source pagesrc;
    enum message_type msgtype;
    enum interval_type inttype;
    enum persistence_type persisttype;
    struct string_list *slist;

    /* Entire bot */
    saxdb_start_record(ctx, key, 1);

    /* Helper list */
    saxdb_start_record(ctx, KEY_HELPERS, 1);
    dict_foreach(hs->users, user_write_helper, ctx);
    saxdb_end_record(ctx);

    /* Open requests */
    if (hs->persist_types[PERSIST_T_REQUEST] == PERSIST_CLOSE) {
        saxdb_start_record(ctx, KEY_REQUESTS, 0);
        dict_foreach(hs->requests, request_write_helper, ctx);
        saxdb_end_record(ctx);
    }

    /* Other settings and state */
    saxdb_write_string(ctx, KEY_HELP_CHANNEL, hs->helpchan->name);
    slist = alloc_string_list(PGSRC_COUNT);
    for (pagesrc=0; pagesrc<PGSRC_COUNT; pagesrc++) {
        struct chanNode *target = hs->page_targets[pagesrc];
        string_list_append(slist, strdup(target ? target->name : "*"));
    }
    saxdb_write_string_list(ctx, KEY_PAGE_DEST, slist);
    free_string_list(slist);
    for (pagesrc=0; pagesrc<PGSRC_COUNT; pagesrc++) {
        const char *src = page_types[hs->page_types[pagesrc]].db_name;
        saxdb_write_string(ctx, page_sources[pagesrc].db_name, src);
    }
    for (msgtype=0; msgtype<MSGTYPE_COUNT; msgtype++) {
        const char *msg = hs->messages[msgtype];
        if (msg)
            saxdb_write_string(ctx, message_types[msgtype].db_name, msg);
    }
    for (inttype=0; inttype<INTERVAL_COUNT; inttype++) {
        if (!hs->intervals[inttype])
            continue;
        saxdb_write_int(ctx, interval_types[inttype].db_name, hs->intervals[inttype]);
    }
    for (persisttype=0; persisttype<PERSIST_T_COUNT; persisttype++) {
        const char *persist = persistence_lengths[hs->persist_types[persisttype]].db_name;
        saxdb_write_string(ctx, persistence_types[persisttype].db_name, persist);
    }
    saxdb_write_string(ctx, KEY_NOTIFICATION, notification_types[hs->notify].db_name);
    saxdb_write_int(ctx, KEY_REGISTERED, hs->registered);
    saxdb_write_int(ctx, KEY_IDWRAP, hs->id_wrap);
    saxdb_write_int(ctx, KEY_REQ_MAXLEN, hs->req_maxlen);
    saxdb_write_int(ctx, KEY_LAST_REQUESTID, hs->last_requestid);
    if (hs->registrar)
        saxdb_write_string(ctx, KEY_REGISTRAR, hs->registrar);
    saxdb_write_int(ctx, KEY_PRIVMSG_ONLY, hs->privmsg_only);
    saxdb_write_int(ctx, KEY_REQ_ON_JOIN, hs->req_on_join);
    saxdb_write_int(ctx, KEY_AUTO_VOICE, hs->auto_voice);
    saxdb_write_int(ctx, KEY_AUTO_DEVOICE, hs->auto_devoice);
    saxdb_write_int(ctx, KEY_LAST_ACTIVE, hs->last_active);

    /* End bot record */
    saxdb_end_record(ctx);
    return 0;
}

static int
helpserv_saxdb_write(struct saxdb_context *ctx) {
    saxdb_start_record(ctx, KEY_BOTS, 1);
    dict_foreach(helpserv_bots_dict, helpserv_bot_write, ctx);
    saxdb_end_record(ctx);
    saxdb_write_int(ctx, KEY_LAST_STATS_UPDATE, last_stats_update);
    return 0;
}

static int helpserv_bot_read(const char *key, void *data, UNUSED_ARG(void *extra)) {
    struct record_data *br = data, *raw_record;
    struct helpserv_bot *hs;
    char *registrar, *helpchannel_name, *str;
    dict_t users, requests;
    enum page_source pagesrc;
    enum message_type msgtype;
    enum interval_type inttype;
    enum persistence_type persisttype;

    users = database_get_data(GET_RECORD_OBJECT(br), KEY_HELPERS, RECDB_OBJECT);
    if (!users) {
        log_module(HS_LOG, LOG_ERROR, "%s has no users.", key);
        return 0;
    }
    helpchannel_name = database_get_data(GET_RECORD_OBJECT(br), KEY_HELP_CHANNEL, RECDB_QSTRING);
    if (!helpchannel_name || !IsChannelName(helpchannel_name)) {
        log_module(HS_LOG, LOG_ERROR, "%s has an invalid channel name.", key);
        return 0;
    }
    registrar = database_get_data(GET_RECORD_OBJECT(br), KEY_REGISTRAR, RECDB_QSTRING);

    hs = register_helpserv(key, helpchannel_name, registrar);

    raw_record = dict_find(GET_RECORD_OBJECT(br), KEY_PAGE_DEST, NULL);
    switch (raw_record ? raw_record->type : RECDB_INVALID) {
    case RECDB_QSTRING:
        set_page_target(hs, PGSRC_COMMAND, GET_RECORD_QSTRING(raw_record));
        pagesrc = PGSRC_COMMAND + 1;
        break;
    case RECDB_STRING_LIST: {
        struct string_list *slist = GET_RECORD_STRING_LIST(raw_record);
        for (pagesrc=0; (pagesrc<slist->used) && (pagesrc<PGSRC_COUNT); pagesrc++) {
            const char *dest = slist->list[pagesrc];
            set_page_target(hs, pagesrc, strcmp(dest, "*") ? dest : NULL);
        }
        break;
    }
    default:
        set_page_target(hs, PGSRC_COMMAND, NULL);
        pagesrc = PGSRC_COMMAND + 1;
        break;
    }
    while (pagesrc < PGSRC_COUNT) {
        set_page_target(hs, pagesrc++, hs->page_targets[PGSRC_COMMAND] ? hs->page_targets[PGSRC_COMMAND]->name : NULL);
    }

    for (pagesrc=0; pagesrc<PGSRC_COUNT; pagesrc++) {
        str = database_get_data(GET_RECORD_OBJECT(br), page_sources[pagesrc].db_name, RECDB_QSTRING);
        hs->page_types[pagesrc] = str ? page_type_from_name(str) : PAGE_NONE;
    }

    for (msgtype=0; msgtype<MSGTYPE_COUNT; msgtype++) {
        str = database_get_data(GET_RECORD_OBJECT(br), message_types[msgtype].db_name, RECDB_QSTRING);
        hs->messages[msgtype] = str ? strdup(str) : NULL;
    }

    for (inttype=0; inttype<INTERVAL_COUNT; inttype++) {
        str = database_get_data(GET_RECORD_OBJECT(br), interval_types[inttype].db_name, RECDB_QSTRING);
        hs->intervals[inttype] = str ? ParseInterval(str) : 0;
    }
    if (hs->intervals[INTERVAL_WHINE_INTERVAL])
        timeq_add(now + hs->intervals[INTERVAL_WHINE_INTERVAL], run_whine_interval, hs);
    if (hs->intervals[INTERVAL_EMPTY_INTERVAL])
        timeq_add(now + hs->intervals[INTERVAL_EMPTY_INTERVAL], run_empty_interval, hs);

    for (persisttype=0; persisttype<PERSIST_T_COUNT; persisttype++) {
        str = database_get_data(GET_RECORD_OBJECT(br), persistence_types[persisttype].db_name, RECDB_QSTRING);
        hs->persist_types[persisttype] = str ? persistence_from_name(str) : PERSIST_QUIT;
    }
    str = database_get_data(GET_RECORD_OBJECT(br), KEY_NOTIFICATION, RECDB_QSTRING);
    hs->notify = str ? notification_from_name(str) : NOTIFY_NONE;
    str = database_get_data(GET_RECORD_OBJECT(br), KEY_REGISTERED, RECDB_QSTRING);
    if (str)
        hs->registered = (time_t)strtol(str, NULL, 0);
    str = database_get_data(GET_RECORD_OBJECT(br), KEY_IDWRAP, RECDB_QSTRING);
    if (str)
        hs->id_wrap = strtoul(str, NULL, 0);
    str = database_get_data(GET_RECORD_OBJECT(br), KEY_REQ_MAXLEN, RECDB_QSTRING);
    if (str)
        hs->req_maxlen = strtoul(str, NULL, 0);
    str = database_get_data(GET_RECORD_OBJECT(br), KEY_LAST_REQUESTID, RECDB_QSTRING);
    if (str)
        hs->last_requestid = strtoul(str, NULL, 0);
    str = database_get_data(GET_RECORD_OBJECT(br), KEY_PRIVMSG_ONLY, RECDB_QSTRING);
    hs->privmsg_only = str ? enabled_string(str) : 0;
    str = database_get_data(GET_RECORD_OBJECT(br), KEY_REQ_ON_JOIN, RECDB_QSTRING);
    hs->req_on_join = str ? enabled_string(str) : 0;
    str = database_get_data(GET_RECORD_OBJECT(br), KEY_AUTO_VOICE, RECDB_QSTRING);
    hs->auto_voice = str ? enabled_string(str) : 0;
    str = database_get_data(GET_RECORD_OBJECT(br), KEY_AUTO_DEVOICE, RECDB_QSTRING);
    hs->auto_devoice = str ? enabled_string(str) : 0;
    str = database_get_data(GET_RECORD_OBJECT(br), KEY_LAST_ACTIVE, RECDB_QSTRING);
    hs->last_active = str ? atoi(str) : now;

    dict_foreach(users, user_read_helper, hs);

    requests = database_get_data(GET_RECORD_OBJECT(br), KEY_REQUESTS, RECDB_OBJECT);
    if (requests)
        dict_foreach(requests, request_read_helper, hs);

    return 0;
}

static int
helpserv_saxdb_read(struct dict *conf_db) {
    dict_t object;
    char *str;

    if ((object = database_get_data(conf_db, KEY_BOTS, RECDB_OBJECT))) {
        dict_foreach(object, helpserv_bot_read, NULL);
    }

    str = database_get_data(conf_db, KEY_LAST_STATS_UPDATE, RECDB_QSTRING);
    last_stats_update = str ? (time_t)strtol(str, NULL, 0) : now;
    return 0;
}

static void helpserv_conf_read(void) {
    dict_t conf_node;
    const char *str;

    if (!(conf_node = conf_get_data(HELPSERV_CONF_NAME, RECDB_OBJECT))) {
        log_module(HS_LOG, LOG_ERROR, "config node `%s' is missing or has wrong type", HELPSERV_CONF_NAME);
        return;
    }

    str = database_get_data(conf_node, "db_backup_freq", RECDB_QSTRING);
    helpserv_conf.db_backup_frequency = str ? ParseInterval(str) : 7200;

    str = database_get_data(conf_node, "description", RECDB_QSTRING);
    helpserv_conf.description = str;

    str = database_get_data(conf_node, "reqlogfile", RECDB_QSTRING);
    if (str && strlen(str))
        helpserv_conf.reqlogfile = str;
    else
        helpserv_conf.reqlogfile = NULL;

    str = database_get_data(conf_node, "expiration", RECDB_QSTRING);
    helpserv_conf.expire_age = ParseInterval(str ? str : "60d");
    str = database_get_data(conf_node, "user_escape", RECDB_QSTRING);
    helpserv_conf.user_escape = str ? str[0] : '@';

    if (reqlog_f) {
        fclose(reqlog_f);
        reqlog_f = NULL;
    }
    if (helpserv_conf.reqlogfile
        && !(reqlog_f = fopen(helpserv_conf.reqlogfile, "a"))) {
        log_module(HS_LOG, LOG_ERROR, "Unable to open request logfile (%s): %s", helpserv_conf.reqlogfile, strerror(errno));
    }
}

static struct helpserv_cmd *
helpserv_define_func(const char *name, helpserv_func_t *func, enum helpserv_level access, long flags) {
    struct helpserv_cmd *cmd = calloc(1, sizeof(struct helpserv_cmd));

    cmd->access = access;
    cmd->weight = 1.0;
    cmd->func = func;
    cmd->flags = flags;
    dict_insert(helpserv_func_dict, name, cmd);

    return cmd;
}

/* Drop requests that persist until part when a user leaves the chan */
static void handle_part(struct modeNode *mn, UNUSED_ARG(const char *reason)) {
    struct helpserv_botlist *botlist;
    struct helpserv_userlist *userlist;
    const int from_opserv = 0; /* for helpserv_notice */
    unsigned int i;

    if ((botlist = dict_find(helpserv_bots_bychan_dict, mn->channel->name, NULL))) {
        for (i=0; i < botlist->used; i++) {
            struct helpserv_bot *hs;
            dict_iterator_t it;

            hs = botlist->list[i];
            if (!hs->helpserv)
                continue;
            if (hs->persist_types[PERSIST_T_REQUEST] != PERSIST_PART)
                continue;

            for (it=dict_first(hs->requests); it; it=iter_next(it)) {
                struct helpserv_request *req = iter_data(it);

                if (mn->user != req->user)
                    continue;
                if (req->text->used) {
                    helpserv_message(hs, mn->user, MSGTYPE_REQ_DROPPED);
                    helpserv_msguser(mn->user, "HSMSG_REQ_DROPPED_PART", mn->channel->name, req->id);
                    if (req->helper && (hs->notify >= NOTIFY_DROP))
                        helpserv_notify(req->helper, "HSMSG_NOTIFY_REQ_DROP_PART", req->id, mn->user->nick);
                }
                helpserv_log_request(req, "Dropped");
                dict_remove(hs->requests, iter_key(it));
                break;
            }
        }
    }
    
    if (mn->user->handle_info && (userlist = dict_find(helpserv_users_byhand_dict, mn->user->handle_info->handle, NULL))) {
        for (i=0; i < userlist->used; i++) {
            struct helpserv_user *hs_user = userlist->list[i];
            struct helpserv_bot *hs = hs_user->hs;
            dict_iterator_t it;

            if ((hs->helpserv == NULL) || (hs->helpchan != mn->channel) || find_handle_in_channel(hs->helpchan, mn->user->handle_info, mn->user))
                continue;

            /* In case of the clock being set back for whatever reason,
             * minimize penalty. Don't duplicate this in handle_quit because
             * when users quit, handle_part is called for every channel first.
             */
            if (hs_user->join_time && (hs_user->join_time < now)) {
                hs_user->time_per_week[0] += (unsigned int)(now - hs_user->join_time);
                hs_user->time_per_week[4] += (unsigned int)(now - hs_user->join_time);
            }
            hs_user->join_time = 0;

            for (it=dict_first(hs->requests); it; it=iter_next(it)) {
                struct helpserv_request *req=iter_data(it);

                if ((hs->persist_types[PERSIST_T_HELPER] == PERSIST_PART)
                    && (req->helper == hs_user)) {
                    char reason[CHANNELLEN + 8];
                    sprintf(reason, "parted %s", mn->channel->name);
                    helpserv_page_helper_gone(hs, req, reason);
                }
            }

            if (hs->intervals[INTERVAL_EMPTY_INTERVAL] && hs_user->level >= HlHelper) {
                int num_trials;

                if ((num_trials = find_helpchan_helpers(hs)) >= 0) {
                    unsigned int num_unh;
                    struct helpserv_request *unh;

                    for (num_unh=0, unh=hs->unhandled; unh; num_unh++)
                        unh = unh->next_unhandled;

                    if (num_trials) {
                        helpserv_page(PGSRC_ALERT, "HSMSG_PAGE_FIRSTONLYTRIALALERT", hs->helpchan->name, mn->user->nick, num_trials, num_unh);
                    } else {
                        helpserv_page(PGSRC_ALERT, "HSMSG_PAGE_FIRSTEMPTYALERT", hs->helpchan->name, mn->user->nick, num_unh);
                    }
                    if (num_unh || !hs->req_on_join) {
                        timeq_del(0, run_empty_interval, hs, TIMEQ_IGNORE_WHEN);
                        timeq_add(now + hs->intervals[INTERVAL_EMPTY_INTERVAL], run_empty_interval, hs);
                    }
                }
            }
        }
    }
}

/* Drop requests that persist until part or quit when a user quits. Otherwise
 * set req->user to null (it's no longer valid) if they have a handle,
 * and drop it if they don't (nowhere to store the request).
 *
 * Unassign requests where req->helper persists until the helper parts or
 * quits. */
static void handle_quit(struct userNode *user, UNUSED_ARG(struct userNode *killer), UNUSED_ARG(const char *why)) {
    struct helpserv_reqlist *reqlist;
    struct helpserv_userlist *userlist;
    unsigned int i, n;

    if (IsLocal(user)) {
        struct helpserv_bot *hs;
        if ((hs = dict_find(helpserv_bots_dict, user->nick, NULL))) {
            hs->helpserv = NULL;
        }
        return;
    }

    if ((reqlist = dict_find(helpserv_reqs_bynick_dict, user->nick, NULL))) {
        n = reqlist->used;
        for (i=0; i < n; i++) {
            struct helpserv_request *req = reqlist->list[0];

            if ((req->hs->persist_types[PERSIST_T_REQUEST] == PERSIST_QUIT) || !req->handle) {
                char buf[12];
                sprintf(buf, "%lu", req->id);

                if (req->helper && (req->hs->notify >= NOTIFY_DROP))
                    helpserv_notify(req->helper, "HSMSG_NOTIFY_REQ_DROP_QUIT", req->id, req->user->nick);

                helpserv_log_request(req, "Dropped");
                dict_remove(req->hs->requests, buf);
            } else {
                req->user = NULL;
                req->parent_nick_list = NULL;
                helpserv_reqlist_remove(reqlist, req);

                if (req->helper && (req->hs->notify >= NOTIFY_USER))
                    helpserv_notify(req->helper, "HSMSG_NOTIFY_USER_QUIT", req->id, user->nick);
            }
        }

        dict_remove(helpserv_reqs_bynick_dict, user->nick);
    }

    if (user->handle_info && (userlist = dict_find(helpserv_users_byhand_dict, user->handle_info->handle, NULL))) {
        for (i=0; i < userlist->used; i++) {
            struct helpserv_user *hs_user = userlist->list[i];
            struct helpserv_bot *hs = hs_user->hs;
            dict_iterator_t it;

            if ((hs->helpserv == NULL) || user->next_authed || (user->handle_info->users != user))
                continue;

            for (it=dict_first(hs->requests); it; it=iter_next(it)) {
                struct helpserv_request *req=iter_data(it);

                if ((hs->persist_types[PERSIST_T_HELPER] == PERSIST_QUIT) && (req->helper == hs_user)) {
                    helpserv_page_helper_gone(hs, req, "disconnected");
                }
            }
        }
    }
}

static void associate_requests_bybot(struct helpserv_bot *hs, struct userNode *user, int force_greet) {
    struct helpserv_reqlist *reqlist, *hand_reqlist=NULL;
    struct helpserv_request *newest=NULL, *nicknewest=NULL;
    unsigned int i;
    const int from_opserv = 0; /* For helpserv_notice */
    
    if (!(user->handle_info && (hand_reqlist = dict_find(helpserv_reqs_byhand_dict, user->handle_info->handle, NULL))) && !force_greet) {
        return;
    }

    reqlist = dict_find(helpserv_reqs_bynick_dict, user->nick, NULL);

    if (hand_reqlist) {
        for (i=0; i < hand_reqlist->used; i++) {
            struct helpserv_request *req=hand_reqlist->list[i];

            if (req->user || (req->hs != hs))
                continue;

            req->user = user;
            if (!reqlist) {
                reqlist = helpserv_reqlist_alloc();
                dict_insert(helpserv_reqs_bynick_dict, user->nick, reqlist);
            }
            req->parent_nick_list = reqlist;
            helpserv_reqlist_append(reqlist, req);

            if (req->helper && (hs->notify >= NOTIFY_USER))
                helpserv_notify(req->helper, "HSMSG_NOTIFY_USER_FOUND", req->id, user->nick);

            if (!newest || (newest->opened < req->opened))
                newest = req;
        }
    }

    /* If it's supposed to force a greeting, only bail out if there are no
     * requests at all. If it's not supposed to force a greeting, bail out if
     * nothing was changed. */
    if (!(newest || (force_greet && reqlist)))
        return;

    /* Possible conditions here:
     * 1. newest == NULL, force_greeting == 1, reqlist != NULL
     * 2. newest != NULL, force_greeting doesn't matter, reqlist != NULL */

    /* Figure out which request will get their next message */
    for (i=0; i < reqlist->used; i++) {
        struct helpserv_request *req=reqlist->list[i];

        if (req->hs != hs)
            continue;

        if (!nicknewest || (nicknewest->opened < req->opened))
            nicknewest = req;

        if (hs->auto_voice && req->helper)
        {
            struct mod_chanmode change;
            mod_chanmode_init(&change);
            change.argc = 1;
            change.args[0].mode = MODE_VOICE;
            if ((change.args[0].member = GetUserMode(hs->helpchan, user)))
                mod_chanmode_announce(hs->helpserv, hs->helpchan, &change);
        }
    }

    if ((force_greet && nicknewest) || (newest && (nicknewest == newest))) {
        /* Let the user know. Either the user is forced to be greeted, or the
         * above has changed which request will get their next message. */
        helpserv_msguser(user, "HSMSG_GREET_EXISTING_REQ", hs->helpchan->name, nicknewest->id);
    }
}

static void associate_requests_bychan(struct chanNode *chan, struct userNode *user, int force_greet) {
    struct helpserv_botlist *botlist;
    unsigned int i;

    if (!(botlist = dict_find(helpserv_bots_bychan_dict, chan->name, NULL)))
        return;

    for (i=0; i < botlist->used; i++)
        associate_requests_bybot(botlist->list[i], user, force_greet);
}


/* Greet users upon joining a helpserv channel (if greeting is set) and set
 * req->user to the user joining for all requests owned by the user's handle
 * (if any) with a req->user == NULL */
static int handle_join(struct modeNode *mNode) {
    struct userNode *user = mNode->user;
    struct chanNode *chan = mNode->channel;
    struct helpserv_botlist *botlist;
    unsigned int i;
    const int from_opserv = 0; /* for helpserv_notice */

    if (IsLocal(user))
        return 0;
    
    if (!(botlist = dict_find(helpserv_bots_bychan_dict, chan->name, NULL)))
        return 0;

    for (i=0; i < botlist->used; i++) {
        struct helpserv_bot *hs=botlist->list[i];

        if (user->handle_info) {
            struct helpserv_user *hs_user;

            if ((hs_user = dict_find(hs->users, user->handle_info->handle, NULL))) {
                if (!hs_user->join_time)
                    hs_user->join_time = now;

                if (hs_user->level >= HlHelper && hs->intervals[INTERVAL_EMPTY_INTERVAL] && hs->helpchan_empty) {
                    hs->helpchan_empty = 0;
                    timeq_del(0, run_empty_interval, hs, TIMEQ_IGNORE_WHEN);
                    helpserv_page(PGSRC_ALERT, "HSMSG_PAGE_EMPTYNOMORE", user->nick, hs->helpchan->name);
                }
                continue; /* Don't want helpers to have request-on-join */
            }
        }

        if (self->burst && !hs->req_on_join)
            continue;

        associate_requests_bybot(hs, user, 1);

        helpserv_message(hs, user, MSGTYPE_GREETING);

        /* Make sure this is at the end (because of the continues) */
        if (hs->req_on_join) {
            struct helpserv_reqlist *reqlist;
            unsigned int j;

            if ((reqlist = dict_find(helpserv_reqs_bynick_dict, user->nick, NULL))) {
                for (j=0; j < reqlist->used; j++)
                    if (reqlist->list[i]->hs == hs)
                        break;
                if (j < reqlist->used)
                    continue;
            }

            create_request(user, hs, 1);
        }
    }
    return 0;
}

/* Update helpserv_reqs_bynick_dict upon nick change */
static void handle_nickchange(struct userNode *user, const char *old_nick) {
    struct helpserv_reqlist *reqlist;
    unsigned int i;

    if (!(reqlist = dict_find(helpserv_reqs_bynick_dict, old_nick, NULL)))
        return;

    /* Don't free the list when we switch it over to the new nick. */
    dict_remove2(helpserv_reqs_bynick_dict, old_nick, 1);
    dict_insert(helpserv_reqs_bynick_dict, user->nick, reqlist);

    for (i=0; i < reqlist->used; i++) {
        struct helpserv_request *req=reqlist->list[i];

        if (req->helper && (req->hs->notify >= NOTIFY_USER))
            helpserv_notify(req->helper, "HSMSG_NOTIFY_USER_NICK", req->id, old_nick, user->nick);
    }
}

/* Also update helpserv_reqs_byhand_dict upon handle rename */
static void handle_nickserv_rename(struct handle_info *handle, const char *old_handle) {
    struct helpserv_reqlist *reqlist;
    struct helpserv_userlist *userlist;
    unsigned int i;

    /* First, rename the handle in the requests dict */
    if ((reqlist = dict_find(helpserv_reqs_byhand_dict, old_handle, NULL))) {
        /* Don't free the list */
        dict_remove2(helpserv_reqs_byhand_dict, old_handle, 1);
        dict_insert(helpserv_reqs_byhand_dict, handle->handle, reqlist);
    }

    /* Second, rename the handle in the users dict */
    if ((userlist = dict_find(helpserv_users_byhand_dict, old_handle, NULL))) {
        dict_remove2(helpserv_users_byhand_dict, old_handle, 1);

        for (i=0; i < userlist->used; i++)
            dict_remove2(userlist->list[i]->hs->users, old_handle, 1);

        dict_insert(helpserv_users_byhand_dict, handle->handle, userlist);
        for (i=0; i < userlist->used; i++)
            dict_insert(userlist->list[i]->hs->users, handle->handle, userlist->list[i]);
    }
    
    if (reqlist) {
        for (i=0; i < reqlist->used; i++) {
            struct helpserv_request *req=reqlist->list[i];

            if (req->helper && (req->hs->notify >= NOTIFY_HANDLE))
                helpserv_notify(req->helper, "HSMSG_NOTIFY_HAND_RENAME", req->id, old_handle, handle->handle);
        }
    }
}

/* Deals with two cases:
 * 1. No handle -> handle
 *    - Bots with a request assigned to both the user (w/o handle) and the
 *      handle can exist in this case. When a message is sent,
 *      helpserv_usermsg will append it to the most recently opened request.
 *    - Requests assigned to the handle are NOT assigned to the user, since
 *      multiple people can auth to the same handle at once. Wait for them to
 *      join / privmsg before setting req->user.
 * 2. Handle -> handle
 *    - Generally avoided, but sometimes the code may allow this.
 *    - Requests that persist only until part/quit are brought along to the
 *      new handle.
 *    - Requests that persist until closed (stay saved with the handle) are
 *      left with the old handle. This is to prevent the confusing situation
 *      where some requests are carried over to the new handle, and some are
 *      left (because req->handle is the same for all of them, but only some
 *      have req->user set).
 * - In either of the above cases, if a user is on a bot's userlist and has
 *   requests assigned to them, it will give them a list. */
static void handle_nickserv_auth(struct userNode *user, struct handle_info *old_handle) {
    struct helpserv_reqlist *reqlist, *dellist=NULL, *hand_reqlist, *oldhand_reqlist;
    struct helpserv_userlist *userlist;
    unsigned int i, j;
    dict_iterator_t it;
    const int from_opserv = 0; /* for helpserv_notice */

    if (!user->handle_info)
        return; /* Authed user is quitting */

    if ((userlist = dict_find(helpserv_users_byhand_dict, user->handle_info->handle, NULL))) {
        for (i=0; i < userlist->used; i++) {
            struct helpserv_user *hs_user = userlist->list[i];
            struct helpserv_bot *hs = hs_user->hs;
            struct helpserv_reqlist helper_reqs;
            struct helpfile_table tbl;

            if (!hs_user->join_time && find_handle_in_channel(hs->helpchan, hs_user->handle, NULL))
                hs_user->join_time = now;

            helpserv_reqlist_init(&helper_reqs);

            for (it=dict_first(hs->requests); it; it=iter_next(it)) {
                struct helpserv_request *req=iter_data(it);

                if (req->helper == hs_user)
                    helpserv_reqlist_append(&helper_reqs, req);
            }

            if (helper_reqs.used) {
                tbl.length = helper_reqs.used+1;
                tbl.width = 5;
                tbl.flags = TABLE_NO_FREE;
                tbl.contents = alloca(tbl.length * sizeof(*tbl.contents));
                tbl.contents[0] = alloca(tbl.width * sizeof(**tbl.contents));
                tbl.contents[0][0] = "Bot";
                tbl.contents[0][1] = "ID#";
                tbl.contents[0][2] = "Nick";
                tbl.contents[0][3] = "Account";
                tbl.contents[0][4] = "Opened";

                for (j=1; j <= helper_reqs.used; j++) {
                    struct helpserv_request *req=helper_reqs.list[j-1];
                    char reqid[12], timestr[MAX_LINE_SIZE];

                    tbl.contents[j] = alloca(tbl.width * sizeof(**tbl.contents));
                    tbl.contents[j][0] = req->hs->helpserv->nick;
                    sprintf(reqid, "%lu", req->id);
                    tbl.contents[j][1] = strdup(reqid);
                    tbl.contents[j][2] = req->user ? req->user->nick : "Not online";
                    tbl.contents[j][3] = req->handle ? req->handle->handle : "Not authed";
                    strftime(timestr, MAX_LINE_SIZE, HSFMT_TIME, localtime(&req->opened));
                    tbl.contents[j][4] = strdup(timestr);
                }

                helpserv_notice(user, "HSMSG_REQLIST_AUTH");
                table_send(hs->helpserv, user->nick, 0, NULL, tbl);

                for (j=1; j <= helper_reqs.used; j++) {
                    free((char *)tbl.contents[j][1]);
                    free((char *)tbl.contents[j][4]);
                }
            }

            helpserv_reqlist_clean(&helper_reqs);
        }
    }


    if (!(reqlist = dict_find(helpserv_reqs_bynick_dict, user->nick, NULL))) {
        for (i=0; i < user->channels.used; i++)
            associate_requests_bychan(user->channels.list[i]->channel, user, 0);
        return;
    }

    if (!(hand_reqlist = dict_find(helpserv_reqs_byhand_dict, user->handle_info->handle, NULL))) {
        hand_reqlist = helpserv_reqlist_alloc();
        dict_insert(helpserv_reqs_byhand_dict, user->handle_info->handle, hand_reqlist);
    }

    if (old_handle) {
        dellist = helpserv_reqlist_alloc();
        oldhand_reqlist = dict_find(helpserv_reqs_byhand_dict, old_handle->handle, NULL);
    } else {
        oldhand_reqlist = NULL;
    }

    for (i=0; i < reqlist->used; i++) {
        struct helpserv_request *req = reqlist->list[i];
        struct helpserv_bot *hs=req->hs;

        if (!old_handle || hs->persist_types[PERSIST_T_REQUEST] == PERSIST_PART || hs->persist_types[PERSIST_T_REQUEST] == PERSIST_QUIT) {
            /* The request needs to be assigned to the new handle; either it
             * only persists until part/quit (so it makes sense to keep it as
             * close to the user as possible, and if it's made persistent later
             * then it's attached to the new handle) or there is no old handle.
             */

            req->handle = user->handle_info;

            req->parent_hand_list = hand_reqlist;
            helpserv_reqlist_append(hand_reqlist, req);

            if (oldhand_reqlist) {
                if (oldhand_reqlist->used == 1) {
                    dict_remove(helpserv_reqs_byhand_dict, old_handle->handle);
                    oldhand_reqlist = NULL;
                } else {
                    helpserv_reqlist_remove(oldhand_reqlist, req);
                }
            }

            if (old_handle) {
                char buf[CHANNELLEN + 14];

                if (hs->persist_types[PERSIST_T_REQUEST] == PERSIST_PART) {
                    sprintf(buf, "part channel %s", hs->helpchan->name);
                } else {
                    strcpy(buf, "quit irc");
                }

                helpserv_msguser(user, "HSMSG_REQ_AUTH_MOVED", user->handle_info->handle, hs->helpchan->name, req->id, old_handle->handle, buf);
                if (req->helper && (hs->notify >= NOTIFY_HANDLE))
                    helpserv_notify(req->helper, "HSMSG_NOTIFY_HAND_MOVE", req->id, user->handle_info->handle, old_handle->handle);
            } else {
                if (req->helper && (hs->notify >= NOTIFY_HANDLE))
                    helpserv_notify(req->helper, "HSMSG_NOTIFY_HAND_AUTH", req->id, user->nick, user->handle_info->handle);
            }
        } else {
            req->user = NULL;
            req->parent_nick_list = NULL;
            /* Would rather not mess with the list while iterating through
             * it */
            helpserv_reqlist_append(dellist, req);

            helpserv_msguser(user, "HSMSG_REQ_AUTH_STUCK", user->handle_info->handle, hs->helpchan->name, req->id, old_handle->handle);
            if (req->helper && (hs->notify >= NOTIFY_HANDLE))
                helpserv_notify(req->helper, "HSMSG_NOTIFY_HAND_STUCK", req->id, user->nick, user->handle_info->handle, old_handle->handle);
        }
    }

    if (old_handle) {
        if (dellist->used) {
            if (dellist->used == reqlist->used) {
                dict_remove(helpserv_reqs_bynick_dict, user->nick);
            } else {
                for (i=0; i < dellist->used; i++)
                    helpserv_reqlist_remove(reqlist, dellist->list[i]);
            }
        }
        helpserv_reqlist_free(dellist);
    }

    for (i=0; i < user->channels.used; i++)
        associate_requests_bychan(user->channels.list[i]->channel, user, 0);
}


/* Disassociate all requests from the handle. If any have req->user == NULL
 * then give them to the user doing the unregistration (if not an oper/helper)
 * otherwise the first nick it finds authed (it lets them know about this). If
 * there are no users authed to the handle online, the requests are lost. This
 * may result in the user having >1 request/bot, and messages go to the most
 * recently opened request.
 *
 * Also, remove the user from all bots that it has access in.
 * helpserv_del_user() will take care of unassigning the requests. */
static void handle_nickserv_unreg(struct userNode *user, struct handle_info *handle) {
    struct helpserv_reqlist *hand_reqlist;
    struct helpserv_userlist *userlist;
    unsigned int i, n;
    const int from_opserv = 0; /* for helpserv_notice */
    struct helpserv_bot *hs; /* for helpserv_notice */

    if ((userlist = dict_find(helpserv_users_byhand_dict, handle->handle, NULL))) {
        n=userlist->used;

        /* Each time helpserv_del_user is called, that entry is going to be
         * taken out of userlist... so this should cope with that */
        for (i=0; i < n; i++) {
            struct helpserv_user *hs_user=userlist->list[0];
            helpserv_del_user(hs_user->hs, hs_user);
        }
    }

    if (!(hand_reqlist = dict_find(helpserv_reqs_byhand_dict, handle->handle, NULL))) {
        return;
    }

    n = hand_reqlist->used;
    for (i=0; i < n; i++) {
        struct helpserv_request *req=hand_reqlist->list[0];
        hs = req->hs;

        req->handle = NULL;
        req->parent_hand_list = NULL;
        helpserv_reqlist_remove(hand_reqlist, req);
        if (user && req->helper && (hs->notify >= NOTIFY_HANDLE))
            helpserv_notify(req->helper, "HSMSG_NOTIFY_HAND_UNREG", req->id, handle->handle, user->nick);

        if (!req->user) {
            if (!user) {
                /* This is probably an expire. Silently remove everything. */

                char buf[12];
                if (req->helper && (hs->notify >= NOTIFY_DROP))
                    helpserv_notify(req->helper, "HSMSG_NOTIFY_REQ_DROP_UNREGGED", req->id, req->handle->handle);
                sprintf(buf, "%lu", req->id);
                helpserv_log_request(req, "Account unregistered");
                dict_remove(req->hs->requests, buf);
            } else if (user->handle_info == handle) {
                req->user = user;
                if (!(req->parent_nick_list = dict_find(helpserv_reqs_bynick_dict, user->nick, NULL))) {
                    req->parent_nick_list = helpserv_reqlist_alloc();
                    dict_insert(helpserv_reqs_bynick_dict, user->nick, req->parent_nick_list);
                }
                helpserv_reqlist_append(req->parent_nick_list, req);

                if (hs->persist_types[PERSIST_T_REQUEST] == PERSIST_CLOSE)
                    helpserv_msguser(req->user, "HSMSG_REQ_WARN_UNREG", handle->handle, hs->helpchan->name, req->id);
            } else {
                if (handle->users) {
                    req->user = handle->users;

                    if (!(req->parent_nick_list = dict_find(helpserv_reqs_bynick_dict, req->user->nick, NULL))) {
                        req->parent_nick_list = helpserv_reqlist_alloc();
                        dict_insert(helpserv_reqs_bynick_dict, req->user->nick, req->parent_nick_list);
                    }
                    helpserv_reqlist_append(req->parent_nick_list, req);

                    helpserv_msguser(req->user, "HSMSG_REQ_ASSIGNED_UNREG", handle->handle, hs->helpchan->name, req->id);
                    if (req->helper && (hs->notify >= NOTIFY_USER))
                        helpserv_notify(req->helper, "HSMSG_NOTIFY_USER_MOVE", req->id, handle->handle, req->user->nick);
                } else {
                    char buf[12];

                    helpserv_notice(user, "HSMSG_REQ_DROPPED_UNREG", handle->handle, hs->helpchan->name, req->id);
                    if (req->helper && (hs->notify >= NOTIFY_DROP))
                        helpserv_notify(req->helper, "HSMSG_NOTIFY_REQ_DROP_UNREGGED", req->id, req->handle->handle);
                    sprintf(buf, "%lu", req->id);
                    helpserv_log_request(req, "Account unregistered");
                    dict_remove(req->hs->requests, buf);
                }
            }
        }
    }

    dict_remove(helpserv_reqs_byhand_dict, handle->handle);
}

static void handle_nickserv_merge(struct userNode *user, struct handle_info *handle_to, struct handle_info *handle_from) {
    struct helpserv_reqlist *reqlist_from, *reqlist_to;
    unsigned int i;

    reqlist_to = dict_find(helpserv_reqs_byhand_dict, handle_to->handle, NULL);

    if ((reqlist_from = dict_find(helpserv_reqs_byhand_dict, handle_from->handle, NULL))) {
        for (i=0; i < reqlist_from->used; i++) {
            struct helpserv_request *req=reqlist_from->list[i];

            if (!reqlist_to) {
                reqlist_to = helpserv_reqlist_alloc();
                dict_insert(helpserv_reqs_byhand_dict, handle_to->handle, reqlist_to);
            }
            req->parent_hand_list = reqlist_to;
            req->handle = handle_to;
            helpserv_reqlist_append(reqlist_to, req);
        }
        dict_remove(helpserv_reqs_byhand_dict, handle_from->handle);
    }

    if (reqlist_to) {
        for (i=0; i < reqlist_to->used; i++) {
            struct helpserv_request *req=reqlist_to->list[i];

            if (req->helper && (req->hs->notify >= NOTIFY_HANDLE)) {
                helpserv_notify(req->helper, "HSMSG_NOTIFY_HAND_MERGE", req->id, handle_to->handle, handle_from->handle, user->nick);
            }
        }
    }
}

static void handle_nickserv_allowauth(struct userNode *user, struct userNode *target, struct handle_info *handle) {
    struct helpserv_reqlist *reqlist;
    unsigned int i;

    if ((reqlist = dict_find(helpserv_reqs_bynick_dict, target->nick, NULL))) {
        for (i=0; i < reqlist->used; i++) {
            struct helpserv_request *req=reqlist->list[i];

            if (req->helper && (req->hs->notify >= NOTIFY_HANDLE)) {
                if (handle) {
                    helpserv_notify(req->helper, "HSMSG_NOTIFY_ALLOWAUTH", req->id, target->nick, user->nick, handle->handle);
                } else {
                    helpserv_notify(req->helper, "HSMSG_NOTIFY_UNALLOWAUTH", req->id, target->nick, user->nick);
                }
            }
        }
    }
}

static void handle_nickserv_failpw(struct userNode *user, struct handle_info *handle) {
    struct helpserv_reqlist *reqlist;
    unsigned int i;

    if ((reqlist = dict_find(helpserv_reqs_bynick_dict, user->nick, NULL))) {
        for (i=0; i < reqlist->used; i++) {
            struct helpserv_request *req=reqlist->list[i];
            if (req->helper && (req->hs->notify >= NOTIFY_HANDLE))
                helpserv_notify(req->helper, "HSMSG_NOTIFY_FAILPW", req->id, user->nick, handle->handle);
        }
    }
}

static time_t helpserv_next_stats(time_t after_when) {
    struct tm *timeinfo = localtime(&after_when);

    /* This works because mktime(3) says it will accept out-of-range values
     * and fix them for us. tm_wday and tm_yday are ignored. */
    timeinfo->tm_mday++;

    /* We want to run stats at midnight (local time). */
    timeinfo->tm_sec = timeinfo->tm_min = timeinfo->tm_hour = 0;

    return mktime(timeinfo);
}

/* If data != NULL, then don't add to the timeq */
static void helpserv_run_stats(time_t when) {
    struct tm when_s;
    struct helpserv_bot *hs;
    struct helpserv_user *hs_user;
    int i;
    dict_iterator_t it, it2;

    last_stats_update = when;
    localtime_r(&when, &when_s);
    for (it=dict_first(helpserv_bots_dict); it; it=iter_next(it)) {
        hs = iter_data(it);

        for (it2=dict_first(hs->users); it2; it2=iter_next(it2)) {
            hs_user = iter_data(it2);

            /* Skip the helper if it's not their week-start day. */
            if (hs_user->week_start != when_s.tm_wday)
                continue;

            /* Adjust their credit if they are in-channel at rollover. */
            if (hs_user->join_time) {
                hs_user->time_per_week[0] += when - hs_user->join_time;
                hs_user->time_per_week[4] += when - hs_user->join_time;
                hs_user->join_time = when;
            }

            /* Shift everything */
            for (i=3; i > 0; i--) {
                hs_user->time_per_week[i] = hs_user->time_per_week[i-1];
                hs_user->picked_up[i] = hs_user->picked_up[i-1];
                hs_user->closed[i] = hs_user->closed[i-1];
                hs_user->reassigned_from[i] = hs_user->reassigned_from[i-1];
                hs_user->reassigned_to[i] = hs_user->reassigned_to[i-1];
            }

            /* Reset it for this week */
            hs_user->time_per_week[0] = hs_user->picked_up[0] = hs_user->closed[0] = hs_user->reassigned_from[0] = hs_user->reassigned_to[0] = 0;
        }
    }
}

static void helpserv_timed_run_stats(UNUSED_ARG(void *data)) {
    helpserv_run_stats(now);
    timeq_add(helpserv_next_stats(now), helpserv_timed_run_stats, data);
}

static void
helpserv_define_option(const char *name, helpserv_option_func_t *func) {
    dict_insert(helpserv_option_dict, name, func);
}

static void helpserv_db_cleanup(void) {
    shutting_down=1;
    unreg_part_func(handle_part);
    unreg_del_user_func(handle_quit);
    close_helpfile(helpserv_helpfile);
    dict_delete(helpserv_func_dict);
    dict_delete(helpserv_option_dict);
    dict_delete(helpserv_usercmd_dict);
    dict_delete(helpserv_bots_dict);
    dict_delete(helpserv_bots_bychan_dict);
    dict_delete(helpserv_reqs_bynick_dict);
    dict_delete(helpserv_reqs_byhand_dict);
    dict_delete(helpserv_users_byhand_dict);

    if (reqlog_f)
        fclose(reqlog_f);
}

int helpserv_init() {
    HS_LOG = log_register_type("HelpServ", "file:helpserv.log");
    conf_register_reload(helpserv_conf_read);

    helpserv_func_dict = dict_new();
    dict_set_free_data(helpserv_func_dict, free);
    helpserv_define_func("HELP", cmd_help, HlNone, CMD_NOT_OVERRIDE|CMD_IGNORE_EVENT);
    helpserv_define_func("LIST", cmd_list, HlTrial, CMD_NEED_BOT|CMD_IGNORE_EVENT);
    helpserv_define_func("NEXT", cmd_next, HlTrial, CMD_NEED_BOT|CMD_NEVER_FROM_OPSERV);
    helpserv_define_func("PICKUP", cmd_pickup, HlTrial, CMD_NEED_BOT|CMD_NEVER_FROM_OPSERV);
    helpserv_define_func("REASSIGN", cmd_reassign, HlManager, CMD_NEED_BOT|CMD_NEVER_FROM_OPSERV);
    helpserv_define_func("CLOSE", cmd_close, HlTrial, CMD_NEED_BOT|CMD_NEVER_FROM_OPSERV);
    helpserv_define_func("SHOW", cmd_show, HlTrial, CMD_NEED_BOT|CMD_IGNORE_EVENT);
    helpserv_define_func("ADDNOTE", cmd_addnote, HlTrial, CMD_NEED_BOT);
    helpserv_define_func("ADDOWNER", cmd_addowner, HlOper, CMD_NEED_BOT|CMD_FROM_OPSERV_ONLY);
    helpserv_define_func("DELOWNER", cmd_deluser, HlOper, CMD_NEED_BOT|CMD_FROM_OPSERV_ONLY);
    helpserv_define_func("ADDTRIAL", cmd_addtrial, HlManager, CMD_NEED_BOT);
    helpserv_define_func("ADDHELPER", cmd_addhelper, HlManager, CMD_NEED_BOT);
    helpserv_define_func("ADDMANAGER", cmd_addmanager, HlOwner, CMD_NEED_BOT);
    helpserv_define_func("GIVEOWNERSHIP", cmd_giveownership, HlOwner, CMD_NEED_BOT);
    helpserv_define_func("DELUSER", cmd_deluser, HlManager, CMD_NEED_BOT);
    helpserv_define_func("HELPERS", cmd_helpers, HlNone, CMD_NEED_BOT);
    helpserv_define_func("WLIST", cmd_wlist, HlNone, CMD_NEED_BOT);
    helpserv_define_func("MLIST", cmd_mlist, HlNone, CMD_NEED_BOT);
    helpserv_define_func("HLIST", cmd_hlist, HlNone, CMD_NEED_BOT);
    helpserv_define_func("TLIST", cmd_tlist, HlNone, CMD_NEED_BOT);
    helpserv_define_func("CLVL", cmd_clvl, HlManager, CMD_NEED_BOT);
    helpserv_define_func("PAGE", cmd_page, HlTrial, CMD_NEED_BOT);
    helpserv_define_func("SET", cmd_set, HlHelper, CMD_NEED_BOT);
    helpserv_define_func("STATS", cmd_stats, HlTrial, CMD_NEED_BOT);
    helpserv_define_func("STATSREPORT", cmd_statsreport, HlManager, CMD_NEED_BOT);
    helpserv_define_func("UNREGISTER", cmd_unregister, HlOwner, CMD_NEED_BOT);
    helpserv_define_func("READHELP", cmd_readhelp, HlOper, CMD_FROM_OPSERV_ONLY);
    helpserv_define_func("REGISTER", cmd_register, HlOper, CMD_FROM_OPSERV_ONLY);
    helpserv_define_func("MOVE", cmd_move, HlOper, CMD_FROM_OPSERV_ONLY|CMD_NEED_BOT);
    helpserv_define_func("BOTS", cmd_bots, HlOper, CMD_FROM_OPSERV_ONLY|CMD_IGNORE_EVENT);
    helpserv_define_func("EXPIRE", cmd_expire, HlOper, CMD_FROM_OPSERV_ONLY);
    helpserv_define_func("WEEKSTART", cmd_weekstart, HlTrial, CMD_NEED_BOT);

    helpserv_option_dict = dict_new();
    helpserv_define_option("PAGETARGET", opt_pagetarget_command);
    helpserv_define_option("ALERTPAGETARGET", opt_pagetarget_alert);
    helpserv_define_option("STATUSPAGETARGET", opt_pagetarget_status);
    helpserv_define_option("PAGE", opt_pagetype);
    helpserv_define_option("PAGETYPE", opt_pagetype);
    helpserv_define_option("ALERTPAGETYPE", opt_alert_page_type);
    helpserv_define_option("STATUSPAGETYPE", opt_status_page_type);
    helpserv_define_option("GREETING", opt_greeting);
    helpserv_define_option("REQOPENED", opt_req_opened);
    helpserv_define_option("REQASSIGNED", opt_req_assigned);
    helpserv_define_option("REQCLOSED", opt_req_closed);
    helpserv_define_option("IDLEDELAY", opt_idle_delay);
    helpserv_define_option("WHINEDELAY", opt_whine_delay);
    helpserv_define_option("WHINEINTERVAL", opt_whine_interval);
    helpserv_define_option("EMPTYINTERVAL", opt_empty_interval);
    helpserv_define_option("STALEDELAY", opt_stale_delay);
    helpserv_define_option("REQPERSIST", opt_request_persistence);
    helpserv_define_option("HELPERPERSIST", opt_helper_persistence);
    helpserv_define_option("NOTIFICATION", opt_notification);
    helpserv_define_option("REQMAXLEN", opt_req_maxlen);
    helpserv_define_option("IDWRAP", opt_id_wrap);
    helpserv_define_option("PRIVMSGONLY", opt_privmsg_only);
    helpserv_define_option("REQONJOIN", opt_req_on_join);
    helpserv_define_option("AUTOVOICE", opt_auto_voice);
    helpserv_define_option("AUTODEVOICE", opt_auto_devoice);

    helpserv_usercmd_dict = dict_new();
    dict_insert(helpserv_usercmd_dict, "WAIT", usercmd_wait);

    helpserv_bots_dict = dict_new();
    dict_set_free_data(helpserv_bots_dict, helpserv_free_bot);
    
    helpserv_bots_bychan_dict = dict_new();
    dict_set_free_data(helpserv_bots_bychan_dict, helpserv_botlist_free);

    helpserv_reqs_bynick_dict = dict_new();
    dict_set_free_data(helpserv_reqs_bynick_dict, helpserv_reqlist_free);
    helpserv_reqs_byhand_dict = dict_new();
    dict_set_free_data(helpserv_reqs_byhand_dict, helpserv_reqlist_free);

    helpserv_users_byhand_dict = dict_new();
    dict_set_free_data(helpserv_users_byhand_dict, helpserv_userlist_free);

    saxdb_register("HelpServ", helpserv_saxdb_read, helpserv_saxdb_write);
    helpserv_helpfile_read();

    /* Make up for downtime... though this will only really affect the
     * time_per_week */
    if (last_stats_update && (helpserv_next_stats(last_stats_update) < now)) {
        time_t statsrun = last_stats_update;
        while ((statsrun = helpserv_next_stats(statsrun)) < now)
            helpserv_run_stats(statsrun);
    }
    timeq_add(helpserv_next_stats(now), helpserv_timed_run_stats, NULL);

    reg_join_func(handle_join);
    reg_part_func(handle_part); /* also deals with kick */
    reg_nick_change_func(handle_nickchange);
    reg_del_user_func(handle_quit);

    reg_auth_func(handle_nickserv_auth);
    reg_handle_rename_func(handle_nickserv_rename);
    reg_unreg_func(handle_nickserv_unreg);
    reg_allowauth_func(handle_nickserv_allowauth);
    reg_failpw_func(handle_nickserv_failpw);
    reg_handle_merge_func(handle_nickserv_merge);

    reg_exit_func(helpserv_db_cleanup);

    helpserv_module = module_register("helpserv", HS_LOG, HELPSERV_HELPFILE_NAME, helpserv_expand_variable);
    modcmd_register(helpserv_module, "helpserv", cmd_helpserv, 1, MODCMD_REQUIRE_AUTHED|MODCMD_NO_LOG|MODCMD_NO_DEFAULT_BIND, "level", "800", NULL);
    message_register_table(msgtab);
    return 1;
}

int
helpserv_finalize(void) {
    return 1;
}

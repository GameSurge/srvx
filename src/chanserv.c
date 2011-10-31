/* chanserv.c - Channel service bot
 * Copyright 2000-2007 srvx Development Team
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

#include "chanserv.h"
#include "conf.h"
#include "global.h"
#include "modcmd.h"
#include "opserv.h" /* for opserv_bad_channel() */
#include "nickserv.h" /* for oper_outranks() */
#include "saxdb.h"
#include "timeq.h"

#define CHANSERV_CONF_NAME  "services/chanserv"

/* ChanServ options */
#define KEY_SUPPORT_CHANNEL         "support_channel"
#define KEY_SUPPORT_CHANNEL_MODES   "support_channel_modes"
#define KEY_DB_BACKUP_FREQ          "db_backup_freq"
#define KEY_INFO_DELAY              "info_delay"
#define KEY_MAX_GREETLEN            "max_greetlen"
#define KEY_ADJUST_THRESHOLD        "adjust_threshold"
#define KEY_ADJUST_DELAY            "adjust_delay"
#define KEY_CHAN_EXPIRE_FREQ        "chan_expire_freq"
#define KEY_CHAN_EXPIRE_DELAY       "chan_expire_delay"
#define KEY_DNR_EXPIRE_FREQ         "dnr_expire_freq"
#define KEY_MAX_CHAN_USERS          "max_chan_users"
#define KEY_MAX_CHAN_BANS           "max_chan_bans"
#define KEY_NICK                    "nick"
#define KEY_OLD_CHANSERV_NAME       "old_chanserv_name"
#define KEY_8BALL_RESPONSES         "8ball"
#define KEY_OLD_BAN_NAMES           "old_ban_names"
#define KEY_REFRESH_PERIOD          "refresh_period"
#define KEY_CTCP_SHORT_BAN_DURATION "ctcp_short_ban_duration"
#define KEY_CTCP_LONG_BAN_DURATION  "ctcp_long_ban_duration"
#define KEY_MAX_OWNED               "max_owned"
#define KEY_IRC_OPERATOR_EPITHET    "irc_operator_epithet"
#define KEY_NETWORK_HELPER_EPITHET  "network_helper_epithet"
#define KEY_SUPPORT_HELPER_EPITHET  "support_helper_epithet"
#define KEY_NODELETE_LEVEL          "nodelete_level"
#define KEY_MAX_USERINFO_LENGTH     "max_userinfo_length"
#define KEY_GIVEOWNERSHIP_PERIOD    "giveownership_timeout"

/* ChanServ database */
#define KEY_CHANNELS                "channels"
#define KEY_NOTE_TYPES              "note_types"

/* Note type parameters */
#define KEY_NOTE_OPSERV_ACCESS      "opserv_access"
#define KEY_NOTE_CHANNEL_ACCESS     "channel_access"
#define KEY_NOTE_SETTER_ACCESS      "setter_access"
#define KEY_NOTE_VISIBILITY         "visibility"
#define KEY_NOTE_VIS_PRIVILEGED     "privileged"
#define KEY_NOTE_VIS_CHANNEL_USERS  "channel_users"
#define KEY_NOTE_VIS_ALL            "all"
#define KEY_NOTE_MAX_LENGTH         "max_length"
#define KEY_NOTE_SETTER             "setter"
#define KEY_NOTE_NOTE               "note"

/* Do-not-register channels */
#define KEY_DNR             "dnr"
#define KEY_DNR_SET         "set"
#define KEY_DNR_SETTER      "setter"
#define KEY_DNR_REASON      "reason"

/* Channel data */
#define KEY_REGISTERED      "registered"
#define KEY_REGISTRAR       "registrar"
#define KEY_SUSPENDED       "suspended"
#define KEY_PREVIOUS        "previous"
#define KEY_SUSPENDER       "suspender"
#define KEY_ISSUED          "issued"
#define KEY_REVOKED         "revoked"
#define KEY_SUSPEND_EXPIRES "suspend_expires"
#define KEY_SUSPEND_REASON  "suspend_reason"
#define KEY_VISITED         "visited"
#define KEY_TOPIC           "topic"
#define KEY_GREETING        "greeting"
#define KEY_USER_GREETING   "user_greeting"
#define KEY_MODES           "modes"
#define KEY_FLAGS           "flags"
#define KEY_OPTIONS         "options"
#define KEY_USERS           "users"
#define KEY_BANS            "bans"
#define KEY_MAX             "max"
#define KEY_NOTES           "notes"
#define KEY_TOPIC_MASK      "topic_mask"
#define KEY_OWNER_TRANSFER  "owner_transfer"

/* User data */
#define KEY_LEVEL   "level"
#define KEY_INFO    "info"
#define KEY_SEEN    "seen"

/* Ban data */
#define KEY_OWNER       "owner"
#define KEY_REASON      "reason"
#define KEY_SET         "set"
#define KEY_DURATION    "duration"
#define KEY_EXPIRES     "expires"
#define KEY_TRIGGERED   "triggered"

#define CHANNEL_DEFAULT_FLAGS   (CHANNEL_OFFCHANNEL | CHANNEL_UNREVIEWED)
#define CHANNEL_PRESERVED_FLAGS (CHANNEL_UNREVIEWED)
#define CHANNEL_DEFAULT_OPTIONS "lmoooanpcnat"

/* Administrative messages */
static const struct message_entry msgtab[] = {
    { "CSMSG_CHANNELS_EXPIRED", "%i channels expired." },

/* Channel registration */
    { "CSMSG_REG_SUCCESS", "You now have ownership of $b%s$b." },
    { "CSMSG_PROXY_SUCCESS", "%s now has ownership of $b%s$b." },
    { "CSMSG_ALREADY_REGGED", "$b%s$b is registered to someone else." },
    { "CSMSG_MUST_BE_OPPED", "You must be a channel operator in $b%s$b to register it." },
    { "CSMSG_PROXY_FORBIDDEN", "You may not register a channel for someone else." },
    { "CSMSG_OWN_TOO_MANY", "%s already owns enough channels (at least %d); use FORCE to override." },

/* Do-not-register channels */
    { "CSMSG_NOT_DNR", "$b%s$b is not a valid channel name or *account." },
    { "CSMSG_DNR_SEARCH_RESULTS", "The following do-not-registers were found:" },
    { "CSMSG_DNR_INFO", "$b%s$b is do-not-register (by $b%s$b): %s" },
    { "CSMSG_DNR_INFO_SET", "$b%s$b is do-not-register (set %s by $b%s$b): %s" },
    { "CSMSG_DNR_INFO_SET_EXPIRES", "$b%s$b is do-not-register (set %s by $b%s$b; expires %s): %s" },
    { "CSMSG_MORE_DNRS", "%d more do-not-register entries skipped." },
    { "CSMSG_DNR_CHANNEL", "Only network staff may register $b%s$b." },
    { "CSMSG_DNR_CHANNEL_MOVE", "Only network staff may move $b%s$b." },
    { "CSMSG_DNR_ACCOUNT", "Only network staff may register channels to $b%s$b." },
    { "CSMSG_NOREGISTER_CHANNEL", "$b%s$b has been added to the do-not-register list." },
    { "CSMSG_NO_SUCH_DNR", "$b%s$b is not in the do-not-register list." },
    { "CSMSG_DNR_REMOVED", "$b%s$b has been removed from the do-not-register list." },
    { "CSMSG_DNR_BAD_ACTION", "$b%s$b is not a recognized do-not-register action." },
    { "CSMSG_DNR_SEARCH_RESULTS", "The following do-not-registers were found:" },

/* Channel unregistration */
    { "CSMSG_UNREG_SUCCESS", "$b%s$b has been unregistered." },
    { "CSMSG_UNREG_NODELETE", "$b%s$b is protected from unregistration." },
    { "CSMSG_CHAN_SUSPENDED", "$b$C$b access to $b%s$b has been temporarily suspended (%s)." },
    { "CSMSG_CONFIRM_UNREG", "To confirm this unregistration, you must use 'unregister %s'." },

/* Channel moving */
    { "CSMSG_MOVE_SUCCESS", "Channel registration has been moved to $b%s$b." },
    { "CSMSG_MOVE_NODELETE", "$b%s$b is protected from unregistration, and cannot be moved." },

/* Channel merging */
    { "CSMSG_MERGE_SUCCESS", "Channel successfully merged into $b%s$b." },
    { "CSMSG_MERGE_SELF", "Merging cannot be performed if the source and target channels are the same." },
    { "CSMSG_MERGE_NODELETE", "You may not merge a channel that is marked NoDelete." },
    { "CSMSG_MERGE_SUSPENDED", "Merging cannot be performed if the source or target channel is suspended." },
    { "CSMSG_MERGE_NOT_OWNER", "You must be the owner of the target channel (or a helper) to merge into the channel." },

/* Handle unregistration */
    { "CSMSG_HANDLE_UNREGISTERED", "As a result of your account unregistration, you have been deleted from all of your channels' userlists." },

/* Error messages */
    { "CSMSG_NOT_USER", "You lack access to $b%s$b." },
    { "CSMSG_NO_CHAN_USER", "%s lacks access to $b%s$b." },
    { "CSMSG_NO_ACCESS", "You lack sufficient access to use this command." },
    { "CSMSG_NOT_REGISTERED", "$b%s$b has not been registered with $b$C$b." },
    { "CSMSG_MAXIMUM_BANS", "This channel has reached the ban count limit of $b%d$b." },
    { "CSMSG_MAXIMUM_USERS", "This channel has reached the user count limit of $b%d$b." },
    { "CSMSG_ILLEGAL_CHANNEL", "$b%s$b is an illegal channel, and cannot be registered." },
    { "CSMSG_GODMODE_UP", "You may not use $b%s$b to op yourself unless you are on the user list.  Use the $bop$b command instead." },
    { "CSMSG_ALREADY_OPPED", "You are already opped in $b%s$b." },
    { "CSMSG_ALREADY_VOICED", "You are already voiced in $b%s$b." },
    { "CSMSG_ALREADY_DOWN", "You are not opped or voiced in $b%s$b." },
    { "CSMSG_ALREADY_OPCHANNED", "There has been no net.join since the last opchan in $b%s$b." },
    { "CSMSG_OUT_OF_CHANNEL", "For some reason I don't seem to be in $b%s$b." },
    { "CSMSG_OPCHAN_DONE", "I have (re-)opped myself in $b%s$b." },

/* Removing yourself from a channel. */
    { "CSMSG_NO_OWNER_DELETEME", "You cannot delete your owner access in $b%s$b." },
    { "CSMSG_CONFIRM_DELETEME", "To really remove yourself, you must use 'deleteme %s'." },
    { "CSMSG_DELETED_YOU", "Your $b%d$b access has been deleted from $b%s$b." },

/* User management */
    { "CSMSG_ADDED_USER", "Added %s to the %s user list with access %d." },
    { "CSMSG_DELETED_USER", "Deleted %s (with access %d) from the %s user list." },
    { "CSMSG_BAD_RANGE", "Invalid access range; minimum (%d) must be greater than maximum (%d)." },
    { "CSMSG_DELETED_USERS", "Deleted accounts matching $b%s$b with access from $b%d$b to $b%d$b from the %s user list." },
    { "CSMSG_TRIMMED_USERS", "Trimmed $b%d users$b with access from %d to %d from the %s user list who were inactive for at least %s." },
    { "CSMSG_INCORRECT_ACCESS", "%s has access $b%d$b, not %s." },
    { "CSMSG_USER_EXISTS", "%s is already on the $b%s$b user list (with access %d)." },
    { "CSMSG_CANNOT_TRIM", "You must include a minimum inactivity duration of at least 60 seconds to trim." },

    { "CSMSG_NO_SELF_CLVL", "You cannot change your own access." },
    { "CSMSG_NO_BUMP_ACCESS", "You cannot give users access greater than or equal to your own." },
    { "CSMSG_MULTIPLE_OWNERS", "There is more than one owner in %s; please use $bCLVL$b, $bDELOWNER$b and/or $bADDOWNER$b instead." },
    { "CSMSG_TRANSFER_WAIT", "You must wait %s before you can give ownership of $b%s$b to someone else." },
    { "CSMSG_NO_TRANSFER_SELF", "You cannot give ownership to your own account." },
    { "CSMSG_CONFIRM_GIVEOWNERSHIP", "To really give ownership to $b%1$s$b, you must use 'giveownership *%1$s %2$s'." },
    { "CSMSG_OWNERSHIP_GIVEN", "Ownership of $b%s$b has been transferred to account $b%s$b." },

/* Ban management */
    { "CSMSG_BAN_ADDED", "Permanently banned $b%s$b from %s." },
    { "CSMSG_TIMED_BAN_ADDED", "Banned $b%s$b from %s for %s." },
    { "CSMSG_KICK_BAN_DONE", "Kickbanned $b%s$b from %s." },
    { "CSMSG_BAN_DONE", "Banned $b%s$b from %s." },
    { "CSMSG_REASON_CHANGE", "Reason for ban $b%s$b changed." },
    { "CSMSG_BAN_EXTENDED", "Extended ban for $b%s$b expires in %s." },
    { "CSMSG_BAN_REMOVED", "Matching ban(s) for $b%s$b removed." },
    { "CSMSG_TRIMMED_BANS", "Trimmed $b%d bans$b from the %s ban list that were inactive for at least %s." },
    { "CSMSG_REDUNDANT_BAN", "$b%s$b is already banned in %s." },
    { "CSMSG_DURATION_TOO_LOW", "Timed bans must last for at least 15 seconds." },
    { "CSMSG_DURATION_TOO_HIGH", "Timed bans must last for less than 2 years." },
    { "CSMSG_LAME_MASK", "$b%s$b is a little too general. Try making it more specific." },
    { "CSMSG_MASK_PROTECTED", "Sorry, ban for $b%s$b conflicts with a protected user's hostmask." },
    { "CSMSG_NO_MATCHING_USERS", "No one in $b%s$b has a hostmask matching $b%s$b." },
    { "CSMSG_BAN_NOT_FOUND", "Sorry, no ban found for $b%s$b." },
    { "CSMSG_BANLIST_FULL", "The $b%s$b channel ban list is $bfull$b." },

    { "CSMSG_INVALID_TRIM", "$b%s$b isn't a valid trim target." },

/* Channel management */
    { "CSMSG_CHANNEL_OPENED", "$b%s$b has been opened." },
    { "CSMSG_WIPED_INFO_LINE", "Removed $b%s$b's infoline in $b%s$b." },
    { "CSMSG_RESYNCED_USERS", "Synchronized users in $b%s$b with the userlist." },

    { "CSMSG_TOPIC_SET", "Topic is now '%s'." },
    { "CSMSG_NO_TOPIC", "$b%s$b does not have a default topic." },
    { "CSMSG_TOPICMASK_CONFLICT1", "I do not know how to make that topic work with the current topic mask in $b%s$b, which is: %s" },
    { "CSMSG_TOPICMASK_CONFLICT2", "Please make sure your topic is at most %d characters and matches the topic mask pattern." },
    { "CSMSG_TOPIC_LOCKED", "The %s topic is locked." },
    { "CSMSG_MASK_BUT_NO_TOPIC", "Warning: $b%s$b does not have a default topic, but you just set the topic mask." },
    { "CSMSG_TOPIC_MISMATCH", "Warning: The default topic for $b%s$b does not match the topic mask; changing it anyway." },

    { "CSMSG_MODES_SET", "Channel modes are now $b%s$b." },
    { "CSMSG_DEFAULTED_MODES", "Channel modes for $b%s$b are set to their defaults." },
    { "CSMSG_NO_MODES", "$b%s$b does not have any default modes." },
    { "CSMSG_MODE_LOCKED", "Modes conflicting with $b%s$b are not allowed in %s." },
    { "CSMSG_CANNOT_SET", "That setting is above your current level, so you cannot change it." },
    { "CSMSG_OWNER_DEFAULTS", "You must have access 500 in %s to reset it to the default options." },
    { "CSMSG_CONFIRM_DEFAULTS", "To reset %s's settings to the defaults, you must use 'set defaults %s'." },
    { "CSMSG_SETTINGS_DEFAULTED", "All settings for %s have been reset to default values." },
    { "CSMSG_BAD_SETLEVEL", "You cannot change any setting to above your level." },
    { "CSMSG_BAD_GIVEVOICE", "You cannot change GiveVoice to above GiveOps (%d)." },
    { "CSMSG_BAD_GIVEOPS", "You cannot change GiveOps to below GiveVoice (%d)." },
    { "CSMSG_BAD_SETTERS", "You cannot change Setters to above your level." },
    { "CSMSG_INVALID_MODE_LOCK", "$b%s$b is an invalid mode lock." },
    { "CSMSG_INVALID_NUMERIC",   "$b%d$b is not a valid choice.  Choose one:" },
    { "CSMSG_SET_DEFAULT_TOPIC", "$bDefaultTopic$b %s" },
    { "CSMSG_SET_TOPICMASK",     "$bTopicMask   $b %s" },
    { "CSMSG_SET_GREETING",      "$bGreeting    $b %s" },
    { "CSMSG_SET_USERGREETING",  "$bUserGreeting$b %s" },
    { "CSMSG_SET_MODES",         "$bModes       $b %s" },
    { "CSMSG_SET_NODELETE",      "$bNoDelete    $b %s" },
    { "CSMSG_SET_DYNLIMIT",      "$bDynLimit    $b %s" },
    { "CSMSG_SET_OFFCHANNEL",    "$bOffChannel  $b %s" },
    { "CSMSG_SET_USERINFO",      "$bUserInfo    $b %d" },
    { "CSMSG_SET_GIVE_VOICE",    "$bGiveVoice   $b %d" },
    { "CSMSG_SET_TOPICSNARF",    "$bTopicSnarf  $b %d" },
    { "CSMSG_SET_INVITEME",      "$bInviteMe    $b %d" },
    { "CSMSG_SET_ENFOPS",        "$bEnfOps      $b %d" },
    { "CSMSG_SET_GIVE_OPS",      "$bGiveOps     $b %d" },
    { "CSMSG_SET_ENFMODES",      "$bEnfModes    $b %d" },
    { "CSMSG_SET_ENFTOPIC",      "$bEnfTopic    $b %d" },
    { "CSMSG_SET_PUBCMD",        "$bPubCmd      $b %d" },
    { "CSMSG_SET_SETTERS",       "$bSetters     $b %d" },
    { "CSMSG_SET_CTCPUSERS",     "$bCTCPUsers   $b %d" },
    { "CSMSG_SET_PROTECT",       "$bProtect     $b %d - %s" },
    { "CSMSG_SET_TOYS",          "$bToys        $b %d - %s" },
    { "CSMSG_SET_CTCPREACTION",  "$bCTCPReaction$b %d - %s" },
    { "CSMSG_SET_TOPICREFRESH",  "$bTopicRefresh$b %d - %s" },
    { "CSMSG_SET_UNREVIEWED",    "$bUnreviewed  $b %s" },
    { "CSMSG_USET_NOAUTOOP",     "$bNoAutoOp    $b %s" },
    { "CSMSG_USET_NOAUTOVOICE",  "$bNoAutoVoice $b %s" },
    { "CSMSG_USET_AUTOINVITE",   "$bAutoInvite  $b %s" },
    { "CSMSG_USET_INFO",         "$bInfo        $b %s" },

    { "CSMSG_USER_PROTECTED", "Sorry, $b%s$b is protected." },
    { "CSMSG_OPBY_LOCKED", "You may not op users who lack op or greater access." },
    { "CSMSG_PROCESS_FAILED", "$b$C$b could not process some of the nicks you provided." },
    { "CSMSG_OPPED_USERS", "Opped users in $b%s$b." },
    { "CSMSG_DEOPPED_USERS", "Deopped users in $b%s$b." },
    { "CSMSG_VOICED_USERS", "Voiced users in $b%s$b." },
    { "CSMSG_DEVOICED_USERS", "Devoiced users in $b%s$b." },
    { "CSMSG_PROTECT_ALL", "Non-users and users will be protected from those of equal or lower access." },
    { "CSMSG_PROTECT_EQUAL", "Users will be protected from those of equal or lower access." },
    { "CSMSG_PROTECT_LOWER", "Users will be protected from those of lower access." },
    { "CSMSG_PROTECT_NONE", "No users will be protected." },
    { "CSMSG_TOYS_DISABLED", "Toys are completely disabled." },
    { "CSMSG_TOYS_PRIVATE", "Toys will only reply privately." },
    { "CSMSG_TOYS_PUBLIC", "Toys will reply publicly." },
    { "CSMSG_TOPICREFRESH_NEVER", "Never refresh topic." },
    { "CSMSG_TOPICREFRESH_3_HOURS", "Refresh every 3 hours." },
    { "CSMSG_TOPICREFRESH_6_HOURS", "Refresh every 6 hours." },
    { "CSMSG_TOPICREFRESH_12_HOURS", "Refresh every 12 hours." },
    { "CSMSG_TOPICREFRESH_24_HOURS", "Refresh every 24 hours." },
    { "CSMSG_CTCPREACTION_KICK", "Kick on disallowed CTCPs" },
    { "CSMSG_CTCPREACTION_KICKBAN", "Kickban on disallowed CTCPs" },
    { "CSMSG_CTCPREACTION_SHORTBAN",  "Short timed ban on disallowed CTCPs" },
    { "CSMSG_CTCPREACTION_LONGBAN", "Long timed ban on disallowed CTCPs" },

    { "CSMSG_INVITED_USER", "Invited $b%s$b to join %s." },
    { "CSMSG_INVITING_YOU_REASON", "$b%s$b invites you to join %s: %s" },
    { "CSMSG_INVITING_YOU", "$b%s$b invites you to join %s." },
    { "CSMSG_ALREADY_PRESENT", "%s is already in $b%s$b." },
    { "CSMSG_YOU_ALREADY_PRESENT", "You are already in $b%s$b." },
    { "CSMSG_LOW_CHANNEL_ACCESS", "You lack sufficient access in %s for $S to invite you." },
    { "CSMSG_INFOLINE_TOO_LONG", "Your infoline may not exceed %u characters." },
    { "CSMSG_BAD_INFOLINE", "You may not use the character \\%03o in your infoline." },

    { "CSMSG_KICK_DONE", "Kicked $b%s$b from %s." },
    { "CSMSG_NO_BANS", "No channel bans found on $b%s$b." },
    { "CSMSG_BANS_REMOVED", "Removed all channel bans from $b%s$b." },

/* Channel userlist */
    { "CSMSG_ACCESS_ALL_HEADER", "%s users from level %d to %d:" },
    { "CSMSG_ACCESS_SEARCH_HEADER", "%s users from level %d to %d matching %s:" },
    { "CSMSG_INVALID_ACCESS", "$b%s$b is an invalid access level." },
    { "CSMSG_CHANGED_ACCESS", "%s now has access $b%d$b in %s." },

/* Channel note list */
    { "CSMSG_NOTELIST_HEADER", "Notes for $b%s$b:" },
    { "CSMSG_REPLACED_NOTE", "Replaced old $b%s$b note on %s (set by %s): %s" },
    { "CSMSG_NOTE_FORMAT", "%s (set by %s): %s" },
    { "CSMSG_NOTELIST_END", "End of notes for $b%s$b." },
    { "CSMSG_NOTELIST_EMPTY", "There are no (visible) notes for $b%s$b." },
    { "CSMSG_NO_SUCH_NOTE", "Channel $b%s$b does not have a note named $b%s$b." },
    { "CSMSG_BAD_NOTE_TYPE", "Note type $b%s$b does not exist." },
    { "CSMSG_NOTE_SET", "Note $b%s$b set in channel $b%s$b." },
    { "CSMSG_NOTE_REMOVED", "Note $b%s$b removed in channel $b%s$b." },
    { "CSMSG_BAD_NOTE_ACCESS", "$b%s$b is not a valid note access type." },
    { "CSMSG_BAD_MAX_LENGTH", "$b%s$b is not a valid maximum length (must be between 20 and 450 inclusive)." },
    { "CSMSG_NOTE_MODIFIED", "Note type $b%s$b modified." },
    { "CSMSG_NOTE_CREATED", "Note type $b%s$b created." },
    { "CSMSG_NOTE_TYPE_USED", "Note type $b%s$b is in use; give the FORCE argument to delete it." },
    { "CSMSG_NOTE_DELETED", "Note type $b%s$b deleted." },

/* Channel [un]suspension */
    { "CSMSG_ALREADY_SUSPENDED", "$b%s$b is already suspended." },
    { "CSMSG_NOT_SUSPENDED", "$b%s$b is not suspended." },
    { "CSMSG_SUSPENDED", "$b$C$b access to $b%s$b has been temporarily suspended." },
    { "CSMSG_UNSUSPENDED", "$b$C$b access to $b%s$b has been restored." },
    { "CSMSG_SUSPEND_NODELETE", "$b%s$b is protected from unregistration, and cannot be suspended." },
    { "CSMSG_USER_SUSPENDED", "$b%s$b's access to $b%s$b has been suspended." },
    { "CSMSG_USER_UNSUSPENDED", "$b%s$b's access to $b%s$b has been restored." },

/* Access information */
    { "CSMSG_IS_CHANSERV", "$b$C$b is the $bchannel service bot$b." },
    { "CSMSG_MYACCESS_SELF_ONLY", "You may only see the list of infolines for yourself (by using $b%s$b with no arguments)." },
    { "CSMSG_SQUAT_ACCESS", "$b%s$b does not have access to any channels." },
    { "CSMSG_INFOLINE_LIST", "Showing all channel entries for account $b%s$b:" },
    { "CSMSG_USER_NO_ACCESS", "%s lacks access to %s." },
    { "CSMSG_USER_HAS_ACCESS", "%s has access $b%d$b in %s." },
    { "CSMSG_HELPER_NO_ACCESS", "%s lacks access to %s but has $bsecurity override$b enabled." },
    { "CSMSG_HELPER_HAS_ACCESS", "%s has access $b%d$b in %s and has $bsecurity override$b enabled." },
    { "CSMSG_LAZY_SMURF_TARGET", "%s is %s ($bIRCOp$b; not logged in)." },
    { "CSMSG_SMURF_TARGET", "%s is %s ($b%s$b)." },
    { "CSMSG_OPERATOR_TITLE", "IRC operator" },
    { "CSMSG_UC_H_TITLE", "network helper" },
    { "CSMSG_LC_H_TITLE", "support helper" },
    { "CSMSG_LAME_SMURF_TARGET", "%s is an IRC operator." },

/* Seen information */
    { "CSMSG_NEVER_SEEN", "%s has never been seen in $b%s$b." },
    { "CSMSG_USER_SEEN", "%s was last seen in $b%s$b %s ago." },
    { "CSMSG_USER_VACATION", "%s is currently on vacation." },
    { "CSMSG_USER_PRESENT", "%s is in the channel $bright now$b." },

/* Names information */
    { "CSMSG_CHANNEL_NAMES", "Users in $b%s$b:%s" },
    { "CSMSG_END_NAMES", "End of names in $b%s$b" },

/* Channel information */
    { "CSMSG_CHANNEL_INFO", "$b%s$b Information:" },
    { "CSMSG_CHANNEL_TOPIC", "$bDefault Topic:       $b%s" },
    { "CSMSG_CHANNEL_MODES", "$bMode Lock:           $b%s" },
    { "CSMSG_CHANNEL_NOTE", "$b%s:%*s$b%s" },
    { "CSMSG_CHANNEL_MAX", "$bRecord Visitors:     $b%i" },
    { "CSMSG_CHANNEL_OWNER", "$bOwner:               $b%s" },
    { "CSMSG_CHANNEL_BANS", "$bBan Count:           $b%i" },
    { "CSMSG_CHANNEL_USERS", "$bTotal User Count:    $b%i" },
    { "CSMSG_CHANNEL_REGISTRAR", "$bRegistrar:           $b%s" },
    { "CSMSG_CHANNEL_SUSPENDED", "$b%s$b is suspended:" },
    { "CSMSG_CHANNEL_HISTORY", "Suspension history for $b%s$b:" },
    { "CSMSG_CHANNEL_SUSPENDED_0", " by %s: %s" },
    { "CSMSG_CHANNEL_SUSPENDED_1", " by %s; expires in %s: %s" },
    { "CSMSG_CHANNEL_SUSPENDED_2", " by %s; expired %s ago: %s" },
    { "CSMSG_CHANNEL_SUSPENDED_3", " by %s; revoked %s ago: %s" },
    { "CSMSG_CHANNEL_SUSPENDED_4", " %s ago by %s: %s" },
    { "CSMSG_CHANNEL_SUSPENDED_5", " %s ago by %s; expires in %s: %s" },
    { "CSMSG_CHANNEL_SUSPENDED_6", " %s ago by %s; expired %s ago: %s" },
    { "CSMSG_CHANNEL_SUSPENDED_7", " %s ago by %s; revoked %s ago: %s" },
    { "CSMSG_CHANNEL_REGISTERED", "$bRegistered:          $b%s ago." },
    { "CSMSG_CHANNEL_VISITED", "$bVisited:             $b%s ago." },

    { "CSMSG_PEEK_INFO", "$b%s$b Status:" },
    { "CSMSG_PEEK_TOPIC", "$bTopic:          $b%s" },
    { "CSMSG_PEEK_MODES", "$bModes:          $b%s" },
    { "CSMSG_PEEK_USERS", "$bTotal users:    $b%d (%d ops, %d voices, %d regulars)" },
    { "CSMSG_PEEK_OPS", "$bOps:$b" },
    { "CSMSG_PEEK_NO_OPS", "$bOps:            $bNone present" },

/* Network information */
    { "CSMSG_NETWORK_INFO", "Network Information:" },
    { "CSMSG_NETWORK_SERVERS", "$bServers:             $b%i" },
    { "CSMSG_NETWORK_USERS",   "$bTotal Users:         $b%i" },
    { "CSMSG_NETWORK_BANS",    "$bTotal Ban Count:     $b%i" },
    { "CSMSG_NETWORK_CHANUSERS", "$bTotal User Count:    $b%i" },
    { "CSMSG_NETWORK_OPERS",   "$bIRC Operators:       $b%i" },
    { "CSMSG_NETWORK_CHANNELS","$bRegistered Channels: $b%i" },
    { "CSMSG_SERVICES_UPTIME", "$bServices Uptime:     $b%s" },
    { "CSMSG_BURST_LENGTH",    "$bLast Burst Length:   $b%s" },

/* Staff list */
    { "CSMSG_NETWORK_STAFF", "$bOnline Network Staff:$b" },
    { "CSMSG_STAFF_OPERS", "$bIRC Operators:$b" },
    { "CSMSG_STAFF_HELPERS", "$bHelpers:$b" },

/* Channel searches */
    { "CSMSG_ACTION_INVALID", "$b%s$b is not a recognized search action." },
    { "CSMSG_UNVISITED_HEADER", "Showing a maximum of %d channels unvisited for $b%s$b:" },
    { "CSMSG_UNVISITED_DATA", "%s: $b%s$b" },
    { "CSMSG_CHANNEL_SEARCH_RESULTS", "The following channels were found:" },

/* Channel configuration */
    { "CSMSG_INVALID_OPTION", "$b%s$b is not a valid %s option." },
    { "CSMSG_INVALID_CFLAG", "$b%s$b is not a recognized channel flag." },
    { "CSMSG_CHANNEL_OPTIONS", "Channel Options:" },
    { "CSMSG_GREETING_TOO_LONG", "Your greeting ($b%d$b characters) must be shorter than $b%d$b characters." },

/* User settings */
    { "CSMSG_USER_OPTIONS", "User Options:" },
    { "CSMSG_USER_PROTECTED_2", "That user is protected." },

/* Toys */
    { "CSMSG_UNF_RESPONSE", "I don't want to be part of your sick fantasies!" },
    { "CSMSG_PING_RESPONSE", "Pong!" },
    { "CSMSG_WUT_RESPONSE", "wut" },
    { "CSMSG_BAD_NUMBER", "$b%s$b is an invalid number.  Please use a number greater than 1 with this command." },
    { "CSMSG_BAD_DIE_FORMAT", "I do not understand $b%s$b.  Please use either a single number or standard 4d6+3 format." },
    { "CSMSG_BAD_DICE_COUNT", "%lu is too many dice.  Please use at most %lu." },
    { "CSMSG_DICE_ROLL", "The total is $b%lu$b from rolling %lud%lu+%lu." },
    { "CSMSG_DIE_ROLL", "A $b%lu$b shows on the %lu-sided die." },
    { "CSMSG_HUGGLES_HIM", "\001ACTION huggles %s\001" },
    { "CSMSG_HUGGLES_YOU", "\001ACTION huggles you\001" },

/* Other things */
    { "CSMSG_EVENT_SEARCH_RESULTS", "The following channel events were found:" },
    { NULL, NULL }
};

/* eject_user and unban_user flags */
#define ACTION_KICK             0x0001
#define ACTION_BAN              0x0002
#define ACTION_ADD_BAN          0x0004
#define ACTION_ADD_TIMED_BAN    0x0008
#define ACTION_UNBAN            0x0010
#define ACTION_DEL_BAN          0x0020

/* The 40 allows for [+-ntlksimprD] and lots of fudge factor. */
#define MODELEN     40 + KEYLEN
#define PADLEN      21
#define ACCESSLEN   10

#define CSFUNC_ARGS user, channel, argc, argv, cmd

#define CHANSERV_FUNC(NAME) MODCMD_FUNC(NAME)
#define CHANSERV_SYNTAX() svccmd_send_help(user, chanserv, cmd)
#define REQUIRE_PARAMS(N)   if(argc < (N)) {            \
        reply("MSG_MISSING_PARAMS", argv[0]); \
        CHANSERV_SYNTAX(); \
        return 0; }

DECLARE_LIST(dnrList, struct do_not_register *);
DEFINE_LIST(dnrList, struct do_not_register *)

static int eject_user(struct userNode *user, struct chanNode *channel, unsigned int argc, char *argv[], struct svccmd *cmd, int action);

struct userNode *chanserv;
dict_t note_types;
int off_channel;
static dict_t plain_dnrs, mask_dnrs, handle_dnrs;
static struct log_type *CS_LOG;

static struct
{
    struct channelList  support_channels;
    struct mod_chanmode default_modes;

    unsigned long   db_backup_frequency;
    unsigned long   channel_expire_frequency;
    unsigned long   dnr_expire_frequency;

    unsigned long   info_delay;
    unsigned long   adjust_delay;
    unsigned long   channel_expire_delay;
    unsigned int    nodelete_level;

    unsigned int    adjust_threshold;
    int             join_flood_threshold;

    unsigned int    greeting_length;
    unsigned int    refresh_period;
    unsigned int    giveownership_period;

    unsigned int    max_owned;
    unsigned int    max_chan_users;
    unsigned int    max_chan_bans;
    unsigned int    max_userinfo_length;

    struct string_list  *set_shows;
    struct string_list  *eightball;
    struct string_list  *old_ban_names;

    const char          *ctcp_short_ban_duration;
    const char          *ctcp_long_ban_duration;

    const char          *irc_operator_epithet;
    const char          *network_helper_epithet;
    const char          *support_helper_epithet;
} chanserv_conf;

struct listData
{
    struct userNode *user;
    struct userNode *bot;
    struct chanNode *channel;
    const char      *search;
    unsigned short  lowest;
    unsigned short  highest;
    struct userData **users;
    struct helpfile_table table;
};

enum note_access_type
{
    NOTE_SET_CHANNEL_ACCESS,
    NOTE_SET_CHANNEL_SETTER,
    NOTE_SET_PRIVILEGED
};

enum note_visible_type
{
    NOTE_VIS_ALL,
    NOTE_VIS_CHANNEL_USERS,
    NOTE_VIS_PRIVILEGED
};

struct note_type
{
    enum note_access_type set_access_type;
    union {
        unsigned int     min_opserv;
        unsigned short   min_ulevel;
    } set_access;
    enum note_visible_type visible_type;
    unsigned int         max_length;
    unsigned int         refs;
    char                 name[1];
};

struct note
{
    struct note_type     *type;
    char                 setter[NICKSERV_HANDLE_LEN+1];
    char                 note[1];
};

static unsigned int registered_channels;
static unsigned int banCount;

static const struct {
    char *name;
    char *title;
    unsigned short level;
    char ch;
} accessLevels[] = {
    { "peon", "Peon", UL_PEON, '+' },
    { "op", "Op", UL_OP, '@' },
    { "master", "Master", UL_MASTER, '%' },
    { "coowner", "Coowner", UL_COOWNER, '*' },
    { "owner", "Owner", UL_OWNER, '!' },
    { "helper", "BUG:", UL_HELPER, 'X' }
};

static const struct {
    char *format_name;
    char *db_name;
    unsigned short default_value;
    unsigned int old_idx;
    unsigned int old_flag;
    unsigned short flag_value;
} levelOptions[] = {
    { "CSMSG_SET_GIVE_VOICE", "givevoice", 100, ~0, CHANNEL_VOICE_ALL, 0 },
    { "CSMSG_SET_GIVE_OPS", "giveops", 200, 2, 0, 0 },
    { "CSMSG_SET_ENFOPS", "enfops", 300, 1, 0, 0 },
    { "CSMSG_SET_ENFMODES", "enfmodes", 200, 3, 0, 0 },
    { "CSMSG_SET_ENFTOPIC", "enftopic", 200, 4, 0, 0 },
    { "CSMSG_SET_PUBCMD", "pubcmd", 0, 5, 0, 0 },
    { "CSMSG_SET_SETTERS", "setters", 400, 7, 0, 0 },
    { "CSMSG_SET_CTCPUSERS", "ctcpusers", 0, 9, 0, 0 },
    { "CSMSG_SET_USERINFO", "userinfo", 1, ~0, CHANNEL_INFO_LINES, 1 },
    { "CSMSG_SET_INVITEME", "inviteme", 1, ~0, CHANNEL_PEON_INVITE, 200 },
    { "CSMSG_SET_TOPICSNARF", "topicsnarf", 501, ~0, CHANNEL_TOPIC_SNARF, 1 }
};

struct charOptionValues {
    char value;
    char *format_name;
} protectValues[] = {
    { 'a', "CSMSG_PROTECT_ALL" },
    { 'e', "CSMSG_PROTECT_EQUAL" },
    { 'l', "CSMSG_PROTECT_LOWER" },
    { 'n', "CSMSG_PROTECT_NONE" }
}, toysValues[] = {
    { 'd', "CSMSG_TOYS_DISABLED" },
    { 'n', "CSMSG_TOYS_PRIVATE" },
    { 'p', "CSMSG_TOYS_PUBLIC" }
}, topicRefreshValues[] = {
    { 'n', "CSMSG_TOPICREFRESH_NEVER" },
    { '1', "CSMSG_TOPICREFRESH_3_HOURS" },
    { '2', "CSMSG_TOPICREFRESH_6_HOURS" },
    { '3', "CSMSG_TOPICREFRESH_12_HOURS" },
    { '4', "CSMSG_TOPICREFRESH_24_HOURS" }
}, ctcpReactionValues[] = {
    { 'k', "CSMSG_CTCPREACTION_KICK" },
    { 'b', "CSMSG_CTCPREACTION_KICKBAN" },
    { 't', "CSMSG_CTCPREACTION_SHORTBAN" },
    { 'T', "CSMSG_CTCPREACTION_LONGBAN" }
};

static const struct {
    char *format_name;
    char *db_name;
    char default_value;
    unsigned int old_idx;
    unsigned char count;
    struct charOptionValues *values;
} charOptions[] = {
    { "CSMSG_SET_PROTECT", "protect", 'l', 0, ArrayLength(protectValues), protectValues },
    { "CSMSG_SET_TOYS", "toys", 'p', 6, ArrayLength(toysValues), toysValues },
    { "CSMSG_SET_TOPICREFRESH", "topicrefresh", 'n', 8, ArrayLength(topicRefreshValues), topicRefreshValues },
    { "CSMSG_SET_CTCPREACTION", "ctcpreaction", 't', 10, ArrayLength(ctcpReactionValues), ctcpReactionValues }
};

struct userData *helperList;
struct chanData *channelList;
static struct module *chanserv_module;
static unsigned int userCount;

#define GetChannelUser(channel, handle) _GetChannelUser(channel, handle, 1, 0)
#define GetChannelAccess(channel, handle) _GetChannelUser(channel, handle, 0, 0)
#define GetTrueChannelAccess(channel, handle) _GetChannelUser(channel, handle, 0, 1)

unsigned short
user_level_from_name(const char *name, unsigned short clamp_level)
{
    unsigned int level = 0, ii;
    if(isdigit(name[0]))
        level = strtoul(name, NULL, 10);
    else for(ii = 0; (ii < ArrayLength(accessLevels)) && !level; ++ii)
        if(!irccasecmp(name, accessLevels[ii].name))
            level = accessLevels[ii].level;
    if(level > clamp_level)
        return 0;
    return level;
}

int
parse_level_range(unsigned short *minl, unsigned short *maxl, const char *arg)
{
    char *sep;
    *minl = strtoul(arg, &sep, 10);
    if(*sep == '\0')
    {
        *maxl = *minl;
        return 1;
    }
    else if(*sep == '-')
    {
        *maxl = strtoul(sep+1, &sep, 10);
        return *sep == '\0';
    }
    else
        return 0;
}

struct userData*
_GetChannelUser(struct chanData *channel, struct handle_info *handle, int override, int allow_suspended)
{
    struct userData *uData, **head;

    if(!channel || !handle)
        return NULL;

    if(override && HANDLE_FLAGGED(handle, HELPING)
       && ((handle->opserv_level >= chanserv_conf.nodelete_level) || !IsProtected(channel)))
    {
        for(uData = helperList;
            uData && uData->handle != handle;
            uData = uData->next);

        if(!uData)
        {
            uData = calloc(1, sizeof(struct userData));
            uData->handle = handle;

            uData->access = UL_HELPER;
            uData->seen = 0;

            uData->info = NULL;

            uData->prev = NULL;
            uData->next = helperList;
            if(helperList)
                helperList->prev = uData;
            helperList = uData;
        }

        head = &helperList;
    }
    else
    {
        for(uData = channel->users; uData; uData = uData->next)
            if((uData->handle == handle) && (allow_suspended || !IsUserSuspended(uData)))
                break;

        head = &(channel->users);
    }

    if(uData && (uData != *head))
    {
        /* Shuffle the user to the head of whatever list he was in. */
        if(uData->next)
            uData->next->prev = uData->prev;
        if(uData->prev)
            uData->prev->next = uData->next;

        uData->prev = NULL;
        uData->next = *head;

        if(*head)
            (**head).prev = uData;
        *head = uData;
    }

    return uData;
}

/* Returns non-zero if user has at least the minimum access.
 * exempt_owner is set when handling !set, so the owner can set things
 * to/from >500.
 */
int check_user_level(struct chanNode *channel, struct userNode *user, enum levelOption opt, int allow_override, int exempt_owner)
{
    struct userData *uData;
    struct chanData *cData = channel->channel_info;
    unsigned short minimum = cData->lvlOpts[opt];
    if(!minimum)
        return 1;
    uData = _GetChannelUser(cData, user->handle_info, allow_override, 0);
    if(!uData)
        return 0;
    if(minimum <= uData->access)
        return 1;
    if((minimum > UL_OWNER) && (uData->access == UL_OWNER) && exempt_owner)
        return 1;
    return 0;
}

/* Scan for other users authenticated to the same handle
   still in the channel. If so, keep them listed as present.

   user is optional, if not null, it skips checking that userNode
   (for the handle_part function) */
static void
scan_user_presence(struct userData *uData, struct userNode *user)
{
    struct modeNode *mn;

    if(IsSuspended(uData->channel)
       || IsUserSuspended(uData)
       || !(mn = find_handle_in_channel(uData->channel->channel, uData->handle, user)))
    {
        uData->present = 0;
    }
    else
    {
        uData->present = 1;
        uData->seen = now;
    }
}

static void
chanserv_ctcp_check(struct userNode *user, struct chanNode *channel, const char *text, UNUSED_ARG(struct userNode *bot), UNUSED_ARG(unsigned int is_notice))
{
    unsigned int eflags, argc;
    char *argv[4];
    static char *bad_ctcp_reason = "CTCPs to this channel are forbidden.";

    /* Bail early if channel is inactive or doesn't restrict CTCPs, or sender is a service */
    if(!channel->channel_info
       || IsSuspended(channel->channel_info)
       || IsService(user)
       || !ircncasecmp(text, "ACTION ", 7))
        return;
    /* Figure out the minimum level needed to CTCP the channel */
    if(check_user_level(channel, user, lvlCTCPUsers, 1, 0))
        return;
    /* We need to enforce against them; do so. */
    eflags = 0;
    argv[0] = (char*)text;
    argv[1] = user->nick;
    argc = 2;
    if(GetUserMode(channel, user))
        eflags |= ACTION_KICK;
    switch(channel->channel_info->chOpts[chCTCPReaction]) {
    default: case 'k': /* just do the kick */ break;
    case 'b':
        eflags |= ACTION_BAN;
        break;
    case 't':
        eflags |= ACTION_BAN | ACTION_ADD_BAN | ACTION_ADD_TIMED_BAN;
        argv[argc++] = (char*)chanserv_conf.ctcp_short_ban_duration;
        break;
    case 'T':
        eflags |= ACTION_BAN | ACTION_ADD_BAN | ACTION_ADD_TIMED_BAN;
        argv[argc++] = (char*)chanserv_conf.ctcp_long_ban_duration;
        break;
    }
    argv[argc++] = bad_ctcp_reason;
    eject_user(chanserv, channel, argc, argv, NULL, eflags);
}

struct note_type *
chanserv_create_note_type(const char *name)
{
    struct note_type *ntype = calloc(1, sizeof(*ntype) + strlen(name));
    strcpy(ntype->name, name);
    ntype->refs = 1;
    dict_insert(note_types, ntype->name, ntype);
    return ntype;
}

static void
chanserv_deref_note_type(void *data)
{
    struct note_type *ntype = data;

    if(--ntype->refs > 0)
        return;
    free(ntype);
}

static void
chanserv_flush_note_type(struct note_type *ntype)
{
    struct chanData *cData;
    for(cData = channelList; cData; cData = cData->next)
        dict_remove(cData->notes, ntype->name);
}

static void
chanserv_truncate_notes(struct note_type *ntype)
{
    struct chanData *cData;
    struct note *note;
    unsigned int size = sizeof(*note) + ntype->max_length;

    for(cData = channelList; cData; cData = cData->next) {
        note = dict_find(cData->notes, ntype->name, NULL);
        if(!note)
            continue;
        if(strlen(note->note) <= ntype->max_length)
            continue;
        dict_remove2(cData->notes, ntype->name, 1);
        note = realloc(note, size);
        note->note[ntype->max_length] = 0;
        dict_insert(cData->notes, ntype->name, note);
    }
}

static int note_type_visible_to_user(struct chanData *channel, struct note_type *ntype, struct userNode *user);

static struct note *
chanserv_add_channel_note(struct chanData *channel, struct note_type *type, const char *setter, const char *text)
{
    struct note *note;
    unsigned int len = strlen(text);

    if(len > type->max_length) len = type->max_length;
    note = calloc(1, sizeof(*note) + len);
    note->type = type;
    strncpy(note->setter, setter, sizeof(note->setter)-1);
    memcpy(note->note, text, len);
    note->note[len] = 0;
    dict_insert(channel->notes, type->name, note);
    type->refs++;
    return note;
}

static void
chanserv_free_note(void *data)
{
    struct note *note = data;

    chanserv_deref_note_type(note->type);
    assert(note->type->refs > 0); /* must use delnote to remove the type */
    free(note);
}

static MODCMD_FUNC(cmd_createnote) {
    struct note_type *ntype;
    unsigned int arg = 1, existed = 0, max_length;

    if((ntype = dict_find(note_types, argv[1], NULL)))
        existed = 1;
    else
        ntype = chanserv_create_note_type(argv[arg]);
    if(!irccasecmp(argv[++arg], "privileged"))
    {
        arg++;
        ntype->set_access_type = NOTE_SET_PRIVILEGED;
        ntype->set_access.min_opserv = strtoul(argv[arg], NULL, 0);
    }
    else if(!irccasecmp(argv[arg], "channel"))
    {
        unsigned short ulvl = user_level_from_name(argv[++arg], UL_OWNER);
        if(!ulvl)
        {
            reply("CSMSG_INVALID_ACCESS", argv[arg]);
            goto fail;
        }
        ntype->set_access_type = NOTE_SET_CHANNEL_ACCESS;
        ntype->set_access.min_ulevel = ulvl;
    }
    else if(!irccasecmp(argv[arg], "setter"))
    {
        ntype->set_access_type = NOTE_SET_CHANNEL_SETTER;
    }
    else
    {
        reply("CSMSG_BAD_NOTE_ACCESS", argv[arg]);
        goto fail;
    }

    if(!irccasecmp(argv[++arg], "privileged"))
        ntype->visible_type = NOTE_VIS_PRIVILEGED;
    else if(!irccasecmp(argv[arg], "channel_users"))
        ntype->visible_type = NOTE_VIS_CHANNEL_USERS;
    else if(!irccasecmp(argv[arg], "all"))
        ntype->visible_type = NOTE_VIS_ALL;
    else {
        reply("CSMSG_BAD_NOTE_ACCESS", argv[arg]);
        goto fail;
    }

    if((arg+1) >= argc) {
        reply("MSG_MISSING_PARAMS", argv[0]);
        goto fail;
    }
    max_length = strtoul(argv[++arg], NULL, 0);
    if(max_length < 20 || max_length > 450)
    {
        reply("CSMSG_BAD_MAX_LENGTH", argv[arg]);
        goto fail;
    }
    if(existed && (max_length < ntype->max_length))
    {
        ntype->max_length = max_length;
        chanserv_truncate_notes(ntype);
    }
    ntype->max_length = max_length;

    if(existed)
        reply("CSMSG_NOTE_MODIFIED", ntype->name);
    else
        reply("CSMSG_NOTE_CREATED", ntype->name);
    return 1;

fail:
    if(!existed)
        dict_remove(note_types, ntype->name);
    return 0;
}

static MODCMD_FUNC(cmd_removenote) {
    struct note_type *ntype;
    int force;

    ntype = dict_find(note_types, argv[1], NULL);
    force = (argc > 2) && !irccasecmp(argv[2], "force");
    if(!ntype)
    {
        reply("CSMSG_BAD_NOTE_TYPE", argv[1]);
        return 0;
    }
    if(ntype->refs > 1)
    {
        if(!force)
        {
            reply("CSMSG_NOTE_TYPE_USED", ntype->name);
            return 0;
        }
        chanserv_flush_note_type(ntype);
    }
    dict_remove(note_types, argv[1]);
    reply("CSMSG_NOTE_DELETED", argv[1]);
    return 1;
}

static int
mode_lock_violated(const struct mod_chanmode *orig, const struct mod_chanmode *change)
{
    if(!orig)
        return 0;
    if(orig->modes_set & change->modes_clear)
        return 1;
    if(orig->modes_clear & change->modes_set)
        return 1;
    if((orig->modes_set & MODE_KEY) && (change->modes_set & MODE_KEY)
       && strcmp(orig->new_key, change->new_key))
        return 1;
    if((orig->modes_set & MODE_LIMIT) && (change->modes_set & MODE_LIMIT)
       && (orig->new_limit != change->new_limit))
        return 1;
    return 0;
}

static char max_length_text[MAXLEN+1][16];

static struct helpfile_expansion
chanserv_expand_variable(const char *variable)
{
    struct helpfile_expansion exp;

    if(!irccasecmp(variable, "notes"))
    {
        dict_iterator_t it;
        exp.type = HF_TABLE;
        exp.value.table.length = 1;
        exp.value.table.width = 3;
        exp.value.table.flags = 0;
        exp.value.table.contents = calloc(dict_size(note_types)+1, sizeof(char**));
        exp.value.table.contents[0] = calloc(exp.value.table.width, sizeof(char*));
        exp.value.table.contents[0][0] = "Note Type";
        exp.value.table.contents[0][1] = "Visibility";
        exp.value.table.contents[0][2] = "Max Length";
        for(it=dict_first(note_types); it; it=iter_next(it))
        {
            struct note_type *ntype = iter_data(it);
            int row;

            if(!note_type_visible_to_user(NULL, ntype, message_dest)) continue;
            row = exp.value.table.length++;
            exp.value.table.contents[row] = calloc(exp.value.table.width, sizeof(char*));
            exp.value.table.contents[row][0] = ntype->name;
            exp.value.table.contents[row][1] = (ntype->visible_type == NOTE_VIS_ALL) ? "all" :
                (ntype->visible_type == NOTE_VIS_CHANNEL_USERS) ? "chan users" :
                "unknown";
            if(!max_length_text[ntype->max_length][0])
                snprintf(max_length_text[ntype->max_length], sizeof(max_length_text[ntype->max_length]), "%u", ntype->max_length);
            exp.value.table.contents[row][2] = max_length_text[ntype->max_length];
        }
        return exp;
    }

    exp.type = HF_STRING;
    exp.value.str = NULL;
    return exp;
}

static struct chanData*
register_channel(struct chanNode *cNode, char *registrar)
{
    struct chanData *channel;
    enum levelOption lvlOpt;
    enum charOption chOpt;

    channel = calloc(1, sizeof(struct chanData));

    channel->notes = dict_new();
    dict_set_free_data(channel->notes, chanserv_free_note);

    channel->registrar = strdup(registrar);
    channel->registered = now;
    channel->visited = now;
    channel->limitAdjusted = now;
    channel->ownerTransfer = now;
    channel->flags = CHANNEL_DEFAULT_FLAGS;
    for(lvlOpt = 0; lvlOpt < NUM_LEVEL_OPTIONS; ++lvlOpt)
        channel->lvlOpts[lvlOpt] = levelOptions[lvlOpt].default_value;
    for(chOpt = 0; chOpt < NUM_CHAR_OPTIONS; ++chOpt)
        channel->chOpts[chOpt] = charOptions[chOpt].default_value;

    channel->prev = NULL;
    channel->next = channelList;

    if(channelList)
        channelList->prev = channel;
    channelList = channel;
    registered_channels++;

    channel->channel = cNode;
    LockChannel(cNode);
    cNode->channel_info = channel;

    return channel;
}

static struct userData*
add_channel_user(struct chanData *channel, struct handle_info *handle, unsigned short access_level, unsigned long seen, const char *info)
{
    struct userData *ud;

    if(access_level > UL_OWNER)
        return NULL;

    ud = calloc(1, sizeof(*ud));
    ud->channel = channel;
    ud->handle = handle;
    ud->seen = seen;
    ud->access = access_level;
    ud->info = info ? strdup(info) : NULL;

    ud->prev = NULL;
    ud->next = channel->users;
    if(channel->users)
        channel->users->prev = ud;
    channel->users = ud;

    channel->userCount++;
    userCount++;

    ud->u_prev = NULL;
    ud->u_next = ud->handle->channels;
    if(ud->u_next)
        ud->u_next->u_prev = ud;
    ud->handle->channels = ud;

    return ud;
}

static void unregister_channel(struct chanData *channel, const char *reason);

void
del_channel_user(struct userData *user, int do_gc)
{
    struct chanData *channel = user->channel;

    channel->userCount--;
    userCount--;

    if(user->prev)
        user->prev->next = user->next;
    else
        channel->users = user->next;
    if(user->next)
        user->next->prev = user->prev;

    if(user->u_prev)
        user->u_prev->u_next = user->u_next;
    else
        user->handle->channels = user->u_next;
    if(user->u_next)
        user->u_next->u_prev = user->u_prev;

    free(user->info);
    free(user);
    if(do_gc && !channel->users && !IsProtected(channel))
        unregister_channel(channel, "lost all users.");
}

static void expire_ban(void *data);

static struct banData*
add_channel_ban(struct chanData *channel, const char *mask, char *owner, unsigned long set, unsigned long triggered, unsigned long expires, char *reason)
{
    struct banData *bd;
    unsigned int ii, l1, l2;

    if(!mask)
        return NULL;

    bd = malloc(sizeof(struct banData));

    bd->channel = channel;
    bd->set = set;
    bd->triggered = triggered;
    bd->expires = expires;

    for(ii = 0; ii < chanserv_conf.old_ban_names->used; ++ii)
    {
        extern const char *hidden_host_suffix;
        const char *old_name = chanserv_conf.old_ban_names->list[ii];
        char *new_mask;

        l1 = strlen(mask);
        l2 = strlen(old_name);
        if(l2+2 > l1)
            continue;
        if(irccasecmp(mask + l1 - l2, old_name))
            continue;
        new_mask = alloca(MAXLEN);
        sprintf(new_mask, "%.*s%s", (int)(l1-l2), mask, hidden_host_suffix);
        mask = new_mask;
    }
    safestrncpy(bd->mask, mask, sizeof(bd->mask));
    if(owner)
        safestrncpy(bd->owner, owner, sizeof(bd->owner));
    bd->reason = strdup(reason);

    if(expires)
        timeq_add(expires, expire_ban, bd);

    bd->prev = NULL;
    bd->next = channel->bans;
    if(channel->bans)
        channel->bans->prev = bd;
    channel->bans = bd;
    channel->banCount++;
    banCount++;

    return bd;
}

static void
del_channel_ban(struct banData *ban)
{
    ban->channel->banCount--;
    banCount--;

    if(ban->prev)
        ban->prev->next = ban->next;
    else
        ban->channel->bans = ban->next;

    if(ban->next)
        ban->next->prev = ban->prev;

    if(ban->expires)
        timeq_del(0, expire_ban, ban, TIMEQ_IGNORE_WHEN);

    if(ban->reason)
        free(ban->reason);

    free(ban);
}

static void
expire_ban(void *data)
{
    struct banData *bd = data;
    if(!IsSuspended(bd->channel))
    {
        struct banList bans;
        struct mod_chanmode change;
        unsigned int ii;
        bans = bd->channel->channel->banlist;
        mod_chanmode_init(&change);
        for(ii=0; ii<bans.used; ii++)
        {
            if(!strcmp(bans.list[ii]->ban, bd->mask))
            {
                change.argc = 1;
                change.args[0].mode = MODE_REMOVE|MODE_BAN;
                change.args[0].u.hostmask = bd->mask;
                mod_chanmode_announce(chanserv, bd->channel->channel, &change);
                break;
            }
        }
    }
    bd->expires = 0;
    del_channel_ban(bd);
}

static void chanserv_expire_suspension(void *data);

static void
unregister_channel(struct chanData *channel, const char *reason)
{
    struct mod_chanmode change;
    char msgbuf[MAXLEN];

    /* After channel unregistration, the following must be cleaned
       up:
       - Channel information.
       - Channel users.
       - Channel bans.
       - Channel suspension data.
       - Timeq entries. (Except timed bans, which are handled elsewhere.)
    */

    if(!channel)
        return;

    timeq_del(0, NULL, channel, TIMEQ_IGNORE_FUNC | TIMEQ_IGNORE_WHEN);

    if(off_channel > 0)
    {
      mod_chanmode_init(&change);
      change.modes_clear |= MODE_REGISTERED;
      mod_chanmode_announce(chanserv, channel->channel, &change);
    }

    while(channel->users)
        del_channel_user(channel->users, 0);

    while(channel->bans)
        del_channel_ban(channel->bans);

    free(channel->topic);
    free(channel->registrar);
    free(channel->greeting);
    free(channel->user_greeting);
    free(channel->topic_mask);

    if(channel->prev)
        channel->prev->next = channel->next;
    else
        channelList = channel->next;

    if(channel->next)
        channel->next->prev = channel->prev;

    if(channel->suspended)
    {
        struct chanNode *cNode = channel->channel;
        struct suspended *suspended, *next_suspended;

        for(suspended = channel->suspended; suspended; suspended = next_suspended)
        {
            next_suspended = suspended->previous;
            free(suspended->suspender);
            free(suspended->reason);
            if(suspended->expires)
                timeq_del(suspended->expires, chanserv_expire_suspension, suspended, 0);
            free(suspended);
        }

        if(cNode)
            cNode->channel_info = NULL;
    }
    channel->channel->channel_info = NULL;

    dict_delete(channel->notes);
    sprintf(msgbuf, "%s %s", channel->channel->name, reason);
    if(!IsSuspended(channel))
        DelChannelUser(chanserv, channel->channel, msgbuf, 0);
    global_message(MESSAGE_RECIPIENT_OPERS | MESSAGE_RECIPIENT_HELPERS, msgbuf);
    UnlockChannel(channel->channel);
    free(channel);
    registered_channels--;
}

static void
expire_channels(void *data)
{
    struct chanData *channel, *next;
    struct userData *user;
    char delay[INTERVALLEN], reason[INTERVALLEN + 64];

    intervalString(delay, chanserv_conf.channel_expire_delay, NULL);
    sprintf(reason, "Channel registration automatically expired after %s of disuse.", delay);

    for(channel = channelList; channel; channel = next)
    {
        next = channel->next;

        /* See if the channel can be expired. */
        if(((now - channel->visited) <= chanserv_conf.channel_expire_delay)
           || IsProtected(channel))
            continue;

        /* Make sure there are no high-ranking users still in the channel. */
        for(user=channel->users; user; user=user->next)
            if(user->present && (user->access >= UL_PRESENT) && !HANDLE_FLAGGED(user->handle, BOT))
                break;
        if(user)
            continue;

        /* Unregister the channel */
        log_module(CS_LOG, LOG_INFO, "(%s) Channel registration expired.", channel->channel->name);
        unregister_channel(channel, "registration expired.");
    }

    if(chanserv_conf.channel_expire_frequency && !data)
        timeq_add(now + chanserv_conf.channel_expire_frequency, expire_channels, NULL);
}

static void
expire_dnrs(UNUSED_ARG(void *data))
{
    dict_iterator_t it, next;
    struct do_not_register *dnr;

    for(it = dict_first(handle_dnrs); it; it = next)
    {
        dnr = iter_data(it);
        next = iter_next(it);
        if(dnr->expires && dnr->expires <= now)
            dict_remove(handle_dnrs, dnr->chan_name + 1);
    }
    for(it = dict_first(plain_dnrs); it; it = next)
    {
        dnr = iter_data(it);
        next = iter_next(it);
        if(dnr->expires && dnr->expires <= now)
            dict_remove(plain_dnrs, dnr->chan_name + 1);
    }
    for(it = dict_first(mask_dnrs); it; it = next)
    {
        dnr = iter_data(it);
        next = iter_next(it);
        if(dnr->expires && dnr->expires <= now)
            dict_remove(mask_dnrs, dnr->chan_name + 1);
    }

    if(chanserv_conf.dnr_expire_frequency)
        timeq_add(now + chanserv_conf.dnr_expire_frequency, expire_dnrs, NULL);
}

static int
protect_user(const struct userNode *victim, const struct userNode *aggressor, struct chanData *channel)
{
    char protect = channel->chOpts[chProtect];
    struct userData *cs_victim, *cs_aggressor;

    /* Don't protect if no one is to be protected, someone is attacking
       himself, or if the aggressor is an IRC Operator. */
    if(protect == 'n' || victim == aggressor || IsOper(aggressor))
        return 0;

    /* Don't protect if the victim isn't authenticated (because they
       can't be a channel user), unless we are to protect non-users
       also. */
    cs_victim = GetChannelAccess(channel, victim->handle_info);
    if(protect != 'a' && !cs_victim)
        return 0;

    /* Protect if the aggressor isn't a user because at this point,
       the aggressor can only be less than or equal to the victim. */
    cs_aggressor = GetChannelAccess(channel, aggressor->handle_info);
    if(!cs_aggressor)
        return 1;

    /* If the aggressor was a user, then the victim can't be helped. */
    if(!cs_victim)
        return 0;

    switch(protect)
    {
    case 'l':
        if(cs_victim->access > cs_aggressor->access)
            return 1;
        break;
    case 'a':
    case 'e':
        if(cs_victim->access >= cs_aggressor->access)
            return 1;
        break;
    }

    return 0;
}

static int
validate_op(struct userNode *user, struct chanNode *channel, struct userNode *victim)
{
    struct chanData *cData = channel->channel_info;
    struct userData *cs_victim;

    if((!(cs_victim = GetChannelUser(cData, victim->handle_info))
        || (cs_victim->access < cData->lvlOpts[lvlGiveOps]))
       && !check_user_level(channel, user, lvlEnfOps, 0, 0))
    {
        send_message(user, chanserv, "CSMSG_OPBY_LOCKED");
        return 0;
    }

    return 1;
}

static int
validate_deop(struct userNode *user, struct chanNode *channel, struct userNode *victim)
{
    if(IsService(victim))
    {
        send_message(user, chanserv, "MSG_SERVICE_IMMUNE", victim->nick);
        return 0;
    }

    if(protect_user(victim, user, channel->channel_info))
    {
        send_message(user, chanserv, "CSMSG_USER_PROTECTED", victim->nick);
        return 0;
    }

    return 1;
}

static struct do_not_register *
chanserv_add_dnr(const char *chan_name, const char *setter, unsigned long expires, const char *reason)
{
    struct do_not_register *dnr = calloc(1, sizeof(*dnr)+strlen(reason));
    safestrncpy(dnr->chan_name, chan_name, sizeof(dnr->chan_name));
    safestrncpy(dnr->setter, setter, sizeof(dnr->setter));
    strcpy(dnr->reason, reason);
    dnr->set = now;
    dnr->expires = expires;
    if(dnr->chan_name[0] == '*')
        dict_insert(handle_dnrs, dnr->chan_name+1, dnr);
    else if(strpbrk(dnr->chan_name, "*?"))
        dict_insert(mask_dnrs, dnr->chan_name, dnr);
    else
        dict_insert(plain_dnrs, dnr->chan_name, dnr);
    return dnr;
}

static struct dnrList
chanserv_find_dnrs(const char *chan_name, const char *handle, unsigned int max)
{
    struct dnrList list;
    dict_iterator_t it, next;
    struct do_not_register *dnr;

    dnrList_init(&list);

    if(handle && (dnr = dict_find(handle_dnrs, handle, NULL)))
    {
        if(dnr->expires && dnr->expires <= now)
            dict_remove(handle_dnrs, handle);
        else if(list.used < max)
            dnrList_append(&list, dnr);
    }

    if(chan_name && (dnr = dict_find(plain_dnrs, chan_name, NULL)))
    {
        if(dnr->expires && dnr->expires <= now)
            dict_remove(plain_dnrs, chan_name);
        else if(list.used < max)
            dnrList_append(&list, dnr);
    }

    if(chan_name)
    {
        for(it = dict_first(mask_dnrs); it && list.used < max; it = next)
        {
            next = iter_next(it);
            if(!match_ircglob(chan_name, iter_key(it)))
                continue;
            dnr = iter_data(it);
            if(dnr->expires && dnr->expires <= now)
                dict_remove(mask_dnrs, iter_key(it));
            else
                dnrList_append(&list, dnr);
        }
    }

    return list;
}

static int dnr_print_func(struct do_not_register *dnr, void *extra)
{
    struct userNode *user;
    char buf1[INTERVALLEN];
    char buf2[INTERVALLEN];
    time_t feh;

    user = extra;
    if(dnr->set)
    {
        feh = dnr->set;
        strftime(buf1, sizeof(buf1), "%d %b %Y", localtime(&feh));
    }
    if(dnr->expires)
    {
        feh = dnr->expires;
        strftime(buf2, sizeof(buf2), "%d %b %Y", localtime(&feh));
        send_message(user, chanserv, "CSMSG_DNR_INFO_SET_EXPIRES", dnr->chan_name, buf1, dnr->setter, buf2, dnr->reason);
    }
    else if(dnr->set)
    {
        send_message(user, chanserv, "CSMSG_DNR_INFO_SET", dnr->chan_name, buf1, dnr->setter, dnr->reason);
    }
    else
        send_message(user, chanserv, "CSMSG_DNR_INFO", dnr->chan_name, dnr->setter, dnr->reason);
    return 0;
}

static unsigned int
chanserv_show_dnrs(struct userNode *user, struct svccmd *cmd, const char *chan_name, const char *handle)
{
    struct dnrList list;
    unsigned int ii;

    list = chanserv_find_dnrs(chan_name, handle, UINT_MAX);
    for(ii = 0; (ii < list.used) && (ii < 10); ++ii)
        dnr_print_func(list.list[ii], user);
    if(ii < list.used)
        reply("CSMSG_MORE_DNRS", list.used - ii);
    free(list.list);
    return ii;
}

struct do_not_register *
chanserv_is_dnr(const char *chan_name, struct handle_info *handle)
{
    struct dnrList list;
    struct do_not_register *dnr;

    list = chanserv_find_dnrs(chan_name, handle ? handle->handle : NULL, 1);
    dnr = list.used ? list.list[0] : NULL;
    free(list.list);
    return dnr;
}

static unsigned int send_dnrs(struct userNode *user, dict_t dict)
{
    struct do_not_register *dnr;
    dict_iterator_t it, next;
    unsigned int matches = 0;

    for(it = dict_first(dict); it; it = next)
    {
        dnr = iter_data(it);
        next = iter_next(it);
        if(dnr->expires && dnr->expires <= now)
        {
            dict_remove(dict, iter_key(it));
            continue;
        }
        dnr_print_func(dnr, user);
        matches++;
    }

    return matches;
}

static CHANSERV_FUNC(cmd_noregister)
{
    const char *target;
    const char *reason;
    unsigned long expiry, duration;
    unsigned int matches;

    if(argc < 2)
    {
        reply("CSMSG_DNR_SEARCH_RESULTS");
        matches = send_dnrs(user, handle_dnrs);
        matches += send_dnrs(user, plain_dnrs);
        matches += send_dnrs(user, mask_dnrs);
        if(matches)
            reply("MSG_MATCH_COUNT", matches);
        else
            reply("MSG_NO_MATCHES");
        return 0;
    }

    target = argv[1];

    if(!IsChannelName(target) && (*target != '*'))
    {
        reply("CSMSG_NOT_DNR", target);
        return 0;
    }

    if(argc > 2)
    {
        if(argc == 3)
        {
            reply("MSG_INVALID_DURATION", argv[2]);
            return 0;
        }

        if(!strcmp(argv[2], "0"))
            expiry = 0;
        else if((duration = ParseInterval(argv[2])))
            expiry = now + duration;
        else
        {
            reply("MSG_INVALID_DURATION", argv[2]);
            return 0;
        }

        reason = unsplit_string(argv + 3, argc - 3, NULL);
        if((*target == '*') && !get_handle_info(target + 1))
        {
            reply("MSG_HANDLE_UNKNOWN", target + 1);
            return 0;
        }
        chanserv_add_dnr(target, user->handle_info->handle, expiry, reason);
        reply("CSMSG_NOREGISTER_CHANNEL", target);
        return 1;
    }

    reply("CSMSG_DNR_SEARCH_RESULTS");
    if(*target == '*')
        matches = chanserv_show_dnrs(user, cmd, NULL, target + 1);
    else
        matches = chanserv_show_dnrs(user, cmd, target, NULL);
    if(!matches)
        reply("MSG_NO_MATCHES");
    return 0;
}

static CHANSERV_FUNC(cmd_allowregister)
{
    const char *chan_name = argv[1];

    if(((chan_name[0] == '*') && dict_remove(handle_dnrs, chan_name+1))
       || dict_remove(plain_dnrs, chan_name)
       || dict_remove(mask_dnrs, chan_name))
    {
        reply("CSMSG_DNR_REMOVED", chan_name);
        return 1;
    }
    reply("CSMSG_NO_SUCH_DNR", chan_name);
    return 0;
}

struct dnr_search {
    struct userNode *source;
    char *chan_mask;
    char *setter_mask;
    char *reason_mask;
    unsigned long min_set, max_set;
    unsigned long min_expires, max_expires;
    unsigned int limit;
};

static int
dnr_search_matches(const struct do_not_register *dnr, const struct dnr_search *search)
{
    return !((dnr->set < search->min_set)
             || (dnr->set > search->max_set)
             || (dnr->expires < search->min_expires)
             || (search->max_expires
                 && ((dnr->expires == 0)
                     || (dnr->expires > search->max_expires)))
             || (search->chan_mask
                 && !match_ircglob(dnr->chan_name, search->chan_mask))
             || (search->setter_mask
                 && !match_ircglob(dnr->setter, search->setter_mask))
             || (search->reason_mask
                 && !match_ircglob(dnr->reason, search->reason_mask)));
}

static struct dnr_search *
dnr_search_create(struct userNode *user, struct svccmd *cmd, unsigned int argc, char *argv[])
{
    struct dnr_search *discrim;
    unsigned int ii;

    discrim = calloc(1, sizeof(*discrim));
    discrim->source = user;
    discrim->chan_mask = NULL;
    discrim->setter_mask = NULL;
    discrim->reason_mask = NULL;
    discrim->max_set = INT_MAX;
    discrim->limit = 50;

    for(ii=0; ii<argc; ++ii)
    {
        if(ii == argc - 1)
        {
            reply("MSG_MISSING_PARAMS", argv[ii]);
            goto fail;
        }
        else if(0 == irccasecmp(argv[ii], "channel"))
        {
            discrim->chan_mask = argv[++ii];
        }
        else if(0 == irccasecmp(argv[ii], "setter"))
        {
            discrim->setter_mask = argv[++ii];
        }
        else if(0 == irccasecmp(argv[ii], "reason"))
        {
            discrim->reason_mask = argv[++ii];
        }
        else if(0 == irccasecmp(argv[ii], "limit"))
        {
            discrim->limit = strtoul(argv[++ii], NULL, 0);
        }
        else if(0 == irccasecmp(argv[ii], "set"))
        {
            const char *cmp = argv[++ii];
            if(cmp[0] == '<') {
                if(cmp[1] == '=')
                    discrim->min_set = now - ParseInterval(cmp + 2);
                else
                    discrim->min_set = now - (ParseInterval(cmp + 1) - 1);
            } else if(cmp[0] == '=') {
                discrim->min_set = discrim->max_set = now - ParseInterval(cmp + 1);
            } else if(cmp[0] == '>') {
                if(cmp[1] == '=')
                    discrim->max_set = now - ParseInterval(cmp + 2);
                else
                    discrim->max_set = now - (ParseInterval(cmp + 1) - 1);
            } else {
                discrim->max_set = now - (ParseInterval(cmp) - 1);
            }
        }
        else if(0 == irccasecmp(argv[ii], "expires"))
        {
            const char *cmp = argv[++ii];
            if(cmp[0] == '<') {
                if(cmp[1] == '=')
                    discrim->max_expires = now + ParseInterval(cmp + 2);
                else
                    discrim->max_expires = now + (ParseInterval(cmp + 1) - 1);
            } else if(cmp[0] == '=') {
                discrim->min_expires = discrim->max_expires = now + ParseInterval(cmp + 1);
            } else if(cmp[0] == '>') {
                if(cmp[1] == '=')
                    discrim->min_expires = now + ParseInterval(cmp + 2);
                else
                    discrim->min_expires = now + (ParseInterval(cmp + 1) - 1);
            } else {
                discrim->min_expires = now + (ParseInterval(cmp) - 1);
            }
        }
        else
        {
            reply("MSG_INVALID_CRITERIA", argv[ii]);
            goto fail;
        }
    }
    return discrim;

  fail:
    free(discrim);
    return NULL;
}

typedef int (*dnr_search_func)(struct do_not_register *match, void *extra);

static unsigned int
dnr_search(struct dnr_search *discrim, dnr_search_func dsf, void *data)
{
    struct do_not_register *dnr;
    dict_iterator_t next;
    dict_iterator_t it;
    unsigned int count;
    int target_fixed;

    /* Initialize local variables. */
    count = 0;
    target_fixed = 0;
    if(discrim->chan_mask)
    {
        int shift = (discrim->chan_mask[0] == '\\' && discrim->chan_mask[1] == '*') ? 2 : 0;
        if('\0' == discrim->chan_mask[shift + strcspn(discrim->chan_mask+shift, "*?")])
            target_fixed = 1;
    }

    if(target_fixed && discrim->chan_mask[0] == '\\' && discrim->chan_mask[1] == '*')
    {
        /* Check against account-based DNRs. */
        dnr = dict_find(handle_dnrs, discrim->chan_mask + 2, NULL);
        if(dnr && dnr_search_matches(dnr, discrim) && (count++ < discrim->limit))
            dsf(dnr, data);
    }
    else if(target_fixed)
    {
        /* Check against channel-based DNRs. */
        dnr = dict_find(plain_dnrs, discrim->chan_mask, NULL);
        if(dnr && dnr_search_matches(dnr, discrim) && (count++ < discrim->limit))
            dsf(dnr, data);
    }
    else
    {
        /* Exhaustively search account DNRs. */
        for(it = dict_first(handle_dnrs); it; it = next)
        {
            next = iter_next(it);
            dnr = iter_data(it);
            if(dnr_search_matches(dnr, discrim) && (count++ < discrim->limit) && dsf(dnr, data))
                break;
        }

        /* Do the same for channel DNRs. */
        for(it = dict_first(plain_dnrs); it; it = next)
        {
            next = iter_next(it);
            dnr = iter_data(it);
            if(dnr_search_matches(dnr, discrim) && (count++ < discrim->limit) && dsf(dnr, data))
                break;
        }

        /* Do the same for wildcarded channel DNRs. */
        for(it = dict_first(mask_dnrs); it; it = next)
        {
            next = iter_next(it);
            dnr = iter_data(it);
            if(dnr_search_matches(dnr, discrim) && (count++ < discrim->limit) && dsf(dnr, data))
                break;
        }
    }
    return count;
}

static int
dnr_remove_func(struct do_not_register *match, void *extra)
{
    struct userNode *user;
    char *chan_name;

    chan_name = alloca(strlen(match->chan_name) + 1);
    strcpy(chan_name, match->chan_name);
    user = extra;
    if(((chan_name[0] == '*') && dict_remove(handle_dnrs, chan_name+1))
       || dict_remove(plain_dnrs, chan_name)
       || dict_remove(mask_dnrs, chan_name))
    {
        send_message(user, chanserv, "CSMSG_DNR_REMOVED", chan_name);
    }
    return 0;
}

static int
dnr_count_func(struct do_not_register *match, void *extra)
{
    return 0; (void)match; (void)extra;
}

static MODCMD_FUNC(cmd_dnrsearch)
{
    struct dnr_search *discrim;
    dnr_search_func action;
    struct svccmd *subcmd;
    unsigned int matches;
    char buf[MAXLEN];

    sprintf(buf, "dnrsearch %s", argv[1]);
    subcmd = dict_find(cmd->parent->commands, buf, NULL);
    if(!subcmd)
    {
        reply("CSMSG_DNR_BAD_ACTION", argv[1]);
        return 0;
    }
    if(!svccmd_can_invoke(user, cmd->parent->bot, subcmd, channel, SVCCMD_NOISY))
        return 0;
    if(!irccasecmp(argv[1], "print"))
        action = dnr_print_func;
    else if(!irccasecmp(argv[1], "remove"))
        action = dnr_remove_func;
    else if(!irccasecmp(argv[1], "count"))
        action = dnr_count_func;
    else
    {
        reply("CSMSG_DNR_BAD_ACTION", argv[1]);
        return 0;
    }

    discrim = dnr_search_create(user, cmd, argc-2, argv+2);
    if(!discrim)
        return 0;

    if(action == dnr_print_func)
        reply("CSMSG_DNR_SEARCH_RESULTS");
    matches = dnr_search(discrim, action, user);
    if(matches)
        reply("MSG_MATCH_COUNT", matches);
    else
        reply("MSG_NO_MATCHES");
    free(discrim);
    return 1;
}

unsigned int
chanserv_get_owned_count(struct handle_info *hi)
{
    struct userData *cList;
    unsigned int owned;

    for(owned=0, cList=hi->channels; cList; cList=cList->u_next)
        if(cList->access == UL_OWNER)
            owned++;
    return owned;
}

static CHANSERV_FUNC(cmd_register)
{
    struct handle_info *handle;
    struct chanData *cData;
    struct modeNode *mn;
    char reason[MAXLEN];
    char *chan_name;
    unsigned int new_channel, force=0;
    struct do_not_register *dnr;

    if(channel)
    {
        if(channel->channel_info)
        {
            reply("CSMSG_ALREADY_REGGED", channel->name);
            return 0;
        }

        if(channel->bad_channel)
        {
            reply("CSMSG_ILLEGAL_CHANNEL", channel->name);
            return 0;
        }

        if(!IsHelping(user)
           && (!(mn = GetUserMode(channel, user)) || !(mn->modes & MODE_CHANOP)))
        {
            reply("CSMSG_MUST_BE_OPPED", channel->name);
            return 0;
        }

        new_channel = 0;
        chan_name = channel->name;
    }
    else
    {
        if((argc < 2) || !IsChannelName(argv[1]))
        {
            reply("MSG_NOT_CHANNEL_NAME");
            return 0;
        }

        if(opserv_bad_channel(argv[1]))
        {
            reply("CSMSG_ILLEGAL_CHANNEL", argv[1]);
            return 0;
        }

        new_channel = 1;
        chan_name = argv[1];
    }

    if(argc >= (new_channel+2))
    {
        if(!IsHelping(user))
        {
            reply("CSMSG_PROXY_FORBIDDEN");
            return 0;
        }

        if(!(handle = modcmd_get_handle_info(user, argv[new_channel+1])))
            return 0;
        force = (argc > (new_channel+2)) && !irccasecmp(argv[new_channel+2], "force");
        dnr = chanserv_is_dnr(chan_name, handle);
    }
    else
    {
        handle = user->handle_info;
        dnr = chanserv_is_dnr(chan_name, handle);
    }
    if(dnr && !force)
    {
        if(!IsHelping(user))
            reply("CSMSG_DNR_CHANNEL", chan_name);
        else
            chanserv_show_dnrs(user, cmd, chan_name, handle->handle);
        return 0;
    }

    if((chanserv_get_owned_count(handle) >= chanserv_conf.max_owned) && !force)
    {
        reply("CSMSG_OWN_TOO_MANY", handle->handle, chanserv_conf.max_owned);
        return 0;
    }

    if(new_channel)
        channel = AddChannel(argv[1], now, NULL, NULL);

    cData = register_channel(channel, user->handle_info->handle);
    scan_user_presence(add_channel_user(cData, handle, UL_OWNER, 0, NULL), NULL);
    cData->modes = chanserv_conf.default_modes;
    if(off_channel > 0)
        cData->modes.modes_set |= MODE_REGISTERED;
    if (IsOffChannel(cData))
    {
        mod_chanmode_announce(chanserv, channel, &cData->modes);
    }
    else
    {
        struct mod_chanmode *change = mod_chanmode_dup(&cData->modes, 1);
        change->args[change->argc].mode = MODE_CHANOP;
        change->args[change->argc].u.member = AddChannelUser(chanserv, channel);
        change->argc++;
        mod_chanmode_announce(chanserv, channel, change);
        mod_chanmode_free(change);
    }

    /* Initialize the channel's max user record. */
    cData->max = channel->members.used;

    if(handle != user->handle_info)
        reply("CSMSG_PROXY_SUCCESS", handle->handle, channel->name);
    else
        reply("CSMSG_REG_SUCCESS", channel->name);

    sprintf(reason, "%s registered to %s by %s.", channel->name, handle->handle, user->handle_info->handle);
    global_message(MESSAGE_RECIPIENT_OPERS | MESSAGE_RECIPIENT_HELPERS, reason);
    return 1;
}

static const char *
make_confirmation_string(struct userData *uData)
{
    static char strbuf[16];
    char *src;
    unsigned int accum;

    accum = 0;
    for(src = uData->handle->handle; *src; )
        accum = accum * 31 + toupper(*src++);
    if(uData->channel)
        for(src = uData->channel->channel->name; *src; )
            accum = accum * 31 + toupper(*src++);
    sprintf(strbuf, "%08x", accum);
    return strbuf;
}

static CHANSERV_FUNC(cmd_unregister)
{
    char *name;
    char reason[MAXLEN];
    struct chanData *cData;
    struct userData *uData;

    cData = channel->channel_info;
    if(!cData)
    {
        reply("CSMSG_NOT_REGISTERED", channel->name);
        return 0;
    }

    uData = GetChannelUser(cData, user->handle_info);
    if(!uData || (uData->access < UL_OWNER))
    {
        reply("CSMSG_NO_ACCESS");
        return 0;
    }

    if(IsProtected(cData))
    {
        reply("CSMSG_UNREG_NODELETE", channel->name);
        return 0;
    }

    if(!IsHelping(user))
    {
        const char *confirm_string;
        if(IsSuspended(cData))
        {
            reply("CSMSG_CHAN_SUSPENDED", channel->name, cData->suspended->reason);
            return 0;
        }
        confirm_string = make_confirmation_string(uData);
        if((argc < 2) || strcmp(argv[1], confirm_string))
        {
            reply("CSMSG_CONFIRM_UNREG", confirm_string);
            return 0;
        }
    }

    sprintf(reason, "unregistered by %s.", user->handle_info->handle);
    name = strdup(channel->name);
    unregister_channel(cData, reason);
    reply("CSMSG_UNREG_SUCCESS", name);
    free(name);
    return 1;
}

static CHANSERV_FUNC(cmd_move)
{
    struct mod_chanmode change;
    struct chanNode *target;
    struct modeNode *mn;
    struct userData *uData;
    char reason[MAXLEN];
    struct do_not_register *dnr;

    REQUIRE_PARAMS(2);

    if(IsProtected(channel->channel_info))
    {
        reply("CSMSG_MOVE_NODELETE", channel->name);
        return 0;
    }

    if(!IsChannelName(argv[1]))
    {
        reply("MSG_NOT_CHANNEL_NAME");
        return 0;
    }

    if(opserv_bad_channel(argv[1]))
    {
        reply("CSMSG_ILLEGAL_CHANNEL", argv[1]);
        return 0;
    }

    if(!IsHelping(user) || (argc < 3) || irccasecmp(argv[2], "force"))
    {
        for(uData = channel->channel_info->users; uData; uData = uData->next)
        {
            if((uData->access == UL_OWNER) && (dnr = chanserv_is_dnr(argv[1], uData->handle)))
            {
                if(!IsHelping(user))
                    reply("CSMSG_DNR_CHANNEL_MOVE", argv[1]);
                else
                    chanserv_show_dnrs(user, cmd, argv[1], uData->handle->handle);
                return 0;
            }
        }
    }

    mod_chanmode_init(&change);
    if(!(target = GetChannel(argv[1])))
    {
        target = AddChannel(argv[1], now, NULL, NULL);
        if(!IsSuspended(channel->channel_info))
            AddChannelUser(chanserv, target);
    }
    else if(target->channel_info)
    {
        reply("CSMSG_ALREADY_REGGED", target->name);
        return 0;
    }
    else if((!(mn = GetUserMode(target, user)) || !(mn->modes && MODE_CHANOP))
            && !IsHelping(user))
    {
        reply("CSMSG_MUST_BE_OPPED", target->name);
        return 0;
    }
    else if(!IsSuspended(channel->channel_info))
    {
        change.argc = 1;
        change.args[0].mode = MODE_CHANOP;
        change.args[0].u.member = AddChannelUser(chanserv, target);
        mod_chanmode_announce(chanserv, target, &change);
    }

    if(off_channel > 0)
    {
        /* Clear MODE_REGISTERED from old channel, add it to new. */
        change.argc = 0;
        change.modes_clear = MODE_REGISTERED;
        mod_chanmode_announce(chanserv, channel, &change);
        change.modes_clear = 0;
        change.modes_set = MODE_REGISTERED;
        mod_chanmode_announce(chanserv, target, &change);
    }

    /* Move the channel_info to the target channel; it
       shouldn't be necessary to clear timeq callbacks
       for the old channel. */
    target->channel_info = channel->channel_info;
    target->channel_info->channel = target;
    channel->channel_info = NULL;

    /* Check whether users are present in the new channel. */
    for(uData = target->channel_info->users; uData; uData = uData->next)
        scan_user_presence(uData, NULL);

    reply("CSMSG_MOVE_SUCCESS", target->name);

    sprintf(reason, "%s moved to %s by %s.", channel->name, target->name, user->handle_info->handle);
    if(!IsSuspended(target->channel_info))
    {
        char reason2[MAXLEN];
        sprintf(reason2, "Channel moved to %s by %s.", target->name, user->handle_info->handle);
        DelChannelUser(chanserv, channel, reason2, 0);
    }
    UnlockChannel(channel);
    LockChannel(target);
    global_message(MESSAGE_RECIPIENT_OPERS | MESSAGE_RECIPIENT_HELPERS, reason);
    return 1;
}

static void
merge_users(struct chanData *source, struct chanData *target)
{
    struct userData *suData, *tuData, *next;
    dict_iterator_t it;
    dict_t merge;

    merge = dict_new();

    /* Insert the source's users into the scratch area. */
    for(suData = source->users; suData; suData = suData->next)
        dict_insert(merge, suData->handle->handle, suData);

    /* Iterate through the target's users, looking for
       users common to both channels. The lower access is
       removed from either the scratch area or target user
       list. */
    for(tuData = target->users; tuData; tuData = next)
    {
        struct userData *choice;

        next = tuData->next;

        /* If a source user exists with the same handle as a target
           channel's user, resolve the conflict by removing one. */
        suData = dict_find(merge, tuData->handle->handle, NULL);
        if(!suData)
            continue;

        /* Pick the data we want to keep. */
        /* If the access is the same, use the later seen time. */
        if(suData->access == tuData->access)
            choice = (suData->seen > tuData->seen) ? suData : tuData;
        else /* Otherwise, keep the higher access level. */
            choice = (suData->access > tuData->access) ? suData : tuData;
        /* Use the later seen time. */
        if(suData->seen < tuData->seen)
            suData->seen = tuData->seen;
        else
            tuData->seen = suData->seen;

        /* Remove the user that wasn't picked. */
        if(choice == tuData)
        {
            dict_remove(merge, suData->handle->handle);
            del_channel_user(suData, 0);
        }
        else
            del_channel_user(tuData, 0);
    }

    /* Move the remaining users to the target channel. */
    for(it = dict_first(merge); it; it = iter_next(it))
    {
        suData = iter_data(it);

        /* Insert the user into the target channel's linked list. */
        suData->prev = NULL;
        suData->next = target->users;
        suData->channel = target;

        if(target->users)
            target->users->prev = suData;
        target->users = suData;

        /* Update the user counts for the target channel; the
           source counts are left alone. */
        target->userCount++;

        /* Check whether the user is in the target channel. */
        scan_user_presence(suData, NULL);
    }

    /* Possible to assert (source->users == NULL) here. */
    source->users = NULL;
    dict_delete(merge);
}

static void
merge_bans(struct chanData *source, struct chanData *target)
{
    struct banData *sbData, *tbData, *sNext, *tNext, *tFront;

    /* Hold on to the original head of the target ban list
       to avoid comparing source bans with source bans. */
    tFront = target->bans;

    /* Perform a totally expensive O(n*m) merge, ick. */
    for(sbData = source->bans; sbData; sbData = sNext)
    {
        /* Flag to track whether the ban's been moved
           to the destination yet. */
        int moved = 0;

        /* Possible to assert (sbData->prev == NULL) here. */
        sNext = sbData->next;

        for(tbData = tFront; tbData; tbData = tNext)
        {
            tNext = tbData->next;

            /* Perform two comparisons between each source
               and target ban, conflicts are resolved by
               keeping the broader ban and copying the later
               expiration and triggered time. */
            if(match_ircglobs(tbData->mask, sbData->mask))
            {
                /* There is a broader ban in the target channel that
                   overrides one in the source channel; remove the
                   source ban and break. */
                if(sbData->expires > tbData->expires)
                    tbData->expires = sbData->expires;
                if(sbData->triggered > tbData->triggered)
                    tbData->triggered = sbData->triggered;
                del_channel_ban(sbData);
                break;
            }
            else if(match_ircglobs(sbData->mask, tbData->mask))
            {
                /* There is a broader ban in the source channel that
                   overrides one in the target channel; remove the
                   target ban, fall through and move the source over. */
                if(tbData->expires > sbData->expires)
                    sbData->expires = tbData->expires;
                if(tbData->triggered > sbData->triggered)
                    sbData->triggered = tbData->triggered;
                if(tbData == tFront)
                    tFront = tNext;
                del_channel_ban(tbData);
            }

            /* Source bans can override multiple target bans, so
               we allow a source to run through this loop multiple
               times, but we can only move it once. */
            if(moved)
                continue;
            moved = 1;

            /* Remove the source ban from the source ban list. */
            if(sbData->next)
                sbData->next->prev = sbData->prev;

            /* Modify the source ban's associated channel. */
            sbData->channel = target;

            /* Insert the ban into the target channel's linked list. */
            sbData->prev = NULL;
            sbData->next = target->bans;

            if(target->bans)
                target->bans->prev = sbData;
            target->bans = sbData;

            /* Update the user counts for the target channel. */
            target->banCount++;
        }
    }

    /* Possible to assert (source->bans == NULL) here. */
    source->bans = NULL;
}

static void
merge_data(struct chanData *source, struct chanData *target)
{
    /* Use more recent visited and owner-transfer time; use older
     * registered time.  Bitwise or may_opchan.  Use higher max.
     * Do not touch last_refresh, ban count or user counts.
     */
    if(source->visited > target->visited)
        target->visited = source->visited;
    if(source->registered < target->registered)
        target->registered = source->registered;
    if(source->ownerTransfer > target->ownerTransfer)
        target->ownerTransfer = source->ownerTransfer;
    if(source->may_opchan)
        target->may_opchan = 1;
    if(source->max > target->max)
        target->max = source->max;
}

static void
merge_channel(struct chanData *source, struct chanData *target)
{
    merge_users(source, target);
    merge_bans(source, target);
    merge_data(source, target);
}

static CHANSERV_FUNC(cmd_merge)
{
    struct userData *target_user;
    struct chanNode *target;
    char reason[MAXLEN];

    REQUIRE_PARAMS(2);

    /* Make sure the target channel exists and is registered to the user
       performing the command. */
    if(!(target = GetChannel(argv[1])))
    {
        reply("MSG_INVALID_CHANNEL");
        return 0;
    }

    if(!target->channel_info)
    {
        reply("CSMSG_NOT_REGISTERED", target->name);
        return 0;
    }

    if(IsProtected(channel->channel_info))
    {
        reply("CSMSG_MERGE_NODELETE");
        return 0;
    }

    if(IsSuspended(target->channel_info))
    {
        reply("CSMSG_MERGE_SUSPENDED");
        return 0;
    }

    if(channel == target)
    {
        reply("CSMSG_MERGE_SELF");
        return 0;
    }

    target_user = GetChannelUser(target->channel_info, user->handle_info);
    if(!target_user || (target_user->access < UL_OWNER))
    {
        reply("CSMSG_MERGE_NOT_OWNER");
        return 0;
    }

    /* Merge the channel structures and associated data. */
    merge_channel(channel->channel_info, target->channel_info);
    sprintf(reason, "merged into %s by %s.", target->name, user->handle_info->handle);
    unregister_channel(channel->channel_info, reason);
    reply("CSMSG_MERGE_SUCCESS", target->name);
    return 1;
}

static CHANSERV_FUNC(cmd_opchan)
{
    struct mod_chanmode change;
    if(!IsHelping(user) && !channel->channel_info->may_opchan)
    {
        reply("CSMSG_ALREADY_OPCHANNED", channel->name);
        return 0;
    }
    channel->channel_info->may_opchan = 0;
    mod_chanmode_init(&change);
    change.argc = 1;
    change.args[0].mode = MODE_CHANOP;
    change.args[0].u.member = GetUserMode(channel, chanserv);
    if(!change.args[0].u.member)
    {
        reply("CSMSG_OUT_OF_CHANNEL", channel->name);
        return 0;
    }
    mod_chanmode_announce(chanserv, channel, &change);
    reply("CSMSG_OPCHAN_DONE", channel->name);
    return 1;
}

static CHANSERV_FUNC(cmd_adduser)
{
    struct userData *actee;
    struct userData *actor, *real_actor;
    struct handle_info *handle;
    unsigned short access_level, override = 0;

    REQUIRE_PARAMS(3);

    if(channel->channel_info->userCount >= chanserv_conf.max_chan_users)
    {
        reply("CSMSG_MAXIMUM_USERS", chanserv_conf.max_chan_users);
        return 0;
    }

    access_level = user_level_from_name(argv[2], UL_OWNER);
    if(!access_level)
    {
        reply("CSMSG_INVALID_ACCESS", argv[2]);
        return 0;
    }

    actor = GetChannelUser(channel->channel_info, user->handle_info);
    real_actor = GetChannelAccess(channel->channel_info, user->handle_info);

    if(actor->access <= access_level)
    {
        reply("CSMSG_NO_BUMP_ACCESS");
        return 0;
    }

    /* Trying to add someone with equal/more access? */
    if (!real_actor || real_actor->access <= access_level)
        override = CMD_LOG_OVERRIDE;

    if(!(handle = modcmd_get_handle_info(user, argv[1])))
        return 0;

    if((actee = GetTrueChannelAccess(channel->channel_info, handle)))
    {
        reply("CSMSG_USER_EXISTS", handle->handle, channel->name, actee->access);
        return 0;
    }

    actee = add_channel_user(channel->channel_info, handle, access_level, 0, NULL);
    scan_user_presence(actee, NULL);
    reply("CSMSG_ADDED_USER", handle->handle, channel->name, access_level);
    return 1 | override;
}

static CHANSERV_FUNC(cmd_clvl)
{
    struct handle_info *handle;
    struct userData *victim;
    struct userData *actor, *real_actor;
    unsigned short new_access, override = 0;
    int privileged = IsHelping(user) && ((user->handle_info->opserv_level >= chanserv_conf.nodelete_level) || !IsProtected(channel->channel_info));

    REQUIRE_PARAMS(3);

    actor = GetChannelUser(channel->channel_info, user->handle_info);
    real_actor = GetChannelAccess(channel->channel_info, user->handle_info);

    if(!(handle = modcmd_get_handle_info(user, argv[1])))
        return 0;

    if(handle == user->handle_info && !privileged)
    {
        reply("CSMSG_NO_SELF_CLVL");
        return 0;
    }

    if(!(victim = GetTrueChannelAccess(channel->channel_info, handle)))
    {
        reply("CSMSG_NO_CHAN_USER", handle->handle, channel->name);
        return 0;
    }

    if(actor->access <= victim->access && !privileged)
    {
        reply("MSG_USER_OUTRANKED", handle->handle);
        return 0;
    }

    new_access = user_level_from_name(argv[2], UL_OWNER);

    if(!new_access)
    {
        reply("CSMSG_INVALID_ACCESS", argv[2]);
        return 0;
    }

    if(new_access >= actor->access && !privileged)
    {
        reply("CSMSG_NO_BUMP_ACCESS");
        return 0;
    }

    /* Trying to clvl a equal/higher user? */
    if(!real_actor || (real_actor->access <= victim->access && handle != user->handle_info))
        override = CMD_LOG_OVERRIDE;
    /* Trying to clvl someone to equal/higher access? */
    if(!real_actor || new_access >= real_actor->access)
        override = CMD_LOG_OVERRIDE;
    /* Helpers clvling themselves get caught by the "clvl someone to equal/higher access" check.
     * If they lower their own access it's not a big problem.
     */

    victim->access = new_access;
    reply("CSMSG_CHANGED_ACCESS", handle->handle, new_access, channel->name);
    return 1 | override;
}

static CHANSERV_FUNC(cmd_deluser)
{
    struct handle_info *handle;
    struct userData *victim;
    struct userData *actor, *real_actor;
    unsigned short access_level, override = 0;
    char *chan_name;

    REQUIRE_PARAMS(2);

    actor = GetChannelUser(channel->channel_info, user->handle_info);
    real_actor = GetChannelAccess(channel->channel_info, user->handle_info);

    if(!(handle = modcmd_get_handle_info(user, argv[argc-1])))
        return 0;

    if(!(victim = GetTrueChannelAccess(channel->channel_info, handle)))
    {
        reply("CSMSG_NO_CHAN_USER", handle->handle, channel->name);
        return 0;
    }

    if(argc > 2)
    {
        access_level = user_level_from_name(argv[1], UL_OWNER);
        if(!access_level)
        {
            reply("CSMSG_INVALID_ACCESS", argv[1]);
            return 0;
        }
        if(access_level != victim->access)
        {
            reply("CSMSG_INCORRECT_ACCESS", handle->handle, victim->access, argv[1]);
            return 0;
        }
    }
    else
    {
        access_level = victim->access;
    }

    if((actor->access <= victim->access) && !IsHelping(user))
    {
        reply("MSG_USER_OUTRANKED", victim->handle->handle);
        return 0;
    }

    /* If people delete themselves it is an override, but they
     * could've used deleteme so we don't log it as an override
     */
    if(!real_actor || (real_actor->access <= victim->access && real_actor != victim))
        override = CMD_LOG_OVERRIDE;

    chan_name = strdup(channel->name);
    del_channel_user(victim, 1);
    reply("CSMSG_DELETED_USER", handle->handle, access_level, chan_name);
    free(chan_name);
    return 1 | override;
}

static int
cmd_mdel_user(struct userNode *user, struct chanNode *channel, unsigned short min_access, unsigned short max_access, char *mask, struct svccmd *cmd)
{
    struct userData *actor, *real_actor, *uData, *next;
    unsigned int override = 0;

    actor = GetChannelUser(channel->channel_info, user->handle_info);
    real_actor = GetChannelAccess(channel->channel_info, user->handle_info);

    if(min_access > max_access)
    {
        reply("CSMSG_BAD_RANGE", min_access, max_access);
        return 0;
    }

    if(actor->access <= max_access)
    {
        reply("CSMSG_NO_ACCESS");
        return 0;
    }

    if(!real_actor || real_actor->access <= max_access)
        override = CMD_LOG_OVERRIDE;

    for(uData = channel->channel_info->users; uData; uData = next)
    {
        next = uData->next;

        if((uData->access >= min_access)
           && (uData->access <= max_access)
           && match_ircglob(uData->handle->handle, mask))
            del_channel_user(uData, 1);
    }

    reply("CSMSG_DELETED_USERS", mask, min_access, max_access, channel->name);
    return 1 | override;
}

static CHANSERV_FUNC(cmd_mdelowner)
{
    return cmd_mdel_user(user, channel, UL_OWNER, UL_OWNER, argv[1], cmd);
}

static CHANSERV_FUNC(cmd_mdelcoowner)
{
    return cmd_mdel_user(user, channel, UL_COOWNER, UL_COOWNER, argv[1], cmd);
}

static CHANSERV_FUNC(cmd_mdelmaster)
{
    return cmd_mdel_user(user, channel, UL_MASTER, UL_MASTER, argv[1], cmd);
}

static CHANSERV_FUNC(cmd_mdelop)
{
    return cmd_mdel_user(user, channel, UL_OP, UL_OP, argv[1], cmd);
}

static CHANSERV_FUNC(cmd_mdelpeon)
{
    return cmd_mdel_user(user, channel, UL_PEON, UL_PEON, argv[1], cmd);
}

static int
cmd_trim_bans(struct userNode *user, struct chanNode *channel, unsigned long duration)
{
    struct banData *bData, *next;
    char interval[INTERVALLEN];
    unsigned int count;
    unsigned long limit;

    count = 0;
    limit = now - duration;
    for(bData = channel->channel_info->bans; bData; bData = next)
    {
        next = bData->next;

        if((bData->triggered && bData->triggered >= limit) || (bData->set && bData->set >= limit))
            continue;

        del_channel_ban(bData);
        count++;
    }

    intervalString(interval, duration, user->handle_info);
    send_message(user, chanserv, "CSMSG_TRIMMED_BANS", count, channel->name, interval);
    return 1;
}

static int
cmd_trim_users(struct userNode *user, struct chanNode *channel, unsigned short min_access, unsigned short max_access, unsigned long duration, int vacation)
{
    struct userData *actor, *uData, *next;
    char interval[INTERVALLEN];
    unsigned int count;
    unsigned long limit;

    actor = GetChannelUser(channel->channel_info, user->handle_info);
    if(min_access > max_access)
    {
        send_message(user, chanserv, "CSMSG_BAD_RANGE", min_access, max_access);
        return 0;
    }

    if(!actor || actor->access <= max_access)
    {
        send_message(user, chanserv, "CSMSG_NO_ACCESS");
        return 0;
    }

    count = 0;
    limit = now - duration;
    for(uData = channel->channel_info->users; uData; uData = next)
    {
        next = uData->next;

        if((uData->seen > limit)
           || uData->present
           || (HANDLE_FLAGGED(uData->handle, FROZEN) && !vacation))
            continue;

        if(((uData->access >= min_access) && (uData->access <= max_access))
           || (!max_access && (uData->access < actor->access)))
        {
            del_channel_user(uData, 1);
            count++;
        }
    }

    if(!max_access)
    {
        min_access = 1;
        max_access = (actor->access > UL_OWNER) ? UL_OWNER : (actor->access - 1);
    }
    send_message(user, chanserv, "CSMSG_TRIMMED_USERS", count, min_access, max_access, channel->name, intervalString(interval, duration, user->handle_info));
    return 1;
}

static CHANSERV_FUNC(cmd_trim)
{
    unsigned long duration;
    unsigned short min_level, max_level;
    int vacation;

    REQUIRE_PARAMS(3);

    vacation = argc > 3 && !strcmp(argv[3], "vacation");
    duration = ParseInterval(argv[2]);
    if(duration < 60)
    {
        reply("CSMSG_CANNOT_TRIM");
        return 0;
    }

    if(!irccasecmp(argv[1], "bans"))
    {
        cmd_trim_bans(user, channel, duration);
        return 1;
    }
    else if(!irccasecmp(argv[1], "users"))
    {
        cmd_trim_users(user, channel, 0, 0, duration, vacation);
        return 1;
    }
    else if(parse_level_range(&min_level, &max_level, argv[1]))
    {
        cmd_trim_users(user, channel, min_level, max_level, duration, vacation);
        return 1;
    }
    else if((min_level = user_level_from_name(argv[1], UL_OWNER)))
    {
        cmd_trim_users(user, channel, min_level, min_level, duration, vacation);
        return 1;
    }
    else
    {
        reply("CSMSG_INVALID_TRIM", argv[1]);
        return 0;
    }
}

/* If argc is 0 in cmd_up or cmd_down, no notices will be sent
   to the user. cmd_all takes advantage of this. */
static CHANSERV_FUNC(cmd_up)
{
    struct mod_chanmode change;
    struct userData *uData;
    const char *errmsg;

    mod_chanmode_init(&change);
    change.argc = 1;
    change.args[0].u.member = GetUserMode(channel, user);
    if(!change.args[0].u.member)
    {
        if(argc)
            reply("MSG_CHANNEL_ABSENT", channel->name);
        return 0;
    }

    uData = GetChannelAccess(channel->channel_info, user->handle_info);
    if(!uData)
    {
        if(argc)
            reply("CSMSG_GODMODE_UP", argv[0]);
        return 0;
    }
    else if(uData->access >= channel->channel_info->lvlOpts[lvlGiveOps])
    {
        change.args[0].mode = MODE_CHANOP;
        errmsg = "CSMSG_ALREADY_OPPED";
    }
    else if(uData->access >= channel->channel_info->lvlOpts[lvlGiveVoice])
    {
        change.args[0].mode = MODE_VOICE;
        errmsg = "CSMSG_ALREADY_VOICED";
    }
    else
    {
        if(argc)
            reply("CSMSG_NO_ACCESS");
        return 0;
    }
    change.args[0].mode &= ~change.args[0].u.member->modes;
    if(!change.args[0].mode)
    {
        if(argc)
            reply(errmsg, channel->name);
        return 0;
    }
    modcmd_chanmode_announce(&change);
    return 1;
}

static CHANSERV_FUNC(cmd_down)
{
    struct mod_chanmode change;

    mod_chanmode_init(&change);
    change.argc = 1;
    change.args[0].u.member = GetUserMode(channel, user);
    if(!change.args[0].u.member)
    {
        if(argc)
            reply("MSG_CHANNEL_ABSENT", channel->name);
        return 0;
    }

    if(!change.args[0].u.member->modes)
    {
        if(argc)
            reply("CSMSG_ALREADY_DOWN", channel->name);
        return 0;
    }

    change.args[0].mode = MODE_REMOVE | change.args[0].u.member->modes;
    modcmd_chanmode_announce(&change);
    return 1;
}

static int cmd_all(struct userNode *user, UNUSED_ARG(struct chanNode *channel), UNUSED_ARG(unsigned int argc), UNUSED_ARG(char *argv[]), struct svccmd *cmd, modcmd_func_t mcmd)
{
    struct userData *cList;

    for(cList = user->handle_info->channels; cList; cList = cList->u_next)
    {
        if(IsSuspended(cList->channel)
           || IsUserSuspended(cList)
           || !GetUserMode(cList->channel->channel, user))
            continue;

        mcmd(user, cList->channel->channel, 0, NULL, cmd);
    }

    return 1;
}

static CHANSERV_FUNC(cmd_upall)
{
    return cmd_all(CSFUNC_ARGS, cmd_up);
}

static CHANSERV_FUNC(cmd_downall)
{
    return cmd_all(CSFUNC_ARGS, cmd_down);
}

typedef int validate_func_t(struct userNode *user, struct chanNode *channel, struct userNode *victim);
typedef void process_func_t(unsigned int num, struct userNode **newops, struct chanNode *channel, struct userNode *who, int announce);

static int
modify_users(struct userNode *user, struct chanNode *channel, unsigned int argc, char *argv[], struct svccmd *cmd, validate_func_t validate, chan_mode_t mode, char *action)
{
    unsigned int ii, valid;
    struct userNode *victim;
    struct mod_chanmode *change;

    change = mod_chanmode_alloc(argc - 1);

    for(ii=valid=0; ++ii < argc; )
    {
        if(!(victim = GetUserH(argv[ii])))
            continue;
        change->args[valid].mode = mode;
        change->args[valid].u.member = GetUserMode(channel, victim);
        if(!change->args[valid].u.member)
            continue;
        if(validate && !validate(user, channel, victim))
            continue;
        valid++;
    }

    change->argc = valid;
    if(valid < (argc-1))
        reply("CSMSG_PROCESS_FAILED");
    if(valid)
    {
        modcmd_chanmode_announce(change);
        reply(action, channel->name);
    }
    mod_chanmode_free(change);
    return 1;
}

static CHANSERV_FUNC(cmd_op)
{
    return modify_users(CSFUNC_ARGS, validate_op, MODE_CHANOP, "CSMSG_OPPED_USERS");
}

static CHANSERV_FUNC(cmd_deop)
{
    return modify_users(CSFUNC_ARGS, validate_deop, MODE_REMOVE|MODE_CHANOP, "CSMSG_DEOPPED_USERS");
}

static CHANSERV_FUNC(cmd_voice)
{
    return modify_users(CSFUNC_ARGS, NULL, MODE_VOICE, "CSMSG_VOICED_USERS");
}

static CHANSERV_FUNC(cmd_devoice)
{
    return modify_users(CSFUNC_ARGS, NULL, MODE_REMOVE|MODE_VOICE, "CSMSG_DEVOICED_USERS");
}

static int
bad_channel_ban(struct chanNode *channel, struct userNode *user, const char *ban, unsigned int *victimCount, struct modeNode **victims)
{
    unsigned int ii;

    if(victimCount)
        *victimCount = 0;
    for(ii=0; ii<channel->members.used; ii++)
    {
        struct modeNode *mn = channel->members.list[ii];

        if(IsService(mn->user))
            continue;

        if(!user_matches_glob(mn->user, ban, MATCH_USENICK | MATCH_VISIBLE))
            continue;

        if(protect_user(mn->user, user, channel->channel_info))
            return 1;

        if(victims)
            victims[(*victimCount)++] = mn;
    }
    return 0;
}

static int
eject_user(struct userNode *user, struct chanNode *channel, unsigned int argc, char *argv[], struct svccmd *cmd, int action)
{
    struct userNode *victim;
    struct modeNode **victims;
    unsigned int offset, n, victimCount, duration = 0;
    char *reason = "Bye.", *ban, *name;
    char interval[INTERVALLEN];

    offset = (action & ACTION_ADD_TIMED_BAN) ? 3 : 2;
    REQUIRE_PARAMS(offset);
    if(argc > offset)
    {
        reason = unsplit_string(argv + offset, argc - offset, NULL);
        if(strlen(reason) > (TOPICLEN - (NICKLEN + 3)))
        {
            /* Truncate the reason to a length of TOPICLEN, as
               the ircd does; however, leave room for an ellipsis
               and the kicker's nick. */
            sprintf(reason + (TOPICLEN - (NICKLEN + 6)), "...");
        }
    }

    if((victim = GetUserH(argv[1])))
    {
        victims = alloca(sizeof(victims[0]));
        victims[0] = GetUserMode(channel, victim);
        /* XXX: The comparison with ACTION_KICK is just because all
         * other actions can work on users outside the channel, and we
         * want to allow those (e.g.  unbans) in that case.  If we add
         * some other ejection action for in-channel users, change
         * this too. */
        victimCount = victims[0] ? 1 : 0;

        if(IsService(victim))
        {
            reply("MSG_SERVICE_IMMUNE", victim->nick);
            return 0;
        }

        if((action == ACTION_KICK) && !victimCount)
        {
            reply("MSG_CHANNEL_USER_ABSENT", victim->nick, channel->name);
            return 0;
        }

        if(protect_user(victim, user, channel->channel_info))
        {
            reply("CSMSG_USER_PROTECTED", victim->nick);
            return 0;
        }

        ban = generate_hostmask(victim, GENMASK_STRICT_HOST|GENMASK_ANY_IDENT);
        name = victim->nick;
    }
    else if(!is_ircmask(argv[1]) && (*argv[1] == '*'))
    {
        struct handle_info *hi;
        extern const char *titlehost_suffix;
        char banmask[NICKLEN + USERLEN + HOSTLEN + 3];
        const char *accountname = argv[1] + 1;

        if(!(hi = get_handle_info(accountname)))
        {
            reply("MSG_HANDLE_UNKNOWN", accountname);
            return 0;
        }

        snprintf(banmask, sizeof(banmask), "*!*@%s.*.%s", hi->handle, titlehost_suffix);
        victims = alloca(sizeof(victims[0]) * channel->members.used);

        if(bad_channel_ban(channel, user, banmask, &victimCount, victims))
        {
            reply("CSMSG_MASK_PROTECTED", banmask);
            return 0;
        }

        if((action == ACTION_KICK) && (victimCount == 0))
        {
            reply("CSMSG_NO_MATCHING_USERS", channel->name, banmask);
            return 0;
        }

        name = ban = strdup(banmask);
    }
    else
    {
        if(!is_ircmask(argv[1]))
        {
            reply("MSG_NICK_UNKNOWN", argv[1]);
            return 0;
        }

        victims = alloca(sizeof(victims[0]) * channel->members.used);

        if(bad_channel_ban(channel, user, argv[1], &victimCount, victims))
        {
            reply("CSMSG_MASK_PROTECTED", argv[1]);
            return 0;
        }

        if((victimCount > 4) && ((victimCount * 3) > channel->members.used) && !IsOper(user))
        {
            reply("CSMSG_LAME_MASK", argv[1]);
            return 0;
        }

        if((action == ACTION_KICK) && (victimCount == 0))
        {
            reply("CSMSG_NO_MATCHING_USERS", channel->name, argv[1]);
            return 0;
        }

        name = ban = strdup(argv[1]);
    }

    /* Truncate the ban in place if necessary; we must ensure
       that 'ban' is a valid ban mask before sanitizing it. */
    sanitize_ircmask(ban);

    if(action & ACTION_ADD_BAN)
    {
        struct banData *bData, *next;

        if(channel->channel_info->banCount >= chanserv_conf.max_chan_bans)
        {
            reply("CSMSG_MAXIMUM_BANS", chanserv_conf.max_chan_bans);
            free(ban);
            return 0;
        }

        if(action & ACTION_ADD_TIMED_BAN)
        {
            duration = ParseInterval(argv[2]);

            if(duration < 15)
            {
                reply("CSMSG_DURATION_TOO_LOW");
                free(ban);
                return 0;
            }
            else if(duration > (86400 * 365 * 2))
            {
                reply("CSMSG_DURATION_TOO_HIGH");
                free(ban);
                return 0;
            }
        }

        for(bData = channel->channel_info->bans; bData; bData = next)
        {
            if(match_ircglobs(bData->mask, ban))
            {
                int exact = !irccasecmp(bData->mask, ban);

                /* The ban is redundant; there is already a ban
                   with the same effect in place. */
                if(exact)
                {
                    if(bData->reason)
                        free(bData->reason);
                    bData->reason = strdup(reason);
                    safestrncpy(bData->owner, (user->handle_info ? user->handle_info->handle : user->nick), sizeof(bData->owner));
                    if(cmd)
                        reply("CSMSG_REASON_CHANGE", ban);
                    if(!bData->expires)
                        goto post_add_ban;
                }
                if(exact && bData->expires)
                {
                    int reset = 0;

                    /* If the ban matches an existing one exactly,
                       extend the expiration time if the provided
                       duration is longer. */
                    if(duration && (now + duration > bData->expires))
                    {
                        bData->expires = now + duration;
                        reset = 1;
                    }
                    else if(!duration)
                    {
                        bData->expires = 0;
                        reset = 1;
                    }

                    if(reset)
                    {
                        /* Delete the expiration timeq entry and
                           requeue if necessary. */
                        timeq_del(0, expire_ban, bData, TIMEQ_IGNORE_WHEN);

                        if(bData->expires)
                            timeq_add(bData->expires, expire_ban, bData);

                        if(!cmd)
                        {
                            /* automated kickban */
                        }
                        else if(duration)
                            reply("CSMSG_BAN_EXTENDED", ban, intervalString(interval, duration, user->handle_info));
                        else
                            reply("CSMSG_BAN_ADDED", name, channel->name);

                        goto post_add_ban;
                    }
                }
                if(cmd)
                    reply("CSMSG_REDUNDANT_BAN", name, channel->name);

                free(ban);
                return 0;
            }

            next = bData->next;
            if(match_ircglobs(ban, bData->mask))
            {
                /* The ban we are adding makes previously existing
                   bans redundant; silently remove them. */
                del_channel_ban(bData);
            }
        }

        bData = add_channel_ban(channel->channel_info, ban, (user->handle_info ? user->handle_info->handle : user->nick), now, (victimCount ? now : 0), (duration ? now + duration : 0), reason);
        free(ban);
        name = ban = strdup(bData->mask);
    }
    else if(ban)
    {
        for(n = 0; n < chanserv_conf.old_ban_names->used; ++n)
        {
            extern const char *hidden_host_suffix;
            const char *old_name = chanserv_conf.old_ban_names->list[n];
            char *new_mask;
            unsigned int l1, l2;

            l1 = strlen(ban);
            l2 = strlen(old_name);
            if(l2+2 > l1)
                continue;
            if(irccasecmp(ban + l1 - l2, old_name))
                continue;
            new_mask = malloc(MAXLEN);
            sprintf(new_mask, "%.*s%s", (int)(l1-l2), ban, hidden_host_suffix);
            free(ban);
            name = ban = new_mask;
        }
    }

    post_add_ban:
    if(action & ACTION_BAN)
    {
        unsigned int exists;
        struct mod_chanmode *change;

        if(channel->banlist.used >= MAXBANS)
        {
            if(cmd)
                reply("CSMSG_BANLIST_FULL", channel->name);
            free(ban);
            return 0;
        }

        exists = ChannelBanExists(channel, ban);
        change = mod_chanmode_alloc(victimCount + 1);
        for(n = 0; n < victimCount; ++n)
        {
            change->args[n].mode = MODE_REMOVE|MODE_CHANOP|MODE_VOICE;
            change->args[n].u.member = victims[n];
        }
        if(!exists)
        {
            change->args[n].mode = MODE_BAN;
            change->args[n++].u.hostmask = ban;
        }
        change->argc = n;
        if(cmd)
            modcmd_chanmode_announce(change);
        else
            mod_chanmode_announce(chanserv, channel, change);
        mod_chanmode_free(change);

        if(exists && (action == ACTION_BAN))
        {
            if(cmd)
                reply("CSMSG_REDUNDANT_BAN", name, channel->name);
            free(ban);
            return 0;
        }
    }

    if(action & ACTION_KICK)
    {
        char kick_reason[MAXLEN];
        sprintf(kick_reason, "(%s) %s", user->nick, reason);

        for(n = 0; n < victimCount; n++)
            KickChannelUser(victims[n]->user, channel, chanserv, kick_reason);
    }

    if(!cmd)
    {
        /* No response, since it was automated. */
    }
    else if(action & ACTION_ADD_BAN)
    {
        if(duration)
            reply("CSMSG_TIMED_BAN_ADDED", name, channel->name, intervalString(interval, duration, user->handle_info));
        else
            reply("CSMSG_BAN_ADDED", name, channel->name);
    }
    else if((action & (ACTION_BAN | ACTION_KICK)) == (ACTION_BAN | ACTION_KICK))
        reply("CSMSG_KICK_BAN_DONE", name, channel->name);
    else if(action & ACTION_BAN)
        reply("CSMSG_BAN_DONE", name, channel->name);
    else if(action & ACTION_KICK && victimCount)
        reply("CSMSG_KICK_DONE", name, channel->name);

    free(ban);
    return 1;
}

static CHANSERV_FUNC(cmd_kickban)
{
    return eject_user(CSFUNC_ARGS, ACTION_KICK | ACTION_BAN);
}

static CHANSERV_FUNC(cmd_kick)
{
    return eject_user(CSFUNC_ARGS, ACTION_KICK);
}

static CHANSERV_FUNC(cmd_ban)
{
    return eject_user(CSFUNC_ARGS, ACTION_BAN);
}

static CHANSERV_FUNC(cmd_addban)
{
    return eject_user(CSFUNC_ARGS, ACTION_KICK | ACTION_BAN | ACTION_ADD_BAN);
}

static CHANSERV_FUNC(cmd_addtimedban)
{
    return eject_user(CSFUNC_ARGS, ACTION_KICK | ACTION_BAN | ACTION_ADD_BAN | ACTION_ADD_TIMED_BAN);
}

static struct mod_chanmode *
find_matching_bans(struct banList *bans, struct userNode *actee, const char *mask)
{
    struct mod_chanmode *change;
    unsigned char *match;
    unsigned int ii, count;

    match = alloca(bans->used);
    if(actee)
    {
        for(ii = count = 0; ii < bans->used; ++ii)
        {
            match[ii] = user_matches_glob(actee, bans->list[ii]->ban,
                                          MATCH_USENICK | MATCH_VISIBLE);
            if(match[ii])
                count++;
        }
    }
    else
    {
        for(ii = count = 0; ii < bans->used; ++ii)
        {
            match[ii] = match_ircglobs(mask, bans->list[ii]->ban);
            if(match[ii])
                count++;
        }
    }
    if(!count)
        return NULL;
    change = mod_chanmode_alloc(count);
    for(ii = count = 0; ii < bans->used; ++ii)
    {
        if(!match[ii])
            continue;
        change->args[count].mode = MODE_REMOVE | MODE_BAN;
        change->args[count++].u.hostmask = strdup(bans->list[ii]->ban);
    }
    assert(count == change->argc);
    return change;
}

static int
unban_user(struct userNode *user, struct chanNode *channel, unsigned int argc, char *argv[], struct svccmd *cmd, int action)
{
    struct userNode *actee;
    char *mask = NULL;
    int acted = 0;

    REQUIRE_PARAMS(2);

    /* may want to allow a comma delimited list of users... */
    if(!(actee = GetUserH(argv[1])))
    {
        if(!is_ircmask(argv[1]) && *argv[1] == '*')
        {
            char banmask[NICKLEN + USERLEN + HOSTLEN + 3];
            const char *accountname = argv[1] + 1;

            snprintf(banmask, sizeof(banmask), "*!*@%s.*", accountname);
            mask = strdup(banmask);
        }
        else if(!is_ircmask(argv[1]))
        {
            reply("MSG_NICK_UNKNOWN", argv[1]);
            return 0;
        }
        else
        {
            mask = strdup(argv[1]);
        }
    }

    /* We don't sanitize the mask here because ircu
       doesn't do it. */
    if(action & ACTION_UNBAN)
    {
        struct mod_chanmode *change;
        change = find_matching_bans(&channel->banlist, actee, mask);
        if(change)
        {
            unsigned int ii;

            modcmd_chanmode_announce(change);
            for(ii = 0; ii < change->argc; ++ii)
                free((char*)change->args[ii].u.hostmask);
            mod_chanmode_free(change);
            acted = 1;
        }
    }

    if(action & ACTION_DEL_BAN)
    {
        struct banData *ban, *next;

        ban = channel->channel_info->bans;
        while(ban)
        {
            if(actee)
                for( ; ban && !user_matches_glob(actee, ban->mask,
                                                 MATCH_USENICK | MATCH_VISIBLE);
                     ban = ban->next);
            else
                for( ; ban && !match_ircglobs(mask, ban->mask);
                     ban = ban->next);
            if(!ban)
                break;
            next = ban->next;
            del_channel_ban(ban);
            ban = next;
            acted = 1;
        }
    }

    if(!acted)
        reply("CSMSG_BAN_NOT_FOUND", actee ? actee->nick : mask);
    else
        reply("CSMSG_BAN_REMOVED", actee ? actee->nick : mask);
    if(mask)
        free(mask);
    return 1;
}

static CHANSERV_FUNC(cmd_unban)
{
    return unban_user(CSFUNC_ARGS, ACTION_UNBAN);
}

static CHANSERV_FUNC(cmd_delban)
{
    /* it doesn't necessarily have to remove the channel ban - may want
       to make that an option. */
    return unban_user(CSFUNC_ARGS, ACTION_UNBAN | ACTION_DEL_BAN);
}

static CHANSERV_FUNC(cmd_unbanme)
{
    struct userData *uData = GetChannelUser(channel->channel_info, user->handle_info);
    long flags = ACTION_UNBAN;

    /* remove permanent bans if the user has the proper access. */
    if(uData->access >= UL_MASTER)
        flags |= ACTION_DEL_BAN;

    argv[1] = user->nick;
    return unban_user(user, channel, 2, argv, cmd, flags);
}

static CHANSERV_FUNC(cmd_unbanall)
{
    struct mod_chanmode *change;
    unsigned int ii;

    if(!channel->banlist.used)
    {
        reply("CSMSG_NO_BANS", channel->name);
        return 0;
    }

    change = mod_chanmode_alloc(channel->banlist.used);
    for(ii=0; ii<channel->banlist.used; ii++)
    {
        change->args[ii].mode = MODE_REMOVE | MODE_BAN;
        change->args[ii].u.hostmask = strdup(channel->banlist.list[ii]->ban);
    }
    modcmd_chanmode_announce(change);
    for(ii = 0; ii < change->argc; ++ii)
        free((char*)change->args[ii].u.hostmask);
    mod_chanmode_free(change);
    reply("CSMSG_BANS_REMOVED", channel->name);
    return 1;
}

static CHANSERV_FUNC(cmd_open)
{
    struct mod_chanmode *change;
    unsigned int ii;

    change = find_matching_bans(&channel->banlist, user, NULL);
    if(!change)
        change = mod_chanmode_alloc(0);
    change->modes_clear |= MODE_INVITEONLY | MODE_LIMIT | MODE_KEY;
    if(!check_user_level(channel, user, lvlEnfModes, 1, 0)
       && channel->channel_info->modes.modes_set)
        change->modes_clear &= ~channel->channel_info->modes.modes_set;
    modcmd_chanmode_announce(change);
    reply("CSMSG_CHANNEL_OPENED", channel->name);
    for(ii = 0; ii < change->argc; ++ii)
        free((char*)change->args[ii].u.hostmask);
    mod_chanmode_free(change);
    return 1;
}

static CHANSERV_FUNC(cmd_myaccess)
{
    static struct string_buffer sbuf;
    struct handle_info *target_handle;
    struct userData *uData;

    if(argc < 2)
        target_handle = user->handle_info;
    else if(!IsStaff(user))
    {
        reply("CSMSG_MYACCESS_SELF_ONLY", argv[0]);
        return 0;
    }
    else if(!(target_handle = modcmd_get_handle_info(user, argv[1])))
        return 0;

    if(!oper_outranks(user, target_handle))
        return 0;

    if(!target_handle->channels)
    {
        reply("CSMSG_SQUAT_ACCESS", target_handle->handle);
        return 1;
    }

    reply("CSMSG_INFOLINE_LIST", target_handle->handle);
    for(uData = target_handle->channels; uData; uData = uData->u_next)
    {
        struct chanData *cData = uData->channel;
        unsigned int base_len;

        if(uData->access > UL_OWNER)
            continue;
        if(IsProtected(cData)
           && (target_handle != user->handle_info)
           && !GetTrueChannelAccess(cData, user->handle_info))
            continue;
        sbuf.used = 0;
        string_buffer_append_printf(&sbuf, "[%s (%d,", cData->channel->name, uData->access);
        base_len = sbuf.used;
        if(IsUserSuspended(uData))
            string_buffer_append(&sbuf, 's');
        if(IsUserAutoOp(uData))
        {
            if(uData->access >= cData->lvlOpts[lvlGiveOps])
                string_buffer_append(&sbuf, 'o');
            else if(uData->access >= cData->lvlOpts[lvlGiveVoice])
                string_buffer_append(&sbuf, 'v');
        }
        if(IsUserAutoInvite(uData) && (uData->access >= cData->lvlOpts[lvlInviteMe]))
            string_buffer_append(&sbuf, 'i');
        if(sbuf.used==base_len)
            sbuf.used--;
        if(uData->info)
            string_buffer_append_printf(&sbuf, ")] %s", uData->info);
        else
            string_buffer_append_string(&sbuf, ")]");
        string_buffer_append(&sbuf, '\0');
        send_message_type(4, user, cmd->parent->bot, "%s", sbuf.list);
    }

    return 1;
}

static CHANSERV_FUNC(cmd_access)
{
    struct userNode *target;
    struct handle_info *target_handle;
    struct userData *uData;
    int helping;
    char prefix[MAXLEN];

    if(argc < 2)
    {
        target = user;
        target_handle = target->handle_info;
    }
    else if((target = GetUserH(argv[1])))
    {
        target_handle = target->handle_info;
    }
    else if(argv[1][0] == '*')
    {
        if(!(target_handle = get_handle_info(argv[1]+1)))
        {
            reply("MSG_HANDLE_UNKNOWN", argv[1]+1);
            return 0;
        }
    }
    else
    {
        reply("MSG_NICK_UNKNOWN", argv[1]);
        return 0;
    }

    assert(target || target_handle);

    if(target == chanserv)
    {
        reply("CSMSG_IS_CHANSERV");
        return 1;
    }

    if(!target_handle)
    {
        if(IsOper(target))
        {
            reply("CSMSG_LAZY_SMURF_TARGET", target->nick, chanserv_conf.irc_operator_epithet);
            return 0;
        }
        if(target != user)
        {
            reply("MSG_USER_AUTHENTICATE", target->nick);
            return 0;
        }
        reply("MSG_AUTHENTICATE");
        return 0;
    }

    if(target)
    {
        const char *epithet = NULL, *type = NULL;
        if(IsOper(target))
        {
            epithet = chanserv_conf.irc_operator_epithet;
            type = user_find_message(user, "CSMSG_OPERATOR_TITLE");
        }
        else if(IsNetworkHelper(target))
        {
            epithet = chanserv_conf.network_helper_epithet;
            type = user_find_message(user, "CSMSG_UC_H_TITLE");
        }
        else if(IsSupportHelper(target))
        {
            epithet = chanserv_conf.support_helper_epithet;
            type = user_find_message(user, "CSMSG_LC_H_TITLE");
        }
        if(epithet)
        {
            if(target_handle->epithet)
                reply("CSMSG_SMURF_TARGET", target->nick, target_handle->epithet, type);
            else if(epithet)
                reply("CSMSG_SMURF_TARGET", target->nick, epithet, type);
        }
        sprintf(prefix, "%s (%s)", target->nick, target_handle->handle);
    }
    else
    {
        sprintf(prefix, "%s", target_handle->handle);
    }

    if(!channel->channel_info)
    {
        reply("CSMSG_NOT_REGISTERED", channel->name);
        return 1;
    }

    helping = HANDLE_FLAGGED(target_handle, HELPING)
        && ((target_handle->opserv_level >= chanserv_conf.nodelete_level) || !IsProtected(channel->channel_info));
    if((uData = GetTrueChannelAccess(channel->channel_info, target_handle)))
    {
        reply((helping ? "CSMSG_HELPER_HAS_ACCESS" : "CSMSG_USER_HAS_ACCESS"), prefix, uData->access, channel->name);
        /* To prevent possible information leaks, only show infolines
         * if the requestor is in the channel or it's their own
         * handle. */
        if(uData->info && (GetUserMode(channel, user) || (target_handle == user->handle_info)))
        {
            send_message_type(4, user, cmd->parent->bot, "[%s] %s", (target ? target->nick : target_handle->handle), uData->info);
        }
        /* Likewise, only say it's suspended if the user has active
         * access in that channel or it's their own entry. */
        if(IsUserSuspended(uData)
           && (GetChannelUser(channel->channel_info, user->handle_info)
               || (user->handle_info == uData->handle)))
        {
            reply("CSMSG_USER_SUSPENDED", (target ? target->nick : target_handle->handle), channel->name);
        }
    }
    else
    {
        reply((helping ? "CSMSG_HELPER_NO_ACCESS" : "CSMSG_USER_NO_ACCESS"), prefix, channel->name);
    }

    return 1;
}

static void
def_list(struct listData *list)
{
    const char *msg;
    if(list->search)
        send_message(list->user, list->bot, "CSMSG_ACCESS_SEARCH_HEADER", list->channel->name, list->lowest, list->highest, list->search);
    else
        send_message(list->user, list->bot, "CSMSG_ACCESS_ALL_HEADER", list->channel->name, list->lowest, list->highest);
    table_send(list->bot, list->user->nick, 0, NULL, list->table);
    if(list->table.length == 1)
    {
        msg = user_find_message(list->user, "MSG_NONE");
        send_message_type(4, list->user, list->bot, "  %s", msg);
    }
}

static int
userData_access_comp(const void *arg_a, const void *arg_b)
{
    const struct userData *a = *(struct userData**)arg_a;
    const struct userData *b = *(struct userData**)arg_b;
    int res;
    if(a->access != b->access)
        res = b->access - a->access;
    else
        res = irccasecmp(a->handle->handle, b->handle->handle);
    return res;
}

static int
cmd_list_users(struct userNode *user, struct chanNode *channel, unsigned int argc, char *argv[], struct svccmd *cmd, unsigned short lowest, unsigned short highest)
{
    void (*send_list)(struct listData *);
    struct userData *uData;
    struct listData lData;
    unsigned int matches;
    const char **ary;

    lData.user = user;
    lData.bot = cmd->parent->bot;
    lData.channel = channel;
    lData.lowest = lowest;
    lData.highest = highest;
    lData.search = (argc > 1) ? argv[1] : NULL;
    send_list = def_list;

    if(user->handle_info)
    {
        switch(user->handle_info->userlist_style)
        {
        case HI_STYLE_DEF: send_list = def_list; break;
        case HI_STYLE_ZOOT: send_list = def_list; break;
        }
    }

    lData.users = alloca(channel->channel_info->userCount * sizeof(struct userData *));
    matches = 0;
    for(uData = channel->channel_info->users; uData; uData = uData->next)
    {
        if((uData->access < lowest)
           || (uData->access > highest)
           || (lData.search && !match_ircglob(uData->handle->handle, lData.search)))
            continue;
        lData.users[matches++] = uData;
    }
    qsort(lData.users, matches, sizeof(lData.users[0]), userData_access_comp);

    lData.table.length = matches+1;
    lData.table.width = 4;
    lData.table.flags = TABLE_NO_FREE;
    lData.table.contents = malloc(lData.table.length*sizeof(*lData.table.contents));
    ary = malloc(lData.table.width*sizeof(**lData.table.contents));
    lData.table.contents[0] = ary;
    ary[0] = "Access";
    ary[1] = "Account";
    ary[2] = "Last Seen";
    ary[3] = "Status";
    for(matches = 1; matches < lData.table.length; ++matches)
    {
        char seen[INTERVALLEN];

        uData = lData.users[matches-1];
        ary = malloc(lData.table.width*sizeof(**lData.table.contents));
        lData.table.contents[matches] = ary;
        ary[0] = strtab(uData->access);
        ary[1] = uData->handle->handle;
        if(uData->present)
            ary[2] = "Here";
        else if(!uData->seen)
            ary[2] = "Never";
        else
            ary[2] = intervalString(seen, now - uData->seen, user->handle_info);
        ary[2] = strdup(ary[2]);
        if(IsUserSuspended(uData))
            ary[3] = "Suspended";
        else if(HANDLE_FLAGGED(uData->handle, FROZEN))
            ary[3] = "Vacation";
        else if(HANDLE_FLAGGED(uData->handle, BOT))
            ary[3] = "Bot";
        else
            ary[3] = "Normal";
    }
    send_list(&lData);
    for(matches = 1; matches < lData.table.length; ++matches)
    {
        free((char*)lData.table.contents[matches][2]);
        free(lData.table.contents[matches]);
    }
    free(lData.table.contents[0]);
    free(lData.table.contents);
    return 1;
}

static CHANSERV_FUNC(cmd_users)
{
    return cmd_list_users(CSFUNC_ARGS, 1, UL_OWNER);
}

static CHANSERV_FUNC(cmd_wlist)
{
    return cmd_list_users(CSFUNC_ARGS, UL_OWNER, UL_OWNER);
}

static CHANSERV_FUNC(cmd_clist)
{
    return cmd_list_users(CSFUNC_ARGS, UL_COOWNER, UL_OWNER-1);
}

static CHANSERV_FUNC(cmd_mlist)
{
    return cmd_list_users(CSFUNC_ARGS, UL_MASTER, UL_COOWNER-1);
}

static CHANSERV_FUNC(cmd_olist)
{
    return cmd_list_users(CSFUNC_ARGS, UL_OP, UL_MASTER-1);
}

static CHANSERV_FUNC(cmd_plist)
{
    return cmd_list_users(CSFUNC_ARGS, 1, UL_OP-1);
}

static CHANSERV_FUNC(cmd_bans)
{
    struct userNode *search_u = NULL;
    struct helpfile_table tbl;
    unsigned int matches = 0, timed = 0, search_wilds = 0, ii;
    char t_buffer[INTERVALLEN], e_buffer[INTERVALLEN], *search;
    const char *msg_never, *triggered, *expires;
    struct banData *ban, **bans;

    if(argc < 2)
        search = NULL;
    else if(strchr(search = argv[1], '!'))
    {
        search = argv[1];
        search_wilds = search[strcspn(search, "?*")];
    }
    else if(!(search_u = GetUserH(search)))
        reply("MSG_NICK_UNKNOWN", search);

    bans = alloca(channel->channel_info->banCount * sizeof(struct banData *));

    for(ban = channel->channel_info->bans; ban; ban = ban->next)
    {
        if(search_u)
        {
            if(!user_matches_glob(search_u, ban->mask, MATCH_USENICK | MATCH_VISIBLE))
                continue;
        }
        else if(search)
        {
            if(search_wilds ? !match_ircglobs(search, ban->mask) : !match_ircglob(search, ban->mask))
                continue;
        }
        bans[matches++] = ban;
        if(ban->expires)
            timed = 1;
    }

    tbl.length = matches + 1;
    tbl.width = 4 + timed;
    tbl.flags = 0;
    tbl.flags = TABLE_NO_FREE;
    tbl.contents = malloc(tbl.length * sizeof(tbl.contents[0]));
    tbl.contents[0] = malloc(tbl.width * sizeof(tbl.contents[0][0]));
    tbl.contents[0][0] = "Mask";
    tbl.contents[0][1] = "Set By";
    tbl.contents[0][2] = "Triggered";
    if(timed)
    {
        tbl.contents[0][3] = "Expires";
        tbl.contents[0][4] = "Reason";
    }
    else
        tbl.contents[0][3] = "Reason";
    if(!matches)
    {
        table_send(cmd->parent->bot, user->nick, 0, NULL, tbl);
        reply("MSG_NONE");
        free(tbl.contents[0]);
        free(tbl.contents);
        return 0;
    }

    msg_never = user_find_message(user, "MSG_NEVER");
    for(ii = 0; ii < matches; )
    {
        ban = bans[ii];

        if(!timed)
            expires = "";
        else if(ban->expires)
            expires = intervalString(e_buffer, ban->expires - now, user->handle_info);
        else
            expires = msg_never;

        if(ban->triggered)
            triggered = intervalString(t_buffer, now - ban->triggered, user->handle_info);
        else
            triggered = msg_never;

        tbl.contents[++ii] = malloc(tbl.width * sizeof(tbl.contents[0][0]));
        tbl.contents[ii][0] = ban->mask;
        tbl.contents[ii][1] = ban->owner;
        tbl.contents[ii][2] = strdup(triggered);
        if(timed)
        {
            tbl.contents[ii][3] = strdup(expires);
            tbl.contents[ii][4] = ban->reason;
        }
        else
            tbl.contents[ii][3] = ban->reason;
    }
    table_send(cmd->parent->bot, user->nick, 0, NULL, tbl);
    reply("MSG_MATCH_COUNT", matches);
    for(ii = 1; ii < tbl.length; ++ii)
    {
        free((char*)tbl.contents[ii][2]);
        if(timed)
            free((char*)tbl.contents[ii][3]);
        free(tbl.contents[ii]);
    }
    free(tbl.contents[0]);
    free(tbl.contents);
    return 1;
}

static int
bad_topic(struct chanNode *channel, struct userNode *user, const char *new_topic)
{
    struct chanData *cData = channel->channel_info;
    if(check_user_level(channel, user, lvlEnfTopic, 1, 0))
        return 0;
    if(cData->topic_mask)
        return !match_ircglob(new_topic, cData->topic_mask);
    else if(cData->topic)
        return irccasecmp(new_topic, cData->topic);
    else
        return 0;
}

static CHANSERV_FUNC(cmd_topic)
{
    struct chanData *cData;
    char *topic;

    cData = channel->channel_info;
    if(argc < 2)
    {
        if(cData->topic)
        {
            SetChannelTopic(channel, chanserv, cData->topic, 1);
            reply("CSMSG_TOPIC_SET", cData->topic);
            return 1;
        }

        reply("CSMSG_NO_TOPIC", channel->name);
        return 0;
    }

    topic = unsplit_string(argv + 1, argc - 1, NULL);
    /* If they say "!topic *", use an empty topic. */
    if((topic[0] == '*') && (topic[1] == 0))
        topic[0] = 0;
    if(bad_topic(channel, user, topic))
    {
        char *topic_mask = cData->topic_mask;
        if(topic_mask)
        {
            char new_topic[TOPICLEN+1], tchar;
            int pos=0, starpos=-1, dpos=0, len;

            while((tchar = topic_mask[pos++]) && (dpos <= TOPICLEN))
            {
                switch(tchar)
                {
                case '*':
                    if(starpos != -1)
                        goto bad_mask;
                    len = strlen(topic);
                    if((dpos + len) > TOPICLEN)
                        len = TOPICLEN + 1 - dpos;
                    memcpy(new_topic+dpos, topic, len);
                    dpos += len;
                    starpos = pos;
                    break;
                case '\\': tchar = topic_mask[pos++]; /* and fall through */
                default: new_topic[dpos++] = tchar; break;
                }
            }
            if((dpos > TOPICLEN) || tchar)
            {
            bad_mask:
                reply("CSMSG_TOPICMASK_CONFLICT1", channel->name, topic_mask);
                reply("CSMSG_TOPICMASK_CONFLICT2", TOPICLEN);
                return 0;
            }
            new_topic[dpos] = 0;
            SetChannelTopic(channel, chanserv, new_topic, 1);
        } else {
            reply("CSMSG_TOPIC_LOCKED", channel->name);
            return 0;
        }
    }
    else
        SetChannelTopic(channel, chanserv, topic, 1);

    if(check_user_level(channel, user, lvlTopicSnarf, 1, 0))
    {
        /* Grab the topic and save it as the default topic. */
        free(cData->topic);
        cData->topic = strdup(channel->topic);
    }

    return 1;
}

static CHANSERV_FUNC(cmd_mode)
{
    struct userData *uData;
    struct mod_chanmode *change;
    short base_oplevel;
    char fmt[MAXLEN];

    if(argc < 2)
    {
        change = &channel->channel_info->modes;
        if(change->modes_set || change->modes_clear) {
            modcmd_chanmode_announce(change);
            reply("CSMSG_DEFAULTED_MODES", channel->name);
        } else
            reply("CSMSG_NO_MODES", channel->name);
        return 1;
    }

    uData = GetChannelUser(channel->channel_info, user->handle_info);
    if (!uData)
        base_oplevel = MAXOPLEVEL;
    else if (uData->access >= UL_OWNER)
        base_oplevel = 1;
    else
        base_oplevel = 1 + UL_OWNER - uData->access;
    change = mod_chanmode_parse(channel, argv+1, argc-1, MCP_KEY_FREE|MCP_REGISTERED|MCP_NO_APASS, base_oplevel);
    if(!change)
    {
        reply("MSG_INVALID_MODES", unsplit_string(argv+1, argc-1, NULL));
        return 0;
    }

    if(!check_user_level(channel, user, lvlEnfModes, 1, 0)
       && mode_lock_violated(&channel->channel_info->modes, change))
    {
        char modes[MAXLEN];
        mod_chanmode_format(&channel->channel_info->modes, modes);
        reply("CSMSG_MODE_LOCKED", modes, channel->name);
        return 0;
    }

    modcmd_chanmode_announce(change);
    mod_chanmode_format(change, fmt);
    mod_chanmode_free(change);
    reply("CSMSG_MODES_SET", fmt);
    return 1;
}

static CHANSERV_FUNC(cmd_invite)
{
    struct userNode *invite;

    if(argc > 1)
    {
        if(!(invite = GetUserH(argv[1])))
        {
            reply("MSG_NICK_UNKNOWN", argv[1]);
            return 0;
        }
    }
    else
        invite = user;

    if(GetUserMode(channel, invite))
    {
        reply("CSMSG_ALREADY_PRESENT", invite->nick, channel->name);
        return 0;
    }

    if(user != invite)
    {
        if(argc > 2)
        {
            char *reason = unsplit_string(argv + 2, argc - 2, NULL);
            send_message(invite, chanserv, "CSMSG_INVITING_YOU_REASON", user->nick, channel->name, reason);
        }
        else
            send_message(invite, chanserv, "CSMSG_INVITING_YOU", user->nick, channel->name);
    }
    irc_invite(chanserv, invite, channel);
    if(argc > 1)
        reply("CSMSG_INVITED_USER", argv[1], channel->name);

    return 1;
}

static CHANSERV_FUNC(cmd_inviteme)
{
    if(GetUserMode(channel, user))
    {
        reply("CSMSG_YOU_ALREADY_PRESENT", channel->name);
        return 0;
    }
    if(channel->channel_info
       && !check_user_level(channel, user, lvlInviteMe, 1, 0))
    {
        reply("CSMSG_LOW_CHANNEL_ACCESS", channel->name);
        return 0;
    }
    irc_invite(cmd->parent->bot, user, channel);
    return 1;
}

static void
show_suspension_info(struct svccmd *cmd, struct userNode *user, struct suspended *suspended)
{
    unsigned int combo;
    char buf1[INTERVALLEN], buf2[INTERVALLEN];

    /* We display things based on two dimensions:
     * - Issue time: present or absent
     * - Expiration: revoked, expired, expires in future, or indefinite expiration
     * (in order of precedence, so something both expired and revoked
     * only counts as revoked)
     */
    combo = (suspended->issued ? 4 : 0)
        + (suspended->revoked ? 3 : suspended->expires ? ((suspended->expires < now) ? 2 : 1) : 0);
    switch(combo) {
    case 0: /* no issue time, indefinite expiration */
        reply("CSMSG_CHANNEL_SUSPENDED_0", suspended->suspender, suspended->reason);
        break;
    case 1: /* no issue time, expires in future */
        intervalString(buf1, suspended->expires-now, user->handle_info);
        reply("CSMSG_CHANNEL_SUSPENDED_1", suspended->suspender, buf1, suspended->reason);
        break;
    case 2: /* no issue time, expired */
        intervalString(buf1, now-suspended->expires, user->handle_info);
        reply("CSMSG_CHANNEL_SUSPENDED_2", suspended->suspender, buf1, suspended->reason);
        break;
    case 3: /* no issue time, revoked */
        intervalString(buf1, now-suspended->revoked, user->handle_info);
        reply("CSMSG_CHANNEL_SUSPENDED_3", suspended->suspender, buf1, suspended->reason);
        break;
    case 4: /* issue time set, indefinite expiration */
        intervalString(buf1, now-suspended->issued, user->handle_info);
        reply("CSMSG_CHANNEL_SUSPENDED_4", buf1, suspended->suspender, suspended->reason);
        break;
    case 5: /* issue time set, expires in future */
        intervalString(buf1, now-suspended->issued, user->handle_info);
        intervalString(buf2, suspended->expires-now, user->handle_info);
        reply("CSMSG_CHANNEL_SUSPENDED_5", buf1, suspended->suspender, buf2, suspended->reason);
        break;
    case 6: /* issue time set, expired */
        intervalString(buf1, now-suspended->issued, user->handle_info);
        intervalString(buf2, now-suspended->expires, user->handle_info);
        reply("CSMSG_CHANNEL_SUSPENDED_6", buf1, suspended->suspender, buf2, suspended->reason);
        break;
    case 7: /* issue time set, revoked */
        intervalString(buf1, now-suspended->issued, user->handle_info);
        intervalString(buf2, now-suspended->revoked, user->handle_info);
        reply("CSMSG_CHANNEL_SUSPENDED_7", buf1, suspended->suspender, buf2, suspended->reason);
        break;
    default:
        log_module(CS_LOG, LOG_ERROR, "Invalid combo value %d in show_suspension_info()", combo);
        return;
    }
}

static CHANSERV_FUNC(cmd_info)
{
    char modes[MAXLEN], buffer[INTERVALLEN];
    struct userData *uData, *owner;
    struct chanData *cData;
    struct do_not_register *dnr;
    struct note *note;
    dict_iterator_t it;
    int privileged;

    cData = channel->channel_info;
    reply("CSMSG_CHANNEL_INFO", channel->name);

    uData = GetChannelUser(cData, user->handle_info);
    if(uData && (uData->access >= cData->lvlOpts[lvlGiveOps]))
    {
        mod_chanmode_format(&cData->modes, modes);
        reply("CSMSG_CHANNEL_TOPIC", cData->topic);
        reply("CSMSG_CHANNEL_MODES", modes[0] ? modes : user_find_message(user, "MSG_NONE"));
    }

    for(it = dict_first(cData->notes); it; it = iter_next(it))
    {
        int padding;

        note = iter_data(it);
        if(!note_type_visible_to_user(cData, note->type, user))
            continue;

        padding = PADLEN - 1 - strlen(iter_key(it));
        reply("CSMSG_CHANNEL_NOTE", iter_key(it), padding > 0 ? padding : 1, "", note->note);
    }

    reply("CSMSG_CHANNEL_MAX", cData->max);
    for(owner = cData->users; owner; owner = owner->next)
        if(owner->access == UL_OWNER)
            reply("CSMSG_CHANNEL_OWNER", owner->handle->handle);
    reply("CSMSG_CHANNEL_USERS", cData->userCount);
    reply("CSMSG_CHANNEL_BANS", cData->banCount);
    reply("CSMSG_CHANNEL_VISITED", intervalString(buffer, now - cData->visited, user->handle_info));

    privileged = IsStaff(user);
    if(privileged)
        reply("CSMSG_CHANNEL_REGISTERED", intervalString(buffer, now - cData->registered, user->handle_info));
    if(((uData && uData->access >= UL_COOWNER) || privileged) && cData->registrar)
        reply("CSMSG_CHANNEL_REGISTRAR", cData->registrar);

    if(privileged && (dnr = chanserv_is_dnr(channel->name, NULL)))
        chanserv_show_dnrs(user, cmd, channel->name, NULL);

    if(cData->suspended && ((uData && (uData->access >= UL_COOWNER)) || IsHelping(user)))
    {
        struct suspended *suspended;
        reply((IsSuspended(cData) ? "CSMSG_CHANNEL_SUSPENDED" : "CSMSG_CHANNEL_HISTORY"), channel->name);
        for(suspended = cData->suspended; suspended; suspended = suspended->previous)
            show_suspension_info(cmd, user, suspended);
    }
    else if(IsSuspended(cData))
    {
        reply("CSMSG_CHANNEL_SUSPENDED", channel->name);
        show_suspension_info(cmd, user, cData->suspended);
    }
    return 1;
}

static CHANSERV_FUNC(cmd_netinfo)
{
    extern unsigned long boot_time;
    extern unsigned long burst_length;
    char interval[INTERVALLEN];

    reply("CSMSG_NETWORK_INFO");
    reply("CSMSG_NETWORK_SERVERS", dict_size(servers));
    reply("CSMSG_NETWORK_USERS", dict_size(clients));
    reply("CSMSG_NETWORK_OPERS", curr_opers.used);
    reply("CSMSG_NETWORK_CHANNELS", registered_channels);
    reply("CSMSG_NETWORK_BANS", banCount);
    reply("CSMSG_NETWORK_CHANUSERS", userCount);
    reply("CSMSG_SERVICES_UPTIME", intervalString(interval, time(NULL) - boot_time, user->handle_info));
    reply("CSMSG_BURST_LENGTH", intervalString(interval, burst_length, user->handle_info));
    return 1;
}

static void
send_staff_list(struct userNode *to, struct userList *list, int skip_flags)
{
    struct helpfile_table table;
    unsigned int nn;
    struct userNode *user;
    char *nick;

    table.length = 0;
    table.width = 1;
    table.flags = TABLE_REPEAT_ROWS | TABLE_NO_FREE | TABLE_NO_HEADERS;
    table.contents = alloca(list->used*sizeof(*table.contents));
    for(nn=0; nn<list->used; nn++)
    {
        user = list->list[nn];
        if(user->modes & skip_flags)
            continue;
        if(IsBot(user))
            continue;
        table.contents[table.length] = alloca(table.width*sizeof(**table.contents));
        if(IsAway(user))
        {
            nick = alloca(strlen(user->nick)+3);
            sprintf(nick, "(%s)", user->nick);
        }
        else
            nick = user->nick;
        table.contents[table.length][0] = nick;
        table.length++;
    }
    table_send(chanserv, to->nick, 0, NULL, table);
}

static CHANSERV_FUNC(cmd_ircops)
{
    reply("CSMSG_STAFF_OPERS");
    send_staff_list(user, &curr_opers, FLAGS_SERVICE);
    return 1;
}

static CHANSERV_FUNC(cmd_helpers)
{
    reply("CSMSG_STAFF_HELPERS");
    send_staff_list(user, &curr_helpers, FLAGS_OPER);
    return 1;
}

static CHANSERV_FUNC(cmd_staff)
{
    reply("CSMSG_NETWORK_STAFF");
    cmd_ircops(CSFUNC_ARGS);
    cmd_helpers(CSFUNC_ARGS);
    return 1;
}

static CHANSERV_FUNC(cmd_peek)
{
    struct modeNode *mn;
    char modes[MODELEN];
    unsigned int n;
    struct helpfile_table table;
    int opcount = 0, voicecount = 0, srvcount = 0;

    irc_make_chanmode(channel, modes);

    reply("CSMSG_PEEK_INFO", channel->name);
    reply("CSMSG_PEEK_TOPIC", channel->topic);
    reply("CSMSG_PEEK_MODES", modes);

    table.length = 0;
    table.width = 1;
    table.flags = TABLE_REPEAT_ROWS | TABLE_NO_FREE | TABLE_NO_HEADERS;
    table.contents = alloca(channel->members.used*sizeof(*table.contents));
    for(n = 0; n < channel->members.used; n++)
    {
        mn = channel->members.list[n];
        if(IsLocal(mn->user))
            srvcount++;
        else if(mn->modes & MODE_CHANOP)
            opcount++;
        else if(mn->modes & MODE_VOICE)
            voicecount++;

        if(!(mn->modes & MODE_CHANOP) || IsLocal(mn->user))
            continue;
        table.contents[table.length] = alloca(sizeof(**table.contents));
        table.contents[table.length][0] = mn->user->nick;
        table.length++;
    }

    reply("CSMSG_PEEK_USERS", channel->members.used, opcount, voicecount,
          (channel->members.used - opcount - voicecount - srvcount));

    if(table.length)
    {
        reply("CSMSG_PEEK_OPS");
        table_send(chanserv, user->nick, 0, NULL, table);
    }
    else
        reply("CSMSG_PEEK_NO_OPS");
    return 1;
}

static MODCMD_FUNC(cmd_wipeinfo)
{
    struct handle_info *victim;
    struct userData *ud, *actor, *real_actor;
    unsigned int override = 0;

    REQUIRE_PARAMS(2);
    actor = GetChannelUser(channel->channel_info, user->handle_info);
    real_actor = GetChannelAccess(channel->channel_info, user->handle_info);
    if(!(victim = modcmd_get_handle_info(user, argv[1])))
        return 0;
    if(!(ud = GetTrueChannelAccess(channel->channel_info, victim)))
    {
        reply("CSMSG_NO_CHAN_USER", argv[1], channel->name);
        return 0;
    }
    if((ud->access >= actor->access) && (ud != actor))
    {
        reply("MSG_USER_OUTRANKED", victim->handle);
        return 0;
    }
    if((ud != real_actor) && (!real_actor || (ud->access >= real_actor->access)))
        override = CMD_LOG_OVERRIDE;
    if(ud->info)
        free(ud->info);
    ud->info = NULL;
    reply("CSMSG_WIPED_INFO_LINE", argv[1], channel->name);
    return 1 | override;
}

static CHANSERV_FUNC(cmd_resync)
{
    struct mod_chanmode *changes;
    struct chanData *cData = channel->channel_info;
    unsigned int ii, used;

    changes = mod_chanmode_alloc(channel->members.used * 2);
    for(ii = used = 0; ii < channel->members.used; ++ii)
    {
        struct modeNode *mn = channel->members.list[ii];
        struct userData *uData;

        if(IsService(mn->user))
            continue;

        uData = GetChannelAccess(cData, mn->user->handle_info);
        if(!cData->lvlOpts[lvlGiveOps]
           || (uData && uData->access >= cData->lvlOpts[lvlGiveOps]))
        {
            if(!(mn->modes & MODE_CHANOP))
            {
                changes->args[used].mode = MODE_CHANOP;
                changes->args[used++].u.member = mn;
            }
        }
        else if(!cData->lvlOpts[lvlGiveVoice]
                || (uData && uData->access >= cData->lvlOpts[lvlGiveVoice]))
        {
            if(mn->modes & MODE_CHANOP)
            {
                changes->args[used].mode = MODE_REMOVE | (mn->modes & ~MODE_VOICE);
                changes->args[used++].u.member = mn;
            }
            if(!(mn->modes & MODE_VOICE))
            {
                changes->args[used].mode = MODE_VOICE;
                changes->args[used++].u.member = mn;
            }
        }
        else
        {
            if(mn->modes)
            {
                changes->args[used].mode = MODE_REMOVE | mn->modes;
                changes->args[used++].u.member = mn;
            }
        }
    }
    changes->argc = used;
    modcmd_chanmode_announce(changes);
    mod_chanmode_free(changes);
    reply("CSMSG_RESYNCED_USERS", channel->name);
    return 1;
}

static CHANSERV_FUNC(cmd_seen)
{
    struct userData *uData;
    struct handle_info *handle;
    char seen[INTERVALLEN];

    REQUIRE_PARAMS(2);

    if(!irccasecmp(argv[1], chanserv->nick))
    {
        reply("CSMSG_IS_CHANSERV");
        return 1;
    }

    if(!(handle = get_handle_info(argv[1])))
    {
        reply("MSG_HANDLE_UNKNOWN", argv[1]);
        return 0;
    }

    if(!(uData = GetTrueChannelAccess(channel->channel_info, handle)))
    {
        reply("CSMSG_NO_CHAN_USER", handle->handle, channel->name);
        return 0;
    }

    if(uData->present)
        reply("CSMSG_USER_PRESENT", handle->handle);
    else if(uData->seen)
        reply("CSMSG_USER_SEEN", handle->handle, channel->name, intervalString(seen, now - uData->seen, user->handle_info));
    else
        reply("CSMSG_NEVER_SEEN", handle->handle, channel->name);

    if(!uData->present && HANDLE_FLAGGED(handle, FROZEN))
        reply("CSMSG_USER_VACATION", handle->handle);

    return 1;
}

static MODCMD_FUNC(cmd_names)
{
    struct userNode *targ;
    struct userData *targData;
    unsigned int ii, pos;
    char buf[400];

    for(ii=pos=0; ii<channel->members.used; ++ii)
    {
        targ = channel->members.list[ii]->user;
        targData = GetTrueChannelAccess(channel->channel_info, targ->handle_info);
        if(!targData)
            continue;
        if(pos + strlen(targ->nick) + strlen(targ->handle_info->handle) + 8 > sizeof(buf))
        {
            buf[pos] = 0;
            reply("CSMSG_CHANNEL_NAMES", channel->name, buf);
            pos = 0;
        }
        buf[pos++] = ' ';
        if(IsUserSuspended(targData))
            buf[pos++] = 's';
        pos += sprintf(buf+pos, "%d:%s(%s)", targData->access, targ->nick, targ->handle_info->handle);
    }
    buf[pos] = 0;
    reply("CSMSG_CHANNEL_NAMES", channel->name, buf);
    reply("CSMSG_END_NAMES", channel->name);
    return 1;
}

static int
note_type_visible_to_user(struct chanData *channel, struct note_type *ntype, struct userNode *user)
{
    switch(ntype->visible_type)
    {
    case NOTE_VIS_ALL: return 1;
    case NOTE_VIS_CHANNEL_USERS: return !channel || !user || (user->handle_info && GetChannelUser(channel, user->handle_info));
    case NOTE_VIS_PRIVILEGED: default: return user && (IsOper(user) || IsSupportHelper(user) || IsNetworkHelper(user));
    }
}

static int
note_type_settable_by_user(struct chanNode *channel, struct note_type *ntype, struct userNode *user)
{
    struct userData *uData;

    switch(ntype->set_access_type)
    {
    case NOTE_SET_CHANNEL_ACCESS:
        if(!user->handle_info)
            return 0;
        if(!(uData = GetChannelUser(channel->channel_info, user->handle_info)))
            return 0;
        return uData->access >= ntype->set_access.min_ulevel;
    case NOTE_SET_CHANNEL_SETTER:
        return check_user_level(channel, user, lvlSetters, 1, 0);
    case NOTE_SET_PRIVILEGED: default:
        return IsHelping(user) && (user->handle_info->opserv_level >= ntype->set_access.min_opserv);
    }
}

static CHANSERV_FUNC(cmd_note)
{
    struct chanData *cData;
    struct note *note;
    struct note_type *ntype;

    cData = channel->channel_info;
    if(!cData)
    {
        reply("CSMSG_NOT_REGISTERED", channel->name);
        return 0;
    }

    /* If no arguments, show all visible notes for the channel. */
    if(argc < 2)
    {
        dict_iterator_t it;
        unsigned int count;

        for(count=0, it=dict_first(cData->notes); it; it=iter_next(it))
        {
            note = iter_data(it);
            if(!note_type_visible_to_user(cData, note->type, user))
                continue;
            if(!count++)
                reply("CSMSG_NOTELIST_HEADER", channel->name);
            reply("CSMSG_NOTE_FORMAT", iter_key(it), note->setter, note->note);
        }
        if(count)
            reply("CSMSG_NOTELIST_END", channel->name);
        else
            reply("CSMSG_NOTELIST_EMPTY", channel->name);
    }
    /* If one argument, show the named note. */
    else if(argc == 2)
    {
        if((note = dict_find(cData->notes, argv[1], NULL))
           && note_type_visible_to_user(cData, note->type, user))
        {
            reply("CSMSG_NOTE_FORMAT", note->type->name, note->setter, note->note);
        }
        else if((ntype = dict_find(note_types, argv[1], NULL))
                && note_type_visible_to_user(NULL, ntype, user))
        {
            reply("CSMSG_NO_SUCH_NOTE", channel->name, ntype->name);
            return 0;
        }
        else
        {
            reply("CSMSG_BAD_NOTE_TYPE", argv[1]);
            return 0;
        }
    }
    /* Assume they're trying to set a note. */
    else
    {
        char *note_text;
        ntype = dict_find(note_types, argv[1], NULL);
        if(!ntype)
        {
            reply("CSMSG_BAD_NOTE_TYPE", argv[1]);
            return 0;
        }
        else if(note_type_settable_by_user(channel, ntype, user))
        {
            note_text = unsplit_string(argv+2, argc-2, NULL);
            if((note = dict_find(cData->notes, argv[1], NULL)))
                reply("CSMSG_REPLACED_NOTE", ntype->name, channel->name, note->setter, note->note);
            chanserv_add_channel_note(cData, ntype, user->handle_info->handle, note_text);
            reply("CSMSG_NOTE_SET", ntype->name, channel->name);

            if(ntype->visible_type == NOTE_VIS_PRIVILEGED)
            {
                /* The note is viewable to staff only, so return 0
                   to keep the invocation from getting logged (or
                   regular users can see it in !events). */
                return 0;
            }
        }
        else
        {
            reply("CSMSG_NO_ACCESS");
            return 0;
        }
    }
    return 1;
}

static CHANSERV_FUNC(cmd_delnote)
{
    struct note *note;

    REQUIRE_PARAMS(2);
    if(!(note = dict_find(channel->channel_info->notes, argv[1], NULL))
       || !note_type_settable_by_user(channel, note->type, user))
    {
        reply("CSMSG_NO_SUCH_NOTE", channel->name, argv[1]);
        return 0;
    }
    dict_remove(channel->channel_info->notes, note->type->name);
    reply("CSMSG_NOTE_REMOVED", argv[1], channel->name);
    return 1;
}

static CHANSERV_FUNC(cmd_events)
{
    struct logSearch discrim;
    struct logReport report;
    unsigned int matches, limit;

    limit = (argc > 1) ? atoi(argv[1]) : 10;
    if(limit < 1 || limit > 200)
        limit = 10;

    memset(&discrim, 0, sizeof(discrim));
    discrim.masks.bot = chanserv;
    discrim.masks.channel_name = channel->name;
    if(argc > 2)
        discrim.masks.command = argv[2];
    discrim.limit = limit;
    discrim.max_time = INT_MAX;
    discrim.severities = 1 << LOG_COMMAND;
    report.reporter = chanserv;
    report.user = user;
    reply("CSMSG_EVENT_SEARCH_RESULTS");
    matches = log_entry_search(&discrim, log_report_entry, &report);
    if(matches)
        reply("MSG_MATCH_COUNT", matches);
    else
        reply("MSG_NO_MATCHES");
    return 1;
}

static CHANSERV_FUNC(cmd_say)
{
    char *msg;
    if(channel)
    {
        REQUIRE_PARAMS(2);
        msg = unsplit_string(argv + 1, argc - 1, NULL);
        send_channel_message(channel, cmd->parent->bot, "%s", msg);
    }
    else if(*argv[1] == '*' && argv[1][1] != '\0')
    {
        struct handle_info *hi;
        struct userNode *authed;

        REQUIRE_PARAMS(3);
        msg = unsplit_string(argv + 2, argc - 2, NULL);

        if (!(hi = get_handle_info(argv[1] + 1)))
        {
            reply("MSG_HANDLE_UNKNOWN", argv[1] + 1);
            return 0;
        }

        for (authed = hi->users; authed; authed = authed->next_authed)
            send_target_message(5, authed->nick, cmd->parent->bot, "%s", msg);
    }
    else if(GetUserH(argv[1]))
    {
        REQUIRE_PARAMS(3);
        msg = unsplit_string(argv + 2, argc - 2, NULL);
        send_target_message(5, argv[1], cmd->parent->bot, "%s", msg);
    }
    else
    {
        reply("MSG_NOT_TARGET_NAME");
        return 0;
    }
    return 1;
}

static CHANSERV_FUNC(cmd_emote)
{
    char *msg;
    assert(argc >= 2);
    if(channel)
    {
        /* CTCP is so annoying. */
        msg = unsplit_string(argv + 1, argc - 1, NULL);
        send_channel_message(channel, cmd->parent->bot, "\001ACTION %s\001", msg);
    }
    else if(*argv[1] == '*' && argv[1][1] != '\0')
    {
        struct handle_info *hi;
        struct userNode *authed;

        REQUIRE_PARAMS(3);
        msg = unsplit_string(argv + 2, argc - 2, NULL);

        if (!(hi = get_handle_info(argv[1] + 1)))
        {
            reply("MSG_HANDLE_UNKNOWN", argv[1] + 1);
            return 0;
        }

        for (authed = hi->users; authed; authed = authed->next_authed)
            send_target_message(5, authed->nick, cmd->parent->bot, "\001ACTION %s\001", msg);
    }
    else if(GetUserH(argv[1]))
    {
        msg = unsplit_string(argv + 2, argc - 2, NULL);
        send_target_message(5, argv[1], cmd->parent->bot, "\001ACTION %s\001", msg);
    }
    else
    {
        reply("MSG_NOT_TARGET_NAME");
        return 0;
    }
    return 1;
}

struct channelList *
chanserv_support_channels(void)
{
    return &chanserv_conf.support_channels;
}

static CHANSERV_FUNC(cmd_expire)
{
    int channel_count = registered_channels;
    expire_channels(chanserv);
    reply("CSMSG_CHANNELS_EXPIRED", channel_count - registered_channels);
    return 1;
}

static void
chanserv_expire_suspension(void *data)
{
    struct suspended *suspended = data;
    struct chanNode *channel;
    unsigned int ii;

    /* Update the channel registration data structure. */
    if(!suspended->expires || (now < suspended->expires))
        suspended->revoked = now;
    channel = suspended->cData->channel;
    suspended->cData->channel = channel;
    suspended->cData->flags &= ~CHANNEL_SUSPENDED;

    /* If appropriate, re-join ChanServ to the channel. */
    if(!IsOffChannel(suspended->cData))
    {
        struct mod_chanmode change;
        mod_chanmode_init(&change);
        change.argc = 1;
        change.args[0].mode = MODE_CHANOP;
        change.args[0].u.member = AddChannelUser(chanserv, channel);
        mod_chanmode_announce(chanserv, channel, &change);
    }

    /* Mark everyone currently in the channel as present. */
    for(ii = 0; ii < channel->members.used; ++ii)
    {
        struct userData *uData = GetChannelAccess(suspended->cData, channel->members.list[ii]->user->handle_info);
        if(uData)
        {
            uData->present = 1;
            uData->seen = now;
        }
    }
}

static CHANSERV_FUNC(cmd_csuspend)
{
    struct suspended *suspended;
    char reason[MAXLEN];
    unsigned long expiry, duration;
    struct userData *uData;

    REQUIRE_PARAMS(3);

    if(IsProtected(channel->channel_info))
    {
        reply("CSMSG_SUSPEND_NODELETE", channel->name);
        return 0;
    }

    if(argv[1][0] == '!')
        argv[1]++;
    else if(IsSuspended(channel->channel_info))
    {
        reply("CSMSG_ALREADY_SUSPENDED", channel->name);
        show_suspension_info(cmd, user, channel->channel_info->suspended);
        return 0;
    }

    if(!strcmp(argv[1], "0"))
        expiry = 0;
    else if((duration = ParseInterval(argv[1])))
        expiry = now + duration;
    else
    {
        reply("MSG_INVALID_DURATION", argv[1]);
        return 0;
    }

    unsplit_string(argv + 2, argc - 2, reason);

    suspended = calloc(1, sizeof(*suspended));
    suspended->revoked = 0;
    suspended->issued = now;
    suspended->suspender = strdup(user->handle_info->handle);
    suspended->expires = expiry;
    suspended->reason = strdup(reason);
    suspended->cData = channel->channel_info;
    suspended->previous = suspended->cData->suspended;
    suspended->cData->suspended = suspended;

    if(suspended->expires)
        timeq_add(suspended->expires, chanserv_expire_suspension, suspended);

    if(IsSuspended(channel->channel_info))
    {
        suspended->previous->revoked = now;
        if(suspended->previous->expires)
            timeq_del(suspended->previous->expires, chanserv_expire_suspension, suspended->previous, 0);
        sprintf(reason, "%s suspension modified by %s.", channel->name, suspended->suspender);
        global_message(MESSAGE_RECIPIENT_OPERS | MESSAGE_RECIPIENT_HELPERS, reason);
    }
    else
    {
        /* Mark all users in channel as absent. */
        for(uData = channel->channel_info->users; uData; uData = uData->next)
        {
            if(uData->present)
            {
                uData->seen = now;
                uData->present = 0;
            }
        }

        /* Mark the channel as suspended, then part. */
        channel->channel_info->flags |= CHANNEL_SUSPENDED;
        DelChannelUser(chanserv, channel, suspended->reason, 0);
        reply("CSMSG_SUSPENDED", channel->name);
        sprintf(reason, "%s suspended by %s.", channel->name, suspended->suspender);
        global_message(MESSAGE_RECIPIENT_OPERS | MESSAGE_RECIPIENT_HELPERS, reason);
    }
    return 1;
}

static CHANSERV_FUNC(cmd_cunsuspend)
{
    struct suspended *suspended;
    char message[MAXLEN];

    if(!IsSuspended(channel->channel_info))
    {
        reply("CSMSG_NOT_SUSPENDED", channel->name);
        return 0;
    }

    suspended = channel->channel_info->suspended;

    /* Expire the suspension and join ChanServ to the channel. */
    timeq_del(suspended->expires, chanserv_expire_suspension, suspended, 0);
    chanserv_expire_suspension(suspended);
    reply("CSMSG_UNSUSPENDED", channel->name);
    sprintf(message, "%s unsuspended by %s.", channel->name, user->handle_info->handle);
    global_message(MESSAGE_RECIPIENT_OPERS|MESSAGE_RECIPIENT_HELPERS, message);
    return 1;
}

typedef struct chanservSearch
{
    char *name;
    char *registrar;

    unsigned long unvisited;
    unsigned long registered;

    unsigned long flags;
    unsigned int limit;
} *search_t;

typedef void (*channel_search_func)(struct chanData *channel, void *data);

static search_t
chanserv_search_create(struct userNode *user, unsigned int argc, char *argv[])
{
    search_t search;
    unsigned int i;

    search = malloc(sizeof(struct chanservSearch));
    memset(search, 0, sizeof(*search));
    search->limit = 25;

    for(i = 0; i < argc; i++)
    {
        /* Assume all criteria require arguments. */
        if(i == (argc - 1))
        {
            send_message(user, chanserv, "MSG_MISSING_PARAMS", argv[i]);
            goto fail;
        }

        if(!irccasecmp(argv[i], "name"))
            search->name = argv[++i];
        else if(!irccasecmp(argv[i], "registrar"))
            search->registrar = argv[++i];
        else if(!irccasecmp(argv[i], "unvisited"))
            search->unvisited = ParseInterval(argv[++i]);
        else if(!irccasecmp(argv[i], "registered"))
            search->registered = ParseInterval(argv[++i]);
        else if(!irccasecmp(argv[i], "flags"))
        {
            i++;
            if(!irccasecmp(argv[i], "nodelete"))
                search->flags |= CHANNEL_NODELETE;
            else if(!irccasecmp(argv[i], "suspended"))
                search->flags |= CHANNEL_SUSPENDED;
            else if(!irccasecmp(argv[i], "unreviewed"))
                search->flags |= CHANNEL_UNREVIEWED;
            else
            {
                send_message(user, chanserv, "CSMSG_INVALID_CFLAG", argv[i]);
                goto fail;
            }
        }
        else if(!irccasecmp(argv[i], "limit"))
            search->limit = strtoul(argv[++i], NULL, 10);
        else
        {
            send_message(user, chanserv, "MSG_INVALID_CRITERIA", argv[i]);
            goto fail;
        }
    }

    if(search->name && !strcmp(search->name, "*"))
        search->name = 0;
    if(search->registrar && !strcmp(search->registrar, "*"))
        search->registrar = 0;

    return search;
  fail:
    free(search);
    return NULL;
}

static int
chanserv_channel_match(struct chanData *channel, search_t search)
{
    const char *name = channel->channel->name;
    if((search->name && !match_ircglob(name, search->name)) ||
       (search->registrar && !channel->registrar) ||
       (search->registrar && !match_ircglob(channel->registrar, search->registrar)) ||
       (search->unvisited && (now - channel->visited) < search->unvisited) ||
       (search->registered && (now - channel->registered) > search->registered) ||
       (search->flags && ((search->flags & channel->flags) != search->flags)))
        return 0;

    return 1;
}

static unsigned int
chanserv_channel_search(search_t search, channel_search_func smf, void *data)
{
    struct chanData *channel;
    unsigned int matches = 0;

    for(channel = channelList; channel && matches < search->limit; channel = channel->next)
    {
        if(!chanserv_channel_match(channel, search))
            continue;
        matches++;
        smf(channel, data);
    }

    return matches;
}

static void
search_count(UNUSED_ARG(struct chanData *channel), UNUSED_ARG(void *data))
{
}

static void
search_print(struct chanData *channel, void *data)
{
    send_message_type(4, data, chanserv, "%s", channel->channel->name);
}

static CHANSERV_FUNC(cmd_search)
{
    search_t search;
    unsigned int matches;
    channel_search_func action;

    REQUIRE_PARAMS(3);

    if(!irccasecmp(argv[1], "count"))
        action = search_count;
    else if(!irccasecmp(argv[1], "print"))
        action = search_print;
    else
    {
        reply("CSMSG_ACTION_INVALID", argv[1]);
        return 0;
    }

    search = chanserv_search_create(user, argc - 2, argv + 2);
    if(!search)
        return 0;

    if(action == search_count)
        search->limit = INT_MAX;

    if(action == search_print)
        reply("CSMSG_CHANNEL_SEARCH_RESULTS");

    matches = chanserv_channel_search(search, action, user);

    if(matches)
        reply("MSG_MATCH_COUNT", matches);
    else
        reply("MSG_NO_MATCHES");

    free(search);
    return 1;
}

static CHANSERV_FUNC(cmd_unvisited)
{
    struct chanData *cData;
    unsigned long interval = chanserv_conf.channel_expire_delay;
    char buffer[INTERVALLEN];
    unsigned int limit = 25, matches = 0;

    if(argc > 1)
    {
        interval = ParseInterval(argv[1]);
        if(argc > 2)
            limit = atoi(argv[2]);
    }

    intervalString(buffer, interval, user->handle_info);
    reply("CSMSG_UNVISITED_HEADER", limit, buffer);

    for(cData = channelList; cData && matches < limit; cData = cData->next)
    {
        if((now - cData->visited) < interval)
            continue;

        intervalString(buffer, now - cData->visited, user->handle_info);
        reply("CSMSG_UNVISITED_DATA", cData->channel->name, buffer);
        matches++;
    }

    return 1;
}

static MODCMD_FUNC(chan_opt_defaulttopic)
{
    if(argc > 1)
    {
        char *topic;

        if(!check_user_level(channel, user, lvlEnfTopic, 1, 0))
        {
            reply("CSMSG_TOPIC_LOCKED", channel->name);
            return 0;
        }

        topic = unsplit_string(argv+1, argc-1, NULL);

        free(channel->channel_info->topic);
        if(topic[0] == '*' && topic[1] == 0)
        {
            topic = channel->channel_info->topic = NULL;
        }
        else
        {
            topic = channel->channel_info->topic = strdup(topic);
            if(channel->channel_info->topic_mask
               && !match_ircglob(channel->channel_info->topic, channel->channel_info->topic_mask))
                reply("CSMSG_TOPIC_MISMATCH", channel->name);
        }
        SetChannelTopic(channel, chanserv, topic ? topic : "", 1);
    }

    if(channel->channel_info->topic)
        reply("CSMSG_SET_DEFAULT_TOPIC", channel->channel_info->topic);
    else
        reply("CSMSG_SET_DEFAULT_TOPIC", user_find_message(user, "MSG_NONE"));
    return 1;
}

static MODCMD_FUNC(chan_opt_topicmask)
{
    if(argc > 1)
    {
        struct chanData *cData = channel->channel_info;
        char *mask;

        if(!check_user_level(channel, user, lvlEnfTopic, 1, 0))
        {
            reply("CSMSG_TOPIC_LOCKED", channel->name);
            return 0;
        }

        mask = unsplit_string(argv+1, argc-1, NULL);

        if(cData->topic_mask)
            free(cData->topic_mask);
        if(mask[0] == '*' && mask[1] == 0)
        {
            cData->topic_mask = 0;
        }
        else
        {
            cData->topic_mask = strdup(mask);
            if(!cData->topic)
                reply("CSMSG_MASK_BUT_NO_TOPIC", channel->name);
            else if(!match_ircglob(cData->topic, cData->topic_mask))
                reply("CSMSG_TOPIC_MISMATCH", channel->name);
        }
    }

    if(channel->channel_info->topic_mask)
        reply("CSMSG_SET_TOPICMASK", channel->channel_info->topic_mask);
    else
        reply("CSMSG_SET_TOPICMASK", user_find_message(user, "MSG_NONE"));
    return 1;
}

int opt_greeting_common(struct userNode *user, struct svccmd *cmd, int argc, char *argv[], char *name, char **data)
{
    if(argc > 1)
    {
        char *greeting = unsplit_string(argv+1, argc-1, NULL);
        char *previous;

        previous = *data;
        if(greeting[0] == '*' && greeting[1] == 0)
            *data = NULL;
        else
        {
            unsigned int length = strlen(greeting);
            if(length > chanserv_conf.greeting_length)
            {
                reply("CSMSG_GREETING_TOO_LONG", length, chanserv_conf.greeting_length);
                return 0;
            }
            *data = strdup(greeting);
        }
        if(previous)
            free(previous);
    }

    if(*data)
        reply(name, *data);
    else
        reply(name, user_find_message(user, "MSG_NONE"));
    return 1;
}

static MODCMD_FUNC(chan_opt_greeting)
{
    return opt_greeting_common(user, cmd, argc, argv, "CSMSG_SET_GREETING", &channel->channel_info->greeting);
}

static MODCMD_FUNC(chan_opt_usergreeting)
{
    return opt_greeting_common(user, cmd, argc, argv, "CSMSG_SET_USERGREETING", &channel->channel_info->user_greeting);
}

static MODCMD_FUNC(chan_opt_modes)
{
    struct mod_chanmode *new_modes;
    char modes[MAXLEN];

    if(argc > 1)
    {
        if(!check_user_level(channel, user, lvlEnfModes, 1, 0))
        {
            reply("CSMSG_NO_ACCESS");
            return 0;
        }
        if(argv[1][0] == '*' && argv[1][1] == 0)
        {
            memset(&channel->channel_info->modes, 0, sizeof(channel->channel_info->modes));
        }
        else if(!(new_modes = mod_chanmode_parse(channel, argv+1, argc-1, MCP_KEY_FREE|MCP_REGISTERED|MCP_NO_APASS, 0)))
        {
            reply("CSMSG_INVALID_MODE_LOCK", unsplit_string(argv+1, argc-1, NULL));
            return 0;
        }
        else if(new_modes->argc > 1)
        {
            reply("CSMSG_INVALID_MODE_LOCK", unsplit_string(argv+1, argc-1, NULL));
            mod_chanmode_free(new_modes);
            return 0;
        }
        else
        {
            channel->channel_info->modes = *new_modes;
            modcmd_chanmode_announce(new_modes);
            mod_chanmode_free(new_modes);
        }
    }

    mod_chanmode_format(&channel->channel_info->modes, modes);
    if(modes[0])
        reply("CSMSG_SET_MODES", modes);
    else
        reply("CSMSG_SET_MODES", user_find_message(user, "MSG_NONE"));
    return 1;
}

#define CHANNEL_BINARY_OPTION(MSG, FLAG) return channel_binary_option(MSG, FLAG, CSFUNC_ARGS);
static int
channel_binary_option(char *name, unsigned long mask, struct userNode *user, struct chanNode *channel, int argc, char *argv[], struct svccmd *cmd)
{
    struct chanData *cData = channel->channel_info;
    int value;

    if(argc > 1)
    {
        /* Set flag according to value. */
        if(enabled_string(argv[1]))
        {
            cData->flags |= mask;
            value = 1;
        }
        else if(disabled_string(argv[1]))
        {
            cData->flags &= ~mask;
            value = 0;
        }
        else
        {
            reply("MSG_INVALID_BINARY", argv[1]);
            return 0;
        }
    }
    else
    {
        /* Find current option value. */
        value = (cData->flags & mask) ? 1 : 0;
    }

    if(value)
        reply(name, user_find_message(user, "MSG_ON"));
    else
        reply(name, user_find_message(user, "MSG_OFF"));
    return 1;
}

static MODCMD_FUNC(chan_opt_nodelete)
{
    if((argc > 1) && (!IsOper(user) || !user->handle_info || (user->handle_info->opserv_level < chanserv_conf.nodelete_level)))
    {
        reply("MSG_SETTING_PRIVILEGED", argv[0]);
        return 0;
    }

    CHANNEL_BINARY_OPTION("CSMSG_SET_NODELETE", CHANNEL_NODELETE);
}

static MODCMD_FUNC(chan_opt_dynlimit)
{
    CHANNEL_BINARY_OPTION("CSMSG_SET_DYNLIMIT", CHANNEL_DYNAMIC_LIMIT);
}

static MODCMD_FUNC(chan_opt_offchannel)
{
    struct chanData *cData = channel->channel_info;
    int value;

    if(argc > 1)
    {
        /* Set flag according to value. */
        if(enabled_string(argv[1]))
        {
            if(!IsOffChannel(cData))
                DelChannelUser(chanserv, channel, "Going off-channel.", 0);
            cData->flags |= CHANNEL_OFFCHANNEL;
            value = 1;
        }
        else if(disabled_string(argv[1]))
        {
            if(IsOffChannel(cData))
            {
                struct mod_chanmode change;
                mod_chanmode_init(&change);
                change.argc = 1;
                change.args[0].mode = MODE_CHANOP;
                change.args[0].u.member = AddChannelUser(chanserv, channel);
                mod_chanmode_announce(chanserv, channel, &change);
            }
            cData->flags &= ~CHANNEL_OFFCHANNEL;
            value = 0;
        }
        else
        {
            reply("MSG_INVALID_BINARY", argv[1]);
            return 0;
        }
    }
    else
    {
        /* Find current option value. */
        value = (cData->flags & CHANNEL_OFFCHANNEL) ? 1 : 0;
    }

    if(value)
        reply("CSMSG_SET_OFFCHANNEL", user_find_message(user, "MSG_ON"));
    else
        reply("CSMSG_SET_OFFCHANNEL", user_find_message(user, "MSG_OFF"));
    return 1;
}

static MODCMD_FUNC(chan_opt_unreviewed)
{
    struct chanData *cData = channel->channel_info;
    int value = (cData->flags & CHANNEL_UNREVIEWED) ? 1 : 0;

    if(argc > 1)
    {
        int new_value;

        /* The two directions can have different ACLs. */
        if(enabled_string(argv[1]))
            new_value = 1;
        else if(disabled_string(argv[1]))
            new_value = 0;
        else
        {
            reply("MSG_INVALID_BINARY", argv[1]);
            return 0;
        }

        if (new_value != value)
        {
            struct svccmd *subcmd;
            char subcmd_name[32];

            snprintf(subcmd_name, sizeof(subcmd_name), "%s %s", argv[0], (new_value ? "on" : "off"));
            subcmd = dict_find(cmd->parent->commands, subcmd_name, NULL);
            if(!subcmd)
            {
                reply("MSG_COMMAND_DISABLED", subcmd_name);
                return 0;
            }
            else if(!svccmd_can_invoke(user, cmd->parent->bot, subcmd, channel, SVCCMD_NOISY))
                return 0;

            if (new_value)
                cData->flags |= CHANNEL_UNREVIEWED;
            else
            {
                free(cData->registrar);
                cData->registrar = strdup(user->handle_info->handle);
                cData->flags &= ~CHANNEL_UNREVIEWED;
            }
            value = new_value;
        }
    }

    if(value)
        reply("CSMSG_SET_UNREVIEWED", user_find_message(user, "MSG_ON"));
    else
        reply("CSMSG_SET_UNREVIEWED", user_find_message(user, "MSG_OFF"));
    return 1;
}

static MODCMD_FUNC(chan_opt_defaults)
{
    struct userData *uData;
    struct chanData *cData;
    const char *confirm;
    enum levelOption lvlOpt;
    enum charOption chOpt;

    cData = channel->channel_info;
    uData = GetChannelUser(cData, user->handle_info);
    if(!uData || (uData->access < UL_OWNER))
    {
        reply("CSMSG_OWNER_DEFAULTS", channel->name);
        return 0;
    }
    confirm = make_confirmation_string(uData);
    if((argc < 2) || strcmp(argv[1], confirm))
    {
        reply("CSMSG_CONFIRM_DEFAULTS", channel->name, confirm);
        return 0;
    }
    cData->flags = (CHANNEL_DEFAULT_FLAGS & ~CHANNEL_PRESERVED_FLAGS)
        | (cData->flags & CHANNEL_PRESERVED_FLAGS);
    cData->modes = chanserv_conf.default_modes;
    for(lvlOpt = 0; lvlOpt < NUM_LEVEL_OPTIONS; ++lvlOpt)
        cData->lvlOpts[lvlOpt] = levelOptions[lvlOpt].default_value;
    for(chOpt = 0; chOpt < NUM_CHAR_OPTIONS; ++chOpt)
        cData->chOpts[chOpt] = charOptions[chOpt].default_value;
    reply("CSMSG_SETTINGS_DEFAULTED", channel->name);
    return 1;
}

static int
channel_level_option(enum levelOption option, struct userNode *user, struct chanNode *channel, int argc, char *argv[], struct svccmd *cmd)
{
    struct chanData *cData = channel->channel_info;
    struct userData *uData;
    unsigned short value;

    if(argc > 1)
    {
        if(!check_user_level(channel, user, option, 1, 1))
        {
            reply("CSMSG_CANNOT_SET");
            return 0;
        }
        value = user_level_from_name(argv[1], UL_OWNER+1);
        if(!value && strcmp(argv[1], "0"))
        {
            reply("CSMSG_INVALID_ACCESS", argv[1]);
            return 0;
        }
        uData = GetChannelUser(cData, user->handle_info);
        if(!uData || ((uData->access < UL_OWNER) && (value > uData->access)))
        {
            reply("CSMSG_BAD_SETLEVEL");
            return 0;
        }
        switch(option)
        {
        case lvlGiveVoice:
            if(value > cData->lvlOpts[lvlGiveOps])
            {
                reply("CSMSG_BAD_GIVEVOICE", cData->lvlOpts[lvlGiveOps]);
                return 0;
            }
            break;
        case lvlGiveOps:
            if(value < cData->lvlOpts[lvlGiveVoice])
            {
                reply("CSMSG_BAD_GIVEOPS", cData->lvlOpts[lvlGiveVoice]);
                return 0;
            }
            break;
        case lvlSetters:
            /* This test only applies to owners, since non-owners
             * trying to set an option to above their level get caught
             * by the CSMSG_BAD_SETLEVEL test above.
             */
            if(value > uData->access)
            {
                reply("CSMSG_BAD_SETTERS");
                return 0;
            }
            break;
        default:
            break;
        }
        cData->lvlOpts[option] = value;
    }
    reply(levelOptions[option].format_name, cData->lvlOpts[option]);
    return argc > 1;
}

static MODCMD_FUNC(chan_opt_enfops)
{
    return channel_level_option(lvlEnfOps, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_giveops)
{
    return channel_level_option(lvlGiveOps, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_enfmodes)
{
    return channel_level_option(lvlEnfModes, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_enftopic)
{
    return channel_level_option(lvlEnfTopic, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_pubcmd)
{
    return channel_level_option(lvlPubCmd, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_setters)
{
    return channel_level_option(lvlSetters, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_ctcpusers)
{
    return channel_level_option(lvlCTCPUsers, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_userinfo)
{
    return channel_level_option(lvlUserInfo, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_givevoice)
{
    return channel_level_option(lvlGiveVoice, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_topicsnarf)
{
    return channel_level_option(lvlTopicSnarf, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_inviteme)
{
    return channel_level_option(lvlInviteMe, CSFUNC_ARGS);
}

static int
channel_multiple_option(enum charOption option, struct userNode *user, struct chanNode *channel, int argc, char *argv[], struct svccmd *cmd)
{
    struct chanData *cData = channel->channel_info;
    int count = charOptions[option].count, idx;

    if(argc > 1)
    {
        idx = atoi(argv[1]);

        if(!isdigit(argv[1][0]) || (idx < 0) || (idx >= count))
        {
            reply("CSMSG_INVALID_NUMERIC", idx);
            /* Show possible values. */
            for(idx = 0; idx < count; idx++)
                reply(charOptions[option].format_name, idx, user_find_message(user, charOptions[option].values[idx].format_name));
            return 0;
        }

        cData->chOpts[option] = charOptions[option].values[idx].value;
    }
    else
    {
        /* Find current option value. */
      find_value:
        for(idx = 0;
            (idx < count) && (cData->chOpts[option] != charOptions[option].values[idx].value);
            idx++);
        if(idx == count)
        {
            /* Somehow, the option value is corrupt; reset it to the default. */
            cData->chOpts[option] = charOptions[option].default_value;
            goto find_value;
        }
    }

    reply(charOptions[option].format_name, idx, user_find_message(user, charOptions[option].values[idx].format_name));
    return 1;
}

static MODCMD_FUNC(chan_opt_protect)
{
    return channel_multiple_option(chProtect, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_toys)
{
    return channel_multiple_option(chToys, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_ctcpreaction)
{
    return channel_multiple_option(chCTCPReaction, CSFUNC_ARGS);
}

static MODCMD_FUNC(chan_opt_topicrefresh)
{
    return channel_multiple_option(chTopicRefresh, CSFUNC_ARGS);
}

static struct svccmd_list set_shows_list;

static void
handle_svccmd_unbind(struct svccmd *target) {
    unsigned int ii;
    for(ii=0; ii<set_shows_list.used; ++ii)
        if(target == set_shows_list.list[ii])
            set_shows_list.used = 0;
}

static CHANSERV_FUNC(cmd_set)
{
    struct svccmd *subcmd;
    char buf[MAXLEN];
    unsigned int ii;

    /* Check if we need to (re-)initialize set_shows_list. */
    if(!set_shows_list.used)
    {
        if(!set_shows_list.size)
        {
            set_shows_list.size = chanserv_conf.set_shows->used;
            set_shows_list.list = calloc(set_shows_list.size, sizeof(set_shows_list.list[0]));
        }
        for(ii = 0; ii < chanserv_conf.set_shows->used; ii++)
        {
            const char *name = chanserv_conf.set_shows->list[ii];
            sprintf(buf, "%s %s", argv[0], name);
            subcmd = dict_find(cmd->parent->commands, buf, NULL);
            if(!subcmd)
            {
                log_module(CS_LOG, LOG_ERROR, "Unable to find set option \"%s\".", name);
                continue;
            }
            svccmd_list_append(&set_shows_list, subcmd);
        }
    }

    if(argc < 2)
    {
        reply("CSMSG_CHANNEL_OPTIONS");
        for(ii = 0; ii < set_shows_list.used; ii++)
        {
            subcmd = set_shows_list.list[ii];
            subcmd->command->func(user, channel, 1, argv+1, subcmd);
        }
        return 1;
    }

    sprintf(buf, "%s %s", argv[0], argv[1]);
    subcmd = dict_find(cmd->parent->commands, buf, NULL);
    if(!subcmd)
    {
        reply("CSMSG_INVALID_OPTION", argv[1], argv[0]);
        return 0;
    }
    if((argc > 2) && !check_user_level(channel, user, lvlSetters, 1, 0))
    {
        reply("CSMSG_NO_ACCESS");
        return 0;
    }

    argv[0] = "";
    argv[1] = buf;
    return subcmd->command->func(user, channel, argc - 1, argv + 1, subcmd);
}

static int
user_binary_option(char *name, unsigned long mask, struct userNode *user, struct chanNode *channel, int argc, char *argv[], struct svccmd *cmd)
{
    struct userData *uData;

    uData = GetChannelAccess(channel->channel_info, user->handle_info);
    if(!uData)
    {
        reply("CSMSG_NOT_USER", channel->name);
        return 0;
    }

    if(argc < 2)
    {
        /* Just show current option value. */
    }
    else if(enabled_string(argv[1]))
    {
        uData->flags |= mask;
    }
    else if(disabled_string(argv[1]))
    {
        uData->flags &= ~mask;
    }
    else
    {
        reply("MSG_INVALID_BINARY", argv[1]);
        return 0;
    }

    reply(name, user_find_message(user, (uData->flags & mask) ? "MSG_ON" : "MSG_OFF"));
    return 1;
}

static MODCMD_FUNC(user_opt_noautoop)
{
    struct userData *uData;

    uData = GetChannelAccess(channel->channel_info, user->handle_info);
    if(!uData)
    {
        reply("CSMSG_NOT_USER", channel->name);
        return 0;
    }
    if(uData->access < channel->channel_info->lvlOpts[lvlGiveOps])
        return user_binary_option("CSMSG_USET_NOAUTOVOICE", USER_AUTO_OP, CSFUNC_ARGS);
    else
        return user_binary_option("CSMSG_USET_NOAUTOOP", USER_AUTO_OP, CSFUNC_ARGS);
}

static MODCMD_FUNC(user_opt_autoinvite)
{
    if((argc > 1) && !check_user_level(channel, user, lvlInviteMe, 1, 0))
    {
        reply("CSMSG_LOW_CHANNEL_ACCESS", channel->name);
    }
    return user_binary_option("CSMSG_USET_AUTOINVITE", USER_AUTO_INVITE, CSFUNC_ARGS);
}

static MODCMD_FUNC(user_opt_info)
{
    struct userData *uData;
    char *infoline;

    uData = GetChannelAccess(channel->channel_info, user->handle_info);

    if(!uData)
    {
        /* If they got past the command restrictions (which require access)
         * but fail this test, we have some fool with security override on.
         */
        reply("CSMSG_NOT_USER", channel->name);
        return 0;
    }

    if(argc > 1)
    {
        size_t bp;
        infoline = unsplit_string(argv + 1, argc - 1, NULL);
        if(strlen(infoline) > chanserv_conf.max_userinfo_length)
        {
            reply("CSMSG_INFOLINE_TOO_LONG", chanserv_conf.max_userinfo_length);
            return 0;
        }
        bp = strcspn(infoline, "\001");
        if(infoline[bp])
        {
            reply("CSMSG_BAD_INFOLINE", infoline[bp]);
            return 0;
        }
        if(uData->info)
            free(uData->info);
        if(infoline[0] == '*' && infoline[1] == 0)
            uData->info = NULL;
        else
            uData->info = strdup(infoline);
    }
    if(uData->info)
        reply("CSMSG_USET_INFO", uData->info);
    else
        reply("CSMSG_USET_INFO", user_find_message(user, "MSG_NONE"));
    return 1;
}

struct svccmd_list uset_shows_list;

static CHANSERV_FUNC(cmd_uset)
{
    struct svccmd *subcmd;
    char buf[MAXLEN];
    unsigned int ii;

    /* Check if we need to (re-)initialize uset_shows_list. */
    if(!uset_shows_list.used)
    {
        char *options[] =
        {
            "NoAutoOp", "AutoInvite", "Info"
        };

        if(!uset_shows_list.size)
        {
            uset_shows_list.size = ArrayLength(options);
            uset_shows_list.list = calloc(uset_shows_list.size, sizeof(uset_shows_list.list[0]));
        }
        for(ii = 0; ii < ArrayLength(options); ii++)
        {
            const char *name = options[ii];
            sprintf(buf, "%s %s", argv[0], name);
            subcmd = dict_find(cmd->parent->commands, buf, NULL);
            if(!subcmd)
            {
                log_module(CS_LOG, LOG_ERROR, "Unable to find uset option %s.", name);
                continue;
            }
            svccmd_list_append(&uset_shows_list, subcmd);
        }
    }

    if(argc < 2)
    {
        /* Do this so options are presented in a consistent order. */
        reply("CSMSG_USER_OPTIONS");
        for(ii = 0; ii < uset_shows_list.used; ii++)
            uset_shows_list.list[ii]->command->func(user, channel, 1, argv+1, uset_shows_list.list[ii]);
        return 1;
    }

    sprintf(buf, "%s %s", argv[0], argv[1]);
    subcmd = dict_find(cmd->parent->commands, buf, NULL);
    if(!subcmd)
    {
        reply("CSMSG_INVALID_OPTION", argv[1], argv[0]);
        return 0;
    }

    return subcmd->command->func(user, channel, argc - 1, argv + 1, subcmd);
}

static CHANSERV_FUNC(cmd_giveownership)
{
    struct handle_info *new_owner_hi;
    struct userData *new_owner;
    struct userData *curr_user;
    struct userData *invoker;
    struct chanData *cData = channel->channel_info;
    struct do_not_register *dnr;
    const char *confirm;
    unsigned int force;
    unsigned short co_access;
    char reason[MAXLEN];

    REQUIRE_PARAMS(2);
    curr_user = GetChannelAccess(cData, user->handle_info);
    force = IsHelping(user) && (argc > 2) && !irccasecmp(argv[2], "force");
    if(!curr_user || (curr_user->access != UL_OWNER))
    {
        struct userData *owner = NULL;
        for(curr_user = channel->channel_info->users;
            curr_user;
            curr_user = curr_user->next)
        {
            if(curr_user->access != UL_OWNER)
                continue;
            if(owner)
            {
                reply("CSMSG_MULTIPLE_OWNERS", channel->name);
                return 0;
            }
            owner = curr_user;
        }
        curr_user = owner;
    }
    else if(!force && (now < cData->ownerTransfer + chanserv_conf.giveownership_period))
    {
        char delay[INTERVALLEN];
        intervalString(delay, cData->ownerTransfer + chanserv_conf.giveownership_period - now, user->handle_info);
        reply("CSMSG_TRANSFER_WAIT", delay, channel->name);
        return 0;
    }
    if(!(new_owner_hi = modcmd_get_handle_info(user, argv[1])))
        return 0;
    if(new_owner_hi == user->handle_info)
    {
        reply("CSMSG_NO_TRANSFER_SELF");
        return 0;
    }
    new_owner = GetChannelAccess(cData, new_owner_hi);
    if(!new_owner)
    {
        if(force)
        {
            new_owner = add_channel_user(cData, new_owner_hi, UL_OWNER - 1, 0, NULL);
        }
        else
        {
            reply("CSMSG_NO_CHAN_USER", new_owner_hi->handle, channel->name);
            return 0;
        }
    }
    if((chanserv_get_owned_count(new_owner_hi) >= chanserv_conf.max_owned) && !force)
    {
        reply("CSMSG_OWN_TOO_MANY", new_owner_hi->handle, chanserv_conf.max_owned);
        return 0;
    }
    if((dnr = chanserv_is_dnr(NULL, new_owner_hi)) && !force) {
        if(!IsHelping(user))
            reply("CSMSG_DNR_ACCOUNT", new_owner_hi->handle);
        else
            chanserv_show_dnrs(user, cmd, NULL, new_owner_hi->handle);
        return 0;
    }
    invoker = GetChannelUser(cData, user->handle_info);
    if(invoker->access <= UL_OWNER)
    {
        confirm = make_confirmation_string(curr_user);
        if((argc < 3) || strcmp(argv[2], confirm))
        {
            reply("CSMSG_CONFIRM_GIVEOWNERSHIP", new_owner_hi->handle, confirm);
            return 0;
        }
    }
    if(new_owner->access >= UL_COOWNER)
        co_access = new_owner->access;
    else
        co_access = UL_COOWNER;
    new_owner->access = UL_OWNER;
    if(curr_user)
        curr_user->access = co_access;
    cData->ownerTransfer = now;
    reply("CSMSG_OWNERSHIP_GIVEN", channel->name, new_owner_hi->handle);
    sprintf(reason, "%s ownership transferred to %s by %s.", channel->name, new_owner_hi->handle, user->handle_info->handle);
    global_message(MESSAGE_RECIPIENT_OPERS | MESSAGE_RECIPIENT_HELPERS, reason);
    return 1;
}

static CHANSERV_FUNC(cmd_suspend)
{
    struct handle_info *hi;
    struct userData *actor, *real_actor, *target;
    unsigned int override = 0;

    REQUIRE_PARAMS(2);
    if(!(hi = modcmd_get_handle_info(user, argv[1]))) return 0;
    actor = GetChannelUser(channel->channel_info, user->handle_info);
    real_actor = GetChannelAccess(channel->channel_info, user->handle_info);
    if(!(target = GetTrueChannelAccess(channel->channel_info, hi)))
    {
        reply("CSMSG_NO_CHAN_USER", hi->handle, channel->name);
        return 0;
    }
    if(target->access >= actor->access)
    {
        reply("MSG_USER_OUTRANKED", hi->handle);
        return 0;
    }
    if(target->flags & USER_SUSPENDED)
    {
        reply("CSMSG_ALREADY_SUSPENDED", hi->handle);
        return 0;
    }
    if(target->present)
    {
        target->present = 0;
        target->seen = now;
    }
    if(!real_actor || target->access >= real_actor->access)
        override = CMD_LOG_OVERRIDE;
    target->flags |= USER_SUSPENDED;
    reply("CSMSG_USER_SUSPENDED", hi->handle, channel->name);
    return 1 | override;
}

static CHANSERV_FUNC(cmd_unsuspend)
{
    struct handle_info *hi;
    struct userData *actor, *real_actor, *target;
    unsigned int override = 0;

    REQUIRE_PARAMS(2);
    if(!(hi = modcmd_get_handle_info(user, argv[1]))) return 0;
    actor = GetChannelUser(channel->channel_info, user->handle_info);
    real_actor = GetChannelAccess(channel->channel_info, user->handle_info);
    if(!(target = GetTrueChannelAccess(channel->channel_info, hi)))
    {
        reply("CSMSG_NO_CHAN_USER", hi->handle, channel->name);
        return 0;
    }
    if(target->access >= actor->access)
    {
        reply("MSG_USER_OUTRANKED", hi->handle);
        return 0;
    }
    if(!(target->flags & USER_SUSPENDED))
    {
        reply("CSMSG_NOT_SUSPENDED", hi->handle);
        return 0;
    }
    if(!real_actor || target->access >= real_actor->access)
        override = CMD_LOG_OVERRIDE;
    target->flags &= ~USER_SUSPENDED;
    scan_user_presence(target, NULL);
    reply("CSMSG_USER_UNSUSPENDED", hi->handle, channel->name);
    return 1 | override;
}

static MODCMD_FUNC(cmd_deleteme)
{
    struct handle_info *hi;
    struct userData *target;
    const char *confirm_string;
    unsigned short access_level;
    char *channel_name;

    hi = user->handle_info;
    if(!(target = GetTrueChannelAccess(channel->channel_info, hi)))
    {
        reply("CSMSG_NO_CHAN_USER", hi->handle, channel->name);
        return 0;
    }
    if(target->access == UL_OWNER)
    {
        reply("CSMSG_NO_OWNER_DELETEME", channel->name);
        return 0;
    }
    confirm_string = make_confirmation_string(target);
    if((argc < 2) || strcmp(argv[1], confirm_string))
    {
        reply("CSMSG_CONFIRM_DELETEME", confirm_string);
        return 0;
    }
    access_level = target->access;
    channel_name = strdup(channel->name);
    del_channel_user(target, 1);
    reply("CSMSG_DELETED_YOU", access_level, channel_name);
    free(channel_name);
    return 1;
}

static void
chanserv_refresh_topics(UNUSED_ARG(void *data))
{
    unsigned int refresh_num = (now - self->link_time) / chanserv_conf.refresh_period;
    struct chanData *cData;
    char opt;

    for(cData = channelList; cData; cData = cData->next)
    {
        if(IsSuspended(cData))
            continue;
        opt = cData->chOpts[chTopicRefresh];
        if(opt == 'n')
            continue;
        if((refresh_num - cData->last_refresh) < (unsigned int)(1 << (opt - '1')))
            continue;
        if(cData->topic)
            SetChannelTopic(cData->channel, chanserv, cData->topic, 1);
        cData->last_refresh = refresh_num;
    }
    timeq_add(now + chanserv_conf.refresh_period, chanserv_refresh_topics, NULL);
}

static CHANSERV_FUNC(cmd_unf)
{
    if(channel)
    {
        char response[MAXLEN];
        const char *fmt = user_find_message(user, "CSMSG_UNF_RESPONSE");
        sprintf(response, "%s: %s", user->nick, fmt);
        irc_privmsg(cmd->parent->bot, channel->name, response);
    }
    else
        reply("CSMSG_UNF_RESPONSE");
    return 1;
}

static CHANSERV_FUNC(cmd_ping)
{
    if(channel)
    {
        char response[MAXLEN];
        const char *fmt = user_find_message(user, "CSMSG_PING_RESPONSE");
        sprintf(response, "%s: %s", user->nick, fmt);
        irc_privmsg(cmd->parent->bot, channel->name, response);
    }
    else
        reply("CSMSG_PING_RESPONSE");
    return 1;
}

static CHANSERV_FUNC(cmd_wut)
{
    if(channel)
    {
        char response[MAXLEN];
        const char *fmt = user_find_message(user, "CSMSG_WUT_RESPONSE");
        sprintf(response, "%s: %s", user->nick, fmt);
        irc_privmsg(cmd->parent->bot, channel->name, response);
    }
    else
        reply("CSMSG_WUT_RESPONSE");
    return 1;
}

static CHANSERV_FUNC(cmd_8ball)
{
    unsigned int i, j, accum;
    const char *resp;

    REQUIRE_PARAMS(2);
    accum = 0;
    for(i=1; i<argc; i++)
        for(j=0; argv[i][j]; j++)
            accum = (accum << 5) - accum + toupper(argv[i][j]);
    resp = chanserv_conf.eightball->list[accum % chanserv_conf.eightball->used];
    if(channel)
    {
        char response[MAXLEN];
        sprintf(response, "%s: %s", user->nick, resp);
        irc_privmsg(cmd->parent->bot, channel->name, response);
    }
    else
        send_message_type(4, user, cmd->parent->bot, "%s", resp);
    return 1;
}

static CHANSERV_FUNC(cmd_d)
{
    unsigned long sides, count, modifier, ii, total;
    char response[MAXLEN], *sep;
    const char *fmt;

    REQUIRE_PARAMS(2);
    if((count = strtoul(argv[1], &sep, 10)) < 1)
        goto no_dice;
    if(sep[0] == 0)
    {
        if(count == 1)
            goto no_dice;
        sides = count;
        count = 1;
        modifier = 0;
    }
    else if(((sep[0] == 'd') || (sep[0] == 'D')) && isdigit(sep[1])
            && (sides = strtoul(sep+1, &sep, 10)) > 1)
    {
        if(sep[0] == 0)
            modifier = 0;
        else if((sep[0] == '-') && isdigit(sep[1]))
            modifier = strtoul(sep, NULL, 10);
        else if((sep[0] == '+') && isdigit(sep[1]))
            modifier = strtoul(sep+1, NULL, 10);
        else
            goto no_dice;
    }
    else
    {
      no_dice:
        reply("CSMSG_BAD_DIE_FORMAT", argv[1]);
        return 0;
    }
    if(count > 10)
    {
        reply("CSMSG_BAD_DICE_COUNT", count, 10);
        return 0;
    }
    for(total = ii = 0; ii < count; ++ii)
        total += (rand() % sides) + 1;
    total += modifier;

    if((count > 1) || modifier)
    {
        fmt = user_find_message(user, "CSMSG_DICE_ROLL");
        sprintf(response, fmt, total, count, sides, modifier);
    }
    else
    {
        fmt = user_find_message(user, "CSMSG_DIE_ROLL");
        sprintf(response, fmt, total, sides);
    }
    if(channel)
        send_channel_message(channel, cmd->parent->bot, "$b%s$b: %s", user->nick, response);
    else
        send_message_type(4, user, cmd->parent->bot, "%s", response);
    return 1;
}

static CHANSERV_FUNC(cmd_huggle)
{
    /* CTCP must be via PRIVMSG, never notice */
    if(channel)
        send_target_message(1, channel->name, cmd->parent->bot, "CSMSG_HUGGLES_HIM", user->nick);
    else
        send_target_message(1, user->nick, cmd->parent->bot, "CSMSG_HUGGLES_YOU");
    return 1;
}

static void
chanserv_adjust_limit(void *data)
{
    struct mod_chanmode change;
    struct chanData *cData = data;
    struct chanNode *channel = cData->channel;
    unsigned int limit;

    if(IsSuspended(cData))
        return;

    cData->limitAdjusted = now;
    limit = channel->members.used + chanserv_conf.adjust_threshold + 5;
    if(cData->modes.modes_set & MODE_LIMIT)
    {
        if(limit > cData->modes.new_limit)
            limit = cData->modes.new_limit;
        else if(limit == cData->modes.new_limit)
            return;
    }

    mod_chanmode_init(&change);
    change.modes_set = MODE_LIMIT;
    change.new_limit = limit;
    mod_chanmode_announce(chanserv, channel, &change);
}

static void
handle_new_channel(struct chanNode *channel)
{
    struct chanData *cData;

    if(!(cData = channel->channel_info))
        return;

    if(cData->modes.modes_set || cData->modes.modes_clear)
        mod_chanmode_announce(chanserv, cData->channel, &cData->modes);

    if(self->uplink && !self->uplink->burst && channel->channel_info->topic)
        SetChannelTopic(channel, chanserv, channel->channel_info->topic, 1);
}

/* Welcome to my worst nightmare. Warning: Read (or modify)
   the code below at your own risk. */
static int
handle_join(struct modeNode *mNode)
{
    struct mod_chanmode change;
    struct userNode *user = mNode->user;
    struct chanNode *channel = mNode->channel;
    struct chanData *cData;
    struct userData *uData = NULL;
    struct banData *bData;
    struct handle_info *handle;
    unsigned int modes = 0, info = 0;
    char *greeting;

    if(IsLocal(user) || !channel->channel_info || IsSuspended(channel->channel_info))
        return 0;

    cData = channel->channel_info;
    if(channel->members.used > cData->max)
        cData->max = channel->members.used;

    /* Check for bans.  If they're joining through a ban, one of two
     * cases applies:
     *   1: Join during a netburst, by riding the break.  Kick them
     *      unless they have ops or voice in the channel.
     *   2: They're allowed to join through the ban (an invite in
     *   ircu2.10, or a +e on Hybrid, or something).
     * If they're not joining through a ban, and the banlist is not
     * full, see if they're on the banlist for the channel.  If so,
     * kickban them.
     */
    if(user->uplink->burst && !mNode->modes)
    {
        unsigned int ii;
        for(ii = 0; ii < channel->banlist.used; ii++)
        {
            if(user_matches_glob(user, channel->banlist.list[ii]->ban, MATCH_USENICK))
            {
                /* Riding a netburst.  Naughty. */
                KickChannelUser(user, channel, chanserv, "User from far side of netsplit should have been banned - bye.");
                return 1;
            }
        }
    }

    mod_chanmode_init(&change);
    change.argc = 1;
    if(channel->banlist.used < MAXBANS)
    {
        /* Not joining through a ban. */
        for(bData = cData->bans;
            bData && !user_matches_glob(user, bData->mask, MATCH_USENICK);
            bData = bData->next);

        if(bData)
        {
            char kick_reason[MAXLEN];
            sprintf(kick_reason, "(%s) %s", bData->owner, bData->reason);

            bData->triggered = now;
            if(bData != cData->bans)
            {
                /* Shuffle the ban to the head of the list. */
                if(bData->next)
                    bData->next->prev = bData->prev;
                if(bData->prev)
                    bData->prev->next = bData->next;

                bData->prev = NULL;
                bData->next = cData->bans;

                if(cData->bans)
                    cData->bans->prev = bData;
                cData->bans = bData;
            }

            change.args[0].mode = MODE_BAN;
            change.args[0].u.hostmask = bData->mask;
            mod_chanmode_announce(chanserv, channel, &change);
            KickChannelUser(user, channel, chanserv, kick_reason);
            return 1;
        }
    }

    /* ChanServ will not modify the limits in join-flooded channels,
       or when there are enough slots left below the limit. */
    if((cData->flags & CHANNEL_DYNAMIC_LIMIT)
       && !channel->join_flooded
       && (channel->limit - channel->members.used) < chanserv_conf.adjust_threshold)
    {
        /* The user count has begun "bumping" into the channel limit,
           so set a timer to raise the limit a bit. Any previous
           timers are removed so three incoming users within the delay
           results in one limit change, not three. */

        timeq_del(0, chanserv_adjust_limit, cData, TIMEQ_IGNORE_WHEN);
        timeq_add(now + chanserv_conf.adjust_delay, chanserv_adjust_limit, cData);
    }

    if(channel->join_flooded)
    {
        /* don't automatically give ops or voice during a join flood */
    }
    else if(cData->lvlOpts[lvlGiveOps] == 0)
        modes |= MODE_CHANOP;
    else if(cData->lvlOpts[lvlGiveVoice] == 0)
        modes |= MODE_VOICE;

    greeting = cData->greeting;
    if(user->handle_info)
    {
        handle = user->handle_info;

        if(IsHelper(user) && !IsHelping(user))
        {
            unsigned int ii;
            for(ii = 0; ii < chanserv_conf.support_channels.used; ++ii)
            {
                if(channel == chanserv_conf.support_channels.list[ii])
                {
                    HANDLE_SET_FLAG(user->handle_info, HELPING);
                    break;
                }
            }
        }

        uData = GetTrueChannelAccess(cData, handle);
        if(uData && !IsUserSuspended(uData))
        {
            /* Ops and above were handled by the above case. */
            if(IsUserAutoOp(uData))
            {
                if(uData->access >= cData->lvlOpts[lvlGiveOps])
                    modes |= MODE_CHANOP;
                else if(uData->access >= cData->lvlOpts[lvlGiveVoice])
                    modes |= MODE_VOICE;
            }
            if(uData->access >= UL_PRESENT && !HANDLE_FLAGGED(uData->handle, BOT))
                cData->visited = now;
            if(cData->user_greeting)
                greeting = cData->user_greeting;
            if(uData->info
               && (uData->access >= cData->lvlOpts[lvlUserInfo])
               && ((now - uData->seen) >= chanserv_conf.info_delay)
               && !uData->present)
                info = 1;
            uData->seen = now;
            uData->present = 1;
        }
    }

    /* If user joining normally (not during burst), apply op or voice,
     * and send greeting/userinfo as appropriate.
     */
    if(!user->uplink->burst)
    {
        if(modes)
        {
            if(modes & MODE_CHANOP)
                modes &= ~MODE_VOICE;
            change.args[0].mode = modes;
            change.args[0].u.member = mNode;
            mod_chanmode_announce(chanserv, channel, &change);
        }
        if(greeting)
            send_message_type(4, user, chanserv, "(%s) %s", channel->name, greeting);
        if(uData && info && (modes || !(channel->modes & MODE_DELAYJOINS)))
            send_target_message(5, channel->name, chanserv, "[%s] %s", user->nick, uData->info);
    }
    return 0;
}

static void
handle_auth(struct userNode *user, UNUSED_ARG(struct handle_info *old_handle))
{
    struct mod_chanmode change;
    struct userData *channel;
    unsigned int ii, jj;

    if(!user->handle_info)
        return;

    mod_chanmode_init(&change);
    change.argc = 1;
    for(channel = user->handle_info->channels; channel; channel = channel->u_next)
    {
        struct chanNode *cn;
        struct modeNode *mn;
        if(IsUserSuspended(channel)
           || IsSuspended(channel->channel)
           || !(cn = channel->channel->channel))
            continue;

        mn = GetUserMode(cn, user);
        if(!mn)
        {
            if(!IsUserSuspended(channel)
               && IsUserAutoInvite(channel)
               && (channel->access >= channel->channel->lvlOpts[lvlInviteMe])
               && !self->burst
               && !user->uplink->burst)
                irc_invite(chanserv, user, cn);
            continue;
        }

        if(channel->access >= UL_PRESENT && !HANDLE_FLAGGED(channel->handle, BOT))
            channel->channel->visited = now;

        if(IsUserAutoOp(channel))
        {
            if(channel->access >= cn->channel_info->lvlOpts[lvlGiveOps])
                change.args[0].mode = MODE_CHANOP;
            else if(channel->access >= cn->channel_info->lvlOpts[lvlGiveVoice])
                change.args[0].mode = MODE_VOICE;
            else
                change.args[0].mode = 0;
            change.args[0].u.member = mn;
            if(change.args[0].mode)
                mod_chanmode_announce(chanserv, cn, &change);
        }

        channel->seen = now;
        channel->present = 1;
    }

    for(ii = 0; ii < user->channels.used; ++ii)
    {
        struct chanNode *chan = user->channels.list[ii]->channel;
        struct banData *ban;

        if((user->channels.list[ii]->modes & (MODE_CHANOP|MODE_VOICE))
           || !chan->channel_info
           || IsSuspended(chan->channel_info))
            continue;
        for(jj = 0; jj < chan->banlist.used; ++jj)
            if(user_matches_glob(user, chan->banlist.list[jj]->ban, MATCH_USENICK))
                break;
        if(jj < chan->banlist.used)
            continue;
        for(ban = chan->channel_info->bans; ban; ban = ban->next)
        {
            char kick_reason[MAXLEN];
            if(!user_matches_glob(user, ban->mask, MATCH_USENICK | MATCH_VISIBLE))
                continue;
            change.args[0].mode = MODE_BAN;
            change.args[0].u.hostmask = ban->mask;
            mod_chanmode_announce(chanserv, chan, &change);
            sprintf(kick_reason, "(%s) %s", ban->owner, ban->reason);
            KickChannelUser(user, chan, chanserv, kick_reason);
            ban->triggered = now;
            break;
        }
    }

    if(IsSupportHelper(user))
    {
        for(ii = 0; ii < chanserv_conf.support_channels.used; ++ii)
        {
            if(GetUserMode(chanserv_conf.support_channels.list[ii], user))
            {
                HANDLE_SET_FLAG(user->handle_info, HELPING);
                break;
            }
        }
    }
}

static void
handle_part(struct modeNode *mn, UNUSED_ARG(const char *reason))
{
    struct chanData *cData;
    struct userData *uData;

    cData = mn->channel->channel_info;
    if(!cData || IsSuspended(cData) || IsLocal(mn->user))
        return;

    if((cData->flags & CHANNEL_DYNAMIC_LIMIT) && !mn->channel->join_flooded)
    {
        /* Allow for a bit of padding so that the limit doesn't
           track the user count exactly, which could get annoying. */
        if((mn->channel->limit - mn->channel->members.used) > chanserv_conf.adjust_threshold + 5)
        {
            timeq_del(0, chanserv_adjust_limit, cData, TIMEQ_IGNORE_WHEN);
            timeq_add(now + chanserv_conf.adjust_delay, chanserv_adjust_limit, cData);
        }
    }

    if((uData = GetTrueChannelAccess(cData, mn->user->handle_info)))
    {
        scan_user_presence(uData, mn->user);
        uData->seen = now;
        if (uData->access >= UL_PRESENT && !HANDLE_FLAGGED(uData->handle, BOT))
            cData->visited = now;
    }

    if(IsHelping(mn->user) && IsSupportHelper(mn->user))
    {
        unsigned int ii;
        for(ii = 0; ii < chanserv_conf.support_channels.used; ++ii) {
            struct chanNode *channel;
            struct userNode *exclude;
            /* When looking at the channel that is being /part'ed, we
             * have to skip over the client that is leaving.  For
             * other channels, we must not do that.
             */
            channel = chanserv_conf.support_channels.list[ii];
            exclude = (channel == mn->channel) ? mn->user : NULL;
            if(find_handle_in_channel(channel, mn->user->handle_info, exclude))
                break;
        }
        if(ii == chanserv_conf.support_channels.used)
            HANDLE_CLEAR_FLAG(mn->user->handle_info, HELPING);
    }
}

static void
handle_kick(struct userNode *kicker, struct userNode *victim, struct chanNode *channel)
{
    struct userData *uData;

    if(!channel->channel_info || !kicker || IsService(kicker)
       || (kicker == victim) || IsSuspended(channel->channel_info)
       || (kicker->handle_info && kicker->handle_info == victim->handle_info))
        return;

    if(protect_user(victim, kicker, channel->channel_info))
    {
        const char *reason = user_find_message(kicker, "CSMSG_USER_PROTECTED_2");
        KickChannelUser(kicker, channel, chanserv, reason);
    }

    if((uData = GetTrueChannelAccess(channel->channel_info, victim->handle_info)))
        uData->seen = now;
}

static int
handle_topic(struct userNode *user, struct chanNode *channel, const char *old_topic)
{
    struct chanData *cData;

    if(!channel->channel_info || !user || IsSuspended(channel->channel_info) || IsService(user))
        return 0;

    cData = channel->channel_info;
    if(bad_topic(channel, user, channel->topic))
    {
        send_message(user, chanserv, "CSMSG_TOPIC_LOCKED", channel->name);
        if(cData->topic_mask && match_ircglob(old_topic, cData->topic_mask))
            SetChannelTopic(channel, chanserv, old_topic, 1);
        else if(cData->topic)
            SetChannelTopic(channel, chanserv, cData->topic, 1);
        return 1;
    }
    /* With topicsnarf, grab the topic and save it as the default topic. */
    if(check_user_level(channel, user, lvlTopicSnarf, 0, 0))
    {
        free(cData->topic);
        cData->topic = strdup(channel->topic);
    }
    return 0;
}

static void
handle_mode(struct chanNode *channel, struct userNode *user, const struct mod_chanmode *change)
{
    struct mod_chanmode *bounce = NULL;
    unsigned int bnc, ii;
    char deopped = 0;

    if(!channel->channel_info || IsLocal(user) || IsSuspended(channel->channel_info) || IsService(user))
        return;

    if(!check_user_level(channel, user, lvlEnfModes, 1, 0)
       && mode_lock_violated(&channel->channel_info->modes, change))
    {
        char correct[MAXLEN];
        bounce = mod_chanmode_dup(&channel->channel_info->modes, change->argc + 1);
        mod_chanmode_format(&channel->channel_info->modes, correct);
        send_message(user, chanserv, "CSMSG_MODE_LOCKED", correct, channel->name);
    }
    for(ii = bnc = 0; ii < change->argc; ++ii)
    {
        if((change->args[ii].mode & (MODE_REMOVE|MODE_CHANOP)) == (MODE_REMOVE|MODE_CHANOP))
        {
            const struct userNode *victim = change->args[ii].u.member->user;
            if(!protect_user(victim, user, channel->channel_info))
                continue;
            if(!bounce)
                bounce = mod_chanmode_alloc(change->argc + 1 - ii);
            if(!deopped)
            {
                bounce->args[bnc].mode = MODE_REMOVE | MODE_CHANOP;
                bounce->args[bnc].u.member = GetUserMode(channel, user);
                if(bounce->args[bnc].u.member)
                    bnc++;
                deopped = 1;
            }
            bounce->args[bnc].mode = MODE_CHANOP;
            bounce->args[bnc].u.member = change->args[ii].u.member;
            bnc++;
            send_message(user, chanserv, "CSMSG_USER_PROTECTED", victim->nick);
        }
        else if(change->args[ii].mode & MODE_CHANOP)
        {
            const struct userNode *victim = change->args[ii].u.member->user;
            if(IsService(victim) || validate_op(user, channel, (struct userNode*)victim))
                continue;
            if(!bounce)
                bounce = mod_chanmode_alloc(change->argc + 1 - ii);
            bounce->args[bnc].mode = MODE_REMOVE | MODE_CHANOP;
            bounce->args[bnc].u.member = change->args[ii].u.member;
            bnc++;
        }
        else if((change->args[ii].mode & (MODE_REMOVE | MODE_BAN)) == MODE_BAN)
        {
            const char *ban = change->args[ii].u.hostmask;
            if(!bad_channel_ban(channel, user, ban, NULL, NULL))
                continue;
            if(!bounce)
                bounce = mod_chanmode_alloc(change->argc + 1 - ii);
            bounce->args[bnc].mode = MODE_REMOVE | MODE_BAN;
            bounce->args[bnc].u.hostmask = strdup(ban);
            bnc++;
            send_message(user, chanserv, "CSMSG_MASK_PROTECTED", ban);
        }
    }
    if(bounce)
    {
        if((bounce->argc = bnc) || bounce->modes_set || bounce->modes_clear)
            mod_chanmode_announce(chanserv, channel, bounce);
        for(ii = 0; ii < change->argc; ++ii)
            if(bounce->args[ii].mode == (MODE_REMOVE | MODE_BAN))
                free((char*)bounce->args[ii].u.hostmask);
        mod_chanmode_free(bounce);
    }
}

static void
handle_nick_change(struct userNode *user, UNUSED_ARG(const char *old_nick))
{
    struct chanNode *channel;
    struct banData *bData;
    struct mod_chanmode change;
    unsigned int ii, jj;
    char kick_reason[MAXLEN];

    mod_chanmode_init(&change);
    change.argc = 1;
    change.args[0].mode = MODE_BAN;
    for(ii = 0; ii < user->channels.used; ++ii)
    {
        channel = user->channels.list[ii]->channel;
        /* Need not check for bans if they're opped or voiced. */
        if(user->channels.list[ii]->modes & (MODE_CHANOP|MODE_VOICE))
            continue;
        /* Need not check for bans unless channel registration is active. */
        if(!channel->channel_info || IsSuspended(channel->channel_info))
            continue;
        /* Look for a matching ban already on the channel. */
        for(jj = 0; jj < channel->banlist.used; ++jj)
            if(user_matches_glob(user, channel->banlist.list[jj]->ban, MATCH_USENICK))
                break;
        /* Need not act if we found one. */
        if(jj < channel->banlist.used)
            continue;
        /* Look for a matching ban in this channel. */
        for(bData = channel->channel_info->bans; bData; bData = bData->next)
        {
            if(!user_matches_glob(user, bData->mask, MATCH_USENICK | MATCH_VISIBLE))
                continue;
            change.args[0].u.hostmask = bData->mask;
            mod_chanmode_announce(chanserv, channel, &change);
            sprintf(kick_reason, "(%s) %s", bData->owner, bData->reason);
            KickChannelUser(user, channel, chanserv, kick_reason);
            bData->triggered = now;
            break; /* we don't need to check any more bans in the channel */
        }
    }
}

static void handle_rename(struct handle_info *handle, const char *old_handle)
{
    struct do_not_register *dnr = dict_find(handle_dnrs, old_handle, NULL);

    if(dnr)
    {
        dict_remove2(handle_dnrs, old_handle, 1);
        safestrncpy(dnr->chan_name + 1, handle->handle, sizeof(dnr->chan_name) - 1);
        dict_insert(handle_dnrs, dnr->chan_name + 1, dnr);
    }
}

static void
handle_unreg(UNUSED_ARG(struct userNode *user), struct handle_info *handle)
{
    struct userNode *h_user;

    if(handle->channels)
    {
        for(h_user = handle->users; h_user; h_user = h_user->next_authed)
            send_message(h_user, chanserv, "CSMSG_HANDLE_UNREGISTERED");

        while(handle->channels)
            del_channel_user(handle->channels, 1);
    }
}

static void
handle_server_link(UNUSED_ARG(struct server *server))
{
    struct chanData *cData;

    for(cData = channelList; cData; cData = cData->next)
    {
        if(!IsSuspended(cData))
            cData->may_opchan = 1;
        if((cData->flags & CHANNEL_DYNAMIC_LIMIT)
           && !cData->channel->join_flooded
           && ((cData->channel->limit - cData->channel->members.used)
               < chanserv_conf.adjust_threshold))
        {
            timeq_del(0, chanserv_adjust_limit, cData, TIMEQ_IGNORE_WHEN);
            timeq_add(now + chanserv_conf.adjust_delay, chanserv_adjust_limit, cData);
        }
    }
}

static void
chanserv_conf_read(void)
{
    dict_t conf_node;
    const char *str;
    char mode_line[MAXLEN], *modes[MAXNUMPARAMS];
    struct mod_chanmode *change;
    struct string_list *strlist;
    struct chanNode *chan;
    unsigned int ii;

    if(!(conf_node = conf_get_data(CHANSERV_CONF_NAME, RECDB_OBJECT)))
    {
        log_module(CS_LOG, LOG_ERROR, "Invalid config node `%s'.", CHANSERV_CONF_NAME);
        return;
    }
    for(ii = 0; ii < chanserv_conf.support_channels.used; ++ii)
        UnlockChannel(chanserv_conf.support_channels.list[ii]);
    chanserv_conf.support_channels.used = 0;
    if((strlist = database_get_data(conf_node, KEY_SUPPORT_CHANNEL, RECDB_STRING_LIST)))
    {
        for(ii = 0; ii < strlist->used; ++ii)
        {
            const char *str2 = database_get_data(conf_node, KEY_SUPPORT_CHANNEL_MODES, RECDB_QSTRING);
            if(!str2)
                str2 = "+nt";
            chan = AddChannel(strlist->list[ii], now, str2, NULL);
            LockChannel(chan);
            channelList_append(&chanserv_conf.support_channels, chan);
        }
    }
    else if((str = database_get_data(conf_node, KEY_SUPPORT_CHANNEL, RECDB_QSTRING)))
    {
        const char *str2;
        str2 = database_get_data(conf_node, KEY_SUPPORT_CHANNEL_MODES, RECDB_QSTRING);
        if(!str2)
            str2 = "+nt";
        chan = AddChannel(str, now, str2, NULL);
        LockChannel(chan);
        channelList_append(&chanserv_conf.support_channels, chan);
    }
    str = database_get_data(conf_node, KEY_DB_BACKUP_FREQ, RECDB_QSTRING);
    chanserv_conf.db_backup_frequency = str ? ParseInterval(str) : 7200;
    str = database_get_data(conf_node, KEY_INFO_DELAY, RECDB_QSTRING);
    chanserv_conf.info_delay = str ? ParseInterval(str) : 180;
    str = database_get_data(conf_node, KEY_MAX_GREETLEN, RECDB_QSTRING);
    chanserv_conf.greeting_length = str ? atoi(str) : 200;
    str = database_get_data(conf_node, KEY_ADJUST_THRESHOLD, RECDB_QSTRING);
    chanserv_conf.adjust_threshold = str ? atoi(str) : 15;
    str = database_get_data(conf_node, KEY_ADJUST_DELAY, RECDB_QSTRING);
    chanserv_conf.adjust_delay = str ? ParseInterval(str) : 30;
    str = database_get_data(conf_node, KEY_CHAN_EXPIRE_FREQ, RECDB_QSTRING);
    chanserv_conf.channel_expire_frequency = str ? ParseInterval(str) : 86400;
    str = database_get_data(conf_node, KEY_CHAN_EXPIRE_DELAY, RECDB_QSTRING);
    chanserv_conf.channel_expire_delay = str ? ParseInterval(str) : 86400*30;
    str = database_get_data(conf_node, KEY_DNR_EXPIRE_FREQ, RECDB_QSTRING);
    chanserv_conf.dnr_expire_frequency = str ? ParseInterval(str) : 3600;
    str = database_get_data(conf_node, KEY_NODELETE_LEVEL, RECDB_QSTRING);
    chanserv_conf.nodelete_level = str ? atoi(str) : 1;
    str = database_get_data(conf_node, KEY_MAX_CHAN_USERS, RECDB_QSTRING);
    chanserv_conf.max_chan_users = str ? atoi(str) : 512;
    str = database_get_data(conf_node, KEY_MAX_CHAN_BANS, RECDB_QSTRING);
    chanserv_conf.max_chan_bans = str ? atoi(str) : 512;
    str = database_get_data(conf_node, KEY_MAX_USERINFO_LENGTH, RECDB_QSTRING);
    chanserv_conf.max_userinfo_length = str ? atoi(str) : 400;
    str = database_get_data(conf_node, KEY_NICK, RECDB_QSTRING);
    if(chanserv && str)
        NickChange(chanserv, str, 0);
    str = database_get_data(conf_node, KEY_REFRESH_PERIOD, RECDB_QSTRING);
    chanserv_conf.refresh_period = str ? ParseInterval(str) : 3*60*60;
    str = database_get_data(conf_node, KEY_GIVEOWNERSHIP_PERIOD, RECDB_QSTRING);
    chanserv_conf.giveownership_period = str ? ParseInterval(str) : 0;
    str = database_get_data(conf_node, KEY_CTCP_SHORT_BAN_DURATION, RECDB_QSTRING);
    chanserv_conf.ctcp_short_ban_duration = str ? str : "3m";
    str = database_get_data(conf_node, KEY_CTCP_LONG_BAN_DURATION, RECDB_QSTRING);
    chanserv_conf.ctcp_long_ban_duration = str ? str : "1h";
    str = database_get_data(conf_node, KEY_MAX_OWNED, RECDB_QSTRING);
    chanserv_conf.max_owned = str ? atoi(str) : 5;
    str = database_get_data(conf_node, KEY_IRC_OPERATOR_EPITHET, RECDB_QSTRING);
    chanserv_conf.irc_operator_epithet = str ? str : "a megalomaniacal power hungry tyrant";
    str = database_get_data(conf_node, KEY_NETWORK_HELPER_EPITHET, RECDB_QSTRING);
    chanserv_conf.network_helper_epithet = str ? str : "a wannabe tyrant";
    str = database_get_data(conf_node, KEY_SUPPORT_HELPER_EPITHET, RECDB_QSTRING);
    chanserv_conf.support_helper_epithet = str ? str : "a wannabe tyrant";
    str = database_get_data(conf_node, "default_modes", RECDB_QSTRING);
    if(!str)
        str = "+nt";
    safestrncpy(mode_line, str, sizeof(mode_line));
    ii = split_line(mode_line, 0, ArrayLength(modes), modes);
    if((change = mod_chanmode_parse(NULL, modes, ii, MCP_KEY_FREE|MCP_NO_APASS, 0))
       && (change->argc < 2))
    {
        chanserv_conf.default_modes = *change;
        mod_chanmode_free(change);
    }
    free_string_list(chanserv_conf.set_shows);
    strlist = database_get_data(conf_node, "set_shows", RECDB_STRING_LIST);
    if(strlist)
        strlist = string_list_copy(strlist);
    else
    {
        static const char *list[] = {
            /* free form text */
            "DefaultTopic", "TopicMask", "Greeting", "UserGreeting", "Modes",
            /* options based on user level */
            "PubCmd", "InviteMe", "UserInfo", "GiveVoice", "GiveOps", "EnfOps",
            "EnfModes", "EnfTopic", "TopicSnarf", "Setters", "CtcpUsers",
            /* multiple choice options */
            "CtcpReaction", "Protect", "Toys", "TopicRefresh",
            /* binary options */
            "DynLimit", "NoDelete",
            /* delimiter */
            NULL
        };
        strlist = alloc_string_list(ArrayLength(list)-1);
        for(ii=0; list[ii]; ii++)
            string_list_append(strlist, strdup(list[ii]));
    }
    chanserv_conf.set_shows = strlist;
    /* We don't look things up now, in case the list refers to options
     * defined by modules initialized after this point.  Just mark the
     * function list as invalid, so it will be initialized.
     */
    set_shows_list.used = 0;
    free_string_list(chanserv_conf.eightball);
    strlist = database_get_data(conf_node, KEY_8BALL_RESPONSES, RECDB_STRING_LIST);
    if(strlist)
    {
        strlist = string_list_copy(strlist);
    }
    else
    {
        strlist = alloc_string_list(4);
        string_list_append(strlist, strdup("Yes."));
        string_list_append(strlist, strdup("No."));
        string_list_append(strlist, strdup("Maybe so."));
    }
    chanserv_conf.eightball = strlist;
    free_string_list(chanserv_conf.old_ban_names);
    strlist = database_get_data(conf_node, KEY_OLD_BAN_NAMES, RECDB_STRING_LIST);
    if(strlist)
        strlist = string_list_copy(strlist);
    else
        strlist = alloc_string_list(2);
    chanserv_conf.old_ban_names = strlist;
    str = database_get_data(conf_node, "off_channel", RECDB_QSTRING);
    off_channel = str ? atoi(str) : 0;
}

static void
chanserv_note_type_read(const char *key, struct record_data *rd)
{
    dict_t obj;
    struct note_type *ntype;
    const char *str;

    if(!(obj = GET_RECORD_OBJECT(rd)))
    {
        log_module(CS_LOG, LOG_ERROR, "Invalid note type %s.", key);
        return;
    }
    if(!(ntype = chanserv_create_note_type(key)))
    {
        log_module(CS_LOG, LOG_ERROR, "Memory allocation failed for note %s.", key);
        return;
    }

    /* Figure out set access */
    if((str = database_get_data(obj, KEY_NOTE_OPSERV_ACCESS, RECDB_QSTRING)))
    {
        ntype->set_access_type = NOTE_SET_PRIVILEGED;
        ntype->set_access.min_opserv = strtoul(str, NULL, 0);
    }
    else if((str = database_get_data(obj, KEY_NOTE_CHANNEL_ACCESS, RECDB_QSTRING)))
    {
        ntype->set_access_type = NOTE_SET_CHANNEL_ACCESS;
        ntype->set_access.min_ulevel = strtoul(str, NULL, 0);
    }
    else if((str = database_get_data(obj, KEY_NOTE_SETTER_ACCESS, RECDB_QSTRING)))
    {
        ntype->set_access_type = NOTE_SET_CHANNEL_SETTER;
    }
    else
    {
        log_module(CS_LOG, LOG_ERROR, "Could not find access type for note %s; defaulting to OpServ access level 0.", key);
        ntype->set_access_type = NOTE_SET_PRIVILEGED;
        ntype->set_access.min_opserv = 0;
    }

    /* Figure out visibility */
    if(!(str = database_get_data(obj, KEY_NOTE_VISIBILITY, RECDB_QSTRING)))
        ntype->visible_type = NOTE_VIS_PRIVILEGED;
    else if(!irccasecmp(str, KEY_NOTE_VIS_PRIVILEGED))
        ntype->visible_type = NOTE_VIS_PRIVILEGED;
    else if(!irccasecmp(str, KEY_NOTE_VIS_CHANNEL_USERS))
        ntype->visible_type = NOTE_VIS_CHANNEL_USERS;
    else if(!irccasecmp(str, KEY_NOTE_VIS_ALL))
        ntype->visible_type = NOTE_VIS_ALL;
    else
        ntype->visible_type = NOTE_VIS_PRIVILEGED;

    str = database_get_data(obj, KEY_NOTE_MAX_LENGTH, RECDB_QSTRING);
    ntype->max_length = str ? strtoul(str, NULL, 0) : 400;
}

static void
user_read_helper(const char *key, struct record_data *rd, struct chanData *chan)
{
    struct handle_info *handle;
    struct userData *uData;
    char *seen, *inf, *flags;
    unsigned long last_seen;
    unsigned short access_level;

    if(rd->type != RECDB_OBJECT || !dict_size(rd->d.object))
    {
        log_module(CS_LOG, LOG_ERROR, "Invalid user in %s.", chan->channel->name);
        return;
    }

    access_level = atoi(database_get_data(rd->d.object, KEY_LEVEL, RECDB_QSTRING));
    if(access_level > UL_OWNER)
    {
        log_module(CS_LOG, LOG_ERROR, "Invalid access level for %s in %s.", key, chan->channel->name);
        return;
    }

    inf = database_get_data(rd->d.object, KEY_INFO, RECDB_QSTRING);
    seen = database_get_data(rd->d.object, KEY_SEEN, RECDB_QSTRING);
    last_seen = seen ? strtoul(seen, NULL, 0) : now;
    flags = database_get_data(rd->d.object, KEY_FLAGS, RECDB_QSTRING);
    handle = get_handle_info(key);
    if(!handle)
    {
        log_module(CS_LOG, LOG_ERROR, "Nonexistent account %s in %s.", key, chan->channel->name);
        return;
    }

    uData = add_channel_user(chan, handle, access_level, last_seen, inf);
    uData->flags = flags ? strtoul(flags, NULL, 0) : 0;
}

static void
ban_read_helper(const char *key, struct record_data *rd, struct chanData *chan)
{
    char *set, *triggered, *s_duration, *s_expires, *reason, *owner;
    unsigned long set_time, triggered_time, expires_time;

    if(rd->type != RECDB_OBJECT || !dict_size(rd->d.object))
    {
        log_module(CS_LOG, LOG_ERROR, "Invalid ban in %s.", chan->channel->name);
        return;
    }

    set = database_get_data(rd->d.object, KEY_SET, RECDB_QSTRING);
    triggered = database_get_data(rd->d.object, KEY_TRIGGERED, RECDB_QSTRING);
    s_duration = database_get_data(rd->d.object, KEY_DURATION, RECDB_QSTRING);
    s_expires = database_get_data(rd->d.object, KEY_EXPIRES, RECDB_QSTRING);
    owner = database_get_data(rd->d.object, KEY_OWNER, RECDB_QSTRING);
    reason = database_get_data(rd->d.object, KEY_REASON, RECDB_QSTRING);
    if (!reason || !owner)
        return;

    set_time = set ? strtoul(set, NULL, 0) : now;
    triggered_time = triggered ? strtoul(triggered, NULL, 0) : 0;
    if(s_expires)
        expires_time = strtoul(s_expires, NULL, 0);
    else if(s_duration)
        expires_time = set_time + atoi(s_duration);
    else
        expires_time = 0;

    if(!reason || (expires_time && (expires_time < now)))
        return;

    add_channel_ban(chan, key, owner, set_time, triggered_time, expires_time, reason);
}

static struct suspended *
chanserv_read_suspended(dict_t obj)
{
    struct suspended *suspended = calloc(1, sizeof(*suspended));
    char *str;
    dict_t previous;

    str = database_get_data(obj, KEY_EXPIRES, RECDB_QSTRING);
    suspended->expires = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(obj, KEY_REVOKED, RECDB_QSTRING);
    suspended->revoked = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(obj, KEY_ISSUED, RECDB_QSTRING);
    suspended->issued = str ? strtoul(str, NULL, 0) : 0;
    suspended->suspender = strdup(database_get_data(obj, KEY_SUSPENDER, RECDB_QSTRING));
    suspended->reason = strdup(database_get_data(obj, KEY_REASON, RECDB_QSTRING));
    previous = database_get_data(obj, KEY_PREVIOUS, RECDB_OBJECT);
    suspended->previous = previous ? chanserv_read_suspended(previous) : NULL;
    return suspended;
}

static int
chanserv_channel_read(const char *key, struct record_data *hir)
{
    struct suspended *suspended;
    struct mod_chanmode *modes;
    struct chanNode *cNode;
    struct chanData *cData;
    struct dict *channel, *obj;
    char *str, *argv[10];
    dict_iterator_t it;
    unsigned int argc;

    channel = hir->d.object;

    str = database_get_data(channel, KEY_REGISTRAR, RECDB_QSTRING);
    if(!str)
        str = "<unknown>";
    cNode = AddChannel(key, now, NULL, NULL);
    if(!cNode)
    {
        log_module(CS_LOG, LOG_ERROR, "Unable to create registered channel %s.", key);
        return 0;
    }
    cData = register_channel(cNode, str);
    if(!cData)
    {
        log_module(CS_LOG, LOG_ERROR, "Unable to register channel %s from database.", key);
        return 0;
    }

    if((obj = database_get_data(channel, KEY_OPTIONS, RECDB_OBJECT)))
    {
        enum levelOption lvlOpt;
        enum charOption chOpt;

        if((str = database_get_data(obj, KEY_FLAGS, RECDB_QSTRING)))
            cData->flags = atoi(str);

        for(lvlOpt = 0; lvlOpt < NUM_LEVEL_OPTIONS; ++lvlOpt)
        {
            str = database_get_data(obj, levelOptions[lvlOpt].db_name, RECDB_QSTRING);
            if(str)
                cData->lvlOpts[lvlOpt] = user_level_from_name(str, UL_OWNER+1);
            else if(levelOptions[lvlOpt].old_flag)
            {
                if(cData->flags & levelOptions[lvlOpt].old_flag)
                    cData->lvlOpts[lvlOpt] = levelOptions[lvlOpt].flag_value;
                else
                    cData->lvlOpts[lvlOpt] = levelOptions[lvlOpt].default_value;
            }
        }

        for(chOpt = 0; chOpt < NUM_CHAR_OPTIONS; ++chOpt)
        {
            if(!(str = database_get_data(obj, charOptions[chOpt].db_name, RECDB_QSTRING)))
                continue;
            cData->chOpts[chOpt] = str[0];
        }
    }
    else if((str = database_get_data(channel, KEY_FLAGS, RECDB_QSTRING)))
    {
        enum levelOption lvlOpt;
        enum charOption chOpt;
        unsigned int count;

        cData->flags = base64toint(str, 5);
        count = strlen(str += 5);
        for(lvlOpt = 0; lvlOpt < NUM_LEVEL_OPTIONS; ++lvlOpt)
        {
            unsigned short lvl;
            if(levelOptions[lvlOpt].old_flag)
            {
                if(cData->flags & levelOptions[lvlOpt].old_flag)
                    lvl = levelOptions[lvlOpt].flag_value;
                else
                    lvl = levelOptions[lvlOpt].default_value;
            }
            else switch(((count <= levelOptions[lvlOpt].old_idx) ? str : CHANNEL_DEFAULT_OPTIONS)[levelOptions[lvlOpt].old_idx])
            {
            case 'c': lvl = UL_COOWNER; break;
            case 'm': lvl = UL_MASTER; break;
            case 'n': lvl = UL_OWNER+1; break;
            case 'o': lvl = UL_OP; break;
            case 'p': lvl = UL_PEON; break;
            case 'w': lvl = UL_OWNER; break;
            default: lvl = 0; break;
            }
            cData->lvlOpts[lvlOpt] = lvl;
        }
        for(chOpt = 0; chOpt < NUM_CHAR_OPTIONS; ++chOpt)
            cData->chOpts[chOpt] = ((count <= charOptions[chOpt].old_idx) ? str : CHANNEL_DEFAULT_OPTIONS)[charOptions[chOpt].old_idx];
    }

    if((obj = database_get_data(hir->d.object, KEY_SUSPENDED, RECDB_OBJECT)))
    {
        suspended = chanserv_read_suspended(obj);
        cData->suspended = suspended;
        suspended->cData = cData;
        /* We could use suspended->expires and suspended->revoked to
         * set the CHANNEL_SUSPENDED flag, but we don't. */
    }
    else if(IsSuspended(cData) && (str = database_get_data(hir->d.object, KEY_SUSPENDER, RECDB_QSTRING)))
    {
        suspended = calloc(1, sizeof(*suspended));
        suspended->issued = 0;
        suspended->revoked = 0;
        suspended->suspender = strdup(str);
        str = database_get_data(hir->d.object, KEY_SUSPEND_EXPIRES, RECDB_QSTRING);
        suspended->expires = str ? atoi(str) : 0;
        str = database_get_data(hir->d.object, KEY_SUSPEND_REASON, RECDB_QSTRING);
        suspended->reason = strdup(str ? str : "No reason");
        suspended->previous = NULL;
        cData->suspended = suspended;
        suspended->cData = cData;
    }
    else
    {
        cData->flags &= ~CHANNEL_SUSPENDED;
        suspended = NULL; /* to squelch a warning */
    }

    if(IsSuspended(cData)) {
        if(suspended->expires > now)
            timeq_add(suspended->expires, chanserv_expire_suspension, suspended);
        else if(suspended->expires)
            cData->flags &= ~CHANNEL_SUSPENDED;
    }

    if((!off_channel || !IsOffChannel(cData)) && !IsSuspended(cData)) {
        struct mod_chanmode change;
        mod_chanmode_init(&change);
        change.argc = 1;
        change.args[0].mode = MODE_CHANOP;
        change.args[0].u.member = AddChannelUser(chanserv, cNode);
        mod_chanmode_announce(chanserv, cNode, &change);
    }

    str = database_get_data(channel, KEY_REGISTERED, RECDB_QSTRING);
    cData->registered = str ? strtoul(str, NULL, 0) : now;
    str = database_get_data(channel, KEY_VISITED, RECDB_QSTRING);
    cData->visited = str ? strtoul(str, NULL, 0) : now;
    str = database_get_data(channel, KEY_OWNER_TRANSFER, RECDB_QSTRING);
    cData->ownerTransfer = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(channel, KEY_MAX, RECDB_QSTRING);
    cData->max = str ? atoi(str) : 0;
    str = database_get_data(channel, KEY_GREETING, RECDB_QSTRING);
    cData->greeting = str ? strdup(str) : NULL;
    str = database_get_data(channel, KEY_USER_GREETING, RECDB_QSTRING);
    cData->user_greeting = str ? strdup(str) : NULL;
    str = database_get_data(channel, KEY_TOPIC_MASK, RECDB_QSTRING);
    cData->topic_mask = str ? strdup(str) : NULL;
    str = database_get_data(channel, KEY_TOPIC, RECDB_QSTRING);
    cData->topic = str ? strdup(str) : NULL;

    if(!IsSuspended(cData)
       && (str = database_get_data(channel, KEY_MODES, RECDB_QSTRING))
       && (argc = split_line(str, 0, ArrayLength(argv), argv))
       && (modes = mod_chanmode_parse(cNode, argv, argc, MCP_KEY_FREE|MCP_NO_APASS, 0))) {
        cData->modes = *modes;
        if(off_channel > 0)
          cData->modes.modes_set |= MODE_REGISTERED;
        if(cData->modes.argc > 1)
            cData->modes.argc = 1;
        mod_chanmode_announce(chanserv, cNode, &cData->modes);
        mod_chanmode_free(modes);
    }

    obj = database_get_data(channel, KEY_USERS, RECDB_OBJECT);
    for(it = dict_first(obj); it; it = iter_next(it))
        user_read_helper(iter_key(it), iter_data(it), cData);

    if(!cData->users && !IsProtected(cData))
    {
        log_module(CS_LOG, LOG_ERROR, "Channel %s had no users in database, unregistering it.", key);
        unregister_channel(cData, "has empty user list.");
        return 0;
    }

    obj = database_get_data(channel, KEY_BANS, RECDB_OBJECT);
    for(it = dict_first(obj); it; it = iter_next(it))
        ban_read_helper(iter_key(it), iter_data(it), cData);

    obj = database_get_data(channel, KEY_NOTES, RECDB_OBJECT);
    for(it = dict_first(obj); it; it = iter_next(it))
    {
        struct note_type *ntype = dict_find(note_types, iter_key(it), NULL);
        struct record_data *rd = iter_data(it);
        const char *note, *setter;

        if(rd->type != RECDB_OBJECT)
        {
            log_module(CS_LOG, LOG_ERROR, "Bad record type for note %s in channel %s.", iter_key(it), key);
        }
        else if(!ntype)
        {
            log_module(CS_LOG, LOG_ERROR, "Bad note type name %s in channel %s.", iter_key(it), key);
        }
        else if(!(note = database_get_data(rd->d.object, KEY_NOTE_NOTE, RECDB_QSTRING)))
        {
            log_module(CS_LOG, LOG_ERROR, "Missing note text for note %s in channel %s.", iter_key(it), key);
        }
        else
        {
            setter = database_get_data(rd->d.object, KEY_NOTE_SETTER, RECDB_QSTRING);
            if(!setter) setter = "<unknown>";
            chanserv_add_channel_note(cData, ntype, setter, note);
        }
    }

    return 0;
}

static void
chanserv_dnr_read(const char *key, struct record_data *hir)
{
    const char *setter, *reason, *str;
    struct do_not_register *dnr;
    unsigned long expiry;

    setter = database_get_data(hir->d.object, KEY_DNR_SETTER, RECDB_QSTRING);
    if(!setter)
    {
        log_module(CS_LOG, LOG_ERROR, "Missing setter for DNR %s.", key);
        return;
    }
    reason = database_get_data(hir->d.object, KEY_DNR_REASON, RECDB_QSTRING);
    if(!reason)
    {
        log_module(CS_LOG, LOG_ERROR, "Missing reason for DNR %s.", key);
        return;
    }
    str = database_get_data(hir->d.object, KEY_EXPIRES, RECDB_QSTRING);
    expiry = str ? strtoul(str, NULL, 0) : 0;
    if(expiry && expiry <= now)
        return;
    dnr = chanserv_add_dnr(key, setter, expiry, reason);
    if(!dnr)
        return;
    str = database_get_data(hir->d.object, KEY_DNR_SET, RECDB_QSTRING);
    if(str)
        dnr->set = atoi(str);
    else
        dnr->set = 0;
}

static int
chanserv_saxdb_read(struct dict *database)
{
    struct dict *section;
    dict_iterator_t it;

    if((section = database_get_data(database, KEY_NOTE_TYPES, RECDB_OBJECT)))
        for(it = dict_first(section); it; it = iter_next(it))
            chanserv_note_type_read(iter_key(it), iter_data(it));

    if((section = database_get_data(database, KEY_CHANNELS, RECDB_OBJECT)))
        for(it = dict_first(section); it; it = iter_next(it))
            chanserv_channel_read(iter_key(it), iter_data(it));

    if((section = database_get_data(database, KEY_DNR, RECDB_OBJECT)))
        for(it = dict_first(section); it; it = iter_next(it))
            chanserv_dnr_read(iter_key(it), iter_data(it));

    return 0;
}

static int
chanserv_write_users(struct saxdb_context *ctx, struct userData *uData)
{
    int high_present = 0;
    saxdb_start_record(ctx, KEY_USERS, 1);
    for(; uData; uData = uData->next)
    {
        if((uData->access >= UL_PRESENT) && uData->present && !HANDLE_FLAGGED(uData->handle, BOT))
            high_present = 1;
        saxdb_start_record(ctx, uData->handle->handle, 0);
        saxdb_write_int(ctx, KEY_LEVEL, uData->access);
        saxdb_write_int(ctx, KEY_SEEN, uData->seen);
        if(uData->flags)
            saxdb_write_int(ctx, KEY_FLAGS, uData->flags);
        if(uData->info)
            saxdb_write_string(ctx, KEY_INFO, uData->info);
        saxdb_end_record(ctx);
    }
    saxdb_end_record(ctx);
    return high_present;
}

static void
chanserv_write_bans(struct saxdb_context *ctx, struct banData *bData)
{
    if(!bData)
        return;
    saxdb_start_record(ctx, KEY_BANS, 1);
    for(; bData; bData = bData->next)
    {
        saxdb_start_record(ctx, bData->mask, 0);
        saxdb_write_int(ctx, KEY_SET, bData->set);
        if(bData->triggered)
            saxdb_write_int(ctx, KEY_TRIGGERED, bData->triggered);
        if(bData->expires)
            saxdb_write_int(ctx, KEY_EXPIRES, bData->expires);
        if(bData->owner[0])
            saxdb_write_string(ctx, KEY_OWNER, bData->owner);
        if(bData->reason)
            saxdb_write_string(ctx, KEY_REASON, bData->reason);
        saxdb_end_record(ctx);
    }
    saxdb_end_record(ctx);
}

static void
chanserv_write_suspended(struct saxdb_context *ctx, const char *name, struct suspended *susp)
{
    saxdb_start_record(ctx, name, 0);
    saxdb_write_string(ctx, KEY_SUSPENDER, susp->suspender);
    saxdb_write_string(ctx, KEY_REASON, susp->reason);
    if(susp->issued)
        saxdb_write_int(ctx, KEY_ISSUED, susp->issued);
    if(susp->expires)
        saxdb_write_int(ctx, KEY_EXPIRES, susp->expires);
    if(susp->revoked)
        saxdb_write_int(ctx, KEY_REVOKED, susp->revoked);
    if(susp->previous)
        chanserv_write_suspended(ctx, KEY_PREVIOUS, susp->previous);
    saxdb_end_record(ctx);
}

static void
chanserv_write_channel(struct saxdb_context *ctx, struct chanData *channel)
{
    char buf[MAXLEN];
    int high_present;
    enum levelOption lvlOpt;
    enum charOption chOpt;

    saxdb_start_record(ctx, channel->channel->name, 1);

    saxdb_write_int(ctx, KEY_REGISTERED, channel->registered);
    saxdb_write_int(ctx, KEY_MAX, channel->max);
    if(channel->topic)
        saxdb_write_string(ctx, KEY_TOPIC, channel->topic);
    if(channel->registrar)
        saxdb_write_string(ctx, KEY_REGISTRAR, channel->registrar);
    if(channel->greeting)
        saxdb_write_string(ctx, KEY_GREETING, channel->greeting);
    if(channel->user_greeting)
        saxdb_write_string(ctx, KEY_USER_GREETING, channel->user_greeting);
    if(channel->topic_mask)
        saxdb_write_string(ctx, KEY_TOPIC_MASK, channel->topic_mask);
    if(channel->suspended)
        chanserv_write_suspended(ctx, "suspended", channel->suspended);

    saxdb_start_record(ctx, KEY_OPTIONS, 0);
    saxdb_write_int(ctx, KEY_FLAGS, channel->flags);
    for(lvlOpt = 0; lvlOpt < NUM_LEVEL_OPTIONS; ++lvlOpt)
        saxdb_write_int(ctx, levelOptions[lvlOpt].db_name, channel->lvlOpts[lvlOpt]);
    for(chOpt = 0; chOpt < NUM_CHAR_OPTIONS; ++chOpt)
    {
        buf[0] = channel->chOpts[chOpt];
        buf[1] = '\0';
        saxdb_write_string(ctx, charOptions[chOpt].db_name, buf);
    }
    saxdb_end_record(ctx);

    if(channel->modes.modes_set || channel->modes.modes_clear)
    {
        mod_chanmode_format(&channel->modes, buf);
        saxdb_write_string(ctx, KEY_MODES, buf);
    }

    high_present = chanserv_write_users(ctx, channel->users);
    chanserv_write_bans(ctx, channel->bans);

    if(dict_size(channel->notes))
    {
        dict_iterator_t it;

        saxdb_start_record(ctx, KEY_NOTES, 1);
        for(it = dict_first(channel->notes); it; it = iter_next(it))
        {
            struct note *note = iter_data(it);
            saxdb_start_record(ctx, iter_key(it), 0);
            saxdb_write_string(ctx, KEY_NOTE_SETTER, note->setter);
            saxdb_write_string(ctx, KEY_NOTE_NOTE, note->note);
            saxdb_end_record(ctx);
        }
        saxdb_end_record(ctx);
    }

    if(channel->ownerTransfer)
        saxdb_write_int(ctx, KEY_OWNER_TRANSFER, channel->ownerTransfer);
    saxdb_write_int(ctx, KEY_VISITED, high_present ? now : channel->visited);
    saxdb_end_record(ctx);
}

static void
chanserv_write_note_type(struct saxdb_context *ctx, struct note_type *ntype)
{
    const char *str;

    saxdb_start_record(ctx, ntype->name, 0);
    switch(ntype->set_access_type)
    {
    case NOTE_SET_CHANNEL_ACCESS:
        saxdb_write_int(ctx, KEY_NOTE_CHANNEL_ACCESS, ntype->set_access.min_ulevel);
        break;
    case NOTE_SET_CHANNEL_SETTER:
        saxdb_write_int(ctx, KEY_NOTE_SETTER_ACCESS, 1);
        break;
    case NOTE_SET_PRIVILEGED: default:
        saxdb_write_int(ctx, KEY_NOTE_OPSERV_ACCESS, ntype->set_access.min_opserv);
        break;
    }
    switch(ntype->visible_type)
    {
    case NOTE_VIS_ALL: str = KEY_NOTE_VIS_ALL; break;
    case NOTE_VIS_CHANNEL_USERS: str = KEY_NOTE_VIS_CHANNEL_USERS; break;
    case NOTE_VIS_PRIVILEGED: default: str = KEY_NOTE_VIS_PRIVILEGED; break;
    }
    saxdb_write_string(ctx, KEY_NOTE_VISIBILITY, str);
    saxdb_write_int(ctx, KEY_NOTE_MAX_LENGTH, ntype->max_length);
    saxdb_end_record(ctx);
}

static void
write_dnrs_helper(struct saxdb_context *ctx, struct dict *dnrs)
{
    struct do_not_register *dnr;
    dict_iterator_t it, next;

    for(it = dict_first(dnrs); it; it = next)
    {
        next = iter_next(it);
        dnr = iter_data(it);
        if(dnr->expires && dnr->expires <= now)
        {
            dict_remove(dnrs, iter_key(it));
            continue;
        }
        saxdb_start_record(ctx, dnr->chan_name, 0);
        if(dnr->set)
            saxdb_write_int(ctx, KEY_DNR_SET, dnr->set);
        if(dnr->expires)
            saxdb_write_int(ctx, KEY_EXPIRES, dnr->expires);
        saxdb_write_string(ctx, KEY_DNR_SETTER, dnr->setter);
        saxdb_write_string(ctx, KEY_DNR_REASON, dnr->reason);
        saxdb_end_record(ctx);
    }
}

static int
chanserv_saxdb_write(struct saxdb_context *ctx)
{
    dict_iterator_t it;
    struct chanData *channel;

    /* Notes */
    saxdb_start_record(ctx, KEY_NOTE_TYPES, 1);
    for(it = dict_first(note_types); it; it = iter_next(it))
        chanserv_write_note_type(ctx, iter_data(it));
    saxdb_end_record(ctx);

    /* DNRs */
    saxdb_start_record(ctx, KEY_DNR, 1);
    write_dnrs_helper(ctx, handle_dnrs);
    write_dnrs_helper(ctx, plain_dnrs);
    write_dnrs_helper(ctx, mask_dnrs);
    saxdb_end_record(ctx);

    /* Channels */
    saxdb_start_record(ctx, KEY_CHANNELS, 1);
    for(channel = channelList; channel; channel = channel->next)
        chanserv_write_channel(ctx, channel);
    saxdb_end_record(ctx);

    return 0;
}

static void
chanserv_db_cleanup(void) {
    unsigned int ii;
    unreg_part_func(handle_part);
    while(channelList)
        unregister_channel(channelList, "terminating.");
    for(ii = 0; ii < chanserv_conf.support_channels.used; ++ii)
        UnlockChannel(chanserv_conf.support_channels.list[ii]);
    free(chanserv_conf.support_channels.list);
    dict_delete(handle_dnrs);
    dict_delete(plain_dnrs);
    dict_delete(mask_dnrs);
    dict_delete(note_types);
    free_string_list(chanserv_conf.eightball);
    free_string_list(chanserv_conf.old_ban_names);
    free_string_list(chanserv_conf.set_shows);
    free(set_shows_list.list);
    free(uset_shows_list.list);
    while(helperList)
    {
        struct userData *helper = helperList;
        helperList = helperList->next;
        free(helper);
    }
}

#if defined(GCC_VARMACROS)
# define DEFINE_COMMAND(NAME, MIN_ARGC, FLAGS, ARGS...) modcmd_register(chanserv_module, #NAME, cmd_##NAME, MIN_ARGC, FLAGS, ARGS)
#elif defined(C99_VARMACROS)
# define DEFINE_COMMAND(NAME, MIN_ARGC, FLAGS, ...) modcmd_register(chanserv_module, #NAME, cmd_##NAME, MIN_ARGC, FLAGS, __VA_ARGS__)
#endif
#define DEFINE_CHANNEL_OPTION(NAME) modcmd_register(chanserv_module, "set "#NAME, chan_opt_##NAME, 1, 0, NULL)
#define DEFINE_USER_OPTION(NAME) modcmd_register(chanserv_module, "uset "#NAME, user_opt_##NAME, 1, MODCMD_REQUIRE_REGCHAN, NULL)

void
init_chanserv(const char *nick)
{
    CS_LOG = log_register_type("ChanServ", "file:chanserv.log");
    conf_register_reload(chanserv_conf_read);

    if(nick)
    {
        reg_server_link_func(handle_server_link);
        reg_new_channel_func(handle_new_channel);
        reg_join_func(handle_join);
        reg_part_func(handle_part);
        reg_kick_func(handle_kick);
        reg_topic_func(handle_topic);
        reg_mode_change_func(handle_mode);
        reg_nick_change_func(handle_nick_change);
        reg_auth_func(handle_auth);
    }

    reg_handle_rename_func(handle_rename);
    reg_unreg_func(handle_unreg);

    handle_dnrs = dict_new();
    dict_set_free_data(handle_dnrs, free);
    plain_dnrs = dict_new();
    dict_set_free_data(plain_dnrs, free);
    mask_dnrs = dict_new();
    dict_set_free_data(mask_dnrs, free);

    reg_svccmd_unbind_func(handle_svccmd_unbind);
    chanserv_module = module_register("ChanServ", CS_LOG, "chanserv.help", chanserv_expand_variable);
    DEFINE_COMMAND(register, 1, MODCMD_REQUIRE_AUTHED, "flags", "+acceptchan,+helping", NULL);
    DEFINE_COMMAND(noregister, 1, MODCMD_REQUIRE_AUTHED, "flags", "+helping", NULL);
    DEFINE_COMMAND(allowregister, 2, 0, "template", "noregister", NULL);
    DEFINE_COMMAND(dnrsearch, 3, 0, "template", "noregister", NULL);
    modcmd_register(chanserv_module, "dnrsearch print", NULL, 0, 0, NULL);
    modcmd_register(chanserv_module, "dnrsearch remove", NULL, 0, 0, NULL);
    modcmd_register(chanserv_module, "dnrsearch count", NULL, 0, 0, NULL);
    DEFINE_COMMAND(move, 1, MODCMD_REQUIRE_AUTHED|MODCMD_REQUIRE_REGCHAN, "template", "register", NULL);
    DEFINE_COMMAND(csuspend, 2, MODCMD_REQUIRE_AUTHED|MODCMD_REQUIRE_REGCHAN, "flags", "+helping", NULL);
    DEFINE_COMMAND(cunsuspend, 1, MODCMD_REQUIRE_AUTHED|MODCMD_REQUIRE_REGCHAN, "flags", "+helping", NULL);
    DEFINE_COMMAND(createnote, 5, 0, "level", "800", NULL);
    DEFINE_COMMAND(removenote, 2, 0, "level", "800", NULL);

    DEFINE_COMMAND(unregister, 1, MODCMD_REQUIRE_AUTHED|MODCMD_REQUIRE_REGCHAN, "flags", "+loghostmask", NULL);
    DEFINE_COMMAND(merge, 2, MODCMD_REQUIRE_AUTHED|MODCMD_REQUIRE_REGCHAN, "access", "owner", NULL);

    DEFINE_COMMAND(adduser, 3, MODCMD_REQUIRE_CHANUSER, "access", "master", NULL);
    DEFINE_COMMAND(deluser, 2, MODCMD_REQUIRE_CHANUSER, "access", "master", NULL);
    DEFINE_COMMAND(suspend, 2, MODCMD_REQUIRE_CHANUSER, "access", "master", NULL);
    DEFINE_COMMAND(unsuspend, 2, MODCMD_REQUIRE_CHANUSER, "access", "master", NULL);
    DEFINE_COMMAND(deleteme, 1, MODCMD_REQUIRE_CHANUSER, NULL);

    DEFINE_COMMAND(mdelowner, 2, MODCMD_REQUIRE_CHANUSER, "flags", "+helping", NULL);
    DEFINE_COMMAND(mdelcoowner, 2, MODCMD_REQUIRE_CHANUSER, "access", "owner", NULL);
    DEFINE_COMMAND(mdelmaster, 2, MODCMD_REQUIRE_CHANUSER, "access", "coowner", NULL);
    DEFINE_COMMAND(mdelop, 2, MODCMD_REQUIRE_CHANUSER, "access", "master", NULL);
    DEFINE_COMMAND(mdelpeon, 2, MODCMD_REQUIRE_CHANUSER, "access", "master", NULL);

    DEFINE_COMMAND(trim, 3, MODCMD_REQUIRE_CHANUSER, "access", "master", NULL);
    DEFINE_COMMAND(opchan, 1, MODCMD_REQUIRE_REGCHAN|MODCMD_NEVER_CSUSPEND, "access", "1", NULL);
    DEFINE_COMMAND(clvl, 3, MODCMD_REQUIRE_CHANUSER, "access", "master", NULL);
    DEFINE_COMMAND(giveownership, 2, MODCMD_REQUIRE_CHANUSER, "access", "owner", "flags", "+loghostmask", NULL);

    DEFINE_COMMAND(up, 1, MODCMD_REQUIRE_CHANUSER, NULL);
    DEFINE_COMMAND(down, 1, MODCMD_REQUIRE_REGCHAN, NULL);
    DEFINE_COMMAND(upall, 1, MODCMD_REQUIRE_AUTHED, NULL);
    DEFINE_COMMAND(downall, 1, MODCMD_REQUIRE_AUTHED, NULL);
    DEFINE_COMMAND(op, 2, MODCMD_REQUIRE_CHANNEL, "access", "op", NULL);
    DEFINE_COMMAND(deop, 2, MODCMD_REQUIRE_CHANNEL, "template", "op", NULL);
    DEFINE_COMMAND(voice, 2, MODCMD_REQUIRE_CHANNEL, "template", "op", NULL);
    DEFINE_COMMAND(devoice, 2, MODCMD_REQUIRE_CHANNEL, "template", "op", NULL);

    DEFINE_COMMAND(kickban, 2, MODCMD_REQUIRE_REGCHAN, "template", "op", NULL);
    DEFINE_COMMAND(kick, 2, MODCMD_REQUIRE_REGCHAN, "template", "op", NULL);
    DEFINE_COMMAND(ban, 2, MODCMD_REQUIRE_REGCHAN, "template", "op", NULL);
    DEFINE_COMMAND(unban, 2, 0, "template", "op", NULL);
    DEFINE_COMMAND(unbanall, 1, 0, "template", "op", NULL);
    DEFINE_COMMAND(unbanme, 1, MODCMD_REQUIRE_CHANUSER, "template", "op", NULL);
    DEFINE_COMMAND(open, 1, MODCMD_REQUIRE_CHANUSER, "template", "op", NULL);
    DEFINE_COMMAND(topic, 1, MODCMD_REQUIRE_REGCHAN, "template", "op", "flags", "+never_csuspend", NULL);
    DEFINE_COMMAND(mode, 1, MODCMD_REQUIRE_REGCHAN, "template", "op", NULL);
    DEFINE_COMMAND(inviteme, 1, MODCMD_REQUIRE_CHANNEL, "access", "1", NULL);
    DEFINE_COMMAND(invite, 1, MODCMD_REQUIRE_CHANNEL, "access", "master", NULL);
    DEFINE_COMMAND(set, 1, MODCMD_REQUIRE_CHANUSER, "access", "op", NULL);
    DEFINE_COMMAND(wipeinfo, 2, MODCMD_REQUIRE_CHANUSER, "access", "master", NULL);
    DEFINE_COMMAND(resync, 1, MODCMD_REQUIRE_CHANUSER, "access", "master", NULL);

    DEFINE_COMMAND(events, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog", "access", "350", NULL);
    DEFINE_COMMAND(addban, 2, MODCMD_REQUIRE_REGCHAN, "access", "250", NULL);
    DEFINE_COMMAND(addtimedban, 3, MODCMD_REQUIRE_REGCHAN, "access", "250", NULL);
    DEFINE_COMMAND(delban, 2, MODCMD_REQUIRE_REGCHAN, "access", "250", NULL);
    DEFINE_COMMAND(uset, 1, MODCMD_REQUIRE_CHANUSER, "access", "1", NULL);

    DEFINE_COMMAND(bans, 1, MODCMD_REQUIRE_REGCHAN, "access", "1", "flags", "+nolog", NULL);
    DEFINE_COMMAND(peek, 1, MODCMD_REQUIRE_REGCHAN, "access", "op", "flags", "+nolog", NULL);

    DEFINE_COMMAND(myaccess, 1, MODCMD_REQUIRE_AUTHED, NULL);
    DEFINE_COMMAND(access, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog,+joinable", NULL);
    DEFINE_COMMAND(users, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog,+joinable", NULL);
    DEFINE_COMMAND(wlist, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog,+joinable", NULL);
    DEFINE_COMMAND(clist, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog,+joinable", NULL);
    DEFINE_COMMAND(mlist, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog,+joinable", NULL);
    DEFINE_COMMAND(olist, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog,+joinable", NULL);
    DEFINE_COMMAND(plist, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog,+joinable", NULL);
    DEFINE_COMMAND(info, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog,+joinable", NULL);
    DEFINE_COMMAND(seen, 2, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog,+joinable", NULL);
    DEFINE_COMMAND(names, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+nolog,+joinable", NULL);

    DEFINE_COMMAND(note, 1, MODCMD_REQUIRE_REGCHAN, "flags", "+joinable,+acceptchan", NULL);
    DEFINE_COMMAND(delnote, 2, MODCMD_REQUIRE_CHANUSER, NULL);

    DEFINE_COMMAND(netinfo, 1, 0, "flags", "+nolog", NULL);
    DEFINE_COMMAND(ircops, 1, 0, "flags", "+nolog", NULL);
    DEFINE_COMMAND(helpers, 1, 0, "flags", "+nolog", NULL);
    DEFINE_COMMAND(staff, 1, 0, "flags", "+nolog", NULL);

    DEFINE_COMMAND(say, 2, 0, "flags", "+oper,+acceptchan", NULL);
    DEFINE_COMMAND(emote, 2, 0, "flags", "+oper,+acceptchan", NULL);
    DEFINE_COMMAND(expire, 1, 0, "flags", "+oper", NULL);
    DEFINE_COMMAND(search, 3, 0, "flags", "+nolog,+helping", NULL);
    DEFINE_COMMAND(unvisited, 1, 0, "flags", "+nolog,+helping", NULL);

    DEFINE_COMMAND(unf, 1, 0, "flags", "+nolog,+toy,+acceptchan", NULL);
    DEFINE_COMMAND(ping, 1, 0, "flags", "+nolog,+toy,+acceptchan", NULL);
    DEFINE_COMMAND(wut, 1, 0, "flags", "+nolog,+toy,+acceptchan", NULL);
    DEFINE_COMMAND(8ball, 2, 0, "flags", "+nolog,+toy,+acceptchan", NULL);
    DEFINE_COMMAND(d, 2, 0, "flags", "+nolog,+toy,+acceptchan", NULL);
    DEFINE_COMMAND(huggle, 1, 0, "flags", "+nolog,+toy,+acceptchan", NULL);

    /* Channel options */
    DEFINE_CHANNEL_OPTION(defaulttopic);
    DEFINE_CHANNEL_OPTION(topicmask);
    DEFINE_CHANNEL_OPTION(greeting);
    DEFINE_CHANNEL_OPTION(usergreeting);
    DEFINE_CHANNEL_OPTION(modes);
    DEFINE_CHANNEL_OPTION(enfops);
    DEFINE_CHANNEL_OPTION(giveops);
    DEFINE_CHANNEL_OPTION(protect);
    DEFINE_CHANNEL_OPTION(enfmodes);
    DEFINE_CHANNEL_OPTION(enftopic);
    DEFINE_CHANNEL_OPTION(pubcmd);
    DEFINE_CHANNEL_OPTION(givevoice);
    DEFINE_CHANNEL_OPTION(userinfo);
    DEFINE_CHANNEL_OPTION(dynlimit);
    DEFINE_CHANNEL_OPTION(topicsnarf);
    DEFINE_CHANNEL_OPTION(nodelete);
    DEFINE_CHANNEL_OPTION(toys);
    DEFINE_CHANNEL_OPTION(setters);
    DEFINE_CHANNEL_OPTION(topicrefresh);
    DEFINE_CHANNEL_OPTION(ctcpusers);
    DEFINE_CHANNEL_OPTION(ctcpreaction);
    DEFINE_CHANNEL_OPTION(inviteme);
    DEFINE_CHANNEL_OPTION(unreviewed);
    modcmd_register(chanserv_module, "set unreviewed on", NULL, 0, 0, "flags", "+helping", NULL);
    modcmd_register(chanserv_module, "set unreviewed off", NULL, 0, 0, "flags", "+oper", NULL);
    if(off_channel > 1)
        DEFINE_CHANNEL_OPTION(offchannel);
    modcmd_register(chanserv_module, "set defaults", chan_opt_defaults, 1, 0, "access", "owner", NULL);

    /* Alias set topic to set defaulttopic for compatibility. */
    modcmd_register(chanserv_module, "set topic", chan_opt_defaulttopic, 1, 0, NULL);

    /* User options */
    DEFINE_USER_OPTION(noautoop);
    DEFINE_USER_OPTION(autoinvite);
    DEFINE_USER_OPTION(info);

    /* Alias uset autovoice to uset autoop. */
    modcmd_register(chanserv_module, "uset noautovoice", user_opt_noautoop, 1, 0, NULL);

    note_types = dict_new();
    dict_set_free_data(note_types, chanserv_deref_note_type);
    if(nick)
    {
        const char *modes = conf_get_data("services/chanserv/modes", RECDB_QSTRING);
        chanserv = AddLocalUser(nick, nick, NULL, "Channel Services", modes);
        service_register(chanserv)->trigger = '!';
        reg_chanmsg_func('\001', chanserv, chanserv_ctcp_check);
    }
    saxdb_register("ChanServ", chanserv_saxdb_read, chanserv_saxdb_write);

    if(chanserv_conf.channel_expire_frequency)
        timeq_add(now + chanserv_conf.channel_expire_frequency, expire_channels, NULL);

    if(chanserv_conf.dnr_expire_frequency)
        timeq_add(now + chanserv_conf.dnr_expire_frequency, expire_dnrs, NULL);

    if(chanserv_conf.refresh_period)
    {
        unsigned long next_refresh;
        next_refresh = (now + chanserv_conf.refresh_period - 1) / chanserv_conf.refresh_period * chanserv_conf.refresh_period;
        timeq_add(next_refresh, chanserv_refresh_topics, NULL);
    }

    reg_exit_func(chanserv_db_cleanup);
    message_register_table(msgtab);
}

/* nickserv.c - Nick/authentication service
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

#include "chanserv.h"
#include "conf.h"
#include "global.h"
#include "modcmd.h"
#include "opserv.h" /* for gag_create(), opserv_bad_channel() */
#include "saxdb.h"
#include "sendmail.h"
#include "timeq.h"

#ifdef HAVE_REGEX_H
#include <regex.h>
#endif

#define NICKSERV_CONF_NAME "services/nickserv"

#define KEY_DISABLE_NICKS "disable_nicks"
#define KEY_DEFAULT_HOSTMASK "default_hostmask"
#define KEY_NICKS_PER_HANDLE "nicks_per_handle"
#define KEY_NICKS_PER_ACCOUNT "nicks_per_account"
#define KEY_PASSWORD_MIN_LENGTH "password_min_length"
#define KEY_PASSWORD_MIN_DIGITS "password_min_digits"
#define KEY_PASSWORD_MIN_UPPER "password_min_upper"
#define KEY_PASSWORD_MIN_LOWER "password_min_lower"
#define KEY_VALID_HANDLE_REGEX "valid_handle_regex"
#define KEY_VALID_ACCOUNT_REGEX "valid_account_regex"
#define KEY_VALID_NICK_REGEX "valid_nick_regex"
#define KEY_DB_BACKUP_FREQ "db_backup_freq"
#define KEY_MODOPER_LEVEL "modoper_level"
#define KEY_SET_EPITHET_LEVEL "set_epithet_level"
#define KEY_SET_TITLE_LEVEL "set_title_level"
#define KEY_SET_FAKEHOST_LEVEL "set_fakehost_level"
#define KEY_TITLEHOST_SUFFIX "titlehost_suffix"
#define KEY_FLAG_LEVELS "flag_levels"
#define KEY_HANDLE_EXPIRE_FREQ	"handle_expire_freq"
#define KEY_ACCOUNT_EXPIRE_FREQ "account_expire_freq"
#define KEY_HANDLE_EXPIRE_DELAY	"handle_expire_delay"
#define KEY_ACCOUNT_EXPIRE_DELAY "account_expire_delay"
#define KEY_NOCHAN_HANDLE_EXPIRE_DELAY "nochan_handle_expire_delay"
#define KEY_NOCHAN_ACCOUNT_EXPIRE_DELAY "nochan_account_expire_delay"
#define KEY_DICT_FILE "dict_file"
#define KEY_NICK "nick"
#define KEY_LANGUAGE "language"
#define KEY_AUTOGAG_ENABLED "autogag_enabled"
#define KEY_AUTOGAG_DURATION "autogag_duration"
#define KEY_AUTH_POLICER "auth_policer"
#define KEY_EMAIL_VISIBLE_LEVEL "email_visible_level"
#define KEY_EMAIL_ENABLED "email_enabled"
#define KEY_EMAIL_REQUIRED "email_required"
#define KEY_COOKIE_TIMEOUT "cookie_timeout"
#define KEY_ACCOUNTS_PER_EMAIL "accounts_per_email"
#define KEY_EMAIL_SEARCH_LEVEL "email_search_level"

#define KEY_ID "id"
#define KEY_PASSWD "passwd"
#define KEY_NICKS "nicks"
#define KEY_MASKS "masks"
#define KEY_OPSERV_LEVEL "opserv_level"
#define KEY_FLAGS "flags"
#define KEY_REGISTER_ON "register"
#define KEY_LAST_SEEN "lastseen"
#define KEY_INFO "info"
#define KEY_USERLIST_STYLE "user_style"
#define KEY_SCREEN_WIDTH "screen_width"
#define KEY_LAST_AUTHED_HOST "last_authed_host"
#define KEY_LAST_QUIT_HOST "last_quit_host"
#define KEY_EMAIL_ADDR "email_addr"
#define KEY_COOKIE "cookie"
#define KEY_COOKIE_DATA "data"
#define KEY_COOKIE_TYPE "type"
#define KEY_COOKIE_EXPIRES "expires"
#define KEY_ACTIVATION "activation"
#define KEY_PASSWORD_CHANGE "password change"
#define KEY_EMAIL_CHANGE "email change"
#define KEY_ALLOWAUTH "allowauth"
#define KEY_EPITHET "epithet"
#define KEY_TABLE_WIDTH "table_width"
#define KEY_ANNOUNCEMENTS "announcements"
#define KEY_MAXLOGINS "maxlogins"
#define KEY_FAKEHOST "fakehost"

#define NICKSERV_VALID_CHARS	"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_"

#define NICKSERV_FUNC(NAME) MODCMD_FUNC(NAME)
#define OPTION_FUNC(NAME) int NAME(struct userNode *user, struct handle_info *hi, UNUSED_ARG(unsigned int override), unsigned int argc, char *argv[])
typedef OPTION_FUNC(option_func_t);

DEFINE_LIST(handle_info_list, struct handle_info*);

#define NICKSERV_MIN_PARMS(N) do { \
  if (argc < N) { \
    reply("MSG_MISSING_PARAMS", argv[0]); \
    svccmd_send_help(user, nickserv, cmd); \
    return 0; \
  } } while (0)

struct userNode *nickserv;
struct userList curr_helpers;
const char *handle_flags = HANDLE_FLAGS;

static struct module *nickserv_module;
static struct service *nickserv_service;
static struct log_type *NS_LOG;
static dict_t nickserv_handle_dict; /* contains struct handle_info* */
static dict_t nickserv_id_dict; /* contains struct handle_info* */
static dict_t nickserv_nick_dict; /* contains struct nick_info* */
static dict_t nickserv_opt_dict; /* contains option_func_t* */
static dict_t nickserv_allow_auth_dict; /* contains struct handle_info* */
static dict_t nickserv_email_dict; /* contains struct handle_info_list*, indexed by email addr */
static char handle_inverse_flags[256];
static unsigned int flag_access_levels[32];
static const struct message_entry msgtab[] = {
    { "NSMSG_HANDLE_EXISTS", "Account $b%s$b is already registered." },
    { "NSMSG_PASSWORD_SHORT", "Your password must be at least %lu characters long." },
    { "NSMSG_PASSWORD_ACCOUNT", "Your password may not be the same as your account name." },
    { "NSMSG_PASSWORD_DICTIONARY", "Your password should not be the word \"password\", or any other dictionary word." },
    { "NSMSG_PASSWORD_READABLE", "Your password must have at least %lu digit(s), %lu capital letter(s), and %lu lower-case letter(s)." },
    { "NSMSG_PARTIAL_REGISTER", "Account has been registered to you; nick was already registered to someone else." },
    { "NSMSG_OREGISTER_VICTIM", "%s has registered a new account for you (named %s)." },
    { "NSMSG_OREGISTER_H_SUCCESS", "Account has been registered." },
    { "NSMSG_REGISTER_H_SUCCESS", "Account has been registered to you." },
    { "NSMSG_REGISTER_HN_SUCCESS", "Account and nick have been registered to you." },
    { "NSMSG_REQUIRE_OPER", "You must be an $bIRC Operator$b to register the first account." },
    { "NSMSG_ROOT_HANDLE", "Account %s has been granted $broot-level privileges$b." },
    { "NSMSG_USE_COOKIE_REGISTER", "To activate your account, you must check your email for the \"cookie\" that has been mailed to it.  When you have it, use the $bcookie$b command to complete registration." },
    { "NSMSG_USE_COOKIE_RESETPASS", "A cookie has been mailed to your account's email address.  You must check your email and use the $bcookie$b command to confirm the password change." },
    { "NSMSG_USE_COOKIE_EMAIL_1", "A cookie has been mailed to the new address you requested.  To finish setting your email address, please check your email for the cookie and use the $bcookie$b command to verify." },
    { "NSMSG_USE_COOKIE_EMAIL_2", "A cookie has been generated, and half mailed to each your old and new addresses.  To finish changing your email address, please check your email for the cookie and use the $bcookie$b command to verify." },
    { "NSMSG_USE_COOKIE_AUTH", "A cookie has been generated and sent to your email address.  Once you have checked your email and received the cookie, auth using the $bcookie$b command." },
    { "NSMSG_COOKIE_LIVE", "Account $b%s$b already has a cookie active.  Please either finish using that cookie, wait for it to expire, or auth to the account and use the $bdelcookie$b command." },
    { "NSMSG_EMAIL_UNACTIVATED", "That email address already has an unused cookie outstanding.  Please use the cookie or wait for it to expire." },
    { "NSMSG_NO_COOKIE", "Your account does not have any cookie issued right now." },
    { "NSMSG_CANNOT_COOKIE", "You cannot use that kind of cookie when you are logged in." },
    { "NSMSG_BAD_COOKIE", "That cookie is not the right one.  Please make sure you are copying it EXACTLY from the email; it is case-sensitive, so $bABC$b is different from $babc$b." },
    { "NSMSG_HANDLE_ACTIVATED", "Your account is now activated (with the password you entered when you registered).  You are now authenticated to your account." },
    { "NSMSG_PASSWORD_CHANGED", "You have successfully changed your password to what you requested with the $bresetpass$b command." },
    { "NSMSG_EMAIL_PROHIBITED", "%s may not be used as an email address: %s" },
    { "NSMSG_EMAIL_OVERUSED", "There are already the maximum number of accounts associated with that email address." },
    { "NSMSG_EMAIL_SAME", "That is the email address already there; no need to change it." },
    { "NSMSG_EMAIL_CHANGED", "You have successfully changed your email address." },
    { "NSMSG_BAD_COOKIE_TYPE", "Your account had bad cookie type %d; sorry.  I am confused.  Please report this bug." },
    { "NSMSG_MUST_TIME_OUT", "You must wait for cookies of that type to time out." },
    { "NSMSG_ATE_COOKIE", "I ate the cookie for your account.  You may now have another." },
    { "NSMSG_USE_RENAME", "You are already authenticated to account $b%s$b -- contact the support staff to rename your account." },
    { "NSMSG_ALREADY_REGISTERING", "You have already used $bREGISTER$b once this session; you may not use it again." },
    { "NSMSG_REGISTER_BAD_NICKMASK", "Could not recognize $b%s$b as either a current nick or a hostmask." },
    { "NSMSG_NICK_NOT_REGISTERED", "Nick $b%s$b has not been registered to any account." },
    { "NSMSG_HANDLE_NOT_FOUND", "Could not find your account -- did you register yet?" },
    { "NSMSG_ALREADY_AUTHED", "You are already authed to account $b%s$b; you must reconnect to auth to a different account." },
    { "NSMSG_USE_AUTHCOOKIE", "Your hostmask is not valid for account $b%1$s$b.  Please use the $bauthcookie$b command to grant yourself access.  (/msg $S authcookie %1$s)" },
    { "NSMSG_HOSTMASK_INVALID", "Your hostmask is not valid for account $b%s$b." },
    { "NSMSG_USER_IS_SERVICE", "$b%s$b is a network service; you can only use that command on real users." },
    { "NSMSG_USER_PREV_AUTH", "$b%s$b is already authenticated." },
    { "NSMSG_USER_PREV_STAMP", "$b%s$b has authenticated to an account once and cannot authenticate again." },
    { "NSMSG_BAD_MAX_LOGINS", "MaxLogins must be at most %d." },
    { "NSMSG_LANGUAGE_NOT_FOUND", "Language $b%s$b is not supported; $b%s$b was the closest available match." },
    { "NSMSG_MAX_LOGINS", "Your account already has its limit of %d user(s) logged in." },
    { "NSMSG_STAMPED_REGISTER", "You have already authenticated to an account once this session; you may not register a new account." },
    { "NSMSG_STAMPED_AUTH", "You have already authenticated to an account once this session; you may not authenticate to another." },
    { "NSMSG_STAMPED_RESETPASS", "You have already authenticated to an account once this session; you may not reset your password to authenticate again." },
    { "NSMSG_STAMPED_AUTHCOOKIE",  "You have already authenticated to an account once this session; you may not use a cookie to authenticate to another account." },
    { "NSMSG_TITLE_INVALID", "Titles cannot contain any dots; please choose another." },
    { "NSMSG_TITLE_TRUNCATED", "That title combined with the user's account name would result in a truncated host; please choose a shorter title." },
    { "NSMSG_FAKEHOST_INVALID", "Fake hosts must be shorter than %d characters and cannot start with a dot." },
    { "NSMSG_HANDLEINFO_ON", "Account information for $b%s$b:" },
    { "NSMSG_HANDLEINFO_ID", "  Account ID: %lu" },
    { "NSMSG_HANDLEINFO_REGGED", "  Registered on: %s" },
    { "NSMSG_HANDLEINFO_LASTSEEN", "  Last seen: %s" },
    { "NSMSG_HANDLEINFO_LASTSEEN_NOW", "  Last seen: Right now!" },
    { "NSMSG_HANDLEINFO_VACATION", "  On vacation." },
    { "NSMSG_HANDLEINFO_EMAIL_ADDR", "  Email address: %s" },
    { "NSMSG_HANDLEINFO_COOKIE_ACTIVATION", "  Cookie: There is currently an activation cookie issued for this account" },
    { "NSMSG_HANDLEINFO_COOKIE_PASSWORD", "  Cookie: There is currently a password change cookie issued for this account" },
    { "NSMSG_HANDLEINFO_COOKIE_EMAIL", "  Cookie: There is currently an email change cookie issued for this account" },
    { "NSMSG_HANDLEINFO_COOKIE_ALLOWAUTH", "  Cookie: There is currently an allowauth cookie issued for this account" },
    { "NSMSG_HANDLEINFO_COOKIE_UNKNOWN", "  Cookie: There is currently an unknown cookie issued for this account" },
    { "NSMSG_HANDLEINFO_INFOLINE", "  Infoline: %s" },
    { "NSMSG_HANDLEINFO_FLAGS", "  Flags: %s" },
    { "NSMSG_HANDLEINFO_EPITHET", "  Epithet: %s" },
    { "NSMSG_HANDLEINFO_FAKEHOST", "  Fake host: %s" },
    { "NSMSG_HANDLEINFO_LAST_HOST", "  Last quit hostmask: %s" },
    { "NSMSG_HANDLEINFO_LAST_HOST_UNKNOWN", "  Last quit hostmask: Unknown" },
    { "NSMSG_HANDLEINFO_NICKS", "  Nickname(s): %s" },
    { "NSMSG_HANDLEINFO_MASKS", "  Hostmask(s): %s" },
    { "NSMSG_HANDLEINFO_CHANNELS", "  Channel(s): %s" },
    { "NSMSG_HANDLEINFO_CURRENT", "  Current nickname(s): %s" },
    { "NSMSG_HANDLEINFO_DNR", "  Do-not-register (by %s): %s" },
    { "NSMSG_USERINFO_AUTHED_AS", "$b%s$b is authenticated to account $b%s$b." },
    { "NSMSG_USERINFO_NOT_AUTHED", "$b%s$b is not authenticated to any account." },
    { "NSMSG_NICKINFO_OWNER", "Nick $b%s$b is owned by account $b%s$b." },
    { "NSMSG_PASSWORD_INVALID", "Incorrect password; please try again." },
    { "NSMSG_PLEASE_SET_EMAIL", "We now require email addresses for users.  Please use the $bset email$b command to set your email address!" },
    { "NSMSG_WEAK_PASSWORD", "WARNING: You are using a password that is considered weak (easy to guess).  It is STRONGLY recommended you change it (now, if not sooner) by typing \"/msg $S@$s PASS oldpass newpass\" (with your current password and a new password)." },
    { "NSMSG_HANDLE_SUSPENDED", "Your $b$N$b account has been suspended; you may not use it." },
    { "NSMSG_AUTH_SUCCESS", "I recognize you." },
    { "NSMSG_ALLOWAUTH_STAFF", "$b%s$b is a helper or oper; please use $bstaff$b after the account name to allowauth." },
    { "NSMSG_AUTH_ALLOWED", "User $b%s$b may now authenticate to account $b%s$b." },
    { "NSMSG_AUTH_ALLOWED_MSG", "You may now authenticate to account $b%s$b by typing $b/msg $N@$s auth %s password$b (using your password).  If you will be using this computer regularly, please type $b/msg $N addmask$b (AFTER you auth) to permanently add your hostmask." },
    { "NSMSG_AUTH_ALLOWED_EMAIL", "You may also (after you auth) type $b/msg $N set email user@your.isp$b to set an email address.  This will let you use the $bauthcookie$b command to be authenticated in the future." },
    { "NSMSG_AUTH_NORMAL_ONLY", "User $b%s$b may now only authenticate to accounts with matching hostmasks." },
    { "NSMSG_AUTH_UNSPECIAL", "User $b%s$b did not have any special auth allowance." },
    { "NSMSG_MUST_AUTH", "You must be authenticated first." },
    { "NSMSG_TOO_MANY_NICKS", "You have already registered the maximum permitted number of nicks." },
    { "NSMSG_NICK_EXISTS", "Nick $b%s$b already registered." },
    { "NSMSG_REGNICK_SUCCESS", "Nick $b%s$b has been registered to you." },
    { "NSMSG_OREGNICK_SUCCESS", "Nick $b%s$b has been registered to account $b%s$b." },
    { "NSMSG_PASS_SUCCESS", "Password changed." },
    { "NSMSG_MASK_INVALID", "$b%s$b is an invalid hostmask." },
    { "NSMSG_ADDMASK_ALREADY", "$b%s$b is already a hostmask in your account." },
    { "NSMSG_ADDMASK_SUCCESS", "Hostmask %s added." },
    { "NSMSG_DELMASK_NOTLAST", "You may not delete your last hostmask." },
    { "NSMSG_DELMASK_SUCCESS", "Hostmask %s deleted." },
    { "NSMSG_DELMASK_NOT_FOUND", "Unable to find mask to be deleted." },
    { "NSMSG_OPSERV_LEVEL_BAD", "You may not promote another oper above your level." },
    { "NSMSG_USE_CMD_PASS", "Please use the PASS command to change your password." },
    { "NSMSG_UNKNOWN_NICK", "I know nothing about nick $b%s$b." },
    { "NSMSG_NOT_YOUR_NICK", "The nick $b%s$b is not registered to you." },
    { "NSMSG_NICK_USER_YOU", "I will not let you kill yourself." },
    { "NSMSG_UNREGNICK_SUCCESS", "Nick $b%s$b has been unregistered." },
    { "NSMSG_UNREGISTER_SUCCESS", "Account $b%s$b has been unregistered." },
    { "NSMSG_UNREGISTER_NICKS_SUCCESS", "Account $b%s$b and all its nicks have been unregistered." },
    { "NSMSG_HANDLE_STATS", "There are %d nicks registered to your account." },
    { "NSMSG_HANDLE_NONE", "You are not authenticated against any account." },
    { "NSMSG_GLOBAL_STATS", "There are %d accounts and %d nicks registered globally." },
    { "NSMSG_GLOBAL_STATS_NONICK", "There are %d accounts registered." },
    { "NSMSG_CANNOT_GHOST_SELF", "You may not ghost-kill yourself." },
    { "NSMSG_CANNOT_GHOST_USER", "$b%s$b is not authed to your account; you may not ghost-kill them." },
    { "NSMSG_GHOST_KILLED", "$b%s$b has been killed as a ghost." },
    { "NSMSG_ON_VACATION", "You are now on vacation.  Your account will be preserved until you authenticate again." },
    { "NSMSG_NO_ACCESS", "Access denied." },
    { "NSMSG_INVALID_FLAG", "$b%c$b is not a valid $N account flag." },
    { "NSMSG_SET_FLAG", "Applied flags $b%s$b to %s's $N account." },
    { "NSMSG_FLAG_PRIVILEGED", "You have insufficient access to set flag %c." },
    { "NSMSG_DB_UNREADABLE", "Unable to read database file %s; check the log for more information." },
    { "NSMSG_DB_MERGED", "$N merged DB from %s (in "FMT_TIME_T".%03lu seconds)." },
    { "NSMSG_HANDLE_CHANGED", "$b%s$b's account name has been changed to $b%s$b." },
    { "NSMSG_BAD_HANDLE", "Account $b%s$b not registered because it is in use by a network service, is too long, or contains invalid characters." },
    { "NSMSG_BAD_NICK", "Nickname $b%s$b not registered because it is in use by a network service, is too long, or contains invalid characters." },
    { "NSMSG_BAD_EMAIL_ADDR", "Please use a well-formed email address." },
    { "NSMSG_FAIL_RENAME", "Account $b%s$b not renamed to $b%s$b because it is in use by a network services, or contains invalid characters." },
    { "NSMSG_ACCOUNT_SEARCH_RESULTS", "The following accounts were found:" },
    { "NSMSG_SEARCH_MATCH", "Match: %s" },
    { "NSMSG_INVALID_ACTION", "%s is an invalid search action." },
    { "NSMSG_CANNOT_MERGE_SELF", "You cannot merge account $b%s$b with itself." },
    { "NSMSG_HANDLES_MERGED", "Merged account $b%s$b into $b%s$b." },
    { "NSMSG_RECLAIM_WARN", "%s is a registered nick - you must auth to account %s or change your nick." },
    { "NSMSG_RECLAIM_KILL", "Unauthenticated user of nick." },
    { "NSMSG_RECLAIMED_NONE", "You cannot manually reclaim a nick." },
    { "NSMSG_RECLAIMED_WARN", "Sent a request for %s to change their nick." },
    { "NSMSG_RECLAIMED_SVSNICK", "Forcibly changed %s's nick." },
    { "NSMSG_RECLAIMED_KILL",  "Disconnected %s from the network." },
    { "NSMSG_CLONE_AUTH", "Warning: %s (%s@%s) authed to your account." },
    { "NSMSG_SETTING_LIST", "$b$N account settings:$b" },
    { "NSMSG_INVALID_OPTION", "$b%s$b is an invalid account setting." },
    { "NSMSG_INVALID_ANNOUNCE", "$b%s$b is an invalid announcements value." },
    { "NSMSG_SET_INFO", "$bINFO:         $b%s" },
    { "NSMSG_SET_WIDTH", "$bWIDTH:        $b%d" },
    { "NSMSG_SET_TABLEWIDTH", "$bTABLEWIDTH:   $b%d" },
    { "NSMSG_SET_COLOR", "$bCOLOR:        $b%s" },
    { "NSMSG_SET_PRIVMSG", "$bPRIVMSG:      $b%s" },
    { "NSMSG_SET_STYLE", "$bSTYLE:        $b%s" },
    { "NSMSG_SET_ANNOUNCEMENTS", "$bANNOUNCEMENTS: $b%s" },
    { "NSMSG_SET_PASSWORD", "$bPASSWORD:     $b%s" },
    { "NSMSG_SET_FLAGS", "$bFLAGS:        $b%s" },
    { "NSMSG_SET_EMAIL", "$bEMAIL:        $b%s" },
    { "NSMSG_SET_MAXLOGINS", "$bMAXLOGINS:    $b%d" },
    { "NSMSG_SET_LANGUAGE", "$bLANGUAGE:     $b%s" },
    { "NSMSG_SET_LEVEL", "$bLEVEL:        $b%d" },
    { "NSMSG_SET_EPITHET", "$bEPITHET:      $b%s" },
    { "NSMSG_SET_TITLE", "$bTITLE:        $b%s" },
    { "NSMSG_SET_FAKEHOST", "$bFAKEHOST:    $b%s" },
    { "NSEMAIL_ACTIVATION_SUBJECT", "Account verification for %s" },
    { "NSEMAIL_ACTIVATION_BODY", "This email has been sent to verify that this email address belongs to the person who tried to register an account on %1$s.  Your cookie is:\n    %2$s\nTo verify your email address and complete the account registration, log on to %1$s and type the following command:\n    /msg %3$s@%4$s COOKIE %5$s %2$s\nThis command is only used once to complete your account registration, and never again. Once you have run this command, you will need to authenticate everytime you reconnect to the network. To do this, you will have to type this command every time you reconnect:\n    /msg %3$s@%4$s AUTH %5$s your-password\n Please remember to fill in 'your-password' with the actual password you gave to us when you registered.\n\nIf you did NOT request this account, you do not need to do anything.  Please contact the %1$s staff if you have questions, and be sure to check our website." },
    { "NSEMAIL_PASSWORD_CHANGE_SUBJECT", "Password change verification on %s" },
    { "NSEMAIL_PASSWORD_CHANGE_BODY", "This email has been sent to verify that you wish to change the password on your account %5$s.  Your cookie is %2$s.\nTo complete the password change, log on to %1$s and type the following command:\n    /msg %3$s@%4$s COOKIE %5$s %2$s\nIf you did NOT request your password to be changed, you do not need to do anything.  Please contact the %1$s staff if you have questions." },
    { "NSEMAIL_EMAIL_CHANGE_SUBJECT", "Email address change verification for %s" },
    { "NSEMAIL_EMAIL_CHANGE_BODY_NEW", "This email has been sent to verify that your email address belongs to the same person as account %5$s on %1$s.  The SECOND HALF of your cookie is %2$.*6$s.\nTo verify your address as associated with this account, log on to %1$s and type the following command:\n    /msg %3$s@%4$s COOKIE %5$s ?????%2$.*6$s\n(Replace the ????? with the FIRST HALF of the cookie, as sent to your OLD email address.)\nIf you did NOT request this email address to be associated with this account, you do not need to do anything.  Please contact the %1$s staff if you have questions." },
    { "NSEMAIL_EMAIL_CHANGE_BODY_OLD", "This email has been sent to verify that you want to change your email for account %5$s on %1$s from this address to %7$s.  The FIRST HALF of your cookie is %2$.*6$s\nTo verify your new address as associated with this account, log on to %1$s and type the following command:\n    /msg %3$s@%4$s COOKIE %5$s %2$.*6$s?????\n(Replace the ????? with the SECOND HALF of the cookie, as sent to your NEW email address.)\nIf you did NOT request this change of email address, you do not need to do anything.  Please contact the %1$s staff if you have questions." },
    { "NSEMAIL_EMAIL_VERIFY_SUBJECT", "Email address verification for %s" },
    { "NSEMAIL_EMAIL_VERIFY_BODY", "This email has been sent to verify that this address belongs to the same person as %5$s on %1$s.  Your cookie is %2$s.\nTo verify your address as associated with this account, log on to %1$s and type the following command:\n    /msg %3$s@%4$s COOKIE %5$s %2$s\nIf you did NOT request this email address to be associated with this account, you do not need to do anything.  Please contact the %1$s staff if you have questions." },
    { "NSEMAIL_ALLOWAUTH_SUBJECT", "Authentication allowed for %s" },
    { "NSEMAIL_ALLOWAUTH_BODY", "This email has been sent to let you authenticate (auth) to account %5$s on %1$s.  Your cookie is %2$s.\nTo auth to that account, log on to %1$s and type the following command:\n    /msg %3$s@%4$s COOKIE %5$s %2$s\nIf you did NOT request this authorization, you do not need to do anything.  Please contact the %1$s staff if you have questions." },
    { "CHECKPASS_YES", "Yes." },
    { "CHECKPASS_NO", "No." },
    { NULL, NULL }
};

enum reclaim_action {
    RECLAIM_NONE,
    RECLAIM_WARN,
    RECLAIM_SVSNICK,
    RECLAIM_KILL
};
static void nickserv_reclaim(struct userNode *user, struct nick_info *ni, enum reclaim_action action);
static void nickserv_reclaim_p(void *data);

static struct {
    unsigned int disable_nicks : 1;
    unsigned int valid_handle_regex_set : 1;
    unsigned int valid_nick_regex_set : 1;
    unsigned int autogag_enabled : 1;
    unsigned int email_enabled : 1;
    unsigned int email_required : 1;
    unsigned int default_hostmask : 1;
    unsigned int warn_nick_owned : 1;
    unsigned int warn_clone_auth : 1;
    unsigned long nicks_per_handle;
    unsigned long password_min_length;
    unsigned long password_min_digits;
    unsigned long password_min_upper;
    unsigned long password_min_lower;
    unsigned long db_backup_frequency;
    unsigned long handle_expire_frequency;
    unsigned long autogag_duration;
    unsigned long email_visible_level;
    unsigned long cookie_timeout;
    unsigned long handle_expire_delay;
    unsigned long nochan_handle_expire_delay;
    unsigned long modoper_level;
    unsigned long set_epithet_level;
    unsigned long set_title_level;
    unsigned long set_fakehost_level;
    unsigned long handles_per_email;
    unsigned long email_search_level;
    const char *network_name;
    const char *titlehost_suffix;
    regex_t valid_handle_regex;
    regex_t valid_nick_regex;
    dict_t weak_password_dict;
    struct policer_params *auth_policer_params;
    enum reclaim_action reclaim_action;
    enum reclaim_action auto_reclaim_action;
    unsigned long auto_reclaim_delay;
    unsigned char default_maxlogins;
    unsigned char hard_maxlogins;
} nickserv_conf;

/* We have 2^32 unique account IDs to use. */
unsigned long int highest_id = 0;

static char *
canonicalize_hostmask(char *mask)
{
    char *out = mask, *temp;
    if ((temp = strchr(mask, '!'))) {
	temp++;
	while (*temp) *out++ = *temp++;
	*out++ = 0;
    }
    return mask;
}

static struct handle_info *
register_handle(const char *handle, const char *passwd, UNUSED_ARG(unsigned long id))
{
    struct handle_info *hi;

#ifdef WITH_PROTOCOL_BAHAMUT
    char id_base64[IDLEN + 1];
    do
    {
        /* Assign a unique account ID to the account; note that 0 is
           an invalid account ID. 1 is therefore the first account ID. */
        if (!id) {
            id = 1 + highest_id++;
        } else {
            /* Note: highest_id is and must always be the highest ID. */
            if(id > highest_id) {
                highest_id = id;
            }
        }
        inttobase64(id_base64, id, IDLEN);

        /* Make sure an account with the same ID doesn't exist. If a
           duplicate is found, log some details and assign a new one.
           This should be impossible, but it never hurts to expect it. */
        if ((hi = dict_find(nickserv_id_dict, id_base64, NULL))) {
            log_module(NS_LOG, LOG_WARNING, "Duplicated account ID %lu (%s) found belonging to %s while inserting %s.", id, id_base64, hi->handle, handle);
            id = 0;
        }
    } while(!id);
#endif

    hi = calloc(1, sizeof(*hi));
    hi->userlist_style = HI_DEFAULT_STYLE;
    hi->announcements = '?';
    hi->handle = strdup(handle);
    safestrncpy(hi->passwd, passwd, sizeof(hi->passwd));
    hi->infoline = NULL;
    dict_insert(nickserv_handle_dict, hi->handle, hi);

#ifdef WITH_PROTOCOL_BAHAMUT
    hi->id = id;
    dict_insert(nickserv_id_dict, strdup(id_base64), hi);
#endif

    return hi;
}

static void
register_nick(const char *nick, struct handle_info *owner)
{
    struct nick_info *ni;
    ni = malloc(sizeof(struct nick_info));
    safestrncpy(ni->nick, nick, sizeof(ni->nick));
    ni->owner = owner;
    ni->next = owner->nicks;
    owner->nicks = ni;
    dict_insert(nickserv_nick_dict, ni->nick, ni);
}

static void
free_nick_info(void *vni)
{
    struct nick_info *ni = vni;
    free(ni);
}

static void
delete_nick(struct nick_info *ni)
{
    struct nick_info *last, *next;
    struct userNode *user;
    /* Check to see if we should mark a user as unregistered. */
    if ((user = GetUserH(ni->nick)) && IsReggedNick(user)) {
        user->modes &= ~FLAGS_REGNICK;
        irc_regnick(user);
    }
    /* Remove ni from the nick_info linked list. */
    if (ni == ni->owner->nicks) {
	ni->owner->nicks = ni->next;
    } else {
	last = ni->owner->nicks;
	next = last->next;
	while (next != ni) {
	    last = next;
	    next = last->next;
	}
	last->next = next->next;
    }
    dict_remove(nickserv_nick_dict, ni->nick);
}

static unreg_func_t *unreg_func_list;
static unsigned int unreg_func_size = 0, unreg_func_used = 0;

void
reg_unreg_func(unreg_func_t func)
{
    if (unreg_func_used == unreg_func_size) {
	if (unreg_func_size) {
	    unreg_func_size <<= 1;
	    unreg_func_list = realloc(unreg_func_list, unreg_func_size*sizeof(unreg_func_t));
	} else {
	    unreg_func_size = 8;
	    unreg_func_list = malloc(unreg_func_size*sizeof(unreg_func_t));
	}
    }
    unreg_func_list[unreg_func_used++] = func;
}

static void
nickserv_free_cookie(void *data)
{
    struct handle_cookie *cookie = data;
    if (cookie->hi) cookie->hi->cookie = NULL;
    if (cookie->data) free(cookie->data);
    free(cookie);
}

static void
free_handle_info(void *vhi)
{
    struct handle_info *hi = vhi;

#ifdef WITH_PROTOCOL_BAHAMUT
    char id[IDLEN + 1];

    inttobase64(id, hi->id, IDLEN);
    dict_remove(nickserv_id_dict, id);
#endif

    free_string_list(hi->masks);
    assert(!hi->users);

    while (hi->nicks)
        delete_nick(hi->nicks);
    free(hi->infoline);
    free(hi->epithet);
    free(hi->fakehost);
    if (hi->cookie) {
        timeq_del(hi->cookie->expires, nickserv_free_cookie, hi->cookie, 0);
        nickserv_free_cookie(hi->cookie);
    }
    if (hi->email_addr) {
        struct handle_info_list *hil = dict_find(nickserv_email_dict, hi->email_addr, NULL);
        handle_info_list_remove(hil, hi);
        if (!hil->used)
            dict_remove(nickserv_email_dict, hi->email_addr);
    }
    free(hi);
}

static void set_user_handle_info(struct userNode *user, struct handle_info *hi, int stamp);

static void
nickserv_unregister_handle(struct handle_info *hi, struct userNode *notify)
{
    unsigned int n;

    for (n=0; n<unreg_func_used; n++)
        unreg_func_list[n](notify, hi);
    while (hi->users)
        set_user_handle_info(hi->users, NULL, 0);
    if (notify) {
        if (nickserv_conf.disable_nicks)
            send_message(notify, nickserv, "NSMSG_UNREGISTER_SUCCESS", hi->handle);
        else
            send_message(notify, nickserv, "NSMSG_UNREGISTER_NICKS_SUCCESS", hi->handle);
    }
    dict_remove(nickserv_handle_dict, hi->handle);
}

struct handle_info*
get_handle_info(const char *handle)
{
    return dict_find(nickserv_handle_dict, handle, 0);
}

struct nick_info*
get_nick_info(const char *nick)
{
    return nickserv_conf.disable_nicks ? 0 : dict_find(nickserv_nick_dict, nick, 0);
}

struct modeNode *
find_handle_in_channel(struct chanNode *channel, struct handle_info *handle, struct userNode *except)
{
    unsigned int nn;
    struct modeNode *mn;

    for (nn=0; nn<channel->members.used; ++nn) {
        mn = channel->members.list[nn];
        if ((mn->user != except) && (mn->user->handle_info == handle))
            return mn;
    }
    return NULL;
}

int
oper_has_access(struct userNode *user, struct userNode *bot, unsigned int min_level, unsigned int quiet) {
    if (!user->handle_info) {
        if (!quiet)
            send_message(user, bot, "MSG_AUTHENTICATE");
        return 0;
    }

    if (!IsOper(user) && (!IsHelping(user) || min_level)) {
	if (!quiet)
            send_message(user, bot, "NSMSG_NO_ACCESS");
	return 0;
    }

    if (HANDLE_FLAGGED(user->handle_info, OPER_SUSPENDED)) {
	if (!quiet)
            send_message(user, bot, "MSG_OPER_SUSPENDED");
	return 0;
    }

    if (user->handle_info->opserv_level < min_level) {
	if (!quiet)
            send_message(user, bot, "NSMSG_NO_ACCESS");
	return 0;
    }

    return 1;
}

static int
is_valid_handle(const char *handle)
{
    struct userNode *user;
    /* cant register a juped nick/service nick as handle, to prevent confusion */
    user = GetUserH(handle);
    if (user && IsLocal(user))
        return 0;
    /* check against maximum length */
    if (strlen(handle) > NICKSERV_HANDLE_LEN)
	return 0;
    /* for consistency, only allow account names that could be nicks */
    if (!is_valid_nick(handle))
        return 0;
    /* disallow account names that look like bad words */
    if (opserv_bad_channel(handle))
        return 0;
    /* test either regex or containing all valid chars */
    if (nickserv_conf.valid_handle_regex_set) {
        int err = regexec(&nickserv_conf.valid_handle_regex, handle, 0, 0, 0);
        if (err) {
            char buff[256];
            buff[regerror(err, &nickserv_conf.valid_handle_regex, buff, sizeof(buff))] = 0;
            log_module(NS_LOG, LOG_INFO, "regexec error: %s (%d)", buff, err);
        }
        return !err;
    } else {
        return !handle[strspn(handle, NICKSERV_VALID_CHARS)];
    }
}

static int
is_registerable_nick(const char *nick)
{
    /* make sure it could be used as an account name */
    if (!is_valid_handle(nick))
        return 0;
    /* check length */
    if (strlen(nick) > NICKLEN)
        return 0;
    /* test either regex or as valid handle */
    if (nickserv_conf.valid_nick_regex_set) {
        int err = regexec(&nickserv_conf.valid_nick_regex, nick, 0, 0, 0);
        if (err) {
            char buff[256];
            buff[regerror(err, &nickserv_conf.valid_nick_regex, buff, sizeof(buff))] = 0;
            log_module(NS_LOG, LOG_INFO, "regexec error: %s (%d)", buff, err);
        }
        return !err;
    }
    return 1;
}

static int
is_valid_email_addr(const char *email)
{
    return strchr(email, '@') != NULL;
}

static const char *
visible_email_addr(struct userNode *user, struct handle_info *hi)
{
    if (hi->email_addr) {
        if (oper_has_access(user, nickserv, nickserv_conf.email_visible_level, 1)) {
            return hi->email_addr;
        } else {
            return "Set.";
        }
    } else {
        return "Not set.";
    }
}

struct handle_info *
smart_get_handle_info(struct userNode *service, struct userNode *user, const char *name)
{
    struct handle_info *hi;
    struct userNode *target;

    switch (*name) {
    case '*':
        if (!(hi = get_handle_info(++name))) {
            send_message(user, service, "MSG_HANDLE_UNKNOWN", name);
            return 0;
        }
        return hi;
    default:
        if (!(target = GetUserH(name))) {
            send_message(user, service, "MSG_NICK_UNKNOWN", name);
            return 0;
        }
        if (IsLocal(target)) {
	    if (IsService(target))
                send_message(user, service, "NSMSG_USER_IS_SERVICE", target->nick);
	    else
                send_message(user, service, "MSG_USER_AUTHENTICATE", target->nick);
            return 0;
        }
        if (!(hi = target->handle_info)) {
            send_message(user, service, "MSG_USER_AUTHENTICATE", target->nick);
            return 0;
        }
        return hi;
    }
}

int
oper_outranks(struct userNode *user, struct handle_info *hi) {
    if (user->handle_info->opserv_level > hi->opserv_level)
        return 1;
    if (user->handle_info->opserv_level == hi->opserv_level) {
        if ((user->handle_info->opserv_level == 1000)
            || (user->handle_info == hi)
            || ((user->handle_info->opserv_level == 0)
                && !(HANDLE_FLAGGED(hi, SUPPORT_HELPER) || HANDLE_FLAGGED(hi, NETWORK_HELPER))
                && HANDLE_FLAGGED(user->handle_info, HELPING))) {
            return 1;
        }
    }
    send_message(user, nickserv, "MSG_USER_OUTRANKED", hi->handle);
    return 0;
}

static struct handle_info *
get_victim_oper(struct userNode *user, const char *target)
{
    struct handle_info *hi;
    if (!(hi = smart_get_handle_info(nickserv, user, target)))
        return 0;
    if (HANDLE_FLAGGED(user->handle_info, OPER_SUSPENDED)) {
	send_message(user, nickserv, "MSG_OPER_SUSPENDED");
	return 0;
    }
    return oper_outranks(user, hi) ? hi : NULL;
}

static int
valid_user_for(struct userNode *user, struct handle_info *hi)
{
    unsigned int ii;

    /* If no hostmasks on the account, allow it. */
    if (!hi->masks->used)
        return 1;
    /* If any hostmask matches, allow it. */
    for (ii=0; ii<hi->masks->used; ii++)
        if (user_matches_glob(user, hi->masks->list[ii], 0))
            return 1;
    /* If they are allowauthed to this account, allow it (removing the aa). */
    if (dict_find(nickserv_allow_auth_dict, user->nick, NULL) == hi) {
	dict_remove(nickserv_allow_auth_dict, user->nick);
	return 2;
    }
    /* The user is not allowed to use this account. */
    return 0;
}

static int
is_secure_password(const char *handle, const char *pass, struct userNode *user)
{
    unsigned int i, len;
    unsigned int cnt_digits = 0, cnt_upper = 0, cnt_lower = 0;
    len = strlen(pass);
    if (len < nickserv_conf.password_min_length) {
        if (user)
            send_message(user, nickserv, "NSMSG_PASSWORD_SHORT", nickserv_conf.password_min_length);
        return 0;
    }
    if (!irccasecmp(pass, handle)) {
        if (user)
            send_message(user, nickserv, "NSMSG_PASSWORD_ACCOUNT");
        return 0;
    }
    dict_find(nickserv_conf.weak_password_dict, pass, &i);
    if (i) {
        if (user)
            send_message(user, nickserv, "NSMSG_PASSWORD_DICTIONARY");
        return 0;
    }
    for (i=0; i<len; i++) {
	if (isdigit(pass[i]))
            cnt_digits++;
	if (isupper(pass[i]))
            cnt_upper++;
	if (islower(pass[i]))
            cnt_lower++;
    }
    if ((cnt_lower < nickserv_conf.password_min_lower)
	|| (cnt_upper < nickserv_conf.password_min_upper)
	|| (cnt_digits < nickserv_conf.password_min_digits)) {
        if (user)
            send_message(user, nickserv, "NSMSG_PASSWORD_READABLE", nickserv_conf.password_min_digits, nickserv_conf.password_min_upper, nickserv_conf.password_min_lower);
        return 0;
    }
    return 1;
}

static auth_func_t *auth_func_list;
static unsigned int auth_func_size = 0, auth_func_used = 0;

void
reg_auth_func(auth_func_t func)
{
    if (auth_func_used == auth_func_size) {
	if (auth_func_size) {
	    auth_func_size <<= 1;
	    auth_func_list = realloc(auth_func_list, auth_func_size*sizeof(auth_func_t));
	} else {
	    auth_func_size = 8;
	    auth_func_list = malloc(auth_func_size*sizeof(auth_func_t));
	}
    }
    auth_func_list[auth_func_used++] = func;
}

static handle_rename_func_t *rf_list;
static unsigned int rf_list_size, rf_list_used;

void
reg_handle_rename_func(handle_rename_func_t func)
{
    if (rf_list_used == rf_list_size) {
        if (rf_list_size) {
            rf_list_size <<= 1;
            rf_list = realloc(rf_list, rf_list_size*sizeof(rf_list[0]));
        } else {
            rf_list_size = 8;
            rf_list = malloc(rf_list_size*sizeof(rf_list[0]));
        }
    }
    rf_list[rf_list_used++] = func;
}

static char *
generate_fakehost(struct handle_info *handle)
{
    extern const char *hidden_host_suffix;
    static char buffer[HOSTLEN+1];

    if (!handle->fakehost) {
        snprintf(buffer, sizeof(buffer), "%s.%s", handle->handle, hidden_host_suffix);
        return buffer;
    } else if (handle->fakehost[0] == '.') {
        /* A leading dot indicates the stored value is actually a title. */
        snprintf(buffer, sizeof(buffer), "%s.%s.%s", handle->handle, handle->fakehost+1, nickserv_conf.titlehost_suffix);
        return buffer;
    }
    return handle->fakehost;
}

static void
apply_fakehost(struct handle_info *handle)
{
    struct userNode *target;
    char *fake;

    if (!handle->users)
        return;
    fake = generate_fakehost(handle);
    for (target = handle->users; target; target = target->next_authed)
        assign_fakehost(target, fake, 1);
}

static void
set_user_handle_info(struct userNode *user, struct handle_info *hi, int stamp)
{
    unsigned int n;
    struct handle_info *old_info;

    /* This can happen if somebody uses COOKIE while authed, or if
     * they re-auth to their current handle (which is silly, but users
     * are like that). */
    if (user->handle_info == hi)
        return;

    if (user->handle_info) {
	struct userNode *other;

	if (IsHelper(user))
            userList_remove(&curr_helpers, user);

	/* remove from next_authed linked list */
	if (user->handle_info->users == user) {
	    user->handle_info->users = user->next_authed;
	} else {
	    for (other = user->handle_info->users;
		 other->next_authed != user;
		 other = other->next_authed) ;
	    other->next_authed = user->next_authed;
	}
        /* if nobody left on old handle, and they're not an oper, remove !god */
        if (!user->handle_info->users && !user->handle_info->opserv_level)
            HANDLE_CLEAR_FLAG(user->handle_info, HELPING);
        /* record them as being last seen at this time */
	user->handle_info->lastseen = now;
        /* and record their hostmask */
        snprintf(user->handle_info->last_quit_host, sizeof(user->handle_info->last_quit_host), "%s@%s", user->ident, user->hostname);
    }
    old_info = user->handle_info;
    user->handle_info = hi;
    if (hi && !hi->users && !hi->opserv_level)
        HANDLE_CLEAR_FLAG(hi, HELPING);
    for (n=0; n<auth_func_used; n++)
        auth_func_list[n](user, old_info);
    if (hi) {
        struct nick_info *ni;

        HANDLE_CLEAR_FLAG(hi, FROZEN);
        if (nickserv_conf.warn_clone_auth) {
            struct userNode *other;
            for (other = hi->users; other; other = other->next_authed)
                send_message(other, nickserv, "NSMSG_CLONE_AUTH", user->nick, user->ident, user->hostname);
        }
	user->next_authed = hi->users;
	hi->users = user;
	hi->lastseen = now;
	if (IsHelper(user))
            userList_append(&curr_helpers, user);

        if (hi->fakehost || old_info)
            apply_fakehost(hi);

        if (stamp) {
#ifdef WITH_PROTOCOL_BAHAMUT
            /* Stamp users with their account ID. */
            char id[IDLEN + 1];
            inttobase64(id, hi->id, IDLEN);
#elif WITH_PROTOCOL_P10
            /* Stamp users with their account name. */
            char *id = hi->handle;
#else
            const char *id = "???";
#endif
            if (!nickserv_conf.disable_nicks) {
                struct nick_info *ni;
                for (ni = hi->nicks; ni; ni = ni->next) {
                    if (!irccasecmp(user->nick, ni->nick)) {
                        user->modes |= FLAGS_REGNICK;
                        break;
                    }
                }
            }
            StampUser(user, id);
        }

        if ((ni = get_nick_info(user->nick)) && (ni->owner == hi))
            timeq_del(0, nickserv_reclaim_p, user, TIMEQ_IGNORE_WHEN);
    } else {
        /* We cannot clear the user's account ID, unfortunately. */
	user->next_authed = NULL;
    }
}

static struct handle_info*
nickserv_register(struct userNode *user, struct userNode *settee, const char *handle, const char *passwd, int no_auth)
{
    struct handle_info *hi;
    struct nick_info *ni;
    char crypted[MD5_CRYPT_LENGTH];

    if ((hi = dict_find(nickserv_handle_dict, handle, NULL))) {
	send_message(user, nickserv, "NSMSG_HANDLE_EXISTS", handle);
	return 0;
    }

    if (!is_secure_password(handle, passwd, user))
        return 0;

    cryptpass(passwd, crypted);
    hi = register_handle(handle, crypted, 0);
    hi->masks = alloc_string_list(1);
    hi->users = NULL;
    hi->language = lang_C;
    hi->registered = now;
    hi->lastseen = now;
    hi->flags = HI_DEFAULT_FLAGS;
    if (settee && !no_auth)
        set_user_handle_info(settee, hi, 1);

    if (user != settee)
        send_message(user, nickserv, "NSMSG_OREGISTER_H_SUCCESS");
    else if (nickserv_conf.disable_nicks)
        send_message(user, nickserv, "NSMSG_REGISTER_H_SUCCESS");
    else if ((ni = dict_find(nickserv_nick_dict, user->nick, NULL)))
        send_message(user, nickserv, "NSMSG_PARTIAL_REGISTER");
    else {
        register_nick(user->nick, hi);
        send_message(user, nickserv, "NSMSG_REGISTER_HN_SUCCESS");
    }
    if (settee && (user != settee))
        send_message(settee, nickserv, "NSMSG_OREGISTER_VICTIM", user->nick, hi->handle);
    return hi;
}

static void
nickserv_bake_cookie(struct handle_cookie *cookie)
{
    cookie->hi->cookie = cookie;
    timeq_add(cookie->expires, nickserv_free_cookie, cookie);
}

static void
nickserv_make_cookie(struct userNode *user, struct handle_info *hi, enum cookie_type type, const char *cookie_data)
{
    struct handle_cookie *cookie;
    char subject[128], body[4096], *misc;
    const char *netname, *fmt;
    int first_time = 0;

    if (hi->cookie) {
        send_message(user, nickserv, "NSMSG_COOKIE_LIVE", hi->handle);
        return;
    }

    cookie = calloc(1, sizeof(*cookie));
    cookie->hi = hi;
    cookie->type = type;
    cookie->data = cookie_data ? strdup(cookie_data) : NULL;
    cookie->expires = now + nickserv_conf.cookie_timeout;
    inttobase64(cookie->cookie, rand(), 5);
    inttobase64(cookie->cookie+5, rand(), 5);

    netname = nickserv_conf.network_name;
    subject[0] = 0;

    switch (cookie->type) {
    case ACTIVATION:
        hi->passwd[0] = 0; /* invalidate password */
        send_message(user, nickserv, "NSMSG_USE_COOKIE_REGISTER");
        fmt = handle_find_message(hi, "NSEMAIL_ACTIVATION_SUBJECT");
        snprintf(subject, sizeof(subject), fmt, netname);
        fmt = handle_find_message(hi, "NSEMAIL_ACTIVATION_BODY");
        snprintf(body, sizeof(body), fmt, netname, cookie->cookie, nickserv->nick, self->name, hi->handle);
        first_time = 1;
        break;
    case PASSWORD_CHANGE:
        send_message(user, nickserv, "NSMSG_USE_COOKIE_RESETPASS");
        fmt = handle_find_message(hi, "NSEMAIL_PASSWORD_CHANGE_SUBJECT");
        snprintf(subject, sizeof(subject), fmt, netname);
        fmt = handle_find_message(hi, "NSEMAIL_PASSWORD_CHANGE_BODY");
        snprintf(body, sizeof(body), fmt, netname, cookie->cookie, nickserv->nick, self->name, hi->handle);
        break;
    case EMAIL_CHANGE:
        misc = hi->email_addr;
        hi->email_addr = cookie->data;
        if (misc) {
            send_message(user, nickserv, "NSMSG_USE_COOKIE_EMAIL_2");
            fmt = handle_find_message(hi, "NSEMAIL_EMAIL_CHANGE_SUBJECT");
            snprintf(subject, sizeof(subject), fmt, netname);
            fmt = handle_find_message(hi, "NSEMAIL_EMAIL_CHANGE_BODY_NEW");
            snprintf(body, sizeof(body), fmt, netname, cookie->cookie+COOKIELEN/2, nickserv->nick, self->name, hi->handle, COOKIELEN/2);
            sendmail(nickserv, hi, subject, body, 1);
            fmt = handle_find_message(hi, "NSEMAIL_EMAIL_CHANGE_BODY_OLD");
            snprintf(body, sizeof(body), fmt, netname, cookie->cookie, nickserv->nick, self->name, hi->handle, COOKIELEN/2, hi->email_addr);
        } else {
            send_message(user, nickserv, "NSMSG_USE_COOKIE_EMAIL_1");
            fmt = handle_find_message(hi, "NSEMAIL_EMAIL_VERIFY_SUBJECT");
            snprintf(subject, sizeof(subject), fmt, netname);
            fmt = handle_find_message(hi, "NSEMAIL_EMAIL_VERIFY_BODY");
            snprintf(body, sizeof(body), fmt, netname, cookie->cookie, nickserv->nick, self->name, hi->handle);
            sendmail(nickserv, hi, subject, body, 1);
            subject[0] = 0;
        }
        hi->email_addr = misc;
        break;
    case ALLOWAUTH:
        fmt = handle_find_message(hi, "NSEMAIL_ALLOWAUTH_SUBJECT");
        snprintf(subject, sizeof(subject), fmt, netname);
        fmt = handle_find_message(hi, "NSEMAIL_ALLOWAUTH_BODY");
        snprintf(body, sizeof(body), fmt, netname, cookie->cookie, nickserv->nick, self->name, hi->handle);
        send_message(user, nickserv, "NSMSG_USE_COOKIE_AUTH");
        break;
    default:
        log_module(NS_LOG, LOG_ERROR, "Bad cookie type %d in nickserv_make_cookie.", cookie->type);
        break;
    }
    if (subject[0])
        sendmail(nickserv, hi, subject, body, first_time);
    nickserv_bake_cookie(cookie);
}

static void
nickserv_eat_cookie(struct handle_cookie *cookie)
{
    cookie->hi->cookie = NULL;
    timeq_del(cookie->expires, nickserv_free_cookie, cookie, 0);
    nickserv_free_cookie(cookie);
}

static void
nickserv_free_email_addr(void *data)
{
    handle_info_list_clean(data);
    free(data);
}

static void
nickserv_set_email_addr(struct handle_info *hi, const char *new_email_addr)
{
    struct handle_info_list *hil;
    /* Remove from old handle_info_list ... */
    if (hi->email_addr && (hil = dict_find(nickserv_email_dict, hi->email_addr, 0))) {
        handle_info_list_remove(hil, hi);
        if (!hil->used) dict_remove(nickserv_email_dict, hil->tag);
        hi->email_addr = NULL;
    }
    /* Add to the new list.. */
    if (new_email_addr) {
        if (!(hil = dict_find(nickserv_email_dict, new_email_addr, 0))) {
            hil = calloc(1, sizeof(*hil));
            hil->tag = strdup(new_email_addr);
            handle_info_list_init(hil);
            dict_insert(nickserv_email_dict, hil->tag, hil);
        }
        handle_info_list_append(hil, hi);
        hi->email_addr = hil->tag;
    }
}

static NICKSERV_FUNC(cmd_register)
{
    struct handle_info *hi;
    const char *email_addr, *password;
    int no_auth;

    if (!IsOper(user) && !dict_size(nickserv_handle_dict)) {
	/* Require the first handle registered to belong to someone +o. */
	reply("NSMSG_REQUIRE_OPER");
	return 0;
    }

    if (user->handle_info) {
        reply("NSMSG_USE_RENAME", user->handle_info->handle);
        return 0;
    }

    if (IsRegistering(user)) {
        reply("NSMSG_ALREADY_REGISTERING");
	return 0;
    }

    if (IsStamped(user)) {
        /* Unauthenticated users might still have been stamped
           previously and could therefore have a hidden host;
           do not allow them to register a new account. */
        reply("NSMSG_STAMPED_REGISTER");
        return 0;
    }

    NICKSERV_MIN_PARMS((unsigned)3 + nickserv_conf.email_required);

    if (!is_valid_handle(argv[1])) {
        reply("NSMSG_BAD_HANDLE", argv[1]);
        return 0;
    }

    if ((argc >= 4) && nickserv_conf.email_enabled) {
        struct handle_info_list *hil;
        const char *str;

        /* Remember email address. */
        email_addr = argv[3];

        /* Check that the email address looks valid.. */
        if (!is_valid_email_addr(email_addr)) {
            reply("NSMSG_BAD_EMAIL_ADDR");
            return 0;
        }

        /* .. and that we are allowed to send to it. */
        if ((str = sendmail_prohibited_address(email_addr))) {
            reply("NSMSG_EMAIL_PROHIBITED", email_addr, str);
            return 0;
        }

        /* If we do email verify, make sure we don't spam the address. */
        if ((hil = dict_find(nickserv_email_dict, email_addr, NULL))) {
            unsigned int nn;
            for (nn=0; nn<hil->used; nn++) {
                if (hil->list[nn]->cookie) {
                    reply("NSMSG_EMAIL_UNACTIVATED");
                    return 0;
                }
            }
            if (hil->used >= nickserv_conf.handles_per_email) {
                reply("NSMSG_EMAIL_OVERUSED");
                return 0;
            }
        }

        no_auth = 1;
    } else {
        email_addr = 0;
        no_auth = 0;
    }

    password = argv[2];
    argv[2] = "****";
    if (!(hi = nickserv_register(user, user, argv[1], password, no_auth)))
        return 0;
    /* Add any masks they should get. */
    if (nickserv_conf.default_hostmask) {
        string_list_append(hi->masks, strdup("*@*"));
    } else {
        string_list_append(hi->masks, generate_hostmask(user, GENMASK_OMITNICK|GENMASK_NO_HIDING|GENMASK_ANY_IDENT));
        if (user->ip.s_addr && user->hostname[strspn(user->hostname, "0123456789.")])
            string_list_append(hi->masks, generate_hostmask(user, GENMASK_OMITNICK|GENMASK_BYIP|GENMASK_NO_HIDING|GENMASK_ANY_IDENT));
    }

    /* If they're the first to register, give them level 1000. */
    if (dict_size(nickserv_handle_dict) == 1) {
        hi->opserv_level = 1000;
        reply("NSMSG_ROOT_HANDLE", argv[1]);
    }

    /* Set their email address. */
    if (email_addr)
        nickserv_set_email_addr(hi, email_addr);

    /* If they need to do email verification, tell them. */
    if (no_auth)
        nickserv_make_cookie(user, hi, ACTIVATION, hi->passwd);

    /* Set registering flag.. */
    user->modes |= FLAGS_REGISTERING; 

    return 1;
}

static NICKSERV_FUNC(cmd_oregister)
{
    char *mask;
    struct userNode *settee;
    struct handle_info *hi;

    NICKSERV_MIN_PARMS(4);

    if (!is_valid_handle(argv[1])) {
        reply("NSMSG_BAD_HANDLE", argv[1]);
        return 0;
    }

    if (strchr(argv[3], '@')) {
	mask = canonicalize_hostmask(strdup(argv[3]));
	if (argc > 4) {
	    settee = GetUserH(argv[4]);
	    if (!settee) {
		reply("MSG_NICK_UNKNOWN", argv[4]);
                free(mask);
		return 0;
	    }
	} else {
	    settee = NULL;
	}
    } else if ((settee = GetUserH(argv[3]))) {
	mask = generate_hostmask(settee, GENMASK_OMITNICK|GENMASK_NO_HIDING|GENMASK_ANY_IDENT);
    } else {
	reply("NSMSG_REGISTER_BAD_NICKMASK", argv[3]);
	return 0;
    }
    if (settee && settee->handle_info) {
        reply("NSMSG_USER_PREV_AUTH", settee->nick);
        free(mask);
        return 0;
    }
    if (!(hi = nickserv_register(user, settee, argv[1], argv[2], 0))) {
        free(mask);
        return 0;
    }
    string_list_append(hi->masks, mask);
    return 1;
}

static NICKSERV_FUNC(cmd_handleinfo)
{
    char buff[400];
    unsigned int i, pos=0, herelen;
    struct userNode *target, *next_un;
    struct handle_info *hi;
    const char *nsmsg_none;

    if (argc < 2) {
        if (!(hi = user->handle_info)) {
            reply("NSMSG_MUST_AUTH");
            return 0;
        }
    } else if (!(hi = modcmd_get_handle_info(user, argv[1]))) {
        return 0;
    }

    nsmsg_none = handle_find_message(hi, "MSG_NONE");
    reply("NSMSG_HANDLEINFO_ON", hi->handle);
#ifdef WITH_PROTOCOL_BAHAMUT
    reply("NSMSG_HANDLEINFO_ID", hi->id);
#endif
    reply("NSMSG_HANDLEINFO_REGGED", ctime(&hi->registered));

    if (!hi->users) {
	intervalString(buff, now - hi->lastseen, user->handle_info);
	reply("NSMSG_HANDLEINFO_LASTSEEN", buff);
    } else {
	reply("NSMSG_HANDLEINFO_LASTSEEN_NOW");
    }

    reply("NSMSG_HANDLEINFO_INFOLINE", (hi->infoline ? hi->infoline : nsmsg_none));
    if (HANDLE_FLAGGED(hi, FROZEN))
        reply("NSMSG_HANDLEINFO_VACATION");

    if (oper_has_access(user, cmd->parent->bot, 0, 1)) {
        struct do_not_register *dnr;
        if ((dnr = chanserv_is_dnr(NULL, hi)))
            reply("NSMSG_HANDLEINFO_DNR", dnr->setter, dnr->reason);
        if (!oper_outranks(user, hi))
            return 1;
    } else if (hi != user->handle_info)
        return 1;

    if (nickserv_conf.email_enabled)
        reply("NSMSG_HANDLEINFO_EMAIL_ADDR", visible_email_addr(user, hi));

    if (hi->cookie) {
        const char *type;
        switch (hi->cookie->type) {
        case ACTIVATION: type = "NSMSG_HANDLEINFO_COOKIE_ACTIVATION"; break;
        case PASSWORD_CHANGE: type = "NSMSG_HANDLEINFO_COOKIE_PASSWORD"; break;
        case EMAIL_CHANGE: type = "NSMSG_HANDLEINFO_COOKIE_EMAIL"; break;
        case ALLOWAUTH: type = "NSMSG_HANDLEINFO_COOKIE_ALLOWAUTH"; break;
        default: type = "NSMSG_HANDLEINFO_COOKIE_UNKNOWN"; break;
        }
        reply(type);
    }

    if (hi->flags) {
	unsigned long flen = 1;
	char flags[34]; /* 32 bits possible plus '+' and '\0' */
	flags[0] = '+';
	for (i=0, flen=1; handle_flags[i]; i++)
	    if (hi->flags & 1 << i)
                flags[flen++] = handle_flags[i];
	flags[flen] = 0;
	reply("NSMSG_HANDLEINFO_FLAGS", flags);
    } else {
	reply("NSMSG_HANDLEINFO_FLAGS", nsmsg_none);
    }

    if (HANDLE_FLAGGED(hi, SUPPORT_HELPER)
        || HANDLE_FLAGGED(hi, NETWORK_HELPER)
        || (hi->opserv_level > 0)) {
        reply("NSMSG_HANDLEINFO_EPITHET", (hi->epithet ? hi->epithet : nsmsg_none));
    }

    if (hi->fakehost)
        reply("NSMSG_HANDLEINFO_FAKEHOST", (hi->fakehost ? hi->fakehost : handle_find_message(hi, "MSG_NONE")));

    if (hi->last_quit_host[0])
        reply("NSMSG_HANDLEINFO_LAST_HOST", hi->last_quit_host);
    else
        reply("NSMSG_HANDLEINFO_LAST_HOST_UNKNOWN");

    if (nickserv_conf.disable_nicks) {
	/* nicks disabled; don't show anything about registered nicks */
    } else if (hi->nicks) {
	struct nick_info *ni, *next_ni;
	for (ni = hi->nicks; ni; ni = next_ni) {
	    herelen = strlen(ni->nick);
	    if (pos + herelen + 1 > ArrayLength(buff)) {
		next_ni = ni;
		goto print_nicks_buff;
	    } else {
		next_ni = ni->next;
	    }
	    memcpy(buff+pos, ni->nick, herelen);
	    pos += herelen; buff[pos++] = ' ';
	    if (!next_ni) {
	      print_nicks_buff:
		buff[pos-1] = 0;
		reply("NSMSG_HANDLEINFO_NICKS", buff);
		pos = 0;
	    }
	}
    } else {
	reply("NSMSG_HANDLEINFO_NICKS", nsmsg_none);
    }

    if (hi->masks->used) {
        for (i=0; i < hi->masks->used; i++) {
            herelen = strlen(hi->masks->list[i]);
            if (pos + herelen + 1 > ArrayLength(buff)) {
                i--;
                goto print_mask_buff;
            }
            memcpy(buff+pos, hi->masks->list[i], herelen);
            pos += herelen; buff[pos++] = ' ';
            if (i+1 == hi->masks->used) {
              print_mask_buff:
                buff[pos-1] = 0;
                reply("NSMSG_HANDLEINFO_MASKS", buff);
                pos = 0;
            }
        }
    } else {
        reply("NSMSG_HANDLEINFO_MASKS", nsmsg_none);
    }

    if (hi->channels) {
	struct userData *channel, *next;
	char *name;

	for (channel = hi->channels; channel; channel = next) {
	    next = channel->u_next;
            name = channel->channel->channel->name;
	    herelen = strlen(name);
	    if (pos + herelen + 7 > ArrayLength(buff)) {
		next = channel;
                goto print_chans_buff;
	    }
            if (IsUserSuspended(channel))
                buff[pos++] = '-';
            pos += sprintf(buff+pos, "%d:%s ", channel->access, name);
	    if (next == NULL) {
	      print_chans_buff:
		buff[pos-1] = 0;
		reply("NSMSG_HANDLEINFO_CHANNELS", buff);
		pos = 0;
	    }
	}
    } else {
	reply("NSMSG_HANDLEINFO_CHANNELS", nsmsg_none);
    }

    for (target = hi->users; target; target = next_un) {
	herelen = strlen(target->nick);
	if (pos + herelen + 1 > ArrayLength(buff)) {
	    next_un = target;
	    goto print_cnick_buff;
	} else {
	    next_un = target->next_authed;
	}
	memcpy(buff+pos, target->nick, herelen);
	pos += herelen; buff[pos++] = ' ';
	if (!next_un) {
	  print_cnick_buff:
	    buff[pos-1] = 0;
	    reply("NSMSG_HANDLEINFO_CURRENT", buff);
	    pos = 0;
	}
    }

    return 1;
}

static NICKSERV_FUNC(cmd_userinfo)
{
    struct userNode *target;

    NICKSERV_MIN_PARMS(2);
    if (!(target = GetUserH(argv[1]))) {
	reply("MSG_NICK_UNKNOWN", argv[1]);
	return 0;
    }
    if (target->handle_info)
	reply("NSMSG_USERINFO_AUTHED_AS", target->nick, target->handle_info->handle);
    else
	reply("NSMSG_USERINFO_NOT_AUTHED", target->nick);
    return 1;
}

static NICKSERV_FUNC(cmd_nickinfo)
{
    struct nick_info *ni;

    NICKSERV_MIN_PARMS(2);
    if (!(ni = get_nick_info(argv[1]))) {
	reply("MSG_NICK_UNKNOWN", argv[1]);
	return 0;
    }
    reply("NSMSG_NICKINFO_OWNER", ni->nick, ni->owner->handle);
    return 1;
}

static NICKSERV_FUNC(cmd_rename_handle)
{
    struct handle_info *hi;
    char msgbuf[MAXLEN], *old_handle;
    unsigned int nn;

    NICKSERV_MIN_PARMS(3);
    if (!(hi = get_victim_oper(user, argv[1])))
        return 0;
    if (!is_valid_handle(argv[2])) {
        reply("NSMSG_FAIL_RENAME", argv[1], argv[2]);
        return 0;
    }
    if (get_handle_info(argv[2])) {
        reply("NSMSG_HANDLE_EXISTS", argv[2]);
        return 0;
    }

    dict_remove2(nickserv_handle_dict, old_handle = hi->handle, 1);
    hi->handle = strdup(argv[2]);
    dict_insert(nickserv_handle_dict, hi->handle, hi);
    for (nn=0; nn<rf_list_used; nn++)
        rf_list[nn](hi, old_handle);
    snprintf(msgbuf, sizeof(msgbuf), "%s renamed account %s to %s.", user->handle_info->handle, old_handle, hi->handle);
    reply("NSMSG_HANDLE_CHANGED", old_handle, hi->handle);
    global_message(MESSAGE_RECIPIENT_STAFF, msgbuf);
    free(old_handle);
    return 1;
}

static failpw_func_t *failpw_func_list;
static unsigned int failpw_func_size = 0, failpw_func_used = 0;

void
reg_failpw_func(failpw_func_t func)
{
    if (failpw_func_used == failpw_func_size) {
        if (failpw_func_size) {
            failpw_func_size <<= 1;
            failpw_func_list = realloc(failpw_func_list, failpw_func_size*sizeof(failpw_func_t));
        } else {
            failpw_func_size = 8;
            failpw_func_list = malloc(failpw_func_size*sizeof(failpw_func_t));
        }
    }
    failpw_func_list[failpw_func_used++] = func;
}

static NICKSERV_FUNC(cmd_auth)
{
    int pw_arg, used, maxlogins;
    struct handle_info *hi;
    const char *passwd;
    struct userNode *other;

    if (user->handle_info) {
        reply("NSMSG_ALREADY_AUTHED", user->handle_info->handle);
        return 0;
    }
    if (IsStamped(user)) {
        /* Unauthenticated users might still have been stamped
           previously and could therefore have a hidden host;
           do not allow them to authenticate. */
        reply("NSMSG_STAMPED_AUTH");
        return 0;
    }
    if (argc == 3) {
        hi = dict_find(nickserv_handle_dict, argv[1], NULL);
        pw_arg = 2;
    } else if (argc == 2) {
        if (nickserv_conf.disable_nicks) {
            if (!(hi = get_handle_info(user->nick))) {
                reply("NSMSG_HANDLE_NOT_FOUND");
                return 0;
            }
        } else {
            /* try to look up their handle from their nick */
            struct nick_info *ni;
            ni = get_nick_info(user->nick);
            if (!ni) {
                reply("NSMSG_NICK_NOT_REGISTERED", user->nick);
                return 0;
            }
            hi = ni->owner;
        }
        pw_arg = 1;
    } else {
        reply("MSG_MISSING_PARAMS", argv[0]);
        svccmd_send_help(user, nickserv, cmd);
        return 0;
    }
    if (!hi) {
        reply("NSMSG_HANDLE_NOT_FOUND");
        return 0;
    }
    /* Responses from here on look up the language used by the handle they asked about. */
    passwd = argv[pw_arg];
    if (!valid_user_for(user, hi)) {
        if (hi->email_addr && nickserv_conf.email_enabled)
            send_message_type(4, user, cmd->parent->bot,
                              handle_find_message(hi, "NSMSG_USE_AUTHCOOKIE"),
                              hi->handle);
        else
            send_message_type(4, user, cmd->parent->bot,
                              handle_find_message(hi, "NSMSG_HOSTMASK_INVALID"),
                              hi->handle);
        argv[pw_arg] = "BADMASK";
        return 1;
    }
    if (!checkpass(passwd, hi->passwd)) {
        unsigned int n;
        send_message_type(4, user, cmd->parent->bot,
                          handle_find_message(hi, "NSMSG_PASSWORD_INVALID"));
        argv[pw_arg] = "BADPASS";
        for (n=0; n<failpw_func_used; n++) failpw_func_list[n](user, hi);
        if (nickserv_conf.autogag_enabled) {
            if (!user->auth_policer.params) {
                user->auth_policer.last_req = now;
                user->auth_policer.params = nickserv_conf.auth_policer_params;
            }
            if (!policer_conforms(&user->auth_policer, now, 1.0)) {
                char *hostmask;
                hostmask = generate_hostmask(user, GENMASK_STRICT_HOST|GENMASK_BYIP|GENMASK_NO_HIDING);
                log_module(NS_LOG, LOG_INFO, "%s auto-gagged for repeated password guessing.", hostmask);
                gag_create(hostmask, nickserv->nick, "Repeated password guessing.", now+nickserv_conf.autogag_duration);
                free(hostmask);
                argv[pw_arg] = "GAGGED";
            }
        }
        return 1;
    }
    if (HANDLE_FLAGGED(hi, SUSPENDED)) {
        send_message_type(4, user, cmd->parent->bot,
                          handle_find_message(hi, "NSMSG_HANDLE_SUSPENDED"));
        argv[pw_arg] = "SUSPENDED";
        return 1;
    }
    maxlogins = hi->maxlogins ? hi->maxlogins : nickserv_conf.default_maxlogins;
    for (used = 0, other = hi->users; other; other = other->next_authed) {
        if (++used >= maxlogins) {
            send_message_type(4, user, cmd->parent->bot,
                              handle_find_message(hi, "NSMSG_MAX_LOGINS"),
                              maxlogins);
            argv[pw_arg] = "MAXLOGINS";
            return 1;
        }
    }

    set_user_handle_info(user, hi, 1);
    if (nickserv_conf.email_required && !hi->email_addr)
        reply("NSMSG_PLEASE_SET_EMAIL");
    if (!is_secure_password(hi->handle, passwd, NULL))
        reply("NSMSG_WEAK_PASSWORD");
    if (hi->passwd[0] != '$')
        cryptpass(passwd, hi->passwd);
    reply("NSMSG_AUTH_SUCCESS");
    argv[pw_arg] = "****";
    return 1;
}

static allowauth_func_t *allowauth_func_list;
static unsigned int allowauth_func_size = 0, allowauth_func_used = 0;

void
reg_allowauth_func(allowauth_func_t func)
{
    if (allowauth_func_used == allowauth_func_size) {
        if (allowauth_func_size) {
            allowauth_func_size <<= 1;
            allowauth_func_list = realloc(allowauth_func_list, allowauth_func_size*sizeof(allowauth_func_t));
        } else {
            allowauth_func_size = 8;
            allowauth_func_list = malloc(allowauth_func_size*sizeof(allowauth_func_t));
        }
    }
    allowauth_func_list[allowauth_func_used++] = func;
}

static NICKSERV_FUNC(cmd_allowauth)
{
    struct userNode *target;
    struct handle_info *hi;
    unsigned int n;

    NICKSERV_MIN_PARMS(2);
    if (!(target = GetUserH(argv[1]))) {
        reply("MSG_NICK_UNKNOWN", argv[1]);
        return 0;
    }
    if (target->handle_info) {
        reply("NSMSG_USER_PREV_AUTH", target->nick);
        return 0;
    }
    if (IsStamped(target)) {
        /* Unauthenticated users might still have been stamped
           previously and could therefore have a hidden host;
           do not allow them to authenticate to an account. */
        reply("NSMSG_USER_PREV_STAMP", target->nick);
        return 0;
    }
    if (argc == 2)
        hi = NULL;
    else if (!(hi = get_handle_info(argv[2]))) {
        reply("MSG_HANDLE_UNKNOWN", argv[2]);
        return 0;
    }
    if (hi) {
        if (hi->opserv_level > user->handle_info->opserv_level) {
            reply("MSG_USER_OUTRANKED", hi->handle);
            return 0;
        }
        if (((hi->flags & (HI_FLAG_SUPPORT_HELPER|HI_FLAG_NETWORK_HELPER))
             || (hi->opserv_level > 0))
            && ((argc < 4) || irccasecmp(argv[3], "staff"))) {
            reply("NSMSG_ALLOWAUTH_STAFF", hi->handle);
            return 0;
        }
        dict_insert(nickserv_allow_auth_dict, target->nick, hi);
        reply("NSMSG_AUTH_ALLOWED", target->nick, hi->handle);
        send_message(target, nickserv, "NSMSG_AUTH_ALLOWED_MSG", hi->handle, hi->handle);
        if (nickserv_conf.email_enabled)
            send_message(target, nickserv, "NSMSG_AUTH_ALLOWED_EMAIL");
    } else {
        if (dict_remove(nickserv_allow_auth_dict, target->nick))
            reply("NSMSG_AUTH_NORMAL_ONLY", target->nick);
        else
            reply("NSMSG_AUTH_UNSPECIAL", target->nick);
    }
    for (n=0; n<allowauth_func_used; n++)
        allowauth_func_list[n](user, target, hi);
    return 1;
}

static NICKSERV_FUNC(cmd_authcookie)
{
    struct handle_info *hi;

    NICKSERV_MIN_PARMS(2);
    if (user->handle_info) {
        reply("NSMSG_ALREADY_AUTHED", user->handle_info->handle);
        return 0;
    }
    if (IsStamped(user)) {
        /* Unauthenticated users might still have been stamped
           previously and could therefore have a hidden host;
           do not allow them to authenticate to an account. */
        reply("NSMSG_STAMPED_AUTHCOOKIE");
        return 0;
    }
    if (!(hi = get_handle_info(argv[1]))) {
        reply("MSG_HANDLE_UNKNOWN", argv[1]);
        return 0;
    }
    if (!hi->email_addr) {
        reply("MSG_SET_EMAIL_ADDR");
        return 0;
    }
    nickserv_make_cookie(user, hi, ALLOWAUTH, NULL);
    return 1;
}

static NICKSERV_FUNC(cmd_delcookie)
{
    struct handle_info *hi;

    hi = user->handle_info;
    if (!hi->cookie) {
        reply("NSMSG_NO_COOKIE");
        return 0;
    }
    switch (hi->cookie->type) {
    case ACTIVATION:
    case EMAIL_CHANGE:
        reply("NSMSG_MUST_TIME_OUT");
        break;
    default:
        nickserv_eat_cookie(hi->cookie);
        reply("NSMSG_ATE_COOKIE");
        break;
    }
    return 1;
}

static NICKSERV_FUNC(cmd_resetpass)
{
    struct handle_info *hi;
    char crypted[MD5_CRYPT_LENGTH];

    NICKSERV_MIN_PARMS(3);
    if (user->handle_info) {
        reply("NSMSG_ALREADY_AUTHED", user->handle_info->handle);
        return 0;
    }
    if (IsStamped(user)) {
        /* Unauthenticated users might still have been stamped
           previously and could therefore have a hidden host;
           do not allow them to activate an account. */
        reply("NSMSG_STAMPED_RESETPASS");
        return 0;
    }
    if (!(hi = get_handle_info(argv[1]))) {
        reply("MSG_HANDLE_UNKNOWN", argv[1]);
        return 0;
    }
    if (!hi->email_addr) {
        reply("MSG_SET_EMAIL_ADDR");
        return 0;
    }
    cryptpass(argv[2], crypted);
    argv[2] = "****";
    nickserv_make_cookie(user, hi, PASSWORD_CHANGE, crypted);
    return 1;
}

static NICKSERV_FUNC(cmd_cookie)
{
    struct handle_info *hi;
    const char *cookie;

    if ((argc == 2) && (hi = user->handle_info) && hi->cookie && (hi->cookie->type == EMAIL_CHANGE)) {
        cookie = argv[1];
    } else {
        NICKSERV_MIN_PARMS(3);
        if (!(hi = get_handle_info(argv[1]))) {
            reply("MSG_HANDLE_UNKNOWN", argv[1]);
            return 0;
        }
        cookie = argv[2];
    }

    if (HANDLE_FLAGGED(hi, SUSPENDED)) {
        reply("NSMSG_HANDLE_SUSPENDED");
        return 0;
    }

    if (!hi->cookie) {
        reply("NSMSG_NO_COOKIE");
        return 0;
    }

    /* Check validity of operation before comparing cookie to
     * prohibit guessing by authed users. */
    if (user->handle_info
        && (hi->cookie->type != EMAIL_CHANGE)
        && (hi->cookie->type != PASSWORD_CHANGE)) {
        reply("NSMSG_CANNOT_COOKIE");
        return 0;
    }

    if (strcmp(cookie, hi->cookie->cookie)) {
        reply("NSMSG_BAD_COOKIE");
        return 0;
    }

    switch (hi->cookie->type) {
    case ACTIVATION:
        safestrncpy(hi->passwd, hi->cookie->data, sizeof(hi->passwd));
        set_user_handle_info(user, hi, 1);
        reply("NSMSG_HANDLE_ACTIVATED");
        break;
    case PASSWORD_CHANGE:
        set_user_handle_info(user, hi, 1);
        safestrncpy(hi->passwd, hi->cookie->data, sizeof(hi->passwd));
        reply("NSMSG_PASSWORD_CHANGED");
        break;
    case EMAIL_CHANGE:
        nickserv_set_email_addr(hi, hi->cookie->data);
        reply("NSMSG_EMAIL_CHANGED");
        break;
    case ALLOWAUTH:
        set_user_handle_info(user, hi, 1);
        reply("NSMSG_AUTH_SUCCESS");
        break;
    default:
        reply("NSMSG_BAD_COOKIE_TYPE", hi->cookie->type);
        log_module(NS_LOG, LOG_ERROR, "Bad cookie type %d for account %s.", hi->cookie->type, hi->handle);
        break;
    }

    nickserv_eat_cookie(hi->cookie);

    return 1;
}

static NICKSERV_FUNC(cmd_oregnick) {
    const char *nick;
    struct handle_info *target;
    struct nick_info *ni;

    NICKSERV_MIN_PARMS(3);
    if (!(target = modcmd_get_handle_info(user, argv[1])))
        return 0;
    nick = argv[2];
    if (!is_registerable_nick(nick)) {
        reply("NSMSG_BAD_NICK", nick);
        return 0;
    }
    ni = dict_find(nickserv_nick_dict, nick, NULL);
    if (ni) {
	reply("NSMSG_NICK_EXISTS", nick);
	return 0;
    }
    register_nick(nick, target);
    reply("NSMSG_OREGNICK_SUCCESS", nick, target->handle);
    return 1;
}

static NICKSERV_FUNC(cmd_regnick) {
    unsigned n;
    struct nick_info *ni;

    if (!is_registerable_nick(user->nick)) {
        reply("NSMSG_BAD_NICK", user->nick);
        return 0;
    }
    /* count their nicks, see if it's too many */
    for (n=0,ni=user->handle_info->nicks; ni; n++,ni=ni->next) ;
    if (n >= nickserv_conf.nicks_per_handle) {
        reply("NSMSG_TOO_MANY_NICKS");
        return 0;
    }
    ni = dict_find(nickserv_nick_dict, user->nick, NULL);
    if (ni) {
	reply("NSMSG_NICK_EXISTS", user->nick);
	return 0;
    }
    register_nick(user->nick, user->handle_info);
    reply("NSMSG_REGNICK_SUCCESS", user->nick);
    return 1;
}

static NICKSERV_FUNC(cmd_pass)
{
    struct handle_info *hi;
    const char *old_pass, *new_pass;

    NICKSERV_MIN_PARMS(3);
    hi = user->handle_info;
    old_pass = argv[1];
    new_pass = argv[2];
    argv[2] = "****";
    if (!is_secure_password(hi->handle, new_pass, user)) return 0;
    if (!checkpass(old_pass, hi->passwd)) {
        argv[1] = "BADPASS";
	reply("NSMSG_PASSWORD_INVALID");
	return 0;
    }
    cryptpass(new_pass, hi->passwd);
    argv[1] = "****";
    reply("NSMSG_PASS_SUCCESS");
    return 1;
}

static int
nickserv_addmask(struct userNode *user, struct handle_info *hi, const char *mask)
{
    unsigned int i;
    char *new_mask = canonicalize_hostmask(strdup(mask));
    for (i=0; i<hi->masks->used; i++) {
        if (!irccasecmp(new_mask, hi->masks->list[i])) {
            send_message(user, nickserv, "NSMSG_ADDMASK_ALREADY", new_mask);
            free(new_mask);
            return 0;
        }
    }
    string_list_append(hi->masks, new_mask);
    send_message(user, nickserv, "NSMSG_ADDMASK_SUCCESS", new_mask);
    return 1;
}

static NICKSERV_FUNC(cmd_addmask)
{
    if (argc < 2) {
        char *mask = generate_hostmask(user, GENMASK_OMITNICK|GENMASK_NO_HIDING|GENMASK_ANY_IDENT);
        int res = nickserv_addmask(user, user->handle_info, mask);
        free(mask);
        return res;
    } else {
        if (!is_gline(argv[1])) {
            reply("NSMSG_MASK_INVALID", argv[1]);
            return 0;
        }
        return nickserv_addmask(user, user->handle_info, argv[1]);
    }
}

static NICKSERV_FUNC(cmd_oaddmask)
{
    struct handle_info *hi;

    NICKSERV_MIN_PARMS(3);
    if (!(hi = get_victim_oper(user, argv[1])))
        return 0;
    return nickserv_addmask(user, hi, argv[2]);
}

static int
nickserv_delmask(struct userNode *user, struct handle_info *hi, const char *del_mask)
{
    unsigned int i;
    for (i=0; i<hi->masks->used; i++) {
	if (!strcmp(del_mask, hi->masks->list[i])) {
	    char *old_mask = hi->masks->list[i];
	    if (hi->masks->used == 1) {
		send_message(user, nickserv, "NSMSG_DELMASK_NOTLAST");
		return 0;
	    }
	    hi->masks->list[i] = hi->masks->list[--hi->masks->used];
	    send_message(user, nickserv, "NSMSG_DELMASK_SUCCESS", old_mask);
	    free(old_mask);
	    return 1;
	}
    }
    send_message(user, nickserv, "NSMSG_DELMASK_NOT_FOUND");
    return 0;
}

static NICKSERV_FUNC(cmd_delmask)
{
    NICKSERV_MIN_PARMS(2);
    return nickserv_delmask(user, user->handle_info, argv[1]);
}

static NICKSERV_FUNC(cmd_odelmask)
{
    struct handle_info *hi;
    NICKSERV_MIN_PARMS(3);
    if (!(hi = get_victim_oper(user, argv[1])))
        return 0;
    return nickserv_delmask(user, hi, argv[2]);
}

int
nickserv_modify_handle_flags(struct userNode *user, struct userNode *bot, const char *str, unsigned long *padded, unsigned long *premoved) {
    unsigned int nn, add = 1, pos;
    unsigned long added, removed, flag;

    for (added=removed=nn=0; str[nn]; nn++) {
	switch (str[nn]) {
	case '+': add = 1; break;
	case '-': add = 0; break;
	default:
	    if (!(pos = handle_inverse_flags[(unsigned char)str[nn]])) {
		send_message(user, bot, "NSMSG_INVALID_FLAG", str[nn]);
		return 0;
	    }
            if (user && (user->handle_info->opserv_level < flag_access_levels[pos-1])) {
                /* cheesy avoidance of looking up the flag name.. */
                send_message(user, bot, "NSMSG_FLAG_PRIVILEGED", str[nn]);
                return 0;
            }
            flag = 1 << (pos - 1);
	    if (add)
                added |= flag, removed &= ~flag;
	    else
                removed |= flag, added &= ~flag;
	    break;
	}
    }
    *padded = added;
    *premoved = removed;
    return 1;
}

static int
nickserv_apply_flags(struct userNode *user, struct handle_info *hi, const char *flags)
{
    unsigned long before, after, added, removed;
    struct userNode *uNode;

    before = hi->flags & (HI_FLAG_SUPPORT_HELPER|HI_FLAG_NETWORK_HELPER);
    if (!nickserv_modify_handle_flags(user, nickserv, flags, &added, &removed))
        return 0;
    hi->flags = (hi->flags | added) & ~removed;
    after = hi->flags & (HI_FLAG_SUPPORT_HELPER|HI_FLAG_NETWORK_HELPER);

    /* Strip helping flag if they're only a support helper and not
     * currently in #support. */
    if (HANDLE_FLAGGED(hi, HELPING) && (after == HI_FLAG_SUPPORT_HELPER)) {
        struct channelList *schannels;
        unsigned int ii;
        schannels = chanserv_support_channels();
        for (uNode = hi->users; uNode; uNode = uNode->next_authed) {
            for (ii = 0; ii < schannels->used; ++ii)
                if (GetUserMode(schannels->list[ii], uNode))
                    break;
            if (ii < schannels->used)
                break;
        }
        if (!uNode)
            HANDLE_CLEAR_FLAG(hi, HELPING);
    }

    if (after && !before) {
        /* Add user to current helper list. */
        for (uNode = hi->users; uNode; uNode = uNode->next_authed)
            userList_append(&curr_helpers, uNode);
    } else if (!after && before) {
        /* Remove user from current helper list. */
        for (uNode = hi->users; uNode; uNode = uNode->next_authed)
            userList_remove(&curr_helpers, uNode);
    }

    return 1;
}

static void
set_list(struct userNode *user, struct handle_info *hi, int override)
{
    option_func_t *opt;
    unsigned int i;
    char *set_display[] = {
        "INFO", "WIDTH", "TABLEWIDTH", "COLOR", "PRIVMSG", "STYLE",
        "EMAIL", "ANNOUNCEMENTS", "MAXLOGINS", "LANGUAGE"
    };

    send_message(user, nickserv, "NSMSG_SETTING_LIST");

    /* Do this so options are presented in a consistent order. */
    for (i = 0; i < ArrayLength(set_display); ++i)
	if ((opt = dict_find(nickserv_opt_dict, set_display[i], NULL)))
	    opt(user, hi, override, 0, NULL);
}

static NICKSERV_FUNC(cmd_set)
{
    struct handle_info *hi;
    option_func_t *opt;

    hi = user->handle_info;
    if (argc < 2) {
	set_list(user, hi, 0);
	return 1;
    }
    if (!(opt = dict_find(nickserv_opt_dict, argv[1], NULL))) {
	reply("NSMSG_INVALID_OPTION", argv[1]);
        return 0;
    }
    return opt(user, hi, 0, argc-1, argv+1);
}

static NICKSERV_FUNC(cmd_oset)
{
    struct handle_info *hi;
    option_func_t *opt;

    NICKSERV_MIN_PARMS(2);

    if (!(hi = get_victim_oper(user, argv[1])))
        return 0;

    if (argc < 3) {
	set_list(user, hi, 0);
	return 1;
    }

    if (!(opt = dict_find(nickserv_opt_dict, argv[2], NULL))) {
	reply("NSMSG_INVALID_OPTION", argv[2]);
        return 0;
    }

    return opt(user, hi, 1, argc-2, argv+2);
}

static OPTION_FUNC(opt_info)
{
    const char *info;
    if (argc > 1) {
	if ((argv[1][0] == '*') && (argv[1][1] == 0)) {
            free(hi->infoline);
            hi->infoline = NULL;
	} else {
	    hi->infoline = strdup(unsplit_string(argv+1, argc-1, NULL));
	}
    }

    info = hi->infoline ? hi->infoline : user_find_message(user, "MSG_NONE");
    send_message(user, nickserv, "NSMSG_SET_INFO", info);
    return 1;
}

static OPTION_FUNC(opt_width)
{
    if (argc > 1)
	hi->screen_width = strtoul(argv[1], NULL, 0);

    if ((hi->screen_width > 0) && (hi->screen_width < MIN_LINE_SIZE))
        hi->screen_width = MIN_LINE_SIZE;
    else if (hi->screen_width > MAX_LINE_SIZE)
        hi->screen_width = MAX_LINE_SIZE;

    send_message(user, nickserv, "NSMSG_SET_WIDTH", hi->screen_width);
    return 1;
}

static OPTION_FUNC(opt_tablewidth)
{
    if (argc > 1)
	hi->table_width = strtoul(argv[1], NULL, 0);

    if ((hi->table_width > 0) && (hi->table_width < MIN_LINE_SIZE))
        hi->table_width = MIN_LINE_SIZE;
    else if (hi->screen_width > MAX_LINE_SIZE)
        hi->table_width = MAX_LINE_SIZE;

    send_message(user, nickserv, "NSMSG_SET_TABLEWIDTH", hi->table_width);
    return 1;
}

static OPTION_FUNC(opt_color)
{
    if (argc > 1) {
	if (enabled_string(argv[1]))
	    HANDLE_SET_FLAG(hi, MIRC_COLOR);
        else if (disabled_string(argv[1]))
	    HANDLE_CLEAR_FLAG(hi, MIRC_COLOR);
	else {
	    send_message(user, nickserv, "MSG_INVALID_BINARY", argv[1]);
	    return 0;
	}
    }

    send_message(user, nickserv, "NSMSG_SET_COLOR", user_find_message(user, HANDLE_FLAGGED(hi, MIRC_COLOR) ? "MSG_ON" : "MSG_OFF"));
    return 1;
}

static OPTION_FUNC(opt_privmsg)
{
    if (argc > 1) {
	if (enabled_string(argv[1]))
	    HANDLE_SET_FLAG(hi, USE_PRIVMSG);
        else if (disabled_string(argv[1]))
	    HANDLE_CLEAR_FLAG(hi, USE_PRIVMSG);
	else {
	    send_message(user, nickserv, "MSG_INVALID_BINARY", argv[1]);
	    return 0;
	}
    }

    send_message(user, nickserv, "NSMSG_SET_PRIVMSG", user_find_message(user, HANDLE_FLAGGED(hi, USE_PRIVMSG) ? "MSG_ON" : "MSG_OFF"));
    return 1;
}

static OPTION_FUNC(opt_style)
{
    char *style;

    if (argc > 1) {
	if (!irccasecmp(argv[1], "Zoot"))
	    hi->userlist_style = HI_STYLE_ZOOT;
	else if (!irccasecmp(argv[1], "def"))
	    hi->userlist_style = HI_STYLE_DEF;
    }

    switch (hi->userlist_style) {
    case HI_STYLE_DEF:
	style = "def";
	break;
    case HI_STYLE_ZOOT:
    default:
	style = "Zoot";
    }

    send_message(user, nickserv, "NSMSG_SET_STYLE", style);
    return 1;
}

static OPTION_FUNC(opt_announcements)
{
    const char *choice;

    if (argc > 1) {
        if (enabled_string(argv[1]))
            hi->announcements = 'y';
        else if (disabled_string(argv[1]))
            hi->announcements = 'n';
        else if (!strcmp(argv[1], "?") || !irccasecmp(argv[1], "default"))
            hi->announcements = '?';
        else {
            send_message(user, nickserv, "NSMSG_INVALID_ANNOUNCE", argv[1]);
            return 0;
        }
    }

    switch (hi->announcements) {
    case 'y': choice = user_find_message(user, "MSG_ON"); break;
    case 'n': choice = user_find_message(user, "MSG_OFF"); break;
    case '?': choice = "default"; break;
    default: choice = "unknown"; break;
    }
    send_message(user, nickserv, "NSMSG_SET_ANNOUNCEMENTS", choice);
    return 1;
}

static OPTION_FUNC(opt_password)
{
    if (!override) {
	send_message(user, nickserv, "NSMSG_USE_CMD_PASS");
	return 0;
    }

    if (argc > 1)
	cryptpass(argv[1], hi->passwd);

    send_message(user, nickserv, "NSMSG_SET_PASSWORD", "***");
    return 1;
}

static OPTION_FUNC(opt_flags)
{
    char flags[33];
    unsigned int ii, flen;

    if (!override) {
	send_message(user, nickserv, "MSG_SETTING_PRIVILEGED", argv[0]);
	return 0;
    }

    if (argc > 1)
	nickserv_apply_flags(user, hi, argv[1]);

    for (ii = flen = 0; handle_flags[ii]; ii++)
        if (hi->flags & (1 << ii))
            flags[flen++] = handle_flags[ii];
    flags[flen] = '\0';
    if (hi->flags)
        send_message(user, nickserv, "NSMSG_SET_FLAGS", flags);
    else
        send_message(user, nickserv, "NSMSG_SET_FLAGS", user_find_message(user, "MSG_NONE"));
    return 1;
}

static OPTION_FUNC(opt_email)
{
    if (argc > 1) {
        const char *str;
        if (!is_valid_email_addr(argv[1])) {
            send_message(user, nickserv, "NSMSG_BAD_EMAIL_ADDR");
            return 0;
        }
        if ((str = sendmail_prohibited_address(argv[1]))) {
            send_message(user, nickserv, "NSMSG_EMAIL_PROHIBITED", argv[1], str);
            return 0;
        }
        if (hi->email_addr && !irccasecmp(hi->email_addr, argv[1]))
            send_message(user, nickserv, "NSMSG_EMAIL_SAME");
        else if (!override)
                nickserv_make_cookie(user, hi, EMAIL_CHANGE, argv[1]);
        else {
            nickserv_set_email_addr(hi, argv[1]);
            if (hi->cookie)
                nickserv_eat_cookie(hi->cookie);
            send_message(user, nickserv, "NSMSG_SET_EMAIL", visible_email_addr(user, hi));
        }
    } else
        send_message(user, nickserv, "NSMSG_SET_EMAIL", visible_email_addr(user, hi));
    return 1;
}

static OPTION_FUNC(opt_maxlogins)
{
    unsigned char maxlogins;
    if (argc > 1) {
        maxlogins = strtoul(argv[1], NULL, 0);
        if ((maxlogins > nickserv_conf.hard_maxlogins) && !override) {
            send_message(user, nickserv, "NSMSG_BAD_MAX_LOGINS", nickserv_conf.hard_maxlogins);
            return 0;
        }
        hi->maxlogins = maxlogins;
    }
    maxlogins = hi->maxlogins ? hi->maxlogins : nickserv_conf.default_maxlogins;
    send_message(user, nickserv, "NSMSG_SET_MAXLOGINS", maxlogins);
    return 1;
}

static OPTION_FUNC(opt_language)
{
    struct language *lang;
    if (argc > 1) {
        lang = language_find(argv[1]);
        if (irccasecmp(lang->name, argv[1]))
            send_message(user, nickserv, "NSMSG_LANGUAGE_NOT_FOUND", argv[1], lang->name);
        hi->language = lang;
    }
    send_message(user, nickserv, "NSMSG_SET_LANGUAGE", hi->language->name);
    return 1;
}

int
oper_try_set_access(struct userNode *user, struct userNode *bot, struct handle_info *target, unsigned int new_level) {
    if (!oper_has_access(user, bot, nickserv_conf.modoper_level, 0))
        return 0;
    if ((user->handle_info->opserv_level < target->opserv_level)
        || ((user->handle_info->opserv_level == target->opserv_level)
            && (user->handle_info->opserv_level < 1000))) {
        send_message(user, bot, "MSG_USER_OUTRANKED", target->handle);
        return 0;
    }
    if ((user->handle_info->opserv_level < new_level)
        || ((user->handle_info->opserv_level == new_level)
            && (user->handle_info->opserv_level < 1000))) {
        send_message(user, bot, "NSMSG_OPSERV_LEVEL_BAD");
        return 0;
    }
    if (user->handle_info == target) {
        send_message(user, bot, "MSG_STUPID_ACCESS_CHANGE");
        return 0;
    }
    if (target->opserv_level == new_level)
        return 0;
    log_module(NS_LOG, LOG_INFO, "Account %s setting oper level for account %s to %d (from %d).",
        user->handle_info->handle, target->handle, new_level, target->opserv_level);
    target->opserv_level = new_level;
    return 1;
}

static OPTION_FUNC(opt_level)
{
    int res;

    if (!override) {
	send_message(user, nickserv, "MSG_SETTING_PRIVILEGED", argv[0]);
	return 0;
    }

    res = (argc > 1) ? oper_try_set_access(user, nickserv, hi, strtoul(argv[1], NULL, 0)) : 0;
    send_message(user, nickserv, "NSMSG_SET_LEVEL", hi->opserv_level);
    return res;
}

static OPTION_FUNC(opt_epithet)
{
    if (!override) {
        send_message(user, nickserv, "MSG_SETTING_PRIVILEGED", argv[0]);
        return 0;
    }

    if ((argc > 1) && oper_has_access(user, nickserv, nickserv_conf.set_epithet_level, 0)) {
        char *epithet = unsplit_string(argv+1, argc-1, NULL);
        if (hi->epithet)
            free(hi->epithet);
        if ((epithet[0] == '*') && !epithet[1])
            hi->epithet = NULL;
        else
            hi->epithet = strdup(epithet);
    }

    if (hi->epithet)
        send_message(user, nickserv, "NSMSG_SET_EPITHET", hi->epithet);
    else
        send_message(user, nickserv, "NSMSG_SET_EPITHET", user_find_message(user, "MSG_NONE"));
    return 1;
}

static OPTION_FUNC(opt_title)
{
    const char *title;

    if (!override) {
        send_message(user, nickserv, "MSG_SETTING_PRIVILEGED", argv[0]);
        return 0;
    }

    if ((argc > 1) && oper_has_access(user, nickserv, nickserv_conf.set_title_level, 0)) {
        title = argv[1];
        if (strchr(title, '.')) {
            send_message(user, nickserv, "NSMSG_TITLE_INVALID");
            return 0;
        }
        if ((strlen(user->handle_info->handle) + strlen(title) +
             strlen(nickserv_conf.titlehost_suffix) + 2) > HOSTLEN) {
            send_message(user, nickserv, "NSMSG_TITLE_TRUNCATED");
            return 0;
        }

        free(hi->fakehost);
        if (!strcmp(title, "*")) {
            hi->fakehost = NULL;
        } else {
            hi->fakehost = malloc(strlen(title)+2);
            hi->fakehost[0] = '.';
            strcpy(hi->fakehost+1, title);
        }
        apply_fakehost(hi);
    } else if (hi->fakehost && (hi->fakehost[0] == '.'))
        title = hi->fakehost + 1;
    else
        title = NULL;
    if (!title)
        title = user_find_message(user, "MSG_NONE");
    send_message(user, nickserv, "NSMSG_SET_TITLE", title);
    return 1;
}

static OPTION_FUNC(opt_fakehost)
{
    const char *fake;

    if (!override) {
        send_message(user, nickserv, "MSG_SETTING_PRIVILEGED", argv[0]);
        return 0;
    }

    if ((argc > 1) && oper_has_access(user, nickserv, nickserv_conf.set_fakehost_level, 0)) {
        fake = argv[1];
        if ((strlen(fake) > HOSTLEN) || (fake[0] == '.')) {
            send_message(user, nickserv, "NSMSG_FAKEHOST_INVALID", HOSTLEN);
            return 0;
        }
        free(hi->fakehost);
        if (!strcmp(fake, "*"))
            hi->fakehost = NULL;
        else
            hi->fakehost = strdup(fake);
        fake = hi->fakehost;
        apply_fakehost(hi);
    } else
        fake = generate_fakehost(hi);
    if (!fake)
        fake = user_find_message(user, "MSG_NONE");
    send_message(user, nickserv, "NSMSG_SET_FAKEHOST", fake);
    return 1;
}

static NICKSERV_FUNC(cmd_reclaim)
{
    struct handle_info *hi;
    struct nick_info *ni;
    struct userNode *victim;

    NICKSERV_MIN_PARMS(2);
    hi = user->handle_info;
    ni = dict_find(nickserv_nick_dict, argv[1], 0);
    if (!ni) {
        reply("NSMSG_UNKNOWN_NICK", argv[1]);
        return 0;
    }
    if (ni->owner != user->handle_info) {
        reply("NSMSG_NOT_YOUR_NICK", ni->nick);
        return 0;
    }
    victim = GetUserH(ni->nick);
    if (!victim) {
        reply("MSG_NICK_UNKNOWN", ni->nick);
        return 0;
    }
    if (victim == user) {
        reply("NSMSG_NICK_USER_YOU");
        return 0;
    }
    nickserv_reclaim(victim, ni, nickserv_conf.reclaim_action);
    switch (nickserv_conf.reclaim_action) {
    case RECLAIM_NONE: reply("NSMSG_RECLAIMED_NONE"); break;
    case RECLAIM_WARN: reply("NSMSG_RECLAIMED_WARN", victim->nick); break;
    case RECLAIM_SVSNICK: reply("NSMSG_RECLAIMED_SVSNICK", victim->nick); break;
    case RECLAIM_KILL: reply("NSMSG_RECLAIMED_KILL", victim->nick); break;
    }
    return 1;
}

static NICKSERV_FUNC(cmd_unregnick)
{
    const char *nick;
    struct handle_info *hi;
    struct nick_info *ni;

    hi = user->handle_info;
    nick = (argc < 2) ? user->nick : (const char*)argv[1];
    ni = dict_find(nickserv_nick_dict, nick, NULL);
    if (!ni) {
	reply("NSMSG_UNKNOWN_NICK", nick);
	return 0;
    }
    if (hi != ni->owner) {
	reply("NSMSG_NOT_YOUR_NICK", nick);
	return 0;
    }
    reply("NSMSG_UNREGNICK_SUCCESS", ni->nick);
    delete_nick(ni);
    return 1;
}

static NICKSERV_FUNC(cmd_ounregnick)
{
    struct nick_info *ni;

    NICKSERV_MIN_PARMS(2);
    if (!(ni = get_nick_info(argv[1]))) {
	reply("NSMSG_NICK_NOT_REGISTERED", argv[1]);
	return 0;
    }
    if (ni->owner->opserv_level >= user->handle_info->opserv_level) {
	reply("MSG_USER_OUTRANKED", ni->nick);
	return 0;
    }
    reply("NSMSG_UNREGNICK_SUCCESS", ni->nick);
    delete_nick(ni);
    return 1;
}

static NICKSERV_FUNC(cmd_unregister)
{
    struct handle_info *hi;
    char *passwd;

    NICKSERV_MIN_PARMS(2);
    hi = user->handle_info;
    passwd = argv[1];
    argv[1] = "****";
    if (checkpass(passwd, hi->passwd)) {
        nickserv_unregister_handle(hi, user);
        return 1;
    } else {
	log_module(NS_LOG, LOG_INFO, "Account '%s' tried to unregister with the wrong password.", hi->handle);
	reply("NSMSG_PASSWORD_INVALID");
        return 0;
    }
}

static NICKSERV_FUNC(cmd_ounregister)
{
    struct handle_info *hi;

    NICKSERV_MIN_PARMS(2);
    if (!(hi = get_victim_oper(user, argv[1])))
        return 0;
    nickserv_unregister_handle(hi, user);
    return 1;
}

static NICKSERV_FUNC(cmd_status)
{
    if (nickserv_conf.disable_nicks) {
        reply("NSMSG_GLOBAL_STATS_NONICK",
                        dict_size(nickserv_handle_dict));
    } else {
        if (user->handle_info) {
            int cnt=0;
            struct nick_info *ni;
            for (ni=user->handle_info->nicks; ni; ni=ni->next) cnt++;
            reply("NSMSG_HANDLE_STATS", cnt);
        } else {
            reply("NSMSG_HANDLE_NONE");
        }
        reply("NSMSG_GLOBAL_STATS",
              dict_size(nickserv_handle_dict),
              dict_size(nickserv_nick_dict));
    }
    return 1;
}

static NICKSERV_FUNC(cmd_ghost)
{
    struct userNode *target;
    char reason[MAXLEN];

    NICKSERV_MIN_PARMS(2);
    if (!(target = GetUserH(argv[1]))) {
        reply("MSG_NICK_UNKNOWN", argv[1]);
        return 0;
    }
    if (target == user) {
        reply("NSMSG_CANNOT_GHOST_SELF");
        return 0;
    }
    if (!target->handle_info || (target->handle_info != user->handle_info)) {
        reply("NSMSG_CANNOT_GHOST_USER", target->nick);
        return 0;
    }
    snprintf(reason, sizeof(reason), "Ghost kill on account %s (requested by %s).", target->handle_info->handle, user->nick);
    DelUser(target, nickserv, 1, reason);
    reply("NSMSG_GHOST_KILLED", argv[1]);
    return 1;
}

static NICKSERV_FUNC(cmd_vacation)
{
    HANDLE_SET_FLAG(user->handle_info, FROZEN);
    reply("NSMSG_ON_VACATION");
    return 1;
}

static int
nickserv_saxdb_write(struct saxdb_context *ctx) {
    dict_iterator_t it;
    struct handle_info *hi;
    char flags[33];

    for (it = dict_first(nickserv_handle_dict); it; it = iter_next(it)) {
        hi = iter_data(it);
#ifdef WITH_PROTOCOL_BAHAMUT
        assert(hi->id);
#endif
        saxdb_start_record(ctx, iter_key(it), 0);
        if (hi->announcements != '?') {
            flags[0] = hi->announcements;
            flags[1] = 0;
            saxdb_write_string(ctx, KEY_ANNOUNCEMENTS, flags);
        }
        if (hi->cookie) {
            struct handle_cookie *cookie = hi->cookie;
            char *type;

            switch (cookie->type) {
            case ACTIVATION: type = KEY_ACTIVATION; break;
            case PASSWORD_CHANGE: type = KEY_PASSWORD_CHANGE; break;
            case EMAIL_CHANGE: type = KEY_EMAIL_CHANGE; break;
            case ALLOWAUTH: type = KEY_ALLOWAUTH; break;
            default: type = NULL; break;
            }
            if (type) {
                saxdb_start_record(ctx, KEY_COOKIE, 0);
                saxdb_write_string(ctx, KEY_COOKIE_TYPE, type);
                saxdb_write_int(ctx, KEY_COOKIE_EXPIRES, cookie->expires);
                if (cookie->data)
                    saxdb_write_string(ctx, KEY_COOKIE_DATA, cookie->data);
                saxdb_write_string(ctx, KEY_COOKIE, cookie->cookie);
                saxdb_end_record(ctx);
            }
        }
        if (hi->email_addr)
            saxdb_write_string(ctx, KEY_EMAIL_ADDR, hi->email_addr);
        if (hi->epithet)
            saxdb_write_string(ctx, KEY_EPITHET, hi->epithet);
        if (hi->fakehost)
            saxdb_write_string(ctx, KEY_FAKEHOST, hi->fakehost);
        if (hi->flags) {
            int ii, flen;

            for (ii=flen=0; handle_flags[ii]; ++ii)
                if (hi->flags & (1 << ii))
                    flags[flen++] = handle_flags[ii];
            flags[flen] = 0;
            saxdb_write_string(ctx, KEY_FLAGS, flags);
        }
#ifdef WITH_PROTOCOL_BAHAMUT
        saxdb_write_int(ctx, KEY_ID, hi->id);
#endif
        if (hi->infoline)
            saxdb_write_string(ctx, KEY_INFO, hi->infoline);
        if (hi->last_quit_host[0])
            saxdb_write_string(ctx, KEY_LAST_QUIT_HOST, hi->last_quit_host);
        saxdb_write_int(ctx, KEY_LAST_SEEN, hi->lastseen);
        if (hi->masks->used)
            saxdb_write_string_list(ctx, KEY_MASKS, hi->masks);
        if (hi->maxlogins)
            saxdb_write_int(ctx, KEY_MAXLOGINS, hi->maxlogins);
        if (hi->nicks) {
            struct string_list *slist;
            struct nick_info *ni;

            slist = alloc_string_list(nickserv_conf.nicks_per_handle);
            for (ni = hi->nicks; ni; ni = ni->next) string_list_append(slist, ni->nick);
            saxdb_write_string_list(ctx, KEY_NICKS, slist);
            free(slist->list);
            free(slist);
        }
        if (hi->opserv_level)
            saxdb_write_int(ctx, KEY_OPSERV_LEVEL, hi->opserv_level);
        if (hi->language != lang_C)
            saxdb_write_string(ctx, KEY_LANGUAGE, hi->language->name);
        saxdb_write_string(ctx, KEY_PASSWD, hi->passwd);
        saxdb_write_int(ctx, KEY_REGISTER_ON, hi->registered);
        if (hi->screen_width)
            saxdb_write_int(ctx, KEY_SCREEN_WIDTH, hi->screen_width);
        if (hi->table_width)
            saxdb_write_int(ctx, KEY_TABLE_WIDTH, hi->table_width);
        flags[0] = hi->userlist_style;
        flags[1] = 0;
        saxdb_write_string(ctx, KEY_USERLIST_STYLE, flags);
        saxdb_end_record(ctx);
    }
    return 0;
}

static handle_merge_func_t *handle_merge_func_list;
static unsigned int handle_merge_func_size = 0, handle_merge_func_used = 0;

void
reg_handle_merge_func(handle_merge_func_t func)
{
    if (handle_merge_func_used == handle_merge_func_size) {
        if (handle_merge_func_size) {
            handle_merge_func_size <<= 1;
            handle_merge_func_list = realloc(handle_merge_func_list, handle_merge_func_size*sizeof(handle_merge_func_t));
        } else {
            handle_merge_func_size = 8;
            handle_merge_func_list = malloc(handle_merge_func_size*sizeof(handle_merge_func_t));
        }
    }
    handle_merge_func_list[handle_merge_func_used++] = func;
}

static NICKSERV_FUNC(cmd_merge)
{
    struct handle_info *hi_from, *hi_to;
    struct userNode *last_user;
    struct userData *cList, *cListNext;
    unsigned int ii, jj, n;
    char buffer[MAXLEN];

    NICKSERV_MIN_PARMS(3);

    if (!(hi_from = get_victim_oper(user, argv[1])))
        return 0;
    if (!(hi_to = get_victim_oper(user, argv[2])))
        return 0;
    if (hi_to == hi_from) {
        reply("NSMSG_CANNOT_MERGE_SELF", hi_to->handle);
        return 0;
    }

    for (n=0; n<handle_merge_func_used; n++)
        handle_merge_func_list[n](user, hi_to, hi_from);

    /* Append "from" handle's nicks to "to" handle's nick list. */
    if (hi_to->nicks) {
        struct nick_info *last_ni;
        for (last_ni=hi_to->nicks; last_ni->next; last_ni=last_ni->next) ;
        last_ni->next = hi_from->nicks;
    }
    while (hi_from->nicks) {
        hi_from->nicks->owner = hi_to;
        hi_from->nicks = hi_from->nicks->next;
    }

    /* Merge the hostmasks. */
    for (ii=0; ii<hi_from->masks->used; ii++) {
        char *mask = hi_from->masks->list[ii];
        for (jj=0; jj<hi_to->masks->used; jj++)
            if (match_ircglobs(hi_to->masks->list[jj], mask))
                break;
        if (jj==hi_to->masks->used) /* Nothing from the "to" handle covered this mask, so add it. */
            string_list_append(hi_to->masks, strdup(mask));
    }

    /* Merge the lists of authed users. */
    if (hi_to->users) {
        for (last_user=hi_to->users; last_user->next_authed; last_user=last_user->next_authed) ;
        last_user->next_authed = hi_from->users;
    } else {
        hi_to->users = hi_from->users;
    }
    /* Repoint the old "from" handle's users. */
    for (last_user=hi_from->users; last_user; last_user=last_user->next_authed) {
        last_user->handle_info = hi_to;
    }
    hi_from->users = NULL;

    /* Merge channel userlists. */
    for (cList=hi_from->channels; cList; cList=cListNext) {
        struct userData *cList2;
        cListNext = cList->u_next;
        for (cList2=hi_to->channels; cList2; cList2=cList2->u_next)
            if (cList->channel == cList2->channel)
                break;
        if (cList2 && (cList2->access >= cList->access)) {
            log_module(NS_LOG, LOG_INFO, "Merge: %s had only %d access in %s (versus %d for %s)", hi_from->handle, cList->access, cList->channel->channel->name, cList2->access, hi_to->handle);
            /* keep cList2 in hi_to; remove cList from hi_from */
            del_channel_user(cList, 1);
        } else {
            if (cList2) {
                log_module(NS_LOG, LOG_INFO, "Merge: %s had only %d access in %s (versus %d for %s)", hi_to->handle, cList2->access, cList->channel->channel->name, cList->access, hi_from->handle);
                /* remove the lower-ranking cList2 from hi_to */
                del_channel_user(cList2, 1);
            } else {
                log_module(NS_LOG, LOG_INFO, "Merge: %s had no access in %s", hi_to->handle, cList->channel->channel->name);
            }
            /* cList needs to be moved from hi_from to hi_to */
            cList->handle = hi_to;
            /* Remove from linked list for hi_from */
            assert(!cList->u_prev);
            hi_from->channels = cList->u_next;
            if (cList->u_next)
                cList->u_next->u_prev = cList->u_prev;
            /* Add to linked list for hi_to */
            cList->u_prev = NULL;
            cList->u_next = hi_to->channels;
            if (hi_to->channels)
                hi_to->channels->u_prev = cList;
            hi_to->channels = cList;
        }
    }

    /* Do they get an OpServ level promotion? */
    if (hi_from->opserv_level > hi_to->opserv_level)
        hi_to->opserv_level = hi_from->opserv_level;

    /* What about last seen time? */
    if (hi_from->lastseen > hi_to->lastseen)
        hi_to->lastseen = hi_from->lastseen;

    /* Notify of success. */
    sprintf(buffer, "%s (%s) merged account %s into %s.", user->nick, user->handle_info->handle, hi_from->handle, hi_to->handle);
    reply("NSMSG_HANDLES_MERGED", hi_from->handle, hi_to->handle);
    global_message(MESSAGE_RECIPIENT_STAFF, buffer);

    /* Unregister the "from" handle. */
    nickserv_unregister_handle(hi_from, NULL);

    return 1;
}

struct nickserv_discrim {
    unsigned int limit, min_level, max_level;
    unsigned long flags_on, flags_off;
    time_t min_registered, max_registered;
    time_t lastseen;
    enum { SUBSET, EXACT, SUPERSET, LASTQUIT } hostmask_type;
    const char *nickmask;
    const char *hostmask;
    const char *handlemask;
    const char *emailmask;
};

typedef void (*discrim_search_func)(struct userNode *source, struct handle_info *hi);

struct discrim_apply_info {
    struct nickserv_discrim *discrim;
    discrim_search_func func;
    struct userNode *source;
    unsigned int matched;
};

static struct nickserv_discrim *
nickserv_discrim_create(struct userNode *user, unsigned int argc, char *argv[])
{
    unsigned int i;
    struct nickserv_discrim *discrim;

    discrim = malloc(sizeof(*discrim));
    memset(discrim, 0, sizeof(*discrim));
    discrim->min_level = 0;
    discrim->max_level = ~0;
    discrim->limit = 50;
    discrim->min_registered = 0;
    discrim->max_registered = INT_MAX;
    discrim->lastseen = now;

    for (i=0; i<argc; i++) {
        if (i == argc - 1) {
            send_message(user, nickserv, "MSG_MISSING_PARAMS", argv[i]);
            goto fail;
        }
        if (!irccasecmp(argv[i], "limit")) {
            discrim->limit = strtoul(argv[++i], NULL, 0);
        } else if (!irccasecmp(argv[i], "flags")) {
            nickserv_modify_handle_flags(user, nickserv, argv[++i], &discrim->flags_on, &discrim->flags_off);
        } else if (!irccasecmp(argv[i], "registered")) {
            const char *cmp = argv[++i];
            if (cmp[0] == '<') {
                if (cmp[1] == '=') {
                    discrim->min_registered = now - ParseInterval(cmp+2);
                } else {
                    discrim->min_registered = now - ParseInterval(cmp+1) + 1;
                }
            } else if (cmp[0] == '=') {
                discrim->min_registered = discrim->max_registered = now - ParseInterval(cmp+1);
            } else if (cmp[0] == '>') {
                if (cmp[1] == '=') {
                    discrim->max_registered = now - ParseInterval(cmp+2);
                } else {
                    discrim->max_registered = now - ParseInterval(cmp+1) - 1;
                }
            } else {
                send_message(user, nickserv, "MSG_INVALID_CRITERIA", cmp);
            }
        } else if (!irccasecmp(argv[i], "seen")) {
            discrim->lastseen = now - ParseInterval(argv[++i]);
        } else if (!nickserv_conf.disable_nicks && !irccasecmp(argv[i], "nickmask")) {
            discrim->nickmask = argv[++i];
        } else if (!irccasecmp(argv[i], "hostmask")) {
            i++;
            if (!irccasecmp(argv[i], "exact")) {
                if (i == argc - 1) {
                    send_message(user, nickserv, "MSG_MISSING_PARAMS", argv[i]);
                    goto fail;
                }
                discrim->hostmask_type = EXACT;
            } else if (!irccasecmp(argv[i], "subset")) {
                if (i == argc - 1) {
                    send_message(user, nickserv, "MSG_MISSING_PARAMS", argv[i]);
                    goto fail;
                }
                discrim->hostmask_type = SUBSET;
            } else if (!irccasecmp(argv[i], "superset")) {
                if (i == argc - 1) {
                    send_message(user, nickserv, "MSG_MISSING_PARAMS", argv[i]);
                    goto fail;
                }
                discrim->hostmask_type = SUPERSET;
	    } else if (!irccasecmp(argv[i], "lastquit") || !irccasecmp(argv[i], "lastauth")) {
	       if (i == argc - 1) {
	           send_message(user, nickserv, "MSG_MISSING_PARAMS", argv[i]);
		   goto fail;
	       }
	       discrim->hostmask_type = LASTQUIT;
            } else {
                i--;
                discrim->hostmask_type = SUPERSET;
            }
            discrim->hostmask = argv[++i];
        } else if (!irccasecmp(argv[i], "handlemask") || !irccasecmp(argv[i], "accountmask")) {
            if (!irccasecmp(argv[++i], "*")) {
                discrim->handlemask = 0;
            } else {
                discrim->handlemask = argv[i];
            }
        } else if (!irccasecmp(argv[i], "email")) {
            if (user->handle_info->opserv_level < nickserv_conf.email_search_level) {
                send_message(user, nickserv, "MSG_NO_SEARCH_ACCESS", "email");
                goto fail;
            } else if (!irccasecmp(argv[++i], "*")) {
                discrim->emailmask = 0;
            } else {
                discrim->emailmask = argv[i];
            }
        } else if (!irccasecmp(argv[i], "access")) {
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
                send_message(user, nickserv, "MSG_INVALID_CRITERIA", cmp);
            }
        } else {
            send_message(user, nickserv, "MSG_INVALID_CRITERIA", argv[i]);
            goto fail;
        }
    }
    return discrim;
  fail:
    free(discrim);
    return NULL;
}

static int
nickserv_discrim_match(struct nickserv_discrim *discrim, struct handle_info *hi)
{
    if (((discrim->flags_on & hi->flags) != discrim->flags_on)
        || (discrim->flags_off & hi->flags)
        || (discrim->min_registered > hi->registered)
        || (discrim->max_registered < hi->registered)
        || (discrim->lastseen < (hi->users?now:hi->lastseen))
        || (discrim->handlemask && !match_ircglob(hi->handle, discrim->handlemask))
        || (discrim->emailmask && (!hi->email_addr || !match_ircglob(hi->email_addr, discrim->emailmask)))
        || (discrim->min_level > hi->opserv_level)
        || (discrim->max_level < hi->opserv_level)) {
        return 0;
    }
    if (discrim->hostmask) {
        unsigned int i;
        for (i=0; i<hi->masks->used; i++) {
            const char *mask = hi->masks->list[i];
            if ((discrim->hostmask_type == SUBSET)
                && (match_ircglobs(discrim->hostmask, mask))) break;
            else if ((discrim->hostmask_type == EXACT)
                     && !irccasecmp(discrim->hostmask, mask)) break;
            else if ((discrim->hostmask_type == SUPERSET)
                     && (match_ircglobs(mask, discrim->hostmask))) break;
	    else if ((discrim->hostmask_type == LASTQUIT)
	    	     && (match_ircglobs(discrim->hostmask, hi->last_quit_host))) break;
        }
        if (i==hi->masks->used) return 0;
    }
    if (discrim->nickmask) {
        struct nick_info *nick = hi->nicks;
        while (nick) {
            if (match_ircglob(nick->nick, discrim->nickmask)) break;
            nick = nick->next;
        }
        if (!nick) return 0;
    }
    return 1;
}

static unsigned int
nickserv_discrim_search(struct nickserv_discrim *discrim, discrim_search_func dsf, struct userNode *source)
{
    dict_iterator_t it, next;
    unsigned int matched;

    for (it = dict_first(nickserv_handle_dict), matched = 0;
         it && (matched < discrim->limit);
         it = next) {
        next = iter_next(it);
        if (nickserv_discrim_match(discrim, iter_data(it))) {
            dsf(source, iter_data(it));
            matched++;
        }
    }
    return matched;
}

static void
search_print_func(struct userNode *source, struct handle_info *match)
{
    send_message(source, nickserv, "NSMSG_SEARCH_MATCH", match->handle);
}

static void
search_count_func(UNUSED_ARG(struct userNode *source), UNUSED_ARG(struct handle_info *match))
{
}

static void
search_unregister_func (struct userNode *source, struct handle_info *match)
{
    if (oper_has_access(source, nickserv, match->opserv_level, 0))
        nickserv_unregister_handle(match, source);
}

static int
nickserv_sort_accounts_by_access(const void *a, const void *b)
{
    const struct handle_info *hi_a = *(const struct handle_info**)a;
    const struct handle_info *hi_b = *(const struct handle_info**)b;
    if (hi_a->opserv_level != hi_b->opserv_level)
        return hi_b->opserv_level - hi_a->opserv_level;
    return irccasecmp(hi_a->handle, hi_b->handle);
}

void
nickserv_show_oper_accounts(struct userNode *user, struct svccmd *cmd)
{
    struct handle_info_list hil;
    struct helpfile_table tbl;
    unsigned int ii;
    dict_iterator_t it;
    const char **ary;

    memset(&hil, 0, sizeof(hil));
    for (it = dict_first(nickserv_handle_dict); it; it = iter_next(it)) {
        struct handle_info *hi = iter_data(it);
        if (hi->opserv_level)
            handle_info_list_append(&hil, hi);
    }
    qsort(hil.list, hil.used, sizeof(hil.list[0]), nickserv_sort_accounts_by_access);
    tbl.length = hil.used + 1;
    tbl.width = 2;
    tbl.flags = TABLE_NO_FREE;
    tbl.contents = malloc(tbl.length * sizeof(tbl.contents[0]));
    tbl.contents[0] = ary = malloc(tbl.width * sizeof(ary[0]));
    ary[0] = "Account";
    ary[1] = "Level";
    for (ii = 0; ii < hil.used; ) {
        ary = malloc(tbl.width * sizeof(ary[0]));
        ary[0] = hil.list[ii]->handle;
        ary[1] = strtab(hil.list[ii]->opserv_level);
        tbl.contents[++ii] = ary;
    }
    table_send(cmd->parent->bot, user->nick, 0, NULL, tbl);
    reply("MSG_MATCH_COUNT", hil.used);
    for (ii = 0; ii < hil.used; ii++)
        free(tbl.contents[ii]);
    free(tbl.contents);
    free(hil.list);
}

static NICKSERV_FUNC(cmd_search)
{
    struct nickserv_discrim *discrim;
    discrim_search_func action;
    struct svccmd *subcmd;
    unsigned int matches;
    char buf[MAXLEN];

    NICKSERV_MIN_PARMS(3);
    sprintf(buf, "search %s", argv[1]);
    subcmd = dict_find(nickserv_service->commands, buf, NULL);
    if (!irccasecmp(argv[1], "print"))
        action = search_print_func;
    else if (!irccasecmp(argv[1], "count"))
        action = search_count_func;
    else if (!irccasecmp(argv[1], "unregister"))
        action = search_unregister_func;
    else {
        reply("NSMSG_INVALID_ACTION", argv[1]);
        return 0;
    }

    if (subcmd && !svccmd_can_invoke(user, nickserv, subcmd, NULL, SVCCMD_NOISY))
        return 0;

    discrim = nickserv_discrim_create(user, argc-2, argv+2);
    if (!discrim)
        return 0;

    if (action == search_print_func)
        reply("NSMSG_ACCOUNT_SEARCH_RESULTS");
    else if (action == search_count_func)
        discrim->limit = INT_MAX;

    matches = nickserv_discrim_search(discrim, action, user);

    if (matches)
        reply("MSG_MATCH_COUNT", matches);
    else
        reply("MSG_NO_MATCHES");

    free(discrim);
    return 0;
}

static MODCMD_FUNC(cmd_checkpass)
{
    struct handle_info *hi;

    NICKSERV_MIN_PARMS(3);
    if (!(hi = get_handle_info(argv[1]))) {
        reply("MSG_HANDLE_UNKNOWN", argv[1]);
        return 0;
    }
    if (checkpass(argv[2], hi->passwd))
        reply("CHECKPASS_YES");
    else
        reply("CHECKPASS_NO");
    argv[2] = "****";
    return 1;
}

static void
nickserv_db_read_handle(const char *handle, dict_t obj)
{
    const char *str;
    struct string_list *masks, *slist;
    struct handle_info *hi;
    struct userNode *authed_users;
    unsigned long int id;
    unsigned int ii;
    dict_t subdb;

    str = database_get_data(obj, KEY_ID, RECDB_QSTRING);
    id = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(obj, KEY_PASSWD, RECDB_QSTRING);
    if (!str) {
        log_module(NS_LOG, LOG_WARNING, "did not find a password for %s -- skipping user.", handle);
        return;
    }
    if ((hi = get_handle_info(handle))) {
        authed_users = hi->users;
        hi->users = NULL;
        dict_remove(nickserv_handle_dict, hi->handle);
    } else {
        authed_users = NULL;
    }
    hi = register_handle(handle, str, id);
    if (authed_users) {
        hi->users = authed_users;
        while (authed_users) {
            authed_users->handle_info = hi;
            authed_users = authed_users->next_authed;
        }
    }
    masks = database_get_data(obj, KEY_MASKS, RECDB_STRING_LIST);
    hi->masks = masks ? string_list_copy(masks) : alloc_string_list(1);
    str = database_get_data(obj, KEY_MAXLOGINS, RECDB_QSTRING);
    hi->maxlogins = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(obj, KEY_LANGUAGE, RECDB_QSTRING);
    hi->language = language_find(str ? str : "C");
    str = database_get_data(obj, KEY_OPSERV_LEVEL, RECDB_QSTRING);
    hi->opserv_level = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(obj, KEY_INFO, RECDB_QSTRING);
    if (str)
        hi->infoline = strdup(str);
    str = database_get_data(obj, KEY_REGISTER_ON, RECDB_QSTRING);
    hi->registered = str ? (time_t)strtoul(str, NULL, 0) : now;
    str = database_get_data(obj, KEY_LAST_SEEN, RECDB_QSTRING);
    hi->lastseen = str ? (time_t)strtoul(str, NULL, 0) : hi->registered;
    /* We want to read the nicks even if disable_nicks is set.  This is so
     * that we don't lose the nick data entirely. */
    slist = database_get_data(obj, KEY_NICKS, RECDB_STRING_LIST);
    if (slist) {
        for (ii=0; ii<slist->used; ii++)
            register_nick(slist->list[ii], hi);
    }
    str = database_get_data(obj, KEY_FLAGS, RECDB_QSTRING);
    if (str) {
        for (ii=0; str[ii]; ii++)
            hi->flags |= 1 << (handle_inverse_flags[(unsigned char)str[ii]] - 1);
    }
    str = database_get_data(obj, KEY_USERLIST_STYLE, RECDB_QSTRING);
    hi->userlist_style = str ? str[0] : HI_STYLE_ZOOT;
    str = database_get_data(obj, KEY_ANNOUNCEMENTS, RECDB_QSTRING);
    hi->announcements = str ? str[0] : '?';
    str = database_get_data(obj, KEY_SCREEN_WIDTH, RECDB_QSTRING);
    hi->screen_width = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(obj, KEY_TABLE_WIDTH, RECDB_QSTRING);
    hi->table_width = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(obj, KEY_LAST_QUIT_HOST, RECDB_QSTRING);
    if (!str)
        str = database_get_data(obj, KEY_LAST_AUTHED_HOST, RECDB_QSTRING);
    if (str)
        safestrncpy(hi->last_quit_host, str, sizeof(hi->last_quit_host));
    str = database_get_data(obj, KEY_EMAIL_ADDR, RECDB_QSTRING);
    if (str)
        nickserv_set_email_addr(hi, str);
    str = database_get_data(obj, KEY_EPITHET, RECDB_QSTRING);
    if (str)
        hi->epithet = strdup(str);
    str = database_get_data(obj, KEY_FAKEHOST, RECDB_QSTRING);
    if (str)
        hi->fakehost = strdup(str);
    subdb = database_get_data(obj, KEY_COOKIE, RECDB_OBJECT);
    if (subdb) {
        const char *data, *type, *expires, *cookie_str;
        struct handle_cookie *cookie;

        cookie = calloc(1, sizeof(*cookie));
        type = database_get_data(subdb, KEY_COOKIE_TYPE, RECDB_QSTRING);
        data = database_get_data(subdb, KEY_COOKIE_DATA, RECDB_QSTRING);
        expires = database_get_data(subdb, KEY_COOKIE_EXPIRES, RECDB_QSTRING);
        cookie_str = database_get_data(subdb, KEY_COOKIE, RECDB_QSTRING);
        if (!type || !expires || !cookie_str) {
            log_module(NS_LOG, LOG_ERROR, "Missing field(s) from cookie for account %s; dropping cookie.", hi->handle);
            goto cookie_out;
        }
        if (!irccasecmp(type, KEY_ACTIVATION))
            cookie->type = ACTIVATION;
        else if (!irccasecmp(type, KEY_PASSWORD_CHANGE))
            cookie->type = PASSWORD_CHANGE;
        else if (!irccasecmp(type, KEY_EMAIL_CHANGE))
            cookie->type = EMAIL_CHANGE;
        else if (!irccasecmp(type, KEY_ALLOWAUTH))
            cookie->type = ALLOWAUTH;
        else {
            log_module(NS_LOG, LOG_ERROR, "Invalid cookie type %s for account %s; dropping cookie.", type, handle);
            goto cookie_out;
        }
        cookie->expires = strtoul(expires, NULL, 0);
        if (cookie->expires < now)
            goto cookie_out;
        if (data)
            cookie->data = strdup(data);
        safestrncpy(cookie->cookie, cookie_str, sizeof(cookie->cookie));
        cookie->hi = hi;
      cookie_out:
        if (cookie->hi)
            nickserv_bake_cookie(cookie);
        else
            nickserv_free_cookie(cookie);
    }
}

static int
nickserv_saxdb_read(dict_t db) {
    dict_iterator_t it;
    struct record_data *rd;

    for (it=dict_first(db); it; it=iter_next(it)) {
        rd = iter_data(it);
        nickserv_db_read_handle(iter_key(it), rd->d.object);
    }
    return 0;
}

static NICKSERV_FUNC(cmd_mergedb)
{
    struct timeval start, stop;
    dict_t db;

    NICKSERV_MIN_PARMS(2);
    gettimeofday(&start, NULL);
    if (!(db = parse_database(argv[1]))) {
        reply("NSMSG_DB_UNREADABLE", argv[1]);
        return 0;
    }
    nickserv_saxdb_read(db);
    free_database(db);
    gettimeofday(&stop, NULL);
    stop.tv_sec -= start.tv_sec;
    stop.tv_usec -= start.tv_usec;
    if (stop.tv_usec < 0) {
	stop.tv_sec -= 1;
	stop.tv_usec += 1000000;
    }
    reply("NSMSG_DB_MERGED", argv[1], stop.tv_sec, stop.tv_usec/1000);
    return 1;
}

static void
expire_handles(UNUSED_ARG(void *data))
{
    dict_iterator_t it, next;
    time_t expiry;
    struct handle_info *hi;

    for (it=dict_first(nickserv_handle_dict); it; it=next) {
        next = iter_next(it);
        hi = iter_data(it);
        if ((hi->opserv_level > 0)
            || hi->users
            || HANDLE_FLAGGED(hi, FROZEN)
            || HANDLE_FLAGGED(hi, NODELETE)) {
            continue;
        }
        expiry = hi->channels ? nickserv_conf.handle_expire_delay : nickserv_conf.nochan_handle_expire_delay;
        if ((now - hi->lastseen) > expiry) {
            log_module(NS_LOG, LOG_INFO, "Expiring account %s for inactivity.", hi->handle);
            nickserv_unregister_handle(hi, NULL);
        }
    }

    if (nickserv_conf.handle_expire_frequency)
        timeq_add(now + nickserv_conf.handle_expire_frequency, expire_handles, NULL);
}

static void
nickserv_load_dict(const char *fname)
{
    FILE *file;
    char line[128];
    if (!(file = fopen(fname, "r"))) {
        log_module(NS_LOG, LOG_ERROR, "Unable to open dictionary file %s: %s", fname, strerror(errno));
        return;
    }
    while (!feof(file)) {
        fgets(line, sizeof(line), file);
        if (!line[0])
            continue;
        if (line[strlen(line)-1] == '\n')
            line[strlen(line)-1] = 0;
        dict_insert(nickserv_conf.weak_password_dict, strdup(line), NULL);
    }
    fclose(file);
    log_module(NS_LOG, LOG_INFO, "Loaded %d words into weak password dictionary.", dict_size(nickserv_conf.weak_password_dict));
}

static enum reclaim_action
reclaim_action_from_string(const char *str) {
    if (!str)
        return RECLAIM_NONE;
    else if (!irccasecmp(str, "warn"))
        return RECLAIM_WARN;
    else if (!irccasecmp(str, "svsnick"))
        return RECLAIM_SVSNICK;
    else if (!irccasecmp(str, "kill"))
        return RECLAIM_KILL;
    else
        return RECLAIM_NONE;
}

static void
nickserv_conf_read(void)
{
    dict_t conf_node, child;
    const char *str;
    dict_iterator_t it;

    if (!(conf_node = conf_get_data(NICKSERV_CONF_NAME, RECDB_OBJECT))) {
	log_module(NS_LOG, LOG_ERROR, "config node `%s' is missing or has wrong type.", NICKSERV_CONF_NAME);
	return;
    }
    str = database_get_data(conf_node, KEY_VALID_HANDLE_REGEX, RECDB_QSTRING);
    if (!str)
        str = database_get_data(conf_node, KEY_VALID_ACCOUNT_REGEX, RECDB_QSTRING);
    if (nickserv_conf.valid_handle_regex_set)
        regfree(&nickserv_conf.valid_handle_regex);
    if (str) {
        int err = regcomp(&nickserv_conf.valid_handle_regex, str, REG_EXTENDED|REG_ICASE|REG_NOSUB);
        nickserv_conf.valid_handle_regex_set = !err;
        if (err) log_module(NS_LOG, LOG_ERROR, "Bad valid_account_regex (error %d)", err);
    } else {
        nickserv_conf.valid_handle_regex_set = 0;
    }
    str = database_get_data(conf_node, KEY_VALID_NICK_REGEX, RECDB_QSTRING);
    if (nickserv_conf.valid_nick_regex_set)
        regfree(&nickserv_conf.valid_nick_regex);
    if (str) {
        int err = regcomp(&nickserv_conf.valid_nick_regex, str, REG_EXTENDED|REG_ICASE|REG_NOSUB);
        nickserv_conf.valid_nick_regex_set = !err;
        if (err) log_module(NS_LOG, LOG_ERROR, "Bad valid_nick_regex (error %d)", err);
    } else {
        nickserv_conf.valid_nick_regex_set = 0;
    }
    str = database_get_data(conf_node, KEY_NICKS_PER_HANDLE, RECDB_QSTRING);
    if (!str)
        str = database_get_data(conf_node, KEY_NICKS_PER_ACCOUNT, RECDB_QSTRING);
    nickserv_conf.nicks_per_handle = str ? strtoul(str, NULL, 0) : 4;
    str = database_get_data(conf_node, KEY_DISABLE_NICKS, RECDB_QSTRING);
    nickserv_conf.disable_nicks = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(conf_node, KEY_DEFAULT_HOSTMASK, RECDB_QSTRING);
    nickserv_conf.default_hostmask = str ? !disabled_string(str) : 0;
    str = database_get_data(conf_node, KEY_PASSWORD_MIN_LENGTH, RECDB_QSTRING);
    nickserv_conf.password_min_length = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(conf_node, KEY_PASSWORD_MIN_DIGITS, RECDB_QSTRING);
    nickserv_conf.password_min_digits = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(conf_node, KEY_PASSWORD_MIN_UPPER, RECDB_QSTRING);
    nickserv_conf.password_min_upper = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(conf_node, KEY_PASSWORD_MIN_LOWER, RECDB_QSTRING);
    nickserv_conf.password_min_lower = str ? strtoul(str, NULL, 0) : 0;
    str = database_get_data(conf_node, KEY_DB_BACKUP_FREQ, RECDB_QSTRING);
    nickserv_conf.db_backup_frequency = str ? ParseInterval(str) : 7200;
    str = database_get_data(conf_node, KEY_MODOPER_LEVEL, RECDB_QSTRING);
    nickserv_conf.modoper_level = str ? strtoul(str, NULL, 0) : 900;
    str = database_get_data(conf_node, KEY_SET_EPITHET_LEVEL, RECDB_QSTRING);
    nickserv_conf.set_epithet_level = str ? strtoul(str, NULL, 0) : 1;
    str = database_get_data(conf_node, KEY_SET_TITLE_LEVEL, RECDB_QSTRING);
    nickserv_conf.set_title_level = str ? strtoul(str, NULL, 0) : 900;
    str = database_get_data(conf_node, KEY_SET_FAKEHOST_LEVEL, RECDB_QSTRING);
    nickserv_conf.set_fakehost_level = str ? strtoul(str, NULL, 0) : 1000;
    str = database_get_data(conf_node, KEY_HANDLE_EXPIRE_FREQ, RECDB_QSTRING);
    if (!str)
        str = database_get_data(conf_node, KEY_ACCOUNT_EXPIRE_FREQ, RECDB_QSTRING);
    nickserv_conf.handle_expire_frequency = str ? ParseInterval(str) : 86400;
    str = database_get_data(conf_node, KEY_HANDLE_EXPIRE_DELAY, RECDB_QSTRING);
    if (!str)
        str = database_get_data(conf_node, KEY_ACCOUNT_EXPIRE_DELAY, RECDB_QSTRING);
    nickserv_conf.handle_expire_delay = str ? ParseInterval(str) : 86400*30;
    str = database_get_data(conf_node, KEY_NOCHAN_HANDLE_EXPIRE_DELAY, RECDB_QSTRING);
    if (!str)
        str = database_get_data(conf_node, KEY_NOCHAN_ACCOUNT_EXPIRE_DELAY, RECDB_QSTRING);
    nickserv_conf.nochan_handle_expire_delay = str ? ParseInterval(str) : 86400*15;
    str = database_get_data(conf_node, "warn_clone_auth", RECDB_QSTRING);
    nickserv_conf.warn_clone_auth = str ? !disabled_string(str) : 1;
    str = database_get_data(conf_node, "default_maxlogins", RECDB_QSTRING);
    nickserv_conf.default_maxlogins = str ? strtoul(str, NULL, 0) : 2;
    str = database_get_data(conf_node, "hard_maxlogins", RECDB_QSTRING);
    nickserv_conf.hard_maxlogins = str ? strtoul(str, NULL, 0) : 10;
    if (!nickserv_conf.disable_nicks) {
        str = database_get_data(conf_node, "reclaim_action", RECDB_QSTRING);
        nickserv_conf.reclaim_action = str ? reclaim_action_from_string(str) : RECLAIM_NONE;
        str = database_get_data(conf_node, "warn_nick_owned", RECDB_QSTRING);
        nickserv_conf.warn_nick_owned = str ? enabled_string(str) : 0;
        str = database_get_data(conf_node, "auto_reclaim_action", RECDB_QSTRING);
        nickserv_conf.auto_reclaim_action = str ? reclaim_action_from_string(str) : RECLAIM_NONE;
        str = database_get_data(conf_node, "auto_reclaim_delay", RECDB_QSTRING);
        nickserv_conf.auto_reclaim_delay = str ? ParseInterval(str) : 0;
    }
    child = database_get_data(conf_node, KEY_FLAG_LEVELS, RECDB_OBJECT);
    for (it=dict_first(child); it; it=iter_next(it)) {
        const char *key = iter_key(it), *value;
        unsigned char flag;
        int pos;

        if (!strncasecmp(key, "uc_", 3))
            flag = toupper(key[3]);
        else if (!strncasecmp(key, "lc_", 3))
            flag = tolower(key[3]);
        else
            flag = key[0];

        if ((pos = handle_inverse_flags[flag])) {
            value = GET_RECORD_QSTRING((struct record_data*)iter_data(it));
            flag_access_levels[pos - 1] = strtoul(value, NULL, 0);
        }
    }
    if (nickserv_conf.weak_password_dict)
        dict_delete(nickserv_conf.weak_password_dict);
    nickserv_conf.weak_password_dict = dict_new();
    dict_set_free_keys(nickserv_conf.weak_password_dict, free);
    dict_insert(nickserv_conf.weak_password_dict, strdup("password"), NULL);
    dict_insert(nickserv_conf.weak_password_dict, strdup("<password>"), NULL);
    str = database_get_data(conf_node, KEY_DICT_FILE, RECDB_QSTRING);
    if (str)
        nickserv_load_dict(str);
    str = database_get_data(conf_node, KEY_NICK, RECDB_QSTRING);
    if (nickserv && str)
        NickChange(nickserv, str, 0);
    str = database_get_data(conf_node, KEY_AUTOGAG_ENABLED, RECDB_QSTRING);
    nickserv_conf.autogag_enabled = str ? strtoul(str, NULL, 0) : 1;
    str = database_get_data(conf_node, KEY_AUTOGAG_DURATION, RECDB_QSTRING);
    nickserv_conf.autogag_duration = str ? ParseInterval(str) : 1800;
    str = database_get_data(conf_node, KEY_EMAIL_VISIBLE_LEVEL, RECDB_QSTRING);
    nickserv_conf.email_visible_level = str ? strtoul(str, NULL, 0) : 800;
    str = database_get_data(conf_node, KEY_EMAIL_ENABLED, RECDB_QSTRING);
    nickserv_conf.email_enabled = str ? enabled_string(str) : 0;
    str = database_get_data(conf_node, KEY_COOKIE_TIMEOUT, RECDB_QSTRING);
    nickserv_conf.cookie_timeout = str ? ParseInterval(str) : 24*3600;
    str = database_get_data(conf_node, KEY_EMAIL_REQUIRED, RECDB_QSTRING);
    nickserv_conf.email_required = (nickserv_conf.email_enabled && str) ? enabled_string(str) : 0;
    str = database_get_data(conf_node, KEY_ACCOUNTS_PER_EMAIL, RECDB_QSTRING);
    nickserv_conf.handles_per_email = str ? strtoul(str, NULL, 0) : 1;
    str = database_get_data(conf_node, KEY_EMAIL_SEARCH_LEVEL, RECDB_QSTRING);
    nickserv_conf.email_search_level = str ? strtoul(str, NULL, 0) : 600;
    str = database_get_data(conf_node, KEY_TITLEHOST_SUFFIX, RECDB_QSTRING);
    nickserv_conf.titlehost_suffix = str ? str : "example.net";
    str = conf_get_data("server/network", RECDB_QSTRING);
    nickserv_conf.network_name = str ? str : "some IRC network";
    if (!nickserv_conf.auth_policer_params) {
        nickserv_conf.auth_policer_params = policer_params_new();
        policer_params_set(nickserv_conf.auth_policer_params, "size", "5");
        policer_params_set(nickserv_conf.auth_policer_params, "drain-rate", "0.05");
    }
    child = database_get_data(conf_node, KEY_AUTH_POLICER, RECDB_OBJECT);
    for (it=dict_first(child); it; it=iter_next(it))
        set_policer_param(iter_key(it), iter_data(it), nickserv_conf.auth_policer_params);
}

static void
nickserv_reclaim(struct userNode *user, struct nick_info *ni, enum reclaim_action action) {
    const char *msg;
    char newnick[NICKLEN+1];

    assert(user);
    assert(ni);
    switch (action) {
    case RECLAIM_NONE:
        /* do nothing */
        break;
    case RECLAIM_WARN:
        send_message(user, nickserv, "NSMSG_RECLAIM_WARN", ni->nick, ni->owner->handle);
        break;
    case RECLAIM_SVSNICK:
        do {
            snprintf(newnick, sizeof(newnick), "Guest%d", rand()%10000);
        } while (GetUserH(newnick));
        irc_svsnick(nickserv, user, newnick);
        break;
    case RECLAIM_KILL:
        msg = user_find_message(user, "NSMSG_RECLAIM_KILL");
        irc_kill(nickserv, user, msg);
        break;
    }
}

static void
nickserv_reclaim_p(void *data) {
    struct userNode *user = data;
    struct nick_info *ni = get_nick_info(user->nick);
    if (ni)
        nickserv_reclaim(user, ni, nickserv_conf.auto_reclaim_action);
}

static int
check_user_nick(struct userNode *user) {
    struct nick_info *ni;
    user->modes &= ~FLAGS_REGNICK;
    if (!(ni = get_nick_info(user->nick)))
        return 0;
    if (user->handle_info == ni->owner) {
        user->modes |= FLAGS_REGNICK;
        irc_regnick(user);
        return 0;
    }
    if (nickserv_conf.warn_nick_owned)
        send_message(user, nickserv, "NSMSG_RECLAIM_WARN", ni->nick, ni->owner->handle);
    if (nickserv_conf.auto_reclaim_action == RECLAIM_NONE)
        return 0;
    if (nickserv_conf.auto_reclaim_delay)
        timeq_add(now + nickserv_conf.auto_reclaim_delay, nickserv_reclaim_p, user);
    else
        nickserv_reclaim(user, ni, nickserv_conf.auto_reclaim_action);
    return 0;
}

int
handle_new_user(struct userNode *user)
{
    return check_user_nick(user);
}

void
handle_account(struct userNode *user, const char *stamp)
{
    struct handle_info *hi;

#ifdef WITH_PROTOCOL_P10
    hi = dict_find(nickserv_handle_dict, stamp, NULL);
#else
    hi = dict_find(nickserv_id_dict, stamp, NULL);
#endif

    if (hi) {
        if (HANDLE_FLAGGED(hi, SUSPENDED)) {
            return;
        }
        set_user_handle_info(user, hi, 0);
    } else {
        log_module(MAIN_LOG, LOG_WARNING, "%s had unknown account stamp %s.", user->nick, stamp);
    }
}

void
handle_nick_change(struct userNode *user, const char *old_nick)
{
    struct handle_info *hi;

    if ((hi = dict_find(nickserv_allow_auth_dict, old_nick, 0))) {
        dict_remove(nickserv_allow_auth_dict, old_nick);
        dict_insert(nickserv_allow_auth_dict, user->nick, hi);
    }
    timeq_del(0, nickserv_reclaim_p, user, TIMEQ_IGNORE_WHEN);
    check_user_nick(user);
}

void
nickserv_remove_user(struct userNode *user, UNUSED_ARG(struct userNode *killer), UNUSED_ARG(const char *why))
{
    dict_remove(nickserv_allow_auth_dict, user->nick);
    timeq_del(0, nickserv_reclaim_p, user, TIMEQ_IGNORE_WHEN);
    set_user_handle_info(user, NULL, 0);
}

static struct modcmd *
nickserv_define_func(const char *name, modcmd_func_t func, int min_level, int must_auth, int must_be_qualified)
{
    if (min_level > 0) {
        char buf[16];
        sprintf(buf, "%u", min_level);
        if (must_be_qualified) {
            return modcmd_register(nickserv_module, name, func, 1, (must_auth ? MODCMD_REQUIRE_AUTHED : 0), "level", buf, "flags", "+qualified,+loghostmask", NULL);
        } else {
            return modcmd_register(nickserv_module, name, func, 1, (must_auth ? MODCMD_REQUIRE_AUTHED : 0), "level", buf, NULL);
        }
    } else if (min_level == 0) {
        if (must_be_qualified) {
            return modcmd_register(nickserv_module, name, func, 1, (must_auth ? MODCMD_REQUIRE_AUTHED : 0), "flags", "+helping", NULL);
        } else {
            return modcmd_register(nickserv_module, name, func, 1, (must_auth ? MODCMD_REQUIRE_AUTHED : 0), "flags", "+helping", NULL);
        }
    } else {
        if (must_be_qualified) {
            return modcmd_register(nickserv_module, name, func, 1, (must_auth ? MODCMD_REQUIRE_AUTHED : 0), "flags", "+qualified,+loghostmask", NULL);
        } else {
            return modcmd_register(nickserv_module, name, func, 1, (must_auth ? MODCMD_REQUIRE_AUTHED : 0), NULL);
        }
    }
}

static void
nickserv_db_cleanup(void)
{
    unreg_del_user_func(nickserv_remove_user);
    userList_clean(&curr_helpers);
    policer_params_delete(nickserv_conf.auth_policer_params);
    dict_delete(nickserv_handle_dict);
    dict_delete(nickserv_nick_dict);
    dict_delete(nickserv_opt_dict);
    dict_delete(nickserv_allow_auth_dict);
    dict_delete(nickserv_email_dict);
    dict_delete(nickserv_id_dict);
    dict_delete(nickserv_conf.weak_password_dict);
    free(auth_func_list);
    free(unreg_func_list);
    free(rf_list);
    free(allowauth_func_list);
    free(handle_merge_func_list);
    free(failpw_func_list);
    if (nickserv_conf.valid_handle_regex_set)
        regfree(&nickserv_conf.valid_handle_regex);
    if (nickserv_conf.valid_nick_regex_set)
        regfree(&nickserv_conf.valid_nick_regex);
}

void
init_nickserv(const char *nick)
{
    unsigned int i;
    NS_LOG = log_register_type("NickServ", "file:nickserv.log");
    reg_new_user_func(handle_new_user);
    reg_nick_change_func(handle_nick_change);
    reg_del_user_func(nickserv_remove_user);
    reg_account_func(handle_account);

    /* set up handle_inverse_flags */
    memset(handle_inverse_flags, 0, sizeof(handle_inverse_flags));
    for (i=0; handle_flags[i]; i++) {
        handle_inverse_flags[(unsigned char)handle_flags[i]] = i + 1;
        flag_access_levels[i] = 0;
    }

    conf_register_reload(nickserv_conf_read);
    nickserv_opt_dict = dict_new();
    nickserv_email_dict = dict_new();
    dict_set_free_keys(nickserv_email_dict, free);
    dict_set_free_data(nickserv_email_dict, nickserv_free_email_addr);

    nickserv_module = module_register("NickServ", NS_LOG, "nickserv.help", NULL);
    modcmd_register(nickserv_module, "AUTH", cmd_auth, 2, MODCMD_KEEP_BOUND, "flags", "+qualified,+loghostmask", NULL);
    nickserv_define_func("ALLOWAUTH", cmd_allowauth, 0, 1, 0);
    nickserv_define_func("REGISTER", cmd_register, -1, 0, 1);
    nickserv_define_func("OREGISTER", cmd_oregister, 0, 1, 0);
    nickserv_define_func("UNREGISTER", cmd_unregister, -1, 1, 1);
    nickserv_define_func("OUNREGISTER", cmd_ounregister, 0, 1, 0);
    nickserv_define_func("ADDMASK", cmd_addmask, -1, 1, 0);
    nickserv_define_func("OADDMASK", cmd_oaddmask, 0, 1, 0);
    nickserv_define_func("DELMASK", cmd_delmask, -1, 1, 0);
    nickserv_define_func("ODELMASK", cmd_odelmask, 0, 1, 0);
    nickserv_define_func("PASS", cmd_pass, -1, 1, 1);
    nickserv_define_func("SET", cmd_set, -1, 1, 0);
    nickserv_define_func("OSET", cmd_oset, 0, 1, 0);
    nickserv_define_func("ACCOUNTINFO", cmd_handleinfo, -1, 0, 0);
    nickserv_define_func("USERINFO", cmd_userinfo, -1, 1, 0);
    nickserv_define_func("RENAME", cmd_rename_handle, -1, 1, 0);
    nickserv_define_func("VACATION", cmd_vacation, -1, 1, 0);
    nickserv_define_func("MERGE", cmd_merge, 0, 1, 0);
    if (!nickserv_conf.disable_nicks) {
	/* nick management commands */
	nickserv_define_func("REGNICK", cmd_regnick, -1, 1, 0);
	nickserv_define_func("OREGNICK", cmd_oregnick, 0, 1, 0);
	nickserv_define_func("UNREGNICK", cmd_unregnick, -1, 1, 0);
	nickserv_define_func("OUNREGNICK", cmd_ounregnick, 0, 1, 0);
	nickserv_define_func("NICKINFO", cmd_nickinfo, -1, 1, 0);
        nickserv_define_func("RECLAIM", cmd_reclaim, -1, 1, 0);
    }
    if (nickserv_conf.email_enabled) {
        nickserv_define_func("AUTHCOOKIE", cmd_authcookie, -1, 0, 0);
        nickserv_define_func("RESETPASS", cmd_resetpass, -1, 0, 1);
        nickserv_define_func("COOKIE", cmd_cookie, -1, 0, 1);
        nickserv_define_func("DELCOOKIE", cmd_delcookie, -1, 1, 0);
        dict_insert(nickserv_opt_dict, "EMAIL", opt_email);
    }
    nickserv_define_func("GHOST", cmd_ghost, -1, 1, 0);
    /* miscellaneous commands */
    nickserv_define_func("STATUS", cmd_status, -1, 0, 0);
    nickserv_define_func("SEARCH", cmd_search, 100, 1, 0);
    nickserv_define_func("SEARCH UNREGISTER", NULL, 800, 1, 0);
    nickserv_define_func("MERGEDB", cmd_mergedb, 999, 1, 0);
    nickserv_define_func("CHECKPASS", cmd_checkpass, 601, 1, 0);
    /* other options */
    dict_insert(nickserv_opt_dict, "INFO", opt_info);
    dict_insert(nickserv_opt_dict, "WIDTH", opt_width);
    dict_insert(nickserv_opt_dict, "TABLEWIDTH", opt_tablewidth);
    dict_insert(nickserv_opt_dict, "COLOR", opt_color);
    dict_insert(nickserv_opt_dict, "PRIVMSG", opt_privmsg);
    dict_insert(nickserv_opt_dict, "STYLE", opt_style);
    dict_insert(nickserv_opt_dict, "PASS", opt_password);
    dict_insert(nickserv_opt_dict, "PASSWORD", opt_password);
    dict_insert(nickserv_opt_dict, "FLAGS", opt_flags);
    dict_insert(nickserv_opt_dict, "ACCESS", opt_level);
    dict_insert(nickserv_opt_dict, "LEVEL", opt_level);
    dict_insert(nickserv_opt_dict, "EPITHET", opt_epithet);
    if (nickserv_conf.titlehost_suffix) {
        dict_insert(nickserv_opt_dict, "TITLE", opt_title);
        dict_insert(nickserv_opt_dict, "FAKEHOST", opt_fakehost);
    }
    dict_insert(nickserv_opt_dict, "ANNOUNCEMENTS", opt_announcements);
    dict_insert(nickserv_opt_dict, "MAXLOGINS", opt_maxlogins);
    dict_insert(nickserv_opt_dict, "LANGUAGE", opt_language);

    nickserv_handle_dict = dict_new();
    dict_set_free_keys(nickserv_handle_dict, free);
    dict_set_free_data(nickserv_handle_dict, free_handle_info);

    nickserv_id_dict = dict_new();
    dict_set_free_keys(nickserv_id_dict, free);

    nickserv_nick_dict = dict_new();
    dict_set_free_data(nickserv_nick_dict, free_nick_info);

    nickserv_allow_auth_dict = dict_new();

    userList_init(&curr_helpers);

    if (nick) {
        const char *modes = conf_get_data("services/nickserv/modes", RECDB_QSTRING);
        nickserv = AddService(nick, modes ? modes : NULL, "Nick Services", NULL);
        nickserv_service = service_register(nickserv);
    }
    saxdb_register("NickServ", nickserv_saxdb_read, nickserv_saxdb_write);
    reg_exit_func(nickserv_db_cleanup);
    if(nickserv_conf.handle_expire_frequency)
        timeq_add(now + nickserv_conf.handle_expire_frequency, expire_handles, NULL);
    message_register_table(msgtab);
}

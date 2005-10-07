/* main.c - srvx
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

#define PID_FILE "srvx.pid"

#include "conf.h"
#include "gline.h"
#include "ioset.h"
#include "modcmd.h"
#include "saxdb.h"
#include "sendmail.h"
#include "timeq.h"

#include "chanserv.h"
#include "global.h"
#include "modules.h"
#include "opserv.h"

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#else
#include "getopt.h"
#endif
#ifdef HAVE_SYS_RESOURCE_H
#include <sys/resource.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#ifndef SIGCHLD
#define SIGCHLD SIGCLD
#endif

extern FILE *replay_file;

time_t boot_time, burst_begin, now;
unsigned long burst_length;
struct log_type *MAIN_LOG;

int quit_services, max_cycles;

char *services_config = "srvx.conf";

char **services_argv;
int services_argc;

struct cManagerNode cManager;

struct policer_params *oper_policer_params, *luser_policer_params, *god_policer_params;

static const struct message_entry msgtab[] = {
    { "MSG_NONE", "None" },
    { "MSG_ON", "On" },
    { "MSG_OFF", "Off" },
    { "MSG_NEVER", "Never" },
    { "MSG_SERVICE_IMMUNE", "$b%s$b may not be kicked, killed, banned, or deopped." },
    { "MSG_SERVICE_PRIVILEGED", "$b%s$b is a privileged service." },
    { "MSG_NOT_A_SERVICE", "$b%s$b is not a service bot." },
    { "MSG_COMMAND_UNKNOWN", "$b%s$b is an unknown command." },
    { "MSG_COMMAND_PRIVILEGED", "$b%s$b is a privileged command." },
    { "MSG_COMMAND_DISABLED", "$b%s$b is a disabled command." },
    { "MSG_SETTING_PRIVILEGED", "$b%s$b is a privileged setting." },
    { "MSG_AUTHENTICATE", "You must first authenticate with $b$N$b." },
    { "MSG_USER_AUTHENTICATE", "%s must first authenticate with $b$N$b." },
    { "MSG_SET_EMAIL_ADDR", "You must first set your account's email address.  (Contact network staff if you cannot auth to your account.)" },
    { "MSG_HANDLE_UNKNOWN", "Account $b%s$b has not been registered." },
    { "MSG_NICK_UNKNOWN", "User with nick $b%s$b does not exist." },
    { "MSG_CHANNEL_UNKNOWN", "Channel with name $b%s$b does not exist." },
    { "MSG_SERVER_UNKNOWN", "Server with name $b%s$b does not exist or is not linked." },
    { "MSG_MODULE_UNKNOWN", "No module has been registered with name $b%s$b." },
    { "MSG_INVALID_MODES", "$b%s$b is an invalid set of channel modes." },
    { "MSG_INVALID_GLINE", "Invalid G-line '%s'." },
    { "MSG_INVALID_DURATION", "Invalid time span '%s'." },
    { "MSG_NOT_TARGET_NAME", "You must provide the name of a channel or user." },
    { "MSG_NOT_CHANNEL_NAME", "You must provide a valid channel name." },
    { "MSG_INVALID_CHANNEL", "You must provide the name of a channel that exists." },
    { "MSG_CHANNEL_ABSENT", "You aren't currently in $b%s$b." },
    { "MSG_CHANNEL_USER_ABSENT", "$b%s$b isn't currently in $b%s$b." },
    { "MSG_MISSING_PARAMS", "$b%s$b requires more parameters." },
    { "MSG_DEPRECATED_COMMAND", "The $b%s$b command has been deprecated, and will be removed in the future; please use $b%s$b instead." },
    { "MSG_OPER_SUSPENDED", "Your $b$O$b access has been suspended." },
    { "MSG_USER_OUTRANKED", "$b%s$b outranks you (command has no effect)." },
    { "MSG_STUPID_ACCESS_CHANGE", "Please ask someone $belse$b to demote you." },
    { "MSG_NO_SEARCH_ACCESS", "You do not have enough access to search based on $b%s$b." },
    { "MSG_INVALID_CRITERIA", "$b%s$b is an invalid search criteria." },
    { "MSG_MATCH_COUNT", "Found $b%u$b matches." },
    { "MSG_NO_MATCHES", "Nothing matched the criteria of your search." },
    { "MSG_TOPIC_UNKNOWN", "No help on that topic." },
    { "MSG_INVALID_BINARY", "$b%s$b is an invalid binary value." },
    { "MSG_INTERNAL_FAILURE", "Your command could not be processed due to an internal failure." },
    { "MSG_DB_UNKNOWN", "I do not know of a database named %s." },
    { "MSG_DB_IS_MONDO", "Database %s is in the \"mondo\" database and cannot be written separately." },
    { "MSG_DB_WRITE_ERROR", "Error while writing database %s." },
    { "MSG_DB_WROTE_DB", "Wrote database %s (in "FMT_TIME_T".%06lu seconds)." },
    { "MSG_DB_WROTE_ALL", "Wrote all databases (in "FMT_TIME_T".%06lu seconds)." },
    { "MSG_AND", "and" },
    { "MSG_0_SECONDS", "0 seconds" },
    { "MSG_YEAR", "year" },
    { "MSG_YEARS", "years" },
    { "MSG_WEEK", "week" },
    { "MSG_WEEKS", "weeks" },
    { "MSG_DAY", "day" },
    { "MSG_DAYS", "days" },
    { "MSG_HOUR", "hour" },
    { "MSG_HOURS", "hours" },
    { "MSG_MINUTE", "minute" },
    { "MSG_MINUTES", "minutes" },
    { "MSG_SECOND", "second" },
    { "MSG_SECONDS", "seconds" },
    { NULL, NULL }
};

void uplink_select(char *name);

static int
uplink_insert(const char *key, void *data, UNUSED_ARG(void *extra))
{
    struct uplinkNode *uplink = malloc(sizeof(struct uplinkNode));
    struct record_data *rd = data;
    int enabled = 1;
    char *str;
    struct sockaddr_in *sin;
    unsigned long addr;

    if(!uplink)
    {
	return 0;
    }

    uplink->name = (char *)key;
    uplink->host = database_get_data(rd->d.object, "address", RECDB_QSTRING);

    str = database_get_data(rd->d.object, "port", RECDB_QSTRING);
    uplink->port = str ? atoi(str) : 6667;
    uplink->password = database_get_data(rd->d.object, "password", RECDB_QSTRING);
    uplink->their_password = database_get_data(rd->d.object, "uplink_password", RECDB_QSTRING);

    str = database_get_data(rd->d.object, "enabled", RECDB_QSTRING);
    if(str)
    {
	enabled = atoi(str) ? 1 : 0;
    }

    cManager.enabled += enabled;

    str = database_get_data(rd->d.object, "max_tries", RECDB_QSTRING);
    uplink->max_tries = str ? atoi(str) : 3;
    uplink->flags = enabled ? 0 : UPLINK_UNAVAILABLE;
    uplink->state = DISCONNECTED;
    uplink->tries = 0;

    str = database_get_data(rd->d.object, "bind_address", RECDB_QSTRING);
    uplink->bind_addr_len = sizeof(*sin);
    if (str && getipbyname(str, &addr)) 
    {
	sin = calloc(1, uplink->bind_addr_len);
	sin->sin_family = AF_INET;
	sin->sin_addr.s_addr = addr;
	uplink->bind_addr = sin;
    } 
    else 
    {
	uplink->bind_addr = NULL;
	uplink->bind_addr_len = 0;
    }

    uplink->next = cManager.uplinks;
    uplink->prev = NULL;

    if(cManager.uplinks)
    {
	cManager.uplinks->prev = uplink;
    }

    cManager.uplinks = uplink;

    /* If the configuration is being reloaded, set the current uplink
       to the reloaded equivalent, if possible. */
    if(cManager.uplink
       && enabled
       && !irccasecmp(uplink->host, cManager.uplink->host)
       && uplink->port == cManager.uplink->port)
    {
	uplink->state = cManager.uplink->state;
	uplink->tries = cManager.uplink->tries;
	cManager.uplink = uplink;
    }

    return 0;
}

void
uplink_compile(void)
{
    const char *cycles;
    dict_t conf_node;
    struct uplinkNode *oldUplinks = NULL, *oldUplink = NULL;

    /* Save the old uplinks, we'll remove them later. */
    oldUplink = cManager.uplink;
    oldUplinks = cManager.uplinks;

    cycles = conf_get_data("server/max_cycles", RECDB_QSTRING);
    max_cycles = cycles ? atoi(cycles) : 30;
    if(!(conf_node = conf_get_data("uplinks", RECDB_OBJECT)))
    {
        log_module(MAIN_LOG, LOG_FATAL, "No uplinks configured; giving up.");
	exit(1);
    }

    cManager.enabled = 0;
    dict_foreach(conf_node, uplink_insert, NULL);

    /* Remove the old uplinks, if any. It doesn't matter if oldUplink (below)
       is a reference to one of these, because it won't get dereferenced. */
    if(oldUplinks)
    {
	struct uplinkNode *uplink, *next;

	oldUplinks->prev->next = NULL;

	for(uplink = oldUplinks; uplink; uplink = next)
	{
	    next = uplink->next;
            free(uplink->bind_addr);
	    free(uplink);
	}
    }

    /* If the uplink hasn't changed, it's either NULL or pointing at
       an uplink that was just deleted, select a new one. */
    if(cManager.uplink == oldUplink)
    {
	if(oldUplink)
	{
	    irc_squit(self, "Uplinks updated; selecting new uplink.", NULL);
	}

	cManager.uplink = NULL;
	uplink_select(NULL);
    }
}

struct uplinkNode *
uplink_find(char *name)
{
    struct uplinkNode *uplink;

    if(!cManager.enabled || !cManager.uplinks)
    {
	return NULL;
    }

    for(uplink = cManager.uplinks; uplink; uplink = uplink->next)
    {
	if(!strcasecmp(uplink->name, name))
	{
	    return uplink;
	}
    }

    return NULL;
}

void
uplink_select(char *name)
{
    struct uplinkNode *start, *uplink, *next;
    int stop;

    if(!cManager.enabled || !cManager.uplinks)
    {
	log_module(MAIN_LOG, LOG_FATAL, "No uplinks enabled; giving up.");
	exit(1);
    }

    if(!cManager.uplink)
    {
	start = cManager.uplinks;
    }
    else
    {
	start = cManager.uplink->next;
	if(!start)
	{
	    start = cManager.uplinks;
	}
    }

    stop = 0;
    for(uplink = start; uplink; uplink = next)
    {
	next = uplink->next ? uplink->next : cManager.uplinks;

	if(stop)
	{
	    uplink = NULL;
	    break;
	}

	/* We've wrapped around the list. */
	if(next == start)
	{
	    sleep((cManager.cycles >> 1) * 5);
	    cManager.cycles++;

	    if(max_cycles && (cManager.cycles >= max_cycles))
	    {
		log_module(MAIN_LOG, LOG_FATAL, "Maximum uplink list cycles exceeded; giving up.");
		exit(1);
	    }

	    /* Give the uplink currently in 'uplink' consideration,
	       and if not selected, break on the next iteration. */
	    stop = 1;
	}

	/* Skip bad uplinks. */
	if(uplink->flags & UPLINK_UNAVAILABLE)
	{
	    continue;
	}

	if(name && irccasecmp(uplink->name, name))
	{
	    /* If we were told to connect to a specific uplink, don't stop
	       until we find it.
	    */
	    continue;
	}

	/* It would be possible to track uplink health through a variety
	   of statistics and only break on the best uplink. For now, break
	   on the first available one.
	*/

	break;
    }

    if(!uplink)
    {
	/* We are shit outta luck if every single uplink has been passed
	   over. Use the current uplink if possible. */
	if(!cManager.uplink || cManager.uplink->flags & UPLINK_UNAVAILABLE)
	{
	    log_module(MAIN_LOG, LOG_FATAL, "All available uplinks exhausted; giving up.");
	    exit(1);
	}

	return;
    }

    cManager.uplink = uplink;
}

int
uplink_connect(void)
{
    struct uplinkNode *uplink = cManager.uplink;

    if(uplink->state != DISCONNECTED)
    {
	return 0;
    }

    if(uplink->flags & UPLINK_UNAVAILABLE)
    {
	uplink_select(NULL);
	uplink = cManager.uplink;
    }

    if(uplink->tries)
    {
	/* This delay could scale with the number of tries. */
	sleep(2);
    }

    if(!create_socket_client(uplink))
    {
	if(uplink->max_tries && (uplink->tries >= uplink->max_tries))
	{
	    /* This is a bad uplink, move on. */
	    uplink->flags |= UPLINK_UNAVAILABLE;
	    uplink_select(NULL);
	}

	return 0;
    }
    else
    {
	uplink->state = AUTHENTICATING;
	irc_introduce(uplink->password);
    }

    return 1;
}

void
received_ping(void)
{
    /* This function is called when a ping is received. Take it as
       a sign of link health and reset the connection manager
       information. */

    cManager.cycles = 0;
}

void sigaction_writedb(int x)
{
#ifndef HAVE_STRSIGNAL
    log_module(MAIN_LOG, LOG_INFO, "Signal %d -- writing databases.", x);
#else
    log_module(MAIN_LOG, LOG_INFO, "%s -- writing databases.", strsignal(x));
#endif
    do_write_dbs = 1;
}

void sigaction_exit(int x)
{
#ifndef HAVE_STRSIGNAL
    log_module(MAIN_LOG, LOG_INFO, "Signal %d -- exiting.", x);
#else
    log_module(MAIN_LOG, LOG_INFO, "%s -- exiting.", strsignal(x));
#endif
    irc_squit(self, "Exiting on signal from console.", NULL);
    quit_services = 1;
}

void sigaction_wait(UNUSED_ARG(int x))
{
    int code;
    wait4(-1, &code, WNOHANG, NULL);
}

void sigaction_rehash(int x)
{
#ifndef HAVE_STRSIGNAL
    log_module(MAIN_LOG, LOG_INFO, "Signal %d -- rehashing.", x);
#else
    log_module(MAIN_LOG, LOG_INFO, "%s -- rehashing.", strsignal(x));
#endif
    do_reopen = 1;
}

static exit_func_t *ef_list;
static unsigned int ef_size = 0, ef_used = 0;

void reg_exit_func(exit_func_t handler)
{
    if (ef_used == ef_size) {
	if (ef_size) {
	    ef_size <<= 1;
	    ef_list = realloc(ef_list, ef_size*sizeof(exit_func_t));
	} else {
	    ef_size = 8;
	    ef_list = malloc(ef_size*sizeof(exit_func_t));
	}
    }
    ef_list[ef_used++] = handler;
}

void call_exit_funcs(void)
{
    unsigned int n = ef_used;

    /* Call them in reverse order because we initialize logs, then
     * nickserv, then chanserv, etc., and they register their exit
     * funcs in that order, and there are some dependencies (for
     * example, ChanServ requires NickServ to not have cleaned up).
     */

    while (n > 0) {
	ef_list[--n]();
    }
    free(ef_list);
    ef_used = ef_size = 0;
}

int
set_policer_param(const char *param, void *data, void *extra)
{
    struct record_data *rd = data;
    const char *str = GET_RECORD_QSTRING(rd);
    if (str) {
        policer_params_set(extra, param, str);
    }
    return 0;
}

static void
conf_globals(void)
{
    const char *info;
    dict_t dict;

    info = conf_get_data("services/global/nick", RECDB_QSTRING);
    if (info && (info[0] == '.'))
        info = NULL;
    init_global(info);

    info = conf_get_data("services/nickserv/nick", RECDB_QSTRING);
    if (info && (info[0] == '.'))
        info = NULL;
    init_nickserv(info);

    info = conf_get_data("services/chanserv/nick", RECDB_QSTRING);
    if (info && (info[0] == '.'))
        info = NULL;
    init_chanserv(info);

    god_policer_params = policer_params_new();
    if ((dict = conf_get_data("policers/commands-god", RECDB_OBJECT))) {
        dict_foreach(dict, set_policer_param, god_policer_params);
    } else {
        policer_params_set(god_policer_params, "size", "30");
        policer_params_set(god_policer_params, "drain-rate", "1");
    }
    oper_policer_params = policer_params_new();
    if ((dict = conf_get_data("policers/commands-oper", RECDB_OBJECT))) {
        dict_foreach(dict, set_policer_param, oper_policer_params);
    } else {
        policer_params_set(oper_policer_params, "size", "10");
        policer_params_set(oper_policer_params, "drain-rate", "1");
    }
    luser_policer_params = policer_params_new();
    if ((dict = conf_get_data("policers/commands-luser", RECDB_OBJECT))) {
        dict_foreach(dict, set_policer_param, luser_policer_params);
    } else {
        policer_params_set(luser_policer_params, "size", "5");
        policer_params_set(luser_policer_params, "drain-rate", "0.50");
    }

    info = conf_get_data("services/opserv/nick", RECDB_QSTRING);
    if (info && (info[0] == '.'))
        info = NULL;
    init_opserv(info);
}

#ifdef HAVE_SYS_RESOURCE_H

static int
set_item_rlimit(const char *name, void *data, void *extra)
{
    long rsrc;
    int found;
    struct record_data *rd = data;
    struct rlimit rlim;
    const char *str;

    rsrc = (long)dict_find(extra, name, &found);
    if (!found) {
        log_module(MAIN_LOG, LOG_ERROR, "Invalid rlimit \"%s\" in rlimits section.", name);
        return 0;
    }
    if (!(str = GET_RECORD_QSTRING(rd))) {
        log_module(MAIN_LOG, LOG_ERROR, "Missing or invalid parameter type for rlimit \"%s\".", name);
        return 0;
    }
    if (getrlimit(rsrc, &rlim) < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "Couldn't get rlimit \"%s\": errno %d: %s", name, errno, strerror(errno));
        return 0;
    }
    rlim.rlim_cur = ParseVolume(str);
    if (setrlimit(rsrc, &rlim) < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "Couldn't set rlimit \"%s\": errno %d: %s", name, errno, strerror(errno));
    }
    return 0;
}

static void
conf_rlimits(void)
{
    dict_t dict, values;

    values = dict_new();
    dict_insert(values, "data", (void*)RLIMIT_DATA);
    dict_insert(values, "stack", (void*)RLIMIT_STACK);
#ifdef RLIMIT_VMEM
    dict_insert(values, "vmem", (void*)RLIMIT_VMEM);
#else
#ifdef RLIMIT_AS
    dict_insert(values, "vmem", (void*)RLIMIT_AS);
#endif
#endif
    if ((dict = conf_get_data("rlimits", RECDB_OBJECT))) {
        dict_foreach(dict, set_item_rlimit, values);
    }
    dict_delete(values);
}

#else

static void
conf_rlimits(void)
{
}

#endif

void main_shutdown(void)
{
    struct uplinkNode *ul, *ul_next;
    ioset_cleanup();
    for (ul = cManager.uplinks; ul; ul = ul_next) {
        ul_next = ul->next;
        free(ul->bind_addr);
        free(ul);
    }
    tools_cleanup();
    conf_close();
    remove(PID_FILE);
    policer_params_delete(god_policer_params);
    policer_params_delete(oper_policer_params);
    policer_params_delete(luser_policer_params);
    if (replay_file)
        fclose(replay_file);
}

void usage(char *self) {
    /* We can assume we have getopt_long(). */
    printf("Usage: %s [-c config] [-r log] [-d] [-f] [-v|-h]\n"
           "-c, --config                    selects a different configuration file.\n"
           "-d, --debug                     enables debug mode.\n"
           "-f, --foreground                run srvx in the foreground.\n"
           "-h, --help                      prints this usage message.\n"
           "-k, --check                     checks the configuration file's syntax.\n"
           "-r, --replay                    replay a log file (for debugging)\n"
           "-v, --version                   prints this program's version.\n"
           , self);
}

void version() {
    printf("    --------------------------------------------------\n"
           "    - "PACKAGE_STRING" ("CODENAME"), Built: " __DATE__ ", " __TIME__".\n"
           "    - Copyright (C) 2000 - 2005, srvx Development Team\n"
           "    --------------------------------------------------\n");
}

void license() {
    printf("\n"
           "This program is free software; you can redistribute it and/or modify\n"
           "it under the terms of the GNU General Public License as published by\n"
           "the Free Software Foundation; either version 2 of the License, or\n"
           "(at your option) any later version.\n"
           "\n"
           "This program is distributed in the hope that it will be useful,\n"
           "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
           "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
           "GNU General Public License for more details.\n"
           "\n"
           "You should have received a copy of the GNU General Public License\n"
           "along with this program; if not, write to the Free Software\n"
           "Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.\n\n");
}

#if WITH_MALLOC_BOEHM_GC
void
gc_warn_proc(char *msg, GC_word arg)
{
    log_module(MAIN_LOG, LOG_ERROR, "GC(%p): %s", (void*)arg, msg);
}
#endif

int main(int argc, char *argv[])
{
    int daemon, debug;
    pid_t pid = 0;
    FILE *file_out;
    struct sigaction sv;

#if WITH_MALLOC_BOEHM_GC
    GC_find_leak = 1;
    GC_set_warn_proc(gc_warn_proc);
    GC_enable_incremental();
#endif

    daemon = 1;
    debug = 0;
    tools_init();

    /* set up some signal handlers */
    memset(&sv, 0, sizeof(sv));
    sigemptyset(&sv.sa_mask);
    sv.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sv, NULL);
    sv.sa_handler = sigaction_rehash;
    sigaction(SIGHUP, &sv, NULL);
    sv.sa_handler = sigaction_writedb;
    sigaction(SIGINT, &sv, NULL);
    sv.sa_handler = sigaction_exit;
    sigaction(SIGQUIT, &sv, NULL);
    sv.sa_handler = sigaction_wait;
    sigaction(SIGCHLD, &sv, NULL);

    if (argc > 1) { /* parse command line, if any */
	int c;
	struct option options[] =
	{
	    {"config", 1, 0, 'c'},
            {"debug", 0, 0, 'd'},
	    {"foreground", 0, 0, 'f'},
	    {"help", 0, 0, 'h'},
	    {"check", 0, 0, 'k'},
            {"replay", 1, 0, 'r'},
	    {"version", 0, 0, 'v'},
	    {"verbose", 0, 0, 'V'},
	    {0, 0, 0, 0}
	};

	while ((c = getopt_long(argc, argv, "c:kr:dfvVh", options, NULL)) != -1) {
	    switch(c) {
	    case 'c':
		services_config = optarg;
		break;
	    case 'k':
		if (conf_read(services_config)) {
		    printf("%s appears to be a valid configuration file.\n", services_config);
		} else {
		    printf("%s is an invalid configuration file.\n", services_config);
		}
		exit(0);
            case 'r':
                replay_file = fopen(optarg, "r");
                if (!replay_file) {
                    fprintf(stderr, "Could not open %s for reading: %s (%d)\n",
                            optarg, strerror(errno), errno);
                    exit(0);
                }
                break;
            case 'd':
                debug = 1;
                break;
	    case 'f':
		daemon = 0;
		break;
	    case 'v':
		version();
		license();
		exit(0);
	    case 'h':
	    default:
		usage(argv[0]);
		exit(0);
	    }
	}
    }

    version();

    if (replay_file) {
        /* We read a line here to "prime" the replay file parser, but
         * mostly to get the right value of "now" for when we do the
         * irc_introduce. */
        replay_read_line();
        boot_time = now;
    } else {
        boot_time = time(&now);
    }

    log_module(MAIN_LOG, LOG_INFO, "Initializing daemon...");
    if (!conf_read(services_config)) {
	log_module(MAIN_LOG, LOG_FATAL, "Unable to read %s.", services_config);
	exit(0);
    }

    conf_register_reload(uplink_compile);

    if (daemon) {
	/* Attempt to fork into the background if daemon mode is on. */
	pid = fork();
	if (pid < 0) {
	    log_module(MAIN_LOG, LOG_FATAL, "Unable to fork: %s", strerror(errno));
        } else if (pid > 0) {
	    log_module(MAIN_LOG, LOG_INFO, "Forking into the background (pid: %i)...", pid);
	    exit(0);
	}
	setsid();
        /* Close these since we should not use them from now on. */
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);
    }

    if ((file_out = fopen(PID_FILE, "w")) == NULL) {
	/* Create the main process' pid file */
	log_module(MAIN_LOG, LOG_ERROR, "Unable to create PID file: %s", strerror(errno));
    } else {
	fprintf(file_out, "%i\n", (int)getpid());
	fclose(file_out);
    }

    services_argc = argc;
    services_argv = argv;

    atexit(call_exit_funcs);
    reg_exit_func(main_shutdown);

    log_init();
    MAIN_LOG = log_register_type("srvx", "file:main.log");
    if (debug)
        log_debug();
    timeq_init();
    init_structs();
    init_parse();
    modcmd_init();
    saxdb_init();
    gline_init();
    sendmail_init();
    helpfile_init();
    conf_globals(); /* initializes the core services */
    conf_rlimits();
    modules_init();
    message_register_table(msgtab);
    modcmd_finalize();
    saxdb_finalize();
    helpfile_finalize();
    modules_finalize();

    /* The first exit func to be called *should* be saxdb_write_all(). */
    reg_exit_func(saxdb_write_all);
    if (replay_file) {
        char *msg;
        log_module(MAIN_LOG, LOG_INFO, "Beginning replay...");
        srand(now);
        replay_event_loop();
        if ((msg = dict_sanity_check(clients))) {
            log_module(MAIN_LOG, LOG_ERROR, "Clients insanity: %s", msg);
            free(msg);
        }
        if ((msg = dict_sanity_check(channels))) {
            log_module(MAIN_LOG, LOG_ERROR, "Channels insanity: %s", msg);
            free(msg);
        }
        if ((msg = dict_sanity_check(servers))) {
            log_module(MAIN_LOG, LOG_ERROR, "Servers insanity: %s", msg);
            free(msg);
        }
    } else {
        srand(time(&now));
        ioset_run();
    }
    return 0;
}

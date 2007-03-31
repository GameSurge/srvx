/* main.c - srvx
 * Copyright 2000-2006 srvx Development Team
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
#include "mail.h"
#include "timeq.h"
#include "sar.h"

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

#include "main-common.c"

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

    fprintf(stdout, "Initializing daemon...\n");
    if (!conf_read(services_config)) {
	fprintf(stderr, "Unable to read %s.\n", services_config);
	exit(1);
    }

    conf_register_reload(uplink_compile);

    if (daemon) {
	/* Attempt to fork into the background if daemon mode is on. */
	pid = fork();
	if (pid < 0) {
	    fprintf(stderr, "Unable to fork: %s\n", strerror(errno));
        } else if (pid > 0) {
	    fprintf(stdout, "Forking into the background (pid: %d)...\n", pid);
	    exit(0);
	}
	setsid();
    }

    file_out = fopen(PID_FILE, "w");
    if (file_out == NULL) {
	/* Create the main process' pid file */
	fprintf(stderr, "Unable to create PID file: %s", strerror(errno));
    } else {
	fprintf(file_out, "%i\n", (int)getpid());
	fclose(file_out);
    }

    if (daemon) {
        /* Close these since we should not use them from now on. */
        fclose(stdin);
        fclose(stdout);
        fclose(stderr);
    }

    services_argc = argc;
    services_argv = argv;

    atexit(call_exit_funcs);
    reg_exit_func(main_shutdown);

    log_init();
    MAIN_LOG = log_register_type("srvx", "file:main.log");
    if (debug)
        log_debug();
    ioset_init();
    init_structs();
    init_parse();
    modcmd_init();
    saxdb_init();
    sar_init();
    gline_init();
    mail_init();
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

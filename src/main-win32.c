#include "common.h"
#include "conf.h"
#include "gline.h"
#include "ioset.h"
#include "modcmd.h"
#include "saxdb.h"
#include "mail.h"
#include "timeq.h"

#include "chanserv.h"
#include "global.h"
#include "modules.h"
#include "opserv.h"

#define sleep(x) Sleep((x) * 1000)
#include "main-common.c"

#include <windows.h>

static int daemon;
static int debug;

void parse_options(LPSTR args)
{
    const char *replay_file_name;
    const char *arg;
    char *argv[16];
    unsigned int jj;
    unsigned int check_conf;
    int argc;
    int ii;

    argc = split_line(args, 0, ArrayLength(argv), argv);
    if (argc < 1)
        return;

    replay_file_name = NULL;
    check_conf = 0;
    daemon = 1;
    for (ii = 0; ii < argc; ++ii) {
        arg = argv[ii];
        if ((arg[0] == '/') || (arg[0] == '-' && arg[1] == '-')) {
            arg += 1 + (arg[0] == '-' && arg[1] == '-');
            if (!strcmp(arg, "config")) {
            } else if (!strcmp(arg, "debug")) {
                debug = 1;
            } else if (!strcmp(arg, "foreground")) {
                daemon = 0;
            } else if (!strcmp(arg, "check")) {
                check_conf = 1;
            } else if (!strcmp(arg, "replay")) {
                replay_file_name = argv[++ii];
            } else if (!strcmp(arg, "version")) {
                version();
                license();
                exit(0);
            } else {
                usage("srvx");
                exit(0);
            }
        } else if (arg[0] == '-') {
            for (jj = 1; arg[jj] != '\0'; ++jj) {
                switch (arg[jj]) {
                case 'c': services_config = argv[++ii]; break;
                case 'd': debug = 1; break;
                case 'f': daemon = 0; break;
                case 'k': check_conf = 1; break;
                case 'r': replay_file_name = argv[++ii]; break;
                    break;
                case 'v':
                    version();
                    license();
                    exit(0);
                default:
                    usage("srvx");
                    exit(0);
                }
            }
        }
    }

    if (check_conf) {
        if (conf_read(services_config)) {
            printf("%s appears to be a valid configuration file.\n", services_config);
        } else {
            printf("%s is an invalid configuration file.\n", services_config);
        }
        exit(0);
    }

    if (replay_file_name) {
        replay_file = fopen(optarg, "r");
        if (!replay_file) {
            fprintf(stderr, "Could not open %s for reading: %s (%d)\n",
                    optarg, strerror(errno), errno);
            exit(0);
        }
    }
}

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow)
{
    tools_init();
    parse_options(lpCmdLine);
    log_module(MAIN_LOG, LOG_INFO, "Initializing daemon...");

    if (!conf_read(services_config)) {
        log_module(MAIN_LOG, LOG_FATAL, "Unable to read %s.", services_config);
        exit(0);
    }

    boot_time = time(&now);
    conf_register_reload(uplink_compile);
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
    gline_init();
    mail_init();
    helpfile_init();
    conf_globals();
    conf_rlimits();
    modules_init();
    message_register_table(msgtab);
    modcmd_finalize();
    saxdb_finalize();
    helpfile_finalize();
    modules_finalize();
    reg_exit_func(saxdb_write_all);
    srand(time(&now));
    ioset_run();

    return quit_services;
    (void)hInst; (void)hPrevInst; (void)nCmdShow;
}


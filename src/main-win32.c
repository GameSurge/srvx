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

int APIENTRY WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow)
{
    tools_init();
    /* TODO: parse lpCmdLine */
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


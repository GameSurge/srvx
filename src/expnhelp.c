#include "log.h"
#include "recdb.h"

/* because recdb likes to log stuff.. */
struct log_type *MAIN_LOG;
void log_module(UNUSED_ARG(struct log_type *lt), UNUSED_ARG(enum log_severity ls), const char *format, ...)
{
    va_list va;
    va_start(va, format);
    vfprintf(stderr, format, va);
    va_end(va);
    fflush(stderr);
}

struct string_list *new_argv;
const char *hidden_host_suffix;

struct cfg_scan {
    struct cfg_scan *parent;
    char *path;
};

int
scan_db(const char *key, void *data, void *extra)
{
    struct record_data *rd = data;
    struct cfg_scan child, *self = extra;

    child.parent = extra;

    switch (rd->type) {
    case RECDB_QSTRING:
        if ((irccasestr(key, "enable")
             || (irccasestr(key, "disable"))
             || (irccasestr(key, "require")))
            && enabled_string(rd->d.qstring)) {
            char *new_arg;
            new_arg = malloc(strlen(self->path)+strlen(key)+4);
            sprintf(new_arg, "-D%s/%s", self->path, key);
            string_list_append(new_argv, new_arg);
        }
        break;
    case RECDB_OBJECT:
        child.path = malloc(strlen(self->path) + strlen(key) + 2);
        sprintf(child.path, "%s/%s", self->path, key);
        dict_foreach(rd->d.object, scan_db, &child);
        free(child.path);
        break;
    default: /* ignore */ break;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    const char *cfg_file;
    struct cfg_scan scanner;
    dict_t cfg_db;

    tools_init();
    new_argv = alloc_string_list(4);
    string_list_append(new_argv, strdup("m4"));

    if (argc > 1) {
        cfg_file = argv[1];
    } else {
        cfg_file = "srvx.conf";
    }

    if (!(cfg_db = parse_database(cfg_file))) {
        fprintf(stderr, "Unable to parse config file %s; you will get a 'default' expansion.\n", cfg_file);
    } else {
        scanner.parent = NULL;
        scanner.path = "";
        dict_foreach(cfg_db, scan_db, &scanner);
    }

    string_list_append(new_argv, NULL);
    execvp("m4", new_argv->list);
    fprintf(stderr, "Error in exec: %s (%d)\n", strerror(errno), errno);
    fprintf(stderr, "Maybe you do not have the 'm4' program installed?\n");
    return 1;
}

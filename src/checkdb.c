#include "conf.h"
#include "modcmd.h"
#include "saxdb.h"
#include "timeq.h"

int bad;
const char *hidden_host_suffix;

/* because recdb likes to log stuff.. */
void log_module(UNUSED_ARG(struct log_type *lt), UNUSED_ARG(enum log_severity ls), const char *format, ...)
{
    va_list va;
    va_start(va, format);
    vfprintf(stderr, format, va);
    va_end(va);
    bad = 1;
}

/* and because saxdb is tied in to lots of stuff.. */

time_t now;

void *conf_get_data(UNUSED_ARG(const char *full_path), UNUSED_ARG(enum recdb_type type)) {
    return NULL;
}

void conf_register_reload(UNUSED_ARG(conf_reload_func crf)) {
}

void reg_exit_func(UNUSED_ARG(exit_func_t handler)) {
}

void timeq_add(UNUSED_ARG(time_t when), UNUSED_ARG(timeq_func func), UNUSED_ARG(void *data)) {
}

void timeq_del(UNUSED_ARG(time_t when), UNUSED_ARG(timeq_func func), UNUSED_ARG(void *data), UNUSED_ARG(int mask)) {
}

int send_message(UNUSED_ARG(struct userNode *dest), UNUSED_ARG(struct userNode *src), UNUSED_ARG(const char *message), ...) {
    return 0;
}

struct module *module_register(UNUSED_ARG(const char *name), UNUSED_ARG(enum log_type clog), UNUSED_ARG(const char *helpfile_name), UNUSED_ARG(expand_func_t expand_help)) {
    return NULL;
}

struct modcmd *modcmd_register(UNUSED_ARG(struct module *module), UNUSED_ARG(const char *name), UNUSED_ARG(modcmd_func_t func), UNUSED_ARG(unsigned int min_argc), UNUSED_ARG(unsigned int flags), ...) {
    return NULL;
}

void table_send(UNUSED_ARG(struct userNode *from), UNUSED_ARG(const char *to), UNUSED_ARG(unsigned int size), UNUSED_ARG(irc_send_func irc_send), UNUSED_ARG(struct helpfile_table table)) {
}

/* back to our regularly scheduled code: */

int check_record(const char *key, void *data, UNUSED_ARG(void *extra))
{
    struct record_data *rd = data;
    switch (rd->type) {
    case RECDB_INVALID:
	fprintf(stdout, "Invalid database record type for key %s\n", key);
	return 1;
    case RECDB_QSTRING:
    case RECDB_STRING_LIST:
	return 0;
    case RECDB_OBJECT:
	return dict_foreach(rd->d.object, check_record, NULL) ? 1 : 0;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    dict_t db;
    char *infile;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "%s usage: %s <dbfile> [outputfile]\n\n", argv[0], argv[0]);
        fprintf(stderr, "If [outputfile] is specified, dbfile is rewritten into outputfile after being\nparsed.\n\n");
        fprintf(stderr, "<dbfile> and/or [outputfile] may be given as '-' to use stdin and stdout,\nrespectively.\n");
        return 1;
    }

    tools_init();
    if (!strcmp(argv[1], "-")) {
        infile = "/dev/stdin";
    } else {
        infile = argv[1];
    }
    if (!(db = parse_database(infile))) return 2;
    fprintf(stdout, "Database read okay.\n");
    fflush(stdout);
    if (dict_foreach(db, check_record, 0)) return 3;
    if (!bad) {
        fprintf(stdout, "Database checked okay.\n");
        fflush(stdout);
    }

    if (argc == 3) {
        FILE *f;

        if (!strcmp(argv[2], "-")) {
            f = stdout;
        } else {
            if (!(f = fopen(argv[2], "w+"))) {
                fprintf(stderr, "fopen: %s\n", strerror(errno));
                return 4;
            }
        }

        write_database(f, db);
        fclose(f);
        fprintf(stdout, "Database written okay.\n");
        fflush(stdout);
    }

    return 0;
}

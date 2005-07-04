#include "hash.h"
#include "log.h"
#include "helpfile.h"

struct glob_test {
    const char *glob;
    const char *texts[8];
};

struct glob_test glob_yes[] = {
    { "*Zoot*!*@*.org", { "Zoot!Zoot@services.org",
			  "zoot!bleh@j00.are.r00t3d.org",
                          0 } },
    { "*!*@*", { "DK-Entrope!entrope@clan-dk.dyndns.org",
                 0 } },
    { "*", { "anything at all!",
	     0 } },
    { 0, { 0 } }
};

struct glob_test glob_no[] = {
    { "*Zoot*!*@*.org", { "Zoot!Zoot@services.net",
                          0 } },
    { "*!*@*", { "luser@host.com",
		 0 } },
    { 0, { 0 } }
};

struct glob_test glob_globs[] = {
    { "*@foo", { "foo@bar",
                 "bar@foo",
                 0 } },
    { "foo@bar", { "*@foo",
                   "bar@foo",
                   "foo@bar",
                   0 } },
    { 0, { 0 } }
};

int
main(UNUSED_ARG(int argc), UNUSED_ARG(char *argv[]))
{
    int i, j;

    tools_init();
    for (i = 0; glob_yes[i].glob; i++) {
	for (j=0; glob_yes[i].texts[j]; j++) {
	    if (!match_ircglob(glob_yes[i].texts[j], glob_yes[i].glob)) {
		fprintf(stderr, "%s did not match glob %s!\n",
			glob_yes[i].texts[j], glob_yes[i].glob);
	    }
	}
    }

    for (i = 0; glob_no[i].glob; i++) {
	for (j=0; glob_no[i].texts[j]; j++) {
	    if (match_ircglob(glob_no[i].texts[j], glob_no[i].glob)) {
		fprintf(stderr, "%s matched glob %s!\n",
			glob_no[i].texts[j], glob_no[i].glob);
	    }
	}
    }

    for (i=0; glob_globs[i].glob; i++) {
        for (j=0; glob_globs[i].texts[j]; j++) {
            fprintf(stdout, "match_ircglobs(\"%s\", \"%s\") -> %d\n",
                    glob_globs[i].glob, glob_globs[i].texts[j],
                    match_ircglobs(glob_globs[i].glob, glob_globs[i].texts[j]));
        }
    }

    return 0;
}

/* because tools.c likes to log stuff.. */
void
log_module(UNUSED_ARG(struct log_type *type), UNUSED_ARG(enum log_severity sev), const char *format, ...)
{
    va_list va;
    va_start(va, format);
    vfprintf(stderr, format, va);
    va_end(va);
}

const char *
language_find_message(UNUSED_ARG(struct language *lang), UNUSED_ARG(const char *msgid))
{
    return "Stub -- Not implemented.";
}

struct language *lang_C = NULL;
struct log_type *MAIN_LOG = NULL;
const char *hidden_host_suffix;

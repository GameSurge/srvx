#include "compat.h"

struct slab_log_entry
{
    struct timeval tv;
    void *slab;
    ssize_t size;
};

static void
read_log_file(const char *name)
{
    struct slab_log_entry sle;
    FILE *log;

    log = fopen(name, "r");
    if (!log)
    {
        fprintf(stderr, "Unable to open %s: %s\n", name, strerror(errno));
        return;
    }

    while (fread(&sle, sizeof(sle), 1, log) == 1)
    {
        fprintf(stdout, "%ld.%06ld %p ", (long)sle.tv.tv_sec, (long)sle.tv.tv_usec, sle.slab);
        if (sle.size > 0)
        {
            fprintf(stdout, "-> %zd\n", sle.size);
        }
        else if (sle.size < 0)
        {
            fprintf(stdout, "<- %zd\n", -sle.size);
        }
        else /* slze.size == 0 */
        {
            fprintf(stdout, "unmap\n");
        }
    }
}

int
main(int argc, char *argv[])
{
    int ii;

    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <logfile ...>\n", argv[0]);
        return 1;
    }

    for (ii = 1; ii < argc; ++ii)
    {
        read_log_file(argv[ii]);
    }
    return 0;
}

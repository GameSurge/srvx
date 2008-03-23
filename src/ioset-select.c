/* ioset select() backend for srvx
 * Copyright 2002-2006 srvx Development Team
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

#include "ioset-impl.h"
#include "common.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>

#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif

extern int clock_skew;
static struct io_fd **fds;
static unsigned int fds_size;
static fd_set read_fds;
static fd_set write_fds;
static fd_set except_fds;

static int
ioset_select_init(void)
{
    return 1;
}

static void
ioset_select_add(struct io_fd *fd)
{
    if ((unsigned)fd->fd >= fds_size) {
        unsigned int old_size = fds_size;
        fds_size = fd->fd + 8;
        fds = realloc(fds, fds_size*sizeof(*fds));
        memset(fds+old_size, 0, (fds_size-old_size)*sizeof(*fds));
    }
    fds[fd->fd] = fd;
}

static void
ioset_select_remove(struct io_fd *fd, int closed)
{
    FD_CLR(fd->fd, &read_fds);
    FD_CLR(fd->fd, &write_fds);
    FD_CLR(fd->fd, &except_fds);
    fds[fd->fd] = NULL;
    (void)closed;
}

static void
ioset_select_update(struct io_fd *fd)
{
    (void)fd;
}

static void
ioset_select_cleanup(void)
{
    free(fds);
}

#if 0
#define debug_fdsets(MSG, NFDS, READ_FDS, WRITE_FDS, EXCEPT_FDS, SELECT_TIMEOUT) (void)0
#else
static void
debug_fdsets(const char *msg, int nfds, fd_set *read_fds, fd_set *write_fds, fd_set *except_fds, struct timeval *select_timeout) {
    static const char *flag_text[8] = { "---", "r", "w", "rw", "e", "er", "ew", "erw" };
    char buf[MAXLEN];
    int pos, ii, flags;
    struct timeval now;

    for (pos=ii=0; ii<nfds; ++ii) {
        flags  = FD_ISSET(ii, read_fds) ? 1 : 0;
        flags |= FD_ISSET(ii, write_fds) ? 2 : 0;
        flags |= FD_ISSET(ii, except_fds) ? 4 : 0;
        if (flags)
            pos += sprintf(buf+pos, " %d%s", ii, flag_text[flags]);
    }
    gettimeofday(&now, NULL);
    if (select_timeout) {
        log_module(MAIN_LOG, LOG_DEBUG, "%s, at %lu.%06lu:%s (timeout %lu.%06lu)", msg, (unsigned long)now.tv_sec, (unsigned long)now.tv_usec, buf, (unsigned long)select_timeout->tv_sec, (unsigned long)select_timeout->tv_usec);
    } else {
        log_module(MAIN_LOG, LOG_DEBUG, "%s, at %lu.%06lu:%s (no timeout)", msg, (unsigned long)now.tv_sec, (unsigned long)now.tv_usec, buf);
    }
}
#endif

static int
ioset_select_loop(struct timeval *timeout)
{
    struct io_fd *fd;
    unsigned int nn;
    int select_result;
    int max_fd;

    /* Set up read_fds and write_fds fdsets. */
    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    FD_ZERO(&except_fds);
    max_fd = -1;
    for (nn=0; nn<fds_size; nn++) {
        if (!(fd = fds[nn]))
            continue;
        max_fd = nn;
        FD_SET(nn, &read_fds);
        FD_SET(nn, &except_fds);
        if (fd_wants_writes(fd))
            FD_SET(nn, &write_fds);
    }

    /* Check for activity, update time. */
    debug_fdsets("Entering select", max_fd+1, &read_fds, &write_fds, &except_fds, timeout);
    select_result = select(max_fd + 1, &read_fds, &write_fds, NULL, timeout);
    debug_fdsets("After select", max_fd+1, &read_fds, &write_fds, &except_fds, timeout);
    now = time(NULL) + clock_skew;
    if (select_result < 0) {
        if (errno != EINTR) {
            log_module(MAIN_LOG, LOG_ERROR, "select() error %d: %s", errno, strerror(errno));
            close_socket();
        }
        return 1;
    }

    /* Call back anybody that has connect or read activity and wants to know. */
    for (nn=0; nn<fds_size; nn++) {
        ioset_events(fds[nn], FD_ISSET(nn, &read_fds) | FD_ISSET(nn, &except_fds), FD_ISSET(nn, &write_fds));
    }
    return 0;
}

struct io_engine io_engine_select = {
    .name = "select",
    .init = ioset_select_init,
    .add = ioset_select_add,
    .remove = ioset_select_remove,
    .update = ioset_select_update,
    .loop = ioset_select_loop,
    .cleanup = ioset_select_cleanup,
};

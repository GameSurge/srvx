/* ioset epoll_*() backend for srvx
 * Copyright 2006 srvx Development Team
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

#ifdef HAVE_SYS_EPOLL_H
# include <sys/epoll.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif

extern int clock_skew;
static int epoll_fd;

static int
ioset_epoll_init(void)
{
    epoll_fd = epoll_create(1024);
    if (epoll_fd < 0)
        return 0;
    return 1;
}

static int
ioset_epoll_events(struct io_fd *fd)
{
    return EPOLLHUP
        | EPOLLIN
        | (fd_wants_writes(fd) ? EPOLLOUT : 0)
        ;
}

static void
ioset_epoll_add(struct io_fd *fd)
{
    struct epoll_event evt;
    int res;

    evt.events = ioset_epoll_events(fd);
    evt.data.ptr = fd;
    res = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd->fd, &evt);
    if (res < 0)
        log_module(MAIN_LOG, LOG_ERROR, "Unable to add fd %d to epoll: %s", fd->fd, strerror(errno));
}

static void
ioset_epoll_remove(struct io_fd *fd, int closed)
{
    static struct epoll_event evt;
    if (!closed)
        (void)epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd->fd, &evt);
}

static void
ioset_epoll_update(struct io_fd *fd)
{
    struct epoll_event evt;
    int res;

    evt.events = ioset_epoll_events(fd);
    evt.data.ptr = fd;
    res = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd->fd, &evt);
    if (res < 0)
        log_module(MAIN_LOG, LOG_ERROR, "Unable to modify fd %d for epoll: %s", fd->fd, strerror(errno));
}

static void
ioset_epoll_cleanup(void)
{
    close(epoll_fd);
}

static int
ioset_epoll_loop(struct timeval *timeout)
{
    struct epoll_event evts[32];
    int events;
    int msec;
    int res;
    int ii;

    msec = timeout ? (timeout->tv_sec * 1000 + timeout->tv_usec / 1000) : -1;

    res = epoll_wait(epoll_fd, evts, ArrayLength(evts), msec);
    now = time(NULL) + clock_skew;
    if (res < 0) {
        if (errno != EINTR) {
            log_module(MAIN_LOG, LOG_ERROR, "epoll_wait() error %d: %s", errno, strerror(errno));
            close_socket();
        }
        return 1;
    }

    for (ii = 0; ii < res; ++ii) {
        events = evts[ii].events;
        ioset_events(evts[ii].data.ptr, (events & (EPOLLIN | EPOLLHUP)), (events & EPOLLOUT));
    }

    return 0;
}

struct io_engine io_engine_epoll = {
    .name = "epoll",
    .init = ioset_epoll_init,
    .add = ioset_epoll_add,
    .remove = ioset_epoll_remove,
    .update = ioset_epoll_update,
    .loop = ioset_epoll_loop,
    .cleanup = ioset_epoll_cleanup,
};

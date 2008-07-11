/* ioset kqueue()/kevent() backend for srvx
 * Copyright 2008 srvx Development Team
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

#ifdef HAVE_SYS_EVENT_H
# include <sys/event.h>
#endif

#define MAX_EVENTS 16

extern int clock_skew;
static int kq_fd;

static int
ioset_kevent_init(void)
{
    kq_fd = kqueue();
    return kq_fd >= 0;
}

static void
ioset_kevent_add(struct io_fd *fd)
{
    struct kevent changes[2];
    int nchanges = 0;
    int res;

    EV_SET(&changes[nchanges++], fd->fd, EVFILT_READ, EV_ADD, 0, 0, fd);
    EV_SET(&changes[nchanges++], fd->fd, EVFILT_WRITE, fd_wants_writes(fd) ? EV_ADD : EV_DELETE, 0, 0, fd);
    res = kevent(kq_fd, changes, nchanges, NULL, 0, NULL);
    if (res < 0) {
	log_module(MAIN_LOG, LOG_ERROR, "kevent() add failed: %s", strerror(errno));
    }
}

static void
ioset_kevent_remove(struct io_fd *fd, int closed)
{
    if (!closed) {
	struct kevent changes[2];
	int nchanges = 0;
	int res;

	EV_SET(&changes[nchanges++], fd->fd, EVFILT_READ, EV_DELETE, 0, 0, fd);
	EV_SET(&changes[nchanges++], fd->fd, EVFILT_WRITE, EV_DELETE, 0, 0, fd);
	res = kevent(kq_fd, changes, nchanges, NULL, 0, NULL);
	if (res < 0) {
	    log_module(MAIN_LOG, LOG_ERROR, "kevent() remove failed: %s", strerror(errno));
	}
    }
}

static void
ioset_kevent_update(struct io_fd *fd)
{
    ioset_kevent_add(fd);
}

static void
ioset_kevent_cleanup(void)
{
    close(kq_fd);
}

static int
ioset_kevent_loop(struct timeval *timeout)
{
    struct kevent events[MAX_EVENTS];
    struct timespec ts;
    struct timespec *pts;
    int is_write;
    int is_read;
    int res;
    int ii;

    /* Try to get events from the kernel. */
    if (timeout) {
	ts.tv_sec = timeout->tv_sec;
	ts.tv_nsec = timeout->tv_usec * 1000;
	pts = &ts;
    } else {
	pts = NULL;
    }
    res = kevent(kq_fd, NULL, 0, events, MAX_EVENTS, pts);
    if (res < 0) {
	log_module(MAIN_LOG, LOG_ERROR, "kevent() poll failed: %s", strerror(errno));
	return 1;
    }

    /* Process the events we got. */
    for (ii = 0; ii < res; ++ii) {
	is_write = events[ii].filter == EVFILT_WRITE;
	is_read = events[ii].filter == EVFILT_READ;
	ioset_events(events[ii].udata, is_read, is_write);
    }

    return 0;
}

struct io_engine io_engine_kevent = {
    .name = "kevent",
    .init = ioset_kevent_init,
    .add = ioset_kevent_add,
    .remove = ioset_kevent_remove,
    .update = ioset_kevent_update,
    .loop = ioset_kevent_loop,
    .cleanup = ioset_kevent_cleanup,
};

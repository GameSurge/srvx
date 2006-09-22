/* srvx event loop implementation details
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

#if !defined(IOSET_IMPL_H)
#define IOSET_IMPL_H

#include "ioset.h"

struct timeval;

#define fd_wants_reads(FD) ((FD)->wants_reads || (FD)->state == IO_LISTENING)
#define fd_wants_writes(FD) (((FD)->send.get != (FD)->send.put) || (FD)->state == IO_CONNECTING)

struct io_engine {
    const char *name;
    int (*init)(void);
    void (*add)(struct io_fd *fd);
    void (*remove)(struct io_fd *fd);
    void (*update)(struct io_fd *fd);
    int (*loop)(struct timeval *timeout);
    void (*cleanup)(void);
};

void ioset_events(struct io_fd *fd, int readable, int writable);

#endif /* !defined(IOSET_IMPL_H) */

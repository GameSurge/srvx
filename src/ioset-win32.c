/* Win32 ioset backend for srvx
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

/* This is massively kludgy.  Unfortunately, the only performant I/O
 * multiplexer with halfway decent semantics under Windows is
 * WSAAsyncSelect() -- which requires a window that can receive
 * messages.
 *
 * So ioset_win32_init() creates a hidden window and sets it up for
 * asynchronous socket notifications.
 */

static int
ioset_win32_init(void)
{
    WSADATA wsadata;
    int res;

    res = WSAStartup(MAKEWORD(2, 0), &wsadata);
    // TODO: finish implementing ioset_win32_init()
    return 0;
}

static void
ioset_win32_add(struct io_fd *fd)
{
    // TODO: implement ioset_win32_add()
}

static void
ioset_win32_remove(struct io_fd *fd)
{
    // TODO: implement ioset_win32_remove()
}

static void
ioset_win32_update(struct io_fd *fd)
{
    // TODO: implement ioset_win32_update()
}

static void
ioset_win32_cleanup(void)
{
    // TODO: finish implementing ioset_win32_cleanup()
    WSACleanup();
}

static int
ioset_win32_loop(struct timeval *timeout)
{
    // TODO: implement ioset_win32_loop()
    return 0;
}

struct io_engine io_engine_win32 = {
    .name = "win32",
    .init = ioset_win32_init,
    .add = ioset_win32_add,
    .remove = ioset_win32_remove,
    .update = ioset_win32_update,
    .loop = ioset_win32_loop,
    .cleanup = ioset_win32_cleanup,
};

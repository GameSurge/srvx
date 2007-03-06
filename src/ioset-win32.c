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

#define IDT_TIMER1 1000
#define IDT_SOCKET 1001

static HWND ioset_window;
static struct io_fd **fds;
static unsigned int fds_used;
static unsigned int fds_size;

static unsigned int
io_fd_pos(int fd)
{
    int lower = 0;
    int upper = fds_used - 1;

    while (lower <= upper)
    {
        int mid = (upper + lower) / 2;
        if (fd < fds[mid]->fd)
            upper = mid - 1;
        else if (fd > fds[mid]->fd)
            lower = mid + 1;
        else
            break;
    }
    return lower;
}

static struct io_fd *
io_fd_from_socket(int fd)
{
    unsigned int ii;
    ii = io_fd_pos(fd);
    return ((ii < fds_used) && (fds[ii]->fd == fd)) ? fds[ii] : NULL;
}

static LRESULT CALLBACK
ioset_win32_wndproc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    struct io_fd *fd;
    int events;
    int err;

    if (hWnd == ioset_window) switch (uMsg)
    {
    case IDT_TIMER1:
        return 0;
    case IDT_SOCKET:
        fd = io_fd_from_socket(wParam);
        events = WSAGETSELECTEVENT(lParam);
        err = WSAGETSELECTERROR(lParam);
        ioset_events(fd, (events & (FD_READ | FD_ACCEPT | FD_CLOSE)) != 0, (events & (FD_WRITE | FD_CONNECT)) != 0);
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static int
ioset_win32_init(void)
{
    WSADATA wsadata;
    WNDCLASSEX wcx;
    HINSTANCE hinst;
    int res;

    // Start Windows Sockets.
    res = WSAStartup(MAKEWORD(2, 0), &wsadata);
    if (res)
    {
        log_module(MAIN_LOG, LOG_FATAL, "Unable to start Windows Sockets (%d)", res);
    }

    // Get Windows HINSTANCE.
    hinst = GetModuleHandle(NULL);

    // Describe and register a window class.
    memset(&wcx, 0, sizeof(wcx));
    wcx.cbSize = sizeof(wcx);
    wcx.lpfnWndProc = ioset_win32_wndproc;
    wcx.hInstance = hinst;
    wcx.lpszClassName = "srvxMainWindow";
    if (!RegisterClassEx(&wcx))
    {
        log_module(MAIN_LOG, LOG_FATAL, "Unable to register window class (%lu)", GetLastError());
    }

    ioset_window = CreateWindow("srvxMainWindow", PACKAGE_STRING, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hinst, NULL);
    if (!ioset_window)
    {
        log_module(MAIN_LOG, LOG_FATAL, "Unable to create window (%lu)", GetLastError());
    }

    return 0;
}

static long
ioset_win32_events(const struct io_fd *fd)
{
    switch (fd->state)
    {
    case IO_CLOSED:
        return 0;
    case IO_LISTENING:
        return FD_ACCEPT;
    case IO_CONNECTING:
        return FD_CONNECT;
    case IO_CONNECTED:
        return FD_READ | FD_CLOSE | (fd_wants_writes(fd) ? FD_WRITE : 0);
    }
}

static void
ioset_win32_update(struct io_fd *fd)
{
    int rc;
    long events;

    events = ioset_win32_events(fd);
    rc = WSAAsyncSelect(fd->fd, ioset_window, IDT_SOCKET, events);
    if (rc)
    {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to add events %#lx for fd %#x (%d)", events, fd->fd, WSAGetLastError());
    }
}

static void
ioset_win32_add(struct io_fd *fd)
{
    unsigned int pos;
    unsigned int ii;

    // Make sure fds[] can hold a new entry.
    if (fds_used + 1 >= fds_size)
    {
        struct io_fd **new_fds;
        unsigned int new_size;

        new_size = (fds_size < 8) ? 8 : fds_size * 2;
        new_fds = calloc(new_size * 2, sizeof(new_fds[0]));
        if (!new_fds)
        {
            log_module(MAIN_LOG, LOG_FATAL, "Unable to allocate %u-entry socket array.", new_size);
        }
        for (ii = 0; ii < fds_used; ++ii)
            new_fds[ii] = fds[ii];
        free(fds);
        fds = new_fds;
        fds_size = new_size;
    }

    // Insert fd into the appropriate spot in fds[].
    pos = io_fd_pos(fd->fd);
    for (ii = pos; ii < fds_used; ++ii)
        fds[ii + 1] = fds[ii];
    fds[pos] = fd;
    ++fds_used;

    // Ask the OS for future notifications.
    ioset_win32_update(fd);
}

static void
ioset_win32_remove(struct io_fd *fd, int os_closed)
{
    unsigned int pos;
    int rc;

    // Unregister from the OS.
    if (!os_closed)
    {
        unsigned long ulong;

        rc = WSAAsyncSelect(fd->fd, ioset_window, IDT_SOCKET, 0);
        if (rc)
        {
            log_module(MAIN_LOG, LOG_ERROR, "Unable to remove events for fd %#x (%d)", fd->fd, WSAGetLastError());
        }

        ulong = 0;
        ioctlsocket(fd->fd, FIONBIO, &ulong);
    }

    // Remove from the fds[] array.
    pos = io_fd_pos(fd->fd);
    for (--fds_used; pos < fds_used; ++pos)
        fds[pos] = fds[pos + 1];
}

static void
ioset_win32_cleanup(void)
{
    DestroyWindow(ioset_window);
    ioset_window = NULL;
    WSACleanup();
}

static int
ioset_win32_loop(struct timeval *timeout)
{
    MSG msg;
    BOOL not_really_bool;
    int msec;

    // Make sure we are woken up after the appropriate time.
    msec = (timeout->tv_sec * 1000) + (timeout->tv_usec / 1000);
    SetTimer(ioset_window, IDT_TIMER1, msec, NULL);
    // Do a blocking read of the message queue.
    not_really_bool = GetMessage(&msg, NULL, 0, 0);
    KillTimer(ioset_window, IDT_TIMER1);
    if (not_really_bool < 0)
    {
        return 1;
    }
    else if (not_really_bool == 0)
    {
        quit_services = 1;
        return 0;
    }
    else
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
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

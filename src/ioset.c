/* ioset.h - srvx event loop
 * Copyright 2002-2004, 2006 srvx Development Team
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
#include "log.h"
#include "timeq.h"
#include "saxdb.h"
#include "conf.h"

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef WITH_IOSET_WIN32

# undef errno
# define errno WSAGetLastError()
# undef EINPROGRESS
# define EINPROGRESS WSAEINPROGRESS
# undef EHOSTUNREACH
# define EHOSTUNREACH WSAEHOSTUNREACH
# undef ECONNREFUSED
# define ECONNREFUSED WSAECONNREFUSED
# undef EAGAIN
# define EAGAIN WSAEWOULDBLOCK
# define strerror wsa_strerror

static const char *
wsa_strerror(int wsa_err)
{
    switch (wsa_err)
    {
    case WSAEINTR: return "Operation interrupted";
    case WSAEBADF: return "Bad file descriptor";
    case WSAEACCES: return "Permission denied";
    case WSAEFAULT: return "Invalid address";
    case WSAEINVAL: return "Invalid parameter";
    case WSAEMFILE: return "Too many open files";
    case WSAEWOULDBLOCK: return "Try again later";
    case WSAEINPROGRESS: return "Operation in progress";
    case WSAEALREADY: return "Operation already in progress";
    case WSAENOTSOCK: return "Not a socket";
    case WSAEDESTADDRREQ: return "Destination address required";
    case WSAEMSGSIZE: return "Invalid message size";
    case WSAEPROTOTYPE: return "Invalid protocol type for socket";
    case WSAENOPROTOOPT: return "Invalid protocol option";
    case WSAEPROTONOSUPPORT: return "Protocol not supported";
    case WSAEOPNOTSUPP: return "Operation not supported";
    case WSAEADDRINUSE: return "Address already in use";
    case WSAEADDRNOTAVAIL: return "Address not available";
    case WSAENETDOWN: return "Network down";
    case WSAENETUNREACH: return "Network unreachable";
    case WSAENETRESET: return "Network reset";
    case WSAECONNABORTED: return "Connection aborted";
    case WSAECONNRESET: return "Connection reset by peer";
    case WSAECONNREFUSED: return "Connection refused";
    }
    return "unknown error";
}

#endif /* WITH_IOSET_WIN32 */

#define IS_EOL(CH) ((CH) == '\n')

extern int uplink_connect(void);
int clock_skew;
int do_write_dbs;
int do_reopen;
static struct io_engine *engine;
static struct io_fd *active_fd;

static void
ioq_init(struct ioq *ioq, int size) {
    ioq->buf = malloc(size);
    ioq->get = ioq->put = 0;
    ioq->size = size;
}

static unsigned int
ioq_put_avail(const struct ioq *ioq) {
    /* Subtract 1 from ioq->get to be sure we don't fill the buffer
     * and make it look empty even when there's data in it. */
    if (ioq->put < ioq->get)
        return ioq->get - ioq->put - 1;
    else if (ioq->get == 0)
        return ioq->size - ioq->put - 1;
    else
        return ioq->size - ioq->put;
}

static unsigned int
ioq_get_avail(const struct ioq *ioq) {
    return ((ioq->put < ioq->get) ? ioq->size : ioq->put) - ioq->get;
}

static unsigned int
ioq_used(const struct ioq *ioq) {
    return ((ioq->put < ioq->get) ? ioq->size : 0) + ioq->put - ioq->get;
}

static unsigned int
ioq_grow(struct ioq *ioq) {
    int new_size = ioq->size << 1;
    char *new_buf = malloc(new_size);
    int get_avail = ioq_get_avail(ioq);
    memcpy(new_buf, ioq->buf + ioq->get, get_avail);
    if (ioq->put < ioq->get)
        memcpy(new_buf + get_avail, ioq->buf, ioq->put);
    free(ioq->buf);
    ioq->put = ioq_used(ioq);
    ioq->get = 0;
    ioq->buf = new_buf;
    ioq->size = new_size;
    return new_size - ioq->put - 1;
}

extern struct io_engine io_engine_kevent;
extern struct io_engine io_engine_epoll;
extern struct io_engine io_engine_win32;
extern struct io_engine io_engine_select;

void
ioset_init(void)
{
    if (engine) /* someone beat us to it */
        return;

#if WITH_IOSET_KEVENT
    if (!engine && io_engine_kevent.init())
        engine = &io_engine_kevent;
#endif

#if WITH_IOSET_EPOLL
    if (!engine && io_engine_epoll.init())
        engine = &io_engine_epoll;
#endif

#if WITH_IOSET_WIN32
    if (!engine && io_engine_win32.init())
        engine = &io_engine_win32;
#endif

    if (engine) {
        /* we found one that works */
    } else if (io_engine_select.init())
        engine = &io_engine_select;
    else
        log_module(MAIN_LOG, LOG_FATAL, "No usable I/O engine found.");
    log_module(MAIN_LOG, LOG_DEBUG, "Using %s I/O engine.", engine->name);
}

void
ioset_cleanup(void) {
    engine->cleanup();
}

struct io_fd *
ioset_add(int fd) {
    struct io_fd *res;
    int flags;

    if (fd < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "Somebody called ioset_add(%d) on a negative fd!", fd);
        return 0;
    }
    if (!engine)
        ioset_init();
    res = calloc(1, sizeof(*res));
    if (!res)
        return 0;
    res->fd = fd;
    ioq_init(&res->send, 1024);
    ioq_init(&res->recv, 1024);
#if defined(F_GETFL)
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags|O_NONBLOCK);
    flags = fcntl(fd, F_GETFD);
    fcntl(fd, F_SETFD, flags|FD_CLOEXEC);
#else
    /* I hope you're using the Win32 backend or something else that
     * automatically marks the file descriptor non-blocking...
     */
    (void)flags;
#endif
    engine->add(res);
    return res;
}

struct io_fd *ioset_listen(struct sockaddr *local, unsigned int sa_size, void *data, void (*accept_cb)(struct io_fd *listener, struct io_fd *new_connect))
{
    struct io_fd *io_fd;
    unsigned int opt;
    int res;
    int fd;

    fd = socket(local ? local->sa_family : PF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to create listening socket: %s", strerror(errno));
        return NULL;
    }

    if (local && sa_size) {
        opt = 1;
        res = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
        if (res < 0) {
            log_module(MAIN_LOG, LOG_WARNING, "Unable to mark listener address as re-usable: %s", strerror(errno));
        }

        res = bind(fd, local, sa_size);
        if (res < 0) {
            log_module(MAIN_LOG, LOG_ERROR, "Unable to bind listening socket %d: %s", fd, strerror(errno));
            close(fd);
            return NULL;
        }
    }

    res = listen(fd, 1);
    if (res < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to listen on socket %d: %s", fd, strerror(errno));
        close(fd);
        return NULL;
    }

    io_fd = ioset_add(fd);
    if (!io_fd) {
        close(fd);
        return NULL;
    }
    io_fd->state = IO_LISTENING;
    io_fd->data = data;
    io_fd->accept_cb = accept_cb;
    engine->update(io_fd);
    return io_fd;
}

struct io_fd *
ioset_connect(struct sockaddr *local, unsigned int sa_size, const char *peer, unsigned int port, int blocking, void *data, void (*connect_cb)(struct io_fd *fd, int error)) {
    struct addrinfo hints;
    struct addrinfo *ai;
    struct io_fd *io_fd;
    struct io_fd *old_active;
    int res;
    int fd;
    char portnum[10];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = local ? local->sa_family : 0;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portnum, sizeof(portnum), "%u", port);
    res = getaddrinfo(peer, portnum, &hints, &ai);
    if (res != 0) {
        log_module(MAIN_LOG, LOG_ERROR, "getaddrinfo(%s, %s) failed: %s.", peer, portnum, gai_strerror(res));
        return NULL;
    }

    if (local) {
        if ((fd = socket(local->sa_family, SOCK_STREAM, 0)) < 0) {
            log_module(MAIN_LOG, LOG_ERROR, "socket() for %s returned errno %d (%s)", peer, errno, strerror(errno));
            freeaddrinfo(ai);
            return NULL;
        }
        if (bind(fd, local, sa_size) < 0) {
            log_module(MAIN_LOG, LOG_ERROR, "bind() of socket for %s (fd %d) returned errno %d (%s).  Will let operating system choose.", peer, fd, errno, strerror(errno));
        }
    } else {
        if ((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
            log_module(MAIN_LOG, LOG_ERROR, "socket() for %s returned errno %d (%s).", peer, errno, strerror(errno));
            freeaddrinfo(ai);
            return NULL;
        }
    }

    if (blocking) {
        res = connect(fd, ai->ai_addr, ai->ai_addrlen);
        io_fd = ioset_add(fd);
    } else {
        io_fd = ioset_add(fd);
        res = connect(fd, ai->ai_addr, ai->ai_addrlen);
    }
    freeaddrinfo(ai);
    if (!io_fd) {
        close(fd);
        return NULL;
    }
    io_fd->state = IO_CONNECTING;
    io_fd->data = data;
    io_fd->connect_cb = connect_cb;
    if (res < 0) {
        switch (errno) {
        case EINPROGRESS: /* only if !blocking */
            engine->update(io_fd);
            return io_fd;
        default:
            log_module(MAIN_LOG, LOG_ERROR, "connect(%s:%d) (fd %d) returned errno %d (%s).", peer, port, io_fd->fd, errno, strerror(errno));
            /* fall through */
        case EHOSTUNREACH:
        case ECONNREFUSED:
            ioset_close(io_fd, 1);
            return NULL;
        }
    }
    io_fd->state = IO_CONNECTED;
    old_active = active_fd;
    if (connect_cb)
        connect_cb(io_fd, ((res < 0) ? errno : 0));
    if (active_fd)
        engine->update(io_fd);
    if (old_active != io_fd)
        active_fd = old_active;
    return io_fd;
}

void ioset_update(struct io_fd *fd) {
    engine->update(fd);
}

static void
ioset_try_write(struct io_fd *fd) {
    int res;
    unsigned int req;

    req = ioq_get_avail(&fd->send);
    res = send(fd->fd, fd->send.buf+fd->send.get, req, 0);
    if (res < 0) {
        if (errno != EAGAIN) {
            log_module(MAIN_LOG, LOG_ERROR, "send() on fd %d error %d: %s", fd->fd, errno, strerror(errno));
        }
    } else {
        fd->send.get += res;
        if (fd->send.get == fd->send.size)
            fd->send.get = 0;
        engine->update(fd);
    }
}

void
ioset_close(struct io_fd *fdp, int os_close) {
    if (!fdp)
        return;
    if (active_fd == fdp)
        active_fd = NULL;
    if (fdp->destroy_cb)
        fdp->destroy_cb(fdp);
#if defined(HAVE_WSAEVENTSELECT)
    /* This is one huge kludge.  Sorry! */
    if (fdp->send.get != fdp->send.put && (os_close & 2)) {
        engine->remove(fdp, 0);
        ioset_try_write(fdp);
        /* it may need to send the beginning of the buffer now.. */
        if (fdp->send.get != fdp->send.put)
            ioset_try_write(fdp);
    }
    free(fdp->send.buf);
    free(fdp->recv.buf);
    if (os_close & 1)
        closesocket(fdp->fd);
#else
    if (fdp->send.get != fdp->send.put && (os_close & 2)) {
        int flags;

        flags = fcntl(fdp->fd, F_GETFL);
        fcntl(fdp->fd, F_SETFL, flags&~O_NONBLOCK);
        ioset_try_write(fdp);
        /* it may need to send the beginning of the buffer now.. */
        if (fdp->send.get != fdp->send.put)
            ioset_try_write(fdp);
    }
    free(fdp->send.buf);
    free(fdp->recv.buf);
    if (os_close & 1)
        close(fdp->fd);
    engine->remove(fdp, os_close & 1);
#endif
    free(fdp);
}

static void
ioset_accept(struct io_fd *listener)
{
    struct io_fd *old_active;
    struct io_fd *new_fd;
    int fd;

    fd = accept(listener->fd, NULL, 0);
    if (fd < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "Unable to accept new connection on listener %d: %s", listener->fd, strerror(errno));
        return;
    }

    new_fd = ioset_add(fd);
    new_fd->state = IO_CONNECTED;
    old_active = active_fd;
    active_fd = new_fd;
    listener->accept_cb(listener, new_fd);
    assert(active_fd == NULL || active_fd == new_fd);
    if (active_fd == new_fd) {
        if (new_fd->send.get != new_fd->send.put)
            ioset_try_write(new_fd);
        else
            engine->update(new_fd);
    }
    active_fd = old_active;
}

static int
ioset_find_line_length(struct io_fd *fd) {
    unsigned int pos, max, len;
    len = 0;
    max = (fd->recv.put < fd->recv.get) ? fd->recv.size : fd->recv.put;
    for (pos = fd->recv.get; pos < max; ++pos, ++len)
        if (IS_EOL(fd->recv.buf[pos]))
            return fd->line_len = len + 1;
    if (fd->recv.put < fd->recv.get)
        for (pos = 0; pos < fd->recv.put; ++pos, ++len)
            if (IS_EOL(fd->recv.buf[pos]))
                return fd->line_len = len + 1;
    return fd->line_len = 0;
}

static void
ioset_buffered_read(struct io_fd *fd) {
    int put_avail, nbr;

    if (!(put_avail = ioq_put_avail(&fd->recv)))
        put_avail = ioq_grow(&fd->recv);
    nbr = recv(fd->fd, fd->recv.buf + fd->recv.put, put_avail, 0);
    if (nbr < 0) {
        if (errno != EAGAIN) {
            log_module(MAIN_LOG, LOG_ERROR, "Unexpected recv() error %d on fd %d: %s", errno, fd->fd, strerror(errno));
            /* Just flag it as EOF and call readable_cb() to notify the fd's owner. */
            fd->state = IO_CLOSED;
            fd->readable_cb(fd);
            if (active_fd == fd)
                engine->update(fd);
        }
    } else if (nbr == 0) {
        fd->state = IO_CLOSED;
        fd->readable_cb(fd);
        if (active_fd == fd)
            engine->update(fd);
    } else {
        if (fd->line_len == 0) {
            unsigned int pos;
            for (pos = fd->recv.put; pos < fd->recv.put + nbr; ++pos) {
                if (IS_EOL(fd->recv.buf[pos])) {
                    if (fd->recv.put < fd->recv.get)
                        fd->line_len = fd->recv.size + pos + 1 - fd->recv.get;
                    else
                        fd->line_len = pos + 1 - fd->recv.get;
                    break;
                }
            }
        }
        fd->recv.put += nbr;
        if (fd->recv.put == fd->recv.size)
            fd->recv.put = 0;
        while (fd->line_len > 0) {
            struct io_fd *old_active;
            int died = 0;

            old_active = active_fd;
            active_fd = fd;
            fd->readable_cb(fd);
            if (active_fd)
                ioset_find_line_length(fd);
            else
                died = 1;
            if (old_active != fd)
                active_fd = old_active;
            if (died)
                break;
        }
    }
}

int
ioset_line_read(struct io_fd *fd, char *dest, int max) {
    int line_len;
    int avail;
    int done;

    line_len = fd->line_len;
    if ((fd->state == IO_CLOSED) && (!ioq_get_avail(&fd->recv) ||  (line_len < 0)))
        return 0;
    if (line_len < 0)
        return -1;
    if (line_len < max)
        max = line_len;
    avail = ioq_get_avail(&fd->recv);
    if (max > avail) {
        memcpy(dest, fd->recv.buf + fd->recv.get, avail);
        assert(fd->recv.get + avail == fd->recv.size);
        fd->recv.get = 0;
        done = avail;
    } else {
        done = 0;
    }
    memcpy(dest + done, fd->recv.buf + fd->recv.get, max - done);
    fd->recv.get += max - done;
    if (fd->recv.get == fd->recv.size)
        fd->recv.get = 0;
    dest[max - 1] = 0;
    ioset_find_line_length(fd);
    return line_len;
}

void
ioset_events(struct io_fd *fd, int readable, int writable)
{
    if (!fd || (!readable && !writable))
        return;
    active_fd = fd;
    switch (fd->state) {
    case IO_CLOSED:
        break;
    case IO_LISTENING:
        if (active_fd && readable)
            ioset_accept(fd);
        break;
    case IO_CONNECTING:
        assert(active_fd == NULL || active_fd == fd);
        if (active_fd && readable) {
            socklen_t arglen;
            int rc;
            arglen = sizeof(rc);
            if (getsockopt(fd->fd, SOL_SOCKET, SO_ERROR, &rc, &arglen) < 0)
                rc = errno;
            fd->state = IO_CLOSED;
            if (fd->connect_cb)
                fd->connect_cb(fd, rc);
        } else if (active_fd && writable) {
            fd->state = IO_CONNECTED;
            if (fd->connect_cb)
                fd->connect_cb(fd, 0);
        }
        if (active_fd != fd)
            break;
        engine->update(fd);
        /* fall through */
    case IO_CONNECTED:
        assert(active_fd == NULL || active_fd == fd);
        if (active_fd && readable) {
            if (fd->line_reads)
                ioset_buffered_read(fd);
            else
                fd->readable_cb(fd);
        }

        assert(active_fd == NULL || active_fd == fd);
        if (active_fd && writable)
            ioset_try_write(fd);
        break;
    }
}

void
ioset_run(void) {
    extern struct io_fd *socket_io_fd;
    struct timeval timeout;
    unsigned long wakey;

    while (!quit_services) {
        while (!socket_io_fd)
            uplink_connect();

        /* How long to sleep? (fill in select_timeout) */
        wakey = timeq_next();
        if (wakey < now)
            timeout.tv_sec = 0;
        else
            timeout.tv_sec = wakey - now;
        timeout.tv_usec = 0;

        if (engine->loop(&timeout))
            continue;

        /* Call any timeq events we need to call. */
        timeq_run();
        if (do_write_dbs) {
            saxdb_write_all();
            do_write_dbs = 0;
        }
        if (do_reopen) {
            extern char *services_config;
            conf_read(services_config);
            do_reopen = 0;
        }
    }
}

void
ioset_write(struct io_fd *fd, const char *buf, unsigned int nbw) {
    unsigned int avail;
    while (ioq_used(&fd->send) + nbw >= fd->send.size)
        ioq_grow(&fd->send);
    avail = ioq_put_avail(&fd->send);
    if (nbw > avail) {
        memcpy(fd->send.buf + fd->send.put, buf, avail);
        buf += avail;
        nbw -= avail;
        fd->send.put = 0;
    }
    memcpy(fd->send.buf + fd->send.put, buf, nbw);
    fd->send.put += nbw;
    if (fd->send.put == fd->send.size)
        fd->send.put = 0;
    engine->update(fd);
}

int
ioset_printf(struct io_fd *fd, const char *fmt, ...) {
    char tmpbuf[MAXLEN];
    va_list ap;
    int res;

    va_start(ap, fmt);
    res = vsnprintf(tmpbuf, sizeof(tmpbuf), fmt, ap);
    va_end(ap);
    if (res > 0 && (size_t)res <= sizeof(tmpbuf))
        ioset_write(fd, tmpbuf, res);
    return res;
}

void
ioset_set_time(unsigned long new_now) {
    clock_skew = new_now - time(NULL);
    now = new_now;
}

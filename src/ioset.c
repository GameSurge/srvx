/* ioset.h - srvx event loop
 * Copyright 2002-2004 srvx Development Team
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

#include "ioset.h"
#include "log.h"
#include "timeq.h"
#include "saxdb.h"
#include "conf.h"

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif
#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifndef IOSET_DEBUG
#define IOSET_DEBUG 0
#endif

#define IS_EOL(CH) ((CH) == '\n')

extern int uplink_connect(void);
static int clock_skew;
int do_write_dbs;
int do_reopen;

static struct io_fd **fds;
static unsigned int fds_size;
static fd_set read_fds, write_fds;

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
    return new_size - ioq->put;
}

void
ioset_cleanup(void) {
    free(fds);
}

struct io_fd *
ioset_add(int fd) {
    struct io_fd *res;
    int flags;

    if (fd < 0) {
        log_module(MAIN_LOG, LOG_ERROR, "Somebody called ioset_add(%d) on a negative fd!", fd);
        return 0;
    }
    res = calloc(1, sizeof(*res));
    if (!res)
        return 0;
    res->fd = fd;
    ioq_init(&res->send, 1024);
    ioq_init(&res->recv, 1024);
    if ((unsigned)fd >= fds_size) {
        unsigned int old_size = fds_size;
        fds_size = fd + 8;
        fds = realloc(fds, fds_size*sizeof(*fds));
        memset(fds+old_size, 0, (fds_size-old_size)*sizeof(*fds));
    }
    fds[fd] = res;
    flags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, flags|O_NONBLOCK);
    return res;
}

struct io_fd *
ioset_connect(struct sockaddr *local, unsigned int sa_size, const char *peer, unsigned int port, int blocking, void *data, void (*connect_cb)(struct io_fd *fd, int error)) {
    int fd, res;
    struct io_fd *io_fd;
    struct sockaddr_in sin;
    unsigned long ip;

    if (!getipbyname(peer, &ip)) {
        log_module(MAIN_LOG, LOG_ERROR, "getipbyname(%s) failed.", peer);
        return NULL;
    }
    sin.sin_addr.s_addr = ip;
    if (local) {
        if ((fd = socket(local->sa_family, SOCK_STREAM, 0)) < 0) {
            log_module(MAIN_LOG, LOG_ERROR, "socket() for %s returned errno %d (%s)", peer, errno, strerror(errno));
            return NULL;
        }
        if (bind(fd, local, sa_size) < 0) {
            log_module(MAIN_LOG, LOG_ERROR, "bind() of socket for %s (fd %d) returned errno %d (%s).  Will let operating system choose.", peer, fd, errno, strerror(errno));
        }
    } else {
        if ((fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
            log_module(MAIN_LOG, LOG_ERROR, "socket() for %s returned errno %d (%s).", peer, errno, strerror(errno));
            return NULL;
        }
    }
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    if (blocking) {
        res = connect(fd, (struct sockaddr*)&sin, sizeof(sin));
        io_fd = ioset_add(fd);
    } else {
        io_fd = ioset_add(fd);
        res = connect(fd, (struct sockaddr*)&sin, sizeof(sin));
    }
    if (!io_fd) {
        close(fd);
        return NULL;
    }
    io_fd->data = data;
    io_fd->connect_cb = connect_cb;
    if (res < 0) {
        switch (errno) {
        case EINPROGRESS: /* only if !blocking */
            return io_fd;
        default:
            log_module(MAIN_LOG, LOG_ERROR, "connect(%s:%d) (fd %d) returned errno %d (%s).", peer, port, io_fd->fd, errno, strerror(errno));
            /* then fall through */
        case EHOSTUNREACH:
        case ECONNREFUSED:
            ioset_close(io_fd->fd, 1);
            return NULL;
        }
    }
    if (connect_cb)
        connect_cb(io_fd, ((res < 0) ? errno : 0));
    return io_fd;
}

static void
ioset_try_write(struct io_fd *fd) {
    int res;
    unsigned int req = ioq_get_avail(&fd->send);
    res = write(fd->fd, fd->send.buf+fd->send.get, req);
    if (res < 0) {
        switch (errno) {
        case EAGAIN:
            break;
        default:
            log_module(MAIN_LOG, LOG_ERROR, "write() on fd %d error %d: %s", fd->fd, errno, strerror(errno));
        }
    } else {
        fd->send.get += res;
        if (fd->send.get == fd->send.size)
            fd->send.get = 0;
    }
}

void
ioset_close(int fd, int os_close) {
    struct io_fd *fdp;
    if (!(fdp = fds[fd]))
        return;
    fds[fd] = NULL;
    if (fdp->destroy_cb)
        fdp->destroy_cb(fdp);
    if (fdp->send.get != fdp->send.put) {
        int flags = fcntl(fd, F_GETFL);
        fcntl(fd, F_SETFL, flags&~O_NONBLOCK);
        ioset_try_write(fdp);
        /* it may need to send the beginning of the buffer now.. */
        if (fdp->send.get != fdp->send.put)
            ioset_try_write(fdp);
    }
    free(fdp->send.buf);
    free(fdp->recv.buf);
    if (os_close)
        close(fd);
    free(fdp);
    FD_CLR(fd, &read_fds);
    FD_CLR(fd, &write_fds);
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
    int put_avail, nbr, fdnum;
    
    if (!(put_avail = ioq_put_avail(&fd->recv)))
        put_avail = ioq_grow(&fd->recv);
    nbr = read(fd->fd, fd->recv.buf + fd->recv.put, put_avail);
    if (nbr < 0) {
        switch (errno) {
        case EAGAIN:
            break;
        default:
            log_module(MAIN_LOG, LOG_ERROR, "Unexpected read() error %d on fd %d: %s", errno, fd->fd, strerror(errno));
            /* Just flag it as EOF and call readable_cb() to notify the fd's owner. */
            fd->eof = 1;
            fd->wants_reads = 0;
            fd->readable_cb(fd);
        }
    } else if (nbr == 0) {
        fd->eof = 1;
        fd->wants_reads = 0;
        fd->readable_cb(fd);
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
        fdnum = fd->fd;
        while (fd->wants_reads && (fd->line_len > 0)) {
            fd->readable_cb(fd);
            if (!fds[fdnum])
                break; /* make sure they didn't close on us */
            ioset_find_line_length(fd);
        }
    }
}

int
ioset_line_read(struct io_fd *fd, char *dest, int max) {
    int avail, done;
    if (fd->eof && (!ioq_get_avail(&fd->recv) ||  (fd->line_len < 0)))
        return 0;
    if (fd->line_len < 0)
        return -1;
    if (fd->line_len < max)
        max = fd->line_len;
    avail = ioq_get_avail(&fd->recv);
    if (max > avail) {
        memcpy(dest, fd->recv.buf + fd->recv.get, avail);
        fd->recv.get += avail;
        assert(fd->recv.get == fd->recv.size);
        fd->recv.get = 0;
        done = avail;
    } else {
        done = 0;
    }
    memcpy(dest + done, fd->recv.buf + fd->recv.get, max - done);
    fd->recv.get += max - done;
    if (fd->recv.get == fd->recv.size)
        fd->recv.get = 0;
    dest[max] = 0;
    ioset_find_line_length(fd);
    return max;
}

#if 1
#define debug_fdsets(MSG, NFDS, READ_FDS, WRITE_FDS, EXCEPT_FDS, SELECT_TIMEOUT) (void)0
#else
static void
debug_fdsets(const char *msg, int nfds, fd_set *read_fds, fd_set *write_fds, fd_set *except_fds, struct timeval *select_timeout) {
    static const char *flag_text[8] = { "---", "r", "w", "rw", "e", "er", "ew", "erw" };
    char buf[MAXLEN];
    int pos, ii, flags;
    struct timeval now;

    for (pos=ii=0; ii<nfds; ++ii) {
        flags  = (read_fds && FD_ISSET(ii, read_fds)) ? 1 : 0;
        flags |= (write_fds && FD_ISSET(ii, write_fds)) ? 2 : 0;
        flags |= (except_fds && FD_ISSET(ii, except_fds)) ? 4 : 0;
        if (!flags)
            continue;
        pos += sprintf(buf+pos, " %d%s", ii, flag_text[flags]);
    }
    gettimeofday(&now, NULL);
    if (select_timeout) {
        log_module(MAIN_LOG, LOG_DEBUG, "%s, at "FMT_TIME_T".%06ld:%s (timeout "FMT_TIME_T".%06ld)", msg, now.tv_sec, now.tv_usec, buf, select_timeout->tv_sec, select_timeout->tv_usec);
    } else {
        log_module(MAIN_LOG, LOG_DEBUG, "%s, at "FMT_TIME_T".%06ld:%s (no timeout)", msg, now.tv_sec, now.tv_usec, buf);
    }
}
#endif

void
ioset_run(void) {
    extern struct io_fd *socket_io_fd;
    struct timeval select_timeout;
    unsigned int nn;
    int select_result, max_fd;
    time_t wakey;
    struct io_fd *fd;

    while (!quit_services) {
        while (!socket_io_fd)
            uplink_connect();

        /* How long to sleep? (fill in select_timeout) */
        wakey = timeq_next();
        if ((wakey - now) < 0)
            select_timeout.tv_sec = 0;
        else
            select_timeout.tv_sec = wakey - now;
        select_timeout.tv_usec = 0;

        /* Set up read_fds and write_fds fdsets. */
        FD_ZERO(&read_fds);
        FD_ZERO(&write_fds);
        max_fd = 0;
        for (nn=0; nn<fds_size; nn++) {
            if (!(fd = fds[nn]))
                continue;
            max_fd = nn;
            if (fd->wants_reads)
                FD_SET(nn, &read_fds);
            if ((fd->send.get != fd->send.put) || !fd->connected)
                FD_SET(nn, &write_fds);
        }

        /* Check for activity, update time. */
        debug_fdsets("Entering select", max_fd+1, &read_fds, &write_fds, NULL, &select_timeout);
        select_result = select(max_fd + 1, &read_fds, &write_fds, NULL, &select_timeout);
        debug_fdsets("After select", max_fd+1, &read_fds, &write_fds, NULL, &select_timeout);
        now = time(NULL) + clock_skew;
        if (select_result < 0) {
            if (errno != EINTR) {
                log_module(MAIN_LOG, LOG_ERROR, "select() error %d: %s", errno, strerror(errno));
                close_socket();
            }
            continue;
        }

        /* Call back anybody that has connect or read activity and wants to know. */
        for (nn=0; nn<fds_size; nn++) {
            if (!(fd = fds[nn]))
                continue;
            if (FD_ISSET(nn, &read_fds)) {
                if (fd->line_reads)
                    ioset_buffered_read(fd);
                else
                    fd->readable_cb(fd);
            }
            if (FD_ISSET(nn, &write_fds) && !fd->connected) {
                int rc, arglen = sizeof(rc);
                if (getsockopt(fd->fd, SOL_SOCKET, SO_ERROR, &rc, &arglen) < 0)
                    rc = errno;
                fd->connected = 1;
                if (fd->connect_cb)
                    fd->connect_cb(fd, rc);
            }
            /* Note: check whether write FD is still set, since the
             * connect_cb() might close the FD, making us dereference
             * a free()'d pointer for the fd.
             */
            if (FD_ISSET(nn, &write_fds) && (fd->send.get != fd->send.put))
                ioset_try_write(fd);
        }

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
}

void
ioset_set_time(unsigned long new_now) {
    clock_skew = new_now - time(NULL);
    now = new_now;
}

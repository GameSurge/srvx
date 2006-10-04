/* ioset.h - srvx event loop
 * Copyright 2002-2003 srvx Development Team
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

#if !defined(IOSET_H)
#define IOSET_H

#include "common.h"

/* Forward declare, since functions take these as arguments. */
struct sockaddr;

struct ioq {
    char *buf;
    unsigned int size, get, put;
};

struct io_fd {
    int fd;
    void *data;
    enum { IO_CLOSED, IO_LISTENING, IO_CONNECTING, IO_CONNECTED } state;
    unsigned int wants_reads : 1;
    unsigned int line_reads : 1;
    int line_len;
    struct ioq send;
    struct ioq recv;
    void (*accept_cb)(struct io_fd *listener, struct io_fd *new_connect);
    void (*connect_cb)(struct io_fd *fd, int error);
    void (*readable_cb)(struct io_fd *fd);
    void (*destroy_cb)(struct io_fd *fd);
};
extern int do_write_dbs;
extern int do_reopen;

void ioset_init(void);
struct io_fd *ioset_add(int fd);
struct io_fd *ioset_listen(struct sockaddr *local, unsigned int sa_size, void *data, void (*accept_cb)(struct io_fd *listener, struct io_fd *new_connect));
struct io_fd *ioset_connect(struct sockaddr *local, unsigned int sa_size, const char *hostname, unsigned int port, int blocking, void *data, void (*connect_cb)(struct io_fd *fd, int error));
void ioset_update(struct io_fd *fd);
void ioset_run(void);
void ioset_write(struct io_fd *fd, const char *buf, unsigned int nbw);
int ioset_printf(struct io_fd *fd, const char *fmt, ...) PRINTF_LIKE(2, 3);
int ioset_line_read(struct io_fd *fd, char *buf, int maxlen);
void ioset_close(struct io_fd *fd, int os_close);
void ioset_cleanup(void);
void ioset_set_time(unsigned long new_now);

#endif /* !defined(IOSET_H) */

/* opserv.h - IRC Operator assistant service
 * Copyright 2000-2004 srvx Development Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.  Important limitations are
 * listed in the COPYING file that accompanies this software.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, email srvx-maintainers@srvx.net.
 */

#ifndef _opserv_h
#define _opserv_h

void init_opserv(const char *nick);
unsigned int gag_create(const char *mask, const char *owner, const char *reason, time_t expires);
int opserv_bad_channel(const char *name);

#endif

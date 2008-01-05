/* timeq.h - time-based event queue
 * Copyright 2000-2002 srvx Development Team
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

#ifndef TIMEQ_H
#define TIMEQ_H

typedef void (*timeq_func)(void *data);

#define TIMEQ_IGNORE_WHEN    0x01
#define TIMEQ_IGNORE_FUNC    0x02
#define TIMEQ_IGNORE_DATA    0x04

void timeq_add(unsigned long when, timeq_func func, void *data);
void timeq_del(unsigned long when, timeq_func func, void *data, int mask);
unsigned long timeq_next(void);
unsigned int timeq_size(void);
void timeq_run(void);

#endif /* ndef TIMEQ_H */

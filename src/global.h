/* global.h - Global notice service
 * Copyright 2000-2004 srvx Development Team
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

#ifndef _global_h
#define _global_h

#define MESSAGE_RECIPIENT_LUSERS		0x001
#define MESSAGE_RECIPIENT_HELPERS		0x002
#define MESSAGE_RECIPIENT_OPERS			0x004
#define MESSAGE_RECIPIENT_CHANNELS     		0x008
#define MESSAGE_RECIPIENT_ANNOUNCE     		0x040

#define MESSAGE_OPTION_SOURCELESS		0x010
#define MESSAGE_OPTION_IMMEDIATE		0x020

#define MESSAGE_RECIPIENT_STAFF			(MESSAGE_RECIPIENT_HELPERS | MESSAGE_RECIPIENT_OPERS)
#define MESSAGE_RECIPIENT_ALL			(MESSAGE_RECIPIENT_LUSERS | MESSAGE_RECIPIENT_CHANNELS)

void init_global(const char *nick);

void global_message(long targets, char *text);

#endif

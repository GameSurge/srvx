/* sendmail.h - mail sending utilities
 * Copyright 2002 srvx Development Team
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

#if !defined(SENDMAIL_H)
#define SENDMAIL_H

void sendmail_init(void);
void sendmail(struct userNode *from, struct handle_info *to, const char *subject, const char *body, int first_time);
const char *sendmail_prohibited_address(const char *addr);

#endif

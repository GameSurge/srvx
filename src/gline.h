/* gline.h - Gline database
 * Copyright 2001-2004 srvx Development Team
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

#ifndef GLINE_H
#define GLINE_H

#include "hash.h"

struct gline {
    time_t issued;
    time_t expires;
    char *issuer;
    char *target;
    char *reason;
};

struct gline_discrim {
    unsigned int limit;
    enum { SUBSET, EXACT, SUPERSET } target_mask_type;
    char *issuer_mask;
    char *target_mask;
    char *alt_target_mask;
    char *reason_mask;
    time_t max_issued;
    time_t min_expire;
};

void gline_init(void);
struct gline *gline_add(const char *issuer, const char *target, unsigned long duration, const char *reason, time_t issued, int announce);
struct gline *gline_find(const char *target);
int gline_remove(const char *target, int announce);
void gline_refresh_server(struct server *srv);
void gline_refresh_all(void);
unsigned int gline_count(void);

typedef void (*gline_search_func)(struct gline *gline, void *extra);
struct gline_discrim *gline_discrim_create(struct userNode *user, struct userNode *src, unsigned int argc, char *argv[]);
unsigned int gline_discrim_search(struct gline_discrim *discrim, gline_search_func gsf, void *data);

#endif /* !defined(GLINE_H) */

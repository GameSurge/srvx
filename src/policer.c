/* policer.c - Leaky bucket
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

#include "common.h"
#include "policer.h"

/* This policer uses the "leaky bucket" (GCRA) algorithm. */ 

struct policer_params {
    double bucket_size;
    double drain_rate;
};

struct policer_params *
policer_params_new(void)
{
    struct policer_params *params = malloc(sizeof(struct policer_params));
    params->bucket_size = 0.0;
    params->drain_rate = 0.0;
    return params;
}

int
policer_params_set(struct policer_params *params, const char *param, const char *value)
{
    if (!irccasecmp(param, "size")) {
	params->bucket_size = strtod(value, NULL);
    } else if (!irccasecmp(param, "drain-rate")) {
	params->drain_rate = strtod(value, NULL);
    } else {
	return 0;
    }
    return 1;
}

void
policer_params_delete(struct policer_params *params)
{
    free(params);
}

int
policer_conforms(struct policer *pol, time_t reqtime, double weight)
{
    int res;
    pol->level -= pol->params->drain_rate * (reqtime - pol->last_req);
    if (pol->level < 0.0) pol->level = 0.0;
    res = pol->level < pol->params->bucket_size;
    pol->level += weight;
    pol->last_req = reqtime;
    return res;
}

/* policer.h - Leaky bucket
 * Copyright 2000-2001 srvx Development Team
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

#ifndef POLICER_H
#define POLICER_H

struct policer_params;

struct policer_params *policer_params_new(void);
int policer_params_set(struct policer_params *params, const char *param, const char *value);
void policer_params_delete(struct policer_params *params);

struct policer {
    double level;
    time_t last_req;
    struct policer_params *params;
};

int policer_conforms(struct policer *pol, time_t reqtime, double weight);
void policer_delete(struct policer *pol);

#endif /* ndef POLICER_H */

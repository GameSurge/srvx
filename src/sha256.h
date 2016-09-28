/* sha256.h - SHA-256 hash implementation
 * Copyright 2016 srvx Development Team
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

#ifndef _sha256_h
#define _sha256_h

#include <stdint.h>
#include <sys/types.h>

struct sha256_context
{
	uint64_t length;
	uint32_t h[8];
	uint8_t block[64];
};

#define SHA256_OUTPUT_SIZE 32

void sha256_init(struct sha256_context *ctx);
void sha256_update(struct sha256_context *ctx, const char *data, size_t length);
const uint8_t *sha256_finish(struct sha256_context *ctx);

#endif

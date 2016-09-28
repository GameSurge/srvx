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

#include "sha256.h"
#include <string.h>

static const char padding[64] = { '\x80', 0 };

void sha256_init(struct sha256_context *ctx)
{
    ctx->length = 0;
    ctx->h[0] = 0x6a09e667;
    ctx->h[1] = 0xbb67ae85;
    ctx->h[2] = 0x3c6ef372;
    ctx->h[3] = 0xa54ff53a;
    ctx->h[4] = 0x510e527f;
    ctx->h[5] = 0x9b05688c;
    ctx->h[6] = 0x1f83d9ab;
    ctx->h[7] = 0x5be0cd19;
}

static void
sha256_update_hash(struct sha256_context *ctx)
{
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
        0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
        0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
        0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
        0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
        0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
        0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
        0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
    };
    uint32_t w[64], a, b, c, d, e, f, g, h, t1, t2;
    unsigned int ii;

    /* Initialize local variables. */
    a = ctx->h[0];
    b = ctx->h[1];
    c = ctx->h[2];
    d = ctx->h[3];
    e = ctx->h[4];
    f = ctx->h[5];
    g = ctx->h[6];
    h = ctx->h[7];

    /* Initialize first part of message block from context. */
    for (ii = 0; ii < 16; ++ii)
        w[ii] = ctx->block[ii * 4] << 24 | ctx->block[ii * 4 + 1] << 16
            | ctx->block[ii * 4 + 2] << 8 | ctx->block[ii * 4 + 3];

#define ROR(V,K) (((V) >> K) | ((V) << (32 - K)))
    /* Initalize the rest of the message box by remixing the start. */
    for (; ii < 64; ++ii)
        w[ii] = (ROR(w[ii-2], 17) ^ ROR(w[ii-2], 19) ^ (w[ii-2] >> 10))
            + w[ii-7]
            + (ROR(w[ii-15], 7) ^ ROR(w[ii-15], 18) ^ (w[ii-15] >> 3))
            + w[ii-16];

    /* Update hash values. */
    for (ii = 0; ii < 64; ++ii) {
        t1 = h + ((e & f) ^ (~e & g)) + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + k[ii] + w[ii];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
    }
#undef ROR

    /* Update the context's hash value. */
    ctx->h[0] += a;
    ctx->h[1] += b;
    ctx->h[2] += c;
    ctx->h[3] += d;
    ctx->h[4] += e;
    ctx->h[5] += f;
    ctx->h[6] += g;
    ctx->h[7] += h;
}

void sha256_update(struct sha256_context *ctx, const char *data, size_t length)
{
    while (length > 0) {
	size_t used, partlen;

	/* How much space is available in the current block? */
	used = ctx->length % sizeof(ctx->block);
	partlen = sizeof(ctx->block) - used;
	if (partlen > length)
	    partlen = length;
	
	/* Copy the next bytes. */
	memcpy(ctx->block + used, data, partlen);
	ctx->length += partlen;
	length -= partlen;
	data += partlen;

	/* Is the current block full? */
	if (ctx->length % sizeof(ctx->block) == 0)
		sha256_update_hash(ctx);
    }
}

const uint8_t *sha256_finish(struct sha256_context *ctx)
{
    uint64_t total;
    size_t partlen;
    unsigned int ii;
    char length[8];

    partlen = ctx->length % sizeof(ctx->block);
    total = ctx->length * 8;
    sha256_update(ctx, padding, ((partlen < 55) ? 56 : 120) - partlen);
    for (ii = 0; ii < 8; ++ii, total >>= 8)
        length[7 - ii] = total & 255;
    sha256_update(ctx, length, 8);
    for (ii = 0; ii < SHA256_OUTPUT_SIZE/4; ++ii) {
        ctx->block[4 * ii + 0] = (ctx->h[ii] >>  0) & 255;
        ctx->block[4 * ii + 1] = (ctx->h[ii] >>  8) & 255;
        ctx->block[4 * ii + 2] = (ctx->h[ii] >> 16) & 255;
        ctx->block[4 * ii + 3] = (ctx->h[ii] >> 24) & 255;
    }
    memset(ctx->block + SHA256_OUTPUT_SIZE, 0,
    	sizeof(ctx->block) - SHA256_OUTPUT_SIZE);
    return ctx->block;
}

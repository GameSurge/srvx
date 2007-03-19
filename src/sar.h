/* sar.h - srvx asynchronous resolver
 * Copyright 2005, 2007 Michael Poole <mdpoole@troilus.org>
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

#if !defined(SRVX_SAR_H)
#define SRVX_SAR_H

#include "common.h"

#define SAI_NUMERICHOST 0x01 /* simply translate address from text form */
#define SAI_CANONNAME   0x02 /* fill in canonical name of host */
#define SAI_PASSIVE     0x04 /* if node==NULL, use unspecified address */
#define SAI_V4MAPPED    0x08 /* accept v4-mapped IPv6 addresses */
#define SAI_ALL         0x10 /* return both IPv4 and IPv6 addresses */
#define SAI_NOSRV       0x20 /* suppress SRV even if default is to use it */
#define SAI_FORCESRV    0x40 /* force SRV request even if questionable */

#define SNI_NOFQDN      0x01 /* omit domain name for local hosts */
#define SNI_NUMERICHOST 0x02 /* do not resolve address, just translate to text */
#define SNI_NAMEREQD    0x04 /* indicate error if no name exists */
#define SNI_NUMERICSERV 0x08 /* return service in numeric form */
#define SNI_DGRAM       0x10 /* return service names for UDP use */
#define SNI_PARANOID    0x20 /* confirm forward resolution of name */

enum sar_errcode {
    SAI_SUCCESS,
    SAI_FAMILY,
    SAI_SOCKTYPE,
    SAI_BADFLAGS,
    SAI_NONAME,
    SAI_SERVICE,
    SAI_ADDRFAMILY,
    SAI_NODATA,
    SAI_MEMORY,
    SAI_FAIL,
    SAI_AGAIN,
    SAI_MISMATCH,
    SAI_SYSTEM
};

struct sockaddr;
struct addrinfo;
struct sar_request;

void sar_init(void);
const char *sar_strerror(enum sar_errcode errcode);

int sar_get_port(const struct sockaddr *sa, unsigned int socklen);
int sar_set_port(struct sockaddr *sa, unsigned int socklen, unsigned short port);
unsigned int sar_pton(struct sockaddr *sa, unsigned int socklen, unsigned int *bits, const char *input);
typedef void (*sar_addr_cb)(void *ctx, struct addrinfo *res, enum sar_errcode errcode);
struct sar_request *sar_getaddr(const char *node, const char *service, const struct addrinfo *hints, sar_addr_cb cb, void *cb_ctx);
void sar_free(struct addrinfo *ai);

/** Maximum value returnable by sar_ntop(). */
#define SAR_NTOP_MAX 40
unsigned int sar_ntop(char *output, unsigned int out_size, const struct sockaddr *sa, unsigned int socklen);
typedef void (*sar_name_cb)(void *ctx, const char *host, const char *serv, enum sar_errcode errcode);
struct sar_request *sar_getname(const struct sockaddr *sa, unsigned int salen, int flags, sar_name_cb cb, void *cb_ctx);

/** Generic DNS lookup support. */

/** DNS message (request and response) header. */
struct dns_header {
    uint16_t id;
    uint16_t flags;
#define REQ_FLAG_QR           0x8000 /* response */
#define REQ_FLAG_OPCODE_MASK  0x7800 /* opcode mask */
#define REQ_FLAG_OPCODE_SHIFT 11     /* opcode shift count */
#define REQ_OPCODE_QUERY      (0 << REQ_FLAG_OPCODE_SHIFT)
#define REQ_FLAG_AA           0x0400 /* authoritative answer */
#define REQ_FLAG_TC           0x0200 /* truncated message */
#define REQ_FLAG_RD           0x0100 /* recursion desired */
#define REQ_FLAG_RA           0x0080 /* recursion available */
/* 0x0040 bit currently reserved and must be zero; 0x0020 and 0x0010
 * used by DNSSEC. */
#define REQ_FLAG_RCODE_MASK   0x000f /* response code mask */
#define REQ_FLAG_RCODE_SHIFT  0      /* response code shift count */
#define RCODE_NO_ERROR        0
#define RCODE_FORMAT_ERROR    1
#define RCODE_SERVER_FAILURE  2
#define RCODE_NAME_ERROR      3  /* aka NXDOMAIN (since RFC2308) */
#define RCODE_NOT_IMPLEMENTED 4
#define RCODE_REFUSED         5
#define RCODE_BAD_OPT_VERSION 16 /* RFC 2671 */
    uint16_t qdcount;  /* count of questions */
    uint16_t ancount;  /* count of answer RRs */
    uint16_t nscount;  /* count of NS (authority) RRs */
    uint16_t arcount;  /* count of additional RRs */
};

/** DNS resource record. */
struct dns_rr {
    uint16_t type;
    uint16_t class;
    uint32_t ttl;
    uint16_t rdlength;
    uint16_t rd_start;
    char *name;
};

#define REQ_TYPE_A     1
#define REQ_TYPE_NS    2
#define REQ_TYPE_CNAME 5
#define REQ_TYPE_SOA   6
#define REQ_TYPE_PTR   12
#define REQ_TYPE_MX    15
#define REQ_TYPE_TXT   16
#define REQ_TYPE_AAAA  28  /* RFC 3596 */
#define REQ_TYPE_SRV   33  /* RFC 2782 */
#define REQ_TYPE_OPT   41  /* RFC 2671 */
#define REQ_QTYPE_ALL  255
#define REQ_CLASS_IN   1
#define REQ_QCLASS_ALL 255

struct sar_request;
typedef void (*sar_request_ok_cb)(struct sar_request *req, struct dns_header *hdr, struct dns_rr *rr, unsigned char *raw, unsigned int raw_size);
typedef void (*sar_request_fail_cb)(struct sar_request *req, unsigned int rcode);

/** Pending request structure.
 * User code should treat this structure as opaque.
 */
struct sar_request {
    int id;
    time_t expiry;
    sar_request_ok_cb cb_ok;
    sar_request_fail_cb cb_fail;
    unsigned char *body;
    unsigned int body_len;
    unsigned char retries;
    char id_text[6];
};

const char *sar_rcode_text(unsigned int rcode);
struct sar_request *sar_request_alloc(unsigned int data_len, sar_request_ok_cb ok_cb, sar_request_fail_cb fail_cb);
unsigned int sar_request_build(struct sar_request *req, ...);
void sar_request_send(struct sar_request *req);
struct sar_request *sar_request_simple(unsigned int data_len, sar_request_ok_cb ok_cb, sar_request_fail_cb fail_cb, ...);
void sar_request_abort(struct sar_request *req);
char *sar_extract_name(const unsigned char *buf, unsigned int size, unsigned int *ppos);

#endif /* !defined(SRVX_SAR_H) */

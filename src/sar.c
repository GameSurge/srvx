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

#include "sar.h"
#include "conf.h"
#include "ioset.h"
#include "log.h"
#include "timeq.h"

#if defined(HAVE_NETINET_IN_H)
# include <netinet/in.h> /* sockaddr_in6 on some BSDs */
#endif

static const char hexdigits[] = "0123456789abcdef";

struct dns_rr;
struct sar_getaddr_state;
struct sar_getname_state;

struct sar_family_helper {
    const char *localhost_addr;
    const char *unspec_addr;
    unsigned int socklen;
    unsigned int family;

    unsigned int (*ntop)(char *output, unsigned int out_size, const struct sockaddr *sa, unsigned int socklen);
    unsigned int (*pton)(struct sockaddr *sa, unsigned int socklen, unsigned int *bits, const char *input);
    int (*get_port)(const struct sockaddr *sa, unsigned int socklen);
    int (*set_port)(struct sockaddr *sa, unsigned int socklen, unsigned short port);
    unsigned int (*build_addr_request)(struct sar_request *req, const char *node, const char *srv_node, unsigned int flags);
    void (*build_ptr_name)(struct sar_getname_state *state, const struct sockaddr *sa, unsigned int socklen);
    int (*decode_addr)(struct sar_getaddr_state *state, struct dns_rr *rr, unsigned char *raw, unsigned int raw_size);

    struct sar_family_helper *next;
};

#define MAX_FAMILY AF_INET
static struct sar_family_helper sar_ipv4_helper;

#if defined(AF_INET6)
# if AF_INET6 > MAX_FAMILY
#  undef MAX_FAMILY
#  define MAX_FAMILY AF_INET6
# endif
static struct sar_family_helper sar_ipv6_helper;
#endif

static struct sar_family_helper *sar_helpers[MAX_FAMILY+1];
static struct sar_family_helper *sar_first_helper;

unsigned int
sar_ntop(char *output, unsigned int out_size, const struct sockaddr *sa, unsigned int socklen)
{
    unsigned int pos;
    assert(output != NULL);
    assert(sa != NULL);
    assert(out_size > 0);

    if (sa->sa_family <= MAX_FAMILY && sar_helpers[sa->sa_family]) {
        pos = sar_helpers[sa->sa_family]->ntop(output, out_size, sa, socklen);
        if (pos)
            return pos;
    }
    *output = '\0';
    return 0;
}

unsigned int
sar_pton(struct sockaddr *sa, unsigned int socklen, unsigned int *bits, const char *input)
{
    struct sar_family_helper *helper;
    unsigned int len;

    assert(sa != NULL);
    assert(input != NULL);

    memset(sa, 0, socklen);
    if (bits)
        *bits = ~0;
    for (helper = sar_first_helper; helper; helper = helper->next) {
        if (socklen < helper->socklen)
            continue;
        len = helper->pton(sa, socklen, bits, input);
        if (len) {
            sa->sa_family = helper->family;
            return len;
        }
    }
    return 0; /* parse failed */
}

int
sar_get_port(const struct sockaddr *sa, unsigned int socklen)
{
    if (sa->sa_family <= MAX_FAMILY
        && sar_helpers[sa->sa_family]
        && socklen >= sar_helpers[sa->sa_family]->socklen)
        return sar_helpers[sa->sa_family]->get_port(sa, socklen);
    else return -1;
}

int
sar_set_port(struct sockaddr *sa, unsigned int socklen, unsigned short port)
{
    if (sa->sa_family <= MAX_FAMILY
        && sar_helpers[sa->sa_family]
        && socklen >= sar_helpers[sa->sa_family]->socklen)
        return sar_helpers[sa->sa_family]->set_port(sa, socklen, port);
    else return 1;
}

const char *
sar_strerror(enum sar_errcode errcode)
{
    switch (errcode) {
    case SAI_SUCCESS: return "Resolution succeeded.";
    case SAI_FAMILY: return "The requested address family is not supported.";
    case SAI_SOCKTYPE: return "The requested socket type is not supported.";
    case SAI_BADFLAGS: return "Invalid flags value.";
    case SAI_NONAME: return "Unknown name or service.";
    case SAI_SERVICE: return "The service is unavailable for that socket type.";
    case SAI_ADDRFAMILY: return "The host has no address in the requested family.";
    case SAI_NODATA: return "The host has no addresses at all.";
    case SAI_MEMORY: return "Unable to allocate memory.";
    case SAI_FAIL: return "The nameserver indicated a permanent error.";
    case SAI_AGAIN: return "The nameserver indicated a temporary error.";
    case SAI_MISMATCH: return "Mismatch between reverse and forward resolution.";
    case SAI_SYSTEM: return strerror(errno);
    default: return "Unknown resolver error code.";
    }
}

void
sar_free(struct addrinfo *ai)
{
    struct addrinfo *next;
    for (; ai; ai = next) {
        next = ai->ai_next;
        free(ai);
    }
}

/** Global variables to support DNS name resolution. */
static struct {
    unsigned int sar_timeout;
    unsigned int sar_retries;
    unsigned int sar_ndots;
    unsigned int sar_edns0;
    char sar_localdomain[MAXLEN];
    struct string_list *sar_search;
    struct string_list *sar_nslist;
    struct sockaddr_storage sar_bind_address;
} conf;
static struct log_type *sar_log;

/* Except as otherwise noted, constants and formats are from RFC1035.
 * This resolver is believed to implement the behaviors mandated (and
 * in many cases those recommended) by these standards: RFC1035,
 * RFC2671, RFC2782, RFC3596, RFC3597.
 *
 * Update queries (including RFC 2136) seems a likely candidate for
 * future support.
 * DNSSEC (including RFCs 2535, 3007, 3655, etc) is less likely until
 * a good application is found.
 * Caching (RFC 2308) and redirection (RFC 2672) are much less likely,
 * since most users will have a separate local, caching, recursive
 * nameserver.
 * Other DNS extensions (at least through RFC 3755) are believed to be
 * too rare or insufficiently useful to bother supporting.
 *
 * The following are useful Reasons For Concern:
 * RFC1536, RFC1912, RFC2606, RFC3363, RFC3425, RFC3467
 * http://www.iana.org/assignments/dns-parameters
 * http://www.ietf.org/html.charters/dnsext-charter.html
 */

struct sar_nameserver {
    char *name;
    unsigned int valid;
    unsigned int req_sent;
    unsigned int resp_used;
    unsigned int resp_ignored;
    unsigned int resp_servfail;
    unsigned int resp_fallback;
    unsigned int resp_failures;
    unsigned int resp_scrambled;
    unsigned int ss_len;
    struct sockaddr_storage ss;
};

/* EDNS0 uses 12 bit RCODEs, TSIG/TKEY use 16 bit RCODEs.
 * Declare local RCODE failures here.*/
enum {
    RCODE_TIMED_OUT = 65536,
    RCODE_QUERY_TOO_LONG,
    RCODE_LABEL_TOO_LONG,
    RCODE_SOCKET_FAILURE,
    RCODE_DESTROYED,
};

#define DNS_NAME_LENGTH 256

#define RES_SIZE_FLAGS 0xc0
#define RES_SF_LABEL   0x00
#define RES_SF_POINTER 0xc0

static dict_t sar_requests;
static dict_t sar_nameservers;
static struct io_fd *sar_fd;
static int sar_fd_fd;

const char *
sar_rcode_text(unsigned int rcode)
{
    switch (rcode) {
    case RCODE_NO_ERROR: return "No error";
    case RCODE_FORMAT_ERROR: return "Format error";
    case RCODE_SERVER_FAILURE: return "Server failure";
    case RCODE_NAME_ERROR: return "Name error";
    case RCODE_NOT_IMPLEMENTED: return "Feature not implemented";
    case RCODE_REFUSED: return "Query refused";
    case RCODE_BAD_OPT_VERSION: return "Unsupported EDNS option version";
    case RCODE_TIMED_OUT: return "Request timed out";
    case RCODE_QUERY_TOO_LONG: return "Query too long";
    case RCODE_LABEL_TOO_LONG: return "Label too long";
    case RCODE_SOCKET_FAILURE: return "Resolver socket failure";
    case RCODE_DESTROYED: return "Request unexpectedly destroyed";
    default: return "Unknown rcode";
    }
}

static void
sar_request_fail(struct sar_request *req, unsigned int rcode)
{
    log_module(sar_log, LOG_DEBUG, "sar_request_fail({id=%d}, rcode=%d)", req->id, rcode);
    req->expiry = 0;
    if (req->cb_fail) {
        req->cb_fail(req, rcode);
        if (req->expiry)
            return;
    }
    sar_request_abort(req);
}

static unsigned long next_sar_timeout;

static void
sar_timeout_cb(void *data)
{
    dict_iterator_t it;
    dict_iterator_t next;
    unsigned long next_timeout = INT_MAX;

    for (it = dict_first(sar_requests); it; it = next) {
        struct sar_request *req;

        req = iter_data(it);
        next = iter_next(it);
        if (req->expiry > next_timeout)
            continue;
        else if (req->expiry > now)
            next_timeout = req->expiry;
        else if (req->retries >= conf.sar_retries)
            sar_request_fail(req, RCODE_TIMED_OUT);
        else
            sar_request_send(req);
    }
    if (next_timeout < INT_MAX) {
        next_sar_timeout = next_timeout;
        timeq_add(next_timeout, sar_timeout_cb, data);
    }
}

static void
sar_check_timeout(unsigned long when)
{
    if (!next_sar_timeout || when < next_sar_timeout) {
        timeq_del(0, sar_timeout_cb, NULL, TIMEQ_IGNORE_WHEN | TIMEQ_IGNORE_DATA);
        timeq_add(when, sar_timeout_cb, NULL);
        next_sar_timeout = when;
    }
}

static void
sar_request_cleanup(void *d)
{
    struct sar_request *req = d;
    log_module(sar_log, LOG_DEBUG, "sar_request_cleanup({id=%d})", req->id);
    free(req->body);
    if (req->cb_fail)
        req->cb_fail(req, RCODE_DESTROYED);
    free(req);
}

static void
sar_dns_init(const char *resolv_conf_path)
{
    struct string_list *ns_sv;
    struct string_list *ds_sv;
    FILE *resolv_conf;
    dict_t node;
    const char *str;

    /* Initialize configuration defaults. */
    conf.sar_localdomain[0] = '\0';
    conf.sar_timeout = 3;
    conf.sar_retries = 3;
    conf.sar_ndots = 1;
    conf.sar_edns0 = 0;
    ns_sv = alloc_string_list(4);
    ds_sv = alloc_string_list(4);

    /* Scan resolver configuration file.  */
    resolv_conf = fopen(resolv_conf_path, "r");
    if (resolv_conf) {
        char *arg, *opt;
        unsigned int len;
        char linebuf[LINE_MAX], ch;

        while (fgets(linebuf, sizeof(linebuf), resolv_conf)) {
            ch = linebuf[len = strcspn(linebuf, " \t\r\n")];
            linebuf[len] = '\0';
            arg = linebuf + len + 1;
            if (!strcmp(linebuf, "nameserver")) {
                while (ch == ' ') {
                    ch = arg[len = strcspn(arg, " \t\r\n")];
                    arg[len] = '\0';
                    string_list_append(ns_sv, strdup(arg));
                    arg += len + 1;
                }
            } else if (!strcmp(linebuf, "domain")) {
                if (ch == ' ') {
                    safestrncpy(conf.sar_localdomain, arg, sizeof(conf.sar_localdomain));
                }
            } else if (!strcmp(linebuf, "search")) {
                while (ch == ' ') {
                    ch = arg[len = strcspn(arg, " \t\r\n")];
                    arg[len] = '\0';
                    string_list_append(ds_sv, strdup(arg));
                    arg += len + 1;
                }
            } else if (!strcmp(linebuf, "options")) {
                while (ch == ' ') {
                    ch = arg[len = strcspn(arg, " \t\r\n")];
                    arg[len] = '\0';
                    opt = strchr(arg, ':');
                    if (opt) {
                        *opt++ = '\0';
                        if (!strcmp(arg, "timeout")) {
                            conf.sar_timeout = atoi(opt);
                        } else if (!strcmp(arg, "attempts")) {
                            conf.sar_retries = atoi(opt);
                        } else if (!strcmp(arg, "ndots")) {
                            conf.sar_ndots = atoi(opt);
                        } else if (!strcmp(arg, "edns0")) {
                            conf.sar_edns0 = atoi(opt);
                        }
                    } else if (!strcmp(arg, "edns0")) {
                        conf.sar_edns0 = 1440;
                    }
                    arg += len + 1;
                }
            }
        }
        fclose(resolv_conf);
    } else {
        /* This is apparently what BIND defaults to using. */
        string_list_append(ns_sv, "127.0.0.1");
    }

    /* Set default search path if domain is set. */
    if (conf.sar_localdomain[0] != '\0' && ds_sv->used == 0)
        string_list_append(ds_sv, strdup(conf.sar_localdomain));

    /* Check configuration entries that might override resolv.conf. */
    node = conf_get_data("modules/sar", RECDB_OBJECT);
    if (node) {
        struct sockaddr *sa;
        struct string_list *slist;

        str = database_get_data(node, "timeout", RECDB_QSTRING);
        if (str) conf.sar_timeout = ParseInterval(str);
        str = database_get_data(node, "retries", RECDB_QSTRING);
        if (str) conf.sar_retries = atoi(str);
        str = database_get_data(node, "ndots", RECDB_QSTRING);
        if (str) conf.sar_ndots = atoi(str);
        str = database_get_data(node, "edns0", RECDB_QSTRING);
        if (str) conf.sar_edns0 = enabled_string(str);
        str = database_get_data(node, "domain", RECDB_QSTRING);
        if (str) safestrncpy(conf.sar_localdomain, str, sizeof(conf.sar_localdomain));
        slist = database_get_data(node, "search", RECDB_STRING_LIST);
        if (slist) {
            free_string_list(ds_sv);
            ds_sv = string_list_copy(slist);
        }
        slist = database_get_data(node, "nameservers", RECDB_STRING_LIST);
        if (slist) {
            free_string_list(ns_sv);
            ns_sv = string_list_copy(slist);
        }
        sa = (struct sockaddr*)&conf.sar_bind_address;
        memset(sa, 0, sizeof(conf.sar_bind_address));
        str = database_get_data(node, "bind_address", RECDB_QSTRING);
        if (str) sar_pton(sa, sizeof(conf.sar_bind_address), NULL, str);
        str = database_get_data(node, "bind_port", RECDB_QSTRING);
        if (str != NULL) {
            if (sa->sa_family == AF_INET) {
                struct sockaddr_in *sin = (struct sockaddr_in*)sa;
                sin->sin_port = ntohs(atoi(str));
            }
#if defined(AF_INET6)
            else if (sa->sa_family == AF_INET6) {
                struct sockaddr_in6 *sin6 = (struct sockaddr_in6*)sa;
                sin6->sin6_port = ntohs(atoi(str));
            }
#endif
        }
    }

    /* Replace config lists with their new values. */
    free_string_list(conf.sar_search);
    conf.sar_search = ds_sv;
    free_string_list(conf.sar_nslist);
    conf.sar_nslist = ns_sv;
}

void
sar_request_abort(struct sar_request *req)
{
    if (!req)
        return;
    assert(dict_find(sar_requests, req->id_text, NULL) == req);
    log_module(sar_log, LOG_DEBUG, "sar_request_abort({id=%d})", req->id);
    req->cb_ok = NULL;
    req->cb_fail = NULL;
    dict_remove(sar_requests, req->id_text);
}

static struct sar_nameserver *
sar_our_server(const struct sockaddr_storage *ss, unsigned int ss_len)
{
    dict_iterator_t it;

    for (it = dict_first(sar_nameservers); it; it = iter_next(it)) {
        struct sar_nameserver *ns;

        ns = iter_data(it);
        if (ns->ss_len == ss_len && !memcmp(ss, &ns->ss, ss_len))
            return ns;
    }
    return NULL;
}

char *
sar_extract_name(const unsigned char *buf, unsigned int size, unsigned int *ppos)
{
    struct string_buffer cv;
    unsigned int pos, jumped;

    pos = *ppos;
    jumped = 0;
    cv.used = 0;
    cv.size = 64;
    cv.list = calloc(1, cv.size);
    while (1) {
        if (pos >= size)
            goto fail;
        if (!buf[pos]) {
            if (!jumped)
                *ppos = pos + 1;
            if (cv.used)
                cv.list[cv.used - 1] = '\0'; /* chop off terminating '.' */
            else
                string_buffer_append(&cv, '\0');
            return cv.list;
        }
        switch (buf[pos] & RES_SIZE_FLAGS) {
        case RES_SF_LABEL: {
            unsigned int len = buf[pos];
            if (pos + len + 1 >= size)
                goto fail;
            string_buffer_append_substring(&cv, (char*)buf + pos + 1, len);
            string_buffer_append(&cv, '.');
            pos += buf[pos] + 1;
            break;
        }
        case RES_SF_POINTER:
            if ((pos + 1 >= size) || (cv.used >= size))
                goto fail;
            if (!jumped)
                *ppos = pos + 2;
            pos = (buf[pos] & ~RES_SIZE_FLAGS) << 8 | buf[pos+1];
            jumped = 1;
            break;
        default:
            goto fail;
        }
    }
 fail:
    free(cv.list);
    return NULL;
}

static int
sar_decode_answer(struct sar_request *req, struct dns_header *hdr, unsigned char *buf, unsigned int size)
{
    struct dns_rr *rr;
    unsigned int ii, rr_count, pos;
    int res;

    /* Skip over query section. */
    for (ii = 0, pos = 12; ii < hdr->qdcount; ++ii) {
        /* Skip over compressed names. */
        while (1) {
            if (pos >= size)
                return 2;
            if (!buf[pos])
                break;
            switch (buf[pos] & RES_SIZE_FLAGS) {
            case RES_SF_LABEL:
                pos += buf[pos] + 1;
                break;
            case RES_SF_POINTER:
                if (pos + 1 >= size)
                    return 2;
                pos = (buf[pos] & ~RES_SIZE_FLAGS) << 8 | buf[pos+1];
                if (pos >= size)
                    return 3;
                break;
            default:
                return 4;
            }
        }
        /* Skip over null terminator, type and class part of question. */
        pos += 5;
    }

    /* Parse each RR in the answer. */
    rr_count = hdr->ancount + hdr->nscount + hdr->arcount;
    rr = calloc(1, rr_count * sizeof(rr[0]));
    for (ii = 0; ii < rr_count; ++ii) {
        rr[ii].name = sar_extract_name(buf, size, &pos);
        if (!rr[ii].name) {
            res = 5;
            goto out;
        }
        if (pos + 10 > size) {
            res = 6;
            goto out;
        }
        rr[ii].type = buf[pos+0] << 8 | buf[pos+1];
        rr[ii].class = buf[pos+2] << 8 | buf[pos+3];
        rr[ii].ttl = buf[pos+4] << 24 | buf[pos+5] << 16 | buf[pos+6] << 8 | buf[pos+7];
        rr[ii].rdlength = buf[pos+8] << 8 | buf[pos+9];
        rr[ii].rd_start = pos + 10;
        pos = pos + rr[ii].rdlength + 10;
        if (pos > size) {
            res = 7;
            goto out;
        }
    }
    res = 0;
    req->expiry = 0;
    req->cb_ok(req, hdr, rr, buf, size);
    if (!req->expiry) {
        req->cb_ok = NULL;
        req->cb_fail = NULL;
        dict_remove(sar_requests, req->id_text);
    }

out:
    while (ii > 0)
        free(rr[--ii].name);
    free(rr);
    return res;
}

static const unsigned char *
sar_extract_rdata(struct dns_rr *rr, unsigned int len, unsigned char *raw, unsigned int raw_size)
{
    if (len > rr->rdlength)
        return NULL;
    if (rr->rd_start + len > raw_size)
        return NULL;
    return raw + rr->rd_start;
}

static void
sar_fd_readable(struct io_fd *fd)
{
    struct sockaddr_storage ss;
    struct dns_header hdr;
    struct sar_nameserver *ns;
    struct sar_request *req;
    unsigned char *buf;
    socklen_t ss_len;
    int res, rcode, buf_len;
    char id_text[6];

    assert(sar_fd == fd);
    buf_len = conf.sar_edns0;
    if (!buf_len)
        buf_len = 512;
    buf = alloca(buf_len);
    ss_len = sizeof(ss);
    res = recvfrom(sar_fd_fd, buf, buf_len, 0, (struct sockaddr*)&ss, &ss_len);
    if (res < 12 || !(ns = sar_our_server(&ss, ss_len)))
        return;
    hdr.id = buf[0] << 8 | buf[1];
    hdr.flags = buf[2] << 8 | buf[3];
    hdr.qdcount = buf[4] << 8 | buf[5];
    hdr.ancount = buf[6] << 8 | buf[7];
    hdr.nscount = buf[8] << 8 | buf[9];
    hdr.arcount = buf[10] << 8 | buf[11];

    sprintf(id_text, "%d", hdr.id);
    req = dict_find(sar_requests, id_text, NULL);
    log_module(sar_log, LOG_DEBUG, "sar_fd_readable(%p): hdr {id=%d, flags=0x%x, qdcount=%d, ancount=%d, nscount=%d, arcount=%d} -> req %p", (void*)fd, hdr.id, hdr.flags, hdr.qdcount, hdr.ancount, hdr.nscount, hdr.arcount, (void*)req);
    if (!req || !req->retries || !(hdr.flags & REQ_FLAG_QR)) {
        ns->resp_ignored++;
        return;
    }
    rcode = hdr.flags & REQ_FLAG_RCODE_MASK;
    if (rcode != RCODE_NO_ERROR) {
        sar_request_fail(req, rcode);
    } else if (sar_decode_answer(req, &hdr, (unsigned char*)buf, res)) {
        ns->resp_scrambled++;
        sar_request_fail(req, RCODE_FORMAT_ERROR);
    }
}

static void
sar_build_nslist(struct string_list *nslist)
{
    dict_iterator_t it, next;
    struct sar_nameserver *ns;
    unsigned int ii;

    for (it = dict_first(sar_nameservers); it; it = iter_next(it)) {
        ns = iter_data(it);
        ns->valid = 0;
    }

    for (ii = 0; ii < nslist->used; ++ii) {
        const char *name;

        name = nslist->list[ii];
        ns = dict_find(sar_nameservers, name, NULL);
        if (!ns) {
            ns = calloc(1, sizeof(*ns) + strlen(name) + 1);
            ns->name = (char*)(ns + 1);
            strcpy(ns->name, name);
            ns->ss_len = sizeof(ns->ss);
            if (!sar_pton((struct sockaddr*)&ns->ss, sizeof(ns->ss), NULL, name)) {
                free(it);
                continue;
            }
            sar_set_port((struct sockaddr*)&ns->ss, sizeof(ns->ss), 53);
            ns->ss_len = sar_helpers[ns->ss.ss_family]->socklen;
            dict_insert(sar_nameservers, ns->name, ns);
        }
        ns->valid = 1;
    }

    for (it = dict_first(sar_nameservers); it; it = next) {
        next = iter_next(it);
        ns = iter_data(it);
        if (!ns->valid)
            dict_remove(sar_nameservers, ns->name);
    }
}

static int
sar_open_fd(void)
{
    int res;

    /* Build list of nameservers. */
    sar_build_nslist(conf.sar_nslist);

    if (conf.sar_bind_address.ss_family != 0) {
        struct addrinfo *ai;

        ai = (struct addrinfo*)&conf.sar_bind_address;
        sar_fd_fd = socket(ai->ai_family, SOCK_DGRAM, 0);
        if (sar_fd_fd < 0) {
            log_module(sar_log, LOG_FATAL, "Unable to create resolver socket: %s", strerror(errno));
            return 1;
        }

        res = bind(sar_fd_fd, ai->ai_addr, ai->ai_addrlen);
        if (res < 0)
            log_module(sar_log, LOG_ERROR, "Unable to bind resolver socket to address [%s]:%s: %s", (char*)conf_get_data("modules/sar/bind_address", RECDB_QSTRING), (char*)conf_get_data("modules/sar/bind_port", RECDB_QSTRING), strerror(errno));
    } else {
        dict_iterator_t it;
        struct sar_nameserver *ns;

        it = dict_first(sar_nameservers);
        ns = iter_data(it);
        sar_fd_fd = socket(ns->ss.ss_family, SOCK_DGRAM, 0);
        if (sar_fd_fd < 0) {
            log_module(sar_log, LOG_FATAL, "Unable to create resolver socket: %s", strerror(errno));
            return 1;
        }
    }

    sar_fd = ioset_add(sar_fd_fd);
    if (!sar_fd) {
        log_module(sar_log, LOG_FATAL, "Unable to register resolver socket with event loop.");
        return 1;
    }
    sar_fd->state = IO_CONNECTED;
    sar_fd->readable_cb = sar_fd_readable;
    return 0;
}

struct name_ofs {
    const char *name;
    unsigned int ofs;
};

static int
set_compare_charp(const void *a_, const void *b_)
{
    char * const *a = a_, * const *b = b_;
    return strcasecmp(*a, *b);
}

static void
string_buffer_reserve(struct string_buffer *cv, unsigned int min_length)
{
    if (cv->size < min_length) {
        char *new_buffer;
        new_buffer = realloc(cv->list, min_length);
        if (new_buffer) {
            cv->size = min_length;
            cv->list = new_buffer;
        }
    }
}

/** Append \a name to \a cv in compressed form. */
static int
sar_append_name(struct string_buffer *cv, const char *name, struct name_ofs *ofs, unsigned int *used, unsigned int alloc)
{
    struct name_ofs *pofs;
    unsigned int len;

    while (1) {
        pofs = bsearch(&name, ofs, *used, sizeof(ofs[0]), set_compare_charp);
        if (pofs) {
            string_buffer_reserve(cv, cv->used + 2);
            cv->list[cv->used++] = RES_SF_POINTER | (pofs->ofs >> 8);
            cv->list[cv->used++] = pofs->ofs & 255;
            return 0;
        }
        len = strcspn(name, ".");
        if (len > 63)
            return 1;
        if (*used < alloc) {
            ofs[*used].name = name;
            ofs[*used].ofs = cv->used;
            qsort(ofs, (*used)++, sizeof(ofs[0]), set_compare_charp);
        }
        string_buffer_reserve(cv, cv->used + len + 1);
        cv->list[cv->used] = RES_SF_LABEL | len;
        memcpy(cv->list + cv->used + 1, name, len);
        cv->used += len + 1;
        if (name[len] == '.')
            name += len + 1;
        else if (name[len] == '\0')
            break;
    }
    string_buffer_append(cv, '\0');
    return 0;
}

/** Build a DNS question packet from a variable-length argument list.
 * In \a args, there is at least one pari consisting of const char
 * *name and unsigned int qtype.  A null name argument terminates the
 * list.
 */
unsigned int
sar_request_vbuild(struct sar_request *req, va_list args)
{
    struct name_ofs suffixes[32];
    struct string_buffer cv;
    const char *name;
    unsigned int suf_used;
    unsigned int val;
    unsigned int qdcount;

    cv.used = 0;
    cv.size = 512;
    cv.list = calloc(1, cv.size);
    suf_used = 0;
    val = REQ_OPCODE_QUERY | REQ_FLAG_RD;
    cv.list[0] = req->id >> 8;
    cv.list[1] = req->id & 255;
    cv.list[2] = val >> 8;
    cv.list[3] = val & 255;
    cv.list[6] = cv.list[7] = cv.list[8] = cv.list[9] = cv.list[10] = 0;
    cv.used = 12;
    for (qdcount = 0; (name = va_arg(args, const char*)); ++qdcount) {
        if (sar_append_name(&cv, name, suffixes, &suf_used, ArrayLength(suffixes))) {
            string_buffer_clean(&cv);
            goto out;
        }
        string_buffer_reserve(&cv, cv.used + 4);
        val = va_arg(args, unsigned int);
        cv.list[cv.used++] = val >> 8;
        cv.list[cv.used++] = val & 255;
        cv.list[cv.used++] = REQ_CLASS_IN >> 8;
        cv.list[cv.used++] = REQ_CLASS_IN & 255;
    }
    cv.list[4] = qdcount >> 8;
    cv.list[5] = qdcount & 255;
    val = conf.sar_edns0;
    if (val) {
        string_buffer_reserve(&cv, cv.used + 11);
        cv.list[cv.used +  0] = '\0'; /* empty name */
        cv.list[cv.used +  1] = REQ_TYPE_OPT >> 8;
        cv.list[cv.used +  2] = REQ_TYPE_OPT & 255;
        cv.list[cv.used +  3] = val >> 8;
        cv.list[cv.used +  4] = val & 255;
        cv.list[cv.used +  5] = 0; /* extended-rcode */
        cv.list[cv.used +  6] = 0; /* version */
        cv.list[cv.used +  7] = 0; /* reserved */
        cv.list[cv.used +  8] = 0; /* reserved */
        cv.list[cv.used +  9] = 0; /* msb rdlen */
        cv.list[cv.used + 10] = 0; /* lsb rdlen */
        cv.used += 11;
        cv.list[11] = 1; /* update arcount */
    } else cv.list[11] = 0;

out:
    free(req->body);
    req->body = (unsigned char*)cv.list;
    req->body_len = cv.used;
    return cv.used;
}

/** Build a DNS question packet.  After \a req, there is at least one
 * pair consisting of const char *name and unsigned int qtype.  A null
 * name argument terminates the list.
 */
unsigned int
sar_request_build(struct sar_request *req, ...)
{
    va_list vargs;
    unsigned int ret;
    va_start(vargs, req);
    ret = sar_request_vbuild(req, vargs);
    va_end(vargs);
    return ret;
}

void
sar_request_send(struct sar_request *req)
{
    dict_iterator_t it;

    /* make sure we have our local socket */
    if (!sar_fd && sar_open_fd()) {
        sar_request_fail(req, RCODE_SOCKET_FAILURE);
        return;
    }

    log_module(sar_log, LOG_DEBUG, "sar_request_send({id=%d})", req->id);

    /* send query to each configured nameserver */
    for (it = dict_first(sar_nameservers); it; it = iter_next(it)) {
        struct sar_nameserver *ns;
        int res;

        ns = iter_data(it);
        res = sendto(sar_fd_fd, req->body, req->body_len, 0, (struct sockaddr*)&ns->ss, ns->ss_len);
        if (res > 0) {
            ns->req_sent++;
            log_module(sar_log, LOG_DEBUG, "Sent %u bytes to %s.", res, ns->name);
        } else if (res < 0)
            log_module(sar_log, LOG_ERROR, "Unable to send %u bytes to nameserver %s: %s", req->body_len, ns->name, strerror(errno));
        else /* res == 0 */
            assert(0 && "resolver sendto() unexpectedly returned zero");
    }

    /* Check that query timeout is soon enough. */
    req->expiry = now + (conf.sar_timeout << ++req->retries);
    sar_check_timeout(req->expiry);
}

struct sar_request *
sar_request_alloc(unsigned int data_len, sar_request_ok_cb ok_cb, sar_request_fail_cb fail_cb)
{
    struct sar_request *req;

    req = calloc(1, sizeof(*req) + data_len);
    req->cb_ok = ok_cb;
    req->cb_fail = fail_cb;
    do {
        req->id = rand() & 0xffff;
        sprintf(req->id_text, "%d", req->id);
    } while (dict_find(sar_requests, req->id_text, NULL));
    dict_insert(sar_requests, req->id_text, req);
    log_module(sar_log, LOG_DEBUG, "sar_request_alloc(%d) -> {id=%d}", data_len, req->id);
    return req;
}

struct sar_request *
sar_request_simple(unsigned int data_len, sar_request_ok_cb ok_cb, sar_request_fail_cb fail_cb, ...)
{
    struct sar_request *req;

    req = sar_request_alloc(data_len, ok_cb, fail_cb);
    if (req) {
        va_list args;

        va_start(args, fail_cb);
        sar_request_vbuild(req, args);
        va_end(args);
        sar_request_send(req);
    }
    return req;
}

enum service_proto {
    SERVICE_UDP,
    SERVICE_TCP,
    SERVICE_NUM_PROTOS
};

struct service_byname {
    const char *name; /* service name */
    struct {
        /* note: if valid != 0, port == 0, check canonical entry */
        struct service_byname *canon; /* if NULL, this is canonical */
        uint16_t port;
        unsigned int valid : 1;
        unsigned int srv : 1;
    } protos[SERVICE_NUM_PROTOS];
};

struct service_byport {
    unsigned int port;
    char port_text[6];
    struct service_byname *byname[SERVICE_NUM_PROTOS];
};

static dict_t services_byname; /* contains struct service_byname */
static dict_t services_byport; /* contains struct service_byport */

static struct service_byname *
sar_service_byname(const char *name, int autocreate)
{
    struct service_byname *byname;

    byname = dict_find(services_byname, name, NULL);
    if (!byname && autocreate) {
        byname = calloc(1, sizeof(*byname) + strlen(name) + 1);
        byname->name = strcpy((char*)(byname + 1), name);
        dict_insert(services_byname, byname->name, byname);
    }
    return byname;
}

static struct service_byport *
sar_service_byport(unsigned int port, int autocreate)
{
    struct service_byport *byport;
    char port_text[12];

    sprintf(port_text, "%d", port);
    byport = dict_find(services_byport, port_text, NULL);
    if (!byport && autocreate) {
        byport = calloc(1, sizeof(*byport));
        byport->port = port;
        sprintf(byport->port_text, "%d", port);
        dict_insert(services_byport, byport->port_text, byport);
    }
    return byport;
}

static void
sar_services_load_file(const char *etc_services)
{
    static const char *whitespace = " \t\r\n";
    struct service_byname *canon;
    struct service_byport *byport;
    char *name, *port, *alias, *ptr;
    FILE *file;
    unsigned int pnum;
    enum service_proto proto;
    char linebuf[LINE_MAX];

    file = fopen(etc_services, "r");
    if (!file)
        return;
    while (fgets(linebuf, sizeof(linebuf), file)) {
        ptr = strchr(linebuf, '#');
        if (ptr)
            *ptr = '\0';
        /* Tokenize canonical service name and port number. */
        name = strtok_r(linebuf, whitespace, &ptr);
        if (name == NULL)
            continue;
        port = strtok_r(NULL, whitespace, &ptr);
        if (port == NULL)
            continue;
        pnum = strtoul(port, &port, 10);
        if (pnum == 0 || *port++ != '/')
            continue;
        if (!strcmp(port, "udp"))
            proto = SERVICE_UDP;
        else if (!strcmp(port, "tcp"))
            proto = SERVICE_TCP;
        else continue;

        /* Set up canonical name-indexed service entry. */
        canon = sar_service_byname(name, 1);
        if (canon->protos[proto].valid) {
            log_module(sar_log, LOG_ERROR, "Service %s/%s listed twice.", name, port);
            continue;
        }
        canon->protos[proto].canon = NULL;
        canon->protos[proto].port = pnum;
        canon->protos[proto].valid = 1;

        /* Set up port-indexed service entry. */
        byport = sar_service_byport(pnum, 1);
        if (!byport->byname[proto])
            byport->byname[proto] = canon;

        /* Add alias entries. */
        while ((alias = strtok_r(NULL, whitespace, &ptr))) {
            struct service_byname *byname;

            byname = sar_service_byname(alias, 1);
            if (byname->protos[proto].valid) {
                /* We do not log this since there are a lot of
                 * duplicate aliases, some only differing in case. */
                continue;
            }
            byname->protos[proto].canon = canon;
            byname->protos[proto].port = pnum;
            byname->protos[proto].valid = 1;
        }
    }
    fclose(file);
}

static void
sar_services_init(const char *etc_services)
{
    /* These are a portion of the services listed at
     * http://www.dns-sd.org/ServiceTypes.html.
     */
    static const char *tcp_srvs[] = { "cvspserver", "distcc", "ftp", "http",
        "imap", "ipp", "irc", "ldap", "login", "nfs", "pop3", "postgresql",
        "rsync", "sftp-ssh", "soap", "ssh", "telnet", "webdav", "xmpp-client",
        "xmpp-server", "xul-http", NULL };
    static const char *udp_srvs[] = { "bootps", "dns-update", "domain", "nfs",
        "ntp", "tftp", NULL };
    struct service_byname *byname;
    unsigned int ii;

    /* Forget old services dicts and allocate new ones. */
    dict_delete(services_byname);
    services_byname = dict_new();
    dict_set_free_data(services_byname, free);

    dict_delete(services_byport);
    services_byport = dict_new();
    dict_set_free_data(services_byport, free);

    /* Load the list from the services file. */
    sar_services_load_file(etc_services);

    /* Mark well-known services as using DNS-SD SRV records. */
    for (ii = 0; tcp_srvs[ii]; ++ii) {
        byname = sar_service_byname(tcp_srvs[ii], 1);
        byname->protos[SERVICE_TCP].srv = 1;
    }

    for (ii = 0; udp_srvs[ii]; ++ii) {
        byname = sar_service_byname(udp_srvs[ii], 1);
        byname->protos[SERVICE_UDP].srv = 1;
    }
}

static void
sar_register_helper(struct sar_family_helper *helper)
{
    assert(helper->family <= MAX_FAMILY);
    sar_helpers[helper->family] = helper;
    helper->next = sar_first_helper;
    sar_first_helper = helper;
}

static unsigned int
sar_addrlen(const struct sockaddr *sa, UNUSED_ARG(unsigned int size))
{
    return sa->sa_family <= MAX_FAMILY && sar_helpers[sa->sa_family]
        ? sar_helpers[sa->sa_family]->socklen : 0;
}

struct sar_getaddr_state {
    struct sar_family_helper *helper;
    struct addrinfo *ai_head;
    struct addrinfo *ai_tail;
    sar_addr_cb cb;
    void *cb_ctx;
    unsigned int search_pos;
    unsigned int flags, socktype, protocol, port;
    unsigned int srv_ofs;
    char full_name[DNS_NAME_LENGTH];
};

static unsigned int
sar_getaddr_append(struct sar_getaddr_state *state, struct addrinfo *ai, int copy)
{
    unsigned int count;

    log_module(sar_log, LOG_DEBUG, "sar_getaddr_append({full_name=%s}, ai=%p, copy=%d)", state->full_name, (void*)ai, copy);

    /* Set the appropriate pointer to the new element(s). */
    if (state->ai_tail)
        state->ai_tail->ai_next = ai;
    else
        state->ai_head = ai;

    /* Find the end of the list. */
    if (copy) {
        /* Make sure we copy fields for both the first and last entries. */
        count = 1;
        while (1) {
            if (!ai->ai_addrlen) {
                assert(sar_helpers[ai->ai_family]);
                ai->ai_addrlen = sar_helpers[ai->ai_family]->socklen;
            }
#if defined(HAVE_SOCKADDR_SA_LEN)
            ai->ai_addr->sa_len = ai->ai_addrlen;
#endif
            ai->ai_addr->sa_family = ai->ai_family;
            ai->ai_socktype = state->socktype;
            ai->ai_protocol = state->protocol;
            if (!ai->ai_next)
                break;
            count++;
            ai = ai->ai_next;
        }
    } else {
        for (count = 1; ai->ai_next; ++count, ai = ai->ai_next)
            ;
    }

    /* Set the tail pointer and return count of appended items. */
    state->ai_tail = ai;
    return count;
}

static struct sar_request *
sar_getaddr_request(struct sar_request *req)
{
    struct sar_getaddr_state *state;
    unsigned int len;
    char full_name[DNS_NAME_LENGTH];

    state = (struct sar_getaddr_state*)(req + 1);

    /* If we can and should, append the current search domain. */
    if (state->search_pos < conf.sar_search->used)
        snprintf(full_name, sizeof(full_name), "%s.%s", state->full_name, conf.sar_search->list[state->search_pos]);
    else if (state->search_pos == conf.sar_search->used)
        safestrncpy(full_name, state->full_name, sizeof(full_name));
    else {
        log_module(sar_log, LOG_DEBUG, "sar_getaddr_request({id=%d}): failed", req->id);
        state->cb(state->cb_ctx, NULL, SAI_NONAME);
        return NULL;
    }

    /* Build the appropriate request for DNS record(s). */
    if (state->flags & SAI_ALL)
        len = sar_request_build(req, full_name + state->srv_ofs, REQ_QTYPE_ALL, NULL);
    else if (state->srv_ofs)
        len = state->helper->build_addr_request(req, full_name + state->srv_ofs, full_name, state->flags);
    else
        len = state->helper->build_addr_request(req, full_name, NULL, state->flags);

    log_module(sar_log, LOG_DEBUG, "sar_getaddr_request({id=%d}): full_name=%s, srv_ofs=%d", req->id, full_name, state->srv_ofs);

    /* Check that the request could be built. */
    if (!len) {
        state->cb(state->cb_ctx, NULL, SAI_NODATA);
        return NULL;
    }

    /* Send the request. */
    sar_request_send(req);
    return req;
}

static int
sar_getaddr_decode(struct sar_request *req, struct dns_header *hdr, struct dns_rr *rr, unsigned char *raw, unsigned int raw_size, unsigned int rr_idx)
{
    struct sar_getaddr_state *state;
    char *cname;
    unsigned int jj, pos, hit;

    log_module(sar_log, LOG_DEBUG, "  sar_getaddr_decode(id=%d, <hdr>, {type=%d, rdlength=%d, name=%s}, <data>, %u, <idx>)", hdr->id, rr[rr_idx].type, rr[rr_idx].rdlength, rr[rr_idx].name, raw_size);
    state = (struct sar_getaddr_state*)(req + 1);

    switch (rr[rr_idx].type) {
    case REQ_TYPE_A:
        if (state->flags & SAI_ALL)
            return sar_ipv4_helper.decode_addr(state, rr + rr_idx, raw, raw_size);
#if defined(AF_INET6)
        else if (state->flags & SAI_V4MAPPED)
            return sar_ipv6_helper.decode_addr(state, rr + rr_idx, raw, raw_size);
#endif
        return state->helper->decode_addr(state, rr + rr_idx, raw, raw_size);

    case REQ_TYPE_AAAA:
#if defined(AF_INET6)
        if (state->flags & SAI_ALL)
            return sar_ipv6_helper.decode_addr(state, rr + rr_idx, raw, raw_size);
        return state->helper->decode_addr(state, rr + rr_idx, raw, raw_size);
#else
        return 0;
#endif

    case REQ_TYPE_CNAME:
        /* there should be the canonical name next */
        pos = rr[rr_idx].rd_start;
        cname = sar_extract_name(raw, raw_size, &pos);
        if (!cname)
            return 0; /* XXX: eventually log the unhandled body */
        /* and it should correspond to some other answer in the response */
        for (jj = hit = 0; jj < hdr->ancount; ++jj) {
            if (strcasecmp(cname, rr[jj].name))
                continue;
            hit += sar_getaddr_decode(req, hdr, rr, raw, raw_size, jj);
        }
        /* XXX: if (!hit) handle or log the incomplete recursion; */
        return hit;

    case REQ_TYPE_SRV:
        /* TODO: decode the SRV record */

    default:
        return 0;
    }
}

static void
sar_getaddr_ok(struct sar_request *req, struct dns_header *hdr, struct dns_rr *rr, unsigned char *raw, unsigned int raw_size)
{
    struct sar_getaddr_state *state;
    unsigned int ii;

    state = (struct sar_getaddr_state*)(req + 1);

    log_module(sar_log, LOG_DEBUG, "sar_getaddr_ok({id=%d}, {id=%d}, <rr>, <data>, %u)", req->id, hdr->id, raw_size);
    for (ii = 0; ii < hdr->ancount; ++ii)
        sar_getaddr_decode(req, hdr, rr, raw, raw_size, ii);

    /* If we found anything, report it, else try again. */
    if (state->ai_head)
        state->cb(state->cb_ctx, state->ai_head, SAI_SUCCESS);
    else
        sar_getaddr_request(req);
}

static void
sar_getaddr_fail(struct sar_request *req, UNUSED_ARG(unsigned int rcode))
{
    struct sar_getaddr_state *state;

    log_module(sar_log, LOG_DEBUG, "sar_getaddr_fail({id=%d}, rcode=%u)", req->id, rcode);
    state = (struct sar_getaddr_state*)(req + 1);
    state->cb(state->cb_ctx, NULL, SAI_FAIL);
}

struct sar_request *
sar_getaddr(const char *node, const char *service, const struct addrinfo *hints_, sar_addr_cb cb, void *cb_ctx)
{
    struct sockaddr_storage ss;
    struct addrinfo hints;
    struct sar_family_helper *helper;
    struct service_byname *svc;
    char *end;
    unsigned int portnum;
    unsigned int pos;
    enum service_proto proto;

    if (!node && !service) {
        cb(cb_ctx, NULL, SAI_NONAME);
        return NULL;
    }

    /* Initialize local hints structure. */
    if (hints_)
        memcpy(&hints, hints_, sizeof(hints));
    else
        memset(&hints, 0, sizeof(hints));

    /* Translate socket type to internal protocol. */
    switch (hints.ai_socktype) {
    case 0: hints.ai_socktype = SOCK_STREAM; /* and fall through */
    case SOCK_STREAM: proto = SERVICE_TCP; break;
    case SOCK_DGRAM: proto = SERVICE_UDP; break;
    default:
        cb(cb_ctx, NULL, SAI_SOCKTYPE);
        return NULL;
    }

    /* Figure out preferred socket size. */
    if (hints.ai_family == AF_UNSPEC)
        hints.ai_family = AF_INET;
    if (hints.ai_family > MAX_FAMILY
        || !(helper = sar_helpers[hints.ai_family])) {
        cb(cb_ctx, NULL, SAI_FAMILY);
        return NULL;
    }
    hints.ai_addrlen = helper->socklen;

    /* If \a node is NULL, figure out the correct default from the
     * requested family and SAI_PASSIVE flag.
     */
    if (node == NULL)
        node = (hints.ai_flags & SAI_PASSIVE) ? helper->unspec_addr : helper->localhost_addr;

    /* Try to parse (failing that, look up) \a service. */
    if (!service)
        portnum = 0, svc = NULL;
    else if ((portnum = strtoul(service, &end, 10)), *end == '\0')
        svc = NULL;
    else if ((svc = sar_service_byname(service, 0)) != NULL)
        portnum = svc->protos[proto].port;
    else {
        cb(cb_ctx, NULL, SAI_SERVICE);
        return NULL;
    }

    /* Try to parse \a node as a numeric hostname.*/
    pos = sar_pton((struct sockaddr*)&ss, sizeof(ss), NULL, node);
    if (pos && node[pos] == '\0') {
        struct addrinfo *ai;
        char canonname[SAR_NTOP_MAX];

        /* we have a valid address; use it */
        sar_set_port((struct sockaddr*)&ss, sizeof(ss), portnum);
        hints.ai_addrlen = sar_addrlen((struct sockaddr*)&ss, sizeof(ss));
        if (!hints.ai_addrlen) {
            cb(cb_ctx, NULL, SAI_FAMILY);
            return NULL;
        }
        pos = sar_ntop(canonname, sizeof(canonname), (struct sockaddr*)&ss, hints.ai_addrlen);

        /* allocate and fill in the addrinfo response */
        ai = calloc(1, sizeof(*ai) + hints.ai_addrlen + pos + 1);
        ai->ai_family = ss.ss_family;
        ai->ai_socktype = hints.ai_socktype;
        ai->ai_protocol = hints.ai_protocol;
        ai->ai_addrlen = hints.ai_addrlen;
        ai->ai_addr = memcpy(ai + 1, &ss, ai->ai_addrlen);
        ai->ai_canonname = strcpy((char*)ai->ai_addr + ai->ai_addrlen, canonname);
        cb(cb_ctx, ai, SAI_SUCCESS);
        return NULL;
    } else if (hints.ai_flags & SAI_NUMERICHOST) {
        cb(cb_ctx, NULL, SAI_NONAME);
        return NULL;
    } else {
        struct sar_request *req;
        struct sar_getaddr_state *state;
        unsigned int len, ii;

        req = sar_request_alloc(sizeof(*state), sar_getaddr_ok, sar_getaddr_fail);

        state = (struct sar_getaddr_state*)(req + 1);
        state->helper = helper;
        state->ai_head = state->ai_tail = NULL;
        state->cb = cb;
        state->cb_ctx = cb_ctx;
        state->flags = hints.ai_flags;
        state->socktype = hints.ai_socktype;
        state->protocol = hints.ai_protocol;
        state->port = portnum;

        if ((state->flags & SAI_NOSRV) || !svc)
            state->srv_ofs = 0;
        else if (svc->protos[proto].srv)
            state->srv_ofs = snprintf(state->full_name, sizeof(state->full_name), "_%s._%s.", svc->name, (proto == SERVICE_UDP ? "udp" : "tcp"));
        else if (state->flags & SAI_FORCESRV)
            state->srv_ofs = snprintf(state->full_name, sizeof(state->full_name), "_%s._%s.", service, (proto == SERVICE_UDP ? "udp" : "tcp"));
        else
            state->srv_ofs = 0;

        if (state->srv_ofs < sizeof(state->full_name))
            safestrncpy(state->full_name + state->srv_ofs, node, sizeof(state->full_name) - state->srv_ofs);

        for (ii = len = 0; node[ii]; ++ii)
            if (node[ii] == '.')
                len++;
        if (len >= conf.sar_ndots)
            state->search_pos = conf.sar_search->used;
        else
            state->search_pos = 0;

        /* XXX: fill in *state with any other fields needed to parse responses. */

        if (!sar_getaddr_request(req)) {
            free(req);
            return NULL;
        }
        return req;
    }
}

struct sar_getname_state {
    sar_name_cb cb;
    void *cb_ctx;
    char *hostname;
    unsigned int flags;
    unsigned int family;
    enum service_proto proto;
    unsigned short port;
    unsigned int doing_arpa : 1; /* checking .ip6.arpa vs .ip6.int */
    unsigned char original[16]; /* original address data */
    /* name must be long enough to hold "0.0.<etc>.ip6.arpa" */
    char name[74];
};

static void
sar_getname_fail(struct sar_request *req, UNUSED_ARG(unsigned int rcode))
{
    struct sar_getname_state *state;
    unsigned int len;

    state = (struct sar_getname_state*)(req + 1);
    if (state->doing_arpa) {
        len = strlen(state->name);
        assert(len == 73);
        strcpy(state->name + len - 4, "int");
        len = sar_request_build(req, state->name, REQ_TYPE_PTR, NULL);
        if (len) {
            sar_request_send(req);
            return;
        }
    }
    state->cb(state->cb_ctx, NULL, NULL, SAI_FAIL);
    free(state->hostname);
}

static const char *sar_getname_port(unsigned int port, unsigned int flags, char *tmpbuf, unsigned int tmpbuf_len)
{
    struct service_byport *service;
    enum service_proto proto;
    char port_text[12];

    sprintf(port_text, "%d", port);
    proto = (flags & SNI_DGRAM) ? SERVICE_UDP : SERVICE_TCP;
    if (!(flags & SNI_NUMERICSERV)
        && (service = dict_find(services_byport, port_text, NULL))
        && service->byname[proto])
        return service->byname[proto]->name;
    snprintf(tmpbuf, tmpbuf_len, "%d", port);
    return tmpbuf;
}

static void
sar_getname_confirm(struct sar_request *req, struct dns_header *hdr, struct dns_rr *rr, unsigned char *raw, unsigned int raw_size)
{
    struct sar_getname_state *state;
    const unsigned char *data;
    const char *portname;
    char servbuf[16];
    unsigned int ii, nbr;

    state = (struct sar_getname_state*)(req + 1);
    for (ii = 0; ii < hdr->ancount; ++ii) {
        /* Is somebody confused or trying to play games? */
        if (rr[ii].class != REQ_CLASS_IN
            || strcasecmp(state->hostname, rr[ii].name))
            continue;
        switch (rr[ii].type) {
        case REQ_TYPE_A: nbr = 4; break;
        case REQ_TYPE_AAAA: nbr = 16; break;
        default: continue;
        }
        data = sar_extract_rdata(rr, nbr, raw, raw_size);
        if (data && !memcmp(data, state->original, nbr)) {
            portname = sar_getname_port(state->port, state->flags, servbuf, sizeof(servbuf));
            state->cb(state->cb_ctx, state->hostname, portname, SAI_SUCCESS);
            free(state->hostname);
            return;
        }
    }
    state->cb(state->cb_ctx, NULL, NULL, SAI_MISMATCH);
    free(state->hostname);
}

static void
sar_getname_ok(struct sar_request *req, struct dns_header *hdr, struct dns_rr *rr, unsigned char *raw, unsigned int raw_size)
{
    struct sar_getname_state *state;
    const char *portname;
    unsigned int ii, pos;
    char servbuf[16];

    state = (struct sar_getname_state*)(req + 1);
    for (ii = 0; ii < hdr->ancount; ++ii) {
        if (rr[ii].type != REQ_TYPE_PTR
            || rr[ii].class != REQ_CLASS_IN
            || strcasecmp(rr[ii].name, state->name))
            continue;
        pos = rr[ii].rd_start;
        state->hostname = sar_extract_name(raw, raw_size, &pos);
        break;
    }

    if (!state->hostname) {
        state->cb(state->cb_ctx, NULL, NULL, SAI_NONAME);
        return;
    }

    if (state->flags & SNI_PARANOID) {
        req->cb_ok = sar_getname_confirm;
        pos = sar_helpers[state->family]->build_addr_request(req, state->hostname, NULL, 0);
        if (pos)
            sar_request_send(req);
        else {
            free(state->hostname);
            state->cb(state->cb_ctx, NULL, NULL, SAI_FAIL);
        }
        return;
    }

    portname = sar_getname_port(state->port, state->flags, servbuf, sizeof(servbuf));
    state->cb(state->cb_ctx, state->hostname, portname, SAI_SUCCESS);
    free(state->hostname);
}

struct sar_request *
sar_getname(const struct sockaddr *sa, unsigned int salen, int flags, sar_name_cb cb, void *cb_ctx)
{
    struct sar_family_helper *helper;
    struct sar_request *req;
    struct sar_getname_state *state;
    unsigned int len;
    int port;

    if (sa->sa_family > MAX_FAMILY
        || !(helper = sar_helpers[sa->sa_family])) {
        cb(cb_ctx, NULL, NULL, SAI_FAMILY);
        return NULL;
    }

    port = helper->get_port(sa, salen);

    if (flags & SNI_NUMERICHOST) {
        const char *servname;
        char host[SAR_NTOP_MAX], servbuf[16];

        /* If appropriate, try to look up service name. */
        servname = sar_getname_port(port, flags, servbuf, sizeof(servbuf));
        len = sar_ntop(host, sizeof(host), sa, salen);
        assert(len != 0);
        cb(cb_ctx, host, servname, SAI_SUCCESS);
        return NULL;
    }

    req = sar_request_alloc(sizeof(*state), sar_getname_ok, sar_getname_fail);

    state = (struct sar_getname_state*)(req + 1);
    state->cb = cb;
    state->cb_ctx = cb_ctx;
    state->flags = flags;
    state->family = sa->sa_family;
    state->port = port;

    helper->build_ptr_name(state, sa, salen);
    assert(strlen(state->name) < sizeof(state->name));
    len = sar_request_build(req, state->name, REQ_TYPE_PTR, NULL);
    if (!len) {
        cb(cb_ctx, NULL, NULL, SAI_NODATA);
        free(req);
        return NULL;
    }

    sar_request_send(req);
    return req;
}

static unsigned int
ipv4_ntop(char *output, unsigned int out_size, const struct sockaddr *sa, UNUSED_ARG(unsigned int socklen))
{
    struct sockaddr_in *sin;
    unsigned int ip4, pos;

    sin = (struct sockaddr_in*)sa;
    ip4 = ntohl(sin->sin_addr.s_addr);
    pos = snprintf(output, out_size, "%u.%u.%u.%u", (ip4 >> 24), (ip4 >> 16) & 255, (ip4 >> 8) & 255, ip4 & 255);
    return (pos < out_size) ? pos : 0;
}

static unsigned int
sar_pton_ip4(const char *input, unsigned int *bits, uint32_t *output)
{
    unsigned int dots = 0, pos = 0, part = 0, ip = 0;

    /* Intentionally no support for bizarre IPv4 formats (plain
     * integers, octal or hex components) -- only vanilla dotted
     * decimal quads, optionally with trailing /nn.
     */
    if (input[0] == '.')
        return 0;
    while (1) {
        if (isdigit(input[pos])) {
            part = part * 10 + input[pos++] - '0';
            if (part > 255)
                return 0;
            if ((dots == 3) && !isdigit(input[pos])) {
                *output = htonl(ip | part);
                return pos;
            }
        } else if (input[pos] == '.') {
            if (input[++pos] == '.')
                return 0;
            ip |= part << (24 - 8 * dots++);
            part = 0;
        } else if (bits && input[pos] == '/' && isdigit(input[pos + 1])) {
            unsigned int len;
            char *term;

            len = strtoul(input + pos + 1, &term, 10);
            if (term <= input + pos + 1)
                return pos;
            else if (len > 32)
                return 0;
            *bits = len;
            return term - input;
        } else return 0;
    }
}

static unsigned int
ipv4_pton(struct sockaddr *sa, UNUSED_ARG(unsigned int socklen), unsigned int *bits, const char *input)
{
    unsigned int pos;

    pos = sar_pton_ip4(input, bits, &((struct sockaddr_in*)sa)->sin_addr.s_addr);
    if (!pos)
        return 0;
    sa->sa_family = AF_INET;
#if defined(HAVE_SOCKADDR_SA_LEN)
    sa->sa_len = sizeof(struct sockaddr_in);
#endif
    return pos;
}

static int
ipv4_get_port(const struct sockaddr *sa, UNUSED_ARG(unsigned int socklen))
{
    return ntohs(((const struct sockaddr_in*)sa)->sin_port);
}

static int
ipv4_set_port(struct sockaddr *sa, UNUSED_ARG(unsigned int socklen), unsigned short port)
{
    ((struct sockaddr_in*)sa)->sin_port = htons(port);
    return 0;
}

static unsigned int
ipv4_addr_request(struct sar_request *req, const char *node, const char *srv_node, UNUSED_ARG(unsigned int flags))
{
    unsigned int len;
    if (srv_node)
        len = sar_request_build(req, node, REQ_TYPE_A, srv_node, REQ_TYPE_SRV, NULL);
    else
        len = sar_request_build(req, node, REQ_TYPE_A, NULL);
    return len;
}

static void
ipv4_ptr_name(struct sar_getname_state *state, const struct sockaddr *sa, UNUSED_ARG(unsigned int socklen))
{
    const uint8_t *bytes;

    bytes = (uint8_t*)&((struct sockaddr_in*)sa)->sin_addr.s_addr;
    memcpy(state->original, bytes, 4);
    snprintf(state->name, sizeof(state->name),
             "%u.%u.%u.%u.in-addr.arpa",
             bytes[3], bytes[2], bytes[1], bytes[0]);
}

static int
ipv4_decode(struct sar_getaddr_state *state, struct dns_rr *rr, unsigned char *raw, UNUSED_ARG(unsigned int raw_size))
{
    struct sockaddr_in *sa;
    struct addrinfo *ai;

    if (rr->rdlength != 4)
        return 0;

    if (state->flags & SAI_CANONNAME) {
        ai = calloc(1, sizeof(*ai) + sizeof(*sa) + strlen(rr->name) + 1);
        sa = (struct sockaddr_in*)(ai->ai_addr = (struct sockaddr*)(ai + 1));
        ai->ai_canonname = strcpy((char*)(sa + 1), rr->name);
    } else {
        ai = calloc(1, sizeof(*ai) + sizeof(*sa));
        sa = (struct sockaddr_in*)(ai->ai_addr = (struct sockaddr*)(ai + 1));
        ai->ai_canonname = NULL;
    }

    ai->ai_family = AF_INET;
    sa->sin_port = htons(state->port);
    memcpy(&sa->sin_addr.s_addr, raw + rr->rd_start, 4);
    return sar_getaddr_append(state, ai, 1);
}

static struct sar_family_helper sar_ipv4_helper = {
    "127.0.0.1",
    "0.0.0.0",
    sizeof(struct sockaddr_in),
    AF_INET,
    ipv4_ntop,
    ipv4_pton,
    ipv4_get_port,
    ipv4_set_port,
    ipv4_addr_request,
    ipv4_ptr_name,
    ipv4_decode,
    NULL
};

#if defined(AF_INET6)

static unsigned int
ipv6_ntop(char *output, unsigned int out_size, const struct sockaddr *sa, UNUSED_ARG(unsigned int socklen))
{
    struct sockaddr_in6 *sin6;
    unsigned int pos, part, max_start, max_zeros, curr_zeros, ii;
    unsigned short addr16;

    sin6 = (struct sockaddr_in6*)sa;
    /* Find longest run of zeros. */
    for (max_start = max_zeros = curr_zeros = ii = 0; ii < 8; ++ii) {
        addr16 = (sin6->sin6_addr.s6_addr[ii * 2] << 8) | sin6->sin6_addr.s6_addr[ii * 2 + 1];
        if (!addr16)
            curr_zeros++;
        else if (curr_zeros > max_zeros) {
            max_start = ii - curr_zeros;
            max_zeros = curr_zeros;
            curr_zeros = 0;
        }
    }
    if (curr_zeros > max_zeros) {
        max_start = ii - curr_zeros;
        max_zeros = curr_zeros;
    }

    /* Print out address. */
#define APPEND(CH) do { output[pos++] = (CH); if (pos >= out_size) return 0; } while (0)
    for (pos = 0, ii = 0; ii < 8; ++ii) {
        if ((max_zeros > 0) && (ii == max_start)) {
            if (ii == 0)
                APPEND(':');
            APPEND(':');
            ii += max_zeros - 1;
            continue;
        }
        part = (sin6->sin6_addr.s6_addr[ii * 2] << 8) | sin6->sin6_addr.s6_addr[ii * 2 + 1];
        if (part >= 0x1000)
            APPEND(hexdigits[part >> 12]);
        if (part >= 0x100)
            APPEND(hexdigits[(part >> 8) & 15]);
        if (part >= 0x10)
            APPEND(hexdigits[(part >> 4) & 15]);
        APPEND(hexdigits[part & 15]);
        if (ii < 7)
            APPEND(':');
    }
    APPEND('\0');
#undef APPEND

    return pos;
}

static const unsigned char xdigit_value[256] = {
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
   0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static unsigned int
ipv6_pton(struct sockaddr *sa, UNUSED_ARG(unsigned int socklen), unsigned int *bits, const char *input)
{
    const char *part_start = NULL;
    struct sockaddr_in6 *sin6;
    char *colon;
    char *dot;
    unsigned int part = 0, pos = 0, ii = 0, cpos = 8;

    if (!(colon = strchr(input, ':')))
        return 0;
    dot = strchr(input, '.');
    if (dot && dot < colon)
        return 0;
    sin6 = (struct sockaddr_in6*)sa;
    /* Parse IPv6, possibly like ::127.0.0.1.
     * This is pretty straightforward; the only trick is borrowed
     * from Paul Vixie (BIND): when it sees a "::" continue as if
     * it were a single ":", but note where it happened, and fill
     * with zeros afterwards.
     */
    if (input[pos] == ':') {
        if ((input[pos+1] != ':') || (input[pos+2] == ':'))
            return 0;
        cpos = 0;
        pos += 2;
        part_start = input + pos;
    }
    while (ii < 8) {
        if (isxdigit(input[pos])) {
            part = (part << 4) | xdigit_value[(unsigned char)input[pos]];
            if (part > 0xffff)
                return 0;
            pos++;
        } else if (input[pos] == ':') {
            part_start = input + ++pos;
            if (input[pos] == '.')
                return 0;
            sin6->sin6_addr.s6_addr[ii * 2] = part >> 8;
            sin6->sin6_addr.s6_addr[ii * 2 + 1] = part & 255;
            ii++;
            part = 0;
            if (input[pos] == ':') {
                if (cpos < 8)
                    return 0;
                cpos = ii;
                pos++;
            }
        } else if (input[pos] == '.') {
            uint32_t ip4;
            unsigned int len;
            len = sar_pton_ip4(part_start, bits, &ip4);
            if (!len || (ii > 6))
                return 0;
            memcpy(sin6->sin6_addr.s6_addr + ii * 2, &ip4, sizeof(ip4));
            if (bits)
                *bits += ii * 16;
            ii += 2;
            pos = part_start + len - input;
            break;
        } else if (bits && input[pos] == '/' && isdigit(input[pos + 1])) {
            unsigned int len;
            char *term;

            len = strtoul(input + pos + 1, &term, 10);
            if (term <= input + pos + 1)
                break;
            else if (len > 128)
                return 0;
            if (bits)
                *bits = len;
            pos = term - input;
            break;
        } else if (cpos <= 8) {
            sin6->sin6_addr.s6_addr[ii * 2] = part >> 8;
            sin6->sin6_addr.s6_addr[ii * 2 + 1] = part & 255;
            ii++;
            break;
        } else return 0;
    }
    /* Shift stuff after "::" up and fill middle with zeros. */
    if (cpos < 8) {
        unsigned int jj;
        ii <<= 1;
        cpos <<= 1;
        for (jj = 0; jj < ii - cpos; jj++)
            sin6->sin6_addr.s6_addr[15 - jj] = sin6->sin6_addr.s6_addr[ii - jj - 1];
        for (jj = 0; jj < 16 - ii; jj++)
            sin6->sin6_addr.s6_addr[cpos + jj] = 0;
    }
    sa->sa_family = AF_INET6;
#if defined(HAVE_SOCKADDR_SA_LEN)
    sa->sa_len = sizeof(struct sockaddr_in6);
#endif
    return pos;
}

static int
ipv6_get_port(const struct sockaddr *sa, UNUSED_ARG(unsigned int socklen))
{
    return ntohs(((const struct sockaddr_in6*)sa)->sin6_port);
}

static int
ipv6_set_port(struct sockaddr *sa, UNUSED_ARG(unsigned int socklen), unsigned short port)
{
    ((struct sockaddr_in6*)sa)->sin6_port = htons(port);
    return 0;
}

static unsigned int
ipv6_addr_request(struct sar_request *req, const char *node, const char *srv_node, unsigned int flags)
{
    unsigned int len;
    if (flags & SAI_V4MAPPED) {
        if (srv_node)
            len = sar_request_build(req, node, REQ_TYPE_AAAA, node, REQ_TYPE_A, srv_node, REQ_TYPE_SRV, NULL);
        else
            len = sar_request_build(req, node, REQ_TYPE_AAAA, node, REQ_TYPE_A, NULL);
    } else {
        if (srv_node)
            len = sar_request_build(req, node, REQ_TYPE_AAAA, srv_node, REQ_TYPE_SRV, NULL);
        else
            len = sar_request_build(req, node, REQ_TYPE_AAAA, NULL);
    }
    return len;
}

static void
ipv6_ptr_name(struct sar_getname_state *state, const struct sockaddr *sa, UNUSED_ARG(unsigned int socklen))
{
    const uint8_t *bytes;
    unsigned int ii, jj;

    bytes = ((struct sockaddr_in6*)sa)->sin6_addr.s6_addr;
    memcpy(state->original, bytes, 16);
    for (jj = 0, ii = 16; ii > 0; ) {
        state->name[jj++] = hexdigits[bytes[--ii] & 15];
        state->name[jj++] = hexdigits[bytes[ii] >> 4];
        state->name[jj++] = '.';
    }
    strcpy(state->name + jj, ".ip6.arpa");
    state->doing_arpa = 1;
}

static int
ipv6_decode(struct sar_getaddr_state *state, struct dns_rr *rr, unsigned char *raw, UNUSED_ARG(unsigned int raw_size))
{
    struct sockaddr_in6 *sa;
    struct addrinfo *ai;

    if (state->flags & SAI_CANONNAME) {
        ai = calloc(1, sizeof(*ai) + sizeof(*sa) + strlen(rr->name) + 1);
        sa = (struct sockaddr_in6*)(ai->ai_addr = (struct sockaddr*)(ai + 1));
        ai->ai_canonname = strcpy((char*)(sa + 1), rr->name);
    } else {
        ai = calloc(1, sizeof(*ai) + sizeof(*sa));
        sa = (struct sockaddr_in6*)(ai->ai_addr = (struct sockaddr*)(ai + 1));
        ai->ai_canonname = NULL;
    }

    if (rr->rdlength == 4) {
        sa->sin6_addr.s6_addr[10] = sa->sin6_addr.s6_addr[11] = 0xff;
        memcpy(sa->sin6_addr.s6_addr + 12, raw + rr->rd_start, 4);
    } else if (rr->rdlength == 16) {
        memcpy(sa->sin6_addr.s6_addr, raw + rr->rd_start, 16);
    } else {
        free(ai);
        return 0;
    }

    ai->ai_family = AF_INET6;
    sa->sin6_port = htons(state->port);
    return sar_getaddr_append(state, ai, 1);
}

static struct sar_family_helper sar_ipv6_helper = {
    "::1",
    "::",
    sizeof(struct sockaddr_in6),
    AF_INET6,
    ipv6_ntop,
    ipv6_pton,
    ipv6_get_port,
    ipv6_set_port,
    ipv6_addr_request,
    ipv6_ptr_name,
    ipv6_decode,
    NULL
};

#endif /* defined(AF_INET6) */

static void
sar_cleanup(void)
{
    ioset_close(sar_fd, 1);
    dict_delete(services_byname);
    dict_delete(services_byport);
    dict_delete(sar_nameservers);
    dict_delete(sar_requests);
    free_string_list(conf.sar_search);
    free_string_list(conf.sar_nslist);
}

static void
sar_conf_reload(void)
{
    dict_t node;
    const char *resolv_conf = "/etc/resolv.conf";
    const char *services = "/etc/services";
    const char *str;

    node = conf_get_data("modules/sar", RECDB_OBJECT);
    if (node != NULL) {
        str = database_get_data(node, "resolv_conf", RECDB_QSTRING);
        if (str) resolv_conf = str;
        str = database_get_data(node, "services", RECDB_QSTRING);
        if (str) services = str;
    }
    sar_dns_init(resolv_conf);
    sar_services_init(services);
}

void
sar_init(void)
{
    reg_exit_func(sar_cleanup);
    sar_log = log_register_type("sar", NULL);

    sar_requests = dict_new();
    dict_set_free_data(sar_requests, sar_request_cleanup);

    sar_nameservers = dict_new();
    dict_set_free_data(sar_nameservers, free);

    sar_register_helper(&sar_ipv4_helper);
#if defined(AF_INET6)
    sar_register_helper(&sar_ipv6_helper);
#endif

    conf_register_reload(sar_conf_reload);
}

/* mod-sockcheck.c - insecure proxy checking
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

#include "conf.h"
#include "gline.h"
#include "ioset.h"
#include "modcmd.h"
#include "timeq.h"

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

/* TODO, 1.3 or later: allow rules like "27374:" "reject:Subseven detected";
 * (For convenience; right now it assumes that there will be a state
 * rather than an immediate response upon connection.)
 */

#if !defined(SOCKCHECK_DEBUG)
#define SOCKCHECK_DEBUG 0
#endif
#define SOCKCHECK_TEST_DB "sockcheck.conf"

enum sockcheck_decision {
    CHECKING,
    ACCEPT,
    REJECT
};

typedef struct {
    irc_in_addr_t addr;
    const char *reason;
    time_t last_touched;
    enum sockcheck_decision decision;
    char hostname[IRC_NTOP_MAX_SIZE]; /* acts as key for checked_ip_dict */
} *sockcheck_cache_info;

DECLARE_LIST(sci_list, sockcheck_cache_info);
DEFINE_LIST(sci_list, sockcheck_cache_info)

/* Here's the list of hosts that need to be started on.
 */
static struct sci_list pending_sci_list;

/* Map of previously checked IPs state (if we've accepted the address yet).
 * The data for each entry is a pointer to a sockcheck_cache_info.
 */
static dict_t checked_ip_dict;

/* Each sockcheck template is formed as a Mealy state machine (that is,
 * the output on a state transition is a function of both the current
 * state and the input).  Mealy state machines require fewer states to
 * match the same input than Moore machines (where the output is only
 * a function of the current state).
 *
 * A state is characterized by sending some data (possibly nothing),
 * waiting a certain amount of time to receive one of zero or more
 * responses, and a decision (accept, reject, continue to another
 * state) based on the received response.
 */

struct sockcheck_response {
    const char *template;
    struct sockcheck_state *next;
};

DECLARE_LIST(response_list, struct sockcheck_response *);
DEFINE_LIST(response_list, struct sockcheck_response *)
static unsigned int max_responses;

struct sockcheck_state {
    unsigned short port;
    unsigned short timeout;
    unsigned short reps;
    enum sockcheck_decision type;
    const char *template;
    struct response_list responses;
};

struct sockcheck_list {
    unsigned int size, used, refs;
    struct sockcheck_state **list;
};

/*
 * List of tests.
 */
static struct sockcheck_list *tests;

/* Stuff to track client state, one instance per open connection. */
struct sockcheck_client {
    struct io_fd *fd;
    struct sockcheck_list *tests;
    sockcheck_cache_info addr;
    unsigned int client_index;
    unsigned int test_index;
    unsigned short test_rep;
    struct sockcheck_state *state;
    unsigned int read_size, read_used, read_pos;
    char *read;
    const char **resp_state;
};

static struct {
    unsigned int max_clients;
    unsigned int max_read;
    unsigned int gline_duration;
    unsigned int max_cache_age;
    struct sockaddr *local_addr;
    int local_addr_len;
} sockcheck_conf;

static unsigned int sockcheck_num_clients;
static struct sockcheck_client **client_list;
static unsigned int proxies_detected, checked_ip_count;
static struct module *sockcheck_module;
static struct log_type *PC_LOG;
const char *sockcheck_module_deps[] = { NULL };

static const struct message_entry msgtab[] = {
    { "PCMSG_PROXY_DEFINITION_FAILED", "Proxy definition failed: %s" },
    { "PCMSG_PROXY_DEFINITION_SUCCEEDED", "New proxy type defined." },
    { "PCMSG_UNSCANNABLE_IP", "%s has a spoofed, hidden or localnet IP." },
    { "PCMSG_ADDRESS_QUEUED", "$b%s$b is now queued to be proxy-checked." },
    { "PCMSG_ADDRESS_UNRESOLVED", "Unable to resolve $b%s$b to an IP address." },
    { "PCMSG_CHECKING_ADDRESS", "$b%s$b is currently being checked; unable to clear it." },
    { "PCMSG_NOT_REMOVED_FROM_CACHE", "$b%s$b was not cached and therefore was not cleared." },
    { "PCMSG_REMOVED_FROM_CACHE", "$b%s$b was cleared from the cached hosts list." },
    { "PCMSG_DISABLED", "Proxy scanning is $bdisabled$b." },
    { "PCMSG_NOT_CACHED", "No proxycheck records exist for IP %s." },
    { "PCMSG_STATUS_CHECKING", "IP %s proxycheck state: last touched %s ago, still checking" },
    { "PCMSG_STATUS_ACCEPTED", "IP %s proxycheck state: last touched %s ago, acepted" },
    { "PCMSG_STATUS_REJECTED", "IP %s proxycheck state: last touched %s ago, rejected: %s" },
    { "PCMSG_STATUS_UNKNOWN", "IP %s proxycheck state: last touched %s ago, invalid status" },
    { "PCMSG_STATISTICS", "Since booting, I have checked %d clients for illicit proxies, and detected %d proxy hosts.\nI am currently checking %d clients (out of %d max) and have a backlog of %d more to start on.\nI currently have %d hosts cached.\nI know how to detect %d kinds of proxies." },
    { NULL, NULL }
};

static struct sockcheck_list *
sockcheck_list_alloc(unsigned int size)
{
    struct sockcheck_list *list = malloc(sizeof(*list));
    list->used = 0;
    list->refs = 1;
    list->size = size;
    list->list = malloc(list->size*sizeof(list->list[0]));
    return list;
}

static void
sockcheck_list_append(struct sockcheck_list *list, struct sockcheck_state *new_item)
{
    if (list->used == list->size) {
	list->size <<= 1;
	list->list = realloc(list->list, list->size*sizeof(list->list[0]));
    }
    list->list[list->used++] = new_item;
}

static struct sockcheck_list *
sockcheck_list_clone(struct sockcheck_list *old_list)
{
    struct sockcheck_list *new_list = malloc(sizeof(*new_list));
    new_list->used = old_list->used;
    new_list->refs = 1;
    new_list->size = old_list->size;
    new_list->list = malloc(new_list->size*sizeof(new_list->list[0]));
    memcpy(new_list->list, old_list->list, new_list->used*sizeof(new_list->list[0]));
    return new_list;
}

static void
sockcheck_list_unref(struct sockcheck_list *list)
{
    if (!list || --list->refs > 0) return;
    free(list->list);
    free(list);
}

static void
sockcheck_issue_gline(sockcheck_cache_info sci)
{
    char addr[IRC_NTOP_MAX_SIZE + 2] = {'*', '@', '\0'};
    irc_ntop(addr + 2, sizeof(addr) - 2, &sci->addr);
    log_module(PC_LOG, LOG_INFO, "Issuing gline for client at %s: %s", addr + 2, sci->reason);
    gline_add("ProxyCheck", addr, sockcheck_conf.gline_duration, sci->reason, now, 1);
}

static struct sockcheck_client *
sockcheck_alloc_client(sockcheck_cache_info sci)
{
    struct sockcheck_client *client;
    client = calloc(1, sizeof(*client));
    client->tests = tests;
    client->tests->refs++;
    client->addr = sci;
    client->read_size = sockcheck_conf.max_read;
    client->read = malloc(client->read_size);
    client->resp_state = malloc(max_responses * sizeof(client->resp_state[0]));
    return client;
}

static void
sockcheck_free_client(struct sockcheck_client *client)
{
    if (SOCKCHECK_DEBUG) {
        log_module(PC_LOG, LOG_INFO, "Goodbye %s (%p)!  I set you free!", client->addr->hostname, client);
    }
    verify(client);
    if (client->fd)
        ioset_close(client->fd->fd, 1);
    sockcheck_list_unref(client->tests);
    free(client->read);
    free(client->resp_state);
    free(client);
}

static void sockcheck_start_client(unsigned int idx);
static void sockcheck_begin_test(struct sockcheck_client *client);
static void sockcheck_advance(struct sockcheck_client *client, unsigned int next_state);

static void
sockcheck_timeout_client(void *data)
{
    struct sockcheck_client *client = data;
    if (SOCKCHECK_DEBUG) {
        log_module(PC_LOG, LOG_INFO, "Client %s timed out.", client->addr->hostname);
    }
    verify(client);
    sockcheck_advance(client, client->state->responses.used-1);
}

static void
sockcheck_print_client(const struct sockcheck_client *client)
{
    static const char *decs[] = {"CHECKING", "ACCEPT", "REJECT"};
    log_module(PC_LOG, LOG_INFO, "client %p: { addr = %p { decision = %s; last_touched = "FMT_TIME_T"; reason = %s; hostname = \"%s\" }; "
        "test_index = %d; state = %p { port = %d; type = %s; template = \"%s\"; ... }; "
        "fd = %p(%d); read = %p; read_size = %d; read_used = %d; read_pos = %d; }",
        client, client->addr, decs[client->addr->decision], client->addr->last_touched,
        client->addr->reason, client->addr->hostname,
        client->test_index, client->state,
        (client->state ? client->state->port : 0),
        (client->state ? decs[client->state->type] : "N/A"),
        (client->state ? client->state->template : "N/A"),
        client->fd, (client->fd ? client->fd->fd : 0),
        client->read, client->read_size, client->read_used, client->read_pos);
}

static char hexvals[256] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 16 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 32 */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0, /* 48 */
    0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 64 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 80 */
    0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 96 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  /* 112 */
};

static void
expand_var(const struct sockcheck_client *client, char var, char **p_expansion, unsigned int *p_exp_length)
{
    extern struct cManagerNode cManager;
    const char *expansion;
    unsigned int exp_length;
#ifndef __SOLARIS__
    u_int32_t exp4;
    u_int16_t exp2;
#else
    uint32_t exp4;
    uint16_t exp2;
#endif
    /* expand variable */
    switch (var) {
    case 'c':
        expansion = client->addr->hostname;
        exp_length = strlen(expansion);
        break;
    case 'i':
	exp4 = client->addr->addr.in6_32[3];
	exp_length = sizeof(exp4);
	expansion = (char*)&exp4;
	break;
    case 'p':
	exp2 = htons(client->state->port);
	exp_length = sizeof(exp2);
	expansion = (char*)&exp2;
	break;
    case 'u':
	expansion = cManager.uplink->host;
	exp_length = strlen(expansion);
	break;
    default:
        log_module(PC_LOG, LOG_WARNING, "Request to expand unknown sockcheck variable $%c, using empty expansion.", var);
	expansion = "";
	exp_length = 0;
    }
    if (p_expansion) {
	*p_expansion = malloc(exp_length);
	memcpy(*p_expansion, expansion, exp_length);
    }
    if (p_exp_length) {
	*p_exp_length = exp_length;
    }
}

static int
sockcheck_check_template(const char *template, int is_input)
{
    unsigned int nn;
    if (is_input && !strcmp(template, "other")) return 1;
    for (nn=0; template[nn]; nn += 2) {
        switch (template[nn]) {
        case '=':
            if (!template[nn+1]) {
                log_module(MAIN_LOG, LOG_ERROR, "ProxyCheck template %s had = at end of template; needs a second character.", template);
                return 0;
            }
            break;
        case '$':
            switch (template[nn+1]) {
            case 'c': case 'i': case 'p': case 'u': break;
            default:
                log_module(MAIN_LOG, LOG_ERROR, "ProxyCheck template %s refers to unknown variable %c (pos %d).", template, template[nn+1], nn);
                return 0;
            }
            break;
        case '.':
            if (!is_input) {
                log_module(MAIN_LOG, LOG_ERROR, "ProxyCheck template %s: . is only valid in input templates.", template);
                return 0;
            }
            if (template[nn+1] != '.') {
                log_module(MAIN_LOG, LOG_ERROR, "ProxyCheck template %s expects .. to come in twos (pos %d).", template, nn);
                return 0;
            }
            break;
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
            if (!hexvals[(unsigned char)template[nn+1]] && (template[nn+1] != '0')) {
                log_module(MAIN_LOG, LOG_ERROR, "ProxyCheck template %s expects hex characters to come in twos (pos %d).", template, nn);
                return 0;
            }
            break;
        default:
            log_module(MAIN_LOG, LOG_ERROR, "ProxyCheck template %s: unrecognized character '%c' (pos %d).", template, template[nn], nn);
            return 0;
        }
    }
    return 1;
}

static void
sockcheck_elaborate_state(struct sockcheck_client *client)
{
    const char *template;
    unsigned int nn;

    for (template = client->state->template, nn = 0; template[nn]; nn += 2) {
        switch (template[nn]) {
        case '=': ioset_write(client->fd, template+nn+1, 1); break;
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': {
            char ch = hexvals[(unsigned char)template[nn]] << 4
                | hexvals[(unsigned char)template[nn+1]];
            ioset_write(client->fd, &ch, 1);
            break;
        }
        case '$': {
            char *expansion;
            unsigned int exp_length;
            expand_var(client, template[nn+1], &expansion, &exp_length);
            ioset_write(client->fd, expansion, exp_length);
            free(expansion);
            break;
        }
        }
    }
    for (nn=0; nn<client->state->responses.used; nn++) {
        /* Set their resp_state to the start of the response. */
	client->resp_state[nn] = client->state->responses.list[nn]->template;
        /* If it doesn't require reading, take it now. */
        if (client->resp_state[nn] && !*client->resp_state[nn]) {
            if (SOCKCHECK_DEBUG) {
                log_module(PC_LOG, LOG_INFO, "Skipping straight to easy option %d for %p.", nn, client);
            }
            sockcheck_advance(client, nn);
            return;
        }
    }
    timeq_add(now + client->state->timeout, sockcheck_timeout_client, client);
    if (SOCKCHECK_DEBUG) {
        log_module(PC_LOG, LOG_INFO, "Elaborated state for %s:", client->addr->hostname);
        sockcheck_print_client(client);
    }
}

static void
sockcheck_decide(struct sockcheck_client *client, enum sockcheck_decision decision)
{
    unsigned int n;

    checked_ip_count++;
    client->addr->decision = decision;
    client->addr->last_touched = now;
    switch (decision) {
    case ACCEPT:
	/* do nothing */
        if (SOCKCHECK_DEBUG) {
            log_module(PC_LOG, LOG_INFO, "Proxy check passed for client at %s.", client->addr->hostname);
        }
        break;
    case REJECT:
	client->addr->reason = client->state->template;
	proxies_detected++;
	sockcheck_issue_gline(client->addr);
        if (SOCKCHECK_DEBUG) {
            log_module(PC_LOG, LOG_INFO, "Proxy check rejects client at %s (%s)", client->addr->hostname, client->addr->reason);
        }
	/* Don't compare test_index != 0 directly, because somebody
	 * else may have reordered the tests already. */
	if (client->tests->list[client->test_index] != tests->list[0]) {
	    struct sockcheck_list *new_tests = sockcheck_list_clone(tests);
	    struct sockcheck_state *new_first = client->tests->list[client->test_index];
            for (n=0; (n<tests->used) && (tests->list[n] != new_first); n++) ;
            for (; n>0; n--) new_tests->list[n] = new_tests->list[n-1];
	    new_tests->list[0] = new_first;
	    sockcheck_list_unref(tests);
	    tests = new_tests;
	}
        break;
    default:
	log_module(PC_LOG, LOG_ERROR, "BUG: sockcheck_decide(\"%s\", %d): unrecognized decision.", client->addr->hostname, decision);
    }
    n = client->client_index;
    sockcheck_free_client(client);
    if ((--sockcheck_num_clients < sockcheck_conf.max_clients)
        && (pending_sci_list.used > 0)) {
        sockcheck_start_client(n);
    } else {
        client_list[n] = 0;
    }
}

static void
sockcheck_advance(struct sockcheck_client *client, unsigned int next_state)
{
    struct sockcheck_state *ns;

    verify(client);
    timeq_del(0, sockcheck_timeout_client, client, TIMEQ_IGNORE_WHEN);
    if (SOCKCHECK_DEBUG) {
        unsigned int n, m;
        char buffer[201];
        static const char *hexmap = "0123456789ABCDEF";
	log_module(PC_LOG, LOG_INFO, "sockcheck_advance(%s) following response %d (type %d) of %d.", client->addr->hostname, next_state, client->state->responses.list[next_state]->next->type, client->state->responses.used);
	for (n=0; n<client->read_used; n++) {
	    for (m=0; (m<(sizeof(buffer)-1)>>1) && ((n+m) < client->read_used); m++) {
		buffer[m << 1] = hexmap[client->read[n+m] >> 4];
		buffer[m << 1 | 1] = hexmap[client->read[n+m] & 15];
	    }
	    buffer[m<<1] = 0;
	    log_module(PC_LOG, LOG_INFO, " .. read data: %s", buffer);
	    n += m;
	}
        sockcheck_print_client(client);
    }

    ns = client->state = client->state->responses.list[next_state]->next;
    switch (ns->type) {
    case CHECKING:
        sockcheck_elaborate_state(client);
        break;
    case REJECT:
        sockcheck_decide(client, REJECT);
        break;
    case ACCEPT:
        if (++client->test_rep < client->tests->list[client->test_index]->reps) {
            sockcheck_begin_test(client);
        } else if (++client->test_index < client->tests->used) {
            client->test_rep = 0;
            sockcheck_begin_test(client);
        } else {
            sockcheck_decide(client, ACCEPT);
        }
        break;
    default:
        log_module(PC_LOG, LOG_ERROR, "BUG: unknown next-state type %d (after %p).", ns->type, client->state);
        break;
    }
}

static void
sockcheck_readable(struct io_fd *fd)
{
    /* read what we can from the fd */
    struct sockcheck_client *client = fd->data;
    unsigned int nn;
    int res;

    verify(client);
    res = read(fd->fd, client->read + client->read_used, client->read_size - client->read_used);
    if (res < 0) {
        switch (res = errno) {
        default:
	    log_module(PC_LOG, LOG_ERROR, "BUG: sockcheck_readable(%d/%s): read() returned errno %d (%s)", fd->fd, client->addr->hostname, errno, strerror(errno));
        case EAGAIN:
            return;
        case ECONNRESET:
            sockcheck_advance(client, client->state->responses.used - 1);
            return;
	}
    } else if (res == 0) {
        sockcheck_advance(client, client->state->responses.used - 1);
        return;
    } else {
	client->read_used += res;
    }
    if (SOCKCHECK_DEBUG) {
        unsigned int n, m;
        char buffer[201];
        static const char *hexmap = "0123456789ABCDEF";
	for (n=0; n<client->read_used; n++) {
	    for (m=0; (m<(sizeof(buffer)-1)>>1) && ((n+m) < client->read_used); m++) {
		buffer[m << 1] = hexmap[client->read[n+m] >> 4];
		buffer[m << 1 | 1] = hexmap[client->read[n+m] & 15];
	    }
	    buffer[m<<1] = 0;
	    log_module(PC_LOG, LOG_INFO, "read %d bytes data: %s", client->read_used, buffer);
	    n += m;
	}
    }

    /* See if what's been read matches any of the expected responses */
    while (client->read_pos < client->read_used) {
        unsigned int last_pos = client->read_pos;
	char bleh;
	const char *resp_state;

	for (nn=0; nn<(client->state->responses.used-1); nn++) {
            char *expected;
            unsigned int exp_length = 1, free_exp = 0;
	    /* compare against possible target */
	    resp_state = client->resp_state[nn];
	    if (resp_state == NULL) continue;
	    switch (*resp_state) {
	    case '=': 
                bleh = resp_state[1];
                expected = &bleh;
                break;
	    case '.':
                /* any character passes */
                client->read_pos++;
                exp_length = 0;
                break;
	    case '0': case '1': case '2': case '3': case '4':
	    case '5': case '6': case '7': case '8': case '9':
	    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
	    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
		bleh = hexvals[(unsigned char)resp_state[0]] << 4
                    | hexvals[(unsigned char)resp_state[1]];
                expected = &bleh;
		break;
	    case '$':
		expand_var(client, resp_state[1], &expected, &exp_length);
                free_exp = 1;
                break;
            }
            if (client->read_pos+exp_length <= client->read_used) {
                if (exp_length && memcmp(client->read+client->read_pos, expected, exp_length)) {
                    resp_state = NULL;
                } else {
                    client->read_pos += exp_length;
                }
            } else {
                /* can't check the variable yet, so come back later */
                resp_state -= 2;
            }
            if (free_exp) free(expected);
	    if (resp_state) {
		client->resp_state[nn] = resp_state = resp_state + 2;
		if (!*resp_state) {
                    sockcheck_advance(client, nn);
		    return;
		}
	    } else {
		client->resp_state[nn] = NULL;
	    }
	}
        if (last_pos == client->read_pos) break;
    }

    /* nothing seemed to match.  what now? */
    if (client->read_used >= client->read_size) {
	/* we got more data than we expected to get .. don't read any more */
        if (SOCKCHECK_DEBUG) {
            log_module(PC_LOG, LOG_INFO, "Buffer filled (unmatched) for client %s", client->addr->hostname);
        }
        sockcheck_advance(client, client->state->responses.used-1);
	return;
    }
}

static void
sockcheck_connected(struct io_fd *fd, int rc)
{
    struct sockcheck_client *client = fd->data;
    verify(client);
    client->fd = fd;
    switch (rc) {
    default:
        log_module(PC_LOG, LOG_ERROR, "BUG: connect() got error %d (%s) for client at %s.", rc, strerror(rc), client->addr->hostname);
    case EHOSTUNREACH:
    case ECONNREFUSED:
    case ETIMEDOUT:
        if (SOCKCHECK_DEBUG) {
            log_module(PC_LOG, LOG_INFO, "Client %s gave us errno %d (%s)", client->addr->hostname, rc, strerror(rc));
        }
        sockcheck_advance(client, client->state->responses.used-1);
        return;
    case 0: break;
    }
    fd->wants_reads = 1;
    if (SOCKCHECK_DEBUG) {
        log_module(PC_LOG, LOG_INFO, "Connected: to %s port %d.", client->addr->hostname, client->state->port);
    }
    sockcheck_elaborate_state(client);
}

static void
sockcheck_begin_test(struct sockcheck_client *client)
{
    struct io_fd *io_fd;

    verify(client);
    if (client->fd) {
        ioset_close(client->fd->fd, 1);
        client->fd = NULL;
    }
    do {
        client->state = client->tests->list[client->test_index];
        client->read_pos = 0;
        client->read_used = 0;
        client->fd = io_fd = ioset_connect(sockcheck_conf.local_addr, sockcheck_conf.local_addr_len, client->addr->hostname, client->state->port, 0, client, sockcheck_connected);
        if (!io_fd) {
            client->test_index++;
            continue;
        }
        io_fd->readable_cb = sockcheck_readable;
        timeq_add(now + client->state->timeout, sockcheck_timeout_client, client);
        if (SOCKCHECK_DEBUG) {
            log_module(PC_LOG, LOG_INFO, "Starting proxy check on %s:%d (test %d) with fd %d (%p).", client->addr->hostname, client->state->port, client->test_index, io_fd->fd, io_fd);
        }
        return;
    } while (client->test_index < client->tests->used);
    /* Ran out of tests to run; accept this client. */
    sockcheck_decide(client, ACCEPT);
}

static void
sockcheck_start_client(unsigned int idx)
{
    sockcheck_cache_info sci;
    struct sockcheck_client *client;

    if (pending_sci_list.used == 0) return;
    if (!(sci = pending_sci_list.list[0])) {
        log_module(PC_LOG, LOG_ERROR, "BUG: sockcheck_start_client(%d) found null pointer in pending_sci_list.", idx);
        return;
    }
    memmove(pending_sci_list.list, pending_sci_list.list+1,
	    (--pending_sci_list.used)*sizeof(pending_sci_list.list[0]));
    sockcheck_num_clients++;
    if (!tests) return;
    client = client_list[idx] = sockcheck_alloc_client(sci);
    log_module(PC_LOG, LOG_INFO, "Proxy-checking client at %s as client %d (%p) of %d.", sci->hostname, idx, client, sockcheck_num_clients);
    client->test_rep = 0;
    client->client_index = idx;
    sockcheck_begin_test(client);
}

void
sockcheck_queue_address(irc_in_addr_t addr)
{
    sockcheck_cache_info sci;
    const char *ipstr = irc_ntoa(&addr);

    sci = dict_find(checked_ip_dict, ipstr, NULL);
    if (sci) {
        verify(sci);
        switch (sci->decision) {
        case CHECKING:
            /* We are already checking this host. */
            return;
        case ACCEPT:
            if ((sci->last_touched + sockcheck_conf.max_cache_age) >= (unsigned)now) return;
            break;
        case REJECT:
            if ((sci->last_touched + sockcheck_conf.gline_duration) >= (unsigned)now) {
                sockcheck_issue_gline(sci);
                return;
            }
            break;
        }
        dict_remove(checked_ip_dict, sci->hostname);
    }
    sci = calloc(1, sizeof(*sci));
    sci->decision = CHECKING;
    sci->last_touched = now;
    sci->reason = NULL;
    sci->addr = addr;
    strncpy(sci->hostname, ipstr, sizeof(sci->hostname));
    dict_insert(checked_ip_dict, sci->hostname, sci);
    sci_list_append(&pending_sci_list, sci);
    if (sockcheck_num_clients < sockcheck_conf.max_clients)
        sockcheck_start_client(sockcheck_num_clients);
}

int
sockcheck_uncache_host(const char *name)
{
    sockcheck_cache_info sci;
    if ((sci = dict_find(checked_ip_dict, name, NULL))
        && (sci->decision == CHECKING)) {
        return -1;
    }
    return dict_remove(checked_ip_dict, name);
}

static int
sockcheck_create_response(const char *key, void *data, void *extra)
{
    const char *str, *end;
    struct record_data *rd = data;
    struct sockcheck_state *parent = extra;
    struct sockcheck_response *resp;
    dict_t resps;
    char *templ;

    /* allocate memory and tack it onto parent->responses */
    resp = malloc(sizeof(*resp));
    for (end = key; *end != ':' && *end != 0; end += 2 && end) ;
    templ = malloc(end - key + 1);
    memcpy(templ, key, end - key);
    templ[end - key] = 0;
    resp->template = templ;
    if (!sockcheck_check_template(resp->template, 1)) _exit(1);
    resp->next = malloc(sizeof(*resp->next));
    resp->next->port = parent->port;
    response_list_append(&parent->responses, resp);
    /* now figure out how to create resp->next */
    if ((str = GET_RECORD_QSTRING(rd))) {
	if (!ircncasecmp(str, "reject", 6)) {
	    resp->next->type = REJECT;
	} else if (!ircncasecmp(str, "accept", 6)) {
	    resp->next->type = ACCEPT;
	} else {
	    log_module(PC_LOG, LOG_ERROR, "Error: unknown sockcheck decision `%s', defaulting to accept.", str);
	    resp->next->type = ACCEPT;
	}
	if (str[6]) {
	    resp->next->template = strdup(str+7);
	} else {
	    resp->next->template = strdup("No explanation given");
	}
    } else if ((resps = GET_RECORD_OBJECT(rd))) {
	resp->next->type = CHECKING;
	response_list_init(&resp->next->responses);
	if (*end == ':') {
	    resp->next->template = strdup(end+1);
            if (!sockcheck_check_template(resp->next->template, 0)) _exit(1);
	} else {
	    resp->next->template = strdup("");
	}
	dict_foreach(resps, sockcheck_create_response, resp->next);
    }
    return 0;
}

/* key: PORT:send-pattern, as in keys of sockcheck.conf.example
 * data: recdb record_data containing response
 * extra: struct sockcheck_list* to append test to
 */
static int
sockcheck_create_test(const char *key, void *data, void *extra)
{
    char *end;
    struct record_data *rd;
    dict_t object;
    struct sockcheck_state *new_test;
    unsigned int n;

    rd = data;
    new_test = malloc(sizeof(*new_test));
    new_test->template = NULL;
    new_test->reps = 1;
    new_test->port = strtoul(key, &end, 0);
    new_test->timeout = 5;
    new_test->type = CHECKING;
    response_list_init(&new_test->responses);
    if (!(object = GET_RECORD_OBJECT(rd))) {
	log_module(PC_LOG, LOG_ERROR, "Error: misformed sockcheck test `%s', skipping it.", key);
        free(new_test);
        return 1;
    }
    while (*end) {
        switch (*end) {
        case '@': new_test->timeout = strtoul(end+1, &end, 0); break;
        case '*': new_test->reps = strtoul(end+1, &end, 0); break;
        case ':':
            new_test->template = strdup(end+1);
            end += strlen(end);
            if (!sockcheck_check_template(new_test->template, 0)) _exit(1);
            break;
        default:
            log_module(PC_LOG, LOG_ERROR, "Error: misformed sockcheck test `%s', skipping it.", key);
            free(new_test);
            return 1;
        }
    }
    if (!new_test->template) {
	log_module(PC_LOG, LOG_ERROR, "Error: misformed sockcheck test `%s', skipping it.", key);
	free(new_test);
	return 1;
    }
    dict_foreach(object, sockcheck_create_response, new_test);
    /* If none of the responses have template "other", create a
     * default response that goes to accept. */
    for (n=0; n<new_test->responses.used; n++) {
	if (!strcmp(new_test->responses.list[n]->template, "other")) break;
    }
    if (n == new_test->responses.used) {
	rd = alloc_record_data_qstring("accept");
	sockcheck_create_response("other", rd, new_test);
	free_record_data(rd);
    } else if (n != (new_test->responses.used - 1)) {
	struct sockcheck_response *tmp;
	/* switch the response for "other" to the end */
	tmp = new_test->responses.list[new_test->responses.used - 1];
	new_test->responses.list[new_test->responses.used - 1] = new_test->responses.list[n];
	new_test->responses.list[n] = tmp;
    }
    if (new_test->responses.used > max_responses) {
	max_responses = new_test->responses.used;
    }
    sockcheck_list_append(extra, new_test);
    return 0;
}

static void
sockcheck_read_tests(void)
{
    dict_t test_db;
    struct sockcheck_list *new_tests;
    test_db = parse_database(SOCKCHECK_TEST_DB);
    if (!test_db)
	return;
    if (dict_size(test_db) > 0) {
	new_tests = sockcheck_list_alloc(dict_size(test_db));
	dict_foreach(test_db, sockcheck_create_test, new_tests);
	if (tests) sockcheck_list_unref(tests);
	tests = new_tests;
    } else {
	log_module(PC_LOG, LOG_ERROR, "%s was empty - disabling sockcheck.", SOCKCHECK_TEST_DB);
    }
    free_database(test_db);
}

void
sockcheck_free_state(struct sockcheck_state *state)
{
    unsigned int n;
    if (state->type == CHECKING) {
	for (n=0; n<state->responses.used; n++) {
	    free((char*)state->responses.list[n]->template);
	    sockcheck_free_state(state->responses.list[n]->next);
	    free(state->responses.list[n]);
	}
	response_list_clean(&state->responses);
    }
    free((char*)state->template);
    free(state);
}

const char *
sockcheck_add_test(const char *desc)
{
    struct sockcheck_list *new_tests;
    const char *reason;
    char *name;
    struct record_data *rd;

    if ((reason = parse_record(desc, &name, &rd)))
        return reason;
    new_tests = sockcheck_list_clone(tests);
    if (sockcheck_create_test(name, rd, new_tests)) {
	sockcheck_list_unref(new_tests);
	return "Sockcheck test parse error";
    }
    sockcheck_list_unref(tests);
    tests = new_tests;
    return 0;
}

static void
sockcheck_shutdown(void)
{
    unsigned int n;

    if (client_list) {
        for (n=0; n<sockcheck_conf.max_clients; n++) {
            if (client_list[n])
                sockcheck_free_client(client_list[n]);
        }
        free(client_list);
    }
    sockcheck_num_clients = 0;
    dict_delete(checked_ip_dict);
    sci_list_clean(&pending_sci_list);
    if (tests)
	for (n=0; n<tests->used; n++)
	    sockcheck_free_state(tests->list[n]);
    sockcheck_list_unref(tests);
    if (sockcheck_conf.local_addr) {
	free(sockcheck_conf.local_addr);
	sockcheck_conf.local_addr_len = 0;
    }
}

static void
sockcheck_clean_cache(UNUSED_ARG(void *data))
{
    dict_t curr_clients;
    dict_iterator_t it, next;
    sockcheck_cache_info sci;
    unsigned int nn;
    int max_age;

    if (SOCKCHECK_DEBUG) {
        struct string_buffer sb;
        string_buffer_init(&sb);
        /* Remember which clients we're still checking; we're not allowed to remove them. */
        for (curr_clients = dict_new(), nn=0; nn < sockcheck_conf.max_clients; nn++) {
            if (!client_list[nn])
                continue;
            dict_insert(curr_clients, client_list[nn]->addr->hostname, client_list[nn]);
            string_buffer_append(&sb, ' ');
            string_buffer_append_string(&sb, client_list[nn]->addr->hostname);
        }
        string_buffer_append(&sb, '\0');
        log_module(PC_LOG, LOG_INFO, "Cleaning sockcheck cache at "FMT_TIME_T"; current clients: %s.", now, sb.list);
        string_buffer_clean(&sb);
    } else {
        for (curr_clients = dict_new(), nn=0; nn < sockcheck_conf.max_clients; nn++) {
            if (!client_list[nn])
                continue;
            dict_insert(curr_clients, client_list[nn]->addr->hostname, client_list[nn]);
        }
    }

    for (it=dict_first(checked_ip_dict); it; it=next) {
        next = iter_next(it);
        sci = iter_data(it);
        max_age = (sci->decision == REJECT) ? sockcheck_conf.gline_duration : sockcheck_conf.max_cache_age;
        if (((sci->last_touched + max_age) < now)
            && !dict_find(curr_clients, sci->hostname, NULL)) {
            if (SOCKCHECK_DEBUG) {
                log_module(PC_LOG, LOG_INFO, " .. nuking %s (last touched "FMT_TIME_T").", sci->hostname, sci->last_touched);
            }
            dict_remove(checked_ip_dict, sci->hostname);
        }
    }
    dict_delete(curr_clients);
    timeq_add(now+sockcheck_conf.max_cache_age, sockcheck_clean_cache, 0);
}

static MODCMD_FUNC(cmd_defproxy)
{
    const char *reason;

    if ((reason = sockcheck_add_test(unsplit_string(argv+1, argc-1, NULL)))) {
	reply("PCMSG_PROXY_DEFINITION_FAILED", reason);
	return 0;
    }
    reply("PCMSG_PROXY_DEFINITION_SUCCEEDED");
    return 1;
}

static MODCMD_FUNC(cmd_hostscan)
{
    unsigned int n;
    irc_in_addr_t ipaddr;
    char hnamebuf[IRC_NTOP_MAX_SIZE];

    for (n=1; n<argc; n++) {
	struct userNode *un = GetUserH(argv[n]);

        if (un) {
            if (!irc_in_addr_is_valid(un->ip)
                || irc_in_addr_is_loopback(un->ip)) {
                reply("PCMSG_UNSCANNABLE_IP", un->nick);
            } else {
                irc_ntop(hnamebuf, sizeof(hnamebuf), &un->ip);
                sockcheck_queue_address(un->ip);
                reply("PCMSG_ADDRESS_QUEUED", hnamebuf);
            }
        } else {
            char *scanhost = argv[n];
            if (!irc_pton(&ipaddr, NULL, scanhost)) {
                sockcheck_queue_address(ipaddr);
                reply("PCMSG_ADDRESS_QUEUED", scanhost);
            } else {
                reply("PCMSG_ADDRESS_UNRESOLVED", scanhost);
            }
        }
    }
    return 1;
}

static MODCMD_FUNC(cmd_clearhost)
{
    unsigned int n;
    char hnamebuf[IRC_NTOP_MAX_SIZE];

    for (n=1; n<argc; n++) {
        struct userNode *un = GetUserH(argv[n]);
        const char *scanhost;

        if (un) {
            irc_ntop(hnamebuf, sizeof(hnamebuf), &un->ip);
            scanhost = hnamebuf;
        } else {
            scanhost = argv[n];
        }
        switch (sockcheck_uncache_host(scanhost)) {
        case -1:
	    reply("PCMSG_CHECKING_ADDRESS", scanhost);
            break;
        case 0:
	    reply("PCMSG_NOT_REMOVED_FROM_CACHE", scanhost);
            break;
        default:
            reply("PCMSG_REMOVED_FROM_CACHE", scanhost);
            break;
        }
    }
    return 1;
}

static MODCMD_FUNC(cmd_stats_proxycheck)
{
    if (argc > 1) {
        const char *hostname = argv[1];
        char elapse_buf[INTERVALLEN];
        const char *msg;

        sockcheck_cache_info sci = dict_find(checked_ip_dict, hostname, NULL);
        if (!sci) {
            reply("PCMSG_NOT_CACHED", hostname);
            return 0;
        }
        intervalString(elapse_buf, now - sci->last_touched, user->handle_info);
        switch (sci->decision) {
        case CHECKING: msg = "PCMSG_STATUS_CHECKING"; break;
        case ACCEPT: msg = "PCMSG_STATUS_ACCEPTED"; break;
        case REJECT: msg = "PCMSG_STATUS_REJECTED"; break;
        default: msg = "PCMSG_STATUS_UNKNOWN"; break;
        }
        reply(msg, sci->hostname, elapse_buf, sci->reason);
        return 1;
    } else {
        reply("PCMSG_STATISTICS", checked_ip_count, proxies_detected, sockcheck_num_clients, sockcheck_conf.max_clients, pending_sci_list.used, dict_size(checked_ip_dict), (tests ? tests->used : 0));
        return 1;
    }
}

static int
sockcheck_new_user(struct userNode *user) {
    /* If they have a bum IP, or are bursting in, don't proxy-check or G-line them. */
    if (irc_in_addr_is_valid(user->ip)
        && !irc_in_addr_is_loopback(user->ip)
        && !user->uplink->burst)
        sockcheck_queue_address(user->ip);
    return 0;
}

static void
_sockcheck_init(void)
{
    checked_ip_dict = dict_new();
    dict_set_free_data(checked_ip_dict, free);
    sci_list_init(&pending_sci_list);
    sockcheck_num_clients = 0;
    sockcheck_read_tests();
    timeq_del(0, sockcheck_clean_cache, 0, TIMEQ_IGNORE_WHEN|TIMEQ_IGNORE_DATA);
    client_list = calloc(sockcheck_conf.max_clients, sizeof(client_list[0]));
    timeq_add(now+sockcheck_conf.max_cache_age, sockcheck_clean_cache, 0);
}

static void
sockcheck_read_conf(void)
{
    dict_t my_node;
    struct addrinfo *ai;
    const char *str;

    /* set the defaults here in case the entire record is missing */
    sockcheck_conf.max_clients = 32;
    sockcheck_conf.max_read = 1024;
    sockcheck_conf.gline_duration = 3600;
    sockcheck_conf.max_cache_age = 60;
    if (sockcheck_conf.local_addr) {
        free(sockcheck_conf.local_addr);
        sockcheck_conf.local_addr = NULL;
    }
    /* now try to read from the conf database */
    if ((my_node = conf_get_data("modules/sockcheck", RECDB_OBJECT))) {
	str = database_get_data(my_node, "max_sockets", RECDB_QSTRING);
	if (str) sockcheck_conf.max_clients = strtoul(str, NULL, 0);
	str = database_get_data(my_node, "max_clients", RECDB_QSTRING);
	if (str) sockcheck_conf.max_clients = strtoul(str, NULL, 0);
	str = database_get_data(my_node, "max_read", RECDB_QSTRING);
	if (str) sockcheck_conf.max_read = strtoul(str, NULL, 0);
	str = database_get_data(my_node, "max_cache_age", RECDB_QSTRING);
	if (str) sockcheck_conf.max_cache_age = ParseInterval(str);
        str = database_get_data(my_node, "gline_duration", RECDB_QSTRING);
        if (str) sockcheck_conf.gline_duration = ParseInterval(str);
	str = database_get_data(my_node, "address", RECDB_QSTRING);
        if (!getaddrinfo(str, NULL, NULL, &ai)) {
	    sockcheck_conf.local_addr_len = ai->ai_addrlen;
            sockcheck_conf.local_addr = calloc(1, ai->ai_addrlen);
            memcpy(sockcheck_conf.local_addr, ai->ai_addr, ai->ai_addrlen);
        } else {
            sockcheck_conf.local_addr_len = 0;
            sockcheck_conf.local_addr = NULL;
            log_module(PC_LOG, LOG_ERROR, "Error: Unable to get host named `%s', not checking a specific address.", str);
	}
    }
}

int
sockcheck_init(void)
{
    PC_LOG = log_register_type("ProxyCheck", "file:proxycheck.log");
    conf_register_reload(sockcheck_read_conf);
    reg_exit_func(sockcheck_shutdown);
    _sockcheck_init();
    message_register_table(msgtab);

    sockcheck_module = module_register("ProxyCheck", PC_LOG, "mod-sockcheck.help", NULL);
    modcmd_register(sockcheck_module, "defproxy", cmd_defproxy, 2, 0, "level", "999", NULL);
    modcmd_register(sockcheck_module, "hostscan", cmd_hostscan, 2, 0, "level", "650", NULL);
    modcmd_register(sockcheck_module, "clearhost", cmd_clearhost, 2, 0, "level", "650", NULL);
    modcmd_register(sockcheck_module, "stats proxycheck", cmd_stats_proxycheck, 0, 0, NULL);
    reg_new_user_func(sockcheck_new_user);
    return 1;
}

int
sockcheck_finalize(void)
{
    return 1;
}

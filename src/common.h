/* common.h - Common functions/includes
 * Copyright 2000-2004 srvx Development Team
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

#ifndef COMMON_H
#define COMMON_H

#include "compat.h"
#include "proto.h"

#if !defined(HAVE_LOCALTIME_R) && !defined(__CYGWIN__)
extern struct tm *localtime_r(const time_t *clock, struct tm *res);
#elif defined(__CYGWIN__)
# define localtime_r(clock, res) memcpy(res, localtime(clock), sizeof(struct tm));
#endif

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffffL
#endif
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001L
#endif

#define ArrayLength(x)		(sizeof(x)/sizeof(x[0]))
#define safestrncpy(dest, src, len) do { char *d = (dest); const char *s = (src); size_t l = strlen(s)+1;  if ((len) < l) l = (len); memmove(d, s, l); d[l-1] = 0; } while (0)

#ifdef __GNUC__
#define PRINTF_LIKE(M,N) __attribute__((format (printf, M, N)))
#else
#define PRINTF_LIKE(M,N)
#endif

#if __GNUC__ >= 2
#define UNUSED_ARG(ARG) ARG __attribute__((unused))
#elif defined(S_SPLINT_S)
#define UNUSED_ARG(ARG) /*@unused@*/ ARG
#define const /*@observer@*/ /*@temp@*/
#else
#define UNUSED_ARG(ARG) ARG
#endif

#if defined(WITH_MALLOC_DMALLOC)
#define DMALLOC_FUNC_CHECK 1
#include <string.h>
#include <dmalloc.h>
#elif defined(WITH_MALLOC_MPATROL)
#include <string.h>
#include <mpatrol.h>
#elif defined(WITH_MALLOC_BOEHM_GC)
#if !defined(NDEBUG)
#define GC_DEBUG 1
#endif
#include <stdlib.h>
#include <string.h>
#include <gc/gc.h>
#define malloc(n) GC_MALLOC(n)
#define calloc(m,n) GC_MALLOC((m)*(n))
#define realloc(p,n) GC_REALLOC((p),(n))
#define free(p) GC_FREE(p)
#undef  HAVE_STRDUP
#undef strdup
#endif

extern time_t now;
extern int quit_services;
extern struct log_type *MAIN_LOG;

int create_socket_client(struct uplinkNode *target);
void close_socket(void);

typedef void (*exit_func_t)(void);
void reg_exit_func(exit_func_t handler);
void call_exit_funcs(void);

const char *inttobase64(char *buf, unsigned int v, unsigned int count);
unsigned long base64toint(const char *s, int count);
int split_line(char *line, int irc_colon, int argv_size, char *argv[]);

/* match_ircglobs(oldglob, newglob) returns non-zero if oldglob is a superset of newglob */
#define match_ircglobs !mmatch
int mmatch(const char *glob, const char *newglob);
int match_ircglob(const char *text, const char *glob);
int user_matches_glob(struct userNode *user, const char *glob, int include_nick);

int is_ircmask(const char *text);
int is_gline(const char *text);

char *sanitize_ircmask(char *text);

unsigned long ParseInterval(const char *interval);
unsigned long ParseVolume(const char *volume);
int parse_ipmask(const char *str, struct in_addr *addr, unsigned long *mask);
#define MATCH_IPMASK(test, addr, mask) (((ntohl(test.s_addr) & mask) ^ (ntohl(addr.s_addr) & mask)) == 0)

#define MD5_CRYPT_LENGTH 42
/* buffer[] must be at least MD5_CRYPT_LENGTH bytes long */
const char *cryptpass(const char *pass, char buffer[]);
int checkpass(const char *pass, const char *crypt);

int split_ircmask(char *text, char **nick, char **ident, char **host);
char *unsplit_string(char *set[], unsigned int max, char *dest);

#define DECLARE_LIST(STRUCTNAME,ITEMTYPE) struct STRUCTNAME {\
  unsigned int used, size;\
  ITEMTYPE *list;\
};\
void STRUCTNAME##_init(struct STRUCTNAME *list);\
void STRUCTNAME##_append(struct STRUCTNAME *list, ITEMTYPE new_item);\
int STRUCTNAME##_remove(struct STRUCTNAME *list, ITEMTYPE new_item);\
void STRUCTNAME##_clean(struct STRUCTNAME *list)

#define DEFINE_LIST(STRUCTNAME,ITEMTYPE) \
void STRUCTNAME##_init(struct STRUCTNAME *list) {\
  list->used = 0;\
  list->size = 8;\
  list->list = malloc(list->size*sizeof(list->list[0]));\
}\
void STRUCTNAME##_append(struct STRUCTNAME *list, ITEMTYPE new_item) {\
  if (list->used == list->size) {\
    list->size = list->size ? (list->size << 1) : 4;\
    list->list = realloc(list->list, list->size*sizeof(list->list[0]));\
  }\
  list->list[list->used++] = new_item;\
}\
int STRUCTNAME##_remove(struct STRUCTNAME *list, ITEMTYPE new_item) {\
    unsigned int n, found;\
    for (found=n=0; n<list->used; n++) {\
	if (list->list[n] == new_item) {\
	    memmove(list->list+n, list->list+n+1, (list->used-n-1)*sizeof(list->list[n]));\
	    found = 1;\
	    list->used--;\
	}\
    }\
    return found;\
}\
void STRUCTNAME##_clean(struct STRUCTNAME *list) {\
  list->used = list->size = 0;\
  free(list->list);\
}

/* The longest string that's likely to be produced is "10 minutes, and 10
   seconds." (27 characters) */
#define INTERVALLEN	32

char *intervalString2(char *output, time_t interval, int brief);
#define intervalString(OUTPUT, INTERVAL) intervalString2((OUTPUT), (INTERVAL), 0)
int getipbyname(const char *name, unsigned long *ip);
int set_policer_param(const char *param, void *data, void *extra);
const char *strtab(unsigned int ii);

void tools_init(void);
void tools_cleanup(void);

int irccasecmp(const char *stra, const char *strb);
int ircncasecmp(const char *stra, const char *strb, unsigned int len);
const char *irccasestr(const char *haystack, const char *needle);

DECLARE_LIST(string_buffer, char);
void string_buffer_append_string(struct string_buffer *buf, const char *tail);
void string_buffer_append_substring(struct string_buffer *buf, const char *tail, unsigned int len);
void string_buffer_append_vprintf(struct string_buffer *buf, const char *fmt, va_list args);
void string_buffer_append_printf(struct string_buffer *buf, const char *fmt, ...);
void string_buffer_replace(struct string_buffer *buf, unsigned int from, unsigned int len, const char *repl);

#define enabled_string(string)  (!irccasecmp((string), "on") || !strcmp((string), "1") || !irccasecmp((string), "enabled"))
#define disabled_string(string) (!irccasecmp((string), "off") || !strcmp((string), "0") || !irccasecmp((string), "disabled"))
#define true_string(string)     (!irccasecmp((string), "true") || !strcmp((string), "1") || !irccasecmp((string), "yes"))
#define false_string(string)    (!irccasecmp((string), "false") || !strcmp((string), "0") || !irccasecmp((string), "no"))

#endif /* ifdef COMMON_H */

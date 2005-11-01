/* common.h - Common functions/includes
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
# define DMALLOC_FUNC_CHECK 1
# include <string.h>
# include <dmalloc.h>
#elif defined(WITH_MALLOC_MPATROL)
# include <string.h>
# include <mpatrol.h>
#elif defined(WITH_MALLOC_BOEHM_GC)
# if !defined(NDEBUG)
#  define GC_DEBUG 1
# endif
# include <stdlib.h>
# include <string.h>
# include <gc/gc.h>
# define malloc(n) GC_MALLOC(n)
# define calloc(m,n) GC_MALLOC((m)*(n))
# define realloc(p,n) GC_REALLOC((p),(n))
# define free(p) GC_FREE(p)
# undef  HAVE_STRDUP
# undef strdup
#elif defined(WITH_MALLOC_SRVX)
# undef malloc
# define malloc(n) srvx_malloc(__FILE__, __LINE__, (n))
# undef calloc
# define calloc(m,n) srvx_malloc(__FILE__, __LINE__, (m)*(n))
# undef realloc
# define realloc(p,n) srvx_realloc(__FILE__, __LINE__, (p), (n))
# undef free
# define free(p) srvx_free(__FILE__, __LINE__, (p))
# undef strdup
# define strdup(s) srvx_strdup(__FILE__, __LINE__, (s))
extern void *srvx_malloc(const char *, unsigned int, size_t);
extern void *srvx_realloc(const char *, unsigned int, void *, size_t);
extern char *srvx_strdup(const char *, unsigned int, const char *);
extern void srvx_free(const char *, unsigned int, void *);
# if !defined(NDEBUG)
extern void verify(const void *ptr);
#  define verify(x) verify(x)
# endif
#elif defined(WITH_MALLOC_SLAB)
# define malloc(n) slab_malloc(__FILE__, __LINE__, (n))
# undef calloc
# define calloc(m,n) slab_malloc(__FILE__, __LINE__, (m)*(n))
# undef realloc
# define realloc(p,n) slab_realloc(__FILE__, __LINE__, (p), (n))
# undef free
# define free(p) slab_free(__FILE__, __LINE__, (p))
# undef strdup
# define strdup(s) slab_strdup(__FILE__, __LINE__, (s))
extern void *slab_malloc(const char *, unsigned int, size_t);
extern void *slab_realloc(const char *, unsigned int, void *, size_t);
extern char *slab_strdup(const char *, unsigned int, const char *);
extern void slab_free(const char *, unsigned int, void *);
# if !defined(NDEBUG)
extern void verify(const void *ptr);
#  define verify(x) verify(x)
# endif
#endif

#ifndef verify
# define verify(ptr) (void)(ptr)
#endif

extern time_t now;
extern int quit_services;
extern struct log_type *MAIN_LOG;

typedef union irc_in_addr {
    uint32_t in6_32[4];
    uint16_t in6[8];
    uint8_t in6_8[16];
} irc_in_addr_t;

#define irc_in_addr_is_valid(ADDR) (((ADDR).in6[0] && (ADDR).in6[0] != 65535) \
                                    || (ADDR).in6[1] != (ADDR).in6[0] \
                                    || (ADDR).in6[2] != (ADDR).in6[0] \
                                    || (ADDR).in6[3] != (ADDR).in6[0] \
                                    || (ADDR).in6[4] != (ADDR).in6[0] \
                                    || (ADDR).in6[5] != (ADDR).in6[0] \
                                    || (ADDR).in6[6] != (ADDR).in6[0] \
                                    || (ADDR).in6[7] != (ADDR).in6[0])
#define irc_in_addr_is_ipv4(ADDR) (!(ADDR).in6[0] && !(ADDR).in6[1] \
                                   && !(ADDR).in6[2] && !(ADDR).in6[3] \
                                   && !(ADDR).in6[4] && (ADDR).in6[6] \
                                   && (!(ADDR).in6[5] || (ADDR).in6[5] == 65535))
#define irc_in_addr_is_ipv6(ADDR) !irc_in_addr_is_ipv4(ADDR)
#define irc_in_addr_is_loopback(ADDR) (irc_in_addr_is_ipv4(ADDR) ? (ADDR).in6_8[12] == 127 \
                                       : (ADDR).in6[0] == 0 && (ADDR).in6[1] == 0 \
                                       && (ADDR).in6[2] == 0 && (ADDR).in6[3] == 0 \
                                       && (ADDR).in6[4] == 0 && (ADDR).in6[5] == 0 \
                                       && (ADDR).in6[6] == 0 && (ADDR).in6[7] == 1)
#define IRC_NTOP_MAX_SIZE 40
unsigned int irc_ntop(char *output, unsigned int out_size, const irc_in_addr_t *addr);
#define IRC_NTOP_MASK_MAX_SIZE (IRC_NTOP_MAX_SIZE + 4)
unsigned int irc_ntop_mask(char *output, unsigned int out_size, const irc_in_addr_t *addr, unsigned char bits);
unsigned int irc_pton(irc_in_addr_t *addr, unsigned char *bits, const char *input);
unsigned int irc_check_mask(const irc_in_addr_t *check, const irc_in_addr_t *mask, unsigned char bits);
const char *irc_ntoa(const irc_in_addr_t *addr);

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
#define MATCH_USENICK 1
#define MATCH_VISIBLE 2
int user_matches_glob(struct userNode *user, const char *glob, int flags);

int is_ircmask(const char *text);
int is_gline(const char *text);

char *sanitize_ircmask(char *text);

unsigned long ParseInterval(const char *interval);
unsigned long ParseVolume(const char *volume);

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
  verify(list->list);\
  if (list->used == list->size) {\
    list->size = list->size ? (list->size << 1) : 4;\
    list->list = realloc(list->list, list->size*sizeof(list->list[0]));\
  }\
  list->list[list->used++] = new_item;\
}\
int STRUCTNAME##_remove(struct STRUCTNAME *list, ITEMTYPE new_item) {\
    unsigned int n, found;\
    verify(list->list);\
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
  list->list = NULL;\
}

/* The longest string that is likely to be produced in English is "10
 * minutes, and 10 seconds" (27 characters).  Other languages will
 * vary, so there's plenty of leeway.
 */
#define INTERVALLEN	50

struct handle_info;
char *intervalString(char *output, time_t interval, struct handle_info *hi);
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

/* tools.c - miscellaneous utility functions
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

#include "log.h"
#include "nickserv.h"
#include "recdb.h"

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif
#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif
#ifdef HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif

#define NUMNICKLOG 6
#define NUMNICKBASE (1 << NUMNICKLOG)
#define NUMNICKMASK (NUMNICKBASE - 1)

/* Yes, P10's encoding here is almost-but-not-quite MIME Base64.  Yay
 * for gratuitous incompatibilities. */
static const char convert2y[256] = {
  'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
  'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
  'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
  'w','x','y','z','0','1','2','3','4','5','6','7','8','9','[',']'
};

static const unsigned char convert2n[256] = {
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  52,53,54,55,56,57,58,59,60,61, 0, 0, 0, 0, 0, 0, 
   0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
  15,16,17,18,19,20,21,22,23,24,25,62, 0,63, 0, 0,
   0,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
  41,42,43,44,45,46,47,48,49,50,51, 0, 0, 0, 0, 0
};

unsigned long int
base64toint(const char* s, int count)
{
    unsigned int i = 0;
    while (*s && count) {
        i = (i << NUMNICKLOG) + convert2n[(unsigned char)*s++];
        count--;
    }
    return i;
}

const char* inttobase64(char* buf, unsigned int v, unsigned int count)
{
  buf[count] = '\0';
  while (count > 0) {
      buf[--count] = convert2y[(unsigned char)(v & NUMNICKMASK)];
      v >>= NUMNICKLOG;
  }
  return buf;
}

static char irc_tolower[256];
#undef tolower
#define tolower(X) irc_tolower[(unsigned char)(X)]

int
irccasecmp(const char *stra, const char *strb) {
    while (*stra && (tolower(*stra) == tolower(*strb)))
        stra++, strb++;
    return tolower(*stra) - tolower(*strb);
}

int
ircncasecmp(const char *stra, const char *strb, unsigned int len) {
    len--;
    while (*stra && (tolower(*stra) == tolower(*strb)) && len)
        stra++, strb++, len--;
    return tolower(*stra) - tolower(*strb);
}

const char *
irccasestr(const char *haystack, const char *needle) {
    unsigned int hay_len = strlen(haystack), needle_len = strlen(needle), pos;
    if (hay_len < needle_len)
        return NULL;
    for (pos=0; pos<hay_len+1-needle_len; ++pos) {
        if ((tolower(haystack[pos]) == tolower(*needle))
            && !ircncasecmp(haystack+pos, needle, needle_len))
            return haystack+pos;
    }
    return NULL;
}

int
split_line(char *line, int irc_colon, int argv_size, char *argv[])
{
    int argc = 0;
    int n;
    while (*line && (argc < argv_size)) {
	while (*line == ' ') *line++ = 0;
	if (*line == ':' && irc_colon && argc > 0) {
	    /* the rest is a single parameter */
	    argv[argc++] = line + 1;
	    break;
	}
        if (!*line)
            break;
	argv[argc++] = line;
	if (argc >= argv_size)
            break;
	while (*line != ' ' && *line) line++;
    }
#ifdef NDEBUG
    n = 0;
#else
    for (n=argc; n<argv_size; n++) {
        argv[n] = (char*)0xFEEDBEEF;
    }
#endif
    return argc;
}

/* This is ircu's mmatch() function, from match.c. */
int mmatch(const char *old_mask, const char *new_mask)
{
  register const char *m = old_mask;
  register const char *n = new_mask;
  const char *ma = m;
  const char *na = n;
  int wild = 0;
  int mq = 0, nq = 0;

  while (1)
  {
    if (*m == '*')
    {
      while (*m == '*')
	m++;
      wild = 1;
      ma = m;
      na = n;
    }

    if (!*m)
    {
      if (!*n)
	return 0;
      for (m--; (m > old_mask) && (*m == '?'); m--)
	;
      if ((*m == '*') && (m > old_mask) && (m[-1] != '\\'))
	return 0;
      if (!wild)
	return 1;
      m = ma;

      /* Added to `mmatch' : Because '\?' and '\*' now is one character: */
      if ((*na == '\\') && ((na[1] == '*') || (na[1] == '?')))
	++na;

      n = ++na;
    }
    else if (!*n)
    {
      while (*m == '*')
	m++;
      return (*m != 0);
    }
    if ((*m == '\\') && ((m[1] == '*') || (m[1] == '?')))
    {
      m++;
      mq = 1;
    }
    else
      mq = 0;

    /* Added to `mmatch' : Because '\?' and '\*' now is one character: */
    if ((*n == '\\') && ((n[1] == '*') || (n[1] == '?')))
    {
      n++;
      nq = 1;
    }
    else
      nq = 0;

/*
 * This `if' has been changed compared to match() to do the following:
 * Match when:
 *   old (m)         new (n)         boolean expression
 *    *               any             (*m == '*' && !mq) ||
 *    ?               any except '*'  (*m == '?' && !mq && (*n != '*' || nq)) ||
 * any except * or ?  same as m       (!((*m == '*' || *m == '?') && !mq) &&
 *                                      toLower(*m) == toLower(*n) &&
 *                                        !((mq && !nq) || (!mq && nq)))
 *
 * Here `any' also includes \* and \? !
 *
 * After reworking the boolean expressions, we get:
 * (Optimized to use boolean shortcircuits, with most frequently occuring
 *  cases upfront (which took 2 hours!)).
 */
    if ((*m == '*' && !mq) ||
	((!mq || nq) && tolower(*m) == tolower(*n)) ||
	(*m == '?' && !mq && (*n != '*' || nq)))
    {
      if (*m)
	m++;
      if (*n)
	n++;
    }
    else
    {
      if (!wild)
	return 1;
      m = ma;

      /* Added to `mmatch' : Because '\?' and '\*' now is one character: */
      if ((*na == '\\') && ((na[1] == '*') || (na[1] == '?')))
	++na;

      n = ++na;
    }
  }
}

int
match_ircglob(const char *text, const char *glob)
{
    unsigned int star_p, q_cnt;
    while (1) {
	switch (*glob) {
	case 0:
	    return !*text;
        case '\\':
            glob++;
            /* intentionally not tolower(...) so people can force
             * capitalization, or we can overload \ in the future */
            if (*text++ != *glob++)
                return 0;
            break;
	case '*':
        case '?':
            star_p = q_cnt = 0;
            do {
                if (*glob == '*')
                    star_p = 1;
                else if (*glob == '?')
                    q_cnt++;
                else
                    break;
                glob++;
            } while (1);
            while (q_cnt) {
                if (!*text++)
                    return 0;
                q_cnt--;
            }
            if (star_p) {
                /* if this is the last glob character, it will match any text */
                if (!*glob)
                    return 1;
                /* Thanks to the loop above, we know that the next
                 * character is a normal character.  So just look for
                 * the right character.
                 */
                for (; *text; text++) {
                    if ((tolower(*text) == tolower(*glob))
                        && match_ircglob(text+1, glob+1)) {
                        return 1;
                    }
                }
                return 0;
            }
            /* if !star_p, fall through to normal character case,
             * first checking to see if ?s carried us to the end */
            if (!*glob && !*text)
                return 1;
	default:
	    if (!*text)
                return 0;
	    while (*text && *glob && *glob != '*' && *glob != '?' && *glob != '\\') {
		if (tolower(*text++) != tolower(*glob++))
                    return 0;
	    }
	}
    }
}

extern const char *hidden_host_suffix;

int
user_matches_glob(struct userNode *user, const char *orig_glob, int include_nick)
{
    char *glob, *marker;

    /* Make a writable copy of the glob */
    glob = alloca(strlen(orig_glob)+1);
    strcpy(glob, orig_glob);
    /* Check the nick, if it's present */
    if (include_nick) {
        if (!(marker = strchr(glob, '!'))) {
            log_module(MAIN_LOG, LOG_ERROR, "user_matches_glob(\"%s\", \"%s\", %d) called, and glob doesn't include a '!'", user->nick, orig_glob, include_nick);
            return 0;
        }
        *marker = 0;
        if (!match_ircglob(user->nick, glob)) return 0;
        glob = marker + 1;
    }
    /* Check the ident */
    if (!(marker = strchr(glob, '@'))) {
        log_module(MAIN_LOG, LOG_ERROR, "user_matches_glob(\"%s\", \"%s\", %d) called, and glob doesn't include an '@'", user->nick, orig_glob, include_nick);
        return 0;
    }
    *marker = 0;
    if (!match_ircglob(user->ident, glob))
        return 0;
    glob = marker + 1;
    /* Now check the host part */
    if (isdigit(*glob) && !glob[strspn(glob, "0123456789./*?")]) {
        /* Looks like an IP-based mask */
        return match_ircglob(inet_ntoa(user->ip), glob);
    } else {
        /* The host part of the mask isn't IP-based */
        if (hidden_host_suffix && user->handle_info) {
            char hidden_host[HOSTLEN+1];
            snprintf(hidden_host, sizeof(hidden_host), "%s.%s", user->handle_info->handle, hidden_host_suffix);
            if (match_ircglob(hidden_host, glob))
                return 1;
        }
        return match_ircglob(user->hostname, glob);
    }
}

int
is_ircmask(const char *text)
{
    while (*text && (isalnum((char)*text) || strchr("-_[]|\\`^{}?*", *text)))
        text++;
    if (*text++ != '!')
        return 0;
    while (*text && *text != '@' && !isspace((char)*text))
        text++;
    if (*text++ != '@')
        return 0;
    while (*text && !isspace((char)*text))
        text++;
    return !*text;
}

int
is_gline(const char *text)
{
    if (*text == '@')
        return 0;
    text += strcspn(text, "@!% \t\r\n");
    if (*text++ != '@')
        return 0;
    if (!*text)
        return 0;
    while (*text && (isalnum((char)*text) || strchr(".-?*", *text)))
        text++;
    return !*text;
}

int
split_ircmask(char *text, char **nick, char **ident, char **host)
{
    char *start;

    start = text;
    while (isalnum((char)*text) || strchr("=[]\\`^{}?*", *text))
        text++;
    if (*text != '!' || ((text - start) > NICKLEN))
        return 0;
    *text = 0;
    if (nick)
        *nick = start;

    start = ++text;
    while (*text && *text != '@' && !isspace((char)*text))
        text++;
    if (*text != '@' || ((text - start) > USERLEN))
        return 0;
    *text = 0;
    if (ident)
        *ident = start;
    
    start = ++text;
    while (*text && (isalnum((char)*text) || strchr(".-?*", *text)))
        text++;
    if (host)
        *host = start;
    return !*text && ((text - start) <= HOSTLEN) && nick && ident && host;
}

char *
sanitize_ircmask(char *input)
{
    unsigned int length, flag;
    char *mask, *start, *output;

    /* Sanitize everything in place; input *must* be a valid
       hostmask. */
    output = input;
    flag = 0;

    /* The nick is truncated at the end. */
    length = 0;
    mask = input;
    while(*input++ != '!')
    {
	length++;
    }
    if(length > NICKLEN)
    {
	mask += NICKLEN;
	*mask++ = '!';

	/* This flag is used to indicate following parts should
	   be shifted. */
	flag = 1;
    }
    else
    {
	mask = input;
    }

    /* The ident and host must be truncated at the beginning and
       replaced with a '*' to be compatible with ircu. */
    length = 0;
    start = input;
    while(*input++ != '@')
    {
	length++;
    }
    if(length > USERLEN || flag)
    {
	if(length > USERLEN)
	{
	    start = input - USERLEN;
	    *mask++ = '*';
	}
	while(*start != '@')
	{
	    *mask++ = *start++;
	}
	*mask++ = '@';

	flag = 1;
    }
    else
    {
	mask = input;
    }

    length = 0;
    start = input;
    while(*input++)
    {
	length++;
    }
    if(length > HOSTLEN || flag)
    {
	if(length > HOSTLEN)
	{
	    start = input - HOSTLEN;
	    *mask++ = '*';
	}
	while(*start)
	{
	    *mask++ = *start++;
	}
	*mask = '\0';
    }

    return output;
}

static long
TypeLength(char type)
{
    switch (type) {
    case 'y': return 365*24*60*60;
    case 'M': return 31*24*60*60;
    case 'w': return 7*24*60*60;
    case 'd': return 24*60*60;
    case 'h': return 60*60;
    case 'm': return 60;
    case 's': return 1;
    default: return 0;
    }
}

unsigned long
ParseInterval(const char *interval)
{
    unsigned long seconds = 0;
    int partial = 0;
    char c;

    /* process the string, resetting the count if we find a unit character */
    while ((c = *interval++)) {
	if (isdigit((int)c)) {
	    partial = partial*10 + c - '0';
	} else {
	    seconds += TypeLength(c) * partial;
	    partial = 0;
	}
    }
    /* assume the last chunk is seconds (the normal case) */
    return seconds + partial;
}

static long
GetSizeMultiplier(char type)
{
    switch (type) {
    case 'G': case 'g': return 1024*1024*1024;
    case 'M': case 'm': return 1024*1024;
    case 'K': case 'k': return 1024;
    case 'B': case 'b': return 1;
    default: return 0;
    }
}

unsigned long
ParseVolume(const char *volume)
{
    unsigned long accum = 0, partial = 0;
    char c;
    while ((c = *volume++)) {
        if (isdigit((int)c)) {
            partial = partial*10 + c - '0';
        } else {
            accum += GetSizeMultiplier(c) * partial;
            partial = 0;
        }
    }
    return accum + partial;
}

int
parse_ipmask(const char *str, struct in_addr *addr, unsigned long *mask)
{
    int accum, pos;
    unsigned long t_a, t_m;

    t_a = t_m = pos = 0;
    if (addr)
        addr->s_addr = htonl(t_a);
    if (mask)
        *mask = t_m;
    while (*str) {
        if (!isdigit(*str))
            return 0;
        accum = 0;
        do {
            accum = (accum * 10) + *str++ - '0';
        } while (isdigit(*str));
        if (accum > 255)
            return 0;
        t_a = (t_a << 8) | accum;
        t_m = (t_m << 8) | 255;
        pos += 8;
        if (*str == '.') {
            str++;
            while (*str == '*') {
                str++;
                if (*str == '.') {
                    t_a <<= 8;
                    t_m <<= 8;
                    pos += 8;
                    str++;
                } else if (*str == 0) {
                    t_a <<= 32 - pos;
                    t_m <<= 32 - pos;
                    pos = 32;
                    goto out;
                } else
                    return 0;
            }
        } else if (*str == '/') {
            int start = pos;
            accum = 0;
            do {
                accum = (accum * 10) + *str++ - '0';
            } while (isdigit(*str));
            while (pos < start+accum && pos < 32) {
                t_a = (t_a << 1) | 0;
                t_m = (t_m << 1) | 1;
                pos++;
            }
            if (pos != start+accum)
                return 0;
        } else if (*str == 0)
            break;
        else
            return 0;
    }
out:
    if (pos != 32)
        return 0;
    if (addr)
        addr->s_addr = htonl(t_a);
    if (mask)
        *mask = t_m;
    return 1;
}

char *
unsplit_string(char *set[], unsigned int max, char *dest)
{
    static char unsplit_buffer[MAXLEN*2];
    unsigned int ii, jj, pos;

    if (!dest)
        dest = unsplit_buffer;
    for (ii=pos=0; ii<max; ii++) {
        for (jj=0; set[ii][jj]; jj++)
            dest[pos++] = set[ii][jj];
        dest[pos++] = ' ';
    }
    dest[--pos] = 0;
    return dest;
}

char *
intervalString2(char *output, time_t interval, int brief)
{
    static const struct {
        const char *name;
        long length;
    } unit[] = {
        { "year", 365 * 24 * 60 * 60 },
        { "week",   7 * 24 * 60 * 60 },
        { "day",        24 * 60 * 60 },
        { "hour",            60 * 60 },
        { "minute",               60 },
        { "second",                1 }
    };
    unsigned int type, words, pos, count;

    if(!interval)
    {
	strcpy(output, brief ? "0s" : "0 seconds");
	return output;
    }

    for (type = 0, words = pos = 0;
         interval && (words < 2) && (type < ArrayLength(unit));
         type++) {
	if (interval < unit[type].length)
            continue;
        count = interval / unit[type].length;
        interval = interval % unit[type].length;

        if (brief)
            pos += sprintf(output + pos, "%d%c", count, unit[type].name[0]);
        else if (words == 1)
            pos += sprintf(output + pos, " and %d %s", count, unit[type].name);
        else
            pos += sprintf(output + pos, "%d %s", count, unit[type].name);
        if (count != 1)
            output[pos++] = 's';
        words++;
    }

    output[pos] = 0;
    return output;
}

int
getipbyname(const char *name, unsigned long *ip)
{
    struct hostent *he = gethostbyname(name);
    if (!he)
        return 0;
    if (he->h_addrtype != AF_INET)
        return 0;
    memcpy(ip, he->h_addr_list[0], sizeof(*ip));
    return 1;
}

DEFINE_LIST(string_buffer, char)

void
string_buffer_append_substring(struct string_buffer *buf, const char *tail, unsigned int len)
{
    while (buf->used + len >= buf->size) {
        if (!buf->size)
            buf->size = 16;
        else
            buf->size <<= 1;
        buf->list = realloc(buf->list, buf->size*sizeof(buf->list[0]));
    }
    memcpy(buf->list + buf->used, tail, len+1);
    buf->used += len;
}

void
string_buffer_append_string(struct string_buffer *buf, const char *tail)
{
    string_buffer_append_substring(buf, tail, strlen(tail));
}

void
string_buffer_append_vprintf(struct string_buffer *buf, const char *fmt, va_list args)
{
    va_list working;
    unsigned int len;
    int ret;

    VA_COPY(working, args);
    len = strlen(fmt);
    if (!buf->list || ((buf->used + buf->size) < len)) {
        buf->size = buf->used + len;
        buf->list = realloc(buf->list, buf->size);
    }
    ret = vsnprintf(buf->list + buf->used, buf->size - buf->used, fmt, working);
    if (ret <= 0) {
        /* pre-C99 behavior; double buffer size until it is big enough */
        va_end(working);
        VA_COPY(working, args);
        while ((ret = vsnprintf(buf->list + buf->used, buf->size, fmt, working)) == -1) {
            buf->size += len;
            buf->list = realloc(buf->list, buf->size);
            va_end(working);
            VA_COPY(working, args);
        }
        buf->used += ret;
    } else if (buf->used + ret < buf->size) {
        /* no need to increase allocation size */
        buf->used += ret;
    } else {
        /* now we know exactly how much space we need */
        if (buf->size <= buf->used + ret) {
            buf->size = buf->used + ret + 1;
            buf->list = realloc(buf->list, buf->size);
        }
        va_end(working);
        VA_COPY(working, args);
        buf->used += vsnprintf(buf->list + buf->used, buf->size, fmt, working);
    }
    va_end(working);
    va_end(args);
}

void string_buffer_append_printf(struct string_buffer *buf, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    string_buffer_append_vprintf(buf, fmt, args);
}

void
string_buffer_replace(struct string_buffer *buf, unsigned int from, unsigned int len, const char *repl)
{
    unsigned int repl_len = strlen(repl);
    if (from > buf->used)
        return;
    if (len + from > buf->used)
        len = buf->used - from;
    buf->used = buf->used + repl_len - len;
    if (buf->size <= buf->used) {
        while (buf->used >= buf->size)
            buf->size <<= 1;
        buf->list = realloc(buf->list, buf->size*sizeof(buf->list[0]));
    }
    memmove(buf->list+from+repl_len, buf->list+from+len, strlen(buf->list+from+len));
    strcpy(buf->list+from, repl);
}

struct string_list str_tab;

const char *
strtab(unsigned int ii) {
    if (ii > 65536)
        return NULL;
    if (ii > str_tab.size) {
        unsigned int old_size = str_tab.size;
        while (ii >= str_tab.size)
            str_tab.size <<= 1;
        str_tab.list = realloc(str_tab.list, str_tab.size*sizeof(str_tab.list[0]));
        memset(str_tab.list+old_size, 0, (str_tab.size-old_size)*sizeof(str_tab.list[0]));
    }
    if (!str_tab.list[ii]) {
        str_tab.list[ii] = malloc(12);
        sprintf(str_tab.list[ii], "%u", ii);
    }
    return str_tab.list[ii];
}

void
tools_init(void)
{
    unsigned int upr, lwr;
    for (lwr=0; lwr<256; ++lwr)
        tolower(lwr) = lwr;
    for (upr='A', lwr='a'; lwr <= 'z'; ++upr, ++lwr)
        tolower(upr) = lwr;
#ifdef WITH_PROTOCOL_P10
    for (upr='[', lwr='{'; lwr <= '~'; ++upr, ++lwr)
        tolower(upr) = lwr;
    for (upr=0xc0, lwr=0xe0; lwr <= 0xf6; ++upr, ++lwr)
        tolower(upr) = lwr;
    for (upr=0xd8, lwr=0xf8; lwr <= 0xfe; ++upr, ++lwr)
        tolower(upr) = lwr;
#endif
    str_tab.size = 1001;
    str_tab.list = calloc(str_tab.size, sizeof(str_tab.list[0]));
}

void
tools_cleanup(void)
{
    unsigned int ii;
    for (ii=0; ii<str_tab.size; ++ii)
        free(str_tab.list[ii]);
    free(str_tab.list);
}

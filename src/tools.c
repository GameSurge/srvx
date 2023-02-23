/* tools.c - miscellaneous utility functions
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

#include "helpfile.h"
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

static const unsigned char ctype[256] = {
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 0, 0, 0, 0,
   0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0,10,11,12,13,14,15, 0, 0, 0, 0, 0, 0, 0, 0, 0,
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

/* This layout (groups of ten) follows the ZeroMQ Z85 spec. */
static const char to_z85[86] =
    "0123456789" /*  0-9  */
    "abcdefghij"
    "klmnopqrst" /* 20-29 */
    "uvwxyzABCD"
    "EFGHIJKLMN" /* 40-49 */
    "OPQRSTUVWX"
    "YZ.-:+=^!/" /* 60-69 */
    "*?&<>()[]{"
    "}@%$#";     /* 80-84 */

static const unsigned char from_z85[256] = {
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
   0,68, 0,84,83,82,72, 0,75,76,70,65, 0,63,62,69,
   0, 1, 2, 3, 4, 5, 6, 7, 8, 9,64, 0,73,66,74,71,
  81,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,
  51,52,53,54,55,56,57,58,59,60,61,77, 0,78,67, 0,
   0,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,
  25,26,27,28,29,30,31,32,33,34,35,79, 0,80
};

void
inttoz85(char *buf, unsigned int v)
{
    buf[4] = to_z85[v % 85]; v /= 85;
    buf[3] = to_z85[v % 85]; v /= 85;
    buf[2] = to_z85[v % 85]; v /= 85;
    buf[1] = to_z85[v % 85]; v /= 85;
    buf[0] = to_z85[v];
}

unsigned int
z85toint(const char *s)
{
    return (int)from_z85[(unsigned char)s[0]] * 85 * 85 * 85 * 85
        + (int)from_z85[(unsigned char)s[1]] * 85 * 85 * 85
        + (int)from_z85[(unsigned char)s[2]] * 85 * 85
        + (int)from_z85[(unsigned char)s[3]] * 85
        + from_z85[(unsigned char)s[4]];
}

unsigned int
irc_ntop(char *output, unsigned int out_size, const irc_in_addr_t *addr)
{
    static const char hexdigits[] = "0123456789abcdef";
    unsigned int pos;

    assert(output);
    assert(addr);

    if (irc_in_addr_is_ipv4(*addr)) {
        unsigned int ip4;

        ip4 = (ntohs(addr->in6[6]) << 16) | ntohs(addr->in6[7]);
        pos = snprintf(output, out_size, "%u.%u.%u.%u", (ip4 >> 24), (ip4 >> 16) & 255, (ip4 >> 8) & 255, ip4 & 255);
   } else {
        unsigned int part, max_start, max_zeros, curr_zeros, ii;

        /* Find longest run of zeros. */
        for (max_start = max_zeros = curr_zeros = ii = 0; ii < 8; ++ii) {
            if (!addr->in6[ii])
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
#define APPEND(CH) do { if (pos < out_size) output[pos] = (CH); pos++; } while (0)
        for (pos = 0, ii = 0; ii < 8; ++ii) {
            if ((max_zeros > 0) && (ii == max_start)) {
                if (ii == 0)
                    APPEND(':');
                APPEND(':');
                ii += max_zeros - 1;
                continue;
            }
            part = ntohs(addr->in6[ii]);
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
#undef APPEND
        output[pos < out_size ? pos : out_size - 1] = '\0';
    }

    return pos;
}

unsigned int
irc_ntop_mask(char *output, unsigned int out_size, const irc_in_addr_t *addr, unsigned char bits)
{
    char base_addr[IRC_NTOP_MAX_SIZE];
    int len;

    if (bits >= 128)
        return irc_ntop(output, out_size, addr);
    if (!irc_ntop(base_addr, sizeof(base_addr), addr))
        return 0;
    len = snprintf(output, out_size, "%s/%d", base_addr, bits);
    if ((unsigned int)len >= out_size)
        return 0;
    return len;
}

static unsigned int
irc_pton_ip4(const char *input, unsigned char *pbits, uint32_t *output)
{
    unsigned int dots = 0, pos = 0, part = 0, ip = 0, bits = 32;

    /* Intentionally no support for bizarre IPv4 formats (plain
     * integers, octal or hex components) -- only vanilla dotted
     * decimal quads, optionally with trailing /nn.
     */
    if (input[0] == '.')
        return 0;
    while (1) switch (input[pos]) {
    default:
        if (dots < 3)
            return 0;
    out:
        ip |= part << (24 - 8 * dots++);
        *output = htonl(ip);
        if (pbits)
            *pbits = bits;
        return pos;
    case '.':
        if (input[++pos] == '.')
            return 0;
        ip |= part << (24 - 8 * dots++);
        part = 0;
        if (input[pos] == '*') {
            while (input[++pos] == '*') ;
            if (input[pos] != '\0')
                return 0;
            if (pbits)
                *pbits = dots * 8;
            *output = htonl(ip);
            return pos;
        }
        break;
    case '/':
        if (!pbits || !isdigit(input[pos + 1]))
            return 0;
        for (bits = 0; isdigit(input[++pos]); )
            bits = bits * 10 + input[pos] - '0';
        if (bits > 32)
            return 0;
        goto out;
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        part = part * 10 + input[pos++] - '0';
        if (part > 255)
            return 0;
        break;
    }
}

unsigned int
irc_pton(irc_in_addr_t *addr, unsigned char *bits, const char *input)
{
    const char *part_start = NULL;
    char *colon;
    char *dot;
    unsigned int part = 0, pos = 0, ii = 0, cpos = 8;

    assert(input);
    memset(addr, 0, sizeof(*addr));
    colon = strchr(input, ':');
    dot = strchr(input, '.');

    if (colon && (!dot || (dot > colon))) {
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
        while (ii < 8) switch (input[pos]) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
            part = (part << 4) | (ctype[(unsigned char)input[pos++]] & 15);
            if (part > 0xffff)
                return 0;
            break;
        case ':':
            part_start = input + ++pos;
            if (input[pos] == '.')
                return 0;
            addr->in6[ii++] = htons(part);
            part = 0;
            if (input[pos] == ':') {
                if (cpos < 8)
                    return 0;
                cpos = ii;
            }
            break;
        case '.': {
            uint32_t ip4;
            unsigned int len;
            len = irc_pton_ip4(part_start, bits, &ip4);
            if (!len || (ii > 6))
                return 0;
            memcpy(addr->in6 + ii, &ip4, sizeof(ip4));
            if (bits)
                *bits += 96;
            ii += 2;
            pos = part_start + len - input;
            goto finish;
        }
        case '/':
            if (!bits || !isdigit(input[pos + 1]))
                return 0;
            addr->in6[ii++] = htons(part);
            for (part = 0; isdigit(input[++pos]); )
                part = part * 10 + input[pos] - '0';
            if (part > 128)
                return 0;
            *bits = part;
            goto finish;
        case '*':
            while (input[++pos] == '*') ;
            if (input[pos] != '\0' || cpos < 8)
                return 0;
            if (bits)
                *bits = ii * 16;
            return pos;
        default:
            addr->in6[ii++] = htons(part);
            if (cpos == 8 && ii < 8)
                return 0;
            if (bits)
                *bits = 128;
            goto finish;
        }
    finish:
        /* Shift stuff after "::" up and fill middle with zeros. */
        if (cpos < 8) {
            unsigned int jj;
            for (jj = 0; jj < ii - cpos; jj++)
                addr->in6[7 - jj] = addr->in6[ii - jj - 1];
            for (jj = 0; jj < 8 - ii; jj++)
                addr->in6[cpos + jj] = 0;
        }
    } else if (dot) {
        uint32_t ip4;
        pos = irc_pton_ip4(input, bits, &ip4);
        if (pos) {
            addr->in6[5] = htons(65535);
            addr->in6[6] = htons(ntohl(ip4) >> 16);
            addr->in6[7] = htons(ntohl(ip4) & 65535);
            if (bits)
                *bits += 96;
        }
    } else if (input[0] == '*') {
        while (input[++pos] == '*') ;
        if (input[pos] != '\0')
            return 0;
        if (bits)
            *bits = 0;
    }
    return pos;
}

const char *irc_ntoa(const irc_in_addr_t *addr)
{
    static char ntoa[IRC_NTOP_MAX_SIZE];
    irc_ntop(ntoa, sizeof(ntoa), addr);
    return ntoa;
}

unsigned int
irc_check_mask(const irc_in_addr_t *check, const irc_in_addr_t *mask, unsigned char bits)
{
    unsigned int ii;

    for (ii = 0; (ii < 8) && (bits > 16); bits -= 16, ++ii)
        if (check->in6[ii] != mask->in6[ii])
            return 0;
    if (ii < 8 && bits > 0
        && (ntohs(check->in6[ii] ^ mask->in6[ii]) >> (16 - bits)))
        return 0;
    return 1;
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

char *
ircstrlower(char *str) {
    size_t ii;
    for (ii = 0; str[ii] != '\0'; ++ii)
        str[ii] = tolower(str[ii]);
    return str;
}

int
split_line(char *line, int irc_colon, int argv_size, char *argv[])
{
    int argc = 0;
#ifndef NDEBUG
    int n;
#endif
    while (*line && (argc < argv_size)) {
        while (*line == ' ')
            *line++ = 0;
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
        while (*line != ' ' && *line)
            line++;
    }
#ifndef NDEBUG
    for (n=argc; n<argv_size; n++)
        argv[n] = (char*)0xFEEDBEEF;
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
    const char *m = glob, *n = text;
    const char *m_tmp = glob, *n_tmp = text;
    int star_p;

    for (;;) switch (*m) {
    case '\0':
        if (!*n)
            return 1;
    backtrack:
        if (m_tmp == glob)
            return 0;
        m = m_tmp;
        n = ++n_tmp;
        if (!*n)
            return 0;
        break;
    case '\\':
        m++;
        /* allow escaping to force capitalization */
        if (*m++ != *n++)
            goto backtrack;
        break;
    case '*': case '?':
        for (star_p = 0; ; m++) {
            if (*m == '*')
                star_p = 1;
            else if (*m == '?') {
                if (!*n++)
                    goto backtrack;
            } else break;
        }
        if (star_p) {
            if (!*m)
                return 1;
            else if (*m == '\\') {
                m_tmp = ++m;
                if (!*m)
                    return 0;
                for (n_tmp = n; *n && *n != *m; n++) ;
            } else {
                m_tmp = m;
                for (n_tmp = n; *n && tolower(*n) != tolower(*m); n++) ;
            }
        }
        /* fall through */
    default:
        if (!*n)
            return *m == '\0';
        if (tolower(*m) != tolower(*n))
            goto backtrack;
        m++;
        n++;
        break;
    }
}

extern const char *hidden_host_suffix;

int
user_matches_glob(struct userNode *user, const char *orig_glob, int flags)
{
    irc_in_addr_t mask;
    char *glob, *marker;
    unsigned char mask_bits;

    /* Make a writable copy of the glob */
    glob = alloca(strlen(orig_glob)+1);
    strcpy(glob, orig_glob);
    /* Check the nick, if it's present */
    if (flags & MATCH_USENICK) {
        if (!(marker = strchr(glob, '!'))) {
            log_module(MAIN_LOG, LOG_ERROR, "user_matches_glob(\"%s\", \"%s\", %d) called, and glob doesn't include a '!'", user->nick, orig_glob, flags);
            return 0;
        }
        *marker = 0;
        if (!match_ircglob(user->nick, glob)) return 0;
        glob = marker + 1;
    }
    /* Check the ident */
    if (!(marker = strchr(glob, '@'))) {
        log_module(MAIN_LOG, LOG_ERROR, "user_matches_glob(\"%s\", \"%s\", %d) called, and glob doesn't include an '@'", user->nick, orig_glob, flags);
        return 0;
    }
    *marker = 0;
    if (((IsFakeIdent(user) && IsHiddenHost(user) && (flags & MATCH_VISIBLE)) || !match_ircglob(user->ident, glob)) &&
        !(IsFakeIdent(user) && match_ircglob(user->fakeident, glob)))
        return 0;
    glob = marker + 1;
    /* Check for a fakehost match. */
    if (IsFakeHost(user) && match_ircglob(user->fakehost, glob))
        return 1;
    /* Check for an account match. */
    if (hidden_host_suffix && user->handle_info) {
        char hidden_host[HOSTLEN+1];
        snprintf(hidden_host, sizeof(hidden_host), "%s.%s", user->handle_info->handle, hidden_host_suffix);
        if (match_ircglob(hidden_host, glob))
            return 1;
    }
    /* If only matching the visible hostnames, bail early. */
    if ((flags & MATCH_VISIBLE) && IsHiddenHost(user)
        && (IsFakeHost(user) || (hidden_host_suffix && user->handle_info)))
        return 0;
    /* If it might be an IP glob, test that. */
    if (irc_pton(&mask, &mask_bits, glob)
        && irc_check_mask(&user->ip, &mask, mask_bits))
        return 1;
    /* None of the above; could only be a hostname match. */
    return match_ircglob(user->hostname, glob);
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
    while (*text && (isalnum((char)*text) || strchr(".-?*:", *text)))
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
    while (*text && (isalnum((char)*text) || strchr(".-?*:", *text)))
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
        } else if (strchr("yMwdhms", c)) {
            seconds += TypeLength(c) * partial;
            partial = 0;
        } else {
            return 0;
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
    dest[pos - (pos > 0)] = 0;
    return dest;
}

char *
intervalString(char *output, unsigned long interval, struct handle_info *hi)
{
    static const struct {
        const char *msg_single;
        const char *msg_plural;
        unsigned long length;
    } unit[] = {
        { "MSG_YEAR",   "MSG_YEARS", 365 * 24 * 60 * 60 },
        { "MSG_WEEK",   "MSG_WEEKS",   7 * 24 * 60 * 60 },
        { "MSG_DAY",    "MSG_DAYS",        24 * 60 * 60 },
        { "MSG_HOUR",   "MSG_HOURS",            60 * 60 },
        { "MSG_MINUTE", "MSG_MINUTES",               60 },
        { "MSG_SECOND", "MSG_SECONDS",                1 }
    };
    struct language *lang;
    const char *msg;
    unsigned int type, words, pos, count;

    lang = hi ? hi->language : lang_C;
    if(!interval)
    {
        msg = language_find_message(lang, "MSG_0_SECONDS");
        return strcpy(output, msg);
    }

    for (type = 0, words = pos = 0;
         interval && (words < 2) && (type < ArrayLength(unit));
         type++) {
        if (interval < unit[type].length)
            continue;
        count = interval / unit[type].length;
        interval = interval % unit[type].length;

        if (words++ == 1) {
            msg = language_find_message(lang, "MSG_AND");
            pos += sprintf(output + pos, " %s ", msg);
        }
        if (count == 1)
            msg = language_find_message(lang, unit[type].msg_single);
        else
            msg = language_find_message(lang, unit[type].msg_plural);
        pos += sprintf(output + pos, "%d %s", count, msg);
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
    while (buf->used + len + 1 >= buf->size) {
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
        while ((ret = vsnprintf(buf->list + buf->used, buf->size - buf->used, fmt, working)) <= 0) {
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
do_expandos(char *output, unsigned int out_len, const char *input, ...)
{
    va_list args;
    const char *key;
    const char *datum;
    char *found;
    unsigned int klen;
    unsigned int dlen;
    unsigned int rlen;

    if (!input || !*input) {
        *output = '\0';
        return;
    }

    safestrncpy(output, input, out_len);
    va_start(args, input);
    while ((key = va_arg(args, const char *)) != NULL) {
        datum = va_arg(args, const char *);
        klen = strlen(key);
        dlen = strlen(datum);
        for (found = output; (found = strstr(output, key)) != NULL; found += dlen) {
            rlen = strlen(found + klen);
            /* Save 1 spot for the null terminator at all times */
            if ((dlen > klen) && (dlen > out_len - rlen - (size_t)(found - output) - 1))
                rlen = output + out_len - found - dlen - 1;
            memmove(found + dlen, found + klen, rlen);
            /* Add null terminator at the very end */
            found[dlen + rlen] = '\0';
            memcpy(found, datum, dlen);
        }
    }

    va_end(args);
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

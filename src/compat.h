#ifndef COMPAT_H
#define COMPAT_H

#include "config.h"

/* AIX's compiler requires this to be the first thing in the compiled
 * files.  Yay for braindead compilers. */
#if defined(__GNUC__) && !defined(HAVE_ALLOCA_H)
# define alloca __builtin_alloca
#else
# if defined(HAVE_ALLOCA_H)
#  include <alloca.h>
# else
#  ifdef _AIX
#   pragma alloca
#  else
#   ifndef alloca
char *alloca();
#   endif
#  endif
# endif
#endif

/* These are ANSI C89 headers, so everybody should have them.  If
 * they're missing, we probably don't care much about the platform.
 * If we do, we can add an autoconf test and try to patch around;
 * this is the right file for that, after all :)
 */
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_WINSOCK2_H
/* Windows XP+ only -- older versions lack getaddrinfo() etc. */
# define _WIN32_WINNT 0x0501
# include <winsock2.h>
#endif

#ifdef HAVE_WS2TCPIP_H
# include <ws2tcpip.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

/* Cygwin (for example) doesn't define LINE_MAX, although mingw does. */
#if !defined(LINE_MAX)
# define LINE_MAX 2048
#endif

#ifdef HAVE_VA_COPY
#define VA_COPY(DEST, SRC) va_copy(DEST, SRC)
#elif HAVE___VA_COPY
#define VA_COPY(DEST, SRC) __va_copy(DEST, SRC)
#else
#define VA_COPY(DEST, SRC) memcpy(&(DEST), &(SRC), sizeof(DEST))
#endif

#ifndef HAVE_GETTIMEOFDAY
struct timezone;
extern int gettimeofday(struct timeval * tv, struct timezone * tz);
#endif

#ifndef HAVE_LOCALTIME_R
extern struct tm *localtime_r(const time_t *timep, struct tm *result);
#endif

#ifndef HAVE_MEMCPY
/* this should use size_t, but some systems don't define it */
extern void * memcpy(void * dest, void const * src, unsigned long n);
#endif

#ifndef HAVE_MEMSET
/* this should use size_t, but some systems don't define it */
extern void * memset(void * dest, int c, unsigned long n);
#endif

#ifndef HAVE_STRDUP
extern char * strdup(char const * str);
#endif

#ifndef HAVE_STRERROR
extern char const * strerror(int errornum);
#endif

#ifndef HAVE_STRUCT_ADDRINFO

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

#define AI_PASSIVE 1
#define AI_CANONNAME 2
#define AI_NUMERICHOST 4

#endif /* !defined(HAVE_STRUCT_ADDRINFO) */

#ifndef HAVE_GETADDRINFO
int getaddrinfo(const char *node, const char *service, const struct addrinfo *hints, struct addrinfo **res);
void freeaddrinfo(struct addrinfo *res);
#endif

#ifndef HAVE_GAI_STRERROR
const char *gai_strerror(int errcode);
#endif

#ifndef EAI_FAMILY
# define EAI_FAMILY -1
# define EAI_NONAME -2
# define EAI_OVERFLOW -3
# define EAI_FAIL -4
# define EAI_AGAIN -5
#endif

#ifndef HAVE_GETNAMEINFO

#define NI_NUMERICHOST 1

int getnameinfo(const struct sockaddr *sa, socklen_t salen, char *host, size_t hostlen, char *serv, size_t servlen, int flags);

#endif

#ifndef EINPROGRESS
# ifdef WSAEINPROGRESS
#  define EINPROGRESS WSAEINPROGRESS
#  define EHOSTUNREACH WSAEHOSTUNREACH
#  define ECONNREFUSED WSAECONNREFUSED
#  define ECONNRESET WSAECONNRESET
#  define ETIMEDOUT WSAETIMEDOUT
# endif
#endif

#ifndef SIGCHLD
# define SIGCHLD SIGCLD
#endif

#endif /* COMPAT_H */

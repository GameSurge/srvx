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

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_VA_COPY
#define VA_COPY(DEST, SRC) va_copy(DEST, SRC)
#elif HAVE___VA_COPY
#define VA_COPY(DEST, SRC) __va_copy(DEST, SRC)
#else
#define VA_COPY(DEST, SRC) memcpy(&(DEST), &(SRC), sizeof(DEST))
#endif

#ifndef HAVE_GETTIMEOFDAY
extern int gettimeofday(struct timeval * tv, struct timezone * tz);
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

#endif /* COMPAT_H */

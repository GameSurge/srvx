dnl Process this file with autoconf to create a configure script.

dnl General initialization.
AC_PREREQ(2.64)
AC_INIT([srvx],[1.4.0-rc3],[srvx-bugs@lists.sourceforge.net])
CODENAME=surge
AC_CONFIG_HEADERS(src/config.h)
AC_CONFIG_SRCDIR(src/opserv.c)
dnl AM_CANONICAL_TARGET must be before AM_INIT_AUTOMAKE() or autoconf whines
AC_CANONICAL_TARGET
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([gnu 1.10 silent-rules])
AM_MAINTAINER_MODE
LT_INIT

dnl Compiler/runtime feature checks.
AC_C_CONST
dnl "const" *should* be in the -Werror section, but that breaks Linux. gg gcc.

dnl Checks for programs.
AC_PROG_AWK
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_GCC_TRADITIONAL

dnl Look for a git client
AC_CHECK_PROGS(GIT, [git])
AM_CONDITIONAL(HAS_GIT, test z$GIT != z)

dnl nice that unixes can all follow a standard.
case $target in
  *-freebsd2* | *-freebsdelf2* | *-freebsd*out3*)
    ANSI_SRC=""
    ;;
  *-freebsd3* | *-freebsdelf3* | *-freebsd*out3*)
    ANSI_SRC=""
    AC_DEFINE(BROKEN_REGEX, 1, [Define if the system regex library is unreliable.])
        BROKEN_REGEX=yes
    ;;
  *-solaris*)
    EXTRA_DEFINE="-D__SOLARIS__"
    ANSI_SRC="-fno-builtin"
    ;;
  *-cygwin)
    ANSI_SRC="-fno-builtin"
    ;;
  *-linux*)
    dnl -D_GNU_SOURCE needed for strsignal()
    EXTRA_DEFINE="-D_GNU_SOURCE"
    ANSI_SRC=""
    ;;
  *)
    ANSI_SRC=""
    ;;
esac
CFLAGS="$CFLAGS $EXTRA_DEFINE"

dnl Checks for libraries.
AC_CHECK_LIB(socket, socket)
AC_CHECK_LIB(nsl, gethostbyname)

dnl Checks for header files.
AC_HEADER_STDC

dnl will be used for portability stuff
AC_HEADER_TIME
AC_STRUCT_TM

dnl Would rather not bail on headers, BSD has alot of the functions elsewhere. -Jedi
AC_CHECK_HEADERS(fcntl.h malloc.h netdb.h arpa/inet.h netinet/in.h sys/resource.h sys/timeb.h sys/times.h sys/param.h sys/socket.h sys/time.h sys/types.h sys/wait.h unistd.h getopt.h memory.h regex.h arpa/inet.h sys/mman.h sys/stat.h dirent.h sys/epoll.h sys/event.h stdint.h,,)

dnl portability stuff, hurray! -Jedi
AC_CHECK_MEMBER([struct sockaddr.sa_len],
                [AC_DEFINE([HAVE_SOCKADDR_SA_LEN],[1],[Define if struct sockaddr has sa_len field])],
                [],[#include <sys/types.h>
#include <sys/socket.h>])
AC_CHECK_MEMBER([struct addrinfo.ai_flags],
                [AC_DEFINE([HAVE_STRUCT_ADDRINFO],[1],[Define if struct addrinfo declared])],
                [],[#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>])

dnl We have fallbacks in case these are missing, so just check for them.
AC_CHECK_FUNCS(freeaddrinfo getaddrinfo gai_strerror getnameinfo getpagesize memcpy memset strdup strerror strsignal localtime localtime_r setrlimit getopt getopt_long regcomp regexec regfree sysconf inet_aton epoll_create kqueue kevent select gettimeofday times GetProcessTimes mprotect,,)

dnl Check for the fallbacks for functions missing above.
if test $ac_cv_func_gettimeofday = no; then
  AC_CHECK_FUNCS(ftime,,AC_MSG_ERROR([ftime or gettimeofday required.  srvx build will fail.]))
fi

dnl Check for absolutely required library functions.
AC_CHECK_FUNCS(socket strcspn strspn strtod strtoul,,AC_MSG_ERROR([a required function was not found.  srvx build will fail.]))

dnl Check for functions (and how to get them).
AC_FUNC_ALLOCA
AC_FUNC_MMAP

AC_CACHE_CHECK([for sin_len], ac_cv_sin_len,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([#include <sys/types.h>
#include <netinet/in.h>],[struct sockaddr_in *sin; sin->sin_len = 0;])],
ac_cv_sin_len="yes", ac_cv_sin_len="no")])
if test $ac_cv_sin_len = yes ; then
  AC_DEFINE(HAVE_SIN_LEN, 1, [Define if struct sockaddr_in contains a sin_len field])
fi

dnl Check for socklen_t.  In traditional BSD this is an int, but some
dnl OSes use a different type.  Test until we find something that will
dnl work properly.  Test borrowed from a patch submitted for Python.
AC_CHECK_TYPE([socklen_t], ,[
  AC_MSG_CHECKING([for socklen_t equivalent])
  AC_CACHE_VAL([curl_cv_socklen_t_equiv],
  [
dnl Systems have either "struct sockaddr*" or "void*" as second
dnl arg to getpeername.
    curl_cv_socklen_t_equiv=
    for arg2 in "struct sockaddr" void ; do
      for t in int size_t unsigned long "unsigned long" ; do
        AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <sys/types.h>
#include <sys/socket.h>
int getpeername (int $arg2 *, $t *);]], [[$t len;
  getpeername(0, 0, &len);]])],[curl_cv_socklen_t_equiv="$t"
  break],[])
      done
    done
  ])
  AC_MSG_RESULT($curl_cv_socklen_t_equiv)
  AC_DEFINE_UNQUOTED(socklen_t, $curl_cv_socklen_t_equiv,
      [type to use in place of socklen_t if not defined])],
  [#include <sys/types.h>
#include<sys/socket.h>])

dnl Can only check with -Werror, but the rest of configure doesn't like -Werror
OLD_CFLAGS=$CFLAGS
CFLAGS="$CFLAGS -W -Wall -Werror"
if test "z$USE_MAINTAINER_MODE" = zyes ; then
  CFLAGS="$CFLAGS -ansi"
fi

dnl Check for post-C89 keywords
AC_C_INLINE

dnl How to copy one va_list to another?
AC_CACHE_CHECK([for va_copy], ac_cv_c_va_copy, [AC_LINK_IFELSE(
  [AC_LANG_PROGRAM([#include <stdarg.h>], [va_list ap1, ap2; va_copy(ap1, ap2);])],
  [ac_cv_c_va_copy="yes"],
  [ac_cv_c_va_copy="no"]
)])
if test "$ac_cv_c_va_copy" = "yes" ; then
  AC_DEFINE(HAVE_VA_COPY, 1, [Define if we have va_copy])
fi

AC_CACHE_CHECK([for __va_copy], ac_cv_c___va_copy, [AC_LINK_IFELSE(
  [AC_LANG_PROGRAM([#include <stdarg.h>], [va_list ap1, ap2; __va_copy(ap1, ap2);])],
  [ac_cv_c___va_copy="yes"],
  [ac_cv_c___va_copy="no"]
)])
if test "$ac_cv_c___va_copy" = "yes" ; then
  AC_DEFINE(HAVE___VA_COPY, 1, [Define if we have __va_copy])
fi

dnl Now fix things back up
CFLAGS=$OLD_CFLAGS

dnl Optional features.
AC_MSG_CHECKING(which malloc to use)
AC_ARG_WITH(malloc,
[  --with-malloc=type      Enables use of a special malloc library; one of:
                          system (the default), boehm-gc, dmalloc, mpatrol, srvx, slab],
[],
[withval="system"])
if test "x$withval" = "xsystem" ; then
  AC_MSG_RESULT(system)
  AC_DEFINE(WITH_MALLOC_SYSTEM, 1, [Define if using the system's malloc])
elif test "x$withval" = "xdmalloc" ; then
  AC_MSG_RESULT(dmalloc)
  AC_CHECK_HEADERS(dmalloc.h,,AC_MSG_ERROR([dmalloc header file missing.  dmalloc build will fail.]))
  AC_CHECK_LIB(dmalloc,malloc,,AC_MSG_ERROR([dmalloc library is missing.  dmalloc build will fail.]))
  AC_DEFINE(WITH_MALLOC_DMALLOC, 1, [Define if using the dmalloc debugging malloc package])
elif test "x$withval" = "xmpatrol" ; then
  AC_MSG_RESULT(mpatrol)
  AC_CHECK_HEADERS(mpatrol.h,,AC_MSG_ERROR([mpatrol header file missing.  mpatrol build will fail.]))
  dnl Using mpatrol requires linking against libelf, at least on Linux.
  AC_CHECK_LIB(elf, elf_begin)
  AC_CHECK_LIB(mpatrol,__mp_atexit,,AC_MSG_ERROR([mpatrol library is missing completely.  mpatrol build will fail.]))
  AC_DEFINE(WITH_MALLOC_MPATROL, 1, [Define if using the mpatrol malloc debugging package])
elif test "x$withval" = "xboehm-gc" ; then
  AC_MSG_RESULT(boehm-gc)
  AC_CHECK_HEADERS(gc/gc.h,,AC_MSG_ERROR([Boehm GC header file missing.  boehm-gc build will fail.]))
  AC_CHECK_LIB(dl, dlopen, , AC_MSG_ERROR([libdl library is missing.  boehm-gc build will fail.]))
  AC_CHECK_LIB(gc, GC_gcollect, , AC_MSG_ERROR([Boehm GC library is missing.  boehm-gc build will fail.]))
  AC_DEFINE(WITH_MALLOC_BOEHM_GC, 1, [Define if using the Boehm GC to garbage collect and check memory leaks])
elif test "x$withval" = "xsrvx" ; then
  AC_MSG_RESULT(srvx)
  AC_DEFINE(WITH_MALLOC_SRVX, 1, [Define if using the srvx internal debug allocator])
  MODULE_OBJS="$MODULE_OBJS alloc-srvx.\$(OBJEXT)"
elif test "x$withval" = "xslab" ; then
  AC_MSG_RESULT(slab)
  AC_DEFINE(WITH_MALLOC_SLAB, 1, [Define if using the slab internal debug allocator])
  MODULE_OBJS="$MODULE_OBJS alloc-slab.\$(OBJEXT)"
else
  AC_MSG_ERROR([Unknown malloc type $withval])
fi

AC_MSG_CHECKING(which protocol to use)
AC_ARG_WITH(protocol,
[  --with-protocol=name    Choose IRC dialect to support; one of:
                          p10 (the default), bahamut],
[],
[withval="p10"])
if test "x$withval" = "xp10" ; then
  AC_MSG_RESULT([P10])
  AC_DEFINE(WITH_PROTOCOL_P10, 1, [Define if using the P10 dialect of IRC])
  MODULE_OBJS="$MODULE_OBJS proto-p10.\$(OBJEXT)"
elif test "x$withval" = "xbahamut" ; then
  AC_MSG_RESULT([Bahamut])
  AC_DEFINE(WITH_PROTOCOL_BAHAMUT, 1, [Define if using the Bahamut dialect of IRC])
  MODULE_OBJS="$MODULE_OBJS proto-bahamut.\$(OBJEXT)"
elif test -r "${srcdir}/src/proto-${withval}.c" ; then
  AC_MSG_RESULT([$withval])
  MODULE_OBJS="$MODULE_OBJS proto-${withval}.\$(OBJEXT)"
else
  AC_MSG_ERROR([Unknown IRC dialect $withval])
fi

AC_MSG_CHECKING(how to send mail)
AC_ARG_WITH(mail,
[  --with-mail=name        How to send mail; one of:
                          sendmail (the default), smtp],
[],
[withval="sendmail"])
if test -r "${srcdir}/src/mail-${withval}.c" ; then
  AC_MSG_RESULT([$withval])
  MODULE_OBJS="$MODULE_OBJS mail-${withval}.\$(OBJEXT)"
else
  AC_MSG_ERROR([Unknown mail method $withval])
fi

AC_MSG_CHECKING([I/O multiplexing backends])
IOMUXES=""

if test "x$ac_cv_func_select" = xyes ; then
  AC_DEFINE(WITH_IOSET_SELECT, 1, [Define if using the select() I/O backend])
  MODULE_OBJS="$MODULE_OBJS ioset-select.\$(OBJEXT)"
  IOMUXES="$IOMUXES select"
fi

AC_ARG_WITH([epoll],
[  --without-epoll         Disables the epoll_*() I/O backend],
[],
[withval="$ac_cv_func_epoll_create"])
if test "x$withval" = xyes ; then
  AC_DEFINE(WITH_IOSET_EPOLL, 1, [Define if using the epoll I/O backend])
  MODULE_OBJS="$MODULE_OBJS ioset-epoll.\$(OBJEXT)"
  IOMUXES="$IOMUXES epoll"
fi

AC_ARG_WITH([kevent],
[  --without-kevent        Disables the kevent() I/O backend],
[],
[withval="$ac_cv_func_kevent"])
if test "x$withval" = xyes ; then
  AC_DEFINE(WITH_IOSET_KEVENT, 1, [Define if using the kevent I/O backend])
  MODULE_OBJS="$MODULE_OBJS ioset-kevent.\$(OBJEXT)"
  IOMUXES="$IOMUXES kevent"
fi

AC_ARG_WITH([win32],
[  --with-win32            Enables the Win32 I/O backend],
[],
[withval="no"])
if test "x$withval" = xyes ; then
  AC_DEFINE(WITH_IOSET_WIN32, 1, [Define if using the Win32 I/O backend])
  MODULE_OBJS="$MODULE_OBJS ioset-win32.\$(OBJEXT)"
  IOMUXES="$IOMUXES win32"
fi

IOMUXES=`echo $IOMUXES | sed 's/^ +//'`
if test "x$IOMUXES" = "x" ; then
  AC_MSG_ERROR([No supported I/O multiplexing backend found])
else
  AC_MSG_RESULT($IOMUXES)
fi

AC_ARG_WITH(getopt,
[  --without-getopt        Disables building of the GNU getopt library],
[if test "$withval" = no; then
  AC_DEFINE(IGNORE_GETOPT, 1, [Define to disable built-in getopt library])
fi])

AC_MSG_CHECKING(whether to enable tokenization)
AC_ARG_ENABLE(tokens,
[  --disable-tokens        Disables tokenization of P10 protocol output
                           (tokens required if linking to ircu 2.10.11)],
[],[enableval=yes])
if test "z$enableval" = zno ; then
  AC_MSG_RESULT(no)
else
  AC_DEFINE(ENABLE_TOKENS, 1, [Define if tokenized P10 desired])
  AC_MSG_RESULT(yes)
fi

AC_MSG_CHECKING(whether to enable debug behaviors)
AC_ARG_ENABLE(debug,
[  --enable-debug          Enables debugging behaviors],
[
  CPPFLAGS="$CPPFLAGS"
  AC_MSG_RESULT(yes)
],
[
  CPPFLAGS="$CPPFLAGS -DNDEBUG"
  AC_MSG_RESULT(no)
])

if test -e src ; then
  if test ! -d src ; then
    AC_MSG_ERROR([src exists but is not a directory; please move it out of the way.])
  fi
else
  mkdir src
fi
AC_MSG_CHECKING(for extra module files)
MODULE_DEFINES="src/modules-list.h"
echo > $MODULE_DEFINES
touch $MODULE_DEFINES
AC_ARG_ENABLE(modules,
[  --enable-modules=list,of,modules   Enable extra modules],
[
  OIFS="$IFS"
  IFS=','
  EXTRA_MODULE_OBJS=""
  module_list=""
  dnl Must use a separate file because autoconf can't stand newlines in an AC_SUBSTed variable.
  for module in $enableval ; do
    module=`echo $module | sed -e s/^mod-// -e s/\\\.c\\\$//`
    EXTRA_MODULE_OBJS="$EXTRA_MODULE_OBJS mod-$module.\$(OBJEXT)"
    module_list="$module_list $module"
    echo "WITH_MODULE($module)" >> $MODULE_DEFINES
  done
  IFS="$OIFS"
  MODULE_OBJS="$MODULE_OBJS $EXTRA_MODULE_OBJS"
  AC_MSG_RESULT($module_list)
],
[
  AC_MSG_RESULT(none)
])

MY_SUBDIRS=""
RX_INCLUDES=""
RX_LIBS=""
if test "${BROKEN_REGEX}" = yes -o "${ac_cv_func_regcomp}" = no; then
  MY_SUBDIRS="rx $MY_SUBDIRS"
  RX_INCLUDES="-I../rx"
  RX_LIBS="../rx/librx.a"
fi
MY_SUBDIRS="$MY_SUBDIRS src"
CFLAGS="$CFLAGS $ANSI_SRC -W -Wall"
if test "z$USE_MAINTAINER_MODE" = zyes ; then
  CFLAGS="$CFLAGS -Werror -Wshadow -ansi"
fi

AC_DEFINE_UNQUOTED(CODENAME, "${CODENAME}", [Code name for this release])
AC_SUBST(MODULE_OBJS)
AC_SUBST(MY_SUBDIRS)
AC_SUBST(RX_INCLUDES)
AC_SUBST(RX_LIBS)
AC_CONFIG_FILES(Makefile rx/Makefile src/Makefile)
AC_OUTPUT

AM_CPPFLAGS = @RX_INCLUDES@
LIBS = @LIBS@ @RX_LIBS@

check_PROGRAMS = sha256_test
noinst_PROGRAMS = srvx slab-read
EXTRA_PROGRAMS = checkdb globtest
noinst_DATA = \
	chanserv.help \
	global.help \
	modcmd.help \
	nickserv.help \
	opserv.help \
	saxdb.help \
	mail.help \
	mod-helpserv.help \
	mod-memoserv.help \
	mod-qserver.help \
	mod-snoop.help \
	mod-sockcheck.help
EXTRA_DIST = $(noinst_DATA)

DISTCLEANFILES = modules-list.h

if HAS_GIT
BUILT_SOURCES = git-version.c
DISTCLEANFILES += git-version.c
.PHONY: checkversion
git-version.c: checkversion
checkversion:
	@GIT_VERSION=`$(GIT) -C $(top_srcdir) describe`; \
	[ "z$$GIT_VERSION" != "z" ] || exit 0; \
	TMPFILE=`mktemp git-version.c.XXXXXX` || exit 1 ; \
	echo "const char git_version[] = \"$${GIT_VERSION}\";" >> $$TMPFILE ; \
	if diff -q git-version.c $$TMPFILE >/dev/null 2>&1 ; then \
	    rm $$TMPFILE ; \
        else \
	    echo "Putting new git version into git-version.c" ; \
	    rm -f git-version.c ; \
	    mv $$TMPFILE git-version.c ; \
	fi
endif

EXTRA_srvx_SOURCES = \
	alloc-slab.c \
	alloc-srvx.c \
	config.h.win32 \
	ioset-epoll.c \
	ioset-kevent.c \
	ioset-select.c \
	ioset-win32.c \
	mail-common.c \
	mail-sendmail.c \
	main-common.c \
	main-win32.c \
	proto-bahamut.c \
	proto-common.c \
	proto-p10.c \
	mod-blacklist.c \
	mod-helpserv.c \
	mod-memoserv.c \
	mod-qserver.c \
	mod-snoop.c \
	mod-sockcheck.c
srvx_LDADD = @MODULE_OBJS@
srvx_DEPENDENCIES = @MODULE_OBJS@
srvx_SOURCES = \
	git-version.c \
	chanserv.c chanserv.h \
	compat.c compat.h \
	conf.c conf.h \
	dict-splay.c dict.h \
	getopt.c getopt1.c g_getopt.h \
	gline.c gline.h \
	global.c global.h \
	hash.c hash.h \
	heap.c heap.h \
	helpfile.c helpfile.h \
	ioset.c ioset.h ioset-impl.h \
	log.c log.h \
	mail.h \
	main.c common.h \
	md5.c md5.h \
	modcmd.c modcmd.h \
	modules.c modules.h \
	nickserv.c nickserv.h \
	opserv.c opserv.h \
	policer.c policer.h \
	proto.h \
	recdb.c recdb.h \
	sar.c sar.h \
	saxdb.c saxdb.h \
	sha256.c sha256.h \
	timeq.c timeq.h \
	tools.c

sha256_test_SOURCES = sha256_test.c sha256.c sha256.h
checkdb_SOURCES = checkdb.c common.h compat.c compat.h dict-splay.c dict.h recdb.c recdb.h saxdb.c saxdb.h tools.c conf.h log.h modcmd.h saxdb.h timeq.h
globtest_SOURCES = common.h compat.c compat.h dict-splay.c dict.h globtest.c tools.c
slab_read_SOURCES = slab-read.c

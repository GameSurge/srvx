AM_CPPFLAGS = @RX_INCLUDES@
LIBS = @LIBS@ @RX_LIBS@

noinst_PROGRAMS = srvx
EXTRA_PROGRAMS = checkdb globtest
noinst_DATA = \
	chanserv.help \
	global.help \
	modcmd.help \
	nickserv.help \
	opserv.help \
	saxdb.help \
	sendmail.help \
	mod-helpserv.help \
	mod-memoserv.help \
	mod-sockcheck.help
EXTRA_DIST = $(noinst_DATA)
BUILT_SOURCES = git-version.h
noinst_HEADERS = git-version.h

if HAS_GIT
.PHONY: checkversion
git-version.h: checkversion
checkversion:
	@( cd $(top_srcdir) && $(GIT) symbolic-ref HEAD ) >/dev/null 2>&1 || exit 0; \
	TMPFILE=`mktemp git-version.h.XXXXXX` || exit 1 ; \
	( \
	  cd $(top_srcdir) ; \
	  GIT_DIR=$${GIT_DIR:-.git} ; \
	  HEAD_NAME=`$(GIT) symbolic-ref HEAD` ; \
	  HEAD_SHA1=`cat $${GIT_DIR}/$${HEAD_NAME}` ; \
	  echo "/* Full revision $${HEAD_SHA1} */" ; \
	  echo "#define GIT_VERSION \"$${HEAD_SHA1%????????????????????????????????}\"" ; \
	) >> $$TMPFILE ; \
	if diff -q git-version.h $$TMPFILE >/dev/null 2>&1 ; then \
	    rm $$TMPFILE ; \
        else \
	    echo "Putting new git version into git-version.h" ; \
	    rm -f git-version.h ; \
	    mv $$TMPFILE git-version.h ; \
	fi
endif

EXTRA_srvx_SOURCES = \
	alloc-slab.c \
	alloc-srvx.c \
	config.h.win32 \
	ioset-epoll.c \
	ioset-select.c \
	ioset-win32.c \
	proto-bahamut.c \
	proto-common.c \
	proto-p10.c \
	mod-snoop.c \
	mod-memoserv.c \
	mod-helpserv.c \
	mod-qserver.c \
	mod-sockcheck.c
srvx_LDADD = @MODULE_OBJS@
srvx_DEPENDENCIES = @MODULE_OBJS@
srvx_SOURCES = \
	git-version.h \
	chanserv.c chanserv.h \
	compat.c compat.h \
	conf.c conf.h \
	dict-splay.c dict.h \
	getopt.c getopt.h \
	getopt1.c getopt.h \
	gline.c gline.h \
	global.c global.h \
	hash.c hash.h \
	heap.c heap.h \
	helpfile.c helpfile.h \
	ioset.c ioset.h ioset-impl.h \
	log.c log.h \
	main.c common.h \
	md5.c md5.h \
	modcmd.c modcmd.h \
	modules.c modules.h \
	nickserv.c nickserv.h \
	opserv.c opserv.h \
	policer.c policer.h \
	proto.h \
	recdb.c recdb.h \
	saxdb.c saxdb.h \
	sendmail.c sendmail.h \
	timeq.c timeq.h \
	tools.c

checkdb_SOURCES = checkdb.c common.h compat.c compat.h dict-splay.c dict.h recdb.c recdb.h saxdb.c saxdb.h tools.c conf.h log.h modcmd.h saxdb.h timeq.h
globtest_SOURCES = common.h compat.c compat.h dict-splay.c dict.h globtest.c tools.c

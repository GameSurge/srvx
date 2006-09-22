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
BUILT_SOURCES = arch-version.h
noinst_HEADERS = arch-version.h

if HAS_GNU_ARCH
.PHONY: checkversion
arch-version.h: checkversion
checkversion:
	@$(GNU_ARCH) logs -f >/dev/null 2>&1 || exit 0; \
	TMPFILE=`mktemp arch-version.h.XXXXXX` || exit 1 ; \
	echo "#define ARCH_VERSION \"`$(GNU_ARCH) logs -f | tail -n 1`\"" >> $$TMPFILE ; \
	if diff -q arch-version.h $$TMPFILE >/dev/null 2>&1 ; then \
	    rm $$TMPFILE ; \
        else \
	    echo "Putting new arch version into arch-version.h" ; \
	    rm -f arch-version.h ; \
	    mv $$TMPFILE arch-version.h ; \
	fi
endif

EXTRA_srvx_SOURCES = \
	alloc-slab.c \
	alloc-srvx.c \
	ioset-select.c \
	proto-bahamut.c \
	proto-common.c \
	proto-p10.c \
	mod-snoop.c \
	mod-memoserv.c \
	mod-helpserv.c \
	mod-sockcheck.c
srvx_LDADD = @MODULE_OBJS@
srvx_DEPENDENCIES = @MODULE_OBJS@
srvx_SOURCES = \
	arch-version.h \
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

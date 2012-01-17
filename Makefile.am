EXTRA_DIST = FAQ UPGRADE srvx.conf.example sockcheck.conf.example Makefile.win32
SUBDIRS = @MY_SUBDIRS@
DIST_SUBDIRS = src rx
ACLOCAL_AMFLAGS = -I m4

all: srvx

srvx: src/srvx
	cp ./src/srvx $(srcdir)/src/*.help .

install-exec-local:
	$(INSTALL) -d -m 755 $(prefix)
	$(INSTALL) -m 744 ./src/srvx $(prefix)
	$(INSTALL) -m 644 $(srcdir)/src/*.help $(prefix)
	$(INSTALL) -m 600 $(srcdir)/srvx.conf.example $(prefix)
	$(INSTALL) -m 644 $(srcdir)/sockcheck.conf.example $(prefix)
	@echo
	@echo srvx-$(VERSION) has been installed to $(prefix)
	@echo Remember to edit srvx.conf.example and sockcheck.conf.example
	@echo And of course, ./srvx --help before starting.
	@echo

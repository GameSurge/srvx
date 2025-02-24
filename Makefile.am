EXTRA_DIST = FAQ srvx.conf.example sockcheck.conf.example Makefile.win32
SUBDIRS = @MY_SUBDIRS@
DIST_SUBDIRS = src rx
ACLOCAL_AMFLAGS = -I m4

DISTCLEANFILES = srvx *.help

all-am: srvx

srvx: src/srvx
	cp ./src/srvx $(srcdir)/src/*.help .

install-exec-local:
	$(INSTALL) -d -m 755 $(DESTDIR)$(prefix)
	$(INSTALL) -m 744 ./src/srvx $(DESTDIR)$(prefix)
	$(INSTALL) -m 644 $(srcdir)/src/*.help $(DESTDIR)$(prefix)
	$(INSTALL) -m 600 $(srcdir)/srvx.conf.example $(DESTDIR)$(prefix)
	$(INSTALL) -m 644 $(srcdir)/sockcheck.conf.example $(DESTDIR)$(prefix)
	@echo
	@echo srvx-$(VERSION) has been installed to $(DESTDIR)$(prefix)
	@echo Remember to edit srvx.conf.example and sockcheck.conf.example
	@echo And of course, ./srvx --help before starting.
	@echo

uninstall-local:
	rm $(DESTDIR)$(prefix)/srvx $(DESTDIR)$(prefix)/*.help $(DESTDIR)$(prefix)/*.conf.example

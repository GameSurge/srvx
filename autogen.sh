#! /bin/sh

aclocal
autoheader -Wall
automake -a --gnu Makefile rx/Makefile src/Makefile
autoconf -Wall

#! /bin/sh

libtoolize --automake -c -f
aclocal -Wall
autoheader -Wall
automake --gnu -a -c
autoconf -Wall

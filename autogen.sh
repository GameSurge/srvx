#! /bin/sh

aclocal
autoheader -Wall
automake -a --gnu Makefile rx/Makefile src/Makefile
autoconf -Wall
if [ -d ./src/srvx ] ; then
  echo "WARNING: It looks like you still have the obsolete src/srvx directory."
  echo "Since we try to compile the binary into that place, this will break the"
  echo "compile.  This is probably because you did not \"cvs update\" properly"
  echo "(i.e. with the -P flag).  Since you almost always want to do \"cvs update -P\","
  echo "we suggest you add a line similar to \"update -P\" to your ~/.cvsrc file."
  echo "For example:"
  echo "  echo update -P >> ~/.cvsrc"
  echo "At the very least, \"rm -r src/srvx\" before you try to compile."
fi

dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.53)
AC_INIT(libdisasm, 0.23, bastard-libdisasm@lists.sourceforge.net, libdisasm)
AC_CONFIG_SRCDIR(libdisasm/x86_disasm.c)
AM_INIT_AUTOMAKE

AM_CONFIG_HEADER(config.h)
AM_MAINTAINER_MODE

AC_CANONICAL_HOST
AC_ISC_POSIX
AC_PROG_CC
AM_PROG_CC_STDC

AC_CACHE_SAVE

AC_HEADER_STDC

AM_PROG_LIBTOOL

AC_CACHE_SAVE

dnl Use -Wall if we have gcc.
if test "x$GCC" = "xyes"; then
  case " $CFLAGS " in
  *\ -Wall\ *) ;;
  *) CFLAGS="$CFLAGS -Wall -ggdb" ;;
  esac
fi

AC_CONFIG_FILES([
Makefile
libdisasm/Makefile
x86dis/Makefile
test/Makefile
doc/Makefile
man/Makefile
perl/Makefile
])

AC_OUTPUT


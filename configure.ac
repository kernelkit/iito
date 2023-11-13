AC_PREREQ(2.61)
AC_INIT(iito, 1.0.0, https://github.com/kernelkit/iito/issues)

AC_GNU_SOURCE
AM_INIT_AUTOMAKE(1.11 foreign subdir-objects)
AM_SILENT_RULES(yes)

AC_CONFIG_HEADER(config.h)
AC_CONFIG_FILES([
	Makefile
	src/Makefile
	test/Makefile
])
AC_CONFIG_MACRO_DIRS(m4)

AC_PROG_CC
AC_PROG_INSTALL

AC_SEARCH_LIBS([ev_run], [ev], [], [
	AC_MSG_ERROR([Unable to locate libev])
])
PKG_CHECK_MODULES([libjansson], [jansson >= 2.13.1])
PKG_CHECK_MODULES([libudev], [libudev >= 243])

AC_HEADER_STDC

AC_OUTPUT

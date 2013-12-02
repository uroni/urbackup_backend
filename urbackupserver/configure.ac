#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([urbackupserver], [0.1], [urpc@gmx.de])
AC_CONFIG_SRCDIR([dllmain.cpp])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE

AC_ARG_ENABLE([packaging],
    AS_HELP_STRING([--enable-packaging], [Will be installed for packaging.]))
	
AC_ARG_ENABLE([disable_fortify],
     AS_HELP_STRING([--disable-fortify], [Disable GCC/runtime fortification]))
AM_CONDITIONAL(WITH_FORTIFY, test "x$disable_fortify" != xyes)

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_LIBTOOL

AX_PTHREAD
if !($HAVE_PTHREAD)
then
	echo "Sorry, your system needs the pthread library."
	echo "Either install it or give up."
	exit 1
fi

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for library functions.

if test "x$enable_packaging" != xyes; then
	INSTALL_OPTS="-o urbackup -g urbackup"
	AC_SUBST([INSTALL_OPTS])
fi

AM_CONDITIONAL(PACKAGING, test "x$enable_packaging" = xyes)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

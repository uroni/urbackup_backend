#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([urbackupserver], [0.1], [urpc@gmx.de])
AC_CONFIG_SRCDIR([dllmain.cpp])
AC_CONFIG_HEADER([config.h])
AM_INIT_AUTOMAKE

AC_ARG_ENABLE([packaging],
    AS_HELP_STRING([--enable-packaging], [Will be installed for packaging.]))

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([stdlib.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_SIZE_T

# Checks for library functions.

AM_CONDITIONAL(PACKAGING, test "x$enable_packaging" = xyes)

AC_CONFIG_FILES([Makefile])
AC_OUTPUT

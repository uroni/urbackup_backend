#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([urbackupserver_pychart], [0.1], [urpc@gmx.de])
AC_CONFIG_SRCDIR([dllmain.cpp])
AC_CONFIG_HEADER([config.h])

AM_INIT_AUTOMAKE

AC_ARG_ENABLE([fortify],
     AS_HELP_STRING([--disable-fortify], [Disable GCC/runtime fortification]))
AM_CONDITIONAL(WITH_FORTIFY, test "x$enable_fortify" != xno)

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC
AC_PROG_LIBTOOL

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC

AX_BOOST_BASE([1.38.0])
AX_BOOST_PYTHON

if !($HAVE_BOOST_PYTHON)
then
   echo "Sorry, you need the Python-Lib from Boost."
   echo "Please install from http://www.boost.org"
   exit 1
fi

if test "x$PYTHON_INCLUDE_DIR" = "x"
then
    echo "Please install python-dev"
    exit 2
fi

# Checks for typedefs, structures, and compiler characteristics.
AC_HEADER_STDBOOL
AC_C_CONST
AC_TYPE_SIZE_T

AC_CHECK_HEADERS([stdlib.h stdio.h sys/types.h sys/stat.h errno.h fcntl.h signal.h])

# Checks for library functions.


AC_CONFIG_FILES([Makefile])
AC_OUTPUT

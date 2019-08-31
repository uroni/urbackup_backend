## configure.ac - Autotools configuration file for Crypto++.
##                written and placed in public domain by Jeffrey Walton.
##                based on Debian configure.ac by László Böszörményi.
##

## Visit the link below for the original Debian Autotools files
##   http://sources.debian.net/src/libcrypto++/5.6.4-8/debian/autotools/
##

## TODO
##
## - Figure out how to make Autoconf stop using the fucking C compiler for tasks.
##   The damn tool does not honor AC_PROG_CXX or observe the absence of AC_PROG_CC. It also
##   fails to provide a AC_PROG_NO_CC macro. The damn tool is no better than Cmake.
##
## - Fix Autoconf using the wrong AR and ARFLAGS on platforms like OS X.
##   Also see http://lists.gnu.org/archive/html/bug-autoconf/2017-11/msg00002.html.
##
## - Fix Autoconf ignoring our config.h.in. This one looks like it is a problem without a solution.
##   Also see https://stackoverflow.com/q/48436715/608639
##
## - Stop using local.h, and write the config data into a Crypto++ compatible config.h. There's
##   not much to "Crypto++ compatible config.h". We need test and write preprocessor macros like
##   CRYPTOPP_BIG_ENDIAN, CRYPTOPP_LITTLE_ENDIAN, CRYPTOPP_CXX11, etc. There's just a lot of them.
##   This problem now appears to be closely related to the config.h.in problem. We can't supply a
##   template and expect Autotools to honor it.
##
## - Fix compile tests on Solaris using SunCC. AC_COMPILE_IFELSE returns success even for failure.
##   Also see http://stackoverflow.com/q/47160617/608639.
##
## - Fix compile tests on Linux using xlC. AC_COMPILE_IFELSE returns success even for failure.
##   Also see http://stackoverflow.com/q/47160617/608639.
##
## - Fix compile tests on OS X using LLVM. AC_COMPILE_IFELSE and AC_LINK_IFELSE return success even for failure.
##   Also see http://stackoverflow.com/q/47160617/608639.
##
## - Cut-in a more robust CRYPTOPP_COMPILE_ELSEIF. We have the code at the associated  below; we need
#    to cut it in. Also see http://stackoverflow.com/q/47160617/608639.
##
## - Fix incorrect use of --with-sysroot. Autotools does not seem to honor it when it is set.
##   Also see http://stackoverflow.com/q/47226389/608639
##
## - Fix MSYS2 compile error due to `lt-cryptest.c` and `lt-cryptestcwd.c`. They are C files and
##   don't use necessary C++ flags. They also use Posix functions but fail to define _XOPEN_SOURCE.
##   I'm guessing this has something to do with libtool, but I have no idea how to fix it at
##   the moment. Also see http://lists.gnu.org/archive/html/bug-libtool/2017-11/msg00002.html.
##
## - Add ubsan and asan targets for testing.
##
## - Setup test environment with Dash, which appears to be the lamest shell
##
## - Package Autotools propery, for a regular user's definition of "proper". I don't know what the
##   final deliverables or artifacts are supposed to look like. We also get warnings and errors
##   due to a missing m4/ directory even though we don't use it. Maybe we need to distribute an
##   empty m4/ directory to keep the tools quiet.
##

#############################################################################
AC_INIT([Crypto++], [6.0], [http://www.cryptopp.com/wiki/Bug_Report], [cryptopp], [http://www.cryptopp.com])
AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE

AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_HEADERS([local.h])
AC_CONFIG_FILES([Makefile] [libcryptopp.pc])
AC_CONFIG_SRCDIR([configure.ac])

AC_PROG_LIBTOOL
LT_INIT

AC_PROG_CXX
AC_LANG([C++])
AC_PROG_GREP

## Assert this once to avoid surprises
AC_LANG_ASSERT([C++])

AC_ARG_ENABLE([assertions],
     AS_HELP_STRING([--enable-assertions], [Enable assertions (bug finding).]))
AM_CONDITIONAL(WITH_ASSERTIONS, test "x$enable_assertions" = xyes)

#############################################################################

## Determine the CPU and OS of the machine we are building for.
## Most people call this the target machine, but not Autotools.

AC_CANONICAL_HOST

## These are some variables that help us track platform and compiler.
## It speeds up configuration by adding some intelligence to the
## test selection process. It also avoids failed configurations by
## avoiding some tests that fail but Autotools reports as success.

IS_AIX_OS=`echo $host | $EGREP -i -c -E 'aix'`
IS_SUN_OS=`uname -s | $EGREP -i -c 'SunOS'`
IS_APPLE_OS=`echo "$host" | $EGREP -i -c -E 'apple|darwin'`
IS_ANDROID_OS=`echo $host | $EGREP -i -c -E 'android'`
IS_CYGWIN_OS=`echo $host | $EGREP -i -c -E 'cygwin'`

IS_IA32=`echo $host | $EGREP -i -c -E 'i.86|x86_64|amd64'`
IS_ARM32=`echo $host | $EGREP -i -c 'arm'`
IS_AARCH64=`echo $host | $EGREP -i -c -E 'aarch32|aarch64'`
IS_PPC=`echo $host | $EGREP -i -c -E 'ppc|powerpc|powermac'`
IS_SPARC=`echo $host | $EGREP -i -c -E 'sparc|sparcv8|sparcv9|sparc64'`

#############################################################################

## Determine the compiler's target. GCC '-dumpmachine' is the target,
## so it represents what the compiler is building for (and not what its
## running on). We add '2>/dev/null' because IBM's xlC dumps the man page
## when presented with '-dumpmachine' since it is not a valid option.

## See if CXX is valid. Autotools just trucks on with an invalid one.

if test x"$CXX" != "x"; then
   COMPILER_TARGET=`command -v "$CXX"`
   if test x"$COMPILER_TARGET" = "x"; then
      AC_MSG_FAILURE(["$CXX" is not a valid compiler or is not on-path])
   fi
fi

## Compiler is valid. Try to detect the target and vendor

COMPILER_TARGET=`"$CXX" -dumpmachine 2>/dev/null`

## Determine the compiler's vendor. We need to work around some Autoconf bugs.

COMPILER_VERSION=`"$CXX" --version 2>/dev/null | head -n 1`

## IBM xlC test if COMPILER_VERSION is empty
if test x"$COMPILER_VERSION" = "x"; then
   COMPILER_VERSION=`"$CXX" -qversion 2>/dev/null | head -n 1`
fi

## SunCC test if COMPILER_VERSION is empty
if test x"$COMPILER_VERSION" = "x"; then
   COMPILER_VERSION=`"$CXX" -V 2>&1 | head -n 1`
fi

IS_GCC_COMPILER=`echo $COMPILER_VERSION | $EGREP -i -v 'Clang' | $EGREP -i -c -E 'GCC|G\+\+'`
IS_LLVM_COMPILER=`echo $COMPILER_VERSION | $EGREP -i -c -E 'LLVM|Clang'`
IS_SUN_COMPILER=`echo $COMPILER_VERSION | $EGREP -i -c -E 'Sun C\+\+'`
IS_XLC_COMPILER=`echo $COMPILER_VERSION | $EGREP -i -c -E 'IBM XL C/C\+\+'`
IS_MACPORTS_COMPILER=`echo $COMPILER_VERSION | $EGREP -i -c -E 'MacPorts'`

## Friendly names for reporting
CXXNAME=`basename "$CXX"`
LDNAME=`basename "$LD"`

#############################################################################

## An improved COMPILE_ELSEIF that's not so braindead. Unfortunately, it
## does not work. It has something to do with quoting or double quoting or
## under quoting. Also see http://stackoverflow.com/q/47160617/608639
## and http://lists.gnu.org/archive/html/autoconf/2009-03/msg00099.html.

AC_DEFUN([XXX_COMPILE_ELSEIF],
   [AC_REQUIRE([AC_PROG_EGREP])
   AC_COMPILE_IFELSE([$1],[retval=0
      if $EGREP -i -c -E 'fatal|error|illegal|unrecognized|not found|not exist' conftest.err >/dev/null; then retval=1; fi],[retval=1])
   AS_IF([test $retval = 0],[$2],[$3])]
)

#############################################################################
## Shared and static libraries

if true; then

   AC_MSG_RESULT([checking if static libraries should be enabled... yes])
   AC_ENABLE_STATIC

   ## http://stackoverflow.com/a/21455267/608639
   case $host_os in
      msys* | mingw* | cygwin* )
         AC_MSG_RESULT([checking if shared libraries should be enabled... no])
         enable_shared=no; enable_static=yes;
         ;;
	  *)
         AC_MSG_RESULT([checking if shared libraries should be enabled... yes])
         AC_ENABLE_SHARED
         ;;
   esac
fi

#############################################################################
## Solaris, SunCC and missing optimizations

OPT_ANY=`echo $CXXFLAGS | $EGREP -c -E '\-O0|\-O1|\-O2|\-O3|\-O4|\-O5|\-Os|\-Ofast'`

if test "$OPT_ANY" -eq "0"; then

   infile="in.$RANDOM$RANDOM.cxx"
   outfile="out.$RANDOM$RANDOM"
   echo 'int main(int argc, char* argv[]) {return 0;}' > "$infile"
   echo "" >> "$infile"

   FLAG="-O2"
   AC_MSG_CHECKING([if $CXXNAME supports $FLAG])
   OPT_ERROR=`$CXX $FLAG -o "$outfile" "$infile" 2>&1 | $EGREP -i -c -E 'fatal|error|illegal|unrecognized|not found|not exist'`
   if test "$OPT_ERROR" -eq "0"; then
      echo "yes"
      AM_CXXFLAGS="$AM_CXXFLAGS $FLAG"
   else
      echo "no"
   fi

   if test "$OPT_ERROR" -ne "0"; then
      FLAG="-xO2"
      AC_MSG_CHECKING([if $CXXNAME supports $FLAG])
      OPT_ERROR=`$CXX $FLAG -o "$outfile" "$infile" 2>&1 | $EGREP -i -c -E 'fatal|error|illegal|unrecognized|not found|not exist'`
      if test "$OPT_ERROR" -eq "0"; then
         echo "yes"
         AM_CXXFLAGS="$AM_CXXFLAGS $FLAG"
      else
         echo "no"
      fi
   fi

   rm -f "$infile" "$outfile" 1>/dev/null 2>&1
fi

#############################################################################
## pthreads for thread local storage

AC_ARG_ENABLE(tls,
   AS_HELP_STRING([--enable-tls], [enable thread storage (default is yes)]),
   ac_enable_tls=$enableval,
   ac_enable_tls=yes)
AM_CONDITIONAL(HAS_PTHREADS, test $ac_enable_tls = yes)

## Hack: work around AC_COMPILE_IFELSE and AC_LINK_IFELSE misdetections
if test "$ac_enable_tls" = "yes"; then
if test "$IS_SUN_COMPILER" -eq "0"; then
if test "$IS_XLC_COMPILER" -eq "0"; then

   SAVED_CXXFLAGS="$CXXFLAGS"
   CXXFLAGS="-pthread"
   AC_MSG_CHECKING([for C++ compiler pthread support])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes])],
      [AC_MSG_FAILURE(["--enable-tls=yes but pthreads are not available"])]
   )
   AC_MSG_CHECKING([for linker pthread support])
   AC_LINK_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes])],
      [AC_MSG_FAILURE(["--enable-tls=yes but pthreads are not available"])]
   )
   # AC_MSG_FAILURE stops the configure process
   AM_CXXFLAGS="$AM_CXXFLAGS -pthread"
   AM_LDFLAGS="$AM_LDFLAGS -pthread"
   CXXFLAGS="$SAVED_CXXFLAGS"
fi
fi
fi

#############################################################################
## Big endian/little endian

AC_C_BIGENDIAN(
  AC_DEFINE(CRYPTOPP_BIG_ENDIAN, 1, [machine is big-endian]),
  AC_DEFINE(CRYPTOPP_LITTLE_ENDIAN, 1, [machine is little-endian]),
  AC_MSG_ERROR(unknown endianess),
  AC_MSG_ERROR(universial endianess not supported)
)

#############################################################################
## Fix AR on iOS and OS X

## AR and ARFLAGS usage is currently broke under Autotools. Also see
## http://lists.gnu.org/archive/html/bug-autoconf/2017-11/msg00002.html

#if test "$IS_APPLE_OS" -ne "0"; then
#   AC_SUBST([AR], ["/usr/bin/libtool"])
#   AC_SUBST([ARFLAGS], ["-static -o"])
#   AC_SUBST([ac_ct_AR], ["/usr/bin/libtool"])
#fi

#############################################################################
## Dead code stripping

## Hack: work around AC_COMPILE_IFELSE and AC_LINK_IFELSE misdetections
if true; then
if test "$IS_AIX_OS" -eq "0"; then
if test "$IS_SUN_COMPILER" -eq "0" && test "$IS_XLC_COMPILER" -eq "0"; then

   ## Save FLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"
   SAVED_LDFLAGS="$LDFLAGS"

   CXXFLAGS="-fdata-sections"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
   fi

   CXXFLAGS="-ffunction-sections"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
   fi

   ## -Wl,-dead_strip is an OS X.
   if test "$IS_APPLE_OS" -ne "0"; then

      LDFLAGS="-Wl,-dead_strip"
      AC_MSG_CHECKING([if $LDNAME supports $LDFLAGS])
      AC_LINK_IFELSE([AC_LANG_PROGRAM([])],
         [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
         [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
      )

      if test "$tr_RESULT" -eq "1"; then
         AM_LDFLAGS="$AM_LDFLAGS $LDFLAGS"
      fi

   else

      LDFLAGS="-Wl,--gc-sections"
      AC_MSG_CHECKING([if $LDNAME supports $LDFLAGS])
      AC_LINK_IFELSE([AC_LANG_PROGRAM([])],
         [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
         [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
      )

      if test "$tr_RESULT" -eq "1"; then
         AM_LDFLAGS="$AM_LDFLAGS $LDFLAGS"
      fi

   fi

   ## Restore FLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
   LDFLAGS="$SAVED_LDFLAGS"
fi
fi
fi

#############################################################################
## Solaris, GCC and -Wa,--divide

if test "$IS_SUN_OS" -ne "0" && test "$IS_SUN_COMPILER" -eq "0"; then

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"

   CXXFLAGS="-Wa,--divide"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi

#############################################################################
## Newlib and _XOPEN_SOURCE

if true; then

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"

   ## This test may need to be changed if the Newlib folks provide directions.
   ## Also see http://www.sourceware.org/ml/newlib/2017/msg01095.html.

   AC_MSG_CHECKING([if runtime library is Newlib])
   AC_PREPROC_IFELSE([AC_LANG_PROGRAM([#include <_newlib_version.h>])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then

      CXXFLAGS="-D_XOPEN_SOURCE=600"
      AC_MSG_CHECKING([if runtime library supports $CXXFLAGS])
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
         [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
         [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
      )

      if test "$tr_RESULT" -eq "1"; then
         AC_DEFINE([_XOPEN_SOURCE], [600],
            [Define to 600 if you are using Newlib as your runtime library.])
         AM_CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
      fi
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi

#############################################################################
## MacPorts GCC using Clang integrated assembler

## Modern GCC must use Clang as the assembler. The old
## Apple assembler cannot handle much more than SSE3 or SSE4

if test "$IS_APPLE_OS" -ne "0"; then
if test "$IS_GCC_COMPILER" -ne "0" && test "$IS_MACPORTS_COMPILER" -ne "0"; then

   HAVE_OPT=`echo $CXXFLAGS | $EGREP -i -c -E '\-Wa,-q'`
   if test "$HAVE_OPT" -eq "0"; then

      ## Save CXXFLAGS
      SAVED_CXXFLAGS="$CXXFLAGS"

      CXXFLAGS="-Wa,-q"
      AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
         [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
         [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
      )

      if test "$tr_RESULT" -eq "1"; then
         AM_CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
      fi

      HAVE_OPT=`echo $CXXFLAGS | $EGREP -i -c -E '\-DCRYPTOPP_CLANG_INTEGRATED_ASSEMBLER'`
      if test "$tr_RESULT" -eq "1"; then
         AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_CLANG_INTEGRATED_ASSEMBLER=1"
         AC_DEFINE([CRYPTOPP_CLANG_INTEGRATED_ASSEMBLER], [1],
            [Define to 1 if you are using the Clang integrated assembler.])
      fi

      ## Restore CXXFLAGS
      CXXFLAGS="$SAVED_CXXFLAGS"
   fi
fi
fi

#############################################################################
## Solaris, map file and libraries

if test "$IS_SUN_OS" -ne "0"; then

   ## Save LDFLAGS
   SAVED_LDFLAGS="$LDFLAGS"

   ## IA-32 gets a Mapfile for capabilities. Due to libtool stripping
   ## -M arguments, we need to escape it with -XCClinker in AM_LDFLAGS.
   if test "$IS_IA32" -ne "0"; then
      LDFLAGS="-Mcryptopp.mapfile"
      AC_MSG_CHECKING([if $LDNAME supports $LDFLAGS])
      AC_LINK_IFELSE([AC_LANG_PROGRAM([])],
         [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
         [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
      )

      if test "$tr_RESULT" -eq "1"; then
         AM_LDFLAGS="$AM_LDFLAGS -XCClinker -Mcryptopp.mapfile"
      fi
   fi

   LDFLAGS="-lnsl"
   AC_MSG_CHECKING([if $LDNAME supports $LDFLAGS])
   AC_LINK_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_LDFLAGS="$AM_LDFLAGS $LDFLAGS"
   fi

   LDFLAGS="-lsocket"
   AC_MSG_CHECKING([if $LDNAME supports $LDFLAGS])
   AC_LINK_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_LDFLAGS="$AM_LDFLAGS $LDFLAGS"
   fi

   ## Restore LDFLAGS
   LDFLAGS="$SAVED_LDFLAGS"
fi

#############################################################################
## Solaris, SunCC compiler options

if test "$IS_SUN_OS" -ne "0" && test "$IS_SUN_COMPILER" -ne "0"; then

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"

   CXXFLAGS="-template=no%extdef"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi

#############################################################################
## SPARC (Solaris), SunCC compiler options

## TODO: Does GCC need an equivalent to -xmemalign=4i?

if test "$IS_SUN_OS" -ne "0" && test "$IS_SUN_COMPILER" -ne "0"; then
if test "$IS_SPARC" -ne "0"; then

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"

   CXXFLAGS="-xmemalign=4i"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi
fi

#############################################################################
## IA-32, i386, i586, i686, x86_64, etc.

## We are kind of screwed on some platforms because AC_COMPILE_IFELSE fails to
## detect "illegal option". The SunCC compiler literally generates and prints
## the message, but Autoconf ignores it and reports the option is valid.

## This block handles GCC and friends. i86pc and SunCC is handled after this block
if test "$IS_IA32" -ne "0" && test "$IS_SUN_COMPILER" -eq "0"; then

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"

   CXXFLAGS="-msse2"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_SSE_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_SSE2_AVAILABLE], [1],
         [Define to 1 if you have IA-32 SSE2 instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_ASM], [1],
         [Define to 1 if you do not have IA-32 SSE2 instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_ASM"
   fi

   CXXFLAGS="-mssse3"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_ARIA_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SIMON_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SPECK_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_SSSE3_AVAILABLE], [1],
         [Define to 1 if you have IA-32 SSSE3 instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_SSSE3], [1],
         [Define to 1 if you do not have IA-32 SSSE3 instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_SSSE3"
   fi

   CXXFLAGS="-msse4.1"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_BLAKE2_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SIMON_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SPECK_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_SSE41_AVAILABLE], [1],
         [Define to 1 if you have IA-32 SSE4.1 instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_SSE4], [1],
         [Define to 1 if you do not have IA-32 SSE4.1 instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_SSE4"
   fi

   CXXFLAGS="-msse4.2"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_CRC_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_SSE42_AVAILABLE], [1],
         [Define to 1 if you have IA-32 SSE4.2 instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_SSE4], [1],
         [Define to 1 if you do not have IA-32 SSE4.2 instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_SSE4"
   fi

   CXXFLAGS="-mssse3 -mpclmul"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_GCM_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_CLMUL_AVAILABLE], [1],
         [Define to 1 if you have IA-32 CLMUL instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_CLMUL], [1],
         [Define to 1 if you do not have IA-32 CLMUL instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_CLMUL"
   fi

   CXXFLAGS="-msse4.1 -maes"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_AES_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_AESNI_AVAILABLE], [1],
         [Define to 1 if you have IA-32 AES-NI instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_AESNI], [1],
         [Define to 1 if you do not have IA-32 AES-NI instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_AESNI"
   fi

   CXXFLAGS="-msse4.2 -msha"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_SHA_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_SHANI_AVAILABLE], [1],
         [Define to 1 if you have IA-32 SHA instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_SHANI], [1],
         [Define to 1 if you do not have IA-32 SHA instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_SHANI"
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi

## This block handles i86pc and SunCC. GCC and friends were handled before this block
if test "$IS_IA32" -ne "0" && test "$IS_SUN_COMPILER" -ne "0"; then

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"
   ## SunCC does not pass GCC-like defines through the compiler
   SSE_DEFINES=""

   CXXFLAGS="-xarch=sse2"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_SSE_FLAG], [$CXXFLAGS])
      SSE_DEFINES="$SSE_DEFINES -D__SSE2__=1"
      AC_DEFINE([CRYPTOPP_SSE2_AVAILABLE], [1],
         [Define to 1 if you have IA-32 SSE2 instructions available.])
      AM_LDFLAGS="$AM_LDFLAGS -xarch=sse2"
   else
      AC_DEFINE([CRYPTOPP_DISABLE_ASM], [1],
         [Define to 1 if you do not have IA-32 SSE2 instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_ASM"
   fi

   CXXFLAGS="-xarch=ssse3"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_ARIA_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SIMON_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SPECK_FLAG], [$CXXFLAGS])
      SSE_DEFINES="$SSE_DEFINES -D__SSSE3__=1"
      AC_DEFINE([CRYPTOPP_SSSE3_AVAILABLE], [1],
         [Define to 1 if you have IA-32 SSSE3 instructions available.])
      AM_LDFLAGS="$AM_LDFLAGS -xarch=ssse3"
   else
      AC_DEFINE([CRYPTOPP_DISABLE_SSSE3], [1],
         [Define to 1 if you do not have IA-32 SSSE3 instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_SSSE3"
   fi

   CXXFLAGS="-xarch=sse4_1"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      SSE_DEFINES="$SSE_DEFINES -D__SSE4_1__=1"
      AC_DEFINE([CRYPTOPP_SSE41_AVAILABLE], [1],
         [Define to 1 if you have IA-32 SSE4.1 instructions available.])
      AM_LDFLAGS="$AM_LDFLAGS -xarch=sse4_1"
   else
      AC_DEFINE([CRYPTOPP_DISABLE_SSE4], [1],
         [Define to 1 if you do not have IA-32 SSE4.1 instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_SSE4"
   fi

   CXXFLAGS="-xarch=sse4_2"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_CRC_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_BLAKE2_FLAG], [$CXXFLAGS])
      SSE_DEFINES="$SSE_DEFINES -D__SSE4_2__=1"
      AC_DEFINE([CRYPTOPP_SSE42_AVAILABLE], [1],
         [Define to 1 if you have IA-32 SSE42 instructions available.])
      AM_LDFLAGS="$AM_LDFLAGS -xarch=sse4_2"
   else
      AC_DEFINE([CRYPTOPP_DISABLE_SSE4], [1],
         [Define to 1 if you do not have IA-32 SSE4.2 instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_SSE4"
   fi

   CXXFLAGS="-xarch=aes"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_GCM_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_AES_FLAG], [$CXXFLAGS])
      SSE_DEFINES="$SSE_DEFINES -D__AES__=1"
      AC_DEFINE([CRYPTOPP_CLMUL_AVAILABLE], [1],
         [Define to 1 if you have IA-32 CLMUL instructions available.])
      AC_DEFINE([CRYPTOPP_AESNI_AVAILABLE], [1],
         [Define to 1 if you have IA-32 AES-NI instructions available.])
      AM_LDFLAGS="$AM_LDFLAGS -xarch=aes"
   else
      AC_DEFINE([CRYPTOPP_DISABLE_CLMUL], [1],
         [Define to 1 if you do not have IA-32 CLMUL instructions available.])
      AC_DEFINE([CRYPTOPP_DISABLE_AESNI], [1],
         [Define to 1 if you do not have IA-32 AES-NI instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_AESNI"
   fi

## Hack: disable -xarch=sha for the moment; enable the defines.
## Its too bad we cannot use AC_COMPILE_ELSEIF to perform the test.
if false; then
   CXXFLAGS="-xarch=sha"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_SHA_FLAG], [$CXXFLAGS])
      SSE_DEFINES="$SSE_DEFINES -D__SHA__=1"
      AC_DEFINE([CRYPTOPP_SHA_AVAILABLE], [1],
         [Define to 1 if you have IA-32 SHA-NI instructions available.])
      AM_LDFLAGS="$AM_LDFLAGS -xarch=sha"
   else
      AC_DEFINE([CRYPTOPP_DISABLE_SHANI], [1],
         [Define to 1 if you do not have IA-32 SHA instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_SHANI"
   fi
else
   AC_SUBST([CRYPTOPP_SHA_FLAG])
   AC_DEFINE([CRYPTOPP_DISABLE_SHANI], [1],
      [Define to 1 if you do not have IA-32 SHA instructions available.])
   AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_SHANI"
fi

   if test x"$SSE_DEFINES" != "x"; then
      AM_CXXFLAGS="$AM_CXXFLAGS $SSE_DEFINES"
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi

#############################################################################
## ARM A-32, including NEON, soft floats and hard floats

if test "$IS_ARM32" -ne "0" && test "$IS_ANDROID_OS" -eq "0"; then

   ## Most Linux are hard-float systems.
   IS_ARMHF=`echo $host | $EGREP -i -c -E 'armhf|arm7l|gnueabihf'`
   if test "$IS_ARMHF" -ne "0"; then
      AC_MSG_NOTICE([ARM hard-floats detected, using -mfloat-abi=hard.])
      FP_FLAG="hard"
   else
      AC_MSG_NOTICE([ARM soft-floats detected, using -mfloat-abi=softfp.])
      FP_FLAG="softfp"
   fi

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"

   CXXFLAGS="-march=armv7-a -mfloat-abi=$FP_FLAG -mfpu=neon"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_ARIA_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_BLAKE2_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_GCM_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_NEON_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SIMON_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SPECK_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_ARM_NEON_AVAILABLE], [1],
         [Define to 1 if you have the NEON instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_ASM], [1],
         [Define to 1 if you do not have ARM NEON instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_ASM"
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi

#############################################################################
## ARM Aarch32 and Aarch64

if test "$IS_AARCH64" -ne "0"; then

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"

   ## aria-simd.cpp, blake2-simd.cpp, neon-simd.cpp
   CXXFLAGS="-march=armv8-a"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_ARIA_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_BLAKE2_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_NEON_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SIMON_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SPECK_FLAG], [$CXXFLAGS])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_ASM], [1],
         [Define to 1 if you do not have ARM NEON instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_ASM"
   fi

   ## crc-simd.cpp
   CXXFLAGS="-march=armv8-a+crc"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_CRC_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_ARM_CRC32_AVAILABLE], [1],
         [Define to 1 if you have ARMv8-a CRC32 instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_CRC32], [1],
         [Define to 1 if you do not have ARMv8-a CRC32 instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_ARM_CRC32"
   fi

   ## gcm-simd.cpp, rijndael-simd.cpp, sha-simd.cpp
   CXXFLAGS="-march=armv8-a+crypto"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_GCM_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_AES_FLAG], [$CXXFLAGS])
      AC_SUBST([CRYPTOPP_SHA_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_ARM_PMULL_AVAILABLE], [1],
         [Define to 1 if you have ARMv8-a PMULL instructions available.])
      AC_DEFINE([CRYPTOPP_ARM_AES_AVAILABLE], [1],
         [Define to 1 if you have ARMv8-a AES instructions available.])
      AC_DEFINE([CRYPTOPP_ARM_SHA_AVAILABLE], [1],
         [Define to 1 if you have ARMv8-a SHA instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_ARM_PMULL], [1],
         [Define to 1 if you do not have ARMv8-a PMULL instructions available.])
      AC_DEFINE([CRYPTOPP_DISABLE_ARM_AES], [1],
         [Define to 1 if you do not have ARMv8-a AES instructions available.])
      AC_DEFINE([CRYPTOPP_DISABLE_ARM_SHA], [1],
         [Define to 1 if you do not have ARMv8-a SHA instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_ARM_PMULL"
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_ARM_AES"
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_ARM_SHA"
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi

#############################################################################
## IBC XL C/C++ compiler, applies to IA-32 and PPC

if test "$IS_XLC_COMPILER" -ne "0"; then

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"

   ## -qrtti must be present for some xlC compilers
   HAS_RTTI=`echo $CPPFLAGS $CXXFLAGS | $EGREP -i -c -E '\-qrtti'`
   if test "$HAS_RTTI" -eq "0"; then

      CXXFLAGS="-qrtti"
      AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
         [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
         [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
      )

      if test "$tr_RESULT" -eq "1"; then
         AM_CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
      fi
   fi

   # Promotes optimizations
   CXXFLAGS="-qmaxmem=-1"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi

#############################################################################
## PowerPC and PowerPC-64

if test "$IS_PPC" -ne "0"; then

   ## Hack: work around AC_COMPILE_IFELSE and AC_LINK_IFELSE misdetections
   if test "$IS_XLC_COMPILER" -ne "0"; then
      PPC_POWER4_FLAG="-qarch=pwr4 -qaltivec"
      PPC_POWER7_FLAG="-qarch=pwr7 -qaltivec"
      PPC_POWER8_FLAG="-qarch=pwr8 -qaltivec"
   else
      PPC_POWER4_FLAG="-mcpu=power4 -maltivec"
      PPC_POWER7_FLAG="-mcpu=power7 -maltivec"
      PPC_POWER8_FLAG="-mcpu=power8 -maltivec"
   fi

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"

   ## ppc-simd.cpp, gcm-simd.cpp, rijndael-simd.cpp, sha-simd.cpp
   ## Altivec needs Power4, but the VSX loads and stores needs Power7.
   CXXFLAGS="$PPC_POWER4_FLAG"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_PPC_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_ALTIVEC_AVAILABLE], [1],
         [Define to 1 if you have Altivec instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_ALTIVEC], [1],
         [Define to 1 if you do not have the Altivec instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_ALTIVEC=1"
   fi

   CXXFLAGS="$PPC_POWER7_FLAG"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AC_SUBST([CRYPTOPP_PPC_FLAG], [$CXXFLAGS])
      AC_DEFINE([CRYPTOPP_POWER7_AVAILABLE], [1],
         [Define to 1 if you have Power7 instructions available.])
   else
      AC_DEFINE([CRYPTOPP_DISABLE_POWER7], [1],
         [Define to 1 if you do not have the Altivec instructions available.])
      AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_POWER7=1"
   fi

   ## Power7 and Altivec is available. Test for Power8 and Crypto extensions.
   if test "$tr_RESULT" -eq "1"; then

      CXXFLAGS="$PPC_POWER8_FLAG"
      AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
      AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
         [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
         [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
      )

      if test "$tr_RESULT" -eq "1"; then
         AC_SUBST([CRYPTOPP_PPC_FLAG], [$CXXFLAGS])
         AC_SUBST([CRYPTOPP_CRC_FLAG], [$CXXFLAGS])
         AC_SUBST([CRYPTOPP_AES_FLAG], [$CXXFLAGS])
         AC_SUBST([CRYPTOPP_SHA_FLAG], [$CXXFLAGS])
         AC_DEFINE([CRYPTOPP_POWER8_AVAILABLE], [1],
            [Define to 1 if you have POWER8 instructions available.])
         AC_DEFINE([CRYPTOPP_POWER8_CRC_AVAILABLE], [1],
            [Define to 1 if you have POWER8 Crypto instructions available.])
         AC_DEFINE([CRYPTOPP_POWER8_AES_AVAILABLE], [1],
            [Define to 1 if you have POWER8 Crypto instructions available.])
         AC_DEFINE([CRYPTOPP_POWER8_SHA_AVAILABLE], [1],
            [Define to 1 if you have POWER8 Crypto instructions available.])
      else
         AC_DEFINE([CRYPTOPP_DISABLE_POWER8], [1],
            [Define to 1 if you do not have the Power8 instructions available.])
         AC_DEFINE([CRYPTOPP_DISABLE_POWER8_CRC], [1],
            [Define to 1 if you do not have the Power8 PMULL instructions available.])
         AC_DEFINE([CRYPTOPP_DISABLE_POWER8_AES], [1],
            [Define to 1 if you do not have the Power8 AES instructions available.])
         AC_DEFINE([CRYPTOPP_DISABLE_POWER8_SHA], [1],
            [Define to 1 if you do not have the Power8 SHA instructions available.])
         AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_POWER8=1"
         AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_POWER8_CRC=1"
         AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_POWER8_AES=1"
         AM_CXXFLAGS="$AM_CXXFLAGS -DCRYPTOPP_DISABLE_POWER8_SHA=1"
      fi
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi

#############################################################################
## Other fodder

## Add -pipe for non-IoT gadgets to speedup the compile. IoT gadgets, like
## BeagleBoards and CubieTrucks, don't have swap files and run out of memory.
## Hack: work around AC_COMPILE_IFELSE and AC_LINK_IFELSE misdetections

if test "$IS_IA32" -ne "0" || test "$IS_AARCH64" -ne "0" || test "$IS_PPC" -ne "0"; then
if test "$IS_SUN_COMPILER" -eq "0"; then
if test "$IS_XLC_COMPILER" -eq "0"; then

   ## Save CXXFLAGS
   SAVED_CXXFLAGS="$CXXFLAGS"

   CXXFLAGS="-pipe"
   AC_MSG_CHECKING([if $CXXNAME supports $CXXFLAGS])
   AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
   fi

   ## Restore CXXFLAGS
   CXXFLAGS="$SAVED_CXXFLAGS"
fi
fi
fi

## Add -Wl,--exclude-libs,ALL for GNU linker to stops us from re-exporting
## symbols in other libraries. This keeps our ELF section header smaller.
## Hack: work around AC_COMPILE_IFELSE and AC_LINK_IFELSE misdetections

if test "$enable_shared" = "yes"; then
if test "$IS_APPLE_OS" -eq "0"; then
if test "$IS_SUN_OS" -eq "0"; then
if test "$IS_AIX_OS" -eq "0"; then

   SAVED_LDFLAGS="$LDFLAGS"
   LDFLAGS="-Wl,--exclude-libs,ALL"

   AC_MSG_CHECKING([if $LDNAME supports $LDFLAGS])
   AC_LINK_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_LDFLAGS="$AM_LDFLAGS $LDFLAGS"
   fi

   LDFLAGS="$SAVED_LDFLAGS"
fi
fi
fi
fi

## Add -Wl,--as-needed for GNU linker to avoid linking unneeded libraries.
## Hack: work around AC_COMPILE_IFELSE and AC_LINK_IFELSE misdetections

if test "$IS_APPLE_OS" -eq "0"; then
if test "$IS_SUN_OS" -eq "0"; then
if test "$IS_AIX_OS" -eq "0"; then

   SAVED_LDFLAGS="$LDFLAGS"
   LDFLAGS="-Wl,--as-needed"

   AC_MSG_CHECKING([if $LDNAME supports $LDFLAGS])
   AC_LINK_IFELSE([AC_LANG_PROGRAM([])],
      [AC_MSG_RESULT([yes]); AC_SUBST([tr_RESULT], [1])],
      [AC_MSG_RESULT([no]);  AC_SUBST([tr_RESULT], [0])]
   )

   if test "$tr_RESULT" -eq "1"; then
      AM_LDFLAGS="$AM_LDFLAGS $LDFLAGS"
   fi

   LDFLAGS="$SAVED_LDFLAGS"
fi
fi
fi

cat << EOT >> local.h
typedef unsigned char byte;
typedef unsigned short word16;
typedef unsigned int word32;

#if defined(_MSC_VER) || defined(__BORLANDC__)
	typedef unsigned __int64 word64;
	#define W64LIT(x) x##ui64
#elif (_LP64 || __LP64__)
	typedef unsigned long word64;
	#define W64LIT(x) x##UL
#else
	typedef unsigned long long word64;
	#define W64LIT(x) x##ULL
#endif

// define large word type, used for file offsets and such
typedef word64 lword;
const lword LWORD_MAX = W64LIT(0xffffffffffffffff);
EOT

## Without this 'make' uses out GNUmakefile, and not Makefile
AC_CONFIG_COMMANDS_PRE([rm -f GNUmakefile 2>/dev/null])
AC_CONFIG_COMMANDS_PRE([rm -f GNUmakefile-cross 2>/dev/null])

## Write the Automake flags we set
AC_SUBST([AM_CXXFLAGS])
AC_SUBST([AM_LDFLAGS])

## Write the output artifacts
AC_OUTPUT

## Automake bug workaround. If libcryptopp_la_SOURCES is an empty list, Automake assumes
## C source files and drive link through the C compiler. We provide the empty adhoc.cpp
## to get things back on course, so adhoc.cpp must always be copied.

AC_CONFIG_COMMANDS_POST([cp adhoc.cpp.proto adhoc.cpp])

#############################################################################
## Print a summary of information

echo ""
echo "***********************************************************************"

echo "Auto-configuration complete. A summary of options are below. If"
echo "something looks wrong then please modify config.h and please report"
echo "it at http://github.com/noloader/cryptopp-autotools."
echo ""

echo "   Build triplet: $host"
echo " Compiler target: $COMPILER_TARGET"
echo "Compiler version: $COMPILER_VERSION"
echo ""

echo "Static library: $enable_static"
echo "Shared library: $enable_shared"
echo ""

if test x"$CRYPTOPP_SSE_FLAG" != "x"; then
   echo "CRYPTOPP_SSE_FLAG: $CRYPTOPP_SSE_FLAG"
fi
if test x"$CRYPTOPP_NEON_FLAG" != "x"; then
   echo "CRYPTOPP_NEON_FLAG: $CRYPTOPP_NEON_FLAG"
fi
if test x"$CRYPTOPP_PPC_FLAG" != "x"; then
   echo "CRYPTOPP_PPC_FLAG: $CRYPTOPP_PPC_FLAG"
fi
if test x"$CRYPTOPP_ARIA_FLAG" != "x"; then
   echo "CRYPTOPP_ARIA_FLAG: $CRYPTOPP_ARIA_FLAG"
fi
if test x"$CRYPTOPP_BLAKE2_FLAG" != "x"; then
   echo "CRYPTOPP_BLAKE2_FLAG: $CRYPTOPP_BLAKE2_FLAG"
fi
if test x"$CRYPTOPP_CRC_FLAG" != "x"; then
   echo "CRYPTOPP_CRC_FLAG: $CRYPTOPP_CRC_FLAG"
fi
if test x"$CRYPTOPP_GCM_FLAG" != "x"; then
   echo "CRYPTOPP_GCM_FLAG: $CRYPTOPP_GCM_FLAG"
fi
if test x"$CRYPTOPP_AES_FLAG" != "x"; then
   echo "CRYPTOPP_AES_FLAG: $CRYPTOPP_AES_FLAG"
fi
if test x"$CRYPTOPP_SHA_FLAG" != "x"; then
   echo "CRYPTOPP_SHA_FLAG: $CRYPTOPP_SHA_FLAG"
fi
if test x"$CRYPTOPP_SPECK_FLAG" != "x"; then
   echo "CRYPTOPP_SPECK_FLAG: $CRYPTOPP_SPECK_FLAG"
fi
if test x"$CRYPTOPP_SIMON_FLAG" != "x"; then
   echo "CRYPTOPP_SIMON_FLAG: $CRYPTOPP_SIMON_FLAG"
fi

echo ""
echo "Automake flags (can be overridden by user flags):"
echo "AM_CXXFLAGS: $AM_CXXFLAGS"
echo "AM_LDFLAGS: $AM_LDFLAGS"

echo ""
echo "User flags (overrides Automake flags on conflict):"
echo "CXXFLAGS: $CXXFLAGS"
echo "LDFLAGS: $LDFLAGS"

OPT_SYM=`echo $CPPFLAGS $CXXFLAGS | $EGREP -c -E '\-g'`
if test "$OPT_SYM" -eq "0"; then
   echo ""
   echo "CPPFLAGS does not include -g. You should consider building with -g"
   echo "to add debug symbols in case you have to analyze a crash."
fi

OPT_O3=`echo $CXXFLAGS | $EGREP -c -E '\-O3|\-O4|\-O5|\-Os|\-Ofast'`
if test "$OPT_O3" -eq "0"; then
   echo ""
   echo "CXXFLAGS does not include -O3. You should consider building at -O3"
   echo "to engage compiler vectorizations and enhance performance."
fi

HAVE_WIDTH=`echo $CXXFLAGS | $EGREP -i -c -E '\-m32|\-m64|\-q32|\-q64|\-march=native|\-native|\-arch x86_64|\-arch arm64'`
IS_64BITS=`getconf LONG_BIT 2>&1 | $EGREP -i -c '64'`
if test "$IS_64BITS" -eq "0"; then
   IS_64BITS=`isainfo -b 2>/dev/null | $EGREP -i -c -E -E '64'`
fi
if test "$IS_64BITS" -eq "0"; then
   IS_64BITS=`getconf KERNEL_BITMODE 2>/dev/null | $EGREP -i -c -E '64'`
fi

## Don't print message for Aarch64
if test "$IS_AARCH64" -eq "0"; then
if test "$IS_64BITS" -ne "0" && test "$HAVE_WIDTH" -eq "0"; then
   echo ""
   echo "The machine is 64-bit capable but -m64 is not present in CXXFLAGS"
fi
fi

echo "***********************************************************************"
echo ""

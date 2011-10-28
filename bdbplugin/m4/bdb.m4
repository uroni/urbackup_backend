dnl -*- mode: autoconf -*-
dnl Copyright 2010 Vivien Malerba
dnl
dnl SYNOPSIS
dnl
dnl   BDB_CHECK([libdirname])
dnl
dnl   [libdirname]: defaults to "lib". Can be overridden by the --with-bdb-libdir-name option
dnl
dnl DESCRIPTION
dnl
dnl   This macro tries to find the Bdb libraries and header files. If the BDB setup is found
dnl   then it also tries to find the SQL extension to BDB (starting from version 5.0)
dnl
dnl   It defines two options:
dnl   --with-bdb=yes/no/<directory>
dnl   --with-bdb-libdir-name=<dir. name>
dnl
dnl   If the 1st option is "yes" then the macro in several well known directories
dnl
dnl   If the 1st option is "no" then the macro does not attempt at locating the
dnl   bdb package
dnl
dnl   If the 1st option is a drectory name, then the macro tries to locate the bdb package
dnl   in the specified directory.
dnl
dnl   If the macro has to try to locate the bdb package in one or more directories, it will
dnl   try to locate the header files in $dir/include and the library files in $dir/lib, unless
dnl   the second option is used to specify a directory name to be used instead of "lib" (for
dnl   example lib64).
dnl
dnl USED VARIABLES
dnl
dnl   $linklibext: contains the library suffix (like ".so"). If not specified ".so" is used.
dnl   $platform_win32: contains "yes" on Windows platforms. If not specified, assumes "no"
dnl
dnl
dnl DEFINED VARIABLES
dnl
dnl   This macro always calls:
dnl
dnl    AC_SUBST(BDB_LIBS)
dnl    AC_SUBST(BDB_LIB)
dnl    AC_SUBST(BDB_CFLAGS)
dnl    AC_SUBST(LIBGDA_BDB_INC)
dnl    AC_SUBST(LIBGDA_BDB_TYPE)
dnl    bdb_found=yes/no
dnl    bdbsql_found=yes/no
dnl    AC_SUBST(BDBSQL_LIBS)
dnl    AC_SUBST(BDBSQL_CFLAGS)
dnl
dnl   and if the bdb package is found:
dnl
dnl    AM_CONDITIONAL(BDB, true)
dnl    AM_CONDITIONAL(BDBSQL, true)
dnl
dnl
dnl LICENSE
dnl
dnl This file is free software; the author(s) gives unlimited
dnl permission to copy and/or distribute it, with or without
dnl modifications, as long as this notice is preserved.
dnl

m4_define([_BDB_CHECK_INTERNAL],
[
    AC_BEFORE([AC_PROG_LIBTOOL],[$0])dnl setup libtool first
    AC_BEFORE([AM_PROG_LIBTOOL],[$0])dnl setup libtool first
    AC_BEFORE([LT_INIT],[$0])dnl setup libtool first

    bdb_loclibdir=$1
    if test "x$bdb_loclibdir" = x
    then
        if test "x$platform_win32" = xyes
	then
	    bdb_loclibdir=bin
	else
	    bdb_loclibdir=lib
	fi
    fi

    # determine if Bdb should be searched for
    # and use pkg-config if the "yes" option is used
    bdb_found=no
    try_bdb=true
    BDB_LIBS=""
    bdb_test_dir="/usr /usr/local /local"
    AC_ARG_WITH(bdb,
              AS_HELP_STRING([--with-bdb[=@<:@yes/no/<directory>@:>@]],
                             [Locate Berkeley DB files]),[
			     if test $withval = no
			     then
			         try_bdb=false
			     elif test $withval != yes
			     then
			         bdb_test_dir=$withval
			     fi])
    AC_ARG_WITH(bdb-libdir-name,
              AS_HELP_STRING([--with-bdb-libdir-name[=@<:@<dir. name>@:>@]],
                             [Locate BDB library file, related to the prefix specified from --with-bdb]),
			     [bdb_loclibdir=$withval])

    # try to locate files
    if test $try_bdb = true
    then
	if test "x$linklibext" = x
	then
	    bdb_libext=".so"
	else
	    bdb_libext="$linklibext"
	fi
	bdbdir=""
	for d in $bdb_test_dir
	do
	    bdbdir=""
	    AC_MSG_CHECKING([for Berkeley DB files in $d])
	    for version in "" 5.1 5.0 4.9 4.8 4.7
	    do
	        if test $platform_win32 = yes
		then
		    sversion=`echo $version | sed -e 's,\.,,g'`
		else
		    sversion=`echo $version | sed -e 's,\..*,,g'`
		fi
		if test -z $version
		then
		    db_libfilename="libdb$bdb_libext"
        	    db_lib="-ldb"
		    db_libfile="$d/$bdb_loclibdir/libdb$bdb_libext"
		    try_headers="db.h"
    		else
		    if test $platform_win32 = yes
		    then
		        db_libfilename="libdb$sversion$bdb_libext"
        	        db_lib="-ldb$sversion"
		        db_libfile="$d/$bdb_loclibdir/libdb$sversion$bdb_libext"
        	        try_headers="db.h db$version/db.h db$sversion/db.h"
		    else
		        db_libfilename="libdb-$version$bdb_libext"
        	        db_lib="-ldb-$version"
		        db_libfile="$d/$bdb_loclibdir/libdb-$version$bdb_libext"
        	        try_headers="db.h db$version/db.h db$sversion/db.h"
    		    fi
		fi

		for db_hdr in $try_headers
		do
		    if test -f $d/include/$db_hdr -a -f $db_libfile
		    then
  	                save_CFLAGS="$CFLAGS"
	                CFLAGS="$CFLAGS -I$d/include"
  	                save_LIBS="$LIBS"
	                LIBS="$LIBS -L$d/$bdb_loclibdir $db_lib"
   	                AC_LINK_IFELSE([AC_LANG_SOURCE([
#include <${db_hdr}>
int main() {
    printf("%p", db_create);
    return 0;
}
])],
	                             bdbdir=$d)
	                CFLAGS="$save_CFLAGS"
  	                LIBS="$save_LIBS"
	                if test x$bdbdir != x
		        then
		            break;
		        fi
		    fi
		done
	        if test x$bdbdir != x
		then
		    break;
		fi
	    done
	    if test x$bdbdir != x
	    then
		if test -z $version
	        then
		    AC_MSG_RESULT([found])
		else
		    AC_MSG_RESULT([found version $version])
		fi
		BDB_CFLAGS=-I${bdbdir}/include
	    	BDB_LIBS="-L${bdbdir}/$bdb_loclibdir $db_lib"
		BDB_LIB=$db_libfilename
		BDB_DIR="$bdbdir"
		AC_MSG_NOTICE([the runtime shared library to load will be $BDB_LIB])
		break
  	    else
	        AC_MSG_RESULT([not found])
	    fi
	done

	if test "x$BDB_LIBS" = x
	then
	    AC_MSG_NOTICE([BDB backend not used])
	else
	    LIBGDA_BDB_INC="#include <libgda/gda-data-model-bdb.h>"
	    LIBGDA_BDB_TYPE="gda_data_model_bdb_get_type"
    	    bdb_found=yes
	fi
    fi

    AM_CONDITIONAL(BDB,[test "$bdb_found" = "yes"])
    AC_SUBST(BDB_LIB)
    AC_SUBST(BDB_LIBS)
    AC_SUBST(BDB_CFLAGS)
    AC_SUBST(LIBGDA_BDB_INC)
    AC_SUBST(LIBGDA_BDB_TYPE)
])

m4_define([_BDBSQL_CHECK_INTERNAL],
[
    AC_BEFORE([AC_PROG_LIBTOOL],[$0])dnl setup libtool first
    AC_BEFORE([AM_PROG_LIBTOOL],[$0])dnl setup libtool first
    AC_BEFORE([LT_INIT],[$0])dnl setup libtool first

    m4_if([$1],[],[bdbsql_loclibdir=""],[bdbsql_loclibdir=$1])
    if test "x$bdbsql_loclibdir" = x
    then
        if test "x$platform_win32" = xyes
	then
	    bdbsql_loclibdir=bin
	else
	    bdbsql_loclibdir=lib
	fi
    fi

    bdbsql_found=no
    bdbsqldir=""
    if test "x$BDB_DIR" != x
    then
        AC_MSG_CHECKING([for Berkeley DB SQL files along with found BDB installation])
	if test -f $BDB_DIR/include/dbsql.h
	then
	    BDBSQL_CFLAGS="$BDB_CFLAGS"
	    BDBSQL_LIBS="-L$BDB_DIR/$bdb_loclibdir -ldb_sql"
	    BDBSQL_PATH="$BDB_DIR/$bdb_loclibdir"
	    AC_MSG_RESULT([found])
	    AC_CHECK_LIB(db_sql, sqlite3_table_column_metadata,[bdbsql_api=1], [bdbsql_api=0], $BDBSQL_CFLAGS $BDBSQL_LIBS -pthread -ldl)

	    if test $bdbsql_api = 0
	    then
		BDBSQL_CFLAGS=""
		BDBSQL_LIBS=""
		AC_MSG_NOTICE([Installed BDB Sql was not compiled with the SQLITE_ENABLE_COLUMN_METADATA, BDB Sql provider not compiled])
	    else
	        bdbsql_found=yes
		bdbsqldir="$BDB_DIR"
	    fi
	else
	    AC_MSG_RESULT([not found])
	fi
    fi

    AM_CONDITIONAL(BDBSQL,[test "bdbsql_found" = "yes"])
    AC_SUBST(BDBSQL_LIBS)
    AC_SUBST(BDBSQL_CFLAGS)
])


dnl Usage:
dnl   BDB_CHECK([libdirname])

AC_DEFUN([BDB_CHECK],
[
    _BDB_CHECK_INTERNAL([$1])
    _BDBSQL_CHECK_INTERNAL([$1])
])

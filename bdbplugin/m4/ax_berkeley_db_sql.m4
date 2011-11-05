# ===========================================================================
#      http://www.gnu.org/software/autoconf-archive/ax_berkeley_db.html
# ===========================================================================
#
# SYNOPSIS
#
#   AX_BERKELEY_DB_SQL([MINIMUM-VERSION [, ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]]])
#
# DESCRIPTION
#
#   This macro tries to find Berkeley DB. It honors MINIMUM-VERSION if
#   given.
#
#   If libdb is found, DB_HEADER and DB_LIBS variables are set and
#   ACTION-IF-FOUND shell code is executed if specified. DB_HEADER is set to
#   location of db.h header in quotes (e.g. "db3/db.h") and
#   AC_DEFINE_UNQUOTED is called on it, so that you can type
#
#     #include DB_HEADER
#
#   in your C/C++ code. DB_LIBS is set to linker flags needed to link
#   against the library (e.g. -ldb3.1) and AC_SUBST is called on it.
#
# LICENSE
#
#   Copyright (c) 2008 Vaclav Slavik <vaclav.slavik@matfyz.cz>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

#serial 7

AC_DEFUN([AX_BERKELEY_DB_SQL],
[
  old_LIBS="$LIBS"

  minversion=ifelse([$1], ,,$1)

  DB_HEADER=""
  DB_SQL_LIBS=""

  if test -z $minversion ; then
      minvermajor=0
      minverminor=0
      minverpatch=0
      AC_MSG_CHECKING([for Berkeley DB SQL])
  else
      minvermajor=`echo $minversion | cut -d. -f1`
      minverminor=`echo $minversion | cut -d. -f2`
      minverpatch=`echo $minversion | cut -d. -f3`
      minvermajor=${minvermajor:-0}
      minverminor=${minverminor:-0}
      minverpatch=${minverpatch:-0}
      AC_MSG_CHECKING([for Berkeley DB SQL>= $minversion])
  fi

  for version in 5.2 5.1 5.0 4.9 4.8 4.7 4.6 4.5 4.4 4.3 4.2 4.1 4.0 3.6 3.5 3.4 3.3 3.2 3.1 ""; do
  for version2 in "" 5.2 5.1 5.0 4.9 4.8 4.7 4.6 4.5 4.4 4.3 4.2 4.1 4.0 3.6 3.5 3.4 3.3 3.2 3.1; do

    if test -z $version ; then
	if test -z $version2 ; then
	    db_lib="-ldb_sql -ldb -ldl"
            try_headers="db.h"
        else
    	    db_lib="-ldb_sql-$version2 -ldb -ldl"
            try_headers="db.h"
        fi
    else
	if test "x$version2" != "x"; then
		continue 1;
	fi
        db_lib="-ldb_sql-$version -ldb-$version -ldl"
        try_headers="db$version/db.h db`echo $version | sed -e 's,\..*,,g'`/db.h db.h"
        

    fi
    
    #echo $db_lib
    #echo $try_headers

    LIBS="$old_LIBS $db_lib"

    for db_hdr in $try_headers ; do
        if test -z $DB_HEADER ; then
            AC_LINK_IFELSE(
                [AC_LANG_PROGRAM(
                    [
                        #include <${db_hdr}>
                    ],
                    [
                        #if !((DB_VERSION_MAJOR > (${minvermajor}) || \
                              (DB_VERSION_MAJOR == (${minvermajor}) && \
                                    DB_VERSION_MINOR > (${minverminor})) || \
                              (DB_VERSION_MAJOR == (${minvermajor}) && \
                                    DB_VERSION_MINOR == (${minverminor}) && \
                                    DB_VERSION_PATCH >= (${minverpatch}))))
                            #error "too old version"
                        #endif

                        DB *db;
                        db_create(&db, NULL, 0);
                    ])],
                [
                    AC_MSG_RESULT([header $db_hdr, library $db_lib])

                    DB_HEADER="$db_hdr"
                    DB_SQL_LIBS="$db_lib"
                ])
        fi
    done
  done
  done

  LIBS="$old_LIBS"

  if test -z "$DB_HEADER" ; then
    AC_MSG_RESULT([not found])
    ifelse([$3], , :, [$3])
  else
    AC_DEFINE_UNQUOTED(DB_HEADER, ["$DB_HEADER"], ["Berkeley DB Header File"])
    AC_SUBST(DB_SQL_LIBS)
    ifelse([$2], , :, [$2])
  fi
])

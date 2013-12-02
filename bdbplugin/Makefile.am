ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = liburbackupserver_bdbplugin.la
liburbackupserver_bdbplugin_la_SOURCES = dllmain.cpp BDBFactory.cpp ../Database.cpp ../Query.cpp ../sqlite/shell.c
AM_CXXFLAGS = -DLINUX -DBDBPLUGIN
AM_LDFLAGS =$(DB_SQL_LIBS)
noinst_HEADERS=../Interface/Database.h ../Interface/Query.h ../Interface/Object.h ../Interface/Mutex.h ../Interface/Condition.h ../Interface/DatabaseInt.h ../Interface/DatabaseFactory.h ../Interface/Server.h ../Interface/File.h ../sqlite/sqlite3.h ../sqlite/shell.h ../Query.h ../Database.h ../vld.h ../Interface/Types.h BDBFactory.h
if WITH_FORTIFY
	AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
endif
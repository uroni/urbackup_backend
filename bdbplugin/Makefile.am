ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = liburbackupserver_bdbplugin.la
liburbackupserver_bdbplugin_la_SOURCES = dllmain.cpp BDBFactory.cpp ../Database.cpp ../Query.cpp ../sqlite/shell.c
noinst_HEADERS = IUrlFactory.h pluginmgr.h UrlFactory.h
AM_CXXFLAGS = -DLINUX -DBDBPLUGIN $(BDBSQL_CFLAGS) $(BDB_CFLAGS)
AM_LDFLAGS =$(BDBSQL_LIBS) $(BDB_LIBS)
noinst_HEADERS=../Interface/DatabaseInt.h ../Interface/DatabaseFactory.h ../Interface/Server.h ../Interface/File.h ../sqlite/sqlite3.h ../sqlite/shell.h ../Query.h ../Database.h ../vld.h
ACLOCAL_AMFLAGS = -I m4
bin_PROGRAMS = urbackup_srv
urbackup_srv_SOURCES = AcceptThread.cpp Client.cpp Database.cpp Query.cpp SelectThread.cpp Server.cpp ServerLinux.cpp ServiceAcceptor.cpp ServiceWorker.cpp SessionMgr.cpp StreamPipe.cpp Template.cpp WorkerThread.cpp main.cpp md5.cpp stringtools.cpp libfastcgi/fastcgi.cpp Mutex_lin.cpp LoadbalancerClient.cpp DBSettingsReader.cpp file_common.cpp file_fstream.cpp file_linux.cpp FileSettingsReader.cpp LookupService.cpp SettingsReader.cpp Table.cpp OutputStream.cpp ThreadPool.cpp MemoryPipe.cpp Condition_lin.cpp MemorySettingsReader.cpp sqlite/sqlite3.c
urbackup_srv_LDADD = 
AM_CXXFLAGS = $(PTHREAD_CFLAGS) -DLINUX
#-DTHREAD_BOOST $(BOOST_CPPFLAGS)
AM_CFLAGS = -DSQLITE_ENABLE_UNLOCK_NOTIFY
AM_LDFLAGS = $(PTHREAD_LIBS) -ldl
#$(BOOST_LDFLAGS) $(BOOST_THREAD_LIB)
SUBDIRS=. pychart downloadplugin fileservplugin fsimageplugin httpserver urbackup
install-data-local: $(srcdir)/init.d $(srcdir)/defaults
	mkdir $(localstatedir) || true
	cp $(srcdir)/init.d /etc/init.d/urbackup_srv
	if ! test -e /etc/default/urbackup_srv; then cp $(srcdir)/defaults /etc/default/urbackup_srv; fi
	mkdir $(localstatedir)/urbackup || true
	adduser --system --quiet --group --home $(localstatedir)/urbackup urbackup
	
	update-rc.d urbackup_srv defaults 90
noinst_HEADERS=SessionMgr.h WorkerThread.h Helper_win32.h Database.h defaults.h ServiceAcceptor.h Query.h SettingsReader.h file.h Mutex_boost.h Condition_boost.h file_memory.h MemorySettingsReader.h Condition_lin.h LookupService.h Template.h types.h DBSettingsReader.h stringtools.h ThreadPool.h libs.h vld_.h ServiceWorker.h StreamPipe.h LoadbalancerClient.h socket_header.h FileSettingsReader.h SelectThread.h md5.h vld.h Table.h Client.h MemoryPipe.h Mutex_lin.h AcceptThread.h OutputStream.h Server.h Interface/SessionMgr.h Interface/Service.h Interface/PluginMgr.h Interface/Database.h Interface/Pipe.h Interface/CustomClient.h Interface/User.h Interface/Query.h Interface/SettingsReader.h Interface/Types.h Interface/Template.h Interface/ThreadPool.h Interface/Mutex.h Interface/File.h Interface/Condition.h Interface/Table.h Interface/Plugin.h Interface/Thread.h Interface/Action.h Interface/Object.h Interface/OutputStream.h Interface/Server.h libfastcgi/fastcgi.hpp sqlite/sqlite3.h sqlite/sqlite3ext.h utf8/utf8.h utf8/utf8/checked.h utf8/utf8/core.h utf8/utf8/unchecked.h cryptoplugin/ICryptoFactory.h cryptoplugin/IAESEncryption.h cryptoplugin/IAESDecryption.h
EXTRA_DIST=init.d defaults

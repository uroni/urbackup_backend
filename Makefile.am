ACLOCAL_AMFLAGS = -I m4
bin_PROGRAMS = urbackup_srv
urbackup_srv_SOURCES = AcceptThread.cpp Client.cpp Database.cpp Query.cpp SelectThread.cpp Server.cpp ServerLinux.cpp ServiceAcceptor.cpp ServiceWorker.cpp SessionMgr.cpp StreamPipe.cpp Template.cpp WorkerThread.cpp main.cpp md5.cpp stringtools.cpp libfastcgi/fastcgi.cpp Mutex_lin.cpp LoadbalancerClient.cpp DBSettingsReader.cpp file_common.cpp file_fstream.cpp file_linux.cpp FileSettingsReader.cpp LookupService.cpp SettingsReader.cpp Table.cpp OutputStream.cpp ThreadPool.cpp MemoryPipe.cpp Condition_lin.cpp MemorySettingsReader.cpp sqlite/sqlite3.c
urbackup_srv_LDADD = 
AM_CXXFLAGS = $(PTHREAD_CFLAGS) -DLINUX
#-DTHREAD_BOOST $(BOOST_CPPFLAGS)
AM_CFLAGS = -DSQLITE_ENABLE_UNLOCK_NOTIFY
AM_LDFLAGS = $(PTHREAD_LIBS) -ldl
#$(BOOST_LDFLAGS) $(BOOST_THREAD_LIB)
SUBDIRS=. cryptoplugin downloadplugin fileservplugin fsimageplugin httpserver piped_process pychart urbackup
install-data-local: $(srcdir)/init.d $(srcdir)/defaults
	cp $(srcdir)/init.d /etc/init.d/urbackup_srv
	cp $(srcdir)/defaults /etc/default/urbackup_srv
	mkdir $(localstatedir)/urbackup || true
	adduser --system --quiet --group --home $(localstatedir)/urbackup urbackup
	update-rc.d urbackup_srv defaults 90
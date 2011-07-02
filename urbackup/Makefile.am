lib_LTLIBRARIES = liburbackupserver.la
liburbackupserver_la_SOURCES = dllmain.cpp ../stringtools.cpp clientdao.cpp client.cpp ClientService.cpp os_functions_lin.cpp server.cpp server_get.cpp server_hash.cpp sha2/sha2.c fileclient/data.cpp fileclient/FileClient.cpp fileclient/tcpstack.cpp server_prepare_hash.cpp server_update.cpp server_status.cpp server_channel.cpp server_ping.cpp server_log.cpp escape.cpp server_writer.cpp bufmgr.cpp ClientSend.cpp server_running.cpp server_cleanup.cpp server_settings.cpp server_update_stats.cpp client_restore.cpp  ServerIdentityMgr.cpp serverinterface/helper.cpp serverinterface/json.cpp serverinterface/lastacts.cpp serverinterface/login.cpp serverinterface/progress.cpp serverinterface/salt.cpp serverinterface/templ.cpp serverinterface/users.cpp serverinterface/piegraph.cpp serverinterface/usage.cpp serverinterface/usagegraph.cpp serverinterface/status.cpp serverinterface/settings.cpp serverinterface/backups.cpp serverinterface/logs.cpp serverinterface/isimageready.cpp serverinterface/getimage.cpp serverinterface/google_chart.cpp treediff/TreeDiff.cpp treediff/TreeNode.cpp treediff/TreeReader.cpp glob/glob.cpp
AM_CXXFLAGS = -DLINUX
INSTALL_OPTS=-o urbackup -g urbackup
install-data-local: $(srcdir)/backup_server.db $(srcdir)/status.htm $(srcdir)/www/*.js $(srcdir)/www/*.htm $(srcdir)/www/*.css $(srcdir)/www/*.png $(srcdir)/www/*.gif
	$(INSTALL_DATA) $(INSTALL_OPTS) -D $(srcdir)/backup_server.db $(DESTDIR)$(localstatedir)/urbackup/backup_server.db.template
	$(INSTALL_DATA) $(INSTALL_OPTS) -D $(srcdir)/status.htm $(DESTDIR)$(localstatedir)/urbackup/status.htm
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/google_chart.js $(DESTDIR)$(localstatedir)/urbackup/
	$(INSTALL_DATA) $(INSTALL_OPTS) -d $(DESTDIR)$(localstatedir)/urbackup/www
	chmod u+x $(DESTDIR)$(localstatedir)/urbackup/www
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/*.js $(DESTDIR)$(localstatedir)/urbackup/www/
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/*.htm $(DESTDIR)$(localstatedir)/urbackup/www/
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/*.css $(DESTDIR)$(localstatedir)/urbackup/www/
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/*.png $(DESTDIR)$(localstatedir)/urbackup/www/
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/*.gif $(DESTDIR)$(localstatedir)/urbackup/www/
noinst_HEADERS = server_ping.h server_cleanup.h DirectoryWatcherThread.h os_functions.h serverinterface/json.h serverinterface/helper.h serverinterface/action_header.h serverinterface/actions.h server_writer.h settings.h server_image.h server_settings.h ChangeJournalWatcher.h zero_hash.h server_update.h server_log.h server_hash.h server_status.h bufmgr.h watchdir/DelayedDirectoryChangeHandler.h watchdir/Event.h watchdir/CriticalSection.h watchdir/DirectoryChanges.h server_update_stats.h sha2/sha2.h fileclient/FileClient.h fileclient/data.h fileclient/socket_header.h fileclient/tcpstack.h fileclient/packet_ids.h ServerIdentityMgr.h database.h mbr_code.h action_header.h escape.h server.h server_running.h ClientSend.h clientdao.h server_prepare_hash.h actions.h server_channel.h server_get.h client.h ClientService.h treediff/TreeDiff.h treediff/TreeNode.h treediff/TreeReader.h ../pychart/IPychart.h ../pychart/IPychartFactory.h
EXTRA_DIST = backup_server.db status.htm google_chart.js www/*.js www/*.htm www/*.css www/*.png www/*.gif www/*.ico

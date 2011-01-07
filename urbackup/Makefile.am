lib_LTLIBRARIES = liburbackup.la
liburbackup_la_SOURCES = dllmain.cpp ../stringtools.cpp clientdao.cpp client.cpp ClientService.cpp os_functions_lin.cpp server.cpp server_get.cpp server_hash.cpp sha2/sha2.c fileclient/data.cpp fileclient/FileClient.cpp fileclient/tcpstack.cpp server_prepare_hash.cpp server_update.cpp server_status.cpp server_channel.cpp server_ping.cpp server_log.cpp escape.cpp server_writer.cpp bufmgr.cpp ClientSend.cpp server_running.cpp server_cleanup.cpp server_settings.cpp server_update_stats.cpp client_restore.cpp  ServerIdentityMgr.cpp serverinterface/helper.cpp serverinterface/json.cpp serverinterface/lastacts.cpp serverinterface/login.cpp serverinterface/progress.cpp serverinterface/salt.cpp serverinterface/templ.cpp serverinterface/users.cpp serverinterface/piegraph.cpp serverinterface/usage.cpp serverinterface/usagegraph.cpp serverinterface/status.cpp serverinterface/settings.cpp serverinterface/backups.cpp serverinterface/logs.cpp serverinterface/isimageready.cpp serverinterface/getimage.cpp
AM_CXXFLAGS = -DLINUX
install-data-local: $(srcdir)/backup_server.db $(srcdir)/status.htm $(srcdir)/www/*.js $(srcdir)/www/*.htm $(srcdir)/www/*.css $(srcdir)/www/*.png $(srcdir)/www/*.gif
	if ! test -d $(localstatedir)/urbackup; then mkdir $(localstatedir)/urbackup; fi
	cp $(srcdir)/backup_server.db $(localstatedir)/urbackup/
	cp $(srcdir)/status.htm $(localstatedir)/urbackup/
	cp $(srcdir)/www/*.js $(localstatedir)/urbackup/
	cp $(srcdir)/www/*.htm $(localstatedir)/urbackup/
	cp $(srcdir)/www/*.css $(localstatedir)/urbackup/
	cp $(srcdir)/www/*.png $(localstatedir)/urbackup/
	cp $(srcdir)/www/*.gif $(localstatedir)/urbackup/
	chown -R urbackup:urbackup $(localstatedir)/urbackup



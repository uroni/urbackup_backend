lib_LTLIBRARIES = liburbackupclient.la
liburbackupclient_la_SOURCES = dllmain.cpp ../stringtools.cpp clientdao.cpp client.cpp ClientService.cpp ../urbackupcommon/os_functions_lin.cpp ../urbackupcommon/sha2/sha2.c ../urbackupcommon/escape.cpp ClientSend.cpp client_restore.cpp ServerIdentityMgr.cpp ../urbackupcommon/fileclient/tcpstack.cpp ../common/data.cpp ../urbackupcommon/glob.cpp ../urbackupcommon/bufmgr.cpp ClientServiceCMD.cpp ../urbackupcommon/CompressedPipe.cpp ImageThread.cpp InternetClient.cpp ../urbackupcommon/InternetServicePipe.cpp ../urbackupcommon/settingslist.cpp ../md5.cpp ../urbackupcommon/json.cpp file_permissions.cpp lin_ver.cpp lin_tokens.cpp
if WITH_FORTIFY
AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
endif
AM_CXXFLAGS = -DLINUX -DCLIENT_ONLY
INSTALL_OPTS=
install-data-local: backup_client.db
	$(INSTALL_DATA) $(INSTALL_OPTS) -D "$(srcdir)/backup_client.db" "$(DESTDIR)$(localstatedir)/urbackup/backup_client.db.template"
	touch "$(DESTDIR)$(localstatedir)/urbackup/new.txt"

noinst_HEADERS = DirectoryWatcherThread.h ../urbackupcommon/os_functions.h ChangeJournalWatcher.h ../urbackupcommon/sha2/sha2.h database.h ../urbackupcommon/escape.h ClientSend.h clientdao.h client.h ClientService.h ../fileservplugin/IFileServFactory.h ../fileservplugin/IFileServ.h ../common/data.h ../urbackupcommon/fileclient/tcpstack.h ../urbackupcommon/capa_bits.h ServerIdentityMgr.h ../urbackupcommon/bufmgr.h ../urbackupcommon/CompressedPipe.h ImageThread.h InternetClient.h ../urbackupcommon/InternetServicePipe.h ../md5.h ../urbackupcommon/settingslist.h ../cryptoplugin/IZlibCompression.h ../cryptoplugin/IZlibDecompression.h ../cryptoplugin/ICryptoFactory.h ../cryptoplugin/IAESDecryption.h ../cryptoplugin/IAESEncryption.h ../urbackupcommon/internet_pipe_capabilities.h  ../urbackupcommon/settings.h ../urbackupserver/fileclient/socket_header.h ../urbackupcommon/mbrdata.h ../urbackupcommon/InternetServiceIDs.h ../urbackupcommon/json.h file_permissions.h lin_ver.h ../urbackupcommon/glob.h tokens.h
EXTRA_DIST = backup_client.db

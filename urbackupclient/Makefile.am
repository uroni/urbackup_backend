lib_LTLIBRARIES = liburbackupclient.la
liburbackupclient_la_SOURCES = dllmain.cpp ../stringtools.cpp clientdao.cpp client.cpp ClientService.cpp ../urbackupcommon/os_functions_lin.cpp ../urbackupcommon/sha2/sha2.c ../urbackupcommon/escape.cpp ClientSend.cpp client_restore.cpp ServerIdentityMgr.cpp ../urbackupcommon/fileclient/tcpstack.cpp ../common/data.cpp ../urbackupcommon/glob.cpp ../urbackupcommon/bufmgr.cpp ClientServiceCMD.cpp ../urbackupcommon/CompressedPipe.cpp ImageThread.cpp InternetClient.cpp ../urbackupcommon/InternetServicePipe2.cpp ../urbackupcommon/settingslist.cpp ../md5.cpp ../urbackupcommon/json.cpp file_permissions.cpp lin_ver.cpp lin_tokens.cpp common_tokens.cpp FileMetadataDownloadThread.cpp RestoreFiles.cpp ../urbackupcommon/chunk_hasher.cpp ../common/adler32.cpp ../urbackupcommon/fileclient/FileClient.cpp ../urbackupcommon/fileclient/FileClientChunked.cpp ../urbackupcommon/file_metadata.cpp ../urbackupcommon/filelist_utils.cpp RestoreDownloadThread.cpp
if WITH_FORTIFY
AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
endif
AM_CXXFLAGS = -DLINUX -DCLIENT_ONLY
INSTALL_OPTS=
install-data-local: backup_client.db backup_scripts/list backup_scripts/mariadb.conf backup_scripts/mariadb
	$(MKDIR_P) $(DESTDIR)$(localstatedir)/urbackup/
	$(INSTALL_DATA) $(INSTALL_OPTS) "$(srcdir)/backup_client.db" "$(DESTDIR)$(localstatedir)/urbackup/backup_client.db.template"
	touch "$(DESTDIR)$(localstatedir)/urbackup/new.txt"
	$(MKDIR_P) "$(DESTDIR)/etc/urbackup/scripts" || true
	$(INSTALL_PROGRAM) "$(srcdir)/backup_scripts/list" "$(DESTDIR)/etc/urbackup/scripts/list"
	test -e "$(DESTDIR)/etc/urbackup/scripts/mariadb.conf" || $(INSTALL_PROGRAM) "$(srcdir)/backup_scripts/mariadb.conf" "$(DESTDIR)/etc/urbackup/scripts/mariadb.conf"
	$(INSTALL_PROGRAM) "$(srcdir)/backup_scripts/mariadb" "$(DESTDIR)/etc/urbackup/scripts/mariadb"
	chmod 700 $(DESTDIR)/etc/urbackup/scripts/*
	

noinst_HEADERS = DirectoryWatcherThread.h ../urbackupcommon/os_functions.h ChangeJournalWatcher.h ../urbackupcommon/sha2/sha2.h database.h ../urbackupcommon/escape.h ClientSend.h clientdao.h client.h ClientService.h ../fileservplugin/IFileServFactory.h ../fileservplugin/IFileServ.h ../common/data.h ../urbackupcommon/fileclient/tcpstack.h ../urbackupcommon/capa_bits.h ServerIdentityMgr.h ../urbackupcommon/bufmgr.h ../urbackupcommon/CompressedPipe.h ImageThread.h InternetClient.h ../urbackupcommon/InternetServicePipe2.h ../md5.h ../urbackupcommon/settingslist.h ../cryptoplugin/IZlibCompression.h ../cryptoplugin/IZlibDecompression.h ../cryptoplugin/ICryptoFactory.h ../cryptoplugin/IAESDecryption.h ../cryptoplugin/IAESEncryption.h ../urbackupcommon/internet_pipe_capabilities.h  ../urbackupcommon/settings.h ../urbackupcommon/fileclient/socket_header.h ../urbackupcommon/mbrdata.h ../urbackupcommon/InternetServiceIDs.h ../urbackupcommon/json.h file_permissions.h lin_ver.h ../urbackupcommon/glob.h tokens.h FileMetadataDownloadThread.h RestoreFiles.h ../urbackupcommon/chunk_hasher.h ../common/adler32.h ../urbackupcommon/fileclient/FileClient.h ../urbackupcommon/fileclient/FileClientChunked.h ../urbackupcommon/file_metadata.h ../urbackupcommon/filelist_utils.h RestoreDownloadThread.h
EXTRA_DIST = backup_client.db backup_scripts/list backup_scripts/mariadb.conf backup_scripts/mariadb

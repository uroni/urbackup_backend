ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = liburbackupserver.la
liburbackupserver_la_SOURCES = dllmain.cpp ../stringtools.cpp ../urbackupcommon/os_functions_lin.cpp server.cpp ClientMain.cpp server_hash.cpp ../urbackupcommon/sha2/sha2.c ../common/data.cpp ../urbackupcommon/fileclient/FileClient.cpp ../urbackupcommon/fileclient/tcpstack.cpp server_prepare_hash.cpp server_update.cpp server_status.cpp server_channel.cpp server_ping.cpp server_log.cpp ../urbackupcommon/escape.cpp server_writer.cpp ../urbackupcommon/bufmgr.cpp server_running.cpp server_cleanup.cpp server_settings.cpp server_update_stats.cpp serverinterface/helper.cpp ../urbackupcommon/json.cpp serverinterface/lastacts.cpp serverinterface/login.cpp serverinterface/progress.cpp serverinterface/salt.cpp serverinterface/users.cpp serverinterface/piegraph.cpp serverinterface/usage.cpp serverinterface/usagegraph.cpp serverinterface/status.cpp serverinterface/settings.cpp serverinterface/backups.cpp serverinterface/logs.cpp serverinterface/getimage.cpp serverinterface/download_client.cpp treediff/TreeDiff.cpp treediff/TreeNode.cpp treediff/TreeReader.cpp ChunkPatcher.cpp ../urbackupcommon/CompressedPipe.cpp InternetServiceConnector.cpp ../urbackupcommon/InternetServicePipe.cpp ../md5.cpp ../urbackupcommon/settingslist.cpp ../urbackupcommon/fileclient/FileClientChunked.cpp ../common/adler32.cpp server_archive.cpp filedownload.cpp serverinterface/shutdown.cpp snapshot_helper.cpp verify_hashes.cpp apps/cleanup_cmd.cpp apps/repair_cmd.cpp dao/ServerCleanupDao.cpp lmdb/mdb.c lmdb/midl.c LMDBFileIndex.cpp FileIndex.cpp create_files_index.cpp serverinterface/livelog.cpp serverinterface/start_backup.cpp serverinterface/create_zip.cpp server_dir_links.cpp dao/ServerBackupDao.cpp apps/export_auth_log.cpp apps/check_files_index.cpp ServerDownloadThread.cpp server_hash_existing.cpp ../urbackupcommon/file_metadata.cpp ../urbackupcommon/filelist_utils.cpp Backup.cpp ImageBackup.cpp FileBackup.cpp IncrFileBackup.cpp FullFileBackup.cpp ContinuousBackup.cpp ThrottleUpdater.cpp ../urbackupcommon/glob.cpp
liburbackupserver_la_LDFLAGS = --no-undefined
if WITH_FORTIFY
AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
endif
AM_CXXFLAGS = -DLINUX $(PTHREAD_CFLAGS)
install-data-local: backup_server.db ../urbackup/status.htm www/js/*.js www/*.htm www/css/*.css $(srcdir)/www/images/*.png www/images/*.gif www/fonts/* urbackup_dsa.pub www/*.swf
	$(INSTALL) $(INSTALL_OPTS) -m 744 -d "$(DESTDIR)$(localstatedir)/urbackup"
	chmod +x "$(DESTDIR)$(localstatedir)/urbackup"
	$(INSTALL_DATA) $(INSTALL_OPTS) "$(srcdir)/backup_server.db" "$(DESTDIR)$(localstatedir)/urbackup/backup_server.db.template"
	$(INSTALL_DATA) $(INSTALL_OPTS) "$(srcdir)/../urbackup/status.htm" "$(DESTDIR)$(localstatedir)/urbackup/status.htm"
	$(INSTALL) $(INSTALL_OPTS) -m 744 -d "$(DESTDIR)$(localstatedir)/urbackup/www"
	$(INSTALL) $(INSTALL_OPTS) -m 744 -d "$(DESTDIR)$(localstatedir)/urbackup/www/css"
	$(INSTALL) $(INSTALL_OPTS) -m 744 -d "$(DESTDIR)$(localstatedir)/urbackup/www/js"
	$(INSTALL) $(INSTALL_OPTS) -m 744 -d "$(DESTDIR)$(localstatedir)/urbackup/www/fonts"
	$(INSTALL) $(INSTALL_OPTS) -m 744 -d "$(DESTDIR)$(localstatedir)/urbackup/www/images"
if !PACKAGING
	chmod u+x "$(DESTDIR)$(localstatedir)/urbackup/www"
	chmod u+x "$(DESTDIR)$(localstatedir)/urbackup"
endif
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/js/*.js "$(DESTDIR)$(localstatedir)/urbackup/www/js/"
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/*.htm "$(DESTDIR)$(localstatedir)/urbackup/www/"
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/css/*.css "$(DESTDIR)$(localstatedir)/urbackup/www/css/"
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/images/*.png "$(DESTDIR)$(localstatedir)/urbackup/www/images/"
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/images/*.gif "$(DESTDIR)$(localstatedir)/urbackup/www/images/"
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/urbackup_dsa.pub "$(DESTDIR)$(localstatedir)/urbackup/urbackup_dsa.pub"
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/*.swf "$(DESTDIR)$(localstatedir)/urbackup/www/"
	$(INSTALL_DATA) $(INSTALL_OPTS) $(srcdir)/www/fonts/* "$(DESTDIR)$(localstatedir)/urbackup/www/fonts/"
	
noinst_HEADERS = server_ping.h server_cleanup.h ../urbackupcommon/os_functions.h ../urbackupcommon/json.h serverinterface/helper.h serverinterface/action_header.h serverinterface/actions.h server_writer.h ../urbackupcommon/settings.h server_settings.h zero_hash.h server_update.h server_log.h server_hash.h server_status.h ../urbackupcommon/bufmgr.h server_update_stats.h ../urbackupcommon/sha2/sha2.h ../md5.h ../urbackupcommon/fileclient/FileClient.h ../common/data.h ../urbackupcommon/fileclient/socket_header.h ../urbackupcommon/fileclient/tcpstack.h ../urbackupcommon/fileclient/packet_ids.h database.h mbr_code.h action_header.h ../urbackupcommon/escape.h server.h server_running.h server_prepare_hash.h actions.h server_channel.h ClientMain.h treediff/TreeDiff.h treediff/TreeNode.h treediff/TreeReader.h ../fileservplugin/IFileServFactory.h ../fileservplugin/IFileServ.h ../urlplugin/IUrlFactory.h ../urbackupcommon/capa_bits.h ../cryptoplugin/ICryptoFactory.h ../urbackupcommon/fileclient/FileClientChunked.h ChunkPatcher.h ../urbackupcommon/CompressedPipe.h ../urbackupcommon/InternetServicePipe.h ../urbackupcommon/InternetServiceIDs.h InternetServiceConnector.h ../md5.h ../urbackupcommon/settingslist.h server_archive.h ../cryptoplugin/IZlibCompression.h ../cryptoplugin/IZlibDecompression.h ../cryptoplugin/ICryptoFactory.h ../cryptoplugin/IAESEncryption.h ../cryptoplugin/IAESDecryption.h ../fileservplugin/chunk_settings.h ../urbackupcommon/internet_pipe_capabilities.h ../urbackupcommon/mbrdata.h filedownload.h snapshot_helper.h apps/cleanup_cmd.h apps/repair_cmd.h dao/ServerCleanupDao.h lmdb/lmdb.h lmdb/midl.h LMDBFileIndex.h create_files_index.h FileIndex.h serverinterface/rights.h ../common/miniz.c server_dir_links.h dao/ServerBackupDao.h apps/app.h apps/export_auth_log.h serverinterface/login.h ServerDownloadThread.h ../common/adler32.h server_hash_existing.h ../urbackupcommon/file_metadata.h ../urbackupcommon/filelist_utils.h Backup.h ImageBackup.h FileBackup.h IncrFileBackup.h FullFileBackup.h ContinuousBackup.h ThrottleUpdater.h ../urbackupcommon/glob.h
EXTRA_DIST = backup_server.db ../urbackup/status.htm www/js/*.js www/*.htm www/css/*.css www/images/*.png www/images/*.gif www/*.ico urbackup_dsa.pub www/*.swf www/fonts/*

ACLOCAL_AMFLAGS = -I m4
sbin_PROGRAMS = urbackupclientbackend
if WITH_GUI_CLIENT
bin_PROGRAMS = urbackupclientctl urbackupclientgui
else
bin_PROGRAMS = urbackupclientctl
endif
urbackupclientbackend_SOURCES = AcceptThread.cpp Client.cpp Database.cpp Query.cpp SelectThread.cpp Server.cpp ServerLinux.cpp ServiceAcceptor.cpp ServiceWorker.cpp SessionMgr.cpp StreamPipe.cpp Template.cpp WorkerThread.cpp main.cpp md5.cpp stringtools.cpp libfastcgi/fastcgi.cpp Mutex_lin.cpp LoadbalancerClient.cpp DBSettingsReader.cpp file_common.cpp file_fstream.cpp file_linux.cpp FileSettingsReader.cpp LookupService.cpp SettingsReader.cpp Table.cpp OutputStream.cpp ThreadPool.cpp MemoryPipe.cpp Condition_lin.cpp MemorySettingsReader.cpp sqlite/sqlite3.c sqlite/shell.c SQLiteFactory.cpp PipeThrottler.cpp mt19937ar.cpp DatabaseCursor.cpp SharedMutex_lin.cpp StaticPluginRegistration.cpp common/data.cpp common/adler32.cpp

urbackupclientbackend_SOURCES += urbackupcommon/os_functions_lin.cpp urbackupcommon/sha2/sha2.c urbackupcommon/fileclient/FileClient.cpp urbackupcommon/fileclient/tcpstack.cpp urbackupcommon/escape.cpp urbackupcommon/bufmgr.cpp urbackupcommon/json.cpp urbackupcommon/CompressedPipe.cpp urbackupcommon/InternetServicePipe2.cpp urbackupcommon/settingslist.cpp urbackupcommon/fileclient/FileClientChunked.cpp urbackupcommon/InternetServicePipe.cpp urbackupcommon/filelist_utils.cpp urbackupcommon/file_metadata.cpp urbackupcommon/glob.cpp urbackupcommon/chunk_hasher.cpp urbackupcommon/CompressedPipe2.cpp urbackupcommon/SparseFile.cpp urbackupcommon/ExtentIterator.cpp urbackupcommon/TreeHash.cpp

urbackupclientbackend_SOURCES += cryptoplugin/dllmain.cpp cryptoplugin/AESDecryption.cpp cryptoplugin/CryptoFactory.cpp cryptoplugin/pluginmgr.cpp cryptoplugin/AESEncryption.cpp cryptoplugin/ZlibCompression.cpp cryptoplugin/ZlibDecompression.cpp cryptoplugin/AESGCMDecryption.cpp cryptoplugin/AESGCMEncryption.cpp cryptoplugin/ECDHKeyExchange.cpp

if EMBEDDED_CRYPTOPP
urbackupclientbackend_SOURCES += cryptoplugin/cryptlib.cpp cryptoplugin/algebra.cpp cryptoplugin/algparam.cpp cryptoplugin/asn.cpp cryptoplugin/basecode.cpp cryptoplugin/cbcmac.cpp cryptoplugin/channels.cpp cryptoplugin/dh.cpp cryptoplugin/dll.cpp cryptoplugin/dsa.cpp cryptoplugin/ec2n.cpp cryptoplugin/eccrypto.cpp cryptoplugin/ecp.cpp cryptoplugin/eprecomp.cpp cryptoplugin/files.cpp cryptoplugin/filters.cpp cryptoplugin/gf2n.cpp cryptoplugin/gfpcrypt.cpp cryptoplugin/hex.cpp cryptoplugin/hmac.cpp cryptoplugin/integer.cpp cryptoplugin/iterhash.cpp cryptoplugin/misc.cpp cryptoplugin/modes.cpp cryptoplugin/queue.cpp cryptoplugin/nbtheory.cpp cryptoplugin/oaep.cpp cryptoplugin/osrng.cpp cryptoplugin/pch.cpp cryptoplugin/pkcspad.cpp cryptoplugin/pubkey.cpp cryptoplugin/randpool.cpp cryptoplugin/rdtables.cpp cryptoplugin/rijndael.cpp cryptoplugin/rng.cpp cryptoplugin/rsa.cpp cryptoplugin/sha.cpp cryptoplugin/simple.cpp cryptoplugin/skipjack.cpp cryptoplugin/strciphr.cpp cryptoplugin/trdlocal.cpp cryptoplugin/cpu.cpp cryptoplugin/gzip.cpp cryptoplugin/gcm.cpp cryptoplugin/des.cpp cryptoplugin/authenc.cpp cryptoplugin/fips140.cpp cryptoplugin/zdeflate.cpp cryptoplugin/cmac.cpp cryptoplugin/eax.cpp cryptoplugin/adler32.cpp cryptoplugin/zinflate.cpp cryptoplugin/mqueue.cpp cryptoplugin/hrtimer.cpp cryptoplugin/pssr.cpp cryptoplugin/crc.cpp cryptoplugin/dessp.cpp cryptoplugin/zlib.cpp
endif

urbackupclientbackend_SOURCES += fsimageplugin/dllmain.cpp fsimageplugin/filesystem.cpp fsimageplugin/FSImageFactory.cpp fsimageplugin/pluginmgr.cpp fsimageplugin/vhdfile.cpp fsimageplugin/fs/ntfs.cpp fsimageplugin/fs/unknown.cpp fsimageplugin/CompressedFile.cpp fsimageplugin/LRUMemCache.cpp fsimageplugin/cowfile.cpp fsimageplugin/FileWrapper.cpp fsimageplugin/ClientBitmap.cpp

urbackupclientbackend_SOURCES += urbackupclient/dllmain.cpp urbackupclient/clientdao.cpp urbackupclient/client.cpp urbackupclient/ClientService.cpp urbackupclient/ClientSend.cpp urbackupclient/client_restore.cpp urbackupclient/ServerIdentityMgr.cpp urbackupclient/ClientServiceCMD.cpp  urbackupclient/ImageThread.cpp urbackupclient/InternetClient.cpp urbackupclient/file_permissions.cpp urbackupclient/lin_ver.cpp urbackupclient/lin_tokens.cpp urbackupclient/common_tokens.cpp urbackupclient/FileMetadataDownloadThread.cpp urbackupclient/RestoreFiles.cpp urbackupclient/RestoreDownloadThread.cpp urbackupclient/TokenCallback.cpp common/miniz.c urbackupclient/cmdline_preprocessor.cpp

urbackupclientbackend_SOURCES += fileservplugin/dllmain.cpp fileservplugin/bufmgr.cpp fileservplugin/CClientThread.cpp fileservplugin/CriticalSection.cpp fileservplugin/CTCPFileServ.cpp fileservplugin/CUDPThread.cpp fileservplugin/FileServ.cpp fileservplugin/FileServFactory.cpp fileservplugin/log.cpp fileservplugin/main.cpp fileservplugin/map_buffer.cpp fileservplugin/pluginmgr.cpp fileservplugin/ChunkSendThread.cpp fileservplugin/PipeFile.cpp fileservplugin/PipeSessions.cpp fileservplugin/PipeFileUnix.cpp fileservplugin/PipeFileBase.cpp fileservplugin/FileMetadataPipe.cpp fileservplugin/PipeFileTar.cpp

if WITH_FORTIFY
FORTIFY_FLAGS = -fstack-protector-strong --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2 -fPIE
FORTIFY_ldflags = -Wl,-z,relro
endif

urbackupclientbackend_LDADD = $(PTHREAD_LIBS) $(DLOPEN_LIBS)
urbackupclientbackend_CPPFLAGS = $(FORTIFY_FLAGS) $(SUID_CFLAGS) -DNDEBUG
urbackupclientbackend_CXXFLAGS = $(PTHREAD_CFLAGS) -DLINUX -DSTATIC_PLUGIN -DVARDIR='"$(localstatedir)"' -DSYSCONFDIR='"$(sysconfdir)"' -DDATADIR='"$(datadir)"'
urbackupclientbackend_CFLAGS = -DSQLITE_ENABLE_UNLOCK_NOTIFY 
urbackupclientbackend_LDFLAGS = $(PTHREAD_LIBS) $(SUID_LDFLAGS) $(FORTIFY_ldflags)
if !EMBEDDED_CRYPTOPP
urbackupclientbackend_LDADD += $(CRYPTOPP_LIBS)
else
urbackupclientbackend_CXXFLAGS += -DCRYPTOPP_INCLUDE_PREFIX=cryptoplugin -DCRYPTOPP_NO_UNALIGNED_DATA_ACCESS -DCRYPTOPP_NO_BACKWARDS_COMPATIBILITY_562
endif

if CLIENT_UPDATE
urbackupclientbackend_CXXFLAGS += -DURB_WITH_CLIENTUPDATE
endif

urbackupclientctl_SOURCES = clientctl/main.cpp urbackupcommon/os_functions_lin.cpp stringtools.cpp clientctl/Connector.cpp clientctl/tcpstack.cpp urbackupcommon/escape.cpp clientctl/jsoncpp.cpp
urbackupclientctl_LDADD = 
urbackupclientctl_CXXFLAGS = -DOS_FUNC_NO_NET -DOS_FUNC_NO_SERVER -DLINUX -DVARDIR='"$(localstatedir)"'

if MACOSX
urbackupclientgui_osx_SOURCES = client/osxutils.mm
else
urbackupclientgui_osx_SOURCES =
endif

urbackupclientgui_SOURCES = client/gui/GUI.cpp client/main.cpp client/ConfigPath.cpp client/Connector.cpp client/escape.cpp client/FileSettingsReader.cpp client/Info.cpp client/Logs.cpp client/Settings.cpp client/stringtools.cpp client/TaskBarBaloon.cpp client/tcpstack.cpp client/TrayIcon.cpp client/Status.cpp client/jsoncpp.cpp client/TranslationHelper.cpp $(urbackupclientgui_osx_SOURCES) client/gui/GUISetupWizard.cpp client/SetupWizard.cpp client/CbtStatus.cpp
urbackupclientgui_CXXFLAGS = -DLINUX $(WX_CXXFLAGS_ONLY) -DDATADIR='"$(datadir)"' -DVARDIR='"$(localstatedir)"' -DSBINDIR='"$(sbindir)"' -DBINDIR='"$(bindir)"'
urbackupclientgui_CFLAGS = $(WX_CFLAGS_ONLY)
urbackupclientgui_CPPFLAGS = $(WX_CPPFLAGS) -DNDEBUG
urbackupclientgui_LDADD = $(WX_LIBS)

if MACOSX
urbackupclientgui_LDFLAGS = -framework ServiceManagement -framework CoreServices -framework Security
endif

install-exec-local: defaults_client init.d_client init.d_client_rh
if INSTALL_DEBIAN_INITD
	$(INSTALL) -D "$(srcdir)/init.d_client" "$(sysconfdir)/init.d/urbackupclientbackend"
	if ! test -e "$(sysconfdir)/default/urbackupclientbackend"; then $(INSTALL) -D "$(srcdir)/defaults_client" "$(sysconfdir)/default/urbackupclientbackend"; fi
endif

if INSTALL_REDHAT_INITD
	$(INSTALL) -D "$(srcdir)/init.d_client_rh" "$(sysconfdir)/init.d/urbackupclientbackend"
	if ! test -e "$(sysconfdir)/sysconfig/urbackupclientbackend"; then $(INSTALL) -D "$(srcdir)/defaults_client" "$(sysconfdir)/sysconfig/urbackupclientbackend"; fi
endif

	mkdir -p "$(DESTDIR)$(localstatedir)/urbackup"
	mkdir -p "$(DESTDIR)$(localstatedir)/urbackup/data"
	
INSTALL_OPTS=

if WITH_GUI_CLIENT
if MACOSX
install-data-local: urbackupclient/backup_scripts/list urbackupclient/backup_scripts/mariadbdump.conf urbackupclient/backup_scripts/mariadbdump \
					urbackupclient/backup_scripts/postgresqldump.conf urbackupclient/backup_scripts/postgresqldump \
					urbackupclient/backup_scripts/postgresbase.conf urbackupclient/backup_scripts/postgresbase \
					urbackupclient/backup_scripts/mariadbxtrabackup.conf urbackupclient/backup_scripts/mariadbxtrabackup \
					client/info.txt client/data/backup-bad.ico client/data/backup-ok.ico client/data/backup-progress.ico \
					client/data/backup-progress-pause.ico client/data/backup-no-server.ico client/data/backup-no-recent.ico client/data/backup-indexing.ico \
					client/data/logo1.png client/data/lang/*/*.mo client/version.txt client/data/urbackup_ecdsa409k1.pub client/data/updates_h.dat
else
install-data-local: urbackupclient/backup_scripts/list urbackupclient/backup_scripts/mariadbdump.conf urbackupclient/backup_scripts/mariadbdump \
					urbackupclient/backup_scripts/postgresqldump.conf urbackupclient/backup_scripts/postgresqldump \
					urbackupclient/backup_scripts/postgresbase.conf urbackupclient/backup_scripts/postgresbase \
					urbackupclient/backup_scripts/mariadbxtrabackup.conf urbackupclient/backup_scripts/mariadbxtrabackup \
					client/info.txt client/data/backup-bad.xpm client/data/backup-ok.xpm client/data/backup-progress.xpm \
					client/data/backup-progress-pause.xpm client/data/backup-no-server.xpm client/data/backup-no-recent.xpm client/data/backup-indexing.xpm \
					client/data/logo1.png client/data/lang/*/*.mo client/version.txt client/data/urbackup_ecdsa409k1.pub client/data/updates_h.dat
endif
else
install-data-local: urbackupclient/backup_scripts/list urbackupclient/backup_scripts/mariadbdump.conf urbackupclient/backup_scripts/mariadbdump \
					urbackupclient/backup_scripts/postgresqldump.conf urbackupclient/backup_scripts/postgresqldump \
					urbackupclient/backup_scripts/postgresbase.conf urbackupclient/backup_scripts/postgresbase \
					urbackupclient/backup_scripts/mariadbxtrabackup.conf urbackupclient/backup_scripts/mariadbxtrabackup \
					client/version.txt client/data/urbackup_ecdsa409k1.pub client/data/updates_h.dat
endif

	$(MKDIR_P) "$(DESTDIR)$(localstatedir)/urbackup/"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/scripts"
	$(MKDIR_P) "$(DESTDIR)$(sysconfdir)/urbackup"
	
	$(INSTALL_PROGRAM) "$(srcdir)/urbackupclient/backup_scripts/list" "$(DESTDIR)$(datadir)/urbackup/scripts/list"
	test -e "$(DESTDIR)$(sysconfdir)/mariadbdump.conf" || $(INSTALL_PROGRAM) "$(srcdir)/urbackupclient/backup_scripts/mariadbdump.conf" "$(DESTDIR)$(sysconfdir)/urbackup/mariadbdump.conf"
	$(INSTALL_PROGRAM) "$(srcdir)/urbackupclient/backup_scripts/mariadbdump" "$(DESTDIR)$(datadir)/urbackup/scripts/mariadbdump"
	test -e "$(DESTDIR)$(sysconfdir)/postgresqldump.conf" || $(INSTALL_PROGRAM) "$(srcdir)/urbackupclient/backup_scripts/postgresqldump.conf" "$(DESTDIR)$(sysconfdir)/urbackup/postgresqldump.conf"
	$(INSTALL_PROGRAM) "$(srcdir)/urbackupclient/backup_scripts/postgresqldump" "$(DESTDIR)$(datadir)/urbackup/scripts/postgresqldump"
	test -e "$(DESTDIR)$(sysconfdir)/postgresbase.conf" || $(INSTALL_PROGRAM) "$(srcdir)/urbackupclient/backup_scripts/postgresbase.conf" "$(DESTDIR)$(sysconfdir)/urbackup/postgresbase.conf"
	$(INSTALL_PROGRAM) "$(srcdir)/urbackupclient/backup_scripts/postgresbase" "$(DESTDIR)$(datadir)/urbackup/scripts/postgresbase"
	test -e "$(DESTDIR)$(sysconfdir)/mariadbxtrabackup.conf" || $(INSTALL_PROGRAM) "$(srcdir)/urbackupclient/backup_scripts/mariadbxtrabackup.conf" "$(DESTDIR)$(sysconfdir)/urbackup/mariadbxtrabackup.conf"
	$(INSTALL_PROGRAM) "$(srcdir)/urbackupclient/backup_scripts/mariadbxtrabackup" "$(DESTDIR)$(datadir)/urbackup/scripts/mariadbxtrabackup"
	
	for script in "$(DESTDIR)$(datadir)/urbackup/scripts/"*; do sed "s|SYSCONFDIR|$(sysconfdir)|g" "$$script" > "$$script.r"; mv "$$script.r" "$$script"; done
	chmod 700 "$(DESTDIR)$(datadir)/urbackup/scripts/"*
	chmod 700 "$(DESTDIR)$(sysconfdir)/urbackup/"*
	$(INSTALL_DATA) "$(srcdir)/client/version.txt" "$(DESTDIR)$(localstatedir)/urbackup/version.txt"
	$(INSTALL_DATA) "$(srcdir)/client/data/urbackup_ecdsa409k1.pub" "$(DESTDIR)$(datadir)/urbackup/"
	$(INSTALL_DATA) "$(srcdir)/client/data/updates_h.dat" "$(DESTDIR)$(datadir)/urbackup/"
	
if WITH_GUI_CLIENT
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup"
	$(INSTALL_DATA) $(srcdir)/client/data/*.png "$(DESTDIR)$(datadir)/urbackup/"
if MACOSX
	$(INSTALL_DATA) $(srcdir)/client/data/*.ico "$(DESTDIR)$(datadir)/urbackup/"
else
	$(INSTALL_DATA) $(srcdir)/client/data/*.xpm "$(DESTDIR)$(datadir)/urbackup/"
endif
	$(INSTALL_DATA) $(srcdir)/client/info.txt "$(DESTDIR)$(datadir)/urbackup/"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/it"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/pl"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/pt_BR"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/sk"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/zh_TW"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/zh_CN"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/de"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/es"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/fr"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/ru"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/uk"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/da"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/nl"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/fa"
	$(MKDIR_P) "$(DESTDIR)$(datadir)/urbackup/lang/cs"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/it/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/it/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/pl/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/pl/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/pt_BR/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/pt_BR/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/sk/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/sk/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/zh_TW/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/zh_TW/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/zh_CN/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/zh_CN/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/de/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/de/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/es/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/es/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/fr/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/fr/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/ru/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/ru/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/uk/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/uk/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/da/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/da/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/nl/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/nl/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/fa/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/fa/urbackup.mo"
	$(INSTALL_DATA) "$(srcdir)/client/data/lang/cs/urbackup.mo" "$(DESTDIR)$(datadir)/urbackup/lang/cs/urbackup.mo"
endif
	
cryptoplugin_headers = cryptoplugin/AESEncryption.h cryptoplugin/AESDecryption.h cryptoplugin/IAESDecryption.h cryptoplugin/ICryptoFactory.h cryptoplugin/pluginmgr.h cryptoplugin/IAESEncryption.h cryptoplugin/CryptoFactory.h cryptoplugin/IZlibCompression.h cryptoplugin/IZlibDecompression.h cryptoplugin/ZlibCompression.h cryptoplugin/ZlibDecompression.h cryptoplugin/cryptopp_inc.h cryptoplugin/AESGCMDecryption.h cryptoplugin/AESGCMEncryption.h cryptoplugin/ECDHKeyExchange.h cryptoplugin/IAESGCMDecryption.h cryptoplugin/IAESGCMEncryption.h cryptoplugin/IECDHKeyExchange.h

fileservplugin_headers = fileservplugin/bufmgr.h fileservplugin/CUDPThread.h fileservplugin/FileServFactory.h fileservplugin/IFileServ.h fileservplugin/packet_ids.h fileservplugin/socket_header.h fileservplugin/CriticalSection.h fileservplugin/FileServ.h fileservplugin/log.h fileservplugin/pluginmgr.h   fileservplugin/CClientThread.h fileservplugin/CTCPFileServ.h fileservplugin/IFileServFactory.h fileservplugin/map_buffer.h fileservplugin/settings.h fileservplugin/types.h fileservplugin/chunk_settings.h fileservplugin/ChunkSendThread.h fileservplugin/PipeFile.h fileservplugin/PipeSessions.h  fileservplugin/PipeFileBase.h fileservplugin/IPermissionCallback.h fileservplugin/FileMetadataPipe.h fileservplugin/PipeFileTar.h

fsimageplugin_headers = fsimageplugin/filesystem.h fsimageplugin/FSImageFactory.h fsimageplugin/IFilesystem.h fsimageplugin/IFSImageFactory.h fsimageplugin/IVHDFile.h fsimageplugin/pluginmgr.h fsimageplugin/vhdfile.h fsimageplugin/fs/ntfs.h fsimageplugin/fs/unknown.h fsimageplugin/CompressedFile.h fsimageplugin/LRUMemCache.h common/miniz.c fsimageplugin/cowfile.h fsimageplugin/FileWrapper.h fsimageplugin/ClientBitmap.h

urbackupclientctl_headers = clientctl/Connector.h clientctl/tcpstack.h clientctl/json/json.h clientctl/json/json-forwards.h

if WITH_GUI_CLIENT
client_headers = client/ConfigPath.h client/Connector.h client/escape.h client/FileSettingsReader.h client/Info.h client/Logs.h client/main.h client/resource.h client/Settings.h client/stringtools.h client/TaskBarBaloon.h client/tcpstack.h client/TrayIcon.h client/utf8/utf8.h client/utf8/utf8/checked.h client/utf8/utf8/core.h client/utf8/utf8/unchecked.h client/gui/GUI.h client/capa_bits.h client/Status.h client/json/json.h client/json/json-forwards.h client/TranslationHelper.h client/CbtStatus.h
else
client_headers = 
endif

urbackupclient_headers = urbackupclient/DirectoryWatcherThread.h urbackupcommon/os_functions.h urbackupclient/ChangeJournalWatcher.h urbackupcommon/sha2/sha2.h urbackupclient/database.h urbackupcommon/escape.h urbackupclient/ClientSend.h urbackupclient/clientdao.h urbackupclient/client.h urbackupclient/ClientService.h fileservplugin/IFileServFactory.h fileservplugin/IFileServ.h common/data.h urbackupcommon/fileclient/tcpstack.h urbackupcommon/capa_bits.h urbackupclient/ServerIdentityMgr.h urbackupcommon/bufmgr.h urbackupcommon/CompressedPipe.h urbackupclient/ImageThread.h urbackupclient/InternetClient.h urbackupcommon/InternetServicePipe2.h urbackupcommon/settingslist.h cryptoplugin/IZlibCompression.h cryptoplugin/IZlibDecompression.h cryptoplugin/ICryptoFactory.h cryptoplugin/IAESDecryption.h cryptoplugin/IAESEncryption.h urbackupcommon/internet_pipe_capabilities.h urbackupclient/ urbackupcommon/settings.h urbackupcommon/fileclient/socket_header.h urbackupcommon/mbrdata.h urbackupcommon/InternetServiceIDs.h urbackupcommon/json.h urbackupclient/file_permissions.h urbackupclient/lin_ver.h urbackupcommon/glob.h urbackupclient/tokens.h urbackupclient/FileMetadataDownloadThread.h urbackupclient/RestoreFiles.h urbackupcommon/chunk_hasher.h common/adler32.h urbackupcommon/fileclient/FileClient.h urbackupcommon/fileclient/FileClientChunked.h urbackupcommon/file_metadata.h urbackupcommon/filelist_utils.h urbackupclient/RestoreDownloadThread.h urbackupclient/TokenCallback.h urbackupcommon/CompressedPipe2.h urbackupcommon/server_compat.h urbackupcommon/fileclient/packet_ids.h urbackupcommon/InternetServicePipe.h urbackupclient/backup_client_db.h urbackupcommon/SparseFile.h urbackupcommon/ExtentIterator.h urbackupcommon/TreeHash.h


tclap_headers = \
			 tclap/CmdLineInterface.h \
			 tclap/ArgException.h \
			 tclap/CmdLine.h \
			 tclap/XorHandler.h \
			 tclap/MultiArg.h \
			 tclap/UnlabeledMultiArg.h \
			 tclap/ValueArg.h \
			 tclap/UnlabeledValueArg.h \
			 tclap/Visitor.h tclap/Arg.h \
			 tclap/HelpVisitor.h \
			 tclap/SwitchArg.h \
			 tclap/MultiSwitchArg.h \
			 tclap/VersionVisitor.h \
			 tclap/IgnoreRestVisitor.h \
			 tclap/CmdLineOutput.h \
			 tclap/StdOutput.h \
			 tclap/DocBookOutput.h \
			 tclap/ZshCompletionOutput.h \
			 tclap/OptionalUnlabeledTracker.h \
			 tclap/Constraint.h \
			 tclap/ValuesConstraint.h \
			 tclap/ArgTraits.h \
			 tclap/StandardTraits.h
			 
if EMBEDDED_CRYPTOPP
cryptopp_headers = cryptoplugin/*.h
else
cryptopp_headers = 
endif

noinst_HEADERS=SessionMgr.h WorkerThread.h Helper_win32.h Database.h defaults.h ServiceAcceptor.h Query.h SettingsReader.h file.h file_memory.h MemorySettingsReader.h Condition_lin.h LookupService.h Template.h types.h DBSettingsReader.h stringtools.h ThreadPool.h libs.h vld_.h ServiceWorker.h StreamPipe.h LoadbalancerClient.h socket_header.h FileSettingsReader.h SelectThread.h md5.h vld.h Table.h Client.h MemoryPipe.h Mutex_lin.h AcceptThread.h OutputStream.h Server.h Interface/SessionMgr.h Interface/Service.h Interface/PluginMgr.h Interface/Database.h Interface/Pipe.h Interface/CustomClient.h Interface/User.h Interface/Query.h Interface/SettingsReader.h Interface/Types.h Interface/Template.h Interface/ThreadPool.h Interface/Mutex.h Interface/File.h Interface/Condition.h Interface/Table.h Interface/Plugin.h Interface/Thread.h Interface/Action.h Interface/Object.h Interface/OutputStream.h Interface/Server.h libfastcgi/fastcgi.hpp sqlite/sqlite3.h sqlite/sqlite3ext.h utf8/utf8.h utf8/utf8/checked.h utf8/utf8/core.h utf8/utf8/unchecked.h cryptoplugin/ICryptoFactory.h cryptoplugin/IAESEncryption.h cryptoplugin/IAESDecryption.h Interface/DatabaseFactory.h Interface/DatabaseInt.h sqlite/shell.h SQLiteFactory.h PipeThrottler.h Interface/PipeThrottler.h mt19937ar.h DatabaseCursor.h Interface/DatabaseCursor.h client_version.h Interface/SharedMutex.h SharedMutex_lin.h StaticPluginRegistration.h $(cryptoplugin_headers) $(fileservplugin_headers) $(fsimageplugin_headers) $(urbackupclientctl_headers) $(client_headers) $(tclap_headers) $(urbackupclient_headers) $(cryptopp_headers)

EXTRA_DIST_GUI = client/info.txt client/data/backup-bad.xpm client/data/backup-ok.xpm client/data/backup-progress.xpm client/data/backup-progress-pause.xpm client/data/backup-no-server.xpm client/data/backup-no-recent.xpm client/data/backup-indexing.xpm client/data/logo1.png client/data/lang/it/urbackup.mo client/data/lang/pl/urbackup.mo client/data/lang/pt_BR/urbackup.mo client/data/lang/sk/urbackup.mo client/data/lang/zh_TW/urbackup.mo client/data/lang/zh_CN/urbackup.mo client/data/lang/de/urbackup.mo client/data/lang/es/urbackup.mo client/data/lang/fr/urbackup.mo client/data/lang/ru/urbackup.mo client/data/lang/uk/urbackup.mo client/data/lang/da/urbackup.mo client/data/lang/nl/urbackup.mo client/data/lang/fa/urbackup.mo client/data/lang/cs/urbackup.mo client/gui/GUISetupWizard.h client/SetupWizard.h

EXTRA_DIST=docs/urbackupclientbackend.1 init.d_client init.d_client_rh defaults_client $(EXTRA_DIST_GUI) tclap/COPYING tclap/AUTHORS urbackupclientbackend-debian.service urbackupclientbackend-redhat.service urbackupclient/backup_scripts/list urbackupclient/backup_scripts/mariadbdump.conf urbackupclient/backup_scripts/mariadbdump urbackupclient/backup_scripts/postgresqldump.conf urbackupclient/backup_scripts/postgresqldump urbackupclient/backup_scripts/postgresbase urbackupclient/backup_scripts/postgresbase.conf urbackupclient/backup_scripts/mariadbxtrabackup.conf urbackupclient/backup_scripts/mariadbxtrabackup client/version.txt client/data/urbackup_ecdsa409k1.pub linux_snapshot/btrfs_create_filesystem_snapshot linux_snapshot/btrfs_remove_filesystem_snapshot linux_snapshot/dattobd_create_filesystem_snapshot linux_snapshot/dattobd_remove_filesystem_snapshot linux_snapshot/lvm_create_filesystem_snapshot linux_snapshot/lvm_remove_filesystem_snapshot client/data/updates_h.dat

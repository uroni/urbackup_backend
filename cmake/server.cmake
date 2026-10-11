# UrBackup server: urbackupsrv, urbackup_snapshot_helper, urbackup_mount_helper

set(URBACKUP_SERVER_CONFIG_DIR ${CMAKE_BINARY_DIR}/generated/server)
urbackup_write_config(${URBACKUP_SERVER_CONFIG_DIR} urbackup-server ${URBACKUP_SERVER_VERSION} "${URBACKUP_SERVER_CONFIG_CHECKS}")

if(URBACKUP_WITH_MAIL)
	find_package(CURL 7.20.0 REQUIRED)
endif()

if(URBACKUP_WITH_MOUNTVHD)
	if(NOT PKG_CONFIG_FOUND)
		message(FATAL_ERROR "pkg-config is required to find fuse")
	endif()
	pkg_check_modules(FUSE REQUIRED IMPORTED_TARGET fuse>=2.8.0)
endif()

find_package(Lua 5.3 REQUIRED)

if(NOT URBACKUP_EMBEDDED_LMDB)
	find_package(unofficial-lmdb CONFIG QUIET)
	if(TARGET unofficial::lmdb::lmdb)
		set(URBACKUP_LMDB unofficial::lmdb::lmdb)
	else()
		find_path(LMDB_INCLUDE_DIR lmdb.h)
		find_library(LMDB_LIBRARY lmdb)
		if(NOT LMDB_INCLUDE_DIR OR NOT LMDB_LIBRARY)
			message(FATAL_ERROR "LMDB not found. Please install liblmdb-dev(el).")
		endif()
		add_library(urbackup_lmdb INTERFACE)
		target_include_directories(urbackup_lmdb INTERFACE ${LMDB_INCLUDE_DIR})
		target_link_libraries(urbackup_lmdb INTERFACE ${LMDB_LIBRARY})
		set(URBACKUP_LMDB urbackup_lmdb)
	endif()
endif()

add_executable(urbackupsrv
	${URBACKUP_CORE_SOURCES}
	${URBACKUP_FSIMAGEPLUGIN_SOURCES}
	${URBACKUP_COMMON_SOURCES}
	${URBACKUP_FILESERVPLUGIN_SOURCES}
	httpserver/dllmain.cpp httpserver/IndexFiles.cpp httpserver/HTTPAction.cpp httpserver/HTTPFile.cpp
	httpserver/HTTPService.cpp httpserver/HTTPClient.cpp httpserver/HTTPProxy.cpp httpserver/MIMEType.cpp
	httpserver/HTTPSocket.cpp
	urbackupserver/dllmain.cpp urbackupserver/server.cpp urbackupserver/ClientMain.cpp
	urbackupserver/server_hash.cpp urbackupserver/server_prepare_hash.cpp urbackupserver/server_update.cpp
	urbackupserver/server_status.cpp urbackupserver/server_channel.cpp urbackupserver/server_ping.cpp
	urbackupserver/server_log.cpp urbackupserver/server_writer.cpp urbackupserver/server_running.cpp
	urbackupserver/server_cleanup.cpp urbackupserver/server_settings.cpp urbackupserver/server_update_stats.cpp
	urbackupserver/serverinterface/helper.cpp urbackupserver/serverinterface/lastacts.cpp
	urbackupserver/serverinterface/login.cpp urbackupserver/serverinterface/progress.cpp
	urbackupserver/serverinterface/salt.cpp urbackupserver/serverinterface/users.cpp
	urbackupserver/serverinterface/piegraph.cpp urbackupserver/serverinterface/usage.cpp
	urbackupserver/serverinterface/usagegraph.cpp urbackupserver/serverinterface/status.cpp
	urbackupserver/serverinterface/settings.cpp urbackupserver/serverinterface/backups.cpp
	urbackupserver/serverinterface/logs.cpp urbackupserver/serverinterface/getimage.cpp
	urbackupserver/serverinterface/download_client.cpp urbackupserver/treediff/TreeDiff.cpp
	urbackupserver/treediff/TreeNode.cpp urbackupserver/treediff/TreeReader.cpp urbackupserver/ChunkPatcher.cpp
	urbackupserver/InternetServiceConnector.cpp urbackupserver/server_archive.cpp urbackupserver/filedownload.cpp
	urbackupserver/serverinterface/shutdown.cpp urbackupserver/snapshot_helper.cpp urbackupserver/verify_hashes.cpp
	urbackupserver/apps/cleanup_cmd.cpp urbackupserver/apps/repair_cmd.cpp urbackupserver/apps/md5sum_check.cpp
	urbackupserver/apps/patch.cpp urbackupserver/dao/ServerCleanupDao.cpp urbackupserver/lmdb/mdb.c
	urbackupserver/lmdb/midl.c urbackupserver/LMDBFileIndex.cpp urbackupserver/FileIndex.cpp
	urbackupserver/create_files_index.cpp urbackupserver/serverinterface/livelog.cpp
	urbackupserver/serverinterface/start_backup.cpp urbackupserver/serverinterface/create_zip.cpp
	urbackupserver/server_dir_links.cpp urbackupserver/dao/ServerBackupDao.cpp urbackupserver/apps/export_auth_log.cpp
	urbackupserver/apps/check_files_index.cpp urbackupserver/ServerDownloadThread.cpp
	urbackupserver/ServerDownloadThreadGroup.cpp urbackupserver/Backup.cpp urbackupserver/ImageBackup.cpp
	urbackupserver/FileBackup.cpp urbackupserver/IncrFileBackup.cpp urbackupserver/FullFileBackup.cpp
	urbackupserver/ContinuousBackup.cpp urbackupserver/ThrottleUpdater.cpp
	urbackupserver/FileMetadataDownloadThread.cpp urbackupserver/restore_client.cpp
	urbackupserver/apps/skiphash_copy.cpp urbackupserver/cmdline_preprocessor.cpp
	urbackupserver/dao/ServerFilesDao.cpp urbackupserver/dao/ServerLinkDao.cpp
	urbackupserver/dao/ServerLinkJournalDao.cpp urbackupserver/serverinterface/add_client.cpp
	urbackupserver/serverinterface/restore_prepare_wait.cpp urbackupserver/copy_storage.cpp
	urbackupserver/ImageMount.cpp urbackupserver/DataplanDb.cpp urbackupserver/PhashLoad.cpp
	urbackupserver/serverinterface/scripts.cpp urbackupserver/Alerts.cpp urbackupserver/Mailer.cpp
	urbackupserver/LogReport.cpp urbackupserver/serverinterface/status_check.cpp urbackupserver/apps/blockalign.cpp
	urbackupserver/serverinterface/restore_image.cpp urbackupserver/WebSocketConnector.cpp
	luaplugin/dllmain.cpp luaplugin/LuaInterpreter.cpp luaplugin/pluginmgr.cpp)

urbackup_setup_backend(urbackupsrv ${URBACKUP_SERVER_CONFIG_DIR})
target_compile_definitions(urbackupsrv PRIVATE
	$<$<COMPILE_LANGUAGE:CXX>:VARDIR="${CMAKE_INSTALL_FULL_LOCALSTATEDIR}">
	$<$<COMPILE_LANGUAGE:CXX>:BINDIR="${CMAKE_INSTALL_FULL_BINDIR}">
	$<$<COMPILE_LANGUAGE:CXX>:DATADIR="${CMAKE_INSTALL_FULL_DATADIR}">)
if(URBACKUP_EMBEDDED_SQLITE3)
	target_compile_definitions(urbackupsrv PRIVATE
		$<$<COMPILE_LANGUAGE:C>:SQLITE_ENABLE_DBPAGE_VTAB>
		$<$<COMPILE_LANGUAGE:C>:SQLITE_MAX_MMAP_SIZE=0x10000000000LL>)
endif()

if(URBACKUP_WITH_MAIL)
	target_sources(urbackupsrv PRIVATE urlplugin/dllmain.cpp urlplugin/pluginmgr.cpp urlplugin/UrlFactory.cpp)
	target_link_libraries(urbackupsrv PRIVATE CURL::libcurl)
endif()

if(URBACKUP_WITH_CRYPTO)
	target_sources(urbackupsrv PRIVATE ${URBACKUP_CRYPTOPLUGIN_SOURCES})
	urbackup_link_cryptopp(urbackupsrv)
else()
	target_compile_definitions(urbackupsrv PRIVATE DO_NOT_USE_CRYPTOPP_SHA DO_NOT_USE_CRYPTOPP_MD5)
endif()

target_include_directories(urbackupsrv PRIVATE ${LUA_INCLUDE_DIR})
target_link_libraries(urbackupsrv PRIVATE ${LUA_LIBRARIES})

if(NOT URBACKUP_EMBEDDED_LMDB)
	target_compile_definitions(urbackupsrv PRIVATE $<$<COMPILE_LANGUAGE:CXX>:NO_EMBEDDED_LMDB>)
	target_link_libraries(urbackupsrv PRIVATE ${URBACKUP_LMDB})
endif()

if(URBACKUP_WITH_MOUNTVHD)
	target_sources(urbackupsrv PRIVATE fuseplugin/dllmain.cpp)
	target_compile_definitions(urbackupsrv PRIVATE $<$<COMPILE_LANGUAGE:CXX>:WITH_FUSEPLUGIN>)
	target_link_libraries(urbackupsrv PRIVATE PkgConfig::FUSE)
endif()

# Setuid helpers
add_executable(urbackup_snapshot_helper snapshot_helper/main.cpp urbackupcommon/os_functions_lin_min.cpp stringtools.cpp)
target_compile_definitions(urbackup_snapshot_helper PRIVATE OS_FUNC_NO_NET OS_FUNC_NO_IFACE_SERVER LINUX)

add_executable(urbackup_mount_helper mount_helper/main.cpp urbackupcommon/os_functions_lin_min.cpp stringtools.cpp)
target_compile_definitions(urbackup_mount_helper PRIVATE OS_FUNC_NO_NET OS_FUNC_NO_IFACE_SERVER LINUX
	BINDIR="${CMAKE_INSTALL_FULL_BINDIR}")
if(URBACKUP_WITH_MOUNTVHD)
	target_compile_definitions(urbackup_mount_helper PRIVATE WITH_FUSEPLUGIN)
endif()

foreach(helper urbackup_snapshot_helper urbackup_mount_helper)
	target_include_directories(${helper} PRIVATE ${URBACKUP_SERVER_CONFIG_DIR} ${CMAKE_SOURCE_DIR} ${UTF8CPP_INCLUDE_DIR})
	urbackup_fortify(${helper})
endforeach()

#
# Install
#
install(TARGETS urbackupsrv RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS urbackup_snapshot_helper urbackup_mount_helper RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE SETUID SETGID)

set(_etc ${CMAKE_INSTALL_FULL_SYSCONFDIR})
set(_var ${CMAKE_INSTALL_FULL_LOCALSTATEDIR})
set(_share ${CMAKE_INSTALL_FULL_DATADIR})

if(URBACKUP_INSTALL_INITD)
	install(PROGRAMS init.d_server DESTINATION ${_etc}/init.d RENAME urbackupsrv)
	install(FILES logrotate_urbackupsrv DESTINATION ${_etc}/logrotate.d RENAME urbackupsrv)
	if(URBACKUP_PACKAGING)
		install(PROGRAMS defaults_server DESTINATION ${_etc}/default RENAME urbackupsrv)
	else()
		install(CODE "
			if(NOT EXISTS \"\$ENV{DESTDIR}${_etc}/default/urbackupsrv\")
				file(INSTALL \"${CMAKE_SOURCE_DIR}/defaults_server\" DESTINATION \"${_etc}/default\" RENAME urbackupsrv
					FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
			endif()")
	endif()
endif()

install(DIRECTORY DESTINATION ${_var}/urbackup)
install(FILES urbackup/dataplan_db.txt DESTINATION ${_var}/urbackup)

# Remove a binary left over from old installations that put urbackupsrv into sbin
if(NOT CMAKE_INSTALL_FULL_SBINDIR STREQUAL CMAKE_INSTALL_FULL_BINDIR)
	install(CODE "file(REMOVE \"\$ENV{DESTDIR}${CMAKE_INSTALL_FULL_SBINDIR}/urbackupsrv\")")
endif()

set(_www urbackupserver/www)
file(GLOB _www_root ${_www}/*.htm ${_www}/*.ico)
file(GLOB _www_js ${_www}/js/*.js)
file(GLOB _www_vs ${_www}/js/vs/*.js)
file(GLOB _www_css ${_www}/css/*.css)
file(GLOB _www_images ${_www}/images/*.png ${_www}/images/*.gif)
install(FILES ${_www_root} DESTINATION ${_share}/urbackup/www)
install(FILES ${_www_js} DESTINATION ${_share}/urbackup/www/js)
install(FILES ${_www_vs} DESTINATION ${_share}/urbackup/www/js/vs)
install(DIRECTORY ${_www}/js/vs/base/worker/ DESTINATION ${_share}/urbackup/www/js/vs/base/worker)
install(DIRECTORY ${_www}/js/vs/basic-languages/src/ DESTINATION ${_share}/urbackup/www/js/vs/basic-languages/src)
install(DIRECTORY ${_www}/js/vs/editor/ DESTINATION ${_share}/urbackup/www/js/vs/editor)
install(FILES ${_www_css} DESTINATION ${_share}/urbackup/www/css)
install(FILES ${_www_images} DESTINATION ${_share}/urbackup/www/images)
install(DIRECTORY ${_www}/swf/ DESTINATION ${_share}/urbackup/www/swf)
install(DIRECTORY ${_www}/fonts/ DESTINATION ${_share}/urbackup/www/fonts)
install(FILES urbackupserver/urbackup_ecdsa409k1.pub server-license.txt DESTINATION ${_share}/urbackup)

if(NOT URBACKUP_PACKAGING)
	if(URBACKUP_FREEBSD)
		set(_adduser "
			execute_process(COMMAND pw groupadd urbackup -q)
			execute_process(COMMAND pw useradd urbackup -q -g urbackup -d \"\$ENV{DESTDIR}${_var}/urbackup\" -m -s /usr/bin/false)")
	else()
		set(_adduser "
			execute_process(COMMAND adduser urbackup --system --quiet --group --home \"\$ENV{DESTDIR}${_var}/urbackup\" RESULT_VARIABLE res)
			if(NOT res EQUAL 0)
				execute_process(COMMAND adduser urbackup --system --user-group --home \"\$ENV{DESTDIR}${_var}/urbackup\")
			endif()")
	endif()
	install(CODE "
		${_adduser}
		execute_process(COMMAND chown urbackup:urbackup \"\$ENV{DESTDIR}${_var}/urbackup\")
		execute_process(COMMAND chown urbackup:urbackup \"\$ENV{DESTDIR}${_share}/urbackup\" \"\$ENV{DESTDIR}${_share}/urbackup/urbackup_ecdsa409k1.pub\")
		execute_process(COMMAND chown -R urbackup:urbackup \"\$ENV{DESTDIR}${_share}/urbackup/www\")")
endif()

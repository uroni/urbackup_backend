# UrBackup client: urbackupclientbackend, urbackupclientctl, blockalign,
# urbackupclient_dmsnaptool (if libdevmapper is found) and urbackupclientgui (unless headless)

set(URBACKUP_CLIENT_FULL_VERSION ${URBACKUP_CLIENT_VERSION}.${URBACKUP_BUILDID})
set(URBACKUP_CLIENT_CONFIG_DIR ${CMAKE_BINARY_DIR}/generated/client)
urbackup_write_config(${URBACKUP_CLIENT_CONFIG_DIR} urbackup-client ${URBACKUP_CLIENT_FULL_VERSION} "${URBACKUP_CLIENT_CONFIG_CHECKS}")

set(URBACKUP_FRONTEND_DIR ${CMAKE_SOURCE_DIR}/client)
if(NOT EXISTS ${URBACKUP_FRONTEND_DIR}/version.txt)
	if(NOT URBACKUP_CLIENT_HEADLESS)
		message(FATAL_ERROR "The client GUI needs the frontend sources in ${URBACKUP_FRONTEND_DIR} "
			"(git clone https://github.com/uroni/urbackup_frontend_wx client). "
			"Alternatively configure with -DURBACKUP_CLIENT_HEADLESS=ON.")
	endif()
	message(WARNING "${URBACKUP_FRONTEND_DIR} not found. Client data files (version.txt, public key, updates_h.dat) will not be installed.")
	set(URBACKUP_HAVE_FRONTEND FALSE)
else()
	set(URBACKUP_HAVE_FRONTEND TRUE)
endif()

if(URBACKUP_WITH_C_ARES)
	if(URBACKUP_USE_SYSTEM_LIBS)
		pkg_check_modules(CARES REQUIRED IMPORTED_TARGET libcares)
		set(URBACKUP_CARES PkgConfig::CARES)
	else()
		find_package(c-ares CONFIG REQUIRED)
		set(URBACKUP_CARES c-ares::cares)
	endif()
endif()

if(URBACKUP_USE_SYSTEM_LIBS)
	pkg_check_modules(JSONCPP REQUIRED IMPORTED_TARGET jsoncpp)
	set(URBACKUP_JSONCPP PkgConfig::JSONCPP)
else()
	find_package(jsoncpp CONFIG REQUIRED)
	set(URBACKUP_JSONCPP JsonCpp::JsonCpp)
endif()

set(URBACKUP_CLIENT_OPENSSL FALSE)
if(URBACKUP_WITH_OPENSSL)
	find_package(OpenSSL)
	if(OPENSSL_FOUND)
		set(URBACKUP_CLIENT_OPENSSL TRUE)
	else()
		message(WARNING "OpenSSL library not present. Compiling without OpenSSL support (web socket/https proxy support)")
	endif()
endif()

find_library(DEVMAPPER_LIBRARY devmapper)
find_path(DEVMAPPER_INCLUDE_DIR libdevmapper.h)
if(NOT DEVMAPPER_LIBRARY OR NOT DEVMAPPER_INCLUDE_DIR)
	message(WARNING "Did not find libdevmapper. Not compiling UrBackup dmsnapshot tool. Dm-snapshot method will not be available.")
endif()

#
# urbackupclientbackend
#
add_executable(urbackupclientbackend
	${URBACKUP_CORE_SOURCES}
	OpenSSLPipe.cpp
	${URBACKUP_COMMON_SOURCES}
	${URBACKUP_CRYPTOPLUGIN_SOURCES}
	${URBACKUP_FSIMAGEPLUGIN_SOURCES}
	${URBACKUP_FILESERVPLUGIN_SOURCES}
	urbackupclient/dllmain.cpp urbackupclient/clientdao.cpp urbackupclient/client.cpp
	urbackupclient/ClientService.cpp urbackupclient/ClientSend.cpp urbackupclient/client_restore.cpp
	urbackupclient/ServerIdentityMgr.cpp urbackupclient/ClientServiceCMD.cpp urbackupclient/ImageReadFile.cpp urbackupclient/ImageThread.cpp
	urbackupclient/InternetClient.cpp urbackupclient/file_permissions.cpp urbackupclient/lin_ver.cpp
	urbackupclient/lin_tokens.cpp urbackupclient/common_tokens.cpp urbackupclient/FileMetadataDownloadThread.cpp
	urbackupclient/RestoreFiles.cpp urbackupclient/RestoreDownloadThread.cpp urbackupclient/TokenCallback.cpp
	urbackupclient/cmdline_preprocessor.cpp urbackupclient/ParallelHash.cpp urbackupclient/ClientHash.cpp
	urbackupclient/SambaService.cpp urbackupclient/CachedFile.cpp)

urbackup_setup_backend(urbackupclientbackend ${URBACKUP_CLIENT_CONFIG_DIR})
urbackup_link_cryptopp(urbackupclientbackend)
target_compile_definitions(urbackupclientbackend PRIVATE
	$<$<COMPILE_LANGUAGE:CXX>:VARDIR="${CMAKE_INSTALL_FULL_LOCALSTATEDIR}">
	$<$<COMPILE_LANGUAGE:CXX>:SYSCONFDIR="${CMAKE_INSTALL_FULL_SYSCONFDIR}">
	$<$<COMPILE_LANGUAGE:CXX>:DATADIR="${CMAKE_INSTALL_FULL_DATADIR}">)
if(URBACKUP_WITH_C_ARES)
	target_compile_definitions(urbackupclientbackend PRIVATE ENABLE_C_ARES)
	target_link_libraries(urbackupclientbackend PRIVATE ${URBACKUP_CARES})
endif()
if(URBACKUP_CLIENT_UPDATE)
	target_compile_definitions(urbackupclientbackend PRIVATE $<$<COMPILE_LANGUAGE:CXX>:URB_WITH_CLIENTUPDATE>)
endif()
if(URBACKUP_CLIENT_OPENSSL)
	target_compile_definitions(urbackupclientbackend PRIVATE WITH_OPENSSL)
	target_link_libraries(urbackupclientbackend PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

#
# urbackupclientctl
#
add_executable(urbackupclientctl clientctl/main.cpp urbackupcommon/os_functions_lin.cpp stringtools.cpp
	clientctl/Connector.cpp clientctl/tcpstack.cpp urbackupcommon/escape.cpp)
target_include_directories(urbackupclientctl PRIVATE ${URBACKUP_CLIENT_CONFIG_DIR} ${CMAKE_SOURCE_DIR} ${TCLAP_INCLUDE_DIR} ${UTF8CPP_INCLUDE_DIR})
target_link_libraries(urbackupclientctl PRIVATE ${URBACKUP_JSONCPP})
target_compile_definitions(urbackupclientctl PRIVATE OS_FUNC_NO_SERVER LINUX
	VARDIR="${CMAKE_INSTALL_FULL_LOCALSTATEDIR}")

#
# blockalign
#
add_executable(blockalign blockalign_src/main.cpp blockalign_src/crc32c-adler.cpp blockalign_src/crc.cpp)
target_compile_definitions(blockalign PRIVATE _FILE_OFFSET_BITS=64)
check_cxx_compiler_flag("-msse4.1 -msse4.2" URBACKUP_HAVE_SSE4_2)
if(URBACKUP_HAVE_SSE4_2)
	target_compile_options(blockalign PRIVATE -msse4.2)
endif()
urbackup_fortify(blockalign)

#
# urbackupclient_dmsnaptool
#
if(DEVMAPPER_LIBRARY AND DEVMAPPER_INCLUDE_DIR)
	add_executable(urbackupclient_dmsnaptool dmsnaptool/main.cpp stringtools.cpp)
	target_include_directories(urbackupclient_dmsnaptool PRIVATE ${DEVMAPPER_INCLUDE_DIR} ${TCLAP_INCLUDE_DIR} ${UTF8CPP_INCLUDE_DIR})
	target_link_libraries(urbackupclient_dmsnaptool PRIVATE ${DEVMAPPER_LIBRARY})
	install(TARGETS urbackupclient_dmsnaptool RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
endif()

#
# urbackupclientgui
#
if(NOT URBACKUP_CLIENT_HEADLESS)
	set(_gui ${URBACKUP_FRONTEND_DIR})
	add_executable(urbackupclientgui
		${_gui}/gui/GUI.cpp ${_gui}/main.cpp ${_gui}/ConfigPath.cpp ${_gui}/Connector.cpp ${_gui}/escape.cpp
		${_gui}/FileSettingsReader.cpp ${_gui}/Info.cpp ${_gui}/Logs.cpp ${_gui}/Settings.cpp
		${_gui}/stringtools.cpp ${_gui}/TaskBarBaloon.cpp ${_gui}/tcpstack.cpp ${_gui}/TrayIcon.cpp
		${_gui}/Status.cpp ${_gui}/jsoncpp.cpp ${_gui}/TranslationHelper.cpp ${_gui}/gui/GUISetupWizard.cpp
		${_gui}/SetupWizard.cpp ${_gui}/CbtStatus.cpp)
	# The frontend sources include "../config.h", resolve it relative to a directory below config.h
	file(MAKE_DIRECTORY ${URBACKUP_CLIENT_CONFIG_DIR}/frontend)
	target_include_directories(urbackupclientgui PRIVATE ${URBACKUP_CLIENT_CONFIG_DIR}/frontend)
	target_compile_definitions(urbackupclientgui PRIVATE LINUX
		DATADIR="${CMAKE_INSTALL_FULL_DATADIR}"
		VARDIR="${CMAKE_INSTALL_FULL_LOCALSTATEDIR}"
		SBINDIR="${CMAKE_INSTALL_FULL_SBINDIR}"
		BINDIR="${CMAKE_INSTALL_FULL_BINDIR}")

	# vcpkg provides a config package, otherwise fall back to wx-config via FindwxWidgets
	find_package(wxWidgets CONFIG QUIET)
	if(TARGET wx::core)
		set(_wx_libs "")
		foreach(lib core base net xml xrc html adv)
			if(TARGET wx::${lib})
				list(APPEND _wx_libs wx::${lib})
			endif()
		endforeach()
		target_link_libraries(urbackupclientgui PRIVATE ${_wx_libs})
	else()
		# FindwxWidgets only searches CMAKE_FIND_ROOT_PATH, which the vcpkg toolchain points to its own tree
		find_program(wxWidgets_CONFIG_EXECUTABLE NAMES wx-config wx-config-3.2 wx-config-3.0 NO_CMAKE_FIND_ROOT_PATH)
		find_package(wxWidgets 2.9.0 REQUIRED)
		target_include_directories(urbackupclientgui PRIVATE ${wxWidgets_INCLUDE_DIRS})
		target_compile_definitions(urbackupclientgui PRIVATE ${wxWidgets_DEFINITIONS})
		target_compile_options(urbackupclientgui PRIVATE ${wxWidgets_CXX_FLAGS})
		target_link_libraries(urbackupclientgui PRIVATE ${wxWidgets_LIBRARIES})
	endif()

	if(APPLE)
		enable_language(OBJCXX)
		target_sources(urbackupclientgui PRIVATE ${_gui}/osxutils.mm)
		target_link_libraries(urbackupclientgui PRIVATE "-framework ServiceManagement" "-framework CoreServices" "-framework Security")
	endif()
endif()

#
# Install
#
set(_etc ${CMAKE_INSTALL_FULL_SYSCONFDIR})
set(_var ${CMAKE_INSTALL_FULL_LOCALSTATEDIR})
set(_share ${CMAKE_INSTALL_FULL_DATADIR})

install(TARGETS urbackupclientbackend RUNTIME DESTINATION ${CMAKE_INSTALL_SBINDIR})
install(TARGETS urbackupclientctl blockalign RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(NOT URBACKUP_CLIENT_HEADLESS)
	install(TARGETS urbackupclientgui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

set(_exec_perms OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
function(urbackup_install_default_file src dest_dir dest_name)
	install(CODE "
		if(NOT EXISTS \"\$ENV{DESTDIR}${dest_dir}/${dest_name}\")
			file(INSTALL \"${src}\" DESTINATION \"${dest_dir}\" RENAME \"${dest_name}\" FILE_PERMISSIONS ${_exec_perms})
		endif()")
endfunction()

if(URBACKUP_INSTALL_DEBIAN_INITD)
	install(PROGRAMS init.d_client DESTINATION ${_etc}/init.d RENAME urbackupclientbackend)
	urbackup_install_default_file(${CMAKE_SOURCE_DIR}/defaults_client ${_etc}/default urbackupclientbackend)
endif()
if(URBACKUP_INSTALL_REDHAT_INITD)
	install(PROGRAMS init.d_client_rh DESTINATION ${_etc}/init.d RENAME urbackupclientbackend)
	urbackup_install_default_file(${CMAKE_SOURCE_DIR}/defaults_client ${_etc}/sysconfig urbackupclientbackend)
endif()

install(DIRECTORY DESTINATION ${_var}/urbackup)
install(DIRECTORY DESTINATION ${_var}/urbackup/data)

# Backup scripts get SYSCONFDIR replaced with the actual configuration directory
set(_scripts list list_incr mariadbdump postgresqldump postgresbase postgresqlprebackup postgresqlpostbackup
	setup-postgresbackup mariadbxtrabackup mariadbxtrabackup_incr restore-mariadbbackup mariadbprebackup
	mariadbpostbackup setup-mariadbbackup)
set(_script_confs mariadbdump.conf postgresqldump.conf postgresbase.conf mariadbxtrabackup.conf)
set(_scripts_out ${CMAKE_BINARY_DIR}/generated/client/scripts)
foreach(script IN LISTS _scripts)
	set(_src ${CMAKE_SOURCE_DIR}/urbackupclient/backup_scripts/${script})
	set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_src})
	file(READ ${_src} _content)
	string(REPLACE "SYSCONFDIR" "${_etc}/urbackup" _content "${_content}")
	file(WRITE ${_scripts_out}/${script} "${_content}")
	install(FILES ${_scripts_out}/${script} DESTINATION ${_share}/urbackup/scripts
		PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
endforeach()
foreach(conf IN LISTS _script_confs)
	install(CODE "
		if(NOT EXISTS \"\$ENV{DESTDIR}${_etc}/urbackup/${conf}\")
			file(INSTALL \"${CMAKE_SOURCE_DIR}/urbackupclient/backup_scripts/${conf}\" DESTINATION \"${_etc}/urbackup\")
		endif()")
endforeach()
install(CODE "
	file(GLOB _confs \"\$ENV{DESTDIR}${_etc}/urbackup/*\")
	if(_confs)
		execute_process(COMMAND chmod 700 \${_confs})
	endif()")

if(URBACKUP_HAVE_FRONTEND)
	install(FILES ${URBACKUP_FRONTEND_DIR}/version.txt DESTINATION ${_var}/urbackup)
	install(FILES ${URBACKUP_FRONTEND_DIR}/data/urbackup_ecdsa409k1.pub ${URBACKUP_FRONTEND_DIR}/data/updates_h.dat
		DESTINATION ${_share}/urbackup)
endif()

if(NOT URBACKUP_CLIENT_HEADLESS)
	file(GLOB _gui_data ${URBACKUP_FRONTEND_DIR}/data/*.png ${URBACKUP_FRONTEND_DIR}/*.png)
	if(APPLE)
		file(GLOB _gui_icons ${URBACKUP_FRONTEND_DIR}/data/*.ico)
	else()
		file(GLOB _gui_icons ${URBACKUP_FRONTEND_DIR}/data/*.xpm)
	endif()
	install(FILES ${_gui_data} ${_gui_icons} ${URBACKUP_FRONTEND_DIR}/info.txt DESTINATION ${_share}/urbackup)
	file(GLOB _langs RELATIVE ${URBACKUP_FRONTEND_DIR}/data/lang ${URBACKUP_FRONTEND_DIR}/data/lang/*/urbackup.mo)
	foreach(mo IN LISTS _langs)
		get_filename_component(_lang ${mo} DIRECTORY)
		install(FILES ${URBACKUP_FRONTEND_DIR}/data/lang/${mo} DESTINATION ${_share}/urbackup/lang/${_lang})
	endforeach()
endif()

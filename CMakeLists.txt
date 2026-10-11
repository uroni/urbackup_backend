cmake_minimum_required(VERSION 3.18)

# Version numbers (previously in configure.ac_server / configure.ac_client)
set(URBACKUP_SERVER_VERSION 2.5.38.0)
set(URBACKUP_CLIENT_VERSION 2.5.32)
set(URBACKUP_BUILDID 0 CACHE STRING "Build id appended to the client version")

option(URBACKUP_BUILD_SERVER "Build the UrBackup server (urbackupsrv)" ON)
option(URBACKUP_BUILD_CLIENT "Build the UrBackup client (urbackupclientbackend)" OFF)

option(URBACKUP_CLIENT_HEADLESS "Do not compile and install the client GUI components" OFF)
option(URBACKUP_GUI_FROM_VCPKG "Install wxWidgets for the client GUI via vcpkg instead of using the system wx-config" OFF)
option(URBACKUP_USE_SYSTEM_LIBS "Use system libraries (e.g. Debian packages) instead of vcpkg" OFF)

# vcpkg settings have to be in place before project() runs the vcpkg toolchain
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
	set(VCPKG_OVERLAY_TRIPLETS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vcpkg-triplets)
endif()
if(URBACKUP_BUILD_CLIENT AND NOT URBACKUP_CLIENT_HEADLESS AND URBACKUP_GUI_FROM_VCPKG
	AND NOT "gui" IN_LIST VCPKG_MANIFEST_FEATURES)
	list(APPEND VCPKG_MANIFEST_FEATURES gui)
endif()
if(NOT DEFINED VCPKG_MANIFEST_NO_DEFAULT_FEATURES)
	# With system libraries vcpkg only installs what distributions do not package (the base dependencies).
	# The "server" feature (libcurl, Lua) is not needed by the client.
	set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES ON)
	if(NOT URBACKUP_USE_SYSTEM_LIBS AND NOT "libs" IN_LIST VCPKG_MANIFEST_FEATURES)
		list(APPEND VCPKG_MANIFEST_FEATURES libs)
	endif()
	if(NOT URBACKUP_USE_SYSTEM_LIBS AND URBACKUP_BUILD_SERVER AND NOT "server" IN_LIST VCPKG_MANIFEST_FEATURES)
		list(APPEND VCPKG_MANIFEST_FEATURES server)
	endif()
endif()
if(NOT URBACKUP_USE_SYSTEM_LIBS AND URBACKUP_BUILD_CLIENT AND URBACKUP_WITH_C_ARES
	AND NOT "c-ares" IN_LIST VCPKG_MANIFEST_FEATURES)
	list(APPEND VCPKG_MANIFEST_FEATURES c-ares)
endif()

project(urbackup LANGUAGES C CXX)

if(NOT VCPKG_TOOLCHAIN)
	message(FATAL_ERROR "UrBackup's dependencies are installed via vcpkg. Configure with one of the CMake presets "
		"(e.g. cmake --preset server, with VCPKG_ROOT set) or with -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake. "
		"With URBACKUP_USE_SYSTEM_LIBS (the *-system presets) vcpkg only provides the libraries distributions do not package.")
endif()
if(URBACKUP_USE_SYSTEM_LIBS)
	# Distributions do not ship CMake packages for all dependencies (e.g. Crypto++), use pkg-config for those
	find_package(PkgConfig REQUIRED)
endif()

if(NOT URBACKUP_BUILD_SERVER AND NOT URBACKUP_BUILD_CLIENT)
	message(FATAL_ERROR "Enable at least one of URBACKUP_BUILD_SERVER or URBACKUP_BUILD_CLIENT")
endif()

if(EXISTS "${CMAKE_SOURCE_DIR}/config.h")
	message(FATAL_ERROR "Found stale ${CMAKE_SOURCE_DIR}/config.h from an autotools build. Remove it, it shadows the CMake generated one.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
	OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
include(CheckIncludeFile)
include(CheckFunctionExists)
include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)
include(CheckLinkerFlag)
include(CMakeDependentOption)
include(CMakePushCheckState)
find_package(PkgConfig QUIET)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Options shared by server and client
option(URBACKUP_ENABLE_ASSERTIONS "Enable assertions (bug finding)" OFF)
option(URBACKUP_ENABLE_FORTIFY "Enable GCC/runtime fortification" ON)
option(URBACKUP_EMBEDDED_SQLITE3 "Use the embedded sqlite3. Disabling is not recommended" ON)

# Server options
option(URBACKUP_WITH_MAIL "Enable server mailing features (requires libcurl)" ON)
option(URBACKUP_WITH_CRYPTO "Enable cryptography functions (internet mode, requires Crypto++)" ON)
option(URBACKUP_WITH_MOUNTVHD "Enable mounting of VHD files via fuse" OFF)
option(URBACKUP_EMBEDDED_LMDB "Use the embedded lmdb. Disabling is not recommended" ON)
option(URBACKUP_INSTALL_INITD "Install the init.d/default/logrotate files into sysconfdir (server)" OFF)
option(URBACKUP_PACKAGING "Install for packaging (no user creation/chown during install)" OFF)

# Client options
option(URBACKUP_CLIENT_UPDATE "Enable the internal client update functionality" OFF)
option(URBACKUP_WITH_C_ARES "Use c-ares for DNS resolution instead of the system libc" OFF)
option(URBACKUP_WITH_OPENSSL "Use OpenSSL if available (web socket/https proxy support)" ON)
option(URBACKUP_INSTALL_DEBIAN_INITD "Install the Debian init.d file (client)" OFF)
option(URBACKUP_INSTALL_REDHAT_INITD "Install the RedHat init.d file (client)" OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Android")
	set(URBACKUP_LINUX TRUE)
	# musl >= 1.2.4 only declares stat64, open64, ... with this (glibc has it via _GNU_SOURCE)
	add_compile_definitions(_LARGEFILE64_SOURCE)
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "FreeBSD")
	set(URBACKUP_FREEBSD TRUE)
endif()

#
# Assertions: autotools always passed -DNDEBUG unless --enable-assertions
#
if(URBACKUP_ENABLE_ASSERTIONS)
	foreach(lang C CXX)
		foreach(cfg RELEASE RELWITHDEBINFO MINSIZEREL)
			string(REPLACE "-DNDEBUG" "" CMAKE_${lang}_FLAGS_${cfg} "${CMAKE_${lang}_FLAGS_${cfg}}")
		endforeach()
	endforeach()
else()
	add_compile_definitions(NDEBUG)
endif()

#
# Hardening flags
#
set(URBACKUP_FORTIFY_COMPILE_FLAGS "")
set(URBACKUP_FORTIFY_LINK_FLAGS "")
if(URBACKUP_ENABLE_FORTIFY)
	cmake_push_check_state(RESET)
	set(_fortify_flags -fstack-protector-strong --param=ssp-buffer-size=4 -Wformat -Werror=format-security)
	string(REPLACE ";" " " CMAKE_REQUIRED_FLAGS "${_fortify_flags}")
	check_cxx_compiler_flag("" URBACKUP_HAVE_FORTIFY_FLAGS)
	cmake_pop_check_state()
	check_linker_flag(CXX "-Wl,-z,relro" URBACKUP_HAVE_RELRO)
	if(URBACKUP_HAVE_FORTIFY_FLAGS AND URBACKUP_HAVE_RELRO)
		set(URBACKUP_FORTIFY_COMPILE_FLAGS ${_fortify_flags} -D_FORTIFY_SOURCE=2)
		set(URBACKUP_FORTIFY_LINK_FLAGS "-Wl,-z,relro")
	endif()
endif()

function(urbackup_fortify target)
	target_compile_options(${target} PRIVATE ${URBACKUP_FORTIFY_COMPILE_FLAGS})
	target_link_options(${target} PRIVATE ${URBACKUP_FORTIFY_LINK_FLAGS})
endfunction()

#
# Platform checks (config.h)
#
check_include_file(mntent.h HAVE_MNTENT_H)
check_include_file(spawn.h HAVE_SPAWN_H)
check_include_file(linux/fiemap.h HAVE_LINUX_FIEMAP_H)
check_include_file(linux/fs.h HAVE_LINUX_FS_H)
check_include_file(sys/random.h HAVE_SYS_RANDOM_H)
check_include_file(malloc.h HAVE_MALLOC_H)
check_function_exists(accept4 HAVE_ACCEPT4)
check_function_exists(fallocate64 HAVE_FALLOCATE64)
check_function_exists(syncfs HAVE_SYNCFS)
check_function_exists(utimensat HAVE_UTIMENSAT)
check_function_exists(getrandom HAVE_GETRANDOM)

# The server configure did not check for these headers, so the server never used them
set(URBACKUP_SERVER_CONFIG_CHECKS HAVE_LINUX_FIEMAP_H HAVE_SYS_RANDOM_H
	HAVE_ACCEPT4 HAVE_FALLOCATE64 HAVE_SYNCFS HAVE_UTIMENSAT HAVE_GETRANDOM)
set(URBACKUP_CLIENT_CONFIG_CHECKS ${URBACKUP_SERVER_CONFIG_CHECKS}
	HAVE_MNTENT_H HAVE_SPAWN_H HAVE_LINUX_FS_H HAVE_MALLOC_H)

#
# Dependencies
#
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)

set(URBACKUP_SYSTEM_LIBS Threads::Threads ZLIB::ZLIB ${CMAKE_DL_LIBS})
if(NOT APPLE)
	# clock_gettime is in librt on older glibc
	check_function_exists(clock_gettime URBACKUP_HAVE_CLOCK_GETTIME)
	if(NOT URBACKUP_HAVE_CLOCK_GETTIME)
		find_library(RT_LIBRARY rt)
		if(NOT RT_LIBRARY)
			message(FATAL_ERROR "No library for clock_gettime found")
		endif()
		list(APPEND URBACKUP_SYSTEM_LIBS ${RT_LIBRARY})
	endif()
endif()

# Crypto++
set(CRYPTOPP_INCLUDE_PREFIX cryptopp)
if(URBACKUP_BUILD_CLIENT OR (URBACKUP_BUILD_SERVER AND URBACKUP_WITH_CRYPTO))
	if(URBACKUP_USE_SYSTEM_LIBS)
		# libcrypto++ (Debian), libcryptopp (Fedora)
		pkg_search_module(CRYPTOPP REQUIRED IMPORTED_TARGET libcrypto++ libcryptopp)
		set(URBACKUP_CRYPTOPP PkgConfig::CRYPTOPP)
		cmake_push_check_state(RESET)
		set(CMAKE_REQUIRED_LIBRARIES ${URBACKUP_CRYPTOPP})
		set(CMAKE_REQUIRED_QUIET ON)
		check_cxx_source_compiles("#include <cryptopp/cryptlib.h>\nint main(){return 0;}" URBACKUP_CRYPTOPP_PREFIX_CRYPTOPP)
		cmake_pop_check_state()
		if(NOT URBACKUP_CRYPTOPP_PREFIX_CRYPTOPP)
			set(CRYPTOPP_INCLUDE_PREFIX crypto++)
		endif()
	else()
		find_package(cryptopp CONFIG REQUIRED)
		set(URBACKUP_CRYPTOPP cryptopp::cryptopp)
	endif()
endif()

# ZSTD
if(URBACKUP_USE_SYSTEM_LIBS)
	pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd>=1.4.0)
	set(URBACKUP_ZSTD PkgConfig::ZSTD)
else()
	find_package(zstd CONFIG REQUIRED)
	if(TARGET zstd::libzstd)
		set(URBACKUP_ZSTD zstd::libzstd)
	else()
		set(URBACKUP_ZSTD zstd::libzstd_static)
	endif()
endif()

# miniz (not packaged by distributions, always from vcpkg)
find_package(miniz CONFIG REQUIRED)

# Header-only libraries
find_path(TCLAP_INCLUDE_DIR tclap/CmdLine.h REQUIRED)
find_path(UTF8CPP_INCLUDE_DIR utf8cpp/utf8.h REQUIRED)

if(NOT URBACKUP_EMBEDDED_SQLITE3)
	find_package(SQLite3 3.8.7 REQUIRED)
endif()

#
# Generated config.h, one per program (server and client used different configure scripts)
#
function(urbackup_write_config dir package version checks)
	set(content "/* Generated by CMake */\n#pragma once\n")
	string(APPEND content "#define PACKAGE \"${package}\"\n")
	string(APPEND content "#define PACKAGE_NAME \"${package}\"\n")
	string(APPEND content "#define PACKAGE_TARNAME \"${package}\"\n")
	string(APPEND content "#define PACKAGE_VERSION \"${version}\"\n")
	string(APPEND content "#define PACKAGE_STRING \"${package} ${version}\"\n")
	string(APPEND content "#define PACKAGE_BUGREPORT \"martin@urbackup.org\"\n")
	string(APPEND content "#define VERSION \"${version}\"\n")
	string(APPEND content "#define CRYPTOPP_INCLUDE_PREFIX ${CRYPTOPP_INCLUDE_PREFIX}\n")
	string(APPEND content "#define HAVE_LONG_LONG 1\n#define HAVE_SSTREAM 1\n")
	foreach(check IN LISTS checks)
		if(${check})
			string(APPEND content "#define ${check} 1\n")
		endif()
	endforeach()
	file(CONFIGURE OUTPUT ${dir}/config.h CONTENT "${content}" @ONLY)
endfunction()

#
# Sources shared by server and client
#
set(URBACKUP_CORE_SOURCES
	AcceptThread.cpp Client.cpp Database.cpp Query.cpp SelectThread.cpp Server.cpp ServerLinux.cpp
	ServiceAcceptor.cpp ServiceWorker.cpp SessionMgr.cpp StreamPipe.cpp Template.cpp WorkerThread.cpp
	main.cpp md5.cpp stringtools.cpp libfastcgi/fastcgi.cpp Mutex_lin.cpp LoadbalancerClient.cpp
	DBSettingsReader.cpp file_common.cpp file_fstream.cpp file_linux.cpp FileSettingsReader.cpp
	LookupService.cpp SettingsReader.cpp Table.cpp OutputStream.cpp ThreadPool.cpp MemoryPipe.cpp
	Condition_lin.cpp MemorySettingsReader.cpp sqlite/shell.c SQLiteFactory.cpp PipeThrottler.cpp
	mt19937ar.cpp DatabaseCursor.cpp SharedMutex_lin.cpp StaticPluginRegistration.cpp
	common/data.cpp common/adler32.cpp)

set(URBACKUP_COMMON_SOURCES
	urbackupcommon/os_functions_lin.cpp urbackupcommon/sha2/sha2.cpp urbackupcommon/fileclient/FileClient.cpp
	urbackupcommon/fileclient/tcpstack.cpp urbackupcommon/escape.cpp urbackupcommon/bufmgr.cpp
	urbackupcommon/json.cpp urbackupcommon/CompressedPipe.cpp urbackupcommon/InternetServicePipe2.cpp
	urbackupcommon/settingslist.cpp urbackupcommon/fileclient/FileClientChunked.cpp
	urbackupcommon/InternetServicePipe.cpp urbackupcommon/filelist_utils.cpp urbackupcommon/file_metadata.cpp
	urbackupcommon/glob.cpp urbackupcommon/chunk_hasher.cpp urbackupcommon/CompressedPipe2.cpp
	urbackupcommon/SparseFile.cpp urbackupcommon/ExtentIterator.cpp urbackupcommon/TreeHash.cpp
	urbackupcommon/WalCheckpointThread.cpp urbackupcommon/WebSocketPipe.cpp)

set(URBACKUP_FSIMAGEPLUGIN_SOURCES
	fsimageplugin/dllmain.cpp fsimageplugin/filesystem.cpp fsimageplugin/FSImageFactory.cpp
	fsimageplugin/pluginmgr.cpp fsimageplugin/vhdfile.cpp fsimageplugin/vhdxfile.cpp fsimageplugin/fs/ntfs.cpp
	fsimageplugin/fs/unknown.cpp fsimageplugin/fs/partitioned.cpp fsimageplugin/CompressedFile.cpp fsimageplugin/LRUMemCache.cpp
	fsimageplugin/cowfile.cpp fsimageplugin/FileWrapper.cpp fsimageplugin/ClientBitmap.cpp fsimageplugin/NbdClient.cpp fsimageplugin/NbdService.cpp
	fsimageplugin/partclone.cpp)

set(URBACKUP_FILESERVPLUGIN_SOURCES
	fileservplugin/dllmain.cpp fileservplugin/bufmgr.cpp fileservplugin/CClientThread.cpp
	fileservplugin/CriticalSection.cpp fileservplugin/CTCPFileServ.cpp fileservplugin/CUDPThread.cpp
	fileservplugin/FileServ.cpp fileservplugin/FileServFactory.cpp fileservplugin/log.cpp fileservplugin/main.cpp
	fileservplugin/map_buffer.cpp fileservplugin/pluginmgr.cpp fileservplugin/ChunkSendThread.cpp
	fileservplugin/PipeFile.cpp fileservplugin/PipeSessions.cpp fileservplugin/PipeFileUnix.cpp
	fileservplugin/PipeFileBase.cpp fileservplugin/FileMetadataPipe.cpp fileservplugin/PipeFileTar.cpp
	fileservplugin/PipeFileExt.cpp)

set(URBACKUP_CRYPTOPLUGIN_SOURCES
	cryptoplugin/dllmain.cpp cryptoplugin/AESDecryption.cpp cryptoplugin/CryptoFactory.cpp
	cryptoplugin/pluginmgr.cpp cryptoplugin/AESEncryption.cpp cryptoplugin/ZlibCompression.cpp
	cryptoplugin/ZlibDecompression.cpp cryptoplugin/AESGCMDecryption.cpp cryptoplugin/AESGCMEncryption.cpp
	cryptoplugin/ECDHKeyExchange.cpp)

# Settings shared by urbackupsrv and urbackupclientbackend
function(urbackup_setup_backend target config_dir)
	target_include_directories(${target} PRIVATE ${config_dir} ${CMAKE_SOURCE_DIR} ${TCLAP_INCLUDE_DIR} ${UTF8CPP_INCLUDE_DIR})
	target_compile_definitions(${target} PRIVATE
		SQLITE_PREPARE_RETRIES=5
		$<$<COMPILE_LANGUAGE:CXX>:LINUX>
		$<$<COMPILE_LANGUAGE:CXX>:STATIC_PLUGIN>)
	target_link_libraries(${target} PRIVATE ${URBACKUP_SYSTEM_LIBS} miniz::miniz)
	if(URBACKUP_EMBEDDED_SQLITE3)
		target_sources(${target} PRIVATE sqlite/sqlite3.c)
		target_compile_definitions(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:SQLITE_ENABLE_UNLOCK_NOTIFY>)
	else()
		target_compile_definitions(${target} PRIVATE USE_SYSTEM_SQLITE)
		target_link_libraries(${target} PRIVATE SQLite::SQLite3)
	endif()
	target_sources(${target} PRIVATE urbackupcommon/CompressedPipeZstd.cpp)
	target_compile_definitions(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:ZSTD_STATIC_LINKING_ONLY>)
	target_link_libraries(${target} PRIVATE ${URBACKUP_ZSTD})
	urbackup_fortify(${target})
endfunction()

function(urbackup_link_cryptopp target)
	target_link_libraries(${target} PRIVATE ${URBACKUP_CRYPTOPP})
endfunction()

set(URBACKUP_COMPONENT_NAMES "")
if(URBACKUP_BUILD_SERVER)
	include(cmake/server.cmake)
	list(APPEND URBACKUP_COMPONENT_NAMES server)
endif()
if(URBACKUP_BUILD_CLIENT)
	include(cmake/client.cmake)
	list(APPEND URBACKUP_COMPONENT_NAMES client)
endif()

#
# Source package (replaces "make dist"): cmake --build <dir> --target package_source
#
if(URBACKUP_BUILD_SERVER)
	set(CPACK_SOURCE_PACKAGE_FILE_NAME "urbackup-server-${URBACKUP_SERVER_VERSION}")
else()
	set(CPACK_SOURCE_PACKAGE_FILE_NAME "urbackup-client-${URBACKUP_CLIENT_VERSION}.${URBACKUP_BUILDID}")
endif()
set(CPACK_SOURCE_GENERATOR TGZ)
set(CPACK_SOURCE_IGNORE_FILES
	"/\\\\.git/" "/\\\\.git$" "/\\\\.vs/" "/out/" "/vcpkg_installed/" "/obj-[^/]*/"
	"/build_[a-z_]*ok$" "/install-data" "/linux-installer" "/osx-pkg" "/UrBackupUpdate[^/]*$" "\\\\.o$" "\\\\.a$"
	"/x64/" "/ARM64/" "/Debug/" "/Release/" "/node_modules/")
if(NOT URBACKUP_BUILD_CLIENT)
	list(APPEND CPACK_SOURCE_IGNORE_FILES "/client/")
endif()
include(CPack)

message(STATUS "UrBackup components: ${URBACKUP_COMPONENT_NAMES}")

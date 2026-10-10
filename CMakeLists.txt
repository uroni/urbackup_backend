cmake_minimum_required(VERSION 3.18)

# Version numbers (previously in configure.ac_server / configure.ac_client)
set(URBACKUP_SERVER_VERSION 2.5.38.0)
set(URBACKUP_CLIENT_VERSION 2.5.32)
set(URBACKUP_BUILDID 0 CACHE STRING "Build id appended to the client version")

option(URBACKUP_BUILD_SERVER "Build the UrBackup server (urbackupsrv)" ON)
option(URBACKUP_BUILD_CLIENT "Build the UrBackup client (urbackupclientbackend)" OFF)

option(URBACKUP_CLIENT_HEADLESS "Do not compile and install the client GUI components" OFF)
option(URBACKUP_GUI_FROM_VCPKG "Install wxWidgets for the client GUI via vcpkg instead of using the system wx-config" OFF)

# vcpkg settings have to be in place before project() runs the vcpkg toolchain
if(NOT DEFINED VCPKG_OVERLAY_TRIPLETS)
	set(VCPKG_OVERLAY_TRIPLETS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vcpkg-triplets)
endif()
if(URBACKUP_BUILD_CLIENT AND NOT URBACKUP_CLIENT_HEADLESS AND URBACKUP_GUI_FROM_VCPKG
	AND NOT "gui" IN_LIST VCPKG_MANIFEST_FEATURES)
	list(APPEND VCPKG_MANIFEST_FEATURES gui)
endif()
if(NOT URBACKUP_BUILD_SERVER AND NOT DEFINED VCPKG_MANIFEST_NO_DEFAULT_FEATURES)
	# The default "server" feature pulls in libcurl, which the client does not need
	set(VCPKG_MANIFEST_NO_DEFAULT_FEATURES ON)
endif()
if(URBACKUP_BUILD_CLIENT AND URBACKUP_WITH_C_ARES AND NOT "c-ares" IN_LIST VCPKG_MANIFEST_FEATURES)
	list(APPEND VCPKG_MANIFEST_FEATURES c-ares)
endif()

project(urbackup LANGUAGES C CXX)

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
option(URBACKUP_EMBEDDED_ZSTD "Compile and use ZSTD included with the source distribution" OFF)
option(URBACKUP_EMBEDDED_SQLITE3 "Use the embedded sqlite3. Disabling is not recommended" ON)

# Server options
option(URBACKUP_WITH_MAIL "Enable server mailing features (requires libcurl)" ON)
option(URBACKUP_WITH_CRYPTO "Enable cryptography functions (internet mode, requires Crypto++)" ON)
option(URBACKUP_WITH_MOUNTVHD "Enable mounting of VHD files via fuse" OFF)
option(URBACKUP_EMBEDDED_LUA "Use the embedded lua. Disabling is not recommended" ON)
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

# Crypto++: vcpkg config package, then pkg-config, then plain library search.
# Set CRYPTOPP_LIBRARY (and CRYPTOPP_INCLUDE_DIR) to force a specific library, e.g. a static one.
set(URBACKUP_NEED_CRYPTOPP FALSE)
if(URBACKUP_BUILD_CLIENT OR (URBACKUP_BUILD_SERVER AND URBACKUP_WITH_CRYPTO))
	set(URBACKUP_NEED_CRYPTOPP TRUE)
endif()
if(URBACKUP_NEED_CRYPTOPP)
	if(NOT CRYPTOPP_LIBRARY)
		find_package(cryptopp CONFIG QUIET)
	endif()
	if(TARGET cryptopp::cryptopp)
		set(URBACKUP_CRYPTOPP cryptopp::cryptopp)
	else()
		if(NOT CRYPTOPP_LIBRARY AND PKG_CONFIG_FOUND)
			pkg_check_modules(CRYPTOPP_PC QUIET IMPORTED_TARGET libcrypto++)
			if(NOT CRYPTOPP_PC_FOUND)
				pkg_check_modules(CRYPTOPP_PC QUIET IMPORTED_TARGET libcryptopp)
			endif()
		endif()
		if(CRYPTOPP_PC_FOUND)
			set(URBACKUP_CRYPTOPP PkgConfig::CRYPTOPP_PC)
		else()
			find_path(CRYPTOPP_INCLUDE_DIR NAMES cryptopp/cryptlib.h crypto++/cryptlib.h)
			find_library(CRYPTOPP_LIBRARY NAMES cryptopp crypto++)
			if(NOT CRYPTOPP_INCLUDE_DIR OR NOT CRYPTOPP_LIBRARY)
				message(FATAL_ERROR "Crypto++ not found. Install it via vcpkg or the system package manager (cryptopp-devel/libcrypto++-dev), or set CRYPTOPP_LIBRARY and CRYPTOPP_INCLUDE_DIR.")
			endif()
			add_library(urbackup_cryptopp INTERFACE)
			target_include_directories(urbackup_cryptopp INTERFACE ${CRYPTOPP_INCLUDE_DIR})
			target_link_libraries(urbackup_cryptopp INTERFACE ${CRYPTOPP_LIBRARY})
			set(URBACKUP_CRYPTOPP urbackup_cryptopp)
		endif()
	endif()

	cmake_push_check_state(RESET)
	set(CMAKE_REQUIRED_LIBRARIES ${URBACKUP_CRYPTOPP} Threads::Threads)
	set(CMAKE_REQUIRED_QUIET ON)
	check_cxx_source_compiles("#include <cryptopp/cryptlib.h>\nint main(){return 0;}" URBACKUP_CRYPTOPP_PREFIX_CRYPTOPP)
	if(URBACKUP_CRYPTOPP_PREFIX_CRYPTOPP)
		set(CRYPTOPP_INCLUDE_PREFIX cryptopp)
	else()
		set(CRYPTOPP_INCLUDE_PREFIX crypto++)
	endif()
	check_cxx_source_compiles("#include <${CRYPTOPP_INCLUDE_PREFIX}/misc.h>\nint main(){CryptoPP::AlignedAllocate(5);return 0;}" URBACKUP_CRYPTOPP_ALIGNED_ALLOCATE_MISC)
	check_cxx_source_compiles("#include <${CRYPTOPP_INCLUDE_PREFIX}/allocate.h>\nint main(){CryptoPP::AlignedAllocate(5);return 0;}" URBACKUP_CRYPTOPP_ALIGNED_ALLOCATE)
	cmake_pop_check_state()
	set(URBACKUP_CRYPTOPP_DEFINITIONS "")
	if(NOT URBACKUP_CRYPTOPP_ALIGNED_ALLOCATE_MISC AND NOT URBACKUP_CRYPTOPP_ALIGNED_ALLOCATE)
		set(URBACKUP_CRYPTOPP_DEFINITIONS CRYPTOPP_DISABLE_ASM)
	endif()
else()
	set(CRYPTOPP_INCLUDE_PREFIX cryptopp)
endif()

# ZSTD
set(URBACKUP_WITH_ZSTD FALSE)
if(URBACKUP_EMBEDDED_ZSTD)
	set(ZSTD_DIR ${CMAKE_SOURCE_DIR}/external/zstd)
	add_library(urbackup_zstd STATIC
		${ZSTD_DIR}/common/debug.c
		${ZSTD_DIR}/common/entropy_common.c
		${ZSTD_DIR}/common/error_private.c
		${ZSTD_DIR}/common/fse_decompress.c
		${ZSTD_DIR}/common/pool.c
		${ZSTD_DIR}/common/threading.c
		${ZSTD_DIR}/common/xxhash.c
		${ZSTD_DIR}/common/zstd_common.c
		${ZSTD_DIR}/compress/fse_compress.c
		${ZSTD_DIR}/compress/hist.c
		${ZSTD_DIR}/compress/huf_compress.c
		${ZSTD_DIR}/compress/zstdmt_compress.c
		${ZSTD_DIR}/compress/zstd_compress.c
		${ZSTD_DIR}/compress/zstd_compress_literals.c
		${ZSTD_DIR}/compress/zstd_compress_sequences.c
		${ZSTD_DIR}/compress/zstd_double_fast.c
		${ZSTD_DIR}/compress/zstd_fast.c
		${ZSTD_DIR}/compress/zstd_lazy.c
		${ZSTD_DIR}/compress/zstd_ldm.c
		${ZSTD_DIR}/compress/zstd_opt.c
		${ZSTD_DIR}/decompress/huf_decompress.c
		${ZSTD_DIR}/decompress/zstd_ddict.c
		${ZSTD_DIR}/decompress/zstd_decompress.c
		${ZSTD_DIR}/decompress/zstd_decompress_block.c
		${ZSTD_DIR}/deprecated/zbuff_common.c
		${ZSTD_DIR}/deprecated/zbuff_compress.c
		${ZSTD_DIR}/deprecated/zbuff_decompress.c
		${ZSTD_DIR}/dictBuilder/cover.c
		${ZSTD_DIR}/dictBuilder/divsufsort.c
		${ZSTD_DIR}/dictBuilder/fastcover.c
		${ZSTD_DIR}/dictBuilder/zdict.c)
	target_include_directories(urbackup_zstd PUBLIC ${ZSTD_DIR} ${ZSTD_DIR}/common)
	target_compile_definitions(urbackup_zstd PUBLIC XXH_NAMESPACE=ZSTD_)
	set(URBACKUP_ZSTD urbackup_zstd)
	set(URBACKUP_WITH_ZSTD TRUE)
else()
	find_package(zstd CONFIG QUIET)
	if(TARGET zstd::libzstd)
		set(URBACKUP_ZSTD zstd::libzstd)
	elseif(TARGET zstd::libzstd_static)
		set(URBACKUP_ZSTD zstd::libzstd_static)
	elseif(TARGET zstd::libzstd_shared)
		set(URBACKUP_ZSTD zstd::libzstd_shared)
	elseif(PKG_CONFIG_FOUND)
		pkg_check_modules(ZSTD_PC QUIET IMPORTED_TARGET libzstd)
		if(ZSTD_PC_FOUND)
			set(URBACKUP_ZSTD PkgConfig::ZSTD_PC)
		endif()
	endif()
	if(URBACKUP_ZSTD)
		cmake_push_check_state(RESET)
		set(CMAKE_REQUIRED_LIBRARIES ${URBACKUP_ZSTD})
		set(CMAKE_REQUIRED_QUIET ON)
		check_cxx_source_compiles("#include <zstd.h>\nint main(){ZSTD_CCtx_setParameter(0, ZSTD_c_compressionLevel, 1);return 0;}" URBACKUP_ZSTD_RECENT)
		cmake_pop_check_state()
		if(URBACKUP_ZSTD_RECENT)
			set(URBACKUP_WITH_ZSTD TRUE)
		endif()
	endif()
	if(NOT URBACKUP_WITH_ZSTD)
		message(WARNING "ZSTD compression library not present or not recent enough. Compiling without ZSTD support. Install zstd or use -DURBACKUP_EMBEDDED_ZSTD=ON")
	endif()
endif()

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
	common/data.cpp common/adler32.cpp common/miniz.c)

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
	fsimageplugin/fs/unknown.cpp fsimageplugin/CompressedFile.cpp fsimageplugin/LRUMemCache.cpp
	fsimageplugin/cowfile.cpp fsimageplugin/FileWrapper.cpp fsimageplugin/ClientBitmap.cpp
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
	target_include_directories(${target} PRIVATE ${config_dir} ${CMAKE_SOURCE_DIR})
	target_compile_definitions(${target} PRIVATE
		SQLITE_PREPARE_RETRIES=5
		$<$<COMPILE_LANGUAGE:CXX>:LINUX>
		$<$<COMPILE_LANGUAGE:CXX>:STATIC_PLUGIN>)
	target_link_libraries(${target} PRIVATE ${URBACKUP_SYSTEM_LIBS})
	if(URBACKUP_EMBEDDED_SQLITE3)
		target_sources(${target} PRIVATE sqlite/sqlite3.c)
		target_compile_definitions(${target} PRIVATE $<$<COMPILE_LANGUAGE:C>:SQLITE_ENABLE_UNLOCK_NOTIFY>)
	else()
		target_compile_definitions(${target} PRIVATE USE_SYSTEM_SQLITE)
		target_link_libraries(${target} PRIVATE SQLite::SQLite3)
	endif()
	if(URBACKUP_WITH_ZSTD)
		target_sources(${target} PRIVATE urbackupcommon/CompressedPipeZstd.cpp)
		target_compile_definitions(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:ZSTD_STATIC_LINKING_ONLY>)
		target_link_libraries(${target} PRIVATE ${URBACKUP_ZSTD})
	else()
		target_compile_definitions(${target} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:NO_ZSTD_COMPRESSION>)
	endif()
	urbackup_fortify(${target})
endfunction()

function(urbackup_link_cryptopp target)
	target_link_libraries(${target} PRIVATE ${URBACKUP_CRYPTOPP})
	target_compile_definitions(${target} PRIVATE ${URBACKUP_CRYPTOPP_DEFINITIONS})
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
	"/build_[a-z_]*ok$" "/install-data" "/linux-installer" "/osx-pkg" "\\\\.o$" "\\\\.a$"
	"/x64/" "/ARM64/" "/Debug/" "/Release/" "/node_modules/")
if(NOT URBACKUP_BUILD_CLIENT)
	list(APPEND CPACK_SOURCE_IGNORE_FILES "/client/")
endif()
include(CPack)

message(STATUS "UrBackup components: ${URBACKUP_COMPONENT_NAMES}")
message(STATUS "  ZSTD: ${URBACKUP_WITH_ZSTD} (embedded: ${URBACKUP_EMBEDDED_ZSTD})")
message(STATUS "  Crypto++ include prefix: ${CRYPTOPP_INCLUDE_PREFIX}")

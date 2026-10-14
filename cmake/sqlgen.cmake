# SQLGen: regenerates the SQL access code in the DAO sources (@SQLGenAccess annotations).
# Not built by default. Run with e.g.: cmake --build <dir> --target sqlgen
# It rewrites the DAO .cpp/.h files in the source tree, using the schema of existing databases.

set(URBACKUP_SQLGEN_SERVER_DB_DIR ${CMAKE_SOURCE_DIR}/urbackup CACHE PATH
	"Directory with the server databases (backup_server*.db) SQLGen uses for the server DAOs")
set(URBACKUP_SQLGEN_CLIENT_DB ${CMAKE_SOURCE_DIR}/urbackup/backup_client.db CACHE FILEPATH
	"Client database SQLGen uses for the client DAOs")

set(URBACKUP_SQLGEN_CONFIG_DIR ${CMAKE_BINARY_DIR}/generated/sqlgen)
urbackup_write_config(${URBACKUP_SQLGEN_CONFIG_DIR} sqlgen 1.0 "${URBACKUP_SERVER_CONFIG_CHECKS}")

set(URBACKUP_SQLGEN_CORE_SOURCES ${URBACKUP_CORE_SOURCES})
# SQLGen/main.cpp has its own main() and globals
list(REMOVE_ITEM URBACKUP_SQLGEN_CORE_SOURCES main.cpp)

add_executable(urbackup_sqlgen EXCLUDE_FROM_ALL
	${URBACKUP_SQLGEN_CORE_SOURCES} SQLGen/main.cpp SQLGen/sqlgen.cpp urbackupcommon/os_functions_lin.cpp)
set_target_properties(urbackup_sqlgen PROPERTIES OUTPUT_NAME SQLGen)
target_include_directories(urbackup_sqlgen PRIVATE ${URBACKUP_SQLGEN_CONFIG_DIR} ${CMAKE_SOURCE_DIR} ${UTF8CPP_INCLUDE_DIR})
target_compile_definitions(urbackup_sqlgen PRIVATE $<$<COMPILE_LANGUAGE:CXX>:LINUX>)
target_link_libraries(urbackup_sqlgen PRIVATE ${URBACKUP_SYSTEM_LIBS})
if(URBACKUP_EMBEDDED_SQLITE3)
	target_sources(urbackup_sqlgen PRIVATE sqlite/sqlite3.c)
	target_compile_definitions(urbackup_sqlgen PRIVATE $<$<COMPILE_LANGUAGE:C>:SQLITE_ENABLE_UNLOCK_NOTIFY>)
else()
	target_compile_definitions(urbackup_sqlgen PRIVATE USE_SYSTEM_SQLITE)
	target_link_libraries(urbackup_sqlgen PRIVATE SQLite::SQLite3)
endif()
if(URBACKUP_CRYPTOPP)
	urbackup_link_cryptopp(urbackup_sqlgen)
else()
	target_compile_definitions(urbackup_sqlgen PRIVATE DO_NOT_USE_CRYPTOPP_SHA DO_NOT_USE_CRYPTOPP_MD5)
endif()

# urbackup_sqlgen_run(<target> <cpp file> <database> [<attached db name> <attached db file> ...])
function(urbackup_sqlgen_run target cppfile db)
	add_custom_target(${target}
		COMMAND ${CMAKE_COMMAND} -DSQLGEN=$<TARGET_FILE:urbackup_sqlgen> -DCPPFILE=${CMAKE_SOURCE_DIR}/${cppfile}
			-DDB=${db} "-DATTACH=${ARGN}" -P ${CMAKE_SOURCE_DIR}/cmake/run_sqlgen.cmake
		DEPENDS urbackup_sqlgen
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
		COMMENT "SQLGen ${cppfile}"
		VERBATIM)
endfunction()

set(URBACKUP_SQLGEN_SERVER_TARGETS "")
if(URBACKUP_BUILD_SERVER)
	set(srvdb ${URBACKUP_SQLGEN_SERVER_DB_DIR})
	urbackup_sqlgen_run(sqlgen_server_backup urbackupserver/dao/ServerBackupDao.cpp
		${srvdb}/backup_server.db settings_db ${srvdb}/backup_server_settings.db)
	urbackup_sqlgen_run(sqlgen_server_cleanup urbackupserver/dao/ServerCleanupDao.cpp
		${srvdb}/backup_server.db settings_db ${srvdb}/backup_server_settings.db)
	urbackup_sqlgen_run(sqlgen_server_files urbackupserver/dao/ServerFilesDao.cpp
		${srvdb}/backup_server_files.db)
	urbackup_sqlgen_run(sqlgen_server_links urbackupserver/dao/ServerLinkDao.cpp
		${srvdb}/backup_server_links.db)
	urbackup_sqlgen_run(sqlgen_server_link_journal urbackupserver/dao/ServerLinkJournalDao.cpp
		${srvdb}/backup_server_link_journal.db)
	set(URBACKUP_SQLGEN_SERVER_TARGETS sqlgen_server_backup sqlgen_server_cleanup sqlgen_server_files
		sqlgen_server_links sqlgen_server_link_journal)
	add_custom_target(sqlgen_server DEPENDS ${URBACKUP_SQLGEN_SERVER_TARGETS})
endif()

set(URBACKUP_SQLGEN_CLIENT_TARGETS "")
if(URBACKUP_BUILD_CLIENT)
	urbackup_sqlgen_run(sqlgen_client_dao urbackupclient/clientdao.cpp ${URBACKUP_SQLGEN_CLIENT_DB})
	urbackup_sqlgen_run(sqlgen_client_journal urbackupclient/watchdir/JournalDAO.cpp ${URBACKUP_SQLGEN_CLIENT_DB})
	set(URBACKUP_SQLGEN_CLIENT_TARGETS sqlgen_client_dao sqlgen_client_journal)
	add_custom_target(sqlgen_client DEPENDS ${URBACKUP_SQLGEN_CLIENT_TARGETS})
endif()

add_custom_target(sqlgen DEPENDS ${URBACKUP_SQLGEN_SERVER_TARGETS} ${URBACKUP_SQLGEN_CLIENT_TARGETS})

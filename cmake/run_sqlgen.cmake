# Runs SQLGen on one DAO source file (see sqlgen.cmake). Invoked via cmake -P with
# SQLGEN, CPPFILE, DB and ATTACH (list of attached db name/file pairs) set.

# SQLite would silently create empty databases, so check they exist first
set(dbs ${DB})
set(attach_args "")
list(LENGTH ATTACH attach_len)
if(attach_len GREATER 0)
	math(EXPR attach_last "${attach_len} - 1")
	foreach(i RANGE 1 ${attach_last} 2)
		list(GET ATTACH ${i} attach_db)
		list(APPEND dbs ${attach_db})
	endforeach()
	set(attach_args ${ATTACH})
endif()
foreach(db IN LISTS dbs)
	if(NOT EXISTS ${db})
		message(FATAL_ERROR "SQLGen: Database ${db} not found. Set URBACKUP_SQLGEN_SERVER_DB_DIR/URBACKUP_SQLGEN_CLIENT_DB.")
	endif()
endforeach()

execute_process(COMMAND ${SQLGEN} ${DB} ${CPPFILE} ${attach_args}
	OUTPUT_VARIABLE out ERROR_VARIABLE out RESULT_VARIABLE res)
message("${out}")
if(NOT res EQUAL 0 OR NOT out MATCHES "SQLGen: Ok.")
	message(FATAL_ERROR "SQLGen failed for ${CPPFILE}")
endif()

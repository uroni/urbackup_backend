#!/bin/bash

set -e
set -x

compress_lua()
{
	cat $1.lua | python3 -c "import zlib,sys; sys.stdout.buffer.write(zlib.compress(sys.stdin.buffer.read()))" > ${1}_lua.z
	xxd -i ${1}_lua.z > ${1}_lua.h
	rm ${1}_lua.z
}

compress_lua alert
compress_lua report
compress_lua alert_pulseway


#!/bin/bash

set -e
set -x

compress_f()
{
	cat "$1" | python3 -c "import zlib,sys; sys.stdout.buffer.write(zlib.compress(sys.stdin.buffer.read()))" > "${1}.z"
	xxd -i "${1}.z" > "${2}"
	rm "${1}.z"
}

compress_f btrfs2.vhdx cache_init_vhdx.h


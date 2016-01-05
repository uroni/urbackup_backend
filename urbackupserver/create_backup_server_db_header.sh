#!/bin/bash

set -e
set -x


cat backup_server.db | python3 -c "import zlib,sys; print(zlib.compress(sys.stdin.buffer.read()))" > backup_server_db.z
xxd -i backup_server_db.z > backup_server_db.h
#!/bin/bash

set -e
set -x


cat backup_client.db | python3 -c "import zlib,sys; sys.stdout.buffer.write(zlib.compress(sys.stdin.buffer.read()))" > backup_client_db.z
xxd -i backup_client_db.z > backup_client_db.h
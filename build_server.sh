#!/bin/sh

set -e

git reset --hard
python3 build/replace_versions.py

if ! test -e build_server_ok
then
	./switch_build.sh server
	./download_cryptopp.sh
	autoreconf || true
	automake --add-missing || true
	autoreconf --install
	./configure --enable-embedded-cryptopp
	touch build_server_ok
fi

./switch_build.sh server

make
make dist
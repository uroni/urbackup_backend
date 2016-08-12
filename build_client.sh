#!/bin/sh

set -e

git reset --hard
python3 build/replace_versions.py

if ! test -e build_client_ok
then
	./switch_build.sh client
	wget https://www.cryptopp.com/cryptopp563.zip -O cryptoplugin/cryptopp563.zip
	cd cryptoplugin
	unzip cryptopp563.zip
	cd ..
	autoreconf || true
	automake --add-missing || true
	autoreconf --install
	./configure --enable-embedded-cryptopp
	touch build_client_ok
fi
./switch_build.sh client
make
make dist
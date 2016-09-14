#!/bin/sh

set -e

git reset --hard
python3 build/replace_versions.py

if ! test -e build_server_ok
then
	./switch_build.sh server
	wget https://www.cryptopp.com/cryptopp564.zip -O cryptoplugin/cryptopp564.zip
	cd cryptoplugin
	SHASUM=`sha256sum cryptopp564.zip | cut -d" " -f1`
	EXPECTED="be430377b05c15971d5ccb6e44b4d95470f561024ed6d701fe3da3a188c84ad7"
	if [ $SHASUM != $EXPECTED ]
	then
		echo "SHASUM of cryptopp564.zip is wrong: got $SHASUM expected $EXPECTED"
		exit 1
	fi
	unzip cryptopp564.zip
	cd ..
	autoreconf || true
	automake --add-missing || true
	autoreconf --install
	./configure --enable-embedded-cryptopp
	touch build_server_ok
fi

./switch_build.sh server

make
make dist
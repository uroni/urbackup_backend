#!/bin/sh

set -e

git reset --hard
python3 build/replace_versions.py

if [ "x$VCPKG_ROOT" = "x" ]
then
	echo "Please set VCPKG_ROOT"
	exit 1
fi

cmake --preset client
cmake --build --preset client
cmake --build --preset client --target package_source
cp out/build/client/urbackup-client-*.tar.gz .

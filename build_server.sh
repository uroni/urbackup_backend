#!/bin/sh

set -e

if ! test -e build_server_ok
then
	./switch_build.sh server
	autoreconf || true
	automake --add-missing || true
	libtoolize || true
	autoreconf
	./configure --with-pychart
	touch build_server_ok
fi

make
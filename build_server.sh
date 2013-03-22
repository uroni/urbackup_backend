#!/bin/sh

set -e

if ! test -e build_server_ok
then
	./switch_build.sh server
	autoreconf
	automake --add-missing
	libtoolize
	autoreconf
	./configure --with-pychart
	touch build_server_ok
fi

make
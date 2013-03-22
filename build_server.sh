#!/bin/sh

if ! test -e configure
then
	./switch_build.sh server
	autoreconf
	automake --add-missing
	autoreconf
	./configure
fi

make
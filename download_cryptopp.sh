#!/bin/sh

set -e

CRYPTOPP_NAME="cryptopp565.zip"
EXPECTED_SHA256="c67c64693f32195e69d3d7e5bdf47afbd91e8b69d0407a2bc68a745d9dbebb26"

cd cryptoplugin
wget http://buildserver.urbackup.org/$CRYPTOPP_NAME -O cryptoplugin/$CRYPTOPP_NAME
SHASUM=`sha256sum $CRYPTOPP_NAME | cut -d" " -f1`
if [ $SHASUM != $EXPECTED_SHA256 ]
then
	echo "SHASUM of $CRYPTOPP_NAME is wrong: got $SHASUM expected $EXPECTED_SHA256"
	exit 1
fi
unzip $CRYPTOPP_NAME
cd ..

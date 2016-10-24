#!/bin/sh

set -e

CRYPTOPP_NAME="cryptopp565.zip"
EXPECTED_SHA256="a75ef486fe3128008bbb201efee3dcdcffbe791120952910883b26337ec32c34"

cd cryptoplugin
wget http://buildserver.urbackup.org/$CRYPTOPP_NAME -O $CRYPTOPP_NAME
SHASUM=`sha256sum $CRYPTOPP_NAME | cut -d" " -f1`
if [ $SHASUM != $EXPECTED_SHA256 ]
then
	echo "SHASUM of $CRYPTOPP_NAME is wrong: got $SHASUM expected $EXPECTED_SHA256"
	exit 1
fi
unzip $CRYPTOPP_NAME
cd ..

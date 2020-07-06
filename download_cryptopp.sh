#!/bin/sh

set -e

CRYPTOPP_NAME="cryptopp700.zip"
EXPECTED_SHA256="a4bc939910edd3d29fb819a6fc0dfdc293f686fa62326f61c56d72d0a366ceb0"

DOWNLOAD_CRYPTOPP=1
cd cryptoplugin/src
if [ -e $CRYPTOPP_NAME ]
then
	SHASUM=`shasum -a 256 $CRYPTOPP_NAME | cut -d" " -f1`
	if [ $SHASUM = $EXPECTED_SHA256 ]
	then
		DOWNLOAD_CRYPTOPP=0
	fi
fi

if [ $DOWNLOAD_CRYPTOPP = 1 ]
then
	wget http://buildserver.urbackup.org/$CRYPTOPP_NAME -O $CRYPTOPP_NAME
	SHASUM=`shasum -a 256 $CRYPTOPP_NAME | cut -d" " -f1`
	if [ $SHASUM != $EXPECTED_SHA256 ]
	then
		echo "SHASUM of $CRYPTOPP_NAME is wrong: got $SHASUM expected $EXPECTED_SHA256"
		exit 1
	fi
	unzip -o $CRYPTOPP_NAME
	rm GNUmakefile
fi

cd ..

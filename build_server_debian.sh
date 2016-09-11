#!/bin/sh

set -e

git reset --hard
python3.3 build/replace_versions.py
if [ "x$STATIC_CRYPTOPP" != "x" ]
then
	sed -i 's/\$(CRYPTOPP_LIBS)/\/usr\/local\/lib\/libcryptopp.a/g' Makefile.am_server
fi

wget http://buildserver.urbackup.org/urbackup-debian.tar.gz -O urbackup-debian.tar.gz
tar xzf urbackup-debian.tar.gz

LTO_FLAG="-flto"
if [ "x$NO_LTO" != "x" ]
then
	LTO_FLAG=""
fi

if ! test -e build_server_debian_ok
then
	./switch_build.sh server
	wget http://buildserver.urbackup.org/cryptopp563.zip -O cryptoplugin/cryptopp563.zip
	cd cryptoplugin
	unzip cryptopp563.zip
	cd ..
	autoreconf --install
	./configure --enable-packaging --enable-install_initd --with-mountvhd LDFLAGS="$LDFLAGS $LTO_FLAG" CPPFLAGS="$LTO_FLAG"
	touch build_server_debian_ok
fi

./switch_build.sh server
make dist > tmp
BASENAME=`awk -F'"' '{ for(i=1; i<NF; i++) if($i ~/urbackup-server-(.*)/) {print $i; exit;} }' tmp`
ANAME="${BASENAME}.tar.gz"
VERSION=`echo $BASENAME | cut -d "-" -f 3`
DEBVERSION="${VERSION}-1"

sed -i "0,/urbackup-server \(.*\)(.*)/s//urbackup-server \($VERSION\)\1/" debian/changelog

if [ "x$EMBEDDED_CRYPTOPP" != "x" ]
then
	sed -i 's/--enable-packaging/--enable-packaging --enable-embedded-cryptopp/g' debian/rules
fi

if [ "x$LDFLAGS" != "x" ]
then
	sed -i "s/LDFLAGS=\"/LDFLAGS=\"${LDFLAGS} /" debian/rules
fi

if [ "x$EXTRA_FLAGS" != "x" ]
then
	sed -i "s/-flto/-flto $EXTRA_FLAGS/g" debian/rules
fi

if [ "x$NO_LTO" != "x" ]
then
	sed -i "s/-flto//g" debian/rules
fi

dh clean
fakeroot dh binary
mkdir output || true
mv ../*.deb output/

#!/bin/sh

set -e

git reset --hard
python build\replace_versions.py

wget http://buildserver.urbackup.org/urbackup-debian.tar.gz -O urbackup-debian.tar.gz
tar xzf urbackup-debian.tar.gz

if ! test -e build_server_debian_ok
then
	./switch_build.sh server
	autoreconf || true
	automake --add-missing || true
	libtoolize || true
	autoreconf --install
	./configure --with-pychart --enable-packaging --enable-install_initd --with-mountvhd
	touch build_server_debian_ok
fi

./switch_build.sh server
make dist > tmp
BASENAME=`awk -F'"' '{ for(i=1; i<NF; i++) if($i ~/urbackup-server-(.*)/) {print $i; exit;} }' tmp`
ANAME="${BASENAME}.tar.gz"
VERSION=`echo $BASENAME | cut -d "-" -f 3`
DEBVERSION="${VERSION}-1"

sed -i "0,/urbackup-server \(.*\)(.*)/s//urbackup-server \($VERSION\)\1/" debian/changelog

dh clean
fakeroot dh binary
mkdir output || true
mv ../*.deb output/

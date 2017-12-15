#!/bin/bash

set -e

git reset --hard
cd client
git reset --hard
cd ..
python3 build/replace_versions.py

./switch_build.sh client
./download_cryptopp.sh

autoreconf --install

rm -R install-data || true
mkdir -p install-data

rm -R install-data-dbg || true
mkdir -p install-data-dbg

mkdir -p install-data/backup_scripts
cp urbackupclient/backup_scripts/* install-data/backup_scripts/
cp client/version.txt install-data/
cp client/data/urbackup_ecdsa409k1.pub install-data/
cp client/data/updates_h.dat install-data/
cp urbackupclientbackend-debian.service install-data/
cp urbackupclientbackend-redhat.service install-data/
cp init.d_client install-data/
cp init.d_client_rh install-data/
cp defaults_client install-data/
cp linux_snapshot/* install-data/
cp uninstall_urbackupclient install-data/
chmod +x install-data/uninstall_urbackupclient
chmod +x install-data/*_filesystem_snapshot

cp -R install-data/* install-data-dbg/

for arch in x86_64-linux-glibc i386-linux-eng x86_64-linux-eng armv6-linux-engeabihf aarch64-linux-eng
do
	echo "Compiling for architecture $arch..."
	
	if [ $arch = x86_64-linux-glibc ]
	then
		./configure --enable-headless --enable-clientupdate --enable-embedded-cryptopp CFLAGS="-ggdb -Os" CPPFLAGS="-DURB_WITH_CLIENTUPDATE -ffunction-sections -fdata-sections -flto -DCRYPTOPP_DISABLE_SSSE3" LDFLAGS="-Wl,--gc-sections -static-libstdc++ -flto" CXX="g++" CC="gcc" CXXFLAGS="-ggdb -Os"
		STRIP_CMD="strip"
	else
		./configure --enable-headless --enable-clientupdate CFLAGS="-target $arch -ggdb -Os" CPPFLAGS="-target $arch -DURB_THREAD_STACKSIZE64=8388608 -DURB_THREAD_STACKSIZE32=1048576 -DURB_WITH_CLIENTUPDATE -ffunction-sections -fdata-sections" LDFLAGS="-target $arch -Wl,--gc-sections" CXX="ecc++" CC="ecc" CXXFLAGS="-ggdb -Os" --with-crypto-prefix=/usr/local/ellcc/libecc --with-zlib=/usr/local/ellcc/libecc
		STRIP_CMD="ecc-strip"
	fi
	
    make clean
    make -j4
    rm -R install-data/$arch || true
    mkdir -p install-data/$arch
	rm -R install-data-dbg/$arch || true
    mkdir -p install-data-dbg/$arch
    cp urbackupclientbackend install-data/$arch/
	cp urbackupclientbackend install-data-dbg/$arch/
	$STRIP_CMD install-data/$arch/urbackupclientbackend
	cp urbackupclientctl install-data/$arch/
	cp urbackupclientctl install-data-dbg/$arch/
	$STRIP_CMD install-data/$arch/urbackupclientctl
	
	if [ $arch = x86_64-linux-glibc ]
	then
		./switch_build.sh client
	fi
done

rm -R linux-installer || true
mkdir -p linux-installer

cd install-data
tar czf ../linux-installer/install-data.tar.gz *
cd ..

cp install_client_linux.sh linux-installer/

rm -R linux-installer-dbg || true
mkdir -p linux-installer-dbg

cd install-data-dbg
tar czf ../linux-installer-dbg/install-data.tar.gz *
cd ..

cp install_client_linux.sh linux-installer-dbg/

makeself --nocomp --nomd5 --nocrc linux-installer "UrBackupUpdateLinux.sh" "UrBackup Client Installer for Linux" sh ./install_client_linux.sh
makeself --nocomp --nomd5 --nocrc linux-installer-dbg "UrBackupUpdateLinux-dbg.sh" "UrBackup Client Installer for Linux (debug)" sh ./install_client_linux.sh
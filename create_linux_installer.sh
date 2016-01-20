#!/bin/bash

set -e

git reset --hard
cd client
git reset --hard
cd ..
python3 build/replace_versions.py

./switch_build.sh client

autoreconf --install

mkdir -p install-data

for arch in i386-linux-eng x86_64-linux-eng armv6-linux-engeabihf armv6-linux-engeabi
do
	echo "Compiling for architecture $arch..."
    ./configure --enable-headless CFLAGS="-target $arch" CPPFLAGS="-target $arch" LDFLAGS="-target $arch" CXX="ecc++" CC="ecc" --with-crypto-prefix=/usr/local/ellcc/libecc
    make clean
    make -j4
    rm -R install-data/$arch || true
    mkdir -p install-data/$arch
    cp urbackupclientbackend install-data/$arch/
	cp urbackupclientctl install-data/$arch/        
done

mkdir -p install-data/backup_scripts
cp urbackupclient/backup_scripts/* install-data/backup_scripts/
cp client/version.txt install-data/
cp client/data/urbackup_ecdsa409k1.pub install-data/
cp urbackupclientbackend-debian.service install-data/
cp urbackupclientbackend-redhat.service install-data/
cp init.d_client install-data/
cp init.d_client_rh install-data/
cp defaults_client install-data/

rm -R linux-installer || true
mkdir -p linux-installer

cd install-data
tar czf ../linux-installer/install-data.tar.gz *
cd ..

cp install_client_linux.sh linux-installer/

makeself --nocomp --nomd5 --nocrc linux-installer "UrBackupUpdateLinux.sh" "UrBackup Client Installer for Linux" ./install_client_linux.sh
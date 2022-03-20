#!/bin/sh

set -e
set -x

./download_cryptopp.sh
./switch_build.sh client

autoreconf --install

./configure CXXFLAGS="-DRESTORE_CLIENT" --enable-headless --enable-embedded-cryptopp --enable-httpserver

make -j4

LANG=en

cd urbackupclient/restorewww
npm ci
npm run build
cd ..
cd ..

mkdir -p restore_cd/urbackup/restore
cp urbackupclient/backup_client.db restore_cd/urbackup/
touch restore_cd/urbackup/new.txt
mkdir -p restore_cd/restorewww/
cp -R urbackupclient/restorewww/build/* restore_cd/restorewww/

cp urbackupclientbackend restore_cd/urbackuprestoreclient
cp urbackupserver/restore/$LANG/* restore_cd/urbackup/restore/
cp urbackupserver/restore/* restore_cd/urbackup/restore/ || true
chmod +x restore_cd/urbackup/restore/*.sh
strip restore_cd/urbackuprestoreclient

cd restore_cd
tar -cJf ../restore_cd_2_amd64.tar.xz *
if [ "x$SCP_RESTORE" = x1 ]
then
	scp ../restore_cd_2_amd64.tar.xz 192.168.0.40:/var/www/ssl/
else
	cp ../restore_cd_2_amd64.tar.xz /var/www/restore_cd_2_amd64.tar.xz
fi

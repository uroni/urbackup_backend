#!/bin/sh

set -e
set -x

if [ "x$VCPKG_ROOT" = "x" ]
then
	echo "Please set VCPKG_ROOT"
	exit 1
fi

cmake --preset client-headless -B out/build/restore -DCMAKE_CXX_FLAGS="-DRESTORE_CLIENT" -DURBACKUP_CLIENT_RESTORE_HTTP=ON
cmake --build out/build/restore -j4

cd urbackupclient/restorewww
npm ci
NODE_OPTIONS=--openssl-legacy-provider npm run build
cd ../..

LANG=en

mkdir -p restore_cd/urbackup/restore
cp urbackupclient/backup_client.db restore_cd/urbackup/
touch restore_cd/urbackup/new.txt
mkdir -p restore_cd/restorewww/
cp -R urbackupclient/restorewww/build/* restore_cd/restorewww/

cp out/build/restore/urbackupclientbackend restore_cd/urbackuprestoreclient
cp urbackupserver/restore/$LANG/* restore_cd/urbackup/restore/
cp urbackupserver/restore/* restore_cd/urbackup/restore/ || true
chmod +x restore_cd/urbackup/restore/*.sh
#strip restore_cd/urbackuprestoreclient

cd restore_cd
tar -cJf ../restore_cd_2.tar.xz *
cp ../restore_cd_2.tar.xz /var/www/restore_cd_2.tar.xz

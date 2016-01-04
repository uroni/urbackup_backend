#!/bin/sh

./switch_build.sh client

autoreconf --install

set -e

./configure CXXFLAGS="-D_GLIBCXX_USE_CXX11_ABI=0 -DRESTORE_CLIENT" --enable-static

make -j4

LANG=en

mkdir restore_cd/urbackup
mkdir restore_cd/urbackup/restore
cp urbackupclient/backup_client.db restore_cd/urbackup/
touch restore_cd/urbackup/new.txt

cp urbackupclientbackend restore_cd/urbackuprestoreclient
cp urbackupserver/restore/$LANG/* restore_cd/urbackup/restore/
cp urbackupserver/restore/* restore_cd/urbackup/restore/
chmod +x restore_cd/urbackup/restore/*.sh
strip restore_cd/urbackuprestoreclient

cd restore_cd
tar -czf ../restore_cd_2.tgz *
cp ../restore_cd.tgz /var/www/restore_cd_2.tgz

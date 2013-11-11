#!/bin/sh

./switch_build.sh client

make
cd fileservplugin
make
cd ..
cd urbackupclient
make
cd ..

./switch_build.sh server
make

LANG=en

mkdir restore_cd/urbackup
mkdir restore_cd/urbackup/restore
cp urbackupclient/backup_client.db restore_cd/urbackup/
touch restore_cd/urbackup/new.txt

cp urbackup_srv restore_cd/urbackup_client
cp urbackupclient/.libs/liburbackupclient.so restore_cd/liburbackupclient.so
cp fsimageplugin/.libs/liburbackupclient_fsimageplugin.so restore_cd/libfsimageplugin.so
cp fileservplugin/.libs/liburbackupclient_fileservplugin.so restore_cd/libfileservplugin.so
cp urbackupserver/restore/$LANG/* restore_cd/urbackup/restore/
cp urbackupserver/restore/* restore_cd/urbackup/restore/
chmod +x restore_cd/urbackup/restore/*.sh
strip restore_cd/urbackup_client
strip restore_cd/liburbackupclient.so
strip restore_cd/libfsimageplugin.so
strip restore_cd/libfileservplugin.so

cd restore_cd
tar -czf ../restore_cd.tgz *
cp ../restore_cd.tgz /var/www/restore_cd.tgz

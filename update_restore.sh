#!/bin/sh

./switch_build.sh server

if test -e Makefile.am~b
then
    cp Makefile.am~b Makefile.am
fi

if test -e configure.ac~b
then
    cp configure.ac~b configure.ac
fi

make
cd fileservplugin
make
cd ..

LANG=de

cp urbackup_srv restore_cd/cserver
cp urbackup/.libs/liburbackupserver.so restore_cd/urbackup/.libs/liburbackup.so
cp fsimageplugin/.libs/liburbackupserver_fsimageplugin.so restore_cd/libfsimageplugin.so
cp fileservplugin/.libs/libfileservplugin.so restore_cd/libfileservplugin.so
cp urbackup/restore/$LANG/* restore_cd/urbackup/restore/
cp urbackup/restore/* restore_cd/urbackup/restore/
chmod +x restore_cd/urbackup/restore/*.sh
strip cserver
strip restore_cd/urbackup/.libs/liburbackup.so
strip restore_cd/libfsimageplugin.so
strip restore_cd/libfileservplugin.so

cd restore_cd
tar -czf ../restore_cd.tgz *
cp ../restore_cd.tgz /var/www/restore_cd.tgz

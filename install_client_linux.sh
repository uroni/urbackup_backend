#!/bin/sh

set -e

PREFIX=/usr/local

SILENT=no

if [ "x$1" = "xsilent" ]
then
	SILENT=yes
fi

USER=`whoami`

if [ "x$USER" != "xroot" ]
then
	echo "Sorry, you must be super user to install UrBackup Client. Try again with sudo?"
	exit 1
fi

if [ $SILENT = no ]
then
	echo "Installation of UrBackup Client $version_short$... Proceed ? [Y/n]"
	read yn
	if [ "x$yn" = xn ]
	then
		exit 1
	fi
else
	echo "Installation of UrBackup Client $version_short$..."
fi

echo "Uncompressing install data..."
tar xzf install-data.tar.gz

DEBIAN=no

if [ -e /etc/debian_version ]
then
	DEBIAN=yes
	echo "Detected Debian (derivative) system"
	mv urbackupclientbackend-debian.service urbackupclientbackend.service
	mv init.d_client init.d
else
	echo "Assuming RedHat (derivative) system"
	mv urbackupclientbackend-redhat.service urbackupclientbackend.service
	mv init.d_client_rh init.d
fi

SYSTEMD=no

if command -v systemctl >/dev/null 2>&1
then
	echo "Detected systemd"
	SYSTEMD=yes
	
	if systemctl status urbackupclientbackend.service >/dev/null 2>&1
	then
		echo "Stopping currently running client service..."
		systemctl stop urbackupclientbackend.service
	fi
else
	if [ -e /etc/init.d/urbackupclientbackend ]
	then
		if /etc/init.d/urbackupclientbackend status >/dev/null 2>&1
		then
			echo "Stopping currently running client service..."
			/etc/init.d/urbackupclientbackend stop
		fi
	fi
fi

TARGET=no

arch=`uname -m`
case "$arch" in
    i?86) TARGET=i386-linux-eng ;;
    x86_64) TARGET=x86_64-linux-eng ;;
    armv6*) TARGET=arm-linux-engeabihf ;;
	armv7*) TARGET=arm-linux-engeabihf ;;
	armv8*) TARGET=aarch64-linux-eng ;;
esac

if [ $TARGET = no ]
then
	echo "Cannot run UrBackup client on this server. CPU architecture $arch not supported. Stopping installation."
	exit 1
else
	echo "Detected architecture $TARGET"
fi

install -c -m 744 -d "$PREFIX/var/urbackup"
install -c -m 744 -d "$PREFIX/sbin"
install -c -m 744 -d "$PREFIX/bin"
install -c -m 744 -d "$PREFIX/share/urbackup/scripts"
install -c -m 744 -d "$PREFIX/etc/urbackup"

install -c "$TARGET/urbackupclientbackend" "$PREFIX/sbin"
install -c "$TARGET/urbackupclientctl" "$PREFIX/bin"

sed "s|SYSCONFDIR|$PREFIX/etc/urbackup|g" "backup_scripts/list" > "backup_scripts/list.r"
mv "backup_scripts/list.r" "backup_scripts/list"
install -c "backup_scripts/list" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/mariadb" "$PREFIX/share/urbackup/scripts"

test -e "$PREFIX/etc/urbackup/mariadb.conf" || install -c "backup_scripts/mariadb.conf" "$PREFIX/etc/urbackup"

chmod 700 "$PREFIX/share/urbackup/scripts"*
chmod 700 "$PREFIX/etc/urbackup/"*

/usr/bin/install -c -m 644 "version.txt" "$PREFIX/var/urbackup"

cat << c71b9d17069d4d03bd7f6b75f36ba5ff > "$PREFIX/var/urbackup/initial_settings.cfg"
#Initial Settings. Changes will not be respected.
#48692563-17e4-4ccb-a078-f14372fdbe20
################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################
#6e7f6ba0-8478-4946-b70a-f1c7e83d28cc
c71b9d17069d4d03bd7f6b75f36ba5ff

if test ! -e "$PREFIX/var/urbackup/server_idents.txt"
then
	cat << c71b9d17069d4d03bd7f6b75f36ba5ff > "$PREFIX/var/urbackup/server_idents.txt"
#17460620-769b-4add-85aa-a764efe84ab7
###################################################
#569d42d2-1b40-4745-a426-e86a577c7f1a
c71b9d17069d4d03bd7f6b75f36ba5ff
fi

if [ $DEBIAN = yes ]
then
	[ ! -e /etc/defaults ] || install -c defaults_client /etc/defaults/urbackupclientbackend
else
	[ ! -e /etc/sysconfig ] || install -c defaults_client /etc/sysconfig/urbackupclientbackend
fi

if [ $SYSTEMD = yes ]
then
	echo "Installing systemd unit..."
	SYSTEMD_DIR=`pkg-config systemd --variable=systemdsystemunitdir`
	
	if [ "x$SYSTEMD_DIR" = x ]
	then
		echo "Cannot find systemd unit dir. Assuming /lib/systemd/system"
		SYSTEMD_DIR="/lib/systemd/system"
	fi
	
	install -c urbackupclientbackend.service $SYSTEMD_DIR
	systemctl enable urbackupclientbackend.service
	
	echo "Starting UrBackup Client service..."
	systemctl start urbackupclientbackend.service
	
	if systemctl status urbackupclientbackend.service >/dev/null 2>&1
	then
		echo "Successfully started client service. Installation complete."
		exit 0
	else
		echo "Starting client service failed. Please investigate."
		exit 1
	fi
else
	echo "Installing System V init script..."
	
	install -c init.d /etc/init.d/urbackupclientbackend
	
	if [ $DEBIAN = yes ]
	then
		update-rc.d urbackupclientbackend defaults
	else
		chkconfig --add urbackupclientbackend
		chkconfig --level 345 urbackupclientbackend on
	fi
	
	echo "Starting UrBackup Client service..."
	/etc/init.d/urbackupclientbackend start
	
	if /etc/init.d/urbackupclientbackend status >/dev/null 2>&1
	then
		echo "Successfully started client service. Installation complete."
		exit 0
	else
		echo "Starting client service failed. Please investigate."
		exit 1
	fi
fi

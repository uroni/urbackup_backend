#!/bin/sh

set -e

restore_image () {
	if ! [ -t 1 ]
	then
		echo "Error: Stdout is not a TTY. Not running non-interactively"
		exit 5
	fi

	USER=`whoami`

	if [ "x$USER" != "xroot" ]
	then
		echo "Sorry, you must be super user to restore an image. Try again with sudo?"
		exit 6
	fi

	if ! command -v systemctl >/dev/null 2>&1
	then
		echo "Sorry, image restore only works with systemd present currently."
		exit 7
	fi

	echo "Uncompressing data..."
	tar xzf install-data.tar.gz

	sh restore_linux_img.sh "$SERVER_NAME" "$SERVER_PORT" "$SERVER_PROXY" "$RESTORE_AUTHKEY" "$RESTORE_TOKEN" "$PREFIX"
	exit $?
}

cat << EOF > /dev/null
#ab6b754c02624b348795c92da78b1e73$version$
EOF

#e1128bd07afd40a0a2752818730589ef
RESTORE_IMAGE=0
################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################################
#902761a5525a4007add0b016a35af985

#Cannot be changed as paths are compiled into the binaries
PREFIX=/usr/local

if [ $RESTORE_IMAGE = 1 ]
then
	restore_image
	exit $?
fi

SILENT=no

if [ "x$1" = "xsilent" ]
then
	SILENT=yes
fi

if ! [ -t 1 ]
then
	echo "Stdout is not a TTY. Running non-interactively"
	SILENT=yes
fi

USER=`whoami`

if [ "x$USER" != "xroot" ]
then
	echo "Sorry, you must be super user to install UrBackup Client. Try again with sudo?"
	exit 6
fi

if [ $SILENT = no ]
then
	echo "Installation of UrBackup Client $version_short$ to $PREFIX ... Proceed ? [Y/n]"
	read yn
	if [ "x$yn" = xn ]
	then
		exit 5
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
	echo "Detected Debian \(derivative\) system"
	mv urbackupclientbackend-debian.service urbackupclientbackend.service
	mv init.d_client init.d
else
	echo "Assuming RedHat \(derivative\) system"
	if [ -e /etc/sysconfig ]
	then
		mv urbackupclientbackend-redhat.service urbackupclientbackend.service
		mv init.d_client_rh init.d
	else
		echo "/etc/sysconfig does not exist. Putting daemon configuration into /etc/default"
		mv urbackupclientbackend-debian.service urbackupclientbackend.service
		mv init.d_client init.d
	fi	
fi

SYSTEMD=no
RESTART_SERVICE=no

if command -v systemctl >/dev/null 2>&1
then
	echo "Detected systemd"
	SYSTEMD=yes
	
	if systemctl status urbackupclientbackend.service >/dev/null 2>&1
	then
		RESTART_SERVICE=yes
	fi
else
	if [ -e /etc/init.d/urbackupclientbackend ]
	then
		if /etc/init.d/urbackupclientbackend status >/dev/null 2>&1
		then
			RESTART_SERVICE=yes
		fi
	fi
fi

TARGET=no

arch=`uname -m`
case "$arch" in
    i?86) TARGET=i686-linux-android ;;
    x86_64) TARGET=x86_64-linux-glibc ;;
    armv6*) TARGET=arm-linux-androideabi ;;
	armv7*) TARGET=arm-linux-androideabi ;;
	armv8*) TARGET=aarch64-linux-android ;;
	aarch64) TARGET=aarch64-linux-android ;;
esac

if [ $TARGET = no ]
then
	echo "Cannot run UrBackup client on this server. CPU architecture $arch not supported. Stopping installation."
	exit 3
else
	echo "Detected architecture $TARGET"
fi

test -e "$PREFIX/sbin" || install -c -m 755 -d "$PREFIX/sbin"
test -e "$PREFIX/bin" || install -c -m 755 -d "$PREFIX/bin"
install -c "$TARGET/urbackupclientbackend" "$PREFIX/sbin"
install -c "$TARGET/urbackupclientctl" "$PREFIX/bin"

ORIG_TARGET=$TARGET

if [ $TARGET = x86_64-linux-glibc ]
then
	if ! "$PREFIX/bin/urbackupclientctl" --version 2>&1 | grep "UrBackup Client Controller" > /dev/null 2>&1
	then
		echo "\(Glibc not installed or too old. Falling back to Android NDK build...\)"
		TARGET=x86_64-linux-android
	else
		if ! "$PREFIX/sbin/urbackupclientbackend" --version 2>&1 | grep "UrBackup Client Backend" > /dev/null 2>&1
		then
			echo "\(Glibc not installed or too old \(2\). Falling back to Android NDK build...\)"
			TARGET=x86_64-linux-android
		fi
	fi
fi

if [ $TARGET = arm-linux-androideabi ]
then
	if ! "$PREFIX/bin/urbackupclientctl" --version 2>&1 | grep "UrBackup Client Controller" > /dev/null 2>&1
	then
		echo "\(Android NDK build not working. Falling back to ELLCC build...\)"
		TARGET=armv6-linux-engeabihf
	else
		if ! "$PREFIX/sbin/urbackupclientbackend" --version 2>&1 | grep "UrBackup Client Backend" > /dev/null 2>&1
		then
			echo "\(Android NDK build not working. Falling back to ELLCC build...\)"
			TARGET=armv6-linux-engeabihf
		fi
	fi
fi

if [ $TARGET != $ORIG_TARGET ]
then
	install -c "$TARGET/urbackupclientbackend" "$PREFIX/sbin"
	install -c "$TARGET/urbackupclientctl" "$PREFIX/bin"
fi

if ! "$PREFIX/bin/urbackupclientctl" --version 2>&1 | grep "UrBackup Client Controller" > /dev/null 2>&1
then
	echo "Error running executable on this system \($arch\). Stopping installation."
	exit 2
fi

install -c "$TARGET/blockalign" "$PREFIX/bin"

test -e "$PREFIX/var/urbackup/data" || install -c -m 744 -d "$PREFIX/var/urbackup/data"
test -e "$PREFIX/share/urbackup/scripts" || install -c -m 744 -d "$PREFIX/share/urbackup/scripts"
test -e "$PREFIX/etc/urbackup" || install -c -m 744 -d "$PREFIX/etc/urbackup"

install -c "uninstall_urbackupclient" "$PREFIX/sbin"

for script in backup_scripts/*
do
	sed "s|SYSCONFDIR|$PREFIX/etc/urbackup|g" "$script" > "$script.r"
	mv "$script.r" "$script"
done

install -c "backup_scripts/list" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/list_incr" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/mariadbdump" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/postgresqldump" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/postgresbase" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/postgresqlprebackup" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/postgresqlpostbackup" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/setup-postgresbackup" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/mariadbxtrabackup" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/mariadbxtrabackup_incr" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/restore-mariadbbackup" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/mariadbprebackup" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/mariadbpostbackup" "$PREFIX/share/urbackup/scripts"
install -c "backup_scripts/setup-mariadbbackup" "$PREFIX/share/urbackup/scripts"

install -c "btrfs_create_filesystem_snapshot" "$PREFIX/share/urbackup"
install -c "btrfs_remove_filesystem_snapshot" "$PREFIX/share/urbackup"
install -c "lvm_create_filesystem_snapshot" "$PREFIX/share/urbackup"
install -c "lvm_remove_filesystem_snapshot" "$PREFIX/share/urbackup"
install -c "dattobd_create_snapshot" "$PREFIX/share/urbackup"
install -c "dattobd_remove_snapshot" "$PREFIX/share/urbackup"
install -c "dm_create_snapshot" "$PREFIX/share/urbackup"
install -c "dm_remove_snapshot" "$PREFIX/share/urbackup"
install -c "filesystem_snapshot_common" "$PREFIX/share/urbackup"

test -e "$PREFIX/etc/urbackup/mariadbdump.conf" || install -c "backup_scripts/mariadbdump.conf" "$PREFIX/etc/urbackup"
test -e "$PREFIX/etc/urbackup/postgresqldump.conf" || install -c "backup_scripts/postgresqldump.conf" "$PREFIX/etc/urbackup"
test -e "$PREFIX/etc/urbackup/postgresbase.conf" || install -c "backup_scripts/postgresbase.conf" "$PREFIX/etc/urbackup"
test -e "$PREFIX/etc/urbackup/mariadbxtrabackup.conf" || install -c "backup_scripts/mariadbxtrabackup.conf" "$PREFIX/etc/urbackup"

chmod 700 "$PREFIX/share/urbackup/scripts"*
chmod 700 "$PREFIX/etc/urbackup/"*

/usr/bin/install -c -m 644 "version.txt" "$PREFIX/var/urbackup"
/usr/bin/install -c -m 644 "urbackup_ecdsa409k1.pub" "$PREFIX/share/urbackup"
/usr/bin/install -c -m 644 "updates_h.dat" "$PREFIX/var/urbackup"

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

CONFIG_FILE=no
if [ $DEBIAN = yes ]
then
	if [ -e /etc/default ] && [ ! -e /etc/default/urbackupclient ]
	then
		CONFIG_FILE=/etc/default/urbackupclient
		install -c defaults_client /etc/default/urbackupclient
	fi
else
	if [ -e /etc/sysconfig ]
	then
		if [ ! -e /etc/sysconfig/urbackupclient ]
		then
			CONFIG_FILE=/etc/sysconfig/urbackupclient
			install -c defaults_client /etc/sysconfig/urbackupclient
		fi
	elif [ -e /etc/default ]
	then
		if [ ! -e /etc/default/urbackupclient ]
		then
			CONFIG_FILE=/etc/default/urbackupclient
			install -c defaults_client /etc/default/urbackupclient
		fi	
	fi
fi

if [ $CONFIG_FILE != no ]
then
	if grep "internet_mode_enabled=true" "$PREFIX/var/urbackup/initial_settings.cfg" > /dev/null 2>&1
	then
		echo "Enabling internet only mode"
		sed 's/INTERNET_ONLY=false/INTERNET_ONLY=true/g' "$CONFIG_FILE" > "$CONFIG_FILE.new"
		mv "$CONFIG_FILE.new" "$CONFIG_FILE"
	fi
	echo "Installed daemon configuration at $CONFIG_FILE..."
	echo "Info: Restoring from web interface is disabled per default. Enable by modifying $CONFIG_FILE."
fi

if [ $SYSTEMD = yes ]
then
	echo "Installing systemd unit..."
	SYSTEMD_DIR=""
	if command -v pkg-config >/dev/null 2>&1
	then
		SYSTEMD_DIR=`pkg-config systemd --variable=systemdsystemunitdir`
	fi
	
	if [ "x$SYSTEMD_DIR" = x ]
	then
		if test -e "/lib/systemd/system/urbackupclientbackend.service"
		then
			echo "Updating existing systemd unit at /lib/systemd/system/urbackupclientbackend.service"
			SYSTEMD_DIR="/lib/systemd/system"
		elif test -e "/usr/lib/systemd/system"
		then
			echo "Cannot find systemd unit dir. Assuming /usr/lib/systemd/system"
			SYSTEMD_DIR="/usr/lib/systemd/system"
		else
			echo "Cannot find systemd unit dir. Assuming /lib/systemd/system"
			SYSTEMD_DIR="/lib/systemd/system"
		fi
	fi
	
	install -c urbackupclientbackend.service $SYSTEMD_DIR
	systemctl enable urbackupclientbackend.service
	
	SYSTEMD_DBUS=yes
	
	if [ $RESTART_SERVICE = no ]
	then
		echo "Starting UrBackup Client service..."
		if ! systemctl start urbackupclientbackend.service
		then
			if ! systemctl > /dev/null
			then
				SYSTEMD_DBUS=no
			fi
		fi
	else
		echo "Restarting UrBackup Client service..."
		#This will kill the installer during auto-update.
		#So do not do anything (important) after that.
		if ! systemctl restart urbackupclientbackend.service
		then
			if ! systemctl > /dev/null
			then
				SYSTEMD_DBUS=no
				killall urbackupclientbackend || true
			fi
		fi
	fi	
	
	if [ $SYSTEMD_DBUS = yes ]
	then
		if systemctl status urbackupclientbackend.service >/dev/null 2>&1
		then
			echo "Successfully started client service. Installation complete."
		else
			echo "Starting client service failed. Please investigate."
			exit 1
		fi
	else
		echo "Systemd failed \(see previous messages\). Starting urbackupclientbackend manually this time..."
		urbackupclientbackend -d -c $CONFIG_FILE
	fi
	
else
	echo "Installing System V init script..."
	
	INSTALL_SYSV=yes
	if [ $DEBIAN = yes ] && ! [ -e /lib/lsb/init-functions ]
	then
		echo "/lib/lsb/init-functions not found. lsb-base \(>= 3.0-6\) not installed? Not installing System V init script."
		INSTALL_SYSV=no
	elif [ $DEBIAN = no ] && ! [ -e /etc/rc.d/init.d/functions ]
	then
		echo "/etc/rc.d/init.d/functions not found. Not installing System V init script."
		INSTALL_SYSV=no
	fi		
	
	if [ $INSTALL_SYSV = yes ]
	then	
		install -c init.d /etc/init.d/urbackupclientbackend
		
		if [ $DEBIAN = yes ]
		then
			update-rc.d urbackupclientbackend defaults
		else
			chkconfig --add urbackupclientbackend
			chkconfig --level 345 urbackupclientbackend on
		fi
		
		if [ $RESTART_SERVICE = no ]
		then
			echo "Starting UrBackup Client service..."
			/etc/init.d/urbackupclientbackend start
		else
			echo "Restarting UrBackup Client service..."
			/etc/init.d/urbackupclientbackend restart
		fi
		
		
		if /etc/init.d/urbackupclientbackend status >/dev/null 2>&1
		then
			echo "Successfully started client service. Installation complete."
		else
			echo "Starting client service failed \(see previous messages\). Starting urbackupclientbackend manually this time..."
			urbackupclientbackend -d -c $CONFIG_FILE
		fi
	else
		echo "Starting urbackupclientbackend manually this time..."
		urbackupclientbackend -d -c $CONFIG_FILE
	fi
fi

if [ $SILENT = no ]
then
	if [ -e $PREFIX/etc/urbackup/snapshot.cfg ] || [ -e $PREFIX/etc/urbackup/no_filesystem_snapshot ]
	then
		exit 0
	fi

    CENTOS=no
	UBUNTU=no
	FEDORA=no

    DATTO=no
    LVM=no
    BTRFS=no
	DMSETUP=no

    if [ $DEBIAN = no ]
    then
        if grep "release 6" /etc/redhat-release > /dev/null 2>&1
        then
            CENTOS=6
        fi

        if grep "release 7" /etc/redhat-release > /dev/null 2>&1
        then
            CENTOS=7
        fi
		
		if grep "release 8" /etc/redhat-release > /dev/null 2>&1
        then
            CENTOS=8
        fi
		
		if grep "Fedora" /etc/fedora-release > /dev/null 2>&1
		then
			FEDORA=yes
		fi
    else
		if grep 'NAME="Ubuntu"' /etc/os-release > /dev/null 2>&1
		then
			if grep 'VERSION="' /etc/os-release | grep "LTS" > /dev/null 2>&1
			then
				echo "+Detected Ubuntu LTS. Dattobd supported"
				UBUNTU=yes
				DATTO=yes
			fi
		elif grep 'NAME="Debian' /etc/os-release > /dev/null 2>&1
		then
			if grep 'PRETTY_NAME="' /etc/os-release | grep "/sid" > /dev/null 2>&1
			then
				echo "+Detected Debian unstable/sid. Dattobd not supported"
			else
				echo "+Detected Debian stable. Dattobd supported"
				DATTO=yes
			fi
		fi
	fi


    if [ $CENTOS != no ]
    then
        echo "+Detected EL/RH $CENTOS. Dattobd supported"
        DATTO=yes
    fi
	
	if [ $FEDORA != no ]
	then
		echo "+Detected Fedora. Dattobd supported"
        DATTO=yes
    fi

    if [ $DATTO = no ]
    then
        echo "-dattobd not supported on this system"
    fi

    if df -T -P | tr -s " " | cut -d" " -f2 | grep "btrfs" > /dev/null 2>&1
    then
        echo "+Detected btrfs filesystem"
        BTRFS=yes
    else
		echo "-Detected no btrfs filesystem"
	fi

	if command -v lvs >/dev/null 2>&1
	then
		LVM_VOLS=`lvs 2> /dev/null | wc -l`
		if [ "x$LVM_VOLS" != x ] && [ $LVM_VOLS -gt 1 ]
		then
			echo "+Detected LVM volumes"
			LVM=yes
		else
			echo "-Detected no LVM volumes"
		fi
	else
		echo "-LVM not installed"
	fi
	
	if command -v dmsetup >/dev/null 2>&1
	then
		DMSETUP=yes
		echo "+dmsetup present"
	else
		echo "-dmsetup not present"
	fi
	

    while true
    do
        echo "Please select the snapshot mechanism to be used for backups:"
        if [ $DATTO != no ]
        then
            echo "1) dattobd volume snapshot kernel module from https://github.com/datto/dattobd (supports image backups and changed block tracking)"
        fi

        if [ $LVM != no ]
        then
            echo "2) LVM - Logical Volume Manager snapshots"
        fi

        if [ $BTRFS != no ]
        then
            echo "3) btrfs filesystem snapshots (dattobd and LVM will automatically use btrfs snapshots for btrfs filesystems)"
        fi
		
		if [ $DMSETUP != no ]
        then
            echo "4) Linux device mapper based snapshots (supports image backups and changed block tracking)"
        fi

        echo "5) Use no snapshot mechanism"

        read snapn

        if [ "x$snapn" = x1 ]
        then
            break
        fi

        if [ "x$snapn" = x2 ]
        then
            break
        fi

        if [ "x$snapn" = x3 ]
        then
            break
        fi

        if [ "x$snapn" = x4 ]
        then
            break
        fi
		
		if [ "x$snapn" = x5 ]
        then
            break
        fi
    done

    mkdir -p $PREFIX/etc/urbackup

	CREATE_SNAPSHOT_SCRIPT=""
	REMOVE_SNAPSHOT_SCRIPT=""
	CREATE_VOLUME_SNAPSHOT=""
	REMOVE_VOLUME_SNAPSHOT=""
    if [ $snapn = 3 ]
    then
		CREATE_SNAPSHOT_SCRIPT="$PREFIX/share/urbackup/btrfs_create_filesystem_snapshot"
		REMOVE_SNAPSHOT_SCRIPT="$PREFIX/share/urbackup/btrfs_remove_filesystem_snapshot"
    fi

    if [ $snapn = 2 ]
    then
		CREATE_SNAPSHOT_SCRIPT="$PREFIX/share/urbackup/lvm_create_filesystem_snapshot"
		REMOVE_SNAPSHOT_SCRIPT="$PREFIX/share/urbackup/lvm_remove_filesystem_snapshot"
    fi

    if [ $snapn = 1 ]
    then
        # if [ $CENTOS != no ]
        # then
            # yum localinstall https://cpkg.datto.com/datto-rpm/EnterpriseLinux/$(rpm -E %rhel)/x86_64/datto-el-rpm-release-$(rpm -E %rhel)-8.1.noarch.rpm
            # yum install dkms-dattobd dattobd-utils
		# elif [ $FEDORA != no ]
		# then
			# yum install https://cpkg.datto.com/datto-rpm/Fedora/$(rpm -E %fedora)/x86_64/datto-fedora-rpm-release-$(rpm -E %fedora)-8.1.noarch.rpm
			# yum install kernel-devel-$(uname -r) dkms-dattobd dattobd-utils
        # elif [ $UBUNTU != no ]
		# then
			# apt-key adv --recv-keys --keyserver keyserver.ubuntu.com 29FF164C
			# CODENAME=`lsb_release -sc`
			# echo "deb https://cpkg.datto.com/repositories $CODENAME main" > /etc/apt/sources.list.d/datto-linux-agent.list
			# apt-get update
			# apt-get install dattobd-dkms dattobd-utils
		# fi
		
		echo "Configured dattobd. Please install dattobd following the instructions at https://github.com/datto/dattobd"

		CREATE_SNAPSHOT_SCRIPT="$PREFIX/share/urbackup/dattobd_create_snapshot"
		REMOVE_SNAPSHOT_SCRIPT="$PREFIX/share/urbackup/dattobd_remove_snapshot"
		CREATE_VOLUME_SNAPSHOT="$PREFIX/share/urbackup/dattobd_create_snapshot"
		REMOVE_VOLUME_SNAPSHOT="$PREFIX/share/urbackup/dattobd_remove_snapshot"
    fi
	
	if [ $snapn = 4 ]
	then
		CREATE_SNAPSHOT_SCRIPT="$PREFIX/share/urbackup/dm_create_snapshot"
		REMOVE_SNAPSHOT_SCRIPT="$PREFIX/share/urbackup/dm_remove_snapshot"
		CREATE_VOLUME_SNAPSHOT="$PREFIX/share/urbackup/dm_create_snapshot"
		REMOVE_VOLUME_SNAPSHOT="$PREFIX/share/urbackup/dm_remove_snapshot"
		
		if [ $DEBIAN = yes ] || [ $UBUNTU = yes ]
		then
			echo "## Install thin-provisioning tools for changed block detection and partclone for used space optimization ##"
			apt-get install thin-provisioning-tools partclone || true
		fi
		
		echo "Convert root device into device mapper device on boot (initramfs)? This is required for root device/filesystem backup. [Y/n]"
		read yn
		if [ "x$yn" != xn ]
		then
			if [ -e /usr/share/initramfs-tools/hooks ]
			then
				install -c "hooks_urbackup-setup-snapshot" /usr/share/initramfs-tools/hooks/urbackup-setup-snapshot
				mkdir -p /usr/share/initramfs-tools/scripts/local-top
				install -c "scripts_local-top_urbackup-setup-snapshot" /usr/share/initramfs-tools/scripts/local-top/urbackup-setup-snapshot
				update-initramfs -u
				echo "Info: You need to reboot your system in order to be able to snapshot the root file system/device"
			else
				echo "Did not find initramfs-tools. Installation failed."
			fi
		fi
	fi
	
	if [ $snapn = 5 ]
	then
		touch $PREFIX/etc/urbackup/no_filesystem_snapshot
		echo "Configured no snapshot mechanism"
	fi
	
	if [ "x$CREATE_SNAPSHOT_SCRIPT" != "x" ]
	then
		echo "#This is a key=value config file for determining the scripts/programs to create snapshots" > $PREFIX/etc/urbackup/snapshot.cfg
		echo "" >> $PREFIX/etc/urbackup/snapshot.cfg
		echo "create_filesystem_snapshot=$CREATE_SNAPSHOT_SCRIPT" >> $PREFIX/etc/urbackup/snapshot.cfg
		echo "remove_filesystem_snapshot=$REMOVE_SNAPSHOT_SCRIPT" >> $PREFIX/etc/urbackup/snapshot.cfg
		if [ "x$CREATE_VOLUME_SNAPSHOT" != "x" ]
		then
			echo "create_volume_snapshot=$CREATE_VOLUME_SNAPSHOT" >> $PREFIX/etc/urbackup/snapshot.cfg
			echo "remove_volume_snapshot=$REMOVE_VOLUME_SNAPSHOT" >> $PREFIX/etc/urbackup/snapshot.cfg
		fi
		echo "Configured snapshot mechanism via $PREFIX/etc/urbackup/snapshot.cfg"
	fi
fi

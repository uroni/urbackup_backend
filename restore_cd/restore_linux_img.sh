#!/bin/sh

set -e -o pipefail

SERVER_NAME="$1"
SERVER_PORT="$2"
SERVER_PROXY="$3"
RESTORE_AUTHKEY="$4"
RESTORE_TOKEN="$5"
PREFIX="$6"

restore_image_cleanup()
{
	if [ $START_URBACKUPCLIENTBACKEND = 1 ]
	then
		echo "Starting urbackupclientbackend service..."
		systemctl start urbackupclientbackend
	fi

	if [ $MOVE_URBACKUP_VAR = 1 ]
	then
		echo "Moving /usr/local/var/urbackup_orig to /usr/local/var/urbackup ..."
		mv /usr/local/var/urbackup_orig /usr/local/var/urbackup
	else
		echo "Removing /usr/local/var/urbackup ..."
		rm -Rf /usr/local/var/urbackup
	fi

	if [ "x$SYSTEMD_DIR" != "x" ]
	then
		systemctl stop urbackuprestoreclient.service > /dev/null 2>&1 || true
		! [ -e "$SYSTEMD_DIR/urbackuprestoreclient.service" ] || rm "$SYSTEMD_DIR/urbackuprestoreclient.service"
	fi

    if [ "x$DMSETUP_REMOVE" != x ]
    then
        dmsetup remove $DMSETUP_REMOVE
    fi
}

DMSETUP_REMOVE=""
SYSTEMD_DIR=""
START_URBACKUPCLIENTBACKEND=0
MOVE_URBACKUP_VAR=0

trap restore_image_cleanup EXIT

if systemctl status urbackupclientbackend > /dev/null 2>&1
then
    START_URBACKUPCLIENTBACKEND=1
    echo "Stopping urbackupclientbackend service..."
    systemctl stop urbackupclientbackend
fi

if [ -e /usr/local/var/urbackup ]
then
    MOVE_URBACKUP_VAR=1
    echo "Moving /usr/local/var/urbackup to /usr/local/var/urbackup_orig ..."
    mv /usr/local/var/urbackup /usr/local/var/urbackup_orig
fi

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
    echo "Cannot run UrBackup restore client on this server. CPU architecture $arch not supported. Stopping restore."
    exit 3
else
    echo "Detected architecture $TARGET"
fi

test -e "$PREFIX/sbin" || install -c -m 755 -d "$PREFIX/sbin"
install -c "$TARGET/urbackupclientbackend" "$PREFIX/sbin/urbackuprestoreclient"
install -c "$TARGET/urbackupclientctl" "$PREFIX/sbin/urbackuprestoreclientctl"
ORIG_TARGET=$TARGET

if [ $TARGET = x86_64-linux-glibc ]
then
    if ! "$PREFIX/sbin/urbackuprestoreclient" --version 2>&1 | grep "UrBackup Client Backend" > /dev/null 2>&1
    then
        echo "(Glibc not installed or too old (2). Falling back to Android NDK build...)"
        TARGET=x86_64-linux-android
    fi
fi

if [ $TARGET = arm-linux-androideabi ]
then
    if ! "$PREFIX/sbin/urbackuprestoreclient" --version 2>&1 | grep "UrBackup Client Backend" > /dev/null 2>&1
    then
        echo "(Android NDK build not working. Falling back to ELLCC build...)"
        TARGET=armv6-linux-engeabihf
    fi
fi

if [ $TARGET != $ORIG_TARGET ]
then
    install -c "$TARGET/urbackupclientbackend" "$PREFIX/sbin/urbackuprestoreclient"
    install -c "$TARGET/urbackupclientctl" "$PREFIX/sbin/urbackuprestoreclientctl"
fi

if command -v pkg-config >/dev/null 2>&1
then
    SYSTEMD_DIR=`pkg-config systemd --variable=systemdsystemunitdir`
fi

if [ "x$SYSTEMD_DIR" = x ]
then
    echo "Cannot find systemd unit dir. Assuming /lib/systemd/system"
    SYSTEMD_DIR="/lib/systemd/system"
fi

cat << EOF > $SYSTEMD_DIR/urbackuprestoreclient.service
[Unit]
Description=UrBackup Restore Client
After=network.target

[Service]
ExecStart=$PREFIX/sbin/urbackuprestoreclient --restore-client --logfile /var/log/urbackuprestore.txt --loglevel debug -t
WorkingDirectory=$PREFIX
User=root
TasksMax=infinity

[Install]
WantedBy=multi-user.target
EOF

mkdir -p /usr/local/var/urbackup/data
cat << EOF > /usr/local/var/urbackup/data/settings.cfg
internet_mode_enabled=true
internet_server=$SERVER_NAME
internet_server_port=$SERVER_PORT
internet_server_proxy=$SERVER_PROXY
internet_authkey=$RESTORE_AUTHKEY
EOF

echo "Starting urbackuprestoreclient service..."
systemctl start urbackuprestoreclient.service

WTIME=0

echo "Waiting for client to connect..."
SERVER_CONNECTED=0
while [ $WTIME -lt 60 ]
do
    if $PREFIX/sbin/urbackuprestoreclientctl status | grep '"internet_connected": true' > /dev/null && ! $PREFIX/sbin/urbackuprestoreclientctl status | grep '"servers": []'
    then
        echo "Successfully connected to Internet server. Logging in..."
        SERVER_CONNECTED=1
        break
    fi
    WTIME=$(expr $WTIME + 1)
done

if [ $SERVER_CONNECTED = 0 ]
then
    echo "Could not connect to Internet server. See log:"
    journalctl -u urbackuprestoreclient
    exit 10
fi

DISKS=$(lsblk -p -P | grep 'TYPE="disk"')

echo "Please select the disk to restore to:"
NUM=1
SELDISK_PATH=""
while true
do
    echo "0) Exit without restoring"
    echo "$DISKS" | while read disk
    do
        DEVNAME=$(echo "$disk" | sed 's@NAME="\([^"]*\)".*@\1@g')
        DEVSIZE=$(echo "$disk" | sed 's@SIZE="\([^"]*\)".*@\1@g')
        echo "$NUM) Restore to $DEVNAME size $DEVSIZE"
        NUM=$(expr $NUM + 1)
    done

    read seldisk

    if [ "x$seldisk" = "x0" ]
    then   
        exit 11
    fi

    echo "$DISKS" | while read disk
    do
        DEVNAME=$(echo "$disk" | sed 's@NAME="\([^"]*\)".*@\1@g')

        if [ "x$seldisk" = "x$NUM" ]
        then
            SELDISK_PATH="$DEVNAME"
            break
        fi

        NUM=$(expr $NUM + 1)
    done

    if [ "x$SELDISK_PATH" != "x" ]
    then
        break
    fi
done

echo "Restoring to $SELDISK_PATH ..."

echo "Loading MBR..."

MBRFILE=$(mktemp)

$PREFIX/sbin/urbackuprestoreclientbackend --mbr-download --download-token "$RESTORE_TOKEN" --out-device "$MBRFILE"

echo "MBR info:"

$PREFIX/sbin/urbackuprestoreclientbackend --mbr-info "$MBRFILE"

PARTNUMBER=$($PREFIX/sbin/urbackuprestoreclientbackend --mbr-info "$MBRFILE" | grep "partition_number=" | sed 's@partition_number=\(.*\)@\1@g')

if [ "x$PARTNUMBER" != "x0"]
then
    echo "Restoring partition $PARTNUMBER at $SELDISK_PATH$PARTNUMBER..."
    RESTORE_TARGET="$SELDISK_PATH$PARTNUMBER"
else
    echo "Restoring whole disk $SELDISK_PATH..."
    RESTORE_TARGET="$SELDISK_PATH"
fi

MOUNTPOINT=$(lsblk -p -P $RESTORE_TARGET | grep "NAME=\"$RESTORE_TARGET\"" | sed 's@MOUNTPOINT="\([^"]*\)".*@\1@g')

if [ "x$MOUNTPOINT" != x ]
then
    if [ $MOUNTPOINT = "/" ]
    then
        AVAIL_MEM=$(cat /proc/meminfo | grep "MemAvailable:" | tr -s ' ' | cut -d ' ' -f 2)
        AVAIL_MEM=$(expr $AVAIL_MEM - 200000)

        if [ $AVAIL_MEM -lt 1500000]
        then
            echo "Not enough available memory to replace root file system ($AVAIL_MEM kB)"
            exit 12
        fi

        echo "$RESTORE_TARGET is mounted as root file system. Will restore via live restore hack. This is DANGEROUS. If the restore is interrupted the machine will not boot or the system files are corrupted."
    else
        echo "$RESTORE_TARGET is mounted at $MOUNTPOINT. Will unmount/remount read-only before restoring."
    fi
    echo "Proceed? y/N"

    read yn
    if [ "x$yn" != xy ] && [ "x$yn" != xY ]
    then
        exit 5
    fi
else    
    echo "Proceed? Y/n"

    read yn
    if [ "x$yn" = xn ]
    then
        exit 5
    fi
fi

PARTITION_SIZE=""
if [ "x$PARTNUMBER" != "x0"]
then
    echo "Overwrite partition layout (MBR/GPT) of $SELDISK_PATH? y/N"

    read yn
    if [ "x$yn" = xY ]
    then
        $PREFIX/sbin/urbackuprestoreclientbackend --restore-mbr "$MBRFILE" --out-device "$SELDISK_PATH"

        echo "Partitions:"
        $PREFIX/sbin/urbackuprestoreclientbackend --mbr-read "$SELDISK_PATH"

        PARTITION_PARAMS=$($PREFIX/sbin/urbackuprestoreclientbackend --mbr-read "$SELDISK_PATH" | grep "partition $PARTNUMBER ")

        PARTITION_OFF=$(echo "$PARTITION_PARAMS" | cut -d ' ' -f4)
        PARTITION_SIZE=$(echo "$PARTITION_PARAMS" | cut -d ' ' -f6)
        PARTITION_OFF=$(expr \( $PARTITION_OFF + 511 \) / 512 \))
        PARTITION_SIZE=$(expr \( $PARTITION_SIZE + 511 \) / 512 \))

        echo "partition $PARTNUMBER offset $PARTITION_OFF length $PARTITION_SIZE"

        echo "$PARTITION_OFF $PARTITION_SIZE linear $RESTORE_TARGET 0" | dmsetup create restore-part-e8174e484b77496894efeccef5a8502f
        DMSETUP_REMOVE="restore-part-e8174e484b77496894efeccef5a8502f"
        RESTORE_TARGET="/dev/mapper/$DMSETUP_REMOVE"
    fi
fi

if [ "x$PARTITION_SIZE" = x ]
then
    if ! command -v blockdev > /dev/null 2>&1
    then
	    echo "blockdev command missing. Please install."
	    exit 1
    fi
    PARTITION_SIZE=$(blockdev --getsz "$RESTORE_TARGET")
fi

if [ "$MOUNTPOINT" = "/" ]
then
    cp restore_linux_root.sh $PREFIX/sbin/restore_linux_root.sh
    cat << EOF > $SYSTEMD_DIR/urbackuprestoreroot.service
[Unit]
Description=UrBackup Restore Image
After=network.target

[Service]
ExecStart=/bin/sh $PREFIX/sbin/restore_linux_root.sh "$RESTORE_TARGET" "$PARTITION_SIZE" "$PREFIX" "$RESTORE_TOKEN"
WorkingDirectory=$PREFIX/var
User=root
TasksMax=infinity

[Install]
WantedBy=multi-user.target
EOF
    echo "Replacing root file system..."
    systemctl start urbackuprestoreroot.service
    journalctl -u urbackuprestoreroot.service -f
    exit 0
elif [ "x$MOUNTPOINT" != x ]
then
    if ! umount "$MOUNTPOINT"
    then
        lsof +f -- "$MOUNTPOINT" | tr -s ' ' | cut -d ' ' -f2 | while read pid
        do
            if [ "$PID" != "PID" ]
            then
                kill -9 $PID > /dev/null 2>&1 || true
            fi
        done

        if ! umount "$MOUNTPOINT"
        then
            mount -o remount,ro "$MOUNTPOINT"
            sync
            if command -v fsfreeze >/dev/null 2>&1
            then
                fsfreeze --freeze "$MOUNTPOINT"
            fi
        fi
    fi
fi

cat << EOF > $SYSTEMD_DIR/urbackuprestoreimage.service
[Unit]
Description=UrBackup Restore Image
After=network.target

[Service]
ExecStart=/bin/sh -c "$PREFIX/sbin/urbackuprestoreclientbackend --image-download --download-token "$RESTORE_TOKEN" --out-device "$RESTORE_TARGET"; rm $SYSTEMD_DIR/urbackuprestoreimage.service"
WorkingDirectory=$PREFIX/var
User=root
TasksMax=infinity

[Install]
WantedBy=multi-user.target
EOF

systemctl start urbackuprestoreimage.service

echo "Please wait. Image restore percent complete: "
$PREFIX/sbin/urbackuprestoreclientbackend --image-download-progress


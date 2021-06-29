#!/bin/sh

set -e

SERVER_NAME="$1"
SERVER_PORT="$2"
SERVER_PROXY="$3"
RESTORE_AUTHKEY="$4"
RESTORE_TOKEN="$5"
PREFIX="$6"

restore_image_cleanup()
{
    if [ "x$SYSTEMD_DIR" != "x" ]
	then
		systemctl stop urbackuprestoreclient.service > /dev/null 2>&1 || true
		! [ -e "$SYSTEMD_DIR/urbackuprestoreclient.service" ] || rm "$SYSTEMD_DIR/urbackuprestoreclient.service"
	fi
    
    if [ $MOVE_URBACKUP_VAR = 1 ] && [ -e $PREFIX/var/urbackup_orig ]
	then
		echo "Moving $PREFIX/var/urbackup_orig to $PREFIX/var/urbackup ..."
		mv $PREFIX/var/urbackup_orig $PREFIX/var/urbackup
	else
		echo "Removing $PREFIX/var/urbackup ..."
		rm -Rf $PREFIX/var/urbackup
	fi

	if [ $START_URBACKUPCLIENTBACKEND = 1 ]
	then
		echo "Starting urbackupclientbackend service..."
		systemctl start urbackupclientbackend
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

trap restore_image_cleanup EXIT INT TERM

if systemctl status urbackupclientbackend > /dev/null 2>&1
then
    START_URBACKUPCLIENTBACKEND=1
    echo "Stopping urbackupclientbackend service..."
    systemctl stop urbackupclientbackend
fi

if [ -e $PREFIX/var/urbackup ]
then
    MOVE_URBACKUP_VAR=1
    echo "Moving $PREFIX/var/urbackup to $PREFIX/var/urbackup_orig ..."
    mv $PREFIX/var/urbackup $PREFIX/var/urbackup_orig
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
ExecStart=$PREFIX/sbin/urbackuprestoreclient --restore-client --logfile /var/log/urbackuprestore.txt --loglevel debug -t --internet-only
WorkingDirectory=$PREFIX/var
User=root
TasksMax=infinity

[Install]
WantedBy=multi-user.target
EOF

mkdir -p $PREFIX/var/urbackup/data
cat << EOF > $PREFIX/var/urbackup/data/settings.cfg
internet_mode_enabled=true
internet_server=$SERVER_NAME
internet_server_port=$SERVER_PORT
internet_server_proxy=$SERVER_PROXY
internet_authkey=$RESTORE_AUTHKEY
EOF

echo "Starting urbackuprestoreclient service..."
systemctl daemon-reload
systemctl start urbackuprestoreclient.service

WTIME=0

echo "Waiting for client to connect..."
SERVER_CONNECTED=0
while [ $WTIME -lt 60 ]
do
    sleep 1
    if $PREFIX/sbin/urbackuprestoreclientctl status 2>&1 | grep '"internet_connected": true' > /dev/null && ! $PREFIX/sbin/urbackuprestoreclientctl status 2>&1 | grep '"servers": \[\]' > /dev/null
    then
        echo "Successfully connected to Internet server. Logging in..."
        SERVER_CONNECTED=1
        break
    fi
    WTIME=$((WTIME + 1))
done

if [ $SERVER_CONNECTED = 0 ]
then
    echo "Could not connect to Internet server. See log:"
    journalctl -u urbackuprestoreclient
    exit 10
fi

DISKS=$(lsblk -p -P | grep 'TYPE="disk"')

echo "Please select the disk to restore to:"
SELDISK_PATH=""
while true
do
    echo "0) Exit without restoring"
    NUM=1
    OLDIFS="$IFS"
    IFS='
'
    for disk in $DISKS
    do
        DEVNAME=$(echo "$disk" | sed 's@.*NAME="\([^"]*\)".*@\1@g')
        DEVSIZE=$(echo "$disk" | sed 's@.*SIZE="\([^"]*\)".*@\1@g')
        echo "$NUM) Restore to $DEVNAME size $DEVSIZE"
        NUM=$((NUM + 1))
    done
    IFS="$OLDIFS"

    read seldisk
    

    if [ "x$seldisk" = "x0" ]
    then   
        exit 11
    fi

    NUM=1
    OLDIFS="$IFS"
    IFS='
'
    for disk in $DISKS
    do
        DEVNAME=$(echo "$disk" | sed 's@NAME="\([^"]*\)".*@\1@g')

        if [ "x$seldisk" = "x$NUM" ]
        then
            SELDISK_PATH="$DEVNAME"
            break
        fi

        NUM=$((NUM + 1))
    done
    IFS="$OLDIFS"

    if [ "x$SELDISK_PATH" != "x" ]
    then
        break
    fi
done

echo "Restoring to $SELDISK_PATH ..."

CWD=$(pwd)
cd "$PREFIX/var"

echo "Loading MBR..."

MBRFILE=$(mktemp)

(cd "$PREFIX/var" && $PREFIX/sbin/urbackuprestoreclient --mbr-download --download-token "$RESTORE_TOKEN" --out-device "$MBRFILE")

echo "MBR info:"

(cd "$PREFIX/var" && $PREFIX/sbin/urbackuprestoreclient --mbr-info "$MBRFILE")

PARTNUMBER=$(cd "$PREFIX/var" && $PREFIX/sbin/urbackuprestoreclient --mbr-info "$MBRFILE" | grep "partition_number=" | sed 's@partition_number=\(.*\)@\1@g')

if [ "x$PARTNUMBER" != "x0" ]
then
    echo "Restoring partition $PARTNUMBER at $SELDISK_PATH$PARTNUMBER of disk $SELDISK_PATH..."
    RESTORE_TARGET="$SELDISK_PATH$PARTNUMBER"
else
    echo "Restoring whole disk $SELDISK_PATH..."
    RESTORE_TARGET="$SELDISK_PATH"
fi

MOUNTPOINT=$(lsblk -p -P "$RESTORE_TARGET" | grep "NAME=\"$RESTORE_TARGET\"" | sed 's@.*MOUNTPOINT="\([^"]*\)".*@\1@g')

if [ "x$MOUNTPOINT" != x ]
then
    if [ "$MOUNTPOINT" = "/" ]
    then
        AVAIL_MEM=$(grep "MemAvailable:" /proc/meminfo | tr -s ' ' | cut -d ' ' -f 2)
        AVAIL_MEM=$((AVAIL_MEM - 200000))

        if [ $AVAIL_MEM -lt 1500000 ]
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
if [ "x$PARTNUMBER" != "x0" ]
then
    echo "Overwrite partition layout (MBR/GPT) of $SELDISK_PATH? y/N"

    read yn
    if [ "x$yn" = xY ] || [ "x$yn" = xy ]
    then
        $PREFIX/sbin/urbackuprestoreclient --restore-mbr "$MBRFILE" --out-device "$SELDISK_PATH"
        sync

        echo "Partitions:"
        $PREFIX/sbin/urbackuprestoreclient --mbr-read "$SELDISK_PATH"

        PARTITION_PARAMS=$($PREFIX/sbin/urbackuprestoreclient --mbr-read "$SELDISK_PATH" | grep "partition $PARTNUMBER ")

        PARTITION_OFF=$(echo "$PARTITION_PARAMS" | cut -d ' ' -f4)
        PARTITION_SIZE=$(echo "$PARTITION_PARAMS" | cut -d ' ' -f6)
        PARTITION_OFF=$(( (PARTITION_OFF + 511) / 512 ))
        PARTITION_SIZE=$(( (PARTITION_SIZE + 511) / 512 ))

        echo "partition $PARTNUMBER offset $PARTITION_OFF length $PARTITION_SIZE"

        #idk why it needs a loop device in-between..., sometimes it doesn't work otherwise
        LODEV=$(losetup -f)
        losetup "$LODEV" "$SELDISK_PATH"
        echo "0 $PARTITION_SIZE linear $LODEV $PARTITION_OFF" | dmsetup create restore-part-e8174e484b77496894efeccef5a8502f
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
    cp "$CWD/restore_linux_root.sh" $PREFIX/sbin/restore_linux_root.sh
    cat << EOF > $SYSTEMD_DIR/urbackuprestoreroot.service
[Unit]
Description=UrBackup Restore Image
After=network.target

[Service]
ExecStart=/bin/sh $PREFIX/sbin/restore_linux_root.sh "$RESTORE_TARGET" "$PARTITION_SIZE" "$PREFIX" "$RESTORE_TOKEN" "$SYSTEMD_DIR"
WorkingDirectory=$PREFIX/var
User=root
TasksMax=infinity

[Install]
WantedBy=multi-user.target
EOF
    echo "Replacing root file system..."
    systemctl daemon-reload
    systemctl start urbackuprestoreroot.service
    journalctl -u urbackuprestoreroot.service -f
    exit 0
elif [ "x$MOUNTPOINT" != x ]
then
    if ! umount "$MOUNTPOINT"
    then
        lsof +f -- "$MOUNTPOINT" | tr -s ' ' | cut -d ' ' -f2 | while read -r PID
        do
            if [ "$PID" != "PID" ]
            then
                kill -9 "$PID" > /dev/null 2>&1 || true
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
ExecStart=/bin/sh -c "$PREFIX/sbin/urbackuprestoreclient --image-download --download-token "$RESTORE_TOKEN" --out-device "$RESTORE_TARGET"; rm $SYSTEMD_DIR/urbackuprestoreimage.service"
WorkingDirectory=$PREFIX/var
User=root
TasksMax=infinity

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl start urbackuprestoreimage.service

echo "Please wait. Image restore percent complete: "
sleep 1
while systemctl status urbackuprestoreimage > /dev/null 2>&1
do
    (cd "$PREFIX/var" && $PREFIX/sbin/urbackuprestoreclient --image-download-progress --image-download-progress-decorate)
    sleep 1
done
echo "Restore complete."


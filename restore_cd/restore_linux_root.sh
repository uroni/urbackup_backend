#!/bin/sh

set -e -o pipefail

RESTORE_TARGET="$1"
PARTITION_SIZE="$2"
PREFIX="$3"
RESTORE_TOKEN="$4"

echo "Setting up in-memory base system..."

AVAIL_MEM=$(cat /proc/meminfo | grep "MemAvailable:" | tr -s ' ' | cut -d ' ' -f 2)
AVAIL_MEM=$(expr $AVAIL_MEM - 200000)

if [ $AVAIL_MEM -lt 1500000 ]
then
    echo "Only $AVAIL_MEM kB of memory available. Not enough memory for restore."
    exit 1
fi

modprobe loop > /dev/null 2>&1 || true
swapoff -a || true
mkdir -p /tmproot
AVAIL_MEM_B=$(expr $AVAIL_MEM * 1000)
PRE_SIZE_KB=$(expr $AVAIL_MEM - 1000000)
PRE_SIZE_SZ=$(expr \( $PRE_SIZE_KB * 1000 \) / 512)
if ! command -v zramctl > /dev/null 2>&1 || ! modprobe zram > /dev/null 2>&1 || ! command -v mkfs.ext4 > /dev/null 2>&1
then
	echo "Using tmpfs..."
	mount -t tmpfs none /tmproot -o size=$AVAIL_MEM_B
else
	echo "Using zram..."
	ZRAMDEV=$(zramctl -f)
	zramctl -b -s $AVAIL_MEM_B $ZRAMDEV
	echo "Formatting zram device..."
	mkfs.ext4 $ZRAMDEV > /dev/null
	mount $ZRAMDEV /tmproot
fi
mkdir /tmproot/{proc,sys,dev,run,usr,var,tmp,oldroot}
mkdir /tmproot/usr/share/
cp -ax /{bin,etc,mnt,sbin,lib,lib64} /tmproot/ > /dev/null 2>&1 || true
cp -ax /usr/{bin,sbin,lib,lib64} /tmproot/usr/ > /dev/null 2>&1 || true
cp -ax /usr/share/ca-certificates /tmproot/usr/share/  > /dev/null 2>&1 || true
cp -ax /var/{account,empty,lib,local,lock,nis,opt,preserve,run,spool,tmp,yp} /tmproot/var/ > /dev/null 2>&1 || true
echo 20971520 > /proc/sys/vm/dirty_bytes
echo 5242880 > /proc/sys/vm/dirty_background_bytes
truncate -s "${PRE_SIZE_KB}KB" /tmproot/pre
LODEV=$(losetup -f)
losetup $LODEV /tmproot/pre
echo "0 $PRE_SIZE_SZ linear $LODEV 0
$PRE_SIZE_SZ $PARTITION_SIZE linear $RESTORE_TARGET $PRE_SIZE_SZ" | dmsetup create live-restore-e046c59e0f0f49d1ad93d55642db5319
mount --make-rprivate /
pivot_root /tmproot /tmproot/oldroot
for i in dev proc sys run; do mount --move /oldroot/$i /$i; done
echo "Restarting services..."
systemctl daemon-reexec
systemctl restart rsyslog
systemctl restart ssh
kill -9 $(pidof dhclient) > /dev/null 2>&1 || true
systemctl stop unattended-upgrades > /dev/null 2>&1 || true
systemctl stop systemd-udevd > /dev/null 2>&1 || true
systemctl restart systemd-timesyncd > /dev/null 2>&1 || true
#systemctl restart dbus > /dev/null 2>&1 || true
systemctl restart atd > /dev/null 2>&1 || true
systemctl restart lvm2-lvmetad > /dev/null 2>&1 || true
systemctl restart lxcfs > /dev/null 2>&1 || true
systemctl restart accounts-daemon > /dev/null 2>&1 || true
systemctl restart networkd-dispatcher > /dev/null 2>&1 || true
systemctl restart polkit > /dev/null 2>&1 || true
systemctl restart serial-getty@ttyS0 > /dev/null 2>&1 || true
systemctl restart systemd-networkd > /dev/null 2>&1 || true
systemctl restart getty@tty1 > /dev/null 2>&1 || true
systemctl restart systemd-logind > /dev/null 2>&1 || true
systemctl restart cron > /dev/null 2>&1 || true
systemctl restart systemd-journald > /dev/null 2>&1 || true
systemctl restart ntp > /dev/null 2>&1 || true
mount -o remount,ro /oldroot
sync
! [ -e /oldroot/usr/sbin/sshd ] || cat /oldroot/usr/sbin/sshd > /dev/null
! [ -e /oldroot/lib/systemd/systemd-logind ] || cat /oldroot/lib/systemd/systemd-logind > /dev/null
! [ -e /oldroot/lib/systemd/systemd ] || cat /oldroot/lib/systemd/systemd > /dev/null
cat << "EOF" > /refresh_sshd.sh
set -e
while true; do
	for p in $(pidof sshd); do
                ! [ -e /oldroot$(readlink /proc/$p/exe) ] || cat /oldroot$(readlink /proc/$p/exe) > /dev/null
		for i in $(ls /proc/$p/map_files); do
			LOC=$(readlink /proc/$p/map_files/$i)
			! [ -e /oldroot$LOC ] || cat /oldroot$LOC > /dev/null
		done
        done
	touch /refresh_done
	sleep 1
done
EOF
bash /refresh_sshd.sh &
while ! [ -e /refresh_done ]; do
	sleep 1
done

LIVE_RESTORE_TARGET="/dev/mapper/live-restore-e046c59e0f0f49d1ad93d55642db5319"

cat << EOF > $SYSTEMD_DIR/urbackuprestoreimage.service
[Unit]
Description=UrBackup Restore Image
After=network.target

[Service]
ExecStart=$PREFIX/sbin/urbackuprestoreclientbackend --image-download --download-token "$RESTORE_TOKEN" --out-device "$LIVE_RESTORE_TARGET"
WorkingDirectory=$PREFIX/var
User=root
TasksMax=infinity

[Install]
WantedBy=multi-user.target
EOF

echo "Replacing root file system... (DO NOT INTERRUPT FROM NOW ON)"
systemctl start urbackuprestoreimage.service

echo "Please wait. Image restore percent complete (DO NOT INTERRUPT): "
$PREFIX/sbin/urbackuprestoreclientbackend --image-download-progress

echo "Replacing critical data... afterwards rebooting machine..."
cat /pre > /dev/null
dd if=/pre of=$RESTORE_TARGET conv=notrunc,fsync bs=32M 2> /dev/null
echo "Done replacing critical data. Rebooting machine."
sleep 1
if [ -e /proc/sysrq-trigger ]
then
	echo b > /proc/sysrq-trigger
else
	init 6
fi
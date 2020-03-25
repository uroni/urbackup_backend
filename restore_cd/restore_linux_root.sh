#!/bin/sh

set -e

RESTORE_TARGET="$1"
PARTITION_SIZE="$2"
PREFIX="$3"
RESTORE_TOKEN="$4"
SYSTEMD_DIR="$5"

echo "Setting up in-memory base system..."

AVAIL_MEM=$(grep "MemAvailable:" /proc/meminfo | tr -s ' ' | cut -d ' ' -f 2)
AVAIL_MEM=$((AVAIL_MEM - 200000))

if [ $AVAIL_MEM -lt 1500000 ]
then
    echo "Only $AVAIL_MEM kB of memory available. Not enough memory for restore."
    exit 1
fi

modprobe loop > /dev/null 2>&1 || true
swapoff -a || true
mkdir -p /tmproot
AVAIL_MEM_B=$((AVAIL_MEM * 1000))
PRE_SIZE_KB=$((AVAIL_MEM - 1000000))
PRE_SIZE_SZ=$(( (PRE_SIZE_KB * 1000 ) / 512 ))
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
for p in proc sys dev run usr var tmp oldroot
do
	mkdir "/tmproot/$p"
done
mkdir /tmproot/usr/share/
for p in bin etc mnt sbin lib lib64
do
	cp -ax "/$p" /tmproot/ > /dev/null 2>&1 || true
done
for p in bin sbin lib lib64
do
	cp -ax "/usr/$p" /tmproot/usr/ > /dev/null 2>&1 || true
done
cp -ax /usr/share/ca-certificates /tmproot/usr/share/  > /dev/null 2>&1 || true
for p in account empty lib local lock nis opt preserve run spool tmp yp
do
	cp -ax "/var/$p" /tmproot/var/ > /dev/null 2>&1 || true
done
mkdir /tmproot/usr/local
for p in sbin var
do
	cp -ax "/usr/local/$p" /tmproot/usr/local/ > /dev/null 2>&1 || true
done
echo 20971520 > /proc/sys/vm/dirty_bytes
echo 5242880 > /proc/sys/vm/dirty_background_bytes
truncate -s $((PRE_SIZE_SZ*512)) /tmproot/pre
LODEV=$(losetup -f)
losetup "$LODEV" /tmproot/pre
LODEV2=$(losetup -f)
losetup "$LODEV2" "$RESTORE_TARGET"
echo "0 $PRE_SIZE_SZ linear $LODEV 0
$PRE_SIZE_SZ $((PARTITION_SIZE-PRE_SIZE_SZ)) linear $LODEV2 $PRE_SIZE_SZ" | dmsetup create live-restore-e046c59e0f0f49d1ad93d55642db5319
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
systemctl restart urbackuprestoreclient > /dev/null 2>&1 || true

REMOUNT_TRIES=5
while true
do
	KPROCS="x"
	while [ "x$KPROCS" != x ]
	do
		KPROCS=$(find /proc -maxdepth 1 -type d -name '[0-9]*' -exec find {}/fd -type l -ilname '/oldroot/*' \; | sed -r 's@/proc/([0-9]*)/fd/([0-9]*)@/proc/\1/fdinfo/\2@' | xargs -I@ /bin/sh -c "if [ -e @ ] && grep -E '^flags:\s+[0-9]*[1-9]$' < @ > /dev/null 2>&1; then echo '@'; fi" | sed -r 's@/proc/([0-9]*)/fdinfo/[0-9]*@\1@' | uniq)
		if [ "x$KPROCS" != x ]
		then
			echo "Killing processes writing to root..."
			kill -9 $KPROCS > /dev/null 2>&1 || true
			sleep 1
		fi
	done
	if ! mount -o remount,ro /oldroot
	then
		if [ $REMOUNT_TRIES -lt 1 ]
		then
			echo "Remounting /oldroot read-only failed. There is probably some service still active with files opened for writing on /oldroot... PLEASE REBOOT to reset your system now."
			lsof | grep "/oldroot"
			exit 5
		else
			echo "Remounting /oldroot read-only failed. Retrying..."
			sleep 1
		fi
	else
		break
	fi
	REMOUNT_TRIES=$((REMOUNT_TRIES-1))
done

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
ExecStart=$PREFIX/sbin/urbackuprestoreclient --image-download --download-token "$RESTORE_TOKEN" --out-device "$LIVE_RESTORE_TARGET"
WorkingDirectory=$PREFIX/var
User=root
TasksMax=infinity

[Install]
WantedBy=multi-user.target
EOF

echo "Replacing root file system... (DO NOT INTERRUPT FROM NOW ON)"
systemctl start urbackuprestoreimage.service

echo "Please wait for restore to start... "

while [ "x$(stat --format="%b" /pre)" = x0 ]
do
	sleep 1
done

echo "Image restore percent complete (DO NOT INTERRUPT):"
sleep 1
while systemctl status urbackuprestoreimage > /dev/null 2>&1
do
	(cd "$PREFIX/var" && $PREFIX/sbin/urbackuprestoreclient --image-download-progress --image-download-progress-decorate)
	sleep 1
done

echo "Replacing critical data... afterwards rebooting machine..."
cat /pre > /dev/null
dd if=/pre of="$RESTORE_TARGET" conv=notrunc,fsync bs=32M 2> /dev/null
echo "Done replacing critical data. Rebooting machine."
sleep 1
if [ -e /proc/sysrq-trigger ]
then
	echo b > /proc/sysrq-trigger
else
	init 6
fi

#!/bin/sh

export LANG=en_EN.UTF-8
export LC_CTYPE=en_EN.UTF-8
export LANGUAGE=en_EN.UTF-8

#Tuning
echo 'net.core.wmem_max=12582912' >> /etc/sysctl.conf
echo 'net.core.rmem_max=12582912' >> /etc/sysctl.conf
echo 'net.ipv4.tcp_rmem= 10240 87380 12582912' >> /etc/sysctl.conf
echo 'net.ipv4.tcp_wmem= 10240 87380 12582912' >> /etc/sysctl.conf
sysctl -p

cd `dirname $0`
echo "Installing services..."
cp restore-client.service /lib/systemd/system/
cp restore-client-internet.service /lib/systemd/system/
systemctl enable restore-client.service
systemctl enable restore-client-internet.service
echo "Starting restore service..."
systemctl start restore-client
echo "Setting up keyboard layout..."
dpkg-reconfigure keyboard-configuration
service keyboard-setup restart
setupcon
echo "Starting restore wizard..."
./urbackuprestoreclient --restore-wizard --logfile restore_wizard.txt --loglevel debug
echo "Wizard stoped with error code: $?"
tail -n 100 restore_wizard.txt
read -p "Press any key to continue..."


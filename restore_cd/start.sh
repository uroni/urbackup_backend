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
cp restore-http.service /lib/systemd/system/
systemctl enable restore-client.service
systemctl enable restore-client-internet.service
systemctl enable restore-http
echo "Starting restore service..."
systemctl start restore-client
echo "Starting restore http service..."
systemctl start restore-http
while ! dialog --menu "Please select restore user interface" 15 50 10 "g" "Graphical/modern interface (recommended)" "t" "Text/console-based interface" 2> out
do
    echo "Please select user interface"
    read -p "Press any key to continue..."
done
SELUI=$(cat out)
if [ "x$SELUI" = xt ]
then
    echo "Setting up keyboard layout..."
    echo "#!/bin/true" > /usr/share/console-setup/keyboard-configuration.config
    chmod +x /usr/share/console-setup/keyboard-configuration.config
    dpkg-reconfigure keyboard-configuration
    service keyboard-setup restart
    setupcon

    echo "Starting restore wizard..."
    ./urbackuprestoreclient --restore-wizard --logfile restore_console_wizard.log --loglevel debug
    echo "Wizard stoped with error code: $?"
    tail -n 100 restore_wizard.txt
    read -p "Press any key to continue..."
else
    cp setxkbmap.sh /home/urbackup/
    chmod +x /home/urbackup/setxkbmap.sh
    chown urbackup  /home/urbackup/setxkbmap.sh
    if [ -e /etc/xdg/lxsession/LXDE/autostart ]
    then
        echo "@/home/urbackup/setxkbmap.sh" >> /etc/xdg/lxsession/LXDE/autostart
    fi
    sed -i 's/allowed_users=console/allowed_users=anybody/g' /etc/X11/Xwrapper.config
    echo "needs_root_rights=yes" >> /etc/X11/Xwrapper.config
    sudo -u urbackup startx
fi


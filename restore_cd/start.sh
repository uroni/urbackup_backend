
#!/bin/sh

export LANG=en_EN.UTF-8
export LC_CTYPE=en_EN.UTF-8
export LANGUAGE=en_EN.UTF-8
cd `dirname $0`
echo "Starting restoration..."
./urbackup_client --no-server --plugin ./libfsimageplugin.so --plugin ./libfileservplugin.so --plugin ./liburbackupclient.so --restore_mode true --logfile restore_mode.txt --loglevel debug > /dev/null 2>&1 &
sleep 1
./urbackup_client --no-server --plugin ./liburbackupclient.so --restore_wizard true --logfile restore_wizard.txt --loglevel debug
echo "Wizard stoped with error code: $?"
cat restore_wizard.txt
read -p "Press any key to continue..."


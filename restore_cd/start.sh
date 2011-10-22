
#!/bin/sh

export LANG=de_DE.UTF-8
export LC_CTYPE=de_DE.UTF-8
export LANGUAGE=de_DE.UTF-8
echo "Starting restoration..."
./cserver --no-server --plugin ./libfsimageplugin.so --plugin ./libfileservplugin.so --plugin urbackup/.libs/liburbackup.so --restore_mode true --logfile restore_mode.txt --loglevel debug > /dev/null 2>&1 &disown
sleep 1
./cserver --no-server --plugin urbackup/.libs/liburbackup.so --restore_wizard true --logfile restore_wizard.txt --loglevel debug
echo "Wizard stoped with error code: $?"
cat restore_wizard.txt
read -p "Press any key to continue..."


#!/usr/bin/env sh
# Run this after each "sudo make install" to verify permissions

USERNAME=urbackup
GROUPNAME=urbackup
PLISTFILE=org.urbackup.server.plist


if [ $EUID -ne 0 ]; then
	echo "This script must be run as root" 
	exit 1
fi

src_dir=$(dirname $0)

# Create urbackup group
GROUPID=$(dscl . -read /Groups/$GROUPNAME PrimaryGroupID 2> /dev/null | awk '{print $2}')
if [ -z "$GROUPID" ]; then
	echo "Could not find group $GROUPNAME. Please run ./preinstall_osx_server.sh before 'make install'."
	exit 1
else
	echo "Found group '$GROUPNAME' with ID $GROUPID"
fi

# Create urbackup user
USERID=$(dscl . -read /Users/$USERNAME UniqueID 2> /dev/null | awk '{print $2}')
if [ -z "$USERID" ]; then
	echo "Could not find user $USERNAME. Please run ./preinstall_osx_server.sh before 'make install'."
	exit 1
else
	echo "Found user '$USERNAME' with ID $USERID"
fi

# Create log folder
if [ ! -d "/var/log/$USERNAME" ]; then
	echo "Creating directory for log files /var/log/$USERNAME" 
	mkdir /var/log/$USERNAME
fi

# Update permissions
echo "Updating permissions..." 
chown $USERNAME:$GROUPNAME /var/log/$USERNAME
chown -R $USERNAME:$GROUPNAME /usr/local/var/urbackup

if [ ! -f /usr/local/var/urbackup/backupfolder ]; then
	echo "Could not find '/usr/local/var/urbackup/backupfolder' file. Please specify your backup folder in this file."
	exit 1
fi

echo "Verifying permissions..." 
BACKUPDIR=$(cat /usr/local/var/urbackup/backupfolder)
parts=$(echo $BACKUPDIR/ | awk 'BEGIN{FS="/"}{for (i=1; i < NF; i++) print $i}')

unset path
for part in $parts
do
	path="$path/$part"
	printf "  Testing access to $path... "
	if ! sudo -u $USERNAME test -d $path ; then
		echo "ERROR!"
		echo "Could not walk into folder '$path' using user '$USERNAME'. Please verify permissions."
		exit 1
	else
		echo "OK"
	fi
done

# Generate unique name
TMPFILE=$BACKUPDIR/test_file.$$

# Try harder if a clash (a bit paranoid)
if [ -f $TMPFILE ]; then
	TMPFILE=$TMPFILE.1
fi

printf "Testing file write permission in '$BACKUPDIR'... "

if ! sudo -u $USERNAME touch "$TMPFILE" ; then
#if ! touch $TMPFILE ; then
	echo "ERROR!"
	echo "No write permission for folder '$BACKUPDIR' using user '$USERNAME'. Please verify permissions."
	exit 1
else
	echo "OK"
	rm $TMPFILE
fi

echo "Verifying Launchd plist file..."

if [ ! -f $src_dir/$PLISTFILE ]; then
	echo "ERROR! Could not find launchd plist file: $src_dir/$PLISTFILE"
	exit 1
fi

if [ -f /Library/LaunchDaemons/$PLISTFILE ]; then
	echo "Launchd plist file found in '/Library/LaunchDaemons/$PLISTFILE'. Keeping it."
else
	echo "Installing a template Launchd plist file in '/Library/LaunchDaemons/$PLISTFILE'..."
	cp "$src_dir/$PLISTFILE" /Library/LaunchDaemons/
fi

echo "All steps done!"
echo
echo "General commands to be done:"
echo "    Start Service:     sudo launchctl load -w /Library/LaunchDaemons/$PLISTFILE"
echo "    Stop Service:      sudo launchctl unload -w /Library/LaunchDaemons/$PLISTFILE"
echo "    Customize Launchd: sudo nano /Library/LaunchDaemons/$PLISTFILE"

#!/usr/bin/env sh
# Run this before the first "sudo make install" to create 'urbackup' user in OS X

USERNAME=urbackup
USERLONGNAME=urbackup.org
GROUPNAME=urbackup
GROUPLONGNAME=urbackup.org


if [ $EUID -ne 0 ]; then
	echo "This script must be run as root" 
	exit 1
fi

getHiddenUserUid () {
    local __UIDS=$(dscl . -list /Users UniqueID | awk '{print $2}' | sort -ugr)

    local __NewUID
    for __NewUID in $__UIDS
    do
        if [[ $__NewUID -lt 499 ]] ; then
            break;
        fi
    done

    echo $((__NewUID+1))
}

getGroupUid () {
	local __UIDS=$(dscl . -list /Groups PrimaryGroupID | awk '{print $2}' | sort -ugr)

	local __NewUID
	for __NewUID in $__UIDS
	do
		if [[ $__NewUID -lt 390 ]] ; then
			break;
		fi
	done

	echo $((__NewUID+1))
}


# Create urbackup group
GROUPID=$(dscl . -read /Groups/$GROUPNAME PrimaryGroupID 2> /dev/null | awk '{print $2}')
if [ -z "$GROUPID" ]; then
	GROUPID=$(getGroupUid)
	echo "Creating group '$GROUPNAME' with ID $GROUPID"
	dseditgroup -i $GROUPID -r "$GROUPLONGNAME" -o create "$GROUPNAME"
else
	echo "Found $GROUPNAME group with ID $GROUPID"
fi

# Create urbackup user
USERID=$(dscl . -read /Users/$USERNAME UniqueID 2> /dev/null | awk '{print $2}')
if [ -z "$USERID" ]; then
	USERID=$(getHiddenUserUid)
	echo "Creating user '$USERNAME' with ID $USERID"
	dscl . -create /Users/$USERNAME UniqueID "$USERID"
	dscl . -append /Users/$USERNAME RealName "$USERLONGNAME"
	dscl . -append /Users/$USERNAME PrimaryGroupID "$GROUPID"
	dscl . -append /Users/$USERNAME NFSHomeDirectory "/usr/local/var/urbackup"
	dscl . -append /Users/$USERNAME UserShell /usr/bin/false
	dscl . -append /Users/$USERNAME IsHidden "1"
	dseditgroup -o edit -t user -a $USERNAME $GROUPNAME
	dseditgroup -o edit -t user -a $USERNAME daemon
else
	echo "Found $USERNAME group with ID $USERID"
fi

# Create log folder
if [ ! -d "/var/log/$USERNAME" ] 
then
	echo "Creating directory for log files /var/log/$USERNAME" 
	mkdir /var/log/$USERNAME
fi

# Update permissions
echo "Updating permissions..." 
chown $USERNAME:$GROUPNAME /var/log/$USERNAME


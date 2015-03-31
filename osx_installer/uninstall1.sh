#!/bin/sh

if /bin/launchctl list "org.urbackup.client.backend" &> /dev/null; then
	/bin/launchctl stop "org.urbackup.client.backend" 
    /bin/launchctl unload "/Library/LaunchDaemons/org.urbackup.client.plist"
fi

if /bin/launchctl list "org.urbackup.client.frontend" &> /dev/null; then
	/bin/launchctl stop "org.urbackup.client.frontend" 
    /bin/launchctl unload "/Library/LaunchAgents/org.urbackup.client.plist"
fi

pkgutil --forget org.urbackup.client
pkgutil --forget org.urbackup.client.frontend2.pkg

sh $PWD/uninstall2.sh
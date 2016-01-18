#!/bin/sh

set -e

#Comment for development ----
git reset --hard
cd client
git reset --hard
cd ..
python3 build/replace_versions.py
#----

rm -R osx-pkg || true
rm -R osx-pkg2 || true

mkdir -p osx-pkg/Library/LaunchDaemons
cp osx_installer/daemon.plist osx-pkg/Library/LaunchDaemons/org.urbackup.client.plist
mkdir -p osx-pkg/Library/LaunchAgents
cp osx_installer/agent.plist osx-pkg/Library/LaunchAgents/org.urbackup.client.plist
./configure --with-crypto-prefix=$PWD/../cryptopp562 CXXFLAGS="-mmacosx-version-min=10.6 -DNDEBUG" CFLAGS="-DNDEBUG" LDFLAGS="-mmacosx-version-min=10.6" --prefix="/Applications/UrBackup Client.app/Contents/MacOS"
make clean
make -j5
make install DESTDIR=$PWD/osx-pkg2
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin"
cp ../cocoasudo/build/Release/cocoasudo "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin/UrBackup Client Administration"
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS"
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources"
cp osx_installer/info.plist "osx-pkg2/Applications/UrBackup Client.app/Contents/Info.plist"
cp osx_installer/urbackup.icns "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources/"
mv "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin/urbackupclientgui" "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/"

mkdir -p "$PWD/osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/sbin"
UNINSTALLER="$PWD/osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/sbin/urbackup_uninstall"

cat osx_installer/uninstall1.sh > "$UNINSTALLER"

cd osx-pkg
find . -type f | cut -d"." -f2-100 | while read line; do echo "rm -fv \"$line\""; done >> "$UNINSTALLER"
cd ..
cd osx-pkg2
find . -type f | cut -d"." -f2-100 | while read line; do echo "rm -fv \"$line\""; done >> "$UNINSTALLER"
cd ..

echo "rm -Rf \"/Applications/UrBackup Client.app\"" >> "$UNINSTALLER"

echo "OK=true" >> $UNINSTALLER

chmod +x "$UNINSTALLER"

VERSION_SHORT_NUM="$version_num_short$"
VERSION_SHORT="$version_short$"

rm -R pkg1 || true
mkdir pkg1 || true
pkgbuild --root osx-pkg --identifier org.urbackup.client.service --version $VERSION_SHORT_NUM --ownership recommended pkg1/output.pkg
pkgbuild --root "osx-pkg2/Applications/UrBackup Client.app" --identifier "org.urbackup.client" --version $VERSION_SHORT_NUM --scripts osx_installer/scripts2 --ownership recommended pkg1/output2.pkg --install-location "/Applications/UrBackup Client.app"
productbuild --distribution osx_installer/distribution.xml --resources osx_installer/resources --package-path pkg1 --version $VERSION_SHORT_NUM final.pkg

cp final.pkg "UrBackup Client $VERSION_SHORT.pkg"

mkdir -p update_installer

cp final.pkg update_installer/final.pkg
cp osx_installer/update_install.sh update_installer/update_install.sh
chmod +x update_installer/update_install.sh
makeself update_installer "UrBackupUpdateMac.sh" "UrBackup Client Installer for Mac OS X" ./update_install.sh

#Uncomment for development
#sudo pkgutil --forget org.urbackup.client.service || true
#sudo pkgutil --forget org.urbackup.client || true
#sudo rm -R "/Applications/UrBackup Client.app" || true
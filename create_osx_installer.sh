#!/bin/sh

set -e

development=false

# Check if using development switch
if [ "$1" != "" ]; then
	if [[ "$1" == "-d" ]] || [[ "$1" == "--development" ]]; then
		development=true
	fi
fi


if !($development); then
	git reset --hard
	cd client
	git reset --hard
	cd ..
	python3 build/replace_versions.py
fi

rm -R osx-pkg || true
rm -R osx-pkg2 || true

./download_cryptopp.sh

mkdir -p osx-pkg/Library/LaunchDaemons
cp osx_installer/daemon.plist osx-pkg/Library/LaunchDaemons/org.urbackup.client.plist
mkdir -p osx-pkg/Library/LaunchAgents
cp osx_installer/agent.plist osx-pkg/Library/LaunchAgents/org.urbackup.client.plist
./configure --enable-embedded-cryptopp --enable-clientupdate CXXFLAGS="-mmacosx-version-min=10.9 -DNDEBUG -DURB_WITH_CLIENTUPDATE" CFLAGS="-DNDEBUG -DURB_WITH_CLIENTUPDATE" LDFLAGS="-mmacosx-version-min=10.9" --prefix="/Applications/UrBackup Client.app/Contents/MacOS" --sysconfdir="/Library/Application Support/UrBackup Client/etc" --localstatedir="/Library/Application Support/UrBackup Client/var"
make clean
make -j5
make install DESTDIR=$PWD/osx-pkg2
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin"
cp ../cocoasudo/build/Release/cocoasudo "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin/UrBackup Client Administration"
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS"
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources"
cp osx_installer/info.plist "osx-pkg2/Applications/UrBackup Client.app/Contents/Info.plist"
cp osx_installer/urbackup.icns "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources/"
cp osx_installer/buildmacOSexclusions "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin/buildmacOSexclusions"
mv "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin/urbackupclientgui" "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/"
strip "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/urbackupclientgui"
strip "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/sbin/urbackupclientbackend"
strip "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin/UrBackup Client Administration"

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

if !($development); then
	VERSION_SHORT_NUM="$version_num_short$"
	VERSION_SHORT="$version_short$"
else
	VERSION_SHORT_NUM="0.1"
	VERSION_SHORT="0.1"
fi

rm -R pkg1 || true
mkdir pkg1 || true
pkgbuild --root osx-pkg --identifier org.urbackup.client.service --version $VERSION_SHORT_NUM --ownership recommended pkg1/output.pkg
pkgbuild --root "osx-pkg2/Applications/UrBackup Client.app" --identifier "org.urbackup.client" --version $VERSION_SHORT_NUM --scripts osx_installer/scripts2 --ownership recommended pkg1/output2.pkg --install-location "/Applications/UrBackup Client.app"
productbuild --distribution osx_installer/distribution.xml --resources osx_installer/resources --package-path pkg1 --version $VERSION_SHORT_NUM final.pkg

if !($development); then
	security unlock-keychain -p foobar /Users/martin/Library/Keychains/dev.keychain
	productsign --keychain /Users/martin/Library/Keychains/dev.keychain --sign 3Y4WACCWC5 final.pkg final-signed.pkg

	cp final-signed.pkg "UrBackup Client $VERSION_SHORT.pkg"

	mkdir -p update_installer

	cp final-signed.pkg update_installer/final.pkg
	cp osx_installer/update_install.sh update_installer/update_install.sh
	chmod +x update_installer/update_install.sh
	makeself --nocomp --nomd5 --nocrc update_installer "UrBackupUpdateMac.sh" "UrBackup Client Installer for Mac OS X" ./update_install.sh
else
	cp final.pkg "UrBackup Client $VERSION_SHORT.pkg"
fi



if ($development); then
	sudo pkgutil --forget org.urbackup.client.service || true
	sudo pkgutil --forget org.urbackup.client || true
	sudo rm -R "/Applications/UrBackup Client.app" || true
fi
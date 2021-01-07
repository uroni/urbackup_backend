#!/usr/bin/env bash

set -e

development=false

# Check if using development switch
if [ "$1" != "" ]; then
	if [[ "$1" == "-d" ]] || [[ "$1" == "--development" ]]; then
		development=true
	fi
fi


if !($development); then
	if [ "x$AC_USERNAME" = x ]; then
		echo "Notarization username not set (AC_USERNAME)"
		exit 1
	fi

	if [ "x$AC_PASSWORD" = x ]; then
		echo "Notarization account password not set (AC_PASSWORD)"
		exit 1
	fi

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
if !($development); then
	./configure --enable-embedded-cryptopp --enable-clientupdate CXXFLAGS="-mmacosx-version-min=10.10 -DNDEBUG -DURB_WITH_CLIENTUPDATE" CFLAGS="-DNDEBUG -DURB_WITH_CLIENTUPDATE" LDFLAGS="-mmacosx-version-min=10.10" --prefix="/Applications/UrBackup Client.app/Contents/MacOS" --sysconfdir="/Library/Application Support/UrBackup Client/etc" --localstatedir="/Library/Application Support/UrBackup Client/var"
else
	./configure --enable-embedded-cryptopp --enable-clientupdate CXXFLAGS="-mmacosx-version-min=10.10 -DDEBUG -DURB_WITH_CLIENTUPDATE -O0 -g" CFLAGS="-DDEBUG -DURB_WITH_CLIENTUPDATE -O0 -g" LDFLAGS="-mmacosx-version-min=10.10" --prefix="/Applications/UrBackup Client.app/Contents/MacOS" --sysconfdir="/Library/Application Support/UrBackup Client/etc" --localstatedir="/Library/Application Support/UrBackup Client/var"
fi
make clean
make -j5
make install DESTDIR=$PWD/osx-pkg2
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin"
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS"
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources"
if !($development); then
	cp osx_installer/info.plist "osx-pkg2/Applications/UrBackup Client.app/Contents/Info.plist"
else
	cp osx_installer/info_development.plist "osx-pkg2/Applications/UrBackup Client.app/Contents/Info.plist"
fi
cp osx_installer/urbackup.icns "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources/"
cp osx_installer/buildmacOSexclusions "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin/buildmacOSexclusions"
mv "osx-pkg2/Library/Application Support" "osx-pkg/Library"
rm -R "osx-pkg2/Library"
mv "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin/urbackupclientgui" "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/"
if !($development); then
	strip "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/urbackupclientgui"
	strip "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/sbin/urbackupclientbackend"
fi

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

echo "OK=true" >> "$UNINSTALLER"

chmod +x "$UNINSTALLER"

if !($development); then
	VERSION_SHORT_NUM="$version_num_short$"
	VERSION_SHORT="$version_short$"
else
	VERSION_SHORT_NUM="0.1"
	VERSION_SHORT="0.1"
fi

function notarization_info {
	echo "$UPLOAD_INFO_PLIST" > tmp.plist
	xcrun altool --notarization-info `/usr/libexec/PlistBuddy -c "Print :notarization-upload:RequestUUID" tmp.plist` -u "$AC_USERNAME" -p "@env:AC_PASSWORD" --output-format xml
}

function wait_for_notarization {
	echo "Waiting for notarization to finish..."
	sleep 30
	while true; do
		REQUEST_INFO_PLIST=$(notarization_info || true)
		echo "$REQUEST_INFO_PLIST" > tmp.plist
		if [ "x$(/usr/libexec/PlistBuddy -c 'Print :product-errors:0:code' tmp.plist)" = x1519 ]; then
			sleep 30
			continue
		fi
		if [ "x$(/usr/libexec/PlistBuddy -c 'Print :notarization-info:Status' tmp.plist)" != "xin progress" ]; then
			echo "Notarization finished"
			break
		fi
		sleep 60
	done

}

function notarize_int {
	xcrun altool --notarize-app --primary-bundle-id "org.urbackup.client.frontend" -u "$AC_USERNAME" -p "@env:AC_PASSWORD" -t osx -f "$1" --output-format xml
}

function notarize {
	echo "Sending $1 to notarization..."
	UPLOAD_INFO_PLIST=$(notarize_int $1)
	echo $UPLOAD_INFO_PLIST
	wait_for_notarization
}

if !($development); then
	echo "Signing code..."
	security unlock-keychain -p foobar /Users/martin/Library/Keychains/dev.keychain
	codesign --deep --keychain dev.keychain --sign 3Y4WACCWC5 --timestamp --options runtime osx-pkg2/Applications/UrBackup\ Client.app
	ditto -c -k --keepParent "osx-pkg2/Applications" "urbackup-client.zip"
	notarize "urbackup-client.zip"
	xcrun stapler staple "osx-pkg2/Applications/UrBackup Client.app"
fi

rm -R pkg1 || true
mkdir pkg1 || true
pkgbuild --root osx-pkg --identifier org.urbackup.client.service --version $VERSION_SHORT_NUM --ownership recommended pkg1/output.pkg
pkgbuild --root "osx-pkg2/Applications/UrBackup Client.app" --identifier "org.urbackup.client" --version $VERSION_SHORT_NUM --scripts osx_installer/scripts2 --ownership recommended pkg1/output2.pkg --install-location "/Applications/UrBackup Client.app"
productbuild --distribution osx_installer/distribution.xml --resources osx_installer/resources --package-path pkg1 --version $VERSION_SHORT_NUM final.pkg

if !($development); then
	productsign --keychain /Users/martin/Library/Keychains/dev.keychain --sign 3Y4WACCWC5 final.pkg final-signed.pkg
	notarize final-signed.pkg
	xcrun stapler staple final-signed.pkg

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

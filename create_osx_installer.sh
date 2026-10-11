#!/usr/bin/env bash

set -e

development=false
already_reset=false

# Check if using development switch
if [ "$1" != "" ]; then
	if [[ "$1" == "-d" ]] || [[ "$1" == "--development" ]]; then
		development=true
	elif [[ "$1" == "--already-reset" ]]; then
		already_reset=true
	fi
fi

if $already_reset
then
	echo "Already reset, skipping git reset"
else
	if !($development); then
		git reset --hard
		cd client
		git reset --hard
		cd ..
		python3 build/replace_versions.py
		./create_osx_installer.sh --already-reset
		exit 0
	fi
fi

rm -R osx-pkg || true
rm -R osx-pkg2 || true
rm -R osx-pkg_x86 || true

if [ "x$VCPKG_ROOT" = "x" ]
then
	echo "Please set VCPKG_ROOT"
	exit 1
fi

CMAKE_INSTALL_ARGS=(-DCMAKE_INSTALL_PREFIX="/Applications/UrBackup Client.app/Contents/MacOS" -DCMAKE_INSTALL_SYSCONFDIR="/Library/Application Support/UrBackup Client/etc" -DCMAKE_INSTALL_LOCALSTATEDIR="/Library/Application Support/UrBackup Client/var")
CMAKE_COMMON_ARGS=(-G Ninja -DURBACKUP_BUILD_SERVER=OFF -DURBACKUP_BUILD_CLIENT=ON -DURBACKUP_CLIENT_UPDATE=ON -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake")

function config() {
	ARCH=$1
	BUILD_DIR=$2
	echo "Configuring for arch $ARCH..."
	WXWIDGETS="$HOME/wxWidgets/dest"
	TRIPLET=arm64-osx-urbackup
	if [ $ARCH = "x86_64" ]
	then
		WXWIDGETS="$HOME/wxWidgets_x86/dest"
		TRIPLET=x64-osx-urbackup
	fi

	if ! [ -e $WXWIDGETS ]
	then
		echo "wxWidgets not found at $WXWIDGETS"
		exit 5
	fi

	rm -R "$BUILD_DIR" || true
	cmake -S . -B "$BUILD_DIR" "${CMAKE_COMMON_ARGS[@]}" "${CMAKE_INSTALL_ARGS[@]}" -DCMAKE_BUILD_TYPE=Release \
		-DVCPKG_TARGET_TRIPLET=$TRIPLET -DCMAKE_OSX_ARCHITECTURES=$ARCH -DCMAKE_OSX_DEPLOYMENT_TARGET=10.10 \
		-DwxWidgets_CONFIG_EXECUTABLE="$WXWIDGETS/bin/wx-config"
}

mkdir -p osx-pkg/Library/LaunchDaemons
cp osx_installer/daemon.plist osx-pkg/Library/LaunchDaemons/org.urbackup.client.plist
mkdir -p osx-pkg/Library/LaunchAgents
cp osx_installer/agent.plist osx-pkg/Library/LaunchAgents/org.urbackup.client.plist
if !($development); then
	config arm64 out/osx/arm64
else
	rm -R out/osx/dev || true
	cmake -S . -B out/osx/dev "${CMAKE_COMMON_ARGS[@]}" "${CMAKE_INSTALL_ARGS[@]}" -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_C_FLAGS="-DDEBUG" -DCMAKE_CXX_FLAGS="-DDEBUG" -DCMAKE_OSX_DEPLOYMENT_TARGET=10.10
fi
if !($development); then
	BUILD_DIR=out/osx/arm64
else
	BUILD_DIR=out/osx/dev
fi
cmake --build $BUILD_DIR -j10
DESTDIR=$PWD/osx-pkg2 cmake --install $BUILD_DIR

if !($development); then
	config x86_64 out/osx/x86_64
	cmake --build out/osx/x86_64 -j10
	DESTDIR=$PWD/osx-pkg_x86 cmake --install out/osx/x86_64
fi

function merge() {
	lipo -create "osx-pkg_x86/Applications/UrBackup Client.app/Contents/MacOS/$1" "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/$1" -output "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/$1.new"
	mv "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/$1.new" "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/$1"
}

for i in $(ls "osx-pkg_x86/Applications/UrBackup Client.app/Contents/MacOS/bin/")
do
	merge bin/$i
done

for i in $(ls "osx-pkg_x86/Applications/UrBackup Client.app/Contents/MacOS/sbin/")
do
	merge sbin/$i
done

mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin"
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS"
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources"

cp osx_installer/info.plist "osx-pkg2/Applications/UrBackup Client.app/Contents/Info.plist"


cp osx_installer/urbackup.icns "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources/"
cp osx_installer/macOS_exclusion_overrides.txt "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources/"
mv "osx-pkg2/Library/Application Support" "osx-pkg/Library"
rm -R "osx-pkg2/Library"
mv "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/bin/urbackupclientgui" "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/"

echo "create_filesystem_snapshot=/Library/Application\ Support/UrBackup\ Client/etc/urbackup/apfs_create_snapshot" > osx-pkg/Library/Application\ Support/UrBackup\ Client/etc/urbackup/snapshot.cfg
echo "remove_filesystem_snapshot=/Library/Application\ Support/UrBackup\ Client/etc/urbackup/apfs_remove_snapshot" >> osx-pkg/Library/Application\ Support/UrBackup\ Client/etc/urbackup/snapshot.cfg

cp linux_snapshot/apfs_create_snapshot osx-pkg/Library/Application\ Support/UrBackup\ Client/etc/urbackup/apfs_create_snapshot
cp linux_snapshot/apfs_remove_snapshot osx-pkg/Library/Application\ Support/UrBackup\ Client/etc/urbackup/apfs_remove_snapshot

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

GIT_REV="$(git rev-parse --short HEAD)"
if [ $? -ne 0 ]; then
	GIT_REV="N/A"
fi

if !($development); then
	VERSION_SHORT_NUM="$version_num_short$ ($GIT_REV)"
	VERSION_SHORT="$version_short$"
else
	VERSION_SHORT_NUM="0.1 ($GIT_REV)"
	VERSION_SHORT="0.1"
fi

if ($development); then
	gsed  -i 's/\$version_num_short\$/0.1/g' "osx-pkg2/Applications/UrBackup Client.app/Contents/Info.plist"
	gsed  -i 's/\$version_maj\$/0/g' "osx-pkg2/Applications/UrBackup Client.app/Contents/Info.plist"
	gsed  -i 's/\$version_min\$/1/g' "osx-pkg2/Applications/UrBackup Client.app/Contents/Info.plist"
fi
gsed  -i 's/\$git_rev\$/'"$GIT_REV"'/g' "osx-pkg2/Applications/UrBackup Client.app/Contents/Info.plist"

function notarize {
	echo "Sending $1 to notarization..."
	xcrun notarytool submit "$1" --keychain-profile "notary-profile" --wait
}

if !($development); then
	echo "Signing code..."
	codesign --deep --sign 3Y4WACCWC5 --timestamp --options runtime osx-pkg2/Applications/UrBackup\ Client.app
	ditto -c -k --keepParent "osx-pkg2/Applications" "urbackup-client.zip"
	notarize "urbackup-client.zip"
	xcrun stapler staple "osx-pkg2/Applications/UrBackup Client.app"
fi

rm -R pkg1 || true
mkdir pkg1 || true
pkgbuild --root osx-pkg --identifier org.urbackup.client.service --version "$VERSION_SHORT_NUM" --ownership recommended pkg1/output.pkg
pkgbuild --root "osx-pkg2/Applications/UrBackup Client.app" --identifier "org.urbackup.client" --version "$VERSION_SHORT_NUM" --scripts osx_installer/scripts2 --ownership recommended pkg1/output2.pkg --install-location "/Applications/UrBackup Client.app"
productbuild --distribution osx_installer/distribution.xml --resources osx_installer/resources --package-path pkg1 --version "$VERSION_SHORT_NUM" final.pkg

if !($development); then
	productsign --sign 3Y4WACCWC5 final.pkg final-signed.pkg
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

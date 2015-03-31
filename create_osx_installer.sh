#!/bin/sh

set -e

#Comment for development ----
git reset --hard
python3 build/replace_versions.py
#----

mkdir -p osx-pkg/Library/LaunchDaemons
cp osx_installer/daemon.plist osx-pkg/Library/LaunchDaemons/org.urbackup.client.plist
mkdir -p osx-pkg/Library/LaunchAgents
cp osx_installer/agent.plist osx-pkg/Library/LaunchAgents/org.urbackup.client.plist
./configure --with-crypto-prefix=$PWD/../cryptopp562 CXXFLAGS="-mmacosx-version-min=10.6" LDFLAGS="-mmacosx-version-min=10.6" --prefix=/usr --disable-fortify
make clean
make -j5
make install DESTDIR=$PWD/osx-pkg
cp ../cocoasudo/build/Release/cocoasudo osx-pkg/usr/bin/urbackup_cocoasudo
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS"
mkdir -p "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources"
cp osx_installer/info.plist "osx-pkg2/Applications/UrBackup Client.app/Contents/Info.plist"
cp osx_installer/urbackup.icns "osx-pkg2/Applications/UrBackup Client.app/Contents/Resources/"
mv osx-pkg/usr/bin/urbackup_client_gui "osx-pkg2/Applications/UrBackup Client.app/Contents/MacOS/"
mkdir pkg1 || true
pkgbuild --root osx-pkg --identifier org.urbackup.client --version 1.5 --scripts osx_installer/scripts --ownership recommended pkg1/output.pkg
pkgbuild --root osx-pkg2/Applications --identifier "org.urbackup.client.frontend2.pkg" --version 1.5 --scripts osx_installer/scripts2 --ownership recommended pkg1/output2.pkg --install-location "/Applications"
productbuild --distribution osx_installer/distribution.xml --resources osx_installer/resources --package-path pkg1 --version 1.5 final.pkg

#Uncomment for development
#sudo pkgutil --forget org.urbackup.client
#sudo pkgutil --forget org.urbackup.client.frontend.pkg
#sudo pkgutil --forget org.urbackup.client.frontend2.pkg
#sudo rm -R "/Applications/UrBackup Client.app"

echo "#!/bin/sh" > uninstall2.sh

cd osx-pkg
find . -type f -exec echo rm -fv /{} \; >> ../uninstall2.sh
cd ..
cd osx-pkg2
find . -type f -exec echo rm -fv /{} \; >> ../uninstall2.sh
cd ..
#!/bin/sh

set -e

# SKIP_PREPARE: build from an already prepared tree (e.g. an extracted source package)
if [ "x$SKIP_PREPARE" = "x" ]
then
	git reset --hard
	python3.3 build/replace_versions.py
fi

wget http://buildserver.urbackup.org/urbackup-debian2.tar.gz -O urbackup-debian.tar.gz
tar xzf urbackup-debian.tar.gz

VERSION=`sed -n 's/^set(URBACKUP_SERVER_VERSION \(.*\))$/\1/p' CMakeLists.txt`
DEBVERSION="${VERSION}-1"

sed -i "0,/urbackup-server \(.*\)(.*)/s//urbackup-server \($VERSION\)\1/" debian/changelog

CMAKE_ARGS="-DURBACKUP_PACKAGING=ON -DURBACKUP_INSTALL_INITD=ON -DURBACKUP_WITH_MOUNTVHD=ON"

if [ "x$VCPKG_ROOT" = "x" ]
then
	echo "VCPKG_ROOT needs to be set (dependencies are built statically via vcpkg)"
	exit 1
fi
CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
CXX_FLAGS="-ggdb -O2"

# The packaging files are shared with the old autotools build. Switch them to CMake.
sed -i "s@^\tdh_auto_configure -- .*@\tdh_auto_configure -- $CMAKE_ARGS -DCMAKE_CXX_FLAGS=\"$CXX_FLAGS\"@" debian/rules
sed -i 's|^\tdh \$@ *$|\tdh $@ --buildsystem=cmake|' debian/rules
sed -i 's@^\(Build-Depends: .*\)$@\1, cmake (>= 3.18), pkg-config@' debian/control

# dh builds with a single job unless parallel=N is set
if [ "x$DEB_BUILD_OPTIONS" = "x" ]
then
	DEB_BUILD_OPTIONS="parallel=$(nproc)"
	export DEB_BUILD_OPTIONS
fi

dh clean
fakeroot dh binary
mkdir output || true
mv ../*.deb output/

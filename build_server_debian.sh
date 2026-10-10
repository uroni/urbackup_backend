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

CMAKE_ARGS="-DURBACKUP_PACKAGING=ON -DURBACKUP_INSTALL_INITD=ON -DURBACKUP_WITH_MOUNTVHD=ON -DURBACKUP_EMBEDDED_ZSTD=ON"
CXX_FLAGS="-D_GLIBCXX_USE_CXX11_ABI=0 -ggdb -O2"

if [ "x$STATIC_CRYPTOPP" != "x" ]
then
	CMAKE_ARGS="$CMAKE_ARGS -DCRYPTOPP_LIBRARY=/usr/local/lib/libcryptopp.a -DCRYPTOPP_INCLUDE_DIR=/usr/local/include"
fi

if [ "x$EMBEDDED_CRYPTOPP" != "x" ]
then
	# Build Crypto++ (and the other dependencies) statically via vcpkg
	if [ "x$VCPKG_ROOT" = "x" ]
	then
		echo "EMBEDDED_CRYPTOPP needs VCPKG_ROOT"
		exit 1
	fi
	CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
	# vcpkg libraries are built with the default libstdc++ ABI
	CXX_FLAGS="-ggdb -O2"
fi

# The packaging files are shared with the old autotools build. Switch them to CMake.
sed -i "s@^\tdh_auto_configure -- .*@\tdh_auto_configure -- $CMAKE_ARGS -DCMAKE_CXX_FLAGS=\"$CXX_FLAGS\"@" debian/rules
sed -i 's|^\tdh \$@ *$|\tdh $@ --buildsystem=cmake|' debian/rules
sed -i 's@^\(Build-Depends: .*\)$@\1, cmake (>= 3.18), pkg-config@' debian/control

dh clean
fakeroot dh binary
mkdir output || true
mv ../*.deb output/

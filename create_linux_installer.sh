#!/bin/bash

set -ex

CDIR=$(pwd)

if [[ $BUILD_SERVER == 1 ]]
then

	git reset --hard
	cd client
	git reset --hard
	cd ..
	python3 build/replace_versions.py
fi

if [ "x$VCPKG_ROOT" = "x" ]
then
	echo "Please set VCPKG_ROOT"
	exit 1
fi

rm -R install-data || true
mkdir -p install-data

rm -R install-data-dbg || true
mkdir -p install-data-dbg

mkdir -p install-data/backup_scripts
cp urbackupclient/backup_scripts/* install-data/backup_scripts/
cp client/version.txt install-data/
cp client/data/urbackup_ecdsa409k1.pub install-data/
cp client/data/updates_h.dat install-data/
cp urbackupclientbackend-debian.service install-data/
cp urbackupclientbackend-redhat.service install-data/
cp init.d_client install-data/
cp init.d_client_rh install-data/
cp defaults_client install-data/
cp linux_snapshot/* install-data/
cp uninstall_urbackupclient install-data/
cp restore_cd/restore_linux_img.sh install-data/
cp restore_cd/restore_linux_root.sh install-data/
chmod +x install-data/*.sh
chmod +x install-data/uninstall_urbackupclient
chmod +x install-data/*_filesystem_snapshot

cp -R install-data/* install-data-dbg/

COMMON_CMAKE_ARGS="-G Ninja -DCMAKE_BUILD_TYPE=None -DURBACKUP_BUILD_SERVER=OFF -DURBACKUP_BUILD_CLIENT=ON -DURBACKUP_CLIENT_HEADLESS=ON -DURBACKUP_CLIENT_UPDATE=ON"

# Fully static musl builds with Zig (zig cc/c++), dependencies from vcpkg. Needs zig and llvm-strip.
build_zig() {
	TARGET_FOLDER=$1
	ARCH_CPPFLAGS=""
	OPT_FLAGS="-O2 -flto"
	EXTRA_CMAKE_ARGS="-DURBACKUP_WITH_C_ARES=ON"
	if [[ $TARGET_FOLDER == "armv6-linux-engeabihf" ]]
	then
		TRIPLET=armv6-linux-zig-urbackup
		ZIG_TARGET=arm-linux-musleabihf
		ZIG_CPU=arm1176jzf_s
		ARCH_CPPFLAGS="-DCRYPTOPP_DISABLE_ASM"
		OPT_FLAGS="-Os"
		EXTRA_CMAKE_ARGS=""
	elif [[ $TARGET_FOLDER == "arm-linux-androideabi" ]]
	then
		TRIPLET=armv7-linux-zig-urbackup
		ZIG_TARGET=arm-linux-musleabihf
		ZIG_CPU=generic+v7a-neon-d32
		ARCH_CPPFLAGS="-DCRYPTOPP_DISABLE_ASM"
	elif [[ $TARGET_FOLDER == "aarch64-linux-android" ]]
	then
		TRIPLET=arm64-linux-zig-urbackup
		ZIG_TARGET=aarch64-linux-musl
		ZIG_CPU=generic+crc
	elif [[ $TARGET_FOLDER == "x86_64-linux-android" ]]
	then
		TRIPLET=x64-linux-zig-urbackup
		ZIG_TARGET=x86_64-linux-musl
		ZIG_CPU=x86_64
	elif [[ $TARGET_FOLDER == "i686-linux-android" ]]
	then
		TRIPLET=x86-linux-zig-urbackup
		ZIG_TARGET=x86-linux-musl
		ZIG_CPU=pentium4
	fi

	STRIP_CMD="$CDIR/cmake/toolchains/zig/zig-strip"

	# -g instead of -ggdb: zig strips debug info when linking with optimization unless -g is passed.
	# blockalign adds -msse4.2 after these flags, so it still gets SSE4.2 on x86
	cmake -S . -B "$BUILD_DIR" $COMMON_CMAKE_ARGS $EXTRA_CMAKE_ARGS \
		-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
		-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$CDIR/cmake/toolchains/zig.cmake" \
		-DVCPKG_TARGET_TRIPLET=$TRIPLET -DZIG_TARGET=$ZIG_TARGET -DZIG_CPU=$ZIG_CPU \
		-DCMAKE_C_FLAGS="-g $OPT_FLAGS $ARCH_CPPFLAGS -ffunction-sections -fdata-sections -DURB_THREAD_STACKSIZE64=8388608 -DURB_THREAD_STACKSIZE32=1048576" \
		-DCMAKE_CXX_FLAGS="-g $OPT_FLAGS $ARCH_CPPFLAGS -ffunction-sections -fdata-sections -DURB_THREAD_STACKSIZE64=8388608 -DURB_THREAD_STACKSIZE32=1048576 -DOPENSSL_SEARCH_CA" \
		-DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections -g $OPT_FLAGS"
}

# The folder names (*-linux-android, armv6-linux-engeabihf) are historical. install_client_linux.sh and
# restore_linux_img.sh select the binaries by them, so they are kept although all non-glibc builds use Zig/musl now.
for arch in armv6-linux-engeabihf x86_64-linux-glibc i686-linux-android  x86_64-linux-android aarch64-linux-android arm-linux-androideabi
do
	echo "Compiling for architecture $arch..."
	BUILD_DIR="out/linux-installer/$arch"
	rm -R "$BUILD_DIR" || true

	if [ $arch = x86_64-linux-glibc ]
	then
		cmake -S . -B "$BUILD_DIR" $COMMON_CMAKE_ARGS \
			-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
			-DVCPKG_TARGET_TRIPLET=x64-linux-urbackup \
			-DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_AR=$(command -v gcc-ar) -DCMAKE_RANLIB=$(command -v gcc-ranlib) \
			-DCMAKE_C_FLAGS="-ggdb -Os -ffunction-sections -fdata-sections -flto -DCRYPTOPP_DISABLE_SSSE3" \
			-DCMAKE_CXX_FLAGS="-ggdb -Os -ffunction-sections -fdata-sections -flto -DCRYPTOPP_DISABLE_SSSE3 -DOPENSSL_SEARCH_CA" \
			-DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections -static-libstdc++ -flto"
		STRIP_CMD="strip"
	else
		build_zig $arch
	fi

	cmake --build "$BUILD_DIR" -j"$(nproc)"

	rm -R install-data/$arch || true
	mkdir -p install-data/$arch
	rm -R install-data-dbg/$arch || true
	mkdir -p install-data-dbg/$arch
	for bin in urbackupclientbackend urbackupclientctl blockalign urbackupclient_dmsnaptool
	do
		if test -e "$BUILD_DIR/$bin"
		then
			cp "$BUILD_DIR/$bin" install-data/$arch/
			cp "$BUILD_DIR/$bin" install-data-dbg/$arch/
			$STRIP_CMD install-data/$arch/$bin
		fi
	done
done

rm -R linux-installer || true
mkdir -p linux-installer

cd install-data
tar czf ../linux-installer/install-data.tar.gz *
cd ..

cp install_client_linux.sh linux-installer/

rm -R linux-installer-dbg || true
mkdir -p linux-installer-dbg

cd install-data-dbg
tar czf ../linux-installer-dbg/install-data.tar.gz *
cd ..

cp install_client_linux.sh linux-installer-dbg/

makeself --nocomp --nomd5 --nocrc linux-installer "UrBackupUpdateLinux.sh" "UrBackup Client Installer for Linux" sh ./install_client_linux.sh
makeself --nocomp --nomd5 --nocrc linux-installer-dbg "UrBackupUpdateLinux-dbg.sh" "UrBackup Client Installer for Linux (debug)" sh ./install_client_linux.sh

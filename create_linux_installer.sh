#!/bin/bash

set -ex

CDIR=$(pwd)

git reset --hard
cd client
git reset --hard
cd ..
python3 build/replace_versions.py

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

COMMON_CMAKE_ARGS="-G Ninja -DCMAKE_BUILD_TYPE=None -DURBACKUP_BUILD_SERVER=OFF -DURBACKUP_BUILD_CLIENT=ON -DURBACKUP_CLIENT_HEADLESS=ON -DURBACKUP_CLIENT_UPDATE=ON -DURBACKUP_EMBEDDED_ZSTD=ON"

build_ndk() {
	TARGET_FOLDER=$1
	NDK_CPUFLAGS=""
	ARCH_CPPFLAGS=""
	if [[ $TARGET_FOLDER == "arm-linux-androideabi" ]]
	then
		ABI=armeabi-v7a
		TRIPLET=arm-android-urbackup
		NDK_CPUFLAGS="-march=armv7-a -mfpu=vfp"
		NDK_LDFLAGS="-Wl,--fix-cortex-a8"
		ARCH_CPPFLAGS="-DCRYPTOPP_DISABLE_ASM"
	elif [[ $TARGET_FOLDER == "aarch64-linux-android" ]]
	then
		ABI=arm64-v8a
		TRIPLET=arm64-android-urbackup
		NDK_CPUFLAGS="-march=armv8-a+crc"
	elif [[ $TARGET_FOLDER == "x86_64-linux-android" ]]
	then
		ABI=x86_64
		TRIPLET=x64-android-urbackup
		NDK_CPUFLAGS="-mno-sse4a -mno-sse4.1 -mno-sse4.2 -mno-popcnt"
	elif [[ $TARGET_FOLDER == "i686-linux-android" ]]
	then
		ABI=x86
		TRIPLET=x86-android-urbackup
		NDK_CPUFLAGS="-mno-sse4a -mno-sse4.1 -mno-sse4.2 -mno-popcnt -mno-sse3"
	fi

	if [[ $TARGET_FOLDER == "x86_64-linux-android" ]] || [[ $TARGET_FOLDER == "i686-linux-android" ]]
	then
		export ANDROID_NDK_HOME=/media/data2/android-ndk/android-ndk-r20
	else
		export ANDROID_NDK_HOME=/media/data2/android-ndk/android-ndk-r20-orig
	fi
	STRIP_CMD=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip
	if [ ! -e $STRIP_CMD ]; then
		STRIP_CMD=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/$TARGET_FOLDER-strip
	fi

	# blockalign adds -msse4.2 after these flags, so it still gets SSE4.2 on x86
	cmake -S . -B "$BUILD_DIR" $COMMON_CMAKE_ARGS -DURBACKUP_WITH_C_ARES=ON \
		-DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
		-DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
		-DVCPKG_TARGET_TRIPLET=$TRIPLET -DANDROID_ABI=$ABI -DANDROID_PLATFORM=29 -DANDROID_STL=c++_static \
		-DCMAKE_C_FLAGS="-ggdb -O2 -flto $NDK_CPUFLAGS $ARCH_CPPFLAGS -ffunction-sections -fdata-sections -DURB_THREAD_STACKSIZE64=8388608 -DURB_THREAD_STACKSIZE32=1048576" \
		-DCMAKE_CXX_FLAGS="-ggdb -O2 -flto $NDK_CPUFLAGS $ARCH_CPPFLAGS -ffunction-sections -fdata-sections -DURB_THREAD_STACKSIZE64=8388608 -DURB_THREAD_STACKSIZE32=1048576 -DCRYPTOPP_INIT_PRIORITY=0 -I$ANDROID_NDK_HOME/sources/android/cpufeatures/ -DOPENSSL_SEARCH_CA" \
		-DCMAKE_EXE_LINKER_FLAGS="-static -Wl,--gc-sections -O2 -flto $NDK_CPUFLAGS $NDK_LDFLAGS"
}

#ELLC: for arch in x86_64-linux-glibc i386-linux-eng x86_64-linux-eng armv6-linux-engeabihf aarch64-linux-eng
for arch in armv6-linux-engeabihf x86_64-linux-glibc i686-linux-android  x86_64-linux-android aarch64-linux-android arm-linux-androideabi
do
	echo "Compiling for architecture $arch..."
	BUILD_DIR="out/linux-installer/$arch"
	NDK_LDFLAGS=""
	rm -R "$BUILD_DIR" || true

	if [ $arch = armv6-linux-engeabihf ]
	then
		# Uses the prebuilt libraries from the ELLCC sysroot instead of vcpkg
		cmake -S . -B "$BUILD_DIR" $COMMON_CMAKE_ARGS \
			-DCMAKE_TOOLCHAIN_FILE="$CDIR/cmake/toolchains/ellcc-armv6.cmake" \
			-DCMAKE_C_FLAGS="-ggdb -Os -ffunction-sections -fdata-sections -DURB_THREAD_STACKSIZE64=8388608 -DURB_THREAD_STACKSIZE32=1048576" \
			-DCMAKE_CXX_FLAGS="-ggdb -Os -ffunction-sections -fdata-sections -DURB_THREAD_STACKSIZE64=8388608 -DURB_THREAD_STACKSIZE32=1048576" \
			-DCMAKE_EXE_LINKER_FLAGS="-Wl,--gc-sections"
		STRIP_CMD="/usr/local/ellcc/bin/ecc-strip"
	elif [ $arch = x86_64-linux-glibc ]
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
		build_ndk $arch
	fi

	cmake --build "$BUILD_DIR" -j4

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

#!/bin/bash

set -ex

CDIR=$(pwd)

git reset --hard
cd client
git reset --hard
cd ..
python3 build/replace_versions.py

./switch_build.sh client
./download_cryptopp.sh

autoreconf --install

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

build_ndk() {
export TARGET_FOLDER=$1
NDK_CPUFLAGS=""
ARCH_CPPFLAGS=""
if [[ $TARGET_FOLDER == "arm-linux-androideabi" ]]
then
        export TARGET=armv7a-linux-androideabi
		NDK_CPUFLAGS="-march=armv7-a -Wl,--fix-cortex-a8 -mfpu=vfp"
		ARCH_CPPFLAGS="-DCRYPTOPP_DISABLE_ASM"
else
        export TARGET=$TARGET_FOLDER
fi
if [[ $TARGET_FOLDER == "aarch64-linux-android" ]]
then
		NDK_CPUFLAGS="-march=armv8-a+crc"
fi


if [[ $TARGET_FOLDER == "x86_64-linux-android" ]] || [[ $TARGET_FOLDER == "i686-linux-android" ]]
then
        NDK_CPUFLAGS="-mno-sse4a -mno-sse4.1 -mno-sse4.2 -mno-popcnt"
	export NDK=/media/data2/android-ndk/android-ndk-r20
else
	export NDK=/media/data2/android-ndk/android-ndk-r20-orig
fi
if [[ $TARGET_FOLDER == "i686-linux-android" ]]
then
	NDK_CPUFLAGS="$NDK_CPUFLAGS -mno-sse3"
fi
export HOST_TAG=linux-x86_64
export TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/$HOST_TAG
export TARGET2=${TARGET}29
export AR=$TOOLCHAIN/bin/$TARGET-ar
if ! [ -e $AR ]; then
        export AR=$TOOLCHAIN/bin/$TARGET_FOLDER-ar
fi
export AS=$TOOLCHAIN/bin/$TARGET-as
export CC=$TOOLCHAIN/bin/$TARGET2-clang
export CXX=$TOOLCHAIN/bin/$TARGET2-clang++
export LD=$TOOLCHAIN/bin/$TARGET-ld
export RANLIB=$TOOLCHAIN/bin/$TARGET-ranlib
export STRIP_CMD=$TOOLCHAIN/bin/$TARGET-strip
if [ ! -e $STRIP_CMD ]; then
	export STRIP_CMD=$TOOLCHAIN/bin/$TARGET_FOLDER-strip
fi
if [ ! -e $RANLIB ]; then
        export RANLIB=$TOOLCHAIN/bin/$TARGET_FOLDER-ranlib
fi
export STRIP=$TOOLCHAIN/bin/$TARGET-strip
#export CRYPTOPP_LIB="-force_load $TOOLCHAIN/sysroot/usr/lib/$TARGET_FOLDER/libcryptopp.a"
#export CRYPTOPP_LIB=""
#CRYPTOPP_LIBS='$(CRYPTOPP_LIBS)'
#./switch_build.sh client
#sed -i "s@$CRYPTOPP_LIBS@$CRYPTOPP_LIB@g" Makefile.am
#CRYPTOPP_LDFLAGS='$(CRYPTOPP_LDFLAGS)'
#CRYPTOPP_LDFLAG="-Wl,--whole-archive $TOOLCHAIN/sysroot/usr/lib/$TARGET_FOLDER/libcryptopp.a -Wl,--no-whole-archive"
#sed -i "s@$CRYPTOPP_LDFLAGS@$CRYPTOPP_LDFLAG@g" Makefile.am
sed -i 's@adhoc.cpp@empty.cpp@' cryptoplugin/src/Makefile.am
touch cryptoplugin/src/empty.cpp
touch cryptoplugin/src/empty.cpp.proto
sed -i 's@CRYPTOPP_INIT_PRIORITY 250@CRYPTOPP_INIT_PRIORITY 0@g' cryptoplugin/src/config.h
if [[ $TARGET_FOLDER == "x86_64-linux-android" ]] || [[ $TARGET_FOLDER == "i686-linux-android" ]]
then
	echo "$CXX" > $CDIR/cxx_path
	export CXX="$CDIR/linux_build_x86_64_cxx_wrapper"
fi
./configure --enable-headless --enable-c-ares --enable-embedded-cryptopp --enable-embedded-zstd LDFLAGS="-static -Wl,--gc-sections -O2 $NDK_CPUFLAGS -flto" --host $TARGET --with-zlib=$TOOLCHAIN/sysroot/usr --with-crypto-prefix=$TOOLCHAIN/sysroot/usr --with-openssl=$TOOLCHAIN/sysroot/usr CPPFLAGS="-DURB_THREAD_STACKSIZE64=8388608 -DURB_THREAD_STACKSIZE32=1048576 -DURB_WITH_CLIENTUPDATE -ffunction-sections -fdata-sections -ggdb -O2 -flto $ARCH_CPPFLAGS" CFLAGS="-ggdb -O2 -flto $NDK_CPUFLAGS" CXXFLAGS="-ggdb -O2 -flto $NDK_CPUFLAGS -I$NDK/sources/android/cpufeatures/ -DOPENSSL_SEARCH_CA" LIBS="-ldl"
}

#ELLC: for arch in x86_64-linux-glibc i386-linux-eng x86_64-linux-eng armv6-linux-engeabihf aarch64-linux-eng
for arch in armv6-linux-engeabihf x86_64-linux-glibc i686-linux-android  x86_64-linux-android aarch64-linux-android arm-linux-androideabi
do
	ORIG_PATH="$PATH"
	echo "Compiling for architecture $arch..."
	
	if [ $arch = armv6-linux-engeabihf ]
	then
		export PATH="$PATH:/usr/local/ellcc/bin"
		./configure --enable-headless --enable-clientupdate --enable-embedded-zstd CFLAGS="-target $arch -ggdb -Os" CPPFLAGS="-target $arch -DURB_THREAD_STACKSIZE64=8388608 -DURB_THREAD_STACKSIZE32=1048576 -DURB_WITH_CLIENTUPDATE -ffunction-sections -fdata-sections" LDFLAGS="-target $arch -Wl,--gc-sections" CXX="ecc++" CC="ecc" CXXFLAGS="-ggdb -Os" --with-crypto-prefix=/usr/local/ellcc/libecc --with-zlib=/usr/local/ellcc/libecc AR=/usr/local/ellcc/libecc/bin/ecc-ar RANLIB=/usr/local/ellcc/libecc/bin/ecc-ranlib
		STRIP_CMD="ecc-strip"
	elif [ $arch = x86_64-linux-glibc ]
	then
		sed -i 's@$(OPENSSL_LIBS)@/usr/lib/x86_64-linux-gnu/libcrypto.a /usr/lib/x86_64-linux-gnu/libssl.a@g' Makefile.am
		sed -i 's@-lzstd@/usr/lib/x86_64-linux-gnu/libzstd.a@g' Makefile.am
		./configure --enable-headless --enable-clientupdate --enable-embedded-cryptopp --enable-embedded-zstd CFLAGS="-ggdb -Os" CPPFLAGS="-DURB_WITH_CLIENTUPDATE -ffunction-sections -fdata-sections -flto -DCRYPTOPP_DISABLE_SSSE3" LDFLAGS="-Wl,--gc-sections -static-libstdc++ -flto" CXX="g++" CC="gcc" CXXFLAGS="-ggdb -Os -DOPENSSL_SEARCH_CA" AR=gcc-ar RANLIB=gcc-ranlib
		STRIP_CMD="strip"
	else
		build_ndk $arch
	fi
	
    make clean
    make -j4
    rm -R install-data/$arch || true
    mkdir -p install-data/$arch
	rm -R install-data-dbg/$arch || true
    mkdir -p install-data-dbg/$arch
    cp urbackupclientbackend install-data/$arch/
	cp urbackupclientbackend install-data-dbg/$arch/
	$STRIP_CMD install-data/$arch/urbackupclientbackend
	cp urbackupclientctl install-data/$arch/
	cp urbackupclientctl install-data-dbg/$arch/
	$STRIP_CMD install-data/$arch/urbackupclientctl
	cp blockalign install-data/$arch/
	cp blockalign install-data-dbg/$arch/
	$STRIP_CMD install-data/$arch/blockalign
	
	export PATH="$ORIG_PATH"
	./switch_build.sh client
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

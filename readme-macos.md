## UrBackup - macOS Build

Please see the website at https://www.urbackup.org for more information, wiki, forums and the issue tracker.

### Note

This version includes an additional script - `buildmacOSexclusions` - which compiles a list
of the standard items which Time Machine excludes from a backup. This list is saved into
`/Library/Application Support/UrBackup Client/var/urbackup/macos_exclusions.txt`.

This list can be edited as required to remove or add further exclusions without polluting the
usual exclusion list inside UrBackup.

To rebuild the list afresh from the Time Machine settings, run:
```bash
sudo ./Applications/UrBackup Client.app/Contents/MacOS/bin/buildmacOSexclusions --force
```


### Building on macOS

Working with macOS 10.12.6:

**Prerequisites**

Install Xcode command line tools:
```bash
xcode-select --install
```

Install Homebrew (see https://brew.sh ):
```bash
/usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

Use Homebrew to install supporting tools:
```bash
brew install wget autoconf automake libtool python3 gnu-sed bash makeself
```

Download and unzip **stable** wxWidgets from https://www.wxwidgets.org/downloads/

`cd` into the wxWidgets folder, then install:
```bash
mkdir build-cocoa-debug
cd build-cocoa-debug
../configure --enable-debug --with-macosx-version-min=10.9 --disable-shared --without-liblzma
sudo make install
```

**Source Installation**

Assuming building in a folder ~/Urbackup:

Download the backend source:
```bash
mkdir ~/UrBackup
cd ~/UrBackup
git clone https://github.com/uroni/urbackup_backend
cd urbackup_backend
git checkout dev
```

Download the client source:
```bash
cd ~/UrBackup/urbackup_backend
git clone https://github.com/uroni/urbackup_frontend_wx.git client
cd client
git checkout dev
```

Switch to building the client:
```bash
cd ~/UrBackup/urbackup_backend
./switch_build_mac.sh client
autoreconf -fvi
```

And then build it!
```bash
cd ~/UrBackup/urbackup_backend
./create_osx_installer.sh
```

For development, use the `-d` or `--development` switches
```bash
cd ~/UrBackup/urbackup_backend
./create_osx_installer.sh -d
```


[![Build Status](https://travis-ci.org/uroni/urbackup_backend.svg?branch=dev)](https://travis-ci.org/uroni/urbackup_backend)

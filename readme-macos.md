## UrBackup - macOS Build

Please see the website at https://www.urbackup.org for more information, wiki, forums and the issue tracker.


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
brew install wget autoconf automake libtool python3
```

Download and unzip **stable** wxWidgets from https://www.wxwidgets.org/downloads/

`cd` into the wxWidgets folder, then install:
```bash
mkdir build-cocoa-debug
cd build-cocoa-debug
../configure --enable-debug --with-macosx-version-min=10.9
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

Download cocoasudo:
```bash
cd ~/UrBackup
git clone https://github.com/performantdesign/cocoasudo.git
cd cocoasudo
git checkout master
```

Switch to building the client:
```bash
cd ~/UrBackup/urbackup_backend
./switch_build.sh client
autoreconf -fvi
```

For development, there are some changes that need to be manually applied:

In `create_osx_installer.sh`:
- Comment out lines 6-10, 64-65 and 75;
- Replace lines 55-56 with this:
```bash
VERSION_SHORT_NUM="0.1"
VERSION_SHORT="0.1"
```
In `osx_installer/info.plist`:
- Replace lines 16, 18, 24 and 26 respectively with these:
```bash
<string>0.1</string>

<string>0.1</string>

<integer>0</integer>

<integer>1</integer>
```



And then build it!
```bash
cd ~/UrBackup/urbackup_backend
./create_osx_installer.sh
```


[![Build Status](https://travis-ci.org/uroni/urbackup_backend.svg?branch=dev)](https://travis-ci.org/uroni/urbackup_backend)

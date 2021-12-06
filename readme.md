## UrBackup

Please see the website at https://www.urbackup.org for more information, wiki, forums and the issue tracker.

### Building on Linux

Use

```bash
./switch_build.sh server
```

to switch to a server build and

```bash
./switch_build.sh client
git clone https://github.com/uroni/urbackup_frontend_wx client
cd client && git checkout BRANCH
```
  
to switch to building a client.

Afterwards build the client/server using

```bash
autoreconf --install
./configure
make -j8
```


### Building on macOS

See the separate `readme-macos.md` for building instructions for macOS.


### Building on Windows

Build with Visual Studio 2019:

 * Install [vcpkg](https://vcpkg.io/en/index.html) and run `vcpkg integrate install`
 * Set global environment variable `VCPKG_FEATURE_FLAGS` to `manifests`
 * Open `UrBackupBackend.sln` with Visual Studio 2019 and build (and run)

`build_client.bat` and `build_server.bat` build the installers but you need
to install a lot of dependencies like WiX, NSIS plus plugins, etc.


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

If git is in `PATH` you can download all dependencies by running `update_deps.bat`.

Afterwards opening and compiling the solution `UrBackupBackend.sln` with
Microsoft Visual Studio 2015 should work.

`build_client.bat` and `build_server.bat` build the installers but you need
to install a lot of dependencies like WiX, NSIS plus plugins, etc.

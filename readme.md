## UrBackup

Please see the website at https://www.urbackup.org for more information, wiki, forums and the issue tracker.

### Building on Linux

UrBackup is built with CMake. Dependencies are installed via [vcpkg](https://vcpkg.io)
and statically linked. Set `VCPKG_ROOT` to a vcpkg checkout (a full or `--filter=blob:none`
clone, not a shallow one, since `vcpkg.json` pins versions). Some vcpkg ports need host tools,
on Debian/Ubuntu: `cmake ninja-build pkg-config autoconf autoconf-archive automake libtool bison`
(plus `libfuse-dev` for `URBACKUP_WITH_MOUNTVHD` and `libwxgtk3.2-dev` for the client GUI, which uses the system wxWidgets):

```bash
cmake --preset server
cmake --build --preset server
```

Alternatively, the `*-system` presets (or `-DURBACKUP_USE_SYSTEM_LIBS=ON`) use the system libraries,
dynamically linked. vcpkg (`VCPKG_ROOT`) is still needed for the libraries distributions do not package
(miniz). On Debian/Ubuntu install `cmake ninja-build pkg-config libcrypto++-dev libcurl4-openssl-dev
libzstd-dev zlib1g-dev liblua5.4-dev libtclap-dev libutfcpp-dev` (plus `libjsoncpp-dev libssl-dev`
for the client):

```bash
cmake --preset server-system
cmake --build --preset server-system
```

To build the client, clone the GUI frontend first

```bash
git clone https://github.com/uroni/urbackup_frontend_wx client
cd client && git checkout BRANCH
```

and use the `client` (with GUI, needs wxWidgets and its `wx-config`) or `client-headless` presets
(`client-system`/`client-headless-system` with system libraries).

The client backend for Android (used by the Android app) is built with the `client-android-arm64`
and `client-android-x64` presets. They need the Android NDK (r27 or newer) in `ANDROID_NDK_HOME`
and target Android 11 (API level 30).

Binaries end up in `out/build/<preset>`. Install with
`cmake --install out/build/<preset> --prefix /usr/local`, create a source tarball
with `cmake --build --preset <preset> --target package_source`.

Build options (pass as `-D<option>=ON/OFF` when configuring):

| Option | Default | Description |
|---|---|---|
| `URBACKUP_BUILD_SERVER` / `URBACKUP_BUILD_CLIENT` | `ON` / `OFF` | Which component to build |
| `URBACKUP_CLIENT_HEADLESS` | `OFF` | Do not build the client GUI |
| `URBACKUP_GUI_FROM_VCPKG` | `OFF` | Build wxWidgets via vcpkg instead of using the system `wx-config` |
| `URBACKUP_USE_SYSTEM_LIBS` | `OFF` | Use system libraries where distributions package them, vcpkg for the rest (set by the `*-system` presets) |
| `URBACKUP_WITH_MOUNTVHD` | `OFF` | Mount VHD files via fuse (server, needs libfuse-dev) |
| `URBACKUP_WITH_MAIL` | `ON` | Mail features (server, needs libcurl) |
| `URBACKUP_WITH_CRYPTO` | `ON` | Internet mode (server, needs Crypto++) |
| `URBACKUP_CLIENT_UPDATE` | `OFF` | Internal client update functionality |
| `URBACKUP_WITH_C_ARES` | `OFF` | Use c-ares for DNS resolution (client) |
| `URBACKUP_WITH_OPENSSL` | `ON` | Use OpenSSL if found (client web socket/https proxy) |
| `URBACKUP_EMBEDDED_SQLITE3`, `_LMDB` | `ON` | Use the embedded copies instead of system libraries |
| `URBACKUP_INSTALL_INITD` | `OFF` | Install init.d/default/logrotate files (server) |
| `URBACKUP_INSTALL_DEBIAN_INITD` / `_REDHAT_INITD` | `OFF` | Install init.d files (client) |
| `URBACKUP_PACKAGING` | `OFF` | Install for packaging (no user creation/chown on install) |
| `URBACKUP_ENABLE_ASSERTIONS` | `OFF` | Enable assertions |
| `URBACKUP_ENABLE_FORTIFY` | `ON` | GCC/runtime fortification |


### Building on macOS

See the separate `readme-macos.md` for building instructions for macOS.


### Building on Windows

Build with Visual Studio 2019:

 * Install [vcpkg](https://vcpkg.io/en/index.html) and run `vcpkg integrate install`
 * Set global environment variable `VCPKG_FEATURE_FLAGS` to `manifests`
 * Open `UrBackupBackend.sln` with Visual Studio 2019 and build (and run)

`build_client.bat` and `build_server.bat` build the installers but you need
to install a lot of dependencies like WiX, NSIS plus plugins, etc.


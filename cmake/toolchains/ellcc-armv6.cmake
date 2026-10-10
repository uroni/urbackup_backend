# Cross compile the client for armv6 (e.g. Raspberry Pi 1) with ELLCC (create_linux_installer.sh).
# Dependencies (Crypto++, zlib) are taken from the prebuilt libraries in the ELLCC sysroot.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR armv6)

set(ELLCC_ROOT /usr/local/ellcc CACHE PATH "ELLCC installation")
set(ELLCC_TARGET armv6-linux-engeabihf)

set(CMAKE_C_COMPILER ${ELLCC_ROOT}/bin/ecc)
set(CMAKE_CXX_COMPILER ${ELLCC_ROOT}/bin/ecc++)
set(CMAKE_AR ${ELLCC_ROOT}/libecc/bin/ecc-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${ELLCC_ROOT}/libecc/bin/ecc-ranlib CACHE FILEPATH "")
set(CMAKE_STRIP ${ELLCC_ROOT}/bin/ecc-strip CACHE FILEPATH "")
set(CMAKE_C_COMPILER_TARGET ${ELLCC_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${ELLCC_TARGET})

set(CMAKE_FIND_ROOT_PATH ${ELLCC_ROOT}/libecc)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Cross compile fully static binaries with Zig (zig cc/zig c++: clang + libc++ + musl).
#
# Set ZIG_TARGET to a Zig target triple (e.g. arm-linux-musleabihf, x86_64-linux-musl)
# and optionally ZIG_CPU to a Zig CPU name (e.g. arm1176jzf_s for armv6/Raspberry Pi 1).
# Both can also be set as environment variables. Zig has to be in PATH, or set ZIG to its path.
#
# With vcpkg, use this as VCPKG_CHAINLOAD_TOOLCHAIN_FILE (see cmake/vcpkg-triplets/*-zig-urbackup.cmake).

if(NOT ZIG_TARGET AND DEFINED ENV{ZIG_TARGET})
	set(ZIG_TARGET $ENV{ZIG_TARGET})
endif()
if(NOT ZIG_CPU AND DEFINED ENV{ZIG_CPU})
	set(ZIG_CPU $ENV{ZIG_CPU})
endif()
if(NOT ZIG_TARGET)
	message(FATAL_ERROR "Set ZIG_TARGET (e.g. -DZIG_TARGET=arm-linux-musleabihf)")
endif()
set(ZIG_TARGET ${ZIG_TARGET} CACHE STRING "Zig target triple")
set(ZIG_CPU ${ZIG_CPU} CACHE STRING "Zig CPU name (-mcpu)")
# try_compile() projects re-read this file without the cache
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES ZIG_TARGET ZIG_CPU)

string(REGEX MATCH "^[^-]+" _zig_arch "${ZIG_TARGET}")
if(_zig_arch STREQUAL "x86")
	set(_zig_arch i686)
endif()
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR ${_zig_arch})

set(_zig_wrappers ${CMAKE_CURRENT_LIST_DIR}/zig)
set(CMAKE_C_COMPILER ${_zig_wrappers}/zig-cc)
set(CMAKE_CXX_COMPILER ${_zig_wrappers}/zig-c++)
set(CMAKE_ASM_COMPILER ${_zig_wrappers}/zig-cc)
set(CMAKE_AR ${_zig_wrappers}/zig-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${_zig_wrappers}/zig-ranlib CACHE FILEPATH "")
set(CMAKE_STRIP ${_zig_wrappers}/zig-strip CACHE FILEPATH "")

# Passed as --target=... (also forwarded to make based vcpkg ports like OpenSSL)
set(CMAKE_C_COMPILER_TARGET ${ZIG_TARGET})
set(CMAKE_CXX_COMPILER_TARGET ${ZIG_TARGET})
set(CMAKE_ASM_COMPILER_TARGET ${ZIG_TARGET})

# zig cc enables trapping UBSan when building without optimization
set(_zig_flags "-fno-sanitize=undefined")
if(ZIG_CPU)
	string(APPEND _zig_flags " -mcpu=${ZIG_CPU}")
endif()

set(_zig_C_flags "${_zig_flags}")
# libc++ removed std::auto_ptr (C++17), which is still used in the sources
set(_zig_CXX_flags "${_zig_flags} -D_LIBCPP_ENABLE_CXX17_REMOVED_AUTO_PTR -D_LIBCPP_DISABLE_DEPRECATION_WARNINGS")
set(_zig_ASM_flags "${_zig_flags}")
set(_zig_EXE_LINKER_flags "-static")

# vcpkg only applies the triplet flags itself with its own toolchains
set(_zig_extra_C_flags "${VCPKG_C_FLAGS}")
set(_zig_extra_CXX_flags "${VCPKG_CXX_FLAGS}")
set(_zig_extra_EXE_LINKER_flags "${VCPKG_LINKER_FLAGS}")

foreach(_zig_kind C CXX ASM EXE_LINKER)
	if(DEFINED CACHE{CMAKE_${_zig_kind}_FLAGS})
		# CMake ignores *_INIT if the flags were passed on the command line (create_linux_installer.sh),
		# but the target CPU is required. Prepend once (the toolchain file is read multiple times).
		string(FIND " ${CMAKE_${_zig_kind}_FLAGS} " " ${_zig_${_zig_kind}_flags} " _zig_pos)
		if(_zig_pos EQUAL -1)
			set_property(CACHE CMAKE_${_zig_kind}_FLAGS PROPERTY VALUE
				"${_zig_${_zig_kind}_flags} ${CMAKE_${_zig_kind}_FLAGS}")
		endif()
	else()
		set(CMAKE_${_zig_kind}_FLAGS_INIT "${_zig_${_zig_kind}_flags} ${_zig_extra_${_zig_kind}_flags}")
	endif()
endforeach()

# Nothing from the host system may be found, only the vcpkg installed tree
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

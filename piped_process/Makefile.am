lib_LTLIBRARIES = libpiped_process.la
bin_PROGRAMS = getpid_urbackup
getpid_urbackup_SOURCES = getpid.cpp
libpiped_process_la_SOURCES = dllmain.cpp piped_process.cpp PipedProcessFactory.cpp piped_process_lin.cpp pluginmgr.cpp ../stringtools.cpp
noinst_HEADERS = IPipedProcessFactory.h IPipedProcess.h PipedProcessFactory.h piped_process.h pluginmgr.h
AM_CXXFLAGS = -DLINUX
if WITH_FORTIFY
	AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
endif
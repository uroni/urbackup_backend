lib_LTLIBRARIES = libpiped_process.la
bin_PROGRAMS = getpid
getpid_SOURCES = getpid.cpp
libpiped_process_la_SOURCES = dllmain.cpp piped_process.cpp PipedProcessFactory.cpp piped_process_lin.cpp pluginmgr.cpp ../stringtools.cpp
noinst_HEADERS = IPipedProcessFactory.h IPipedProcess.h PipedProcessFactory.h piped_process.h pluginmgr.h
AM_CXXFLAGS = -DLINUX
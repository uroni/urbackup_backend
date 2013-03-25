ACLOCAL_AMFLAGS =
bin_PROGRAMS = urbackup_snapshot_helper
urbackup_snapshot_helper_SOURCES = main.cpp ../urbackupcommon/os_functions_lin_min.cpp ../stringtools.cpp
urbackup_snapshot_helper_LDADD = 
AM_CXXFLAGS = -DOS_FUNC_NO_NET -DOS_FUNC_NO_IFACE_SERVER
AM_CFLAGS = 
AM_LDFLAGS = 
noinst_HEADERS=../urbackupcommon/os_functions.h ../stringtools.h
install-exec-hook:
	chmod +s $(DESTDIR)$(bindir)/urbackup_snapshot_helper


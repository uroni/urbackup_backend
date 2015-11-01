ACLOCAL_AMFLAGS =
bin_PROGRAMS = urbackupclientctl
urbackup_snapshot_helper_SOURCES = main.cpp ../urbackupcommon/os_functions_lin.cpp ../stringtools.cpp Connector.cpp tcpstack.cpp
urbackup_snapshot_helper_LDADD = 
AM_CXXFLAGS = -DOS_FUNC_NO_NET -DOS_FUNC_NO_SERVER
AM_CFLAGS = $(SUID_CFLAGS)
AM_LDFLAGS = $(SUID_LDFLAGS)
if WITH_FORTIFY
AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
endif
noinst_HEADERS=../urbackupcommon/os_functions.h ../stringtools.h Connector.h tcpstack.h
install-exec-hook:
	chmod +s $(DESTDIR)$(bindir)/urbackup_snapshot_helper


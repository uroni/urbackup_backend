ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = liburbackupserver_fuseplugin.la
liburbackupserver_fuseplugin_la_SOURCES = dllmain.cpp ../stringtools.cpp
liburbackupserver_fuseplugin_la_LIBADD = $(FUSE_LIBS)
noinst_HEADERS = ../stringtools.h ../fsimageplugin/IFSImageFactory.h ../fsimageplugin/IVHDFile.h
if WITH_FORTIFY
AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
endif
AM_CXXFLAGS = -DLINUX $(FUSE_CFLAGS)
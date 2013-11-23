ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = liburbackupserver_downloadplugin.la
liburbackupserver_downloadplugin_la_SOURCES = dllmain.cpp ../stringtools.cpp pluginmgr.cpp download2.cpp DownloadFactory.cpp DownloadThread.cpp ../common/data.cpp
noinst_HEADERS = DownloadFactory.h DownloadThread.h IFileDownload.h download2.h socket_header.h ../common/data.h IDownloadFactory.h pluginmgr.h
AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
AM_CXXFLAGS = -DLINUX
AM_LDFLAGS = $(LIBS)
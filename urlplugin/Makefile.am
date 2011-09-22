ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = liburbackupserver_urlplugin.la
liburbackupserver_urlplugin_la_SOURCES = dllmain.cpp ../stringtools.cpp pluginmgr.cpp UrlFactory.cpp
noinst_HEADERS = IUrlFactory.h pluginmgr.h UrlFactory.h
AM_CXXFLAGS = -DLINUX $(LIBCURL_CPPFLAGS)
AM_LDFLAGS = $(LIBCURL)
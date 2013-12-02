ACLOCAL_AMFLAGS = -I m4
lib_LTLIBRARIES = liburbackupserver_urlplugin.la
liburbackupserver_urlplugin_la_SOURCES = dllmain.cpp ../stringtools.cpp pluginmgr.cpp UrlFactory.cpp
liburbackupserver_urlplugin_la_LIBADD = $(LIBCURL)
noinst_HEADERS = IUrlFactory.h pluginmgr.h UrlFactory.h
AM_CXXFLAGS = -DLINUX $(LIBCURL_CPPFLAGS)
AM_LDFLAGS = 
if WITH_FORTIFY
	AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
endif
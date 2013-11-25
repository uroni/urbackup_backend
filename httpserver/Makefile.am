lib_LTLIBRARIES = liburbackupserver_httpserver.la
liburbackupserver_httpserver_la_SOURCES = dllmain.cpp ../stringtools.cpp IndexFiles.cpp HTTPAction.cpp HTTPFile.cpp HTTPService.cpp HTTPClient.cpp HTTPProxy.cpp MIMEType.cpp
noinst_HEADERS = HTTPAction.h HTTPClient.h HTTPFile.h HTTPProxy.h HTTPService.h IndexFiles.h MIMEType.h
AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
AM_CXXFLAGS = -DLINUX
lib_LTLIBRARIES = liburbackupserver_pychart.la
liburbackupserver_pychart_la_SOURCES = dllmain.cpp pluginmgr.cpp Pychart.cpp PychartFactory.cpp test.cpp ../stringtools.cpp
liburbackupserver_pychart_la_LIBADD = $(BOOST_LDFLAGS) $(BOOST_PYTHON_LDFLAGS) -l$(PYTHON_LIB) -lboost_python
noinst_HEADERS = IPychartFactory.h IPychart.h pluginmgr.h PychartFactory.h Pychart.h
ACLOCAL_AMFLAGS=-I m4
AM_CXXFLAGS = -DLINUX $(BOOST_CPPFLAGS) -I$(PYTHON_INCLUDE_DIR)
if WITH_FORTIFY
AM_CPPFLAGS = -g -O2 -fstack-protector --param=ssp-buffer-size=4 -Wformat -Werror=format-security -D_FORTIFY_SOURCE=2
endif
AM_LDFLAGS = 
install-data-local: $(srcdir)/pychart.py
	$(INSTALL_DATA) -D $(srcdir)/pychart.py $(DESTDIR)$(localstatedir)/pychart/pychart.py
EXTRA_DIST=pychart.py	
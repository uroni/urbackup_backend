lib_LTLIBRARIES = liburbackupserver_pychart.la
liburbackupserver_pychart_la_SOURCES = dllmain.cpp pluginmgr.cpp Pychart.cpp PychartFactory.cpp test.cpp ../stringtools.cpp
noinst_HEADERS = IPychartFactory.h IPychart.h pluginmgr.h PychartFactory.h Pychart.h
ACLOCAL_AMFLAGS=-I m4
AM_CXXFLAGS = -DLINUX $(BOOST_CPPFLAGS) -I$(PYTHON_INCLUDE_DIR)
AM_LDFLAGS = $(BOOST_LDFLAGS) $(BOOST_PYTHON_LDFLAGS) -lpython2.6 -lboost_python
install-data-local: $(srcdir)/pychart.py
	$(INSTALL_DATA) -D $(srcdir)/pychart.py $(DESTDIR)$(localstatedir)/pychart/pychart.py
EXTRA_DIST=pychart.py	